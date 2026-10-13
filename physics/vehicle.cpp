#include "vehicle.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "btconvert.h"

using namespace rc;
using namespace rc::bt;

// Suspension rays go through the COLL grid instead of Bullet's broadphase, and hit the props on the track
// (a wheel rolls over a can, a bucket ...), which then feel the wheel's friction.
class Vehicle::Raycaster : public btVehicleRaycaster {
  public:
    Raycaster(PhysicsWorld& w, bool& onProp) : world(w), onProp(onProp) {}

    void* castRay(const btVector3& from, const btVector3& to, btVehicleRaycasterResult& result) override {
      const Vec3 f = fromBt(from)*(1.f/S);
      const Vec3 d = fromBt(to-from)*(1.f/S);
      auto  hit  = world.track().raycast(f,d,1.f);
      float dist = hit ? hit->distance : 1.f;
      if(auto prop = world.rayProps(f,f+d); prop.body!=nullptr && prop.fraction<dist) {
        result.m_hitPointInWorld  = toBt(prop.point)*S;
        result.m_hitNormalInWorld = toBt(prop.normal);
        result.m_distFraction     = prop.fraction;
        onProp = true;
        return prop.body;
        }
      if(!hit)
        return nullptr;
      result.m_hitPointInWorld  = toBt(hit->point)*S;
      result.m_hitNormalInWorld = toBt(hit->normal);
      result.m_distFraction     = hit->distance;
      return &world.ground();
      }

  private:
    PhysicsWorld& world;
    bool&         onProp;
  };

PhysicsWorld::PhysicsWorld(const CollisionMesh& track) : coll(track) {
  config     = std::make_unique<btDefaultCollisionConfiguration>();
  dispatcher = std::make_unique<btCollisionDispatcher>(config.get());
  broadphase = std::make_unique<btDbvtBroadphase>();
  solver     = std::make_unique<btSequentialImpulseConstraintSolver>();
  world      = std::make_unique<btDiscreteDynamicsWorld>(dispatcher.get(),broadphase.get(),solver.get(),config.get());
  world->setGravity(btVector3(0,-Gravity*S,0));

  // static track: triangle soup of the COLL faces, without water
  size_t solid = 0;
  for(auto& t:track.triangles()) {
    if(t.surface==Surface::Water)
      continue;
    ++solid;
    solidTris.push_back(uint32_t(&t-track.triangles().data()));
    for(const Vec3* p:{&t.a,&t.b,&t.c}) {
      indices.push_back(int(vertices.size()/3));
      vertices.push_back(p->x*S);
      vertices.push_back(p->y*S);
      vertices.push_back(p->z*S);
      }
    }
  btCollisionShape* shape = nullptr;
  if(solid>0) {
    meshData  = std::make_unique<btTriangleIndexVertexArray>(int(solid),indices.data(),3*int(sizeof(int)),
                                                             int(vertices.size()/3),vertices.data(),3*int(sizeof(float)));
    meshShape = std::make_unique<btBvhTriangleMeshShape>(meshData.get(),true);
    shape     = meshShape.get();
    }
  static btEmptyShape empty;
  btRigidBody::btRigidBodyConstructionInfo ci(0,nullptr,shape ? shape : &empty);
  ci.m_friction    = 0.6f;
  ci.m_restitution = 0.1f;
  groundBody = std::make_unique<btRigidBody>(ci);
  world->addRigidBody(groundBody.get());
  }

PhysicsWorld::~PhysicsWorld() {
  // vehicles remove themselves; only the ground is left
  world->removeRigidBody(groundBody.get());
  }

uint8_t PhysicsWorld::groundSound(int triangle) const {
  if(triangle<0 || size_t(triangle)>=solidTris.size())
    return SoundSurface::None;
  return coll.triangles()[solidTris[size_t(triangle)]].sound;
  }

