#include "vehicle.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

#include "btconvert.h"

using namespace rc;
using namespace rc::bt;

namespace {
constexpr float Pi = 3.14159265f;
}

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
  for(Vehicle* c:cars)
    c->advance(dt);
  // what Bullet keeps of the frame time (btDiscreteDynamicsWorld::stepSimulation)
  localTime += dt;
  if(localTime>=FixedDt)
    localTime -= float(int(localTime/FixedDt))*FixedDt;
  world->stepSimulation(dt,8,FixedDt);
  }

namespace {
// the point of triangle abc nearest to p (Ericson, Real-Time Collision Detection 5.1.5)
Vec3 nearestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3  ab = b-a, ac = c-a, ap = p-a;
  const float d1 = Vec3::dot(ab,ap), d2 = Vec3::dot(ac,ap);
  if(d1<=0 && d2<=0)
    return a;
  const Vec3  bp = p-b;
  const float d3 = Vec3::dot(ab,bp), d4 = Vec3::dot(ac,bp);
  if(d3>=0 && d4<=d3)
    return b;
  const float vc = d1*d4-d3*d2;
  if(vc<=0 && d1>=0 && d3<=0)
    return a+ab*(d1/(d1-d3));
  const Vec3  cp = p-c;
  const float d5 = Vec3::dot(ab,cp), d6 = Vec3::dot(ac,cp);
  if(d6>=0 && d5<=d6)
    return c;
  const float vb = d5*d2-d1*d6;
  if(vb<=0 && d2>=0 && d6<=0)
    return a+ac*(d2/(d2-d6));
  const float va = d3*d6-d5*d4;
  if(va<=0 && d4-d3>=0 && d5-d6>=0)
    return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
  const float den = 1.f/(va+vb+vc);
  return a+ab*(vb*den)+ac*(vc*den);
  }
}

int PhysicsWorld::touchTrack(const Vec3& centre, float reach, TrackTouch* out, int max) const {
  auto& tris = coll.triangles();
  if(seen.size()!=tris.size())
    seen.assign(tris.size(),0);
  if(++query==0) {
    std::fill(seen.begin(),seen.end(),0);
    query = 1;
    }
  int n = 0;
  const Vec3 r = {reach,reach,reach};
  coll.forEachNear(centre-r,centre+r,[&](uint32_t i) {
    if(seen[i]==query)
      return;
    seen[i] = query;
    auto& t = tris[i];
    if(t.surface==Surface::Water)
      return;
    const Vec3  p = nearestOnTriangle(centre,t.a,t.b,t.c);
    const Vec3  d = centre-p;
    const float l = d.length();
    if(l>=reach)
      return;
    TrackTouch tt;
    tt.point    = p;
    tt.distance = l;
    tt.normal   = l>1e-6f ? d*(1.f/l) : t.normal;
    tt.sound    = t.sound;
    // keep the nearest `max`, in order
    int k = std::min(n,max-1);
    if(n==max && out[k].distance<=l)
      return;
    while(k>0 && out[k-1].distance>l) {
      out[k] = out[k-1];
      --k;
      }
    out[k] = tt;
    n = std::min(n+1,max);
    });
  return n;
  }

bool PhysicsWorld::crossesTrack(const Vec3& from, const Vec3& to) const {
  return coll.raycast(from,to-from,1.f).has_value();
  }

Vehicle::Vehicle(PhysicsWorld& w, const CarGeometry& g, const CarParams& p, const Mat4& start)
  : owner(w), geom(g), prm(p), rig(g,p) {
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

  // the shape other bodies find the car by: the spheres of cdt<N>.ini, and the wheels' spheres, which
  // Vehicle::suspension keeps where the wheels are (a little higher, so that the ground under a wheel is left to
  // its spring)
  shape = std::make_unique<btCompoundShape>();
  auto  sphere   = [&](const Vec3& at, float r) {
    sphereShapes.push_back(std::make_unique<btSphereShape>(r*S));
    btTransform local;
    local.setIdentity();
    local.setOrigin(toBt(at-com)*S);
    shape->addChildShape(local,sphereShapes.back().get());
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
  inertiaBody = {m12*(size.y*size.y+size.z*size.z),
                 m12*(size.x*size.x+size.z*size.z),
                 m12*(size.x*size.x+size.y*size.y)};
  if(prm.car==2)
    inertiaBody.z *= 2;

  // kinematic and without a response of its own: Bullet neither moves it nor pushes anything with it, but finds
  // what it touches, the other kinematic bodies (people, the road traffic, the other cars) included
  motion = std::make_unique<btDefaultMotionState>();
  btRigidBody::btRigidBodyConstructionInfo ci(0,motion.get(),shape.get());
  body = std::make_unique<btRigidBody>(ci);
  body->setCollisionFlags(body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT|
                          btCollisionObject::CF_NO_CONTACT_RESPONSE);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setUserIndex(PhysicsWorld::CarTag);
  owner.bullet().addRigidBody(body.get(),btBroadphaseProxy::DefaultFilter,btBroadphaseProxy::AllFilter);

  owner.cars.push_back(this);

  tank = prm.boostTank;
  reset(start);
  // RCCars.exe (0x4f27a0) lets a car it puts on the track settle for 2 s before anything else: 100 steps of
  // 0.02 s under gravity and the springs only
  settling = true;
  for(int i=0; i<100; ++i)
    step(0.02f);
  settling   = false;
  stuckFor   = 0;
  stepTouch  = Contact{};
  slowFor    = calmFor = touchFor = 0;
  sinceTouch = 2e6f;
  }

Vehicle::~Vehicle() {
  owner.cars.erase(std::remove(owner.cars.begin(),owner.cars.end(),this),owner.cars.end());
  owner.bullet().removeRigidBody(body.get());
  }

namespace {
// the rotation of a Mat4 as a quaternion w, x, y, z
void toQuat(const Mat4& m, float q[4]) {
  const float t = m.m[0][0]+m.m[1][1]+m.m[2][2];
  if(t>0) {
    const float s = std::sqrt(t+1.f)*2.f;
    q[0] = 0.25f*s;
    q[1] = (m.m[2][1]-m.m[1][2])/s;
    q[2] = (m.m[0][2]-m.m[2][0])/s;
    q[3] = (m.m[1][0]-m.m[0][1])/s;
    }
  else if(m.m[0][0]>m.m[1][1] && m.m[0][0]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[0][0]-m.m[1][1]-m.m[2][2])*2.f;
    q[0] = (m.m[2][1]-m.m[1][2])/s;
    q[1] = 0.25f*s;
    q[2] = (m.m[0][1]+m.m[1][0])/s;
    q[3] = (m.m[0][2]+m.m[2][0])/s;
    }
  else if(m.m[1][1]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[1][1]-m.m[0][0]-m.m[2][2])*2.f;
    q[0] = (m.m[0][2]-m.m[2][0])/s;
    q[1] = (m.m[0][1]+m.m[1][0])/s;
    q[2] = 0.25f*s;
    q[3] = (m.m[1][2]+m.m[2][1])/s;
    }
  else {
    const float s = std::sqrt(1.f+m.m[2][2]-m.m[0][0]-m.m[1][1])*2.f;
    q[0] = (m.m[1][0]-m.m[0][1])/s;
    q[1] = (m.m[0][2]+m.m[2][0])/s;
    q[2] = (m.m[1][2]+m.m[2][1])/s;
    q[3] = 0.25f*s;
    }
  }

