#include "vehicle.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "btconvert.h"

using namespace rc;
using namespace rc::bt;

namespace {
constexpr float Pi = 3.14159265f;
}

// RCCars.exe's car physics runs on every fixed step of the world, after Bullet has moved the body
class Vehicle::Action : public btActionInterface {
  public:
    explicit Action(Vehicle& v) : car(v) {}
    void updateAction(btCollisionWorld*, btScalar h) override { car.step(float(h)); }
    void debugDraw(btIDebugDraw*) override {}

  private:
    Vehicle& car;
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

void PhysicsWorld::step(float dt) {
  world->stepSimulation(dt,8,FixedDt);
  }

Vehicle::Vehicle(PhysicsWorld& w, const CarGeometry& g, const CarParams& p, const Mat4& start)
  : owner(w), geom(g), prm(p) {
  // RCCars.exe (0x4f2270): the centre of mass CenterMassOY above the model's origin
  com = {0,prm.comHeight,0};

  // wheels (0x4ef680): a sphere hanging from a point springMount above its hub (dx to the outside of the front
  // wheels). The front pair steers, the rear pair is driven; the Hummer's middle pair turns by a third.
  wheels.resize(geom.wheels.size());
  float rearZ = 1e9f;
  for(auto& gw:geom.wheels)
    if(!gw.front)
      rearZ = std::min(rearZ,gw.center.z);
  for(size_t i=0; i<wheels.size(); ++i) {
    const auto& gw = geom.wheels[i];
    Wheel&      wh = wheels[i];
    const bool  rear = !gw.front && gw.center.z<=rearZ+1e-3f;
    wh.front   = gw.front;
    wh.driven  = rear;
    wh.steerBy = gw.front ? 1.f : (rear ? 0.f : 1.f/3.f);
    wh.radius  = gw.front ? prm.wheelRadius : prm.rearWheelRadius;
    wh.mount   = gw.center + Vec3{gw.front ? (gw.left ? prm.springMount.x : -prm.springMount.x) : 0.f,
                                  prm.springMount.y,0};
    int* pair = gw.front ? frontPair : (rear ? rearPair : nullptr);
    if(pair!=nullptr && pair[gw.left ? 0 : 1]<0)
      pair[gw.left ? 0 : 1] = int(i);
    }
  springK = wheels.empty() ? 0.f : prm.mass*PhysicsWorld::Gravity/(float(wheels.size())*std::max(prm.springSag,1e-3f));

  // the body: the spheres of cdt<N>.ini, and the wheels' spheres, which Vehicle::suspension keeps where the wheels
  // are (a little higher, so that the ground under a wheel is left to its spring): what the wheels run into
  shape = std::make_unique<btCompoundShape>();
  float smallest = 1e9f;
  auto  sphere   = [&](const Vec3& at, float r) {
    spheres.push_back(std::make_unique<btSphereShape>(r*S));
    btTransform local;
    local.setIdentity();
    local.setOrigin(toBt(at-com)*S);
    shape->addChildShape(local,spheres.back().get());
    smallest = std::min(smallest,r);
    };
  for(auto& s:prm.body)
    sphere(s.at,s.radius);
  for(auto& wh:wheels)
    sphere(wh.mount-Vec3{0,prm.springMin,0},wh.radius);
  wheelShape[0] = std::make_unique<btSphereShape>(prm.wheelRadius*S);
  wheelShape[1] = std::make_unique<btSphereShape>(prm.rearWheelRadius*S);

  // inertia (0x4f2270, 0x4752b0): a solid box over the whole model, the Buggy's about its forward axis doubled
  const Vec3 size = geom.max-geom.min;
  const float m12 = prm.mass/12.f;
  btVector3 inertia(m12*(size.y*size.y+size.z*size.z),
                    m12*(size.x*size.x+size.z*size.z),
                    m12*(size.x*size.x+size.y*size.y));
  if(prm.car==2)
    inertia.setZ(inertia.z()*2);
  inertia *= S*S;

  motion = std::make_unique<btDefaultMotionState>();
  btRigidBody::btRigidBodyConstructionInfo ci(prm.mass,motion.get(),shape.get(),inertia);
  // the original answers a hit with next to no bounce and the tires do the gripping, not the body
  ci.m_friction       = 0.3f;
  ci.m_restitution    = 0.f;
  ci.m_linearDamping  = 0.f;
  ci.m_angularDamping = 0.f;
  body = std::make_unique<btRigidBody>(ci);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setUserIndex(PhysicsWorld::CarTag);
  // fast and small: sweep a sphere against thin walls
  if(smallest<1e9f) {
    body->setCcdMotionThreshold(smallest*S*0.5f);
    body->setCcdSweptSphereRadius(smallest*S*0.8f);
    }
  owner.bullet().addRigidBody(body.get());

  action = std::make_unique<Action>(*this);
  owner.bullet().addAction(action.get());

  tank = prm.boostTank;
  reset(start);
  }

Vehicle::~Vehicle() {
  owner.bullet().removeAction(action.get());
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
  for(auto& wh:wheels) {
    wh.length  = prm.springLength-prm.springSag;
    wh.rate    = 0;
    wh.contact = false;
    wh.ground  = nullptr;
    wh.spin    = 0;
    }
  steer        = 0;
  steerDeg     = 0;
  gear         = 1;
  braking      = false;
  brakeKey     = false;
  drive        = 0;
  movingTime   = 0;
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
  Mat4 up = Mat4::rotate(1,std::atan2(f.x,f.z)*180.f/Pi);
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
  steerDeg = (steer<0 ? -1.f : 1.f)*prm.steerCurve.at(std::abs(steer));

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

  // gears and brakes (RCCars.exe, 0x4ee5e0): the brake key brakes going forwards and puts the reverse in once the
  // car is down to ReverseSpeed, the throttle the same the other way round; the handbrake holds the car. Braking,
  // the tires hold the car (Vehicle::tires) and the engine does not pull.
  constexpr float GearSpeed = 2.f;
  brakeKey = in.throttle<0;
  braking  = in.handbrake;
  if(in.throttle>0 && gear<0) {
    if(std::abs(v)<=GearSpeed) gear = 1; else braking = true;
    }
  else if(in.throttle<0 && gear>0) {
    if(std::abs(v)<=GearSpeed) gear = -1; else braking = true;
    }
  // engine (0x4eea50): while it pulls it also cancels the resistance
  const float resist = prm.airDrag*ls*ls + prm.rolling;
  drive = 0;
  if(!braking && in.throttle>0 && gear>0) {
    if(float a = prm.acceleration(v,in.throttle,boostOn); a>0.001f)
      drive = a + resist;
    }
  else if(!braking && in.throttle<0 && gear<0) {
    const float full = (boostOn ? 1.5f : 1.f)*prm.reverseAccel*(-in.throttle);
    if(ls<prm.reverseFullSpeed)
      drive = -full;
    else if(ls<prm.reverseSpeed)
      drive = -full*(prm.reverseSpeed-ls)/(prm.reverseSpeed-prm.reverseFullSpeed);
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

// The physics of RCCars.exe's car (0x4f0910, 0x4f5e50), on every fixed step:
//  - the springs (0x4f0f80): each wheel touching the ground pushes the body along its up axis at the top of the
//    spring with springK * (springLength - length - 0.02 r) - springDamping * (rate of change of the length), as
//    long as the spring is shorter than that. The body is pushed by what of it is along the ground's normal, and
//    turned by the spring's moment about the car's own axes, the pitch times springPitch, the roll times
//    springRoll, none about the up axis;
//  - the engine (0x4eea50): `drive` (at most 20 x the rear grip) on each driven wheel standing on ground less
//    steep than 46 degrees, half each, along the ground under the wheel;
//  - the tires (Vehicle::tires).
// Bullet adds gravity and stops the body's spheres at the track and the props. The car's speed stays below 20 m/s
// and its angular momentum below 1 kg m^2/s.
void Vehicle::step(float h) {
  if(h<=0)
    return;
  suspension(h);

  const btTransform& t     = body->getWorldTransform();
  const btMatrix3x3& basis = t.getBasis();
  const Vec3 up  = fromBt(basis.getColumn(1));
  const Vec3 fwd = fromBt(basis.getColumn(2));
  const Vec3 c   = fromBt(body->getCenterOfMassPosition())*(1.f/S);
  // the body where Bullet has just put it (pose() is where it is drawn, as of the last frame)
  const Mat4 m   = fromBt(t)*Mat4::translate(com*-1.f);

  Vec3 force, torque;
  // reactions on the props the wheels stand on
  auto react = [&](const Wheel& wh, const Vec3& f) {
    if(wh.ground==nullptr || wh.ground==&owner.ground())
      return;
    auto* prop = const_cast<btRigidBody*>(btRigidBody::upcast(wh.ground));
    if(prop!=nullptr && prop->getInvMass()>0) {
      prop->activate();
      prop->applyImpulse(toBt(f*(-h))*S,toBt(wh.point)*S-prop->getCenterOfMassPosition());
      }
    };

  // springs
  for(auto& wh:wheels) {
    const float l = wh.length+0.02f*wh.radius;
    if(!wh.contact || l>prm.springLength)
      continue;
    // the damper gives no more than the spring (0x4f0f80): the wheel never pulls the car down
    const float k = springK*(prm.springLength-l);
    const float f = k - std::clamp(prm.springDamping*wh.rate,-k,k);
    if(f<=0)
      continue;
    const Vec3 at = m.point(wh.mount);
    const Vec3 fs = up*f;
    const Vec3 fn = wh.normal*Vec3::dot(fs,wh.normal);
    force = force+fn;
    react(wh,fn);
    Vec3 tl = fromBt(basis.transpose()*toBt(Vec3::cross(at-c,fs)));
    tl.x *= prm.springPitch;
    tl.y  = 0;
    tl.z *= prm.springRoll;
    torque = torque+fromBt(basis*toBt(tl));
    }

  // engine
  if(!braking && drive!=0) {
    const float a = std::clamp(drive,-20.f*prm.gripRear,20.f*prm.gripRear)*prm.mass;
    for(int i:rearPair) {
      if(i<0)
        continue;
      const Wheel& wh = wheels[size_t(i)];
      if(!wh.contact || wh.normal.y<std::cos(46.f*Pi/180.f))
        continue;
      const Vec3 d = (fwd-wh.normal*Vec3::dot(fwd,wh.normal)).normalized();
      const Vec3 f = d*(0.5f*a);
      force  = force+f;
      torque = torque+Vec3::cross(wh.point-c,f);
      react(wh,f);
      }
    }

  tires(h,force,torque);

  body->applyCentralImpulse(toBt(force*h)*S);
  body->applyTorqueImpulse(toBt(torque*h)*(S*S));

  // limits (0x4f5e50): |P| <= 20 m, |L| <= 1
  btVector3   lv  = body->getLinearVelocity();
  const float top = 20.f*S;
  if(lv.length2()>top*top)
    body->setLinearVelocity(lv*(top/lv.length()));
  const btMatrix3x3 iw = body->getInvInertiaTensorWorld().inverse()*(1.f/(S*S)); // real kg m^2
  const btVector3   L  = iw*body->getAngularVelocity();
  if(L.length2()>1.f)
    body->setAngularVelocity(body->getAngularVelocity()*(1.f/float(L.length())));
  }

// RCCars.exe (0x4fb340): a wheel pushed into the ground goes up at once, to springMin at most; otherwise it goes
// down towards its rest, 10 times faster once the car has gone faster than 5 km/h for 0.5 s, and stops on the
// ground. At rest is springLength while the car goes faster than 5 km/h, 0.95 springSag shorter when slower.
// The original moves the wheel by r times a factor we have not decoded; we take WheelRate r m/s (slower, the wheels
// could not keep up with the body and the car sat on its belly). The wheel is swept down from the top of its spring,
// so it also climbs onto a step in front of it.
void Vehicle::suspension(float h) {
  constexpr float Moving    = 1.388889f; // m/s, 5 km/h
  constexpr float WheelRate = 30.f;
  constexpr float WheelLift = 0.3f;
  const Vec3  v     = velocity();
  const float speed = v.length();
  movingTime = speed>=Moving ? movingTime+h : 0.f;

  struct Sweep : btCollisionWorld::ClosestConvexResultCallback {
    const btCollisionObject* ground;
    Sweep(const btVector3& f, const btVector3& t, const btCollisionObject* g)
      : ClosestConvexResultCallback(f,t), ground(g) {}
    bool needsCollision(btBroadphaseProxy* proxy) const override {
      auto obj = static_cast<const btCollisionObject*>(proxy->m_clientObject);
      return (obj==ground || obj->getUserIndex()==PhysicsWorld::PropTag) && ClosestConvexResultCallback::needsCollision(proxy);
      }
    };

  const Mat4      m     = fromBt(body->getWorldTransform())*Mat4::translate(com*-1.f);
  const btMatrix3x3& basis = body->getWorldTransform().getBasis();
  const Vec3      up    = fromBt(basis.getColumn(1));
  const Vec3      fwd   = fromBt(basis.getColumn(2));
  const float     rest  = speed>=Moving ? prm.springLength : prm.springLength-0.95f*prm.springSag;
  const float     reach = std::max(prm.springMax,prm.springLength);
  for(auto& wh:wheels) {
    const float before = wh.length;
    const Vec3  top    = m.point(wh.mount);
    // how far down the sphere can go from the top of the spring before it touches the ground
    btTransform from, to;
    from.setIdentity();
    to.setIdentity();
    from.setOrigin(toBt(top)*S);
    to.setOrigin(toBt(top-up*reach)*S);
    Sweep cb(from.getOrigin(),to.getOrigin(),&owner.ground());
    owner.bullet().convexSweepTest(wh.front ? wheelShape[0].get() : wheelShape[1].get(),from,to,cb);
    const float ground = cb.hasHit() ? float(cb.m_closestHitFraction)*reach : 1e9f;

    float rate = WheelRate*wh.radius;
    if(speed>=Moving)
      rate *= 1.f+18.f*std::min(movingTime,0.5f);
    float len = wh.length;
    if(len<rest)
      len = std::min(len+rate*h,rest);
    else
      len = std::max(len-rate*h,rest);
    wh.contact = ground<=len+0.02f*wh.radius;
    if(ground<len)
      len = ground;
    wh.length = std::clamp(len,prm.springMin,reach);
    wh.rate   = (wh.length-before)/h;
    if(wh.contact) {
      wh.point  = fromBt(cb.m_hitPointWorld)*(1.f/S);
      wh.normal = fromBt(cb.m_hitNormalWorld).normalized();
      wh.ground = cb.m_hitCollisionObject;
      wheelProp |= wh.ground!=nullptr && wh.ground->getUserIndex()==PhysicsWorld::PropTag;
      }
    else {
      wh.ground = nullptr;
      }
    // the wheel's sphere in the body, for what the wheel runs into
    btTransform local;
    local.setIdentity();
    local.setOrigin(toBt(wh.mount-Vec3{0,std::max(wh.length-WheelLift*wh.radius,prm.springMin),0}-com)*S);
    shape->updateChildTransform(int(prm.body.size())+int(&wh-wheels.data()),local,false);
    // rolling, for drawing
    if(wh.contact && !braking)
      wh.spin = std::fmod(wh.spin+Vec3::dot(v,fwd)/wh.radius*h,2.f*Pi);
    }
  shape->recalculateLocalAabb();
  }

// RCCars.exe (0x4edac0, 0x4ee280): the tires act at two wheels at most, one front and one rear: those touching
// the ground, or with three or more touching, the outer ones in the turn (the right ones going straight). Each
// stops its wheel sliding sideways (braking: sliding at all) with the force that, together with the other one,
// cancels the acceleration of the wheel's point by the other forces, up to 5 x grip (rear and drifting: 10 x grip),
// and its velocity in that direction, up to 10 x grip per second. With the wheels straight they also stop the car
// turning, its angular momentum falling at 1/s.
void Vehicle::tires(float h, Vec3& force, Vec3& torque) {
  int picked[2], n = 0;
  int touching = 0;
  for(int i:{frontPair[0],frontPair[1],rearPair[0],rearPair[1]})
    touching += i>=0 && wheels[size_t(i)].contact ? 1 : 0;
  auto touches = [&](int i) { return i>=0 && wheels[size_t(i)].contact; };
  if(touching<3) {
    for(int i:{frontPair[0],frontPair[1],rearPair[0],rearPair[1]})
      if(touches(i) && n<2)
        picked[n++] = i;
    }
  else {
    // + = left: the outer wheels of a left turn are the right ones
    const bool left = steerDeg>=0.01f;
    const int  r = left ? (touches(rearPair[1])  ? rearPair[1]  : rearPair[0])
                        : (touches(rearPair[0])  ? rearPair[0]  : rearPair[1]);
    const int  f = left ? (touches(frontPair[0]) ? frontPair[0] : frontPair[1])
                        : (touches(frontPair[1]) ? frontPair[1] : frontPair[0]);
    if(touches(r)) picked[n++] = r;
    if(touches(f)) picked[n++] = f;
    }
  if(n==0)
    return;

  const btMatrix3x3& basis = body->getWorldTransform().getBasis();
  const Vec3  up   = fromBt(basis.getColumn(1));
  const Vec3  fwd  = fromBt(basis.getColumn(2));
  const Vec3  c    = fromBt(body->getCenterOfMassPosition())*(1.f/S);
  const btMatrix3x3 iinv = body->getInvInertiaTensorWorld()*(S*S); // real 1/(kg m^2)
  auto  inv  = [&](const Vec3& v) { return fromBt(iinv*toBt(v)); };
  const float im   = 1.f/prm.mass;
  const bool  drift = drifting();

  // the acceleration of the body by the other forces, gravity included
  const Vec3 a  = (force+Vec3{0,-PhysicsWorld::Gravity*prm.mass,0})*im;
  const Vec3 al = inv(torque);

  Vec3  d[2], r[2];
  float b[2];
  for(int k=0; k<n; ++k) {
    const Wheel& wh = wheels[size_t(picked[k])];
    Vec3 heading = fwd;
    if(wh.front) {
      const float s = steerAngle(wh)*Pi/180.f;
      heading = fwd*std::cos(s) + Vec3::cross(up,fwd)*std::sin(s);
      }
    r[k] = wh.point-c;
    const Vec3 pv = fromBt(body->getVelocityInLocalPoint(toBt(r[k])*S))*(1.f/S);
    d[k] = Vec3::cross(heading,wh.normal).normalized();
    if(braking) {
      const Vec3 slide = pv-wh.normal*Vec3::dot(pv,wh.normal);
      if(slide.length()>1e-4f)
        d[k] = slide.normalized();
      }
    const float grip = wh.front ? prm.gripFront : prm.gripRear*(drift ? prm.driftGrip : 1.f);
    const float lim  = (!wh.front || drift ? 10.f : 5.f)*grip;
    b[k] = std::clamp(-Vec3::dot(a+Vec3::cross(al,r[k]),d[k]),-lim,lim);
    const float vd = Vec3::dot(pv,d[k]);
    if(std::abs(vd)>0.0005f)
      b[k] -= std::clamp(vd/h,-10.f*grip,10.f*grip);
    }

  // A x = b: the acceleration of point i along d[i] by a unit force at point j along d[j]
  float A[2][2];
  for(int i=0; i<n; ++i)
    for(int j=0; j<n; ++j)
      A[i][j] = Vec3::dot(d[i],d[j])*im + Vec3::dot(Vec3::cross(inv(Vec3::cross(r[j],d[j])),r[i]),d[i]);
  float x[2] = {0,0};
  if(n==1) {
    if(std::abs(A[0][0])<1e-6f)
      return;
    x[0] = b[0]/A[0][0];
    }
  else {
    const float det = A[0][0]*A[1][1]-A[0][1]*A[1][0];
    if(std::abs(det)<1e-9f)
      return;
    x[0] = (b[0]*A[1][1]-A[0][1]*b[1])/det;
    x[1] = (A[0][0]*b[1]-A[1][0]*b[0])/det;
    }
  for(int k=0; k<n; ++k) {
    const Vec3 f = d[k]*x[k];
    force  = force+f;
    torque = torque+Vec3::cross(r[k],f);
    const Wheel& wh = wheels[size_t(picked[k])];
    if(wh.ground!=nullptr && wh.ground!=&owner.ground()) {
      auto* prop = const_cast<btRigidBody*>(btRigidBody::upcast(wh.ground));
      if(prop!=nullptr && prop->getInvMass()>0) {
        prop->activate();
        prop->applyImpulse(toBt(f*(-h))*S,toBt(wh.point)*S-prop->getCenterOfMassPosition());
        }
      }
    }
  if(std::abs(steerDeg)<0.2f) {
    const btMatrix3x3 iw = body->getInvInertiaTensorWorld().inverse()*(1.f/(S*S));
    torque = torque-fromBt(iw*body->getAngularVelocity());
    }
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

  // what the body touches after the last steps (the wheels touch props through Vehicle::suspension)
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
  const int   n     = int(wheels.size());

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
    const Wheel& wh  = wheels[size_t(i)];
    const float r    = wh.radius;
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
    else if(wh.contact) {
      const uint8_t g = wh.ground==&owner.ground()
          ? track.soundBelow(wh.point+Vec3{0,0.02f,0},0.05f)
          : uint8_t(SoundSurface::Wood);
      groundSnd   = std::max(groundSnd,g);
      groundedNow = true;
      }
    Drag d;
    if(depth>0.3f*r) {
      d.at = hub;
      d.k  = prm.waterDrag*std::clamp((depth-0.3f*r)/(0.6f*r),0.f,1.f);
      }
    if(!wh.contact) {
      if(d.k>0 && dragCount<int(std::size(drags)))
        drags[dragCount++] = d;
      continue;
      }
    const Vec3    at = wh.point;
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
  return Vec3::dot(velocity(),fromBt(body->getWorldTransform().getBasis().getColumn(2)));
  }

size_t Vehicle::wheelsOnGround() const {
  size_t n = 0;
  for(auto& wh:wheels)
    n += wh.contact ? 1 : 0;
  return n;
  }

bool Vehicle::onGround() const {
  return wheelsOnGround()>0;
  }

float Vehicle::steerAngle(const Wheel& w) const {
  return steerDeg*w.steerBy;
  }

bool Vehicle::drifting() const {
  return brakeKey && std::abs(steerDeg)>prm.driftAngle;
  }

Mat4 Vehicle::wheelPose(size_t i) const {
  const auto& gw = geom.wheels[i];
  const auto& wh = wheels[i];
  const float deg = 180.f/Pi;
  return Mat4::translate({gw.center.x,wh.mount.y-wh.length,gw.center.z}) *
         Mat4::rotate(1,steerAngle(wh)) *
         Mat4::rotate(0,wh.spin*deg);
  }