PhysicsWorld::PropHit PhysicsWorld::rayProps(const Vec3& f, const Vec3& t) {
  struct Callback : btCollisionWorld::ClosestRayResultCallback {
    using ClosestRayResultCallback::ClosestRayResultCallback;
    bool needsCollision(btBroadphaseProxy* proxy) const override {
      auto obj = static_cast<const btCollisionObject*>(proxy->m_clientObject);
      return obj->getUserIndex()==PropTag && ClosestRayResultCallback::needsCollision(proxy);
      }
    };
  const btVector3 from = toBt(f)*S, to = toBt(t)*S;
  PropHit  ret;
  Callback cb(from,to);
  world->rayTest(from,to,cb);
  if(cb.hasHit()) {
    ret.body     = const_cast<btRigidBody*>(btRigidBody::upcast(cb.m_collisionObject));
    ret.point    = fromBt(cb.m_hitPointWorld)*(1.f/S);
    ret.normal   = fromBt(cb.m_hitNormalWorld);
    ret.fraction = float(cb.m_closestHitFraction);
    }
  return ret;
  }

void PhysicsWorld::step(float dt) {
  world->stepSimulation(dt,8,FixedDt);
  }

Vehicle::Vehicle(PhysicsWorld& w, const CarGeometry& g, const CarParams& p, const Mat4& start)
  : owner(w), geom(g), prm(p) {
  // chassis box: the body's outline from the wheel hubs up (below the hubs only the wheels touch the ground)
  float hubY = geom.bodyMin.y;
  float wheelZ[2] = {1e9f,-1e9f};
  for(auto& wh:geom.wheels) {
    hubY      = std::max(hubY,wh.center.y);
    wheelZ[0] = std::min(wheelZ[0],wh.center.z);
    wheelZ[1] = std::max(wheelZ[1],wh.center.z);
    }
  Vec3 bmin = geom.bodyMin, bmax = geom.bodyMax;
  bmin.y = std::min(hubY,bmax.y-0.02f);
  const Vec3 boxCenter = (bmin+bmax)*0.5f;
  const Vec3 half      = (bmax-bmin)*0.5f;

  com   = boxCenter;
  com.y = geom.min.y + (geom.max.y-geom.min.y)*prm.comHeight;
  if(wheelZ[0]<=wheelZ[1])
    com.z = (wheelZ[0]+wheelZ[1])*0.5f;

  box   = std::make_unique<btBoxShape>(toBt(half)*S);
  shape = std::make_unique<btCompoundShape>();
  btTransform local;
  local.setIdentity();
  local.setOrigin(toBt(boxCenter-com)*S);
  shape->addChildShape(local,box.get());

  btVector3 inertia;
  shape->calculateLocalInertia(prm.mass,inertia);
  inertia.setX(inertia.x()*prm.inertiaPitch);
  inertia.setZ(inertia.z()*prm.inertiaRoll);

  motion = std::make_unique<btDefaultMotionState>();
  btRigidBody::btRigidBodyConstructionInfo ci(prm.mass,motion.get(),shape.get(),inertia);
  ci.m_friction    = 0.3f;
  ci.m_restitution = 0.1f;
  ci.m_linearDamping  = 0.f;
  ci.m_angularDamping = 0.05f;
  body = std::make_unique<btRigidBody>(ci);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setUserIndex(PhysicsWorld::CarTag);
  // fast and small: sweep a sphere inside the box against thin walls
  body->setCcdMotionThreshold(std::min({half.x,half.y,half.z})*S*0.5f);
  body->setCcdSweptSphereRadius(std::min({half.x,half.y,half.z})*S*0.8f);
  owner.bullet().addRigidBody(body.get());

  raycaster = std::make_unique<Raycaster>(owner,wheelProp);
  btRaycastVehicle::btVehicleTuning tuning;
  vehicle = std::make_unique<btRaycastVehicle>(tuning,body.get(),raycaster.get());
  vehicle->setCoordinateSystem(0,1,2);
  owner.bullet().addAction(vehicle.get());

  const btVector3 down(0,-1,0), axle(-1,0,0); // model +X is left, so the axle points right
  for(auto& wh:geom.wheels) {
    // hub at its modelled position when the spring is at rest length
    const Vec3 conn = wh.center - com + Vec3{0,prm.suspRest,0};
    tuning.m_suspensionStiffness   = prm.suspStiffness;
    tuning.m_suspensionCompression = prm.suspCompression;
    tuning.m_suspensionDamping     = prm.suspDamping;
    tuning.m_maxSuspensionTravelCm = prm.suspTravel*S*100.f;
    tuning.m_frictionSlip          = wh.front ? prm.gripFront : prm.gripRear;
    tuning.m_maxSuspensionForce    = prm.mass*PhysicsWorld::Gravity*S*20.f;
    auto& info = vehicle->addWheel(toBt(conn)*S,down,axle,prm.suspRest*S,wh.radius*S,tuning,wh.front);
    info.m_rollInfluence = prm.rollInfluence;
    }
  tank = prm.boostTank;
  reset(start);
  }