Mat4 fromQuat(const float q[4]) {
  const float w = q[0], x = q[1], y = q[2], z = q[3];
  Mat4 m;
  m.m[0][0] = 1-2*(y*y+z*z); m.m[0][1] = 2*(x*y-w*z);   m.m[0][2] = 2*(x*z+w*y);
  m.m[1][0] = 2*(x*y+w*z);   m.m[1][1] = 1-2*(x*x+z*z); m.m[1][2] = 2*(y*z-w*x);
  m.m[2][0] = 2*(x*z-w*y);   m.m[2][1] = 2*(y*z+w*x);   m.m[2][2] = 1-2*(x*x+y*y);
  return m;
  }

// q turned by the angular velocity w for t s (0x4f5400: dq/dt = 1/2 (0, w) q), normalised (0x4078d0)
void turned(const float q[4], const Vec3& w, float t, float out[4]) {
  const float h = 0.5f*t;
  out[0] = q[0] + h*(-w.x*q[1]-w.y*q[2]-w.z*q[3]);
  out[1] = q[1] + h*( w.x*q[0]+w.y*q[3]-w.z*q[2]);
  out[2] = q[2] + h*( w.y*q[0]+w.z*q[1]-w.x*q[3]);
  out[3] = q[3] + h*( w.z*q[0]+w.x*q[2]-w.y*q[1]);
  const float l = std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]+out[3]*out[3]);
  if(l>=1e-6f)
    for(int i=0; i<4; ++i)
      out[i] /= l;
  }
}

void Vehicle::set(const Body& b) {
  st  = b;
  rot = fromQuat(st.q);
  vel = st.P*(1.f/prm.mass);
  omega = invInertia(st.L);
  }

Vec3 Vehicle::invInertia(const Vec3& v) const {
  const Vec3 l = {Vec3::dot(axis(0),v)/inertiaBody.x,Vec3::dot(axis(1),v)/inertiaBody.y,Vec3::dot(axis(2),v)/inertiaBody.z};
  return axis(0)*l.x+axis(1)*l.y+axis(2)*l.z;
  }

Vec3 Vehicle::inertia(const Vec3& v) const {
  const Vec3 l = {Vec3::dot(axis(0),v)*inertiaBody.x,Vec3::dot(axis(1),v)*inertiaBody.y,Vec3::dot(axis(2),v)*inertiaBody.z};
  return axis(0)*l.x+axis(1)*l.y+axis(2)*l.z;
  }

Vec3 Vehicle::pointVelocity(const Vec3& p) const {
  return vel+Vec3::cross(omega,p-st.x);
  }

void Vehicle::syncChassis() {
  Mat4 m = rot;
  m.m[0][3] = st.x.x; m.m[1][3] = st.x.y; m.m[2][3] = st.x.z;
  const btTransform t = toBt(m);
  body->setWorldTransform(t);
  motion->setWorldTransform(t);
  }

void Vehicle::reset(const Mat4& start) {
  // drop in from slightly above the given pose so the springs settle
  place(start*Mat4::translate(Vec3{0,0.02f,0}),{},{});
  }

