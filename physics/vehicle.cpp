#include "vehicle.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace rc;

namespace {

constexpr float S = PhysicsWorld::Scale;

btVector3 toBt(const Vec3& v)   { return btVector3(v.x,v.y,v.z); }
Vec3      fromBt(const btVector3& v) { return {float(v.x()),float(v.y()),float(v.z())}; }

// rotation + translation of a Mat4 (unscaled) -> scaled btTransform
btTransform toBt(const Mat4& m) {
  btMatrix3x3 basis(m.m[0][0],m.m[0][1],m.m[0][2],
                    m.m[1][0],m.m[1][1],m.m[1][2],
                    m.m[2][0],m.m[2][1],m.m[2][2]);
  return btTransform(basis,toBt(m.translation())*S);
  }

Mat4 fromBt(const btTransform& t) {
  Mat4 m;
  const btMatrix3x3& b = t.getBasis();
  for(int r=0; r<3; ++r)
    for(int c=0; c<3; ++c)
      m.m[r][c] = float(b[r][c]);
  const btVector3 o = t.getOrigin()/S;
  m.m[0][3] = float(o.x()); m.m[1][3] = float(o.y()); m.m[2][3] = float(o.z());
  return m;
  }

}

// Suspension rays go through the COLL grid instead of Bullet's broadphase.
class Vehicle::Raycaster : public btVehicleRaycaster {
  public:
    explicit Raycaster(PhysicsWorld& w) : world(w) {}

    void* castRay(const btVector3& from, const btVector3& to, btVehicleRaycasterResult& result) override {
      const Vec3 f = fromBt(from)*(1.f/S);
      const Vec3 d = fromBt(to-from)*(1.f/S);
      auto hit = world.track().raycast(f,d,1.f);
      if(!hit)
        return nullptr;
      result.m_hitPointInWorld  = toBt(hit->point)*S;
      result.m_hitNormalInWorld = toBt(hit->normal);
      result.m_distFraction     = hit->distance;
      return &world.ground();
      }

  private:
    PhysicsWorld& world;
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
  // fast and small: sweep a sphere inside the box against thin walls
  body->setCcdMotionThreshold(std::min({half.x,half.y,half.z})*S*0.5f);
  body->setCcdSweptSphereRadius(std::min({half.x,half.y,half.z})*S*0.8f);
  owner.bullet().addRigidBody(body.get());

  raycaster = std::make_unique<Raycaster>(owner);
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
  jumpCooldown = 0;
  tank         = 1;
  under        = Surface::Default;
  sub          = 0;
  wet          = 0;
  underWater   = 0;
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

void Vehicle::update(const Input& in, float dt) {
  const float v      = speed();
  const float top    = prm.topSpeed;
  const float massS  = prm.mass*S;    // force in scaled units = massS * acceleration in m/s^2

  // steering: eased towards the input, less lock at speed
  const float target = std::clamp(in.steer,-1.f,1.f);
  const float rate   = (std::abs(target)<std::abs(steer) || target*steer<0) ? 6.f : 3.f;
  steer += std::clamp(target-steer,-rate*dt,rate*dt);
  const float k      = std::clamp(std::abs(v)/top,0.f,1.f);
  const float angle  = steer*prm.steerAngle*(1.f + (prm.steerAtTop-1.f)*k)*3.14159265f/180.f;

  // engine and brakes; the acceleration spline is the net acceleration on flat ground, so the engine also
  // pushes against air drag
  const float drag   = prm.airDrag*v*v;
  float accel = 0, brake = 0;
  const bool boosting = in.boost && in.throttle>0 && tank>0;
  if(boosting)
    tank = std::max(tank-dt/prm.boostTime,0.f);
  else
    tank = std::min(tank+dt/prm.boostRecharge,1.f);
  if(in.throttle>0) {
    if(v<-0.5f)
      brake = prm.brakeDecel*in.throttle;
    else
      accel = (prm.acceleration(v,boosting)+drag)*in.throttle;
    }
  else if(in.throttle<0) {
    if(v>0.5f)
      brake = prm.brakeDecel*(-in.throttle);
    else if(-v<top*prm.reverseSpeed)
      accel = -(prm.acceleration(-v)*0.6f)*(-in.throttle);
    }
  else {
    brake = prm.rolling;
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

  // jump: an upward kick along the car's up axis, so on a slope it pushes off the surface
  jumpCooldown = std::max(jumpCooldown-dt,0.f);
  if(in.jump && jumpCooldown<=0 && wheelsOnGround()>=2) {
    btVector3 up = body->getWorldTransform().getBasis().getColumn(1);
    up = (up+btVector3(0,1,0)).normalized();
    body->applyCentralImpulse(up*(prm.mass*prm.jumpSpeed*S));
    jumpCooldown = 0.5f;
    }

  // air drag on the body
  const btVector3 lv = body->getLinearVelocity();
  const float     ls = float(lv.length());
  if(ls>1e-3f)
    body->applyCentralForce(-lv*(prm.mass*prm.airDrag*ls/S));

  surfaces(dt);
  }

void Vehicle::surfaces(float dt) {
  const auto& track = owner.track();
  const int   n     = vehicle->getNumWheels();

  // wheels: the solid surface under each one touching the ground, and how deep each one is in water
  const Mat4 m = pose();
  size_t count[size_t(Surface::Water)+1] = {};
  int    inSand = 0;
  float  wetSum = 0;
  for(int i=0; i<n; ++i) {
    auto&       info = vehicle->getWheelInfo(i);
    const float r    = info.m_wheelsRadius/S;
    // from the body's pose, not btWheelInfo::m_worldTransform: that one is stale until the next step after reset()
    const Vec3  hub  = m.point(wheelPose(size_t(i)).translation());
    if(auto level = track.waterLevel(hub,0.5f))
      wetSum += std::clamp((*level-(hub.y-r))/(2.f*r),0.f,1.f);
    if(!info.m_raycastInfo.m_isInContact)
      continue;
    const Vec3    at = fromBt(info.m_raycastInfo.m_contactPointWS)*(1.f/S);
    const Surface s  = track.surfaceBelow(at+Vec3{0,0.02f,0},0.05f);
    count[size_t(s)]++;
    inSand += s==Surface::DeepSand ? 1 : 0;
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

  // deep sand and water hold the car back in proportion to its speed (a force on the scaled body: m * k * v)
  const float sand = n>0 ? float(inSand)/float(n) : 0.f;
  const float drag = prm.deepSandDrag*sand + prm.waterDrag*std::max(wetNow,subNow);
  if(drag>0)
    body->applyCentralForce(-h*(prm.mass*drag));

  // under water: everything slows down, falling and tumbling included
  if(subNow>0) {
    const float k = std::pow(prm.waterDamping,dt*30.f*subNow);
    body->setLinearVelocity (body->getLinearVelocity()*k);
    body->setAngularVelocity(body->getAngularVelocity()*k);
    }

  wet        = wetNow;
  sub        = subNow;
  underWater = sub>=1.f ? underWater+dt : 0.f;
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

Mat4 Vehicle::pose() const {
  btTransform t;
  motion->getWorldTransform(t);
  return fromBt(t)*Mat4::translate(com*-1.f);
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