Vehicle::~Vehicle() {
  owner.bullet().removeAction(vehicle.get());
  owner.bullet().removeRigidBody(body.get());
  }

void Vehicle::reset(const Mat4& start) {
  // drop in from slightly above the given pose so the springs settle
  btTransform t = toBt(start*Mat4::translate(com+Vec3{0,0.02f,0}));
  body->setWorldTransform(t);
  motion->setWorldTransform(t);
  // Bullet writes the interpolated transform back to the motion state on a frame shorter than its fixed step:
  // without this the car was at the world's origin for one frame after a reset
  body->setInterpolationWorldTransform(t);
  body->setInterpolationLinearVelocity(btVector3(0,0,0));
  body->setInterpolationAngularVelocity(btVector3(0,0,0));
  body->setLinearVelocity(btVector3(0,0,0));
  body->setAngularVelocity(btVector3(0,0,0));
  body->clearForces();
  vehicle->resetSuspension();
  for(int i=0; i<vehicle->getNumWheels(); ++i) {
    vehicle->setBrake(0,i);
    vehicle->applyEngineForce(0,i);
    vehicle->getWheelInfo(i).m_rotation = 0;
    }
  steer        = 0;
  under        = Surface::Default;
  sub          = 0;
  wet          = 0;
  underWater   = 0;
  lastHit      = Hit::None;
  wheelProp    = false;
  sinceProp    = 1e3f;
  touch        = Contact{};
  bump         = false;
  carBefore    = false;
  }

void Vehicle::place(const Mat4& pose, const Vec3& velocity, const Vec3& spin) {
  reset(pose);
  const btTransform t = toBt(pose*Mat4::translate(com));
  body->setWorldTransform(t);
  motion->setWorldTransform(t);
  body->setInterpolationWorldTransform(t);
  body->setLinearVelocity(toBt(velocity)*S);
  body->setAngularVelocity(toBt(spin));
  body->setInterpolationLinearVelocity(body->getLinearVelocity());
  body->setInterpolationAngularVelocity(body->getAngularVelocity());
  }

void Vehicle::recover() {
  const Mat4 m = pose();
  Vec3 f = m.vector({0,0,1});
  f.y = 0;
  f   = f.normalized();
  if(std::abs(f.x)+std::abs(f.z)<1e-3f)
    f = {0,0,1};
  Mat4 up = Mat4::rotate(1,std::atan2(f.x,f.z)*180.f/3.14159265f);
  Vec3 at = m.translation();
  if(auto h = owner.track().groundHeight(at+Vec3{0,0.5f,0}))
    at.y = *h - geom.min.y + 0.05f;
  up.m[0][3] = at.x; up.m[1][3] = at.y; up.m[2][3] = at.z;
  reset(up);
  }

void Vehicle::addBoost(float units) {
  tank = std::clamp(tank+units,0.f,prm.boostTank);
  }