void Vehicle::place(const Mat4& pose, const Vec3& velocity, const Vec3& spin) {
  Body b;
  b.x = pose.point(com);
  toQuat(pose,b.q);
  set(b);
  st.P = velocity*prm.mass;
  st.L = inertia(spin);
  set(st);
  syncChassis();
  frameForce = frameTorque = {};
  stepTouch  = Contact{};
  stuckFor   = 0;
  slowFor    = calmFor = touchFor = 0;
  sinceTouch = 2e6f;
  for(auto& wh:wheels) {
    wh.length  = prm.springLength-prm.springSag;
    wh.rate    = 0;
    wh.contact = false;
    wh.blocked = false;
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
  sinceProp    = 1e3f;
  touch        = Contact{};
  bump         = false;
  carBefore    = false;
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
  const float v  = speed();
  const float ls = vel.length();
  frameForce = frameTorque = {};
  // the keys that keep the car awake (0x4f59a0)
  keysHeld = in.throttle!=0 || in.steer!=0 || in.boost;

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
  climb = 0;
  if(!braking && in.throttle>0 && gear>0) {
    if(float a = prm.acceleration(v,in.throttle,boostOn); a>0.001f) {
      drive = a + resist;
      climb = prm.climbing(v,in.throttle);
      }
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
    const Vec3 up = axis(1);
    if(up.y<std::cos(CarParams::JumpFlipAngle*3.14159265f/180.f)) {
      recover();
      jumpTimer = 0;
      }
    else if(onGround()) {
      st.P = st.P+up*(prm.jumpSpeed*prm.mass);
      Vec3 w = omega;
      w.y *= 1.5f;
      st.L = inertia(w);
      set(st);
      jumpTimer = 0;
      }
    }

  surfaces(dt);
  hits(dt);
  }

// RCCars.exe (0x4f6ea0): the frame time in steps, one step for a frame shorter than 0.02 s, else steps of 0.02 s
// while more than 0.038 s is left and the rest in two halves. Ours: each of them in equal parts of at most
// MaxStep, since our springs lose and find the ground over and over (the car shakes and never comes to rest) with
// steps as long as a frame
void Vehicle::advance(float dt) {
  constexpr float MaxStep = 1.f/120.f;
  auto steps = [this](float h) {
    const int n = std::max(1,int(std::ceil(h/MaxStep-1e-4f)));
    for(int i=0; i<n; ++i)
      step(h/float(n));
    };
  if(dt<=0)
    return;
  if(dt<0.02f) {
    steps(dt);
    return;
    }
  float rest = dt;
  if(dt>0.038f)
    do {
      steps(0.02f);
      rest -= 0.02f;
      } while(rest>0.038f);
  steps(0.5f*rest);
  steps(0.5f*rest);
  }

// RCCars.exe holds a car still (0x4f59a0, 0x4f6610) once, with no key held, it has gone slower than 1.3 km/h and
// turned slower than 1 rad/s for 2 s, touching the track all along for 1 s: it zeroes its momenta and skips the
// step (0x4f5e50), as long as a sphere is within 3 cm of the track
bool Vehicle::asleep(float h) {
  constexpr float SleepSpeed = 0.3611111f; // m/s, 1.3 km/h
  constexpr float SleepSpin  = 1.f;        // rad/s
  if(keysHeld) {
    slowFor = calmFor = touchFor = 0;
    sinceTouch = 2e6f;
    }
  const float v = std::max(vel.length(),st.P.length());
  slowFor = v<SleepSpeed ? slowFor+h : 0.f;
  calmFor = omega.length()<SleepSpin ? calmFor+h : 0.f;
  sinceTouch += h;
  touchFor = sinceTouch<0.1f ? touchFor+h : 0.f;
  if(!(slowFor>2.f && calmFor>2.f && touchFor>1.f))
    return false;
  st.P = st.L = {};
  set(st);
  if(near(AllSpheres,0.03f))
    sinceTouch = 0;
  return true;
  }

// The physics of RCCars.exe's car (0x4f5e50), one step of `h` s: the body moves (Vehicle::move) under the forces
// (Vehicle::forces), and the springs follow the ground after it (Vehicle::suspension)
void Vehicle::step(float h) {
  if(h<=0)
    return;
  if(!settling && asleep(h)) {
    syncChassis();
    return;
    }
  move(h);
  syncChassis();
  }

// RCCars.exe's forces on the car (0x4f0910), anew for every Euler step of the body:
//  - gravity;
//  - the air and the bearings (0x4ed970): coeffAirResistance v^2 + coeffFrictionBearings against the motion;
//  - the springs (0x4f0f80): each wheel touching the ground pushes the body along its up axis at the top of the
//    spring with springK * (springLength - length - 0.02 r) - springDamping * (rate of change of the length), as
//    long as the spring is shorter than that; the damper may outdo the spring and pull the body down (it is held
//    below 2e6 x the spring's force, which it never reaches). The body is pushed by what of it is along the ground's normal, and
//    turned by the spring's moment about the car's own axes, the pitch times springPitch, the roll times
//    springRoll, none about the up axis;
//  - the engine (0x4eea50): `drive` (at most 20 x the rear grip, drifting included) on each driven wheel standing
//    on ground less steep than 46 degrees, half each, along the ground under the wheel; with `climb` added
//    (0x4ee5e0, CarParams::climb);
//  - the tires (Vehicle::tires).
// A car being placed (settling) only has gravity and the springs.
void Vehicle::forces(float h) {
  const Vec3 up  = axis(1);
  const Vec3 fwd = axis(2);
  const Vec3 c   = st.x;
  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += c.x; m.m[1][3] += c.y; m.m[2][3] += c.z;

  Vec3 force{0,-PhysicsWorld::Gravity*prm.mass,0}, torque;

  // air and bearings
  if(!settling)
    if(const float ls = vel.length(); ls>1e-4f)
      force = force-vel*(prm.mass*(prm.airDrag*ls*ls+prm.rolling)/ls);

  // springs
  Vec3 springs;
  for(auto& wh:wheels) {
    const float l = wh.length+0.02f*wh.radius;
    if(!wh.contact || l>prm.springLength)
      continue;
    const float k = springK*(prm.springLength-l);
    const float f = k - prm.springDamping*wh.rate;
    const Vec3 at = m.point(wh.mount);
    const Vec3 fs = up*f;
    const Vec3 fn = wh.normal*Vec3::dot(fs,wh.normal);
    springs = springs+fn;
    const Vec3 t = Vec3::cross(at-c,fs);
    torque = torque+axis(0)*(Vec3::dot(axis(0),t)*prm.springPitch)+axis(2)*(Vec3::dot(axis(2),t)*prm.springRoll);
    }
  force = force+springs;

  if(!settling) {
    // engine
    if(!braking && drive!=0) {
      const float grip = prm.gripRear*(drifting() ? prm.driftGrip : 1.f);
      const float a    = std::clamp(drive,-20.f*grip,20.f*grip)*prm.mass;
      for(int i:rearPair) {
        if(i<0)
          continue;
        const Wheel& wh = wheels[size_t(i)];
        if(!wh.contact || wh.normal.y<std::cos(46.f*Pi/180.f))
          continue;
        const Vec3 d = (fwd-wh.normal*Vec3::dot(fwd,wh.normal)).normalized();
        Vec3 f = d*(0.5f*a);
        // climbing: against what of the springs' push lies along d on the level, the drive less than 80 degrees
        // from the vertical
        const float back = springs.x*d.x+springs.z*d.z;
        if(climb!=0 && d.y>std::cos(80.f*Pi/180.f) && back<0) {
          const float c = 0.5f*climb*prm.mass;
          f = f+d*(c<=0 || c<=-back ? c : -back);
          }
        force  = force+f;
        torque = torque+Vec3::cross(wh.point-c,f);
        }
      }
    tires(h,force,torque);
    force  = force+frameForce;
    torque = torque+frameTorque;
    }

  moveForce  = force;
  moveTorque = torque;
  }

// RCCars.exe's car meets the track (0x4f5e50): the step is cut into at most 3 parts, each one
//  - first holds the momentum below 20 kg m/s (20 m/s) and the angular momentum below 1 kg m^2/s (0x4f5770);
//  - moves the body by an Euler step (0x4f5590) as far as is left of the step, or, where a sphere of the car would
//    go through the track on the way, as far as no point of a sphere moves more than 0.9 of its radius at its
//    velocity, halved until none moves more than 0.99 of it (0x4f5b60);
//  - looks whether a body sphere is in the track after it (0x4efdc0), or a wheel's, where a spring cannot take
//    the ground in (0x4fb340). Then it goes back and finds the moment of the hit, between the step and none, in
//    5 tries: 0.2 of the way from what is free to what is not the first time and halfway after, until a sphere
//    is within 6 mm of the track (0x4f0270). Then the first body sphere touching the track loses its sliding
//    along it, up to 10 m/s x the time moved (0x4f0560), and every touch is pushed out of the track until it
//    leaves it at 0.05 m/s (0x4f0750).
// What is left after the parts is lost; a car that got less than 5 % of the step done is lifted 2^n x 3 mm
// (or, if that is in the track, put 15 mm lower) for the n-th step in a row, and after StuckSteps of them the
// game puts it back at the last checkpoint (0x508b30).
namespace {
constexpr float TouchMargin  = 0.006f;  // m (0x3bc49ba6)
constexpr float WheelSink    = 0.002f;  // m a wheel's sphere may be in the track: it stands on it (ours)
constexpr float LeaveSpeed   = 0.05f;   // m/s (0x554514)
constexpr float StillSpeed   = 0.02f;   // m/s (0x55458c)
}

Vehicle::Body Vehicle::advanced(const Body& from, float h) {
  // the velocities of `from`: the body is at `from` whenever this is called
  forces(h);
  Body to = from;
  to.x = from.x+vel*h;
  turned(from.q,omega,h,to.q);
  to.P = from.P+moveForce*h;
  to.L = from.L+moveTorque*h;
  return to;
  }

void Vehicle::spheres(std::vector<Sphere>& out, Which which) const {
  out.clear();
  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  if(which&WheelSpheres)
    for(auto& wh:wheels)
      out.push_back({m.point(wh.mount-Vec3{0,wh.length,0}),wh.radius,true});
  if(which&BodySpheres)
    for(auto& b:prm.body)
      out.push_back({m.point(b.at),b.radius,false});
  }

bool Vehicle::penetrates(Which which) const {
  auto& sp = const_cast<std::vector<Sphere>&>(sphereBuf[0]);
  spheres(sp,AllSpheres);
  PhysicsWorld::TrackTouch t;
  for(size_t i=0; i<sp.size(); ++i) {
    auto& s = sp[i];
    if(!(which&(s.wheel ? WheelSpheres : BodySpheres)) || (i<inside.size() && inside[i]))
      continue;
    if(owner.touchTrack(s.c,s.wheel ? s.r-WheelSink : s.r,&t,1)>0)
      return true;
    }
  return false;
  }

void Vehicle::findInside() {
  inside.clear();
  spheres(sphereBuf[0],AllSpheres);
  PhysicsWorld::TrackTouch t;
  for(auto& s:sphereBuf[0])
    inside.push_back(owner.touchTrack(s.c,s.wheel ? s.r-WheelSink : s.r,&t,1)>0);
  }

bool Vehicle::near(Which which, float margin) const {
  auto& sp = const_cast<std::vector<Sphere>&>(sphereBuf[0]);
  spheres(sp,which);
  PhysicsWorld::TrackTouch t;
  for(auto& s:sp)
    if(owner.touchTrack(s.c,s.r+margin,&t,1)>0)
      return true;
  return false;
  }

void Vehicle::touches(float margin, std::vector<Touch>& out) const {
  constexpr size_t MaxTouches = 50;
  out.clear();
  auto& sp = const_cast<std::vector<Sphere>&>(sphereBuf[0]);
  spheres(sp,AllSpheres);
  PhysicsWorld::TrackTouch t[5];
  for(auto& s:sp) {
    const int n = owner.touchTrack(s.c,s.r+margin,t,5);
    for(int i=0; i<n && out.size()<MaxTouches; ++i)
      out.push_back({s.c-t[i].normal*s.r,t[i].normal,s.wheel,t[i].sound});
    }
  }

float Vehicle::sweep(const Body& from, float rest) {
  auto& a = sphereBuf[1];
  spheres(a,AllSpheres);
  // the fastest a point of a sphere goes, at the start
  float most = 2e6f;
  for(auto& s:a) {
    const float v = pointVelocity(s.c).length();
    most = std::min(most,v>0.001f ? 0.9f*s.r/v : 1.f);
    }
  set(advanced(from,rest));
  auto& b = sphereBuf[0];
  spheres(b,AllSpheres);
  bool through = false;
  for(size_t i=0; i<a.size() && !through; ++i)
    through = owner.crossesTrack(a[i].c,b[i].c);
  if(!through)
    return rest;
  float h = std::min(most,rest);
  for(int i=0; ; ++i) {
    set(from);
    set(advanced(from,h));
    spheres(b,AllSpheres);
    bool far = false;
    for(size_t k=0; k<a.size() && !far; ++k)
      far = (b[k].c-a[k].c).length()>0.99f*a[k].r;
    if(!far)
      return h;
    h *= 0.5f;
    if(i>=9) {
      set(from);
      return 0;
      }
    }
  }

float Vehicle::impact(const Body& from, float hi, std::vector<Touch>& out) {
  float lo = 0, w = 0.8f;
  bool  found = false;
  for(int i=0; i<5 && !found; ++i) {
    const float t = lo*w+(1.f-w)*hi;
    set(from);
    set(advanced(from,t));
    if(penetrates(AllSpheres)) {
      hi = t;
      w  = 0.8f;
      continue;
      }
    lo = t;
    w  = 0.5f;
    touches(TouchMargin,out);
    found = !out.empty();
    }
  set(from);
  set(advanced(from,lo));
  if(!found)
    touches(TouchMargin,out);
  return lo;
  }

void Vehicle::slide(const std::vector<Touch>& at, float t) {
  for(auto& c:at) {
    if(c.wheel)
      continue;
    const Vec3  v  = pointVelocity(c.point);
    const Vec3  vt = v-c.normal*Vec3::dot(v,c.normal);
    const float l  = vt.length();
    if(l<1e-6f)
      continue;
    applyImpulse(vt*(-std::min(l,std::max(10.f*t,0.f))/l),c.point);
    return;
    }
  }

void Vehicle::resolve(const std::vector<Touch>& at) {
  const float im = 1.f/prm.mass;
  for(int pass=0; pass<10; ++pass) {
    bool again = false;
    for(auto& c:at) {
      const float vn = Vec3::dot(pointVelocity(c.point),c.normal);
      if(vn>StillSpeed)
        continue;
      again = true;
      const Vec3  r = c.point-st.x;
      const float k = im+Vec3::dot(Vec3::cross(invInertia(Vec3::cross(r,c.normal)),r),c.normal);
      if(k<1e-6f)
        continue;
      applyImpulse(c.normal*(std::max(LeaveSpeed-vn,0.f)/k),c.point);
      }
    if(!again)
      break;
    }
  }

void Vehicle::unstick() {
  const Body was = st;
  Body b = st;
  b.x.y += std::pow(2.f,float(std::min(stuckFor,StuckSteps)))*0.003f;
  set(b);
  if(penetrates(AllSpheres)) {
    b.x.y = was.x.y-0.015f;
    set(b);
    if(penetrates(AllSpheres))
      set(was);
    }
  stuckFor = std::min(stuckFor+1,StuckSteps);
  }

void Vehicle::move(float h) {
  float rest = h;
  for(int part=0; part<3 && rest>1e-4f; ++part) {
    // 0x4f5770
    if(const float p = st.P.length(); p>20.f*prm.mass)
      st.P = st.P*(20.f*prm.mass/p);
    if(const float l = st.L.length(); l>1.f)
      st.L = st.L*(1.f/l);
    set(st);

    // a wheel on the track keeps the car's stillness timer going (0x4f0230, 0x4f5980)
    if(near(WheelSpheres,TouchMargin))
      sinceTouch = 0;

    const Body from = st;
    // ours: a sphere already in the track (a wheel the spring has put by a wall) does not stop the move, it is
    // only pushed out of it
    findInside();
    float t = std::clamp(sweep(from,rest),0.f,rest);
    // a body sphere in the track, or else, after the springs have followed the ground, a wheel's where a spring
    // cannot take it in
    bool hit = penetrates(BodySpheres);
    if(!hit && suspension(t,false))
      hit = penetrates(WheelSpheres);
    if(hit) {
      sinceTouch = 0;
      // what the sounds hear of it (0x4f6730): where it went in, at the speed it went in
      touches(0,touchBuf);
      for(auto& c:touchBuf)
        note(stepTouch,pointVelocity(c.point),c.normal,c.sound);
      t = impact(from,t,touchBuf);
      if(!touchBuf.empty()) {
        slide(touchBuf,t);
        resolve(touchBuf);
        }
      suspension(t,true);
      }
    rest -= t;
    }
  inside.clear();
  if(rest>0.95f*h)
    unstick();
  else
    stuckFor = 0;
  }

// RCCars.exe (0x4fb340): a wheel pushed into the ground goes up at once, to springMin at most; otherwise it goes
// down towards its rest, 10 times faster once the car has gone faster than 5 km/h for 0.5 s, and stops on the
// ground. At rest is springLength, but 0.95 springSag shorter for a car slower than 5 km/h after a move that met
// the track, or with a spring that cannot take the ground in. It goes down by r (the wheel's radius) per second,
// r (1 + 18 t) after t s faster than 5 km/h, so 10 r from 0.5 s on (0x8 of the car's data counts t); 10 r while
// the car is being placed. The wheel is swept down from the top of its spring, so it also climbs onto a step in
// front of it.
bool Vehicle::suspension(float h, bool hit) {
  constexpr float Moving    = 1.388889f; // m/s, 5 km/h
  constexpr float WheelLift = 0.3f;
  const Vec3  v     = velocity();
  const float speed = v.length();
  movingTime = speed>=Moving ? movingTime+h : 0.f;

  // the track only: RCCars.exe's wheels do not stand on the props (0x4fbd70 asks the static world), they bump into
  // them (Vehicle::hits)
  struct Sweep : btCollisionWorld::ClosestConvexResultCallback {
    const btCollisionObject* ground;
    Sweep(const btVector3& f, const btVector3& t, const btCollisionObject* g)
      : ClosestConvexResultCallback(f,t), ground(g) {}
    bool needsCollision(btBroadphaseProxy* proxy) const override {
      return proxy->m_clientObject==ground && ClosestConvexResultCallback::needsCollision(proxy);
      }
    };

  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  const Vec3      up    = axis(1);
  const Vec3      fwd   = axis(2);
  const float     reach = std::max(prm.springMax,prm.springLength);
  // how far down each sphere can go from the top of its spring before it touches the ground
  struct Ground { float d = 1e9f; Vec3 point, normal; };
  Ground found[16];
  bool   blocked = false;
  for(size_t i=0; i<wheels.size() && i<std::size(found); ++i) {
    auto&      wh  = wheels[i];
    const Vec3 top = m.point(wh.mount);
    btTransform from, to;
    from.setIdentity();
    to.setIdentity();
    from.setOrigin(toBt(top)*S);
    to.setOrigin(toBt(top-up*reach)*S);
    Sweep cb(from.getOrigin(),to.getOrigin(),&owner.ground());
    owner.bullet().convexSweepTest(wh.front ? wheelShape[0].get() : wheelShape[1].get(),from,to,cb);
    if(cb.hasHit()) {
      found[i].d      = float(cb.m_closestHitFraction)*reach;
      found[i].point  = fromBt(cb.m_hitPointWorld)*(1.f/S);
      found[i].normal = fromBt(cb.m_hitNormalWorld).normalized();
      }
    wh.blocked = found[i].d<prm.springMin;
    blocked    = blocked || wh.blocked;
    }
  float rest = prm.springLength;
  if(speed<Moving && (hit || blocked)) {
    rest       = prm.springLength-0.95f*prm.springSag;
    movingTime = 0;
    }
  for(size_t i=0; i<wheels.size() && i<std::size(found); ++i) {
    auto&       wh     = wheels[i];
    const float before = wh.length;
    const float ground = found[i].d;

    float rate = wh.radius;
    if(settling)
      rate *= 10.f;
    else if(speed>=Moving)
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
    if(h>1e-6f)
      wh.rate = (wh.length-before)/h;
    if(wh.contact) {
      wh.point  = found[i].point;
      wh.normal = found[i].normal;
      }
    // the wheel's sphere in the shape, for the bodies the wheel runs into
    btTransform local;
    local.setIdentity();
    local.setOrigin(toBt(wh.mount-Vec3{0,std::max(wh.length-WheelLift*wh.radius,prm.springMin),0}-com)*S);
    shape->updateChildTransform(int(prm.body.size())+int(&wh-wheels.data()),local,false);
    // rolling, for drawing
    if(wh.contact && !braking)
      wh.spin = std::fmod(wh.spin+Vec3::dot(v,fwd)/wh.radius*h,2.f*Pi);
    }
  shape->recalculateLocalAabb();
  // a spring that cannot take the ground in gives the dampers no rate of change (0x4fb340)
  if(blocked)
    for(auto& wh:wheels)
      wh.rate = 0;
  return blocked;
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

  const Vec3  up   = axis(1);
  const Vec3  fwd  = axis(2);
  const Vec3  c    = st.x;
  auto  inv  = [&](const Vec3& v) { return invInertia(v); };
  const float im   = 1.f/prm.mass;
  const bool  drift = drifting();

  // the acceleration of the body by the other forces (gravity is among them, Vehicle::forces), and its angular
  // acceleration with the gyroscopic part (0x476ba0)
  const Vec3 a  = force*im;
  const Vec3 al = inv(torque-Vec3::cross(omega,st.L));

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
    const Vec3 pv = pointVelocity(wh.point);
    d[k] = Vec3::cross(heading,wh.normal).normalized();
    if(braking && pv.length()>=1e-6f) {
      const Vec3 u     = pv.normalized();
      const Vec3 slide = u-wh.normal*Vec3::dot(u,wh.normal);
      if(slide.length()>=1e-6f)
        d[k] = slide.normalized();
      }
    const float grip = wh.front ? prm.gripFront : prm.gripRear*(drift ? prm.driftGrip : 1.f);
    const float lim  = (!wh.front || drift ? 10.f : 5.f)*grip;
    // the acceleration of the wheel's point along d, d turning with the car (0x476c50): with the centripetal part
    // and the velocity along the turning d
    const Vec3  ap = a+Vec3::cross(al,r[k])+Vec3::cross(omega,Vec3::cross(omega,r[k]));
    const float ad = Vec3::dot(ap,d[k])+Vec3::dot(pv,Vec3::cross(omega,d[k]));
    b[k] = std::clamp(-ad,-lim,lim);
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
    }
  if(std::abs(steerDeg)<0.2f) {
    torque = torque-st.L;
    }
  }

namespace {
// what RCCars.exe needs of a body to push it (0x4754a0, 0x4756c0)
struct Pushed {
  std::function<float()>            invMass;
  std::function<Vec3(const Vec3&)>  invInertia;  // world
  Vec3                              centre;
  std::function<Vec3()>             velocity;    // of the centre of mass
  std::function<void(const Vec3&,const Vec3&)> push; // impulse, at
  };

// RCCars.exe 0x4f0750 for one contact of two bodies at `at`, its normal `n` out of b towards a (0x534be0): over
// up to 10 passes, the first always, while they part at most at 0.02 m/s (their centres' velocities, those
// parting at less than 1 m/s counted as not parting), each pass pushes them apart by an impulse that would part
// them at twice the speed they meet at, at least 0.2 m/s (0x5543ec, 0x5544f0), equal and opposite (0x4f0840)
void exchange(Pushed& a, Pushed& b, const Vec3& at, const Vec3& n) {
  const Vec3 ra = at-a.centre, rb = at-b.centre;
  for(int pass=0; pass<10; ++pass) {
    float vn = Vec3::dot(a.velocity()-b.velocity(),n);   // 0x4f04e0
    if(vn>0 && vn<1.f)
      vn = 0;
    if(pass>0 && vn>StillSpeed)
      break;
    const float dv = std::max(-2.f*vn,0.2f);
    const float k  = a.invMass()+Vec3::dot(Vec3::cross(a.invInertia(Vec3::cross(ra,n)),ra),n)
                   + b.invMass()+Vec3::dot(Vec3::cross(b.invInertia(Vec3::cross(rb,n)),rb),n);
    if(k<1e-6f)
      return;
    const Vec3 j = n*(dv/k);
    a.push(j,at);
    b.push(j*-1.f,at);
    }
  }

// the line from `from` to `to` as 0x534be0 makes a contact of it: through the middle, the normal towards `to`
bool contactOf(const Vec3& from, const Vec3& to, Vec3& at, Vec3& n) {
  const Vec3  d = to-from;
  const float l = d.length();
  at = (from+to)*0.5f;
  if(l<1e-6f)
    return false;
  n = d*(1.f/l);
  return true;
  }
}