void Vehicle::update(const Input& in, float dt) {
  const float v      = speed();
  const float massS  = prm.mass*S;    // force in scaled units = massS * acceleration in m/s^2
  const btVector3 lv = body->getLinearVelocity();
  const float     ls = float(lv.length())/S;

  // steering (RCCars.exe): the position runs towards the side held, slower the faster the car goes, until the
  // wheels reach AngleSteer; back to the middle (and across to the other side) it runs at SteerBack
  const float want = std::clamp(in.steer,-1.f,1.f);
  const float lock = std::abs(want)*prm.steerAngle;
  if(want==0 || want*steer<0) {
    const float to = want==0 ? 0.f : (want>0 ? 1e6f : -1e6f);
    steer += std::clamp(to-steer,-CarParams::SteerBack*dt,CarParams::SteerBack*dt);
    }
  else if(prm.steerCurve.at(std::abs(steer))<lock) {
    const float step = CarParams::steerRate(ls)*dt;
    float       s    = std::abs(steer)+step;
    // stop where the curve reaches the lock instead of a step past it
    for(int i=0; i<16 && prm.steerCurve.at(s)>lock; ++i)
      s -= step/16.f;
    steer = want>0 ? s : -s;
    }
  const float angle = (steer<0 ? -1.f : 1.f)*prm.steerCurve.at(std::abs(steer))*3.14159265f/180.f;

  // turbo tank (RCCars.exe): refills by itself, drains while the key is held; once dry with the key held, the
  // boost works again only BoostLockTime later and with BoostRestart units in the tank
  boostTimer += dt;
  boostOn = false;
  if(!in.boost) {
    boostLock = false;
    }
  else if(boostTimer>CarParams::BoostLockTime) {
    if(!boostLock) {
      if(tank<0.01f) {
        boostLock  = true;
        boostTimer = 0;
        }
      else
        boostOn = true;
      }
    else if(tank>CarParams::BoostRestart) {
      boostLock = false;
      boostOn   = true;
      }
    }
  if(boostOn)
    tank = std::max(tank-prm.boostDrain*dt,0.f);
  else
    tank = std::min(tank+prm.boostCharge*dt,prm.boostTank);

  // engine and brakes (RCCars.exe, but for the brake): while the engine pulls it also cancels the resistance
  const float resist = prm.airDrag*ls*ls + prm.rolling;
  float accel = 0, brake = 0;
  if(in.throttle>0) {
    if(v<-0.5f)
      brake = prm.brakeDecel*in.throttle;
    else if(float a = prm.acceleration(v,in.throttle,boostOn); a>0.001f)
      accel = a + resist;
    }
  else if(in.throttle<0) {
    if(v>0.5f) {
      brake = prm.brakeDecel*(-in.throttle);
      }
    else {
      const float full = (boostOn ? 1.5f : 1.f)*prm.reverseAccel*(-in.throttle);
      if(ls<prm.reverseFullSpeed)
        accel = -full;
      else if(ls<prm.reverseSpeed)
        accel = -full*(prm.reverseSpeed-ls)/(prm.reverseSpeed-prm.reverseFullSpeed);
      }
    }

  const int n = vehicle->getNumWheels();
  for(int i=0; i<n; ++i) {
    const bool front = geom.wheels[size_t(i)].front;
    // btRaycastVehicle takes the brake as an impulse per fixed step
    float b = brake;
    if(in.handbrake && !front)
      b = std::max(b,prm.brakeDecel*1.5f);
    vehicle->applyEngineForce(massS*accel/float(n),i);
    vehicle->setBrake(massS*b*PhysicsWorld::FixedDt/float(n),i);
    vehicle->setSteeringValue(front ? angle : 0.f,i);
    }

  // jump (RCCars.exe): a kick along the car's up axis; tipped over by more than JumpFlipAngle it puts the car
  // back on its wheels instead
  jumpTimer += dt;
  if(in.jump && jumpTimer>=CarParams::JumpCooldown) {
    const btVector3 up = body->getWorldTransform().getBasis().getColumn(1);
    if(up.y()<std::cos(CarParams::JumpFlipAngle*3.14159265f/180.f)) {
      recover();
      jumpTimer = 0;
      }
    else if(onGround()) {
      body->setLinearVelocity(body->getLinearVelocity()+up*(prm.jumpSpeed*S));
      btVector3 w = body->getAngularVelocity();
      w.setY(w.y()*1.5f);
      body->setAngularVelocity(w);
      jumpTimer = 0;
      }
    }

  // air and bearings resistance on the body (fading in over the first 0.1 m/s so that it does not shake a car
  // standing still)
  if(ls>1e-4f)
    body->applyCentralForce(-lv.normalized()*(prm.mass*S*resist*std::min(ls/0.1f,1.f)));

  surfaces(dt);
  hits(dt);
  }

// RCCars.exe gives the player's car a hit, shown as "Łup!" or "Wielkie Łup!" (msg_hits.csi) for a second and worth
// HitBoost turbo units, when it
//  - touches a prop of stone.sb faster than PropHitSpeed, at most every PropHitPause s (0x533ee0);
//  - knocks another car about (Opponents::collide);
//  - (not here yet: no characters on the tracks) hits people, a guard faster than 10 km/h or a dog faster than
//    15 km/h: a great hit, at most every 2 s.
// Road traffic, crabs, spiders and gulls give nothing.
namespace {
constexpr float PropHitSpeed  = 1.944445f; // m/s, 7 km/h
constexpr float PropHitPause  = 0.3f;
}

void Vehicle::hits(float dt) {
  lastHit    = Hit::None;
  sinceProp += dt;

  // what the body touches after the last steps (a wheel touches props through its ray)
  bool prop = wheelProp, car = false;
  wheelProp = false;
  touch     = Contact{};
  touch.impact = -1;   // none yet
  auto& disp = *owner.bullet().getDispatcher();
  for(int i=0; i<disp.getNumManifolds(); ++i) {
    const btPersistentManifold* m = disp.getManifoldByIndexInternal(i);
    const btCollisionObject*    other = nullptr;
    if(m->getBody0()==body.get())
      other = m->getBody1();
    else if(m->getBody1()==body.get())
      other = m->getBody0();
    if(other==nullptr)
      continue;
    bool touching = false;
    for(int c=0; c<m->getNumContacts() && !touching; ++c)
      touching = m->getContactPoint(c).getDistance()<=0;
    if(!touching)
      continue;
    prop |= other->getUserIndex()==PhysicsWorld::PropTag;
    car  |= other->getUserIndex()==PhysicsWorld::CarTag;
    if(other==&owner.ground() || other->getUserIndex()==PhysicsWorld::PropTag)
      contacts(*m,other);
    }
  if(touch.impact<0)
    touch = Contact{};
  bump      = car && !carBefore;
  carBefore = car;

  if(prop && sinceProp>PropHitPause && float(body->getLinearVelocity().length())/S>PropHitSpeed) {
    lastHit   = Hit::Hit;
    sinceProp = 0;
    addBoost(HitBoost);
    }
  }

void Vehicle::contacts(const btPersistentManifold& m, const btCollisionObject* other) {
  const bool  first = m.getBody0()==body.get();
  const auto* otherBody = btRigidBody::upcast(other);
  for(int c=0; c<m.getNumContacts(); ++c) {
    const btManifoldPoint& pt = m.getContactPoint(c);
    if(pt.getDistance()>0)
      continue;
    // the normal out of the other body, towards the car
    const btVector3 n   = first ? pt.m_normalWorldOnB : -pt.m_normalWorldOnB;
    const btVector3 at  = first ? pt.getPositionWorldOnA() : pt.getPositionWorldOnB();
    btVector3       v   = body->getVelocityInLocalPoint(at-body->getCenterOfMassPosition());
    if(otherBody!=nullptr && otherBody->getInvMass()>0)
      v -= otherBody->getVelocityInLocalPoint(at-otherBody->getCenterOfMassPosition());
    v *= 1.f/S;
    const float speed = float(v.length());
    Contact     ct;
    ct.impact = std::abs(float(v.dot(n)))*3.6f;
    ct.hit    = speed>1e-6f && float(v.dot(n))/speed < std::cos(145.f*3.14159265f/180.f);
    if(other==&owner.ground())
      ct.surface = owner.groundSound(first ? pt.m_index1 : pt.m_index0);
    else
      ct.surface = SoundSurface::Wood;
    // the strongest one: a hit before a touch, then the harder one
    if(touch.impact<0 || (ct.hit && !touch.hit) || (ct.hit==touch.hit && ct.impact>touch.impact))
      touch = ct;
    }
  }