void Vehicle::bounce(Vehicle& a, Vehicle& b, const Vec3& aCentre, const Vec3& bCentre) {
  Vec3 at, n;
  if(!contactOf(bCentre,aCentre,at,n))
    return;
  auto of = [](Vehicle& v) {
    Pushed p;
    p.invMass    = [&v]() { return 1.f/v.prm.mass; };
    p.invInertia = [&v](const Vec3& x) { return v.invInertia(x); };
    p.centre     = v.st.x;
    p.velocity   = [&v]() { return v.vel; };
    p.push       = [&v](const Vec3& j, const Vec3& at) { v.applyImpulse(j,at); };
    return p;
    };
  Pushed pa = of(a), pb = of(b);
  exchange(pa,pb,at,n);
  }

// RCCars.exe gives the player's car a hit, shown as "Łup!" or "Wielkie Łup!" (msg_hits.csi) for a second and worth
// HitBoost turbo units, when it
//  - touches a prop of stone.sb faster than PropHitSpeed, at most every PropHitPause s (0x533ee0);
//  - knocks another car about (Opponents::collide);
//  - hits people, a guard faster than 10 km/h or a dog faster than 15 km/h: a great hit (Characters).
// Road traffic, crabs, spiders and gulls give nothing.
//
// What the car touches pushes it as in the original's handlers (0x534d00), once a frame:
//  - a prop (0x533ee0): the two bounce apart along the line between their centres of mass (exchange);
//  - a person, a guard or a dog (0x534340, 0x5344f0, 0x5349d0): the car alone, along the line from the point
//    touched to its centre of mass, through the middle, until it leaves at 0.05 m/s (0x4f0750);
//  - the road traffic (0x534880) and the other cars (Opponents::collide) have their own.
namespace {
constexpr float PropHitSpeed  = 1.944445f; // m/s, 7 km/h
constexpr float PropHitPause  = 0.3f;
}