void Vehicle::surfaces(float dt) {
  const auto& track = owner.track();
  const int   n     = vehicle->getNumWheels();

  // wheels: the solid surface under each one touching the ground, and how deep each one is in water
  const Mat4 m = pose();
  size_t count[size_t(Surface::Water)+1] = {};
  groundSnd   = SoundSurface::None;
  groundedNow = false;
  float  wetSum = 0;
  // per wheel: where it is held back (RCCars.exe: in water by depth, otherwise in deep sand) and how much
  struct Drag { Vec3 at; float k = 0; };
  Drag   drags[16];
  int    dragCount = 0;
  for(int i=0; i<n; ++i) {
    auto&       info = vehicle->getWheelInfo(i);
    const float r    = info.m_wheelsRadius/S;
    // from the body's pose, not btWheelInfo::m_worldTransform: that one is stale until the next step after reset()
    const Vec3  hub  = m.point(wheelPose(size_t(i)).translation());
    float       inWater = 0;  // how deep the wheel is, 0 .. 1 of its diameter
    float       depth   = 0;  // m of the wheel under the surface, from its bottom
    if(auto level = track.waterLevel(hub,0.5f)) {
      depth   = *level-(hub.y-r);
      inWater = std::clamp(depth/(2.f*r),0.f,1.f);
      }
    wetSum += inWater;
    // the wheel's sound surface (0x50acf0: the highest number of the wheels, water above the rest)
    if(inWater>0) {
      groundSnd   = SoundSurface::Water;
      groundedNow = true;
      }
    else if(info.m_raycastInfo.m_isInContact) {
      const uint8_t g = info.m_raycastInfo.m_groundObject==&owner.ground()
          ? track.soundBelow(fromBt(info.m_raycastInfo.m_contactPointWS)*(1.f/S)+Vec3{0,0.02f,0},0.05f)
          : uint8_t(SoundSurface::Wood);
      groundSnd   = std::max(groundSnd,g);
      groundedNow = true;
      }
    Drag d;
    if(depth>0.3f*r) {
      d.at = hub;
      d.k  = prm.waterDrag*std::clamp((depth-0.3f*r)/(0.6f*r),0.f,1.f);
      }
    if(!info.m_raycastInfo.m_isInContact) {
      if(d.k>0 && dragCount<int(std::size(drags)))
        drags[dragCount++] = d;
      continue;
      }
    const Vec3    at = fromBt(info.m_raycastInfo.m_contactPointWS)*(1.f/S);
    const Surface s  = track.surfaceBelow(at+Vec3{0,0.02f,0},0.05f);
    count[size_t(s)]++;
    if(d.k<=0 && s==Surface::DeepSand) {
      d.at = at;
      d.k  = prm.deepSandDrag;
      }
    if(d.k>0 && dragCount<int(std::size(drags)))
      drags[dragCount++] = d;
    }
  const float wetNow = n>0 ? wetSum/float(n) : 0.f;

  // body: how much of its height (from the bottom of the wheels to the roof, antenna aside) is below the water surface
  const float height = std::max(geom.roof-geom.min.y,0.01f);
  const Vec3  centre = m.point(Vec3{(geom.bodyMin.x+geom.bodyMax.x)*0.5f,geom.min.y+height*0.5f,
                                    (geom.bodyMin.z+geom.bodyMax.z)*0.5f});
  float subNow = 0;
  if(auto level = track.waterLevel(centre,5.f))
    subNow = std::clamp((*level-(centre.y-height*0.5f))/height,0.f,1.f);

  // splashing in: the first wheel, then the body, each takes away a share of the horizontal speed
  btVector3 v = body->getLinearVelocity();
  btVector3 h(v.x(),0,v.z());
  float keep = 1;
  if(wet<=0 && wetNow>0)
    keep *= 1.f-prm.waterDropWheels;
  if(sub<=0 && subNow>0)
    keep *= 1.f-prm.waterDropBody;
  if(keep<1) {
    v -= h*(1.f-keep);
    h *= keep;
    body->setLinearVelocity(v);
    }

  // deep sand and water hold every wheel in them back: k * v^2 against the motion of that point of the car
  const float v2 = float(body->getLinearVelocity().length2())/(S*S);
  for(int i=0; i<dragCount; ++i) {
    const btVector3 rel = toBt(drags[i].at)*S - body->getCenterOfMassPosition();
    btVector3       pv  = body->getVelocityInLocalPoint(rel);
    if(pv.length2()<1e-8f)
      continue;
    body->applyForce(-pv.normalized()*(prm.mass*S*drags[i].k*v2),rel);
    }

  // under water: everything slows down, falling and tumbling included
  if(subNow>0) {
    const float k = std::pow(prm.waterDamping,dt*30.f*subNow);
    body->setLinearVelocity (body->getLinearVelocity()*k);
    body->setAngularVelocity(body->getAngularVelocity()*k);
    }

  wet        = wetNow;
  sub        = subNow;
  const float drown = drownRate(track,m.translation(),prm);
  underWater = drown>0 ? underWater+drown*dt : 0.f;
  if(wet>0 || sub>0) {
    under = Surface::Water;
    }
  else {
    under = Surface::Default;
    size_t best = 0;
    for(size_t i=0; i<std::size(count); ++i)
      if(count[i]>best) {
        best  = count[i];
        under = Surface(i);
        }
    }
  }