void Vehicle::hits(float dt) {
  lastHit    = Hit::None;
  sinceProp += dt;

  // the track, from the steps since the last update
  touch          = stepTouch;
  stepTouch      = Contact{};
  if(touch.surface==0)
    touch.impact = -1;   // none yet

  bool prop = false, car = false;
  auto& disp = *owner.bullet().getDispatcher();
  for(int i=0; i<disp.getNumManifolds(); ++i) {
    const btPersistentManifold* m = disp.getManifoldByIndexInternal(i);
    const btCollisionObject*    other = nullptr;
    if(m->getBody0()==body.get())
      other = m->getBody1();
    else if(m->getBody1()==body.get())
      other = m->getBody0();
    if(other==nullptr || other==&owner.ground())
      continue;
    const btManifoldPoint* deepest = nullptr;
    for(int c=0; c<m->getNumContacts(); ++c)
      if(m->getContactPoint(c).getDistance()<=0 && (deepest==nullptr || m->getContactPoint(c).getDistance()<deepest->getDistance()))
        deepest = &m->getContactPoint(c);
    if(deepest==nullptr)
      continue;
    const int tag = other->getUserIndex();
    car |= tag==PhysicsWorld::CarTag;
    if(tag==PhysicsWorld::PropTag) {
      prop = true;
      contacts(*m,other);
      auto* pr = const_cast<btRigidBody*>(btRigidBody::upcast(other));
      if(pr==nullptr || pr->getInvMass()<=0)
        continue;
      pr->activate(true);
      const Vec3 pc = fromBt(pr->getCenterOfMassPosition())*(1.f/S);
      Vec3 at, n;
      if(!contactOf(pc,st.x,at,n))
        continue;
      Pushed me, it;
      me.invMass    = [this]() { return 1.f/prm.mass; };
      me.invInertia = [this](const Vec3& x) { return invInertia(x); };
      me.centre     = st.x;
      me.velocity   = [this]() { return vel; };
      me.push       = [this](const Vec3& j, const Vec3& p) { applyImpulse(j,p); };
      it.invMass    = [pr]() { return float(pr->getInvMass()); };
      it.invInertia = [pr](const Vec3& x) { return fromBt(pr->getInvInertiaTensorWorld()*toBt(x))*(S*S); };
      it.centre     = pc;
      it.velocity   = [pr]() { return fromBt(pr->getLinearVelocity())*(1.f/S); };
      it.push       = [pr](const Vec3& j, const Vec3& p) {
        pr->applyImpulse(toBt(j)*S,toBt(p)*S-pr->getCenterOfMassPosition());
        };
      exchange(me,it,at,n);
      }
    else if(tag==PhysicsWorld::CharacterTag) {
      const bool first = m->getBody0()==body.get();
      const Vec3 p = fromBt(first ? deepest->getPositionWorldOnB() : deepest->getPositionWorldOnA())*(1.f/S);
      Vec3 at, n;
      if(contactOf(p,st.x,at,n))
        resolve({Touch{at,n,false,0}});
      }
    }
  if(touch.impact<0)
    touch = Contact{};
  bump      = car && !carBefore;
  carBefore = car;

  if(prop && sinceProp>PropHitPause && vel.length()>PropHitSpeed) {
    lastHit   = Hit::Hit;
    sinceProp = 0;
    addBoost(HitBoost);
    }
  }

void Vehicle::note(Contact& into, const Vec3& v, const Vec3& n, uint8_t surface) {
  const float speed = v.length();
  Contact     ct;
  ct.surface = surface;
  ct.impact  = std::abs(Vec3::dot(v,n))*3.6f;
  ct.hit     = speed>1e-6f && Vec3::dot(v,n)/speed < std::cos(145.f*3.14159265f/180.f);
  // the strongest one: a hit before a touch, then the harder one
  if(into.surface==0 || (ct.hit && !into.hit) || (ct.hit==into.hit && ct.impact>into.impact))
    into = ct;
  }

void Vehicle::contacts(const btPersistentManifold& m, const btCollisionObject* other) {
  const bool  first = m.getBody0()==body.get();
  const auto* otherBody = btRigidBody::upcast(other);
  for(int c=0; c<m.getNumContacts(); ++c) {
    const btManifoldPoint& pt = m.getContactPoint(c);
    if(pt.getDistance()>0)
      continue;
    // the normal out of the other body, towards the car
    const Vec3 n  = fromBt(first ? pt.m_normalWorldOnB : -pt.m_normalWorldOnB);
    const Vec3 at = fromBt(first ? pt.getPositionWorldOnA() : pt.getPositionWorldOnB())*(1.f/S);
    Vec3       v  = pointVelocity(at);
    if(otherBody!=nullptr && otherBody->getInvMass()>0)
      v = v-fromBt(otherBody->getVelocityInLocalPoint(toBt(at)*S-otherBody->getCenterOfMassPosition()))*(1.f/S);
    Contact ct = touch.impact<0 ? Contact{} : touch;
    note(ct,v,n,SoundSurface::Wood);
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
    const Vec3  hub  = m.point({geom.wheels[size_t(i)].center.x,wh.mount.y-wh.length,geom.wheels[size_t(i)].center.z});
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
      const uint8_t g = track.soundBelow(wh.point+Vec3{0,0.02f,0},0.05f);
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
  float keep = 1;
  if(wet<=0 && wetNow>0)
    keep *= 1.f-prm.waterDropWheels;
  if(sub<=0 && subNow>0)
    keep *= 1.f-prm.waterDropBody;
  if(keep<1) {
    st.P.x *= keep;
    st.P.z *= keep;
    set(st);
    }

  // deep sand and water hold every wheel in them back: k * v^2 against the motion of that point of the car
  const float v2 = vel.length()*vel.length();
  for(int i=0; i<dragCount; ++i) {
    const Vec3 pv = pointVelocity(drags[i].at);
    if(pv.length()<1e-4f)
      continue;
    const Vec3 f = pv.normalized()*(-prm.mass*drags[i].k*v2);
    frameForce  = frameForce+f;
    frameTorque = frameTorque+Vec3::cross(drags[i].at-st.x,f);
    }

  // under water: everything slows down, falling and tumbling included
  if(subNow>0) {
    const float k = std::pow(prm.waterDamping,dt*30.f*subNow);
    st.P = st.P*k;
    st.L = st.L*k;
    set(st);
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

// the cars are stepped through the whole frame (Vehicle::advance): drawn where they are
Mat4 Vehicle::pose() const {
  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  return m;
  }

Vec3 Vehicle::centre() const {
  return st.x;
  }

Vec3 Vehicle::drawnCentre() const {
  return st.x;
  }

Vec3 Vehicle::velocity() const {
  return vel;
  }

Vec3 Vehicle::localSpin() const {
  return {Vec3::dot(axis(0),omega),Vec3::dot(axis(1),omega),Vec3::dot(axis(2),omega)};
  }

void Vehicle::applyImpulse(const Vec3& impulse, const Vec3& at) {
  st.P = st.P+impulse;
  st.L = st.L+Vec3::cross(at-st.x,impulse);
  set(st);
  }

void Vehicle::addVelocity(const Vec3& dv) {
  st.P = st.P+dv*prm.mass;
  set(st);
  }

void Vehicle::shotFrom(const Vec3& gun) {
  Vec3 away = pose().translation()-gun;
  away.y = 0;
  const float l = away.length();
  away = l>1e-6f ? away*(1.f/l) : Vec3{};
  addVelocity(away*ShotPushOut+Vec3{0,ShotPushUp,0});
  }

void Vehicle::hold(const Mat4& at) {
  Body b;
  b.x = at.point(com);
  toQuat(at,b.q);
  set(b);
  syncChassis();
  }

float Vehicle::speed() const {
  return Vec3::dot(vel,axis(2));
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

CarRig::Pose Vehicle::rigPose() const {
  std::vector<CarRig::Wheel> in(wheels.size());
  for(size_t i=0; i<wheels.size(); ++i) {
    in[i].lift  = wheels[i].mount.y-wheels[i].length-geom.wheels[i].center.y;
    in[i].steer = steerAngle(wheels[i]);
    in[i].spin  = wheels[i].spin*(180.f/Pi);
    }
  return rig.pose(in);
  }

Mat4 Vehicle::wheelPose(size_t i) const {
  return rigPose().wheel(i);
  }