float Vehicle::drownRate(const CollisionMesh& track, const Vec3& origin, const CarParams& prm) {
  // a point just above the origin under the surface, the deeper the faster
  const Vec3 probe   = origin+Vec3{0,prm.drownHeight,0};
  const auto surface = track.waterLevel(probe,10.f);
  if(!surface || *surface<=probe.y)
    return 0;
  return probe.y<*surface-prm.drownDeep ? 4.f : 1.f;
  }

Mat4 Vehicle::pose() const {
  btTransform t;
  motion->getWorldTransform(t);
  return fromBt(t)*Mat4::translate(com*-1.f);
  }

Vec3 Vehicle::centre() const {
  return fromBt(body->getCenterOfMassPosition())*(1.f/S);
  }

Vec3 Vehicle::velocity() const {
  return fromBt(body->getLinearVelocity())*(1.f/S);
  }

Vec3 Vehicle::localSpin() const {
  return fromBt(body->getWorldTransform().getBasis().transpose()*body->getAngularVelocity());
  }

void Vehicle::applyImpulse(const Vec3& impulse, const Vec3& at) {
  body->applyImpulse(toBt(impulse)*S,toBt(at)*S-body->getCenterOfMassPosition());
  }

void Vehicle::addVelocity(const Vec3& dv) {
  body->activate(true);
  body->setLinearVelocity(body->getLinearVelocity()+toBt(dv)*S);
  }

void Vehicle::hold(const Mat4& at) {
  const btTransform t = toBt(at*Mat4::translate(com));
  body->setWorldTransform(t);
  motion->setWorldTransform(t);
  body->setInterpolationWorldTransform(t);
  body->setInterpolationLinearVelocity(btVector3(0,0,0));
  body->setInterpolationAngularVelocity(btVector3(0,0,0));
  body->setLinearVelocity(btVector3(0,0,0));
  body->setAngularVelocity(btVector3(0,0,0));
  body->clearForces();
  body->activate(true);
  }

float Vehicle::speed() const {
  return float(vehicle->getCurrentSpeedKmHour())/3.6f/S;
  }

size_t Vehicle::wheelsOnGround() const {
  size_t n = 0;
  for(int i=0; i<vehicle->getNumWheels(); ++i)
    n += vehicle->getWheelInfo(i).m_raycastInfo.m_isInContact ? 1 : 0;
  return n;
  }

bool Vehicle::onGround() const {
  return wheelsOnGround()>0;
  }

Mat4 Vehicle::wheelPose(size_t i) const {
  auto&       wh   = geom.wheels[i];
  auto&       info = vehicle->getWheelInfo(int(i));
  const float drop = float(info.m_raycastInfo.m_suspensionLength)/S - prm.suspRest;
  const float deg  = 180.f/3.14159265f;
  return Mat4::translate(wh.center-Vec3{0,drop,0}) *
         Mat4::rotate(1,float(info.m_steering)*deg) *
         Mat4::rotate(0,float(info.m_rotation)*deg);
  }
