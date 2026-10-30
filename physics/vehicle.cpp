#include "vehicle.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

#include "props.h"

using namespace rc;

namespace {
constexpr float Pi = 3.14159265f;
}

PhysicsWorld::PhysicsWorld(const CollisionMesh& track) : coll(track) {
  }

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float dt) {
  for(Vehicle* c:cars)
    c->advance(dt);
  // 0x50cb00 -> 0x50f190: a prop moves while a viewer is near it (0x50ec10)
  for(Prop* p:propList)
    p->step(dt,inFront(p->origin()));
  meet();
  }

bool PhysicsWorld::nearViewer(const Vec3& at, float reach) const {
  if(viewers.empty()) {
    for(Vehicle* c:cars)
      if((c->pose().translation()-at).length()<reach)
        return true;
    return false;
    }
  for(auto& v:viewers)
    if((v.translation()-at).length()<reach)
      return true;
  return false;
  }

bool PhysicsWorld::inFront(const Vec3& at) const {
  // 0x50ec10: no more than 20 m below the viewer, within 50 m of it, and at most 90 degrees to the left of where it
  // looks across the ground (the angle has a sign, 0x410150: anything to the right counts too)
  auto check = [&](const Mat4& v) {
    const Vec3 p = v.translation(), f = v.vector({0,0,1});
    if(at.y<p.y-20.f)
      return false;
    const Vec3 d = at-p;
    if(d.length()>50.f)
      return false;
    const float dl = std::sqrt(d.x*d.x+d.z*d.z), fl = std::sqrt(f.x*f.x+f.z*f.z);
    if(dl<1e-6f || fl<1e-6f)
      return true;
    float a = std::acos(std::clamp((d.x*f.x+d.z*f.z)/(dl*fl),-1.f,1.f))*180.f/Pi;
    if(d.z*f.x-f.z*d.x<0)
      a = -a;
    return a<=90.f;
    };
  if(viewers.empty()) {
    for(Vehicle* c:cars)
      if(check(c->pose()))
        return true;
    return false;
    }
  for(auto& v:viewers)
    if(check(v))
      return true;
  return false;
  }

void PhysicsWorld::meet() {
  // 0x533340: the objects within 10 m of a viewer; 0x533200 / 0x5335a0: each pair of them closer than 2.5 m, by
  // the first two of their spheres that touch
  constexpr float Around = 10.f;
  std::vector<Prop*> near;
  for(Prop* p:propList)
    if(nearViewer(p->origin(),Around))
      near.push_back(p);
  if(near.empty())
    return;
  auto& a = ballBuf[0];
  auto& b = ballBuf[1];
  for(Vehicle* c:cars) {
    const Vec3 at = c->pose().translation();
    if(!nearViewer(at,Around))
      continue;
    c->balls(a);
    for(Prop* p:near) {
      if((p->origin()-at).length()>=BallsReach)
        continue;
      p->balls(b);
      if(ballsMeet(a.data(),a.size(),b.data(),b.size()))
        c->meet(*p,rngState);
      }
    }
  for(size_t i=0; i<near.size(); ++i) {
    near[i]->balls(a);
    for(size_t j=i+1; j<near.size(); ++j) {
      if((near[j]->origin()-near[i]->origin()).length()>=BallsReach)
        continue;
      near[j]->balls(b);
      if(!ballsMeet(a.data(),a.size(),b.data(),b.size()))
        continue;
      // 0x534240: both sound, and bounce apart along the line between their centres of mass
      Prop& p = *near[i];
      Prop& q = *near[j];
      p.knock();
      q.knock();
      Vec3 at, n;
      if(contactOf(p.centre(),q.centre(),at,n))
        Vehicle::exchange(p,q,at,n);
      p.balls(a);
      }
    }
  }

void PhysicsWorld::pushProps(const std::vector<Ball>& balls, const Vec3& origin) {
  auto& b = ballBuf[1];
  for(Prop* p:propList) {
    if((p->origin()-origin).length()>=BallsReach)
      continue;
    p->balls(b);
    Vec3 n;
    if(!ballsMeet(b.data(),b.size(),balls.data(),balls.size(),nullptr,&n))
      continue;
    p->knock();
    p->applyImpulse(n*(1.f/p->invMass()),p->centre());
    }
  }

bool PhysicsWorld::touchesProp(const std::vector<Ball>& balls, const Vec3& origin) const {
  std::vector<Ball> b;
  for(Prop* p:propList) {
    if((p->origin()-origin).length()>=BallsReach)
      continue;
    p->balls(b);
    if(ballsMeet(balls.data(),balls.size(),b.data(),b.size()))
      return true;
    }
  return false;
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

  // inertia (0x4f2270, 0x4752b0): a solid box over the whole model, the Buggy's about its forward axis doubled
  const Vec3 size = geom.max-geom.min;
  const float m12 = prm.mass/12.f;
  inertiaBody = {m12*(size.y*size.y+size.z*size.z),
                 m12*(size.x*size.x+size.z*size.z),
                 m12*(size.x*size.x+size.y*size.y)};
  if(prm.car==2)
    inertiaBody.z *= 2;

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
  }

namespace {
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
  frameForce = frameTorque = {};
  stepTouch  = Contact{};
  stuckFor   = 0;
  slowFor    = calmFor = touchFor = 0;
  sinceTouch = 2e6f;
  for(auto& wh:wheels) {
    wh.length  = prm.springLength-prm.springSag;
    wh.change  = 0;
    wh.contact = false;
    wh.spin    = 0;
    wh.drag    = 0;
    wh.roll    = wh.turn = wh.gasSpin = 0;
    }
  steer        = 0;
  steerDeg     = 0;
  gear         = 1;
  braking      = false;
  brakeKey     = false;
  throttle     = 0;
  handbrake    = false;
  drive        = 0;
  movingTime   = 0;
  springsOn    = true;
  under        = Surface::Default;
  sub          = 0;
  wet          = 0;
  underWater   = 0;
  lastHit      = Hit::None;
  sinceProp    = 1e3f;
  touch        = Contact{};
  carBumped         = false;
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
  const float ls = vel.length();
  frameForce = frameTorque = {};
  // the keys that keep the car awake (0x4f59a0: Forw, Back, Left, Right and Jump of the key bits 0x574c + 8 n)
  keysHeld = in.throttle!=0 || in.steer!=0 || in.jump;
  rollKey  = std::clamp(in.steer,-1.f,1.f);
  jumpKey  = in.jump;
  movingTime += dt;

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

  brakeKey  = in.throttle<0;
  throttle  = in.throttle;
  handbrake = in.handbrake;

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
    }
  else {
    float rest = dt;
    if(dt>0.038f)
      do {
        steps(0.02f);
        rest -= 0.02f;
        } while(rest>0.038f);
    steps(0.5f*rest);
    steps(0.5f*rest);
    }
  wheelSpin(dt);
  }

// RCCars.exe 0x4ef280, once a frame after the steps. A wheel on the ground is rolled by it at roll = v/r (v: the
// velocity of the wheel's point along its heading), in the air roll is 0; `turn` follows roll by 200 rad/s^2 on the
// ground and 5 in the air. The gas (or the reverse key) held past 0.3 spins the wheel faster by up to
// CarParams::spinMax, reached at 70 rad/s^2, on the ground only as long as the car is slower than 4 m/s (all of it
// standing still, none at 4 m/s); otherwise that goes at 35 rad/s^2. Braking stops both. The wheel is drawn turned
// by (turn + gasSpin) * dt every frame.
void Vehicle::wheelSpin(float dt) {
  auto towards = [](float v, float to, float rate) {
    return std::abs(to-v)<=rate ? to : v+(to>v ? rate : -rate);
    };
  const bool  gas  = throttle>0.3f, back = throttle<-0.3f;
  const float ls   = vel.length();
  const auto  rp   = rigPose();
  for(size_t i=0; i<wheels.size(); ++i) {
    auto&      wh  = wheels[i];
    const Mat4 wp  = rp.wheel(i);
    const Vec3 hub = rot.point(wp.translation()-com)+st.x;
    const float a  = steerAngle(wh)*Pi/180.f;   // + = left, towards +X
    const Vec3 fwd = rot.vector({std::sin(a),0,std::cos(a)});
    wh.roll = wh.contact ? Vec3::dot(pointVelocity(hub),fwd)/wh.radius : 0.f;
    wh.turn = braking ? 0.f : towards(wh.turn,wh.roll,(wh.contact ? 200.f : 5.f)*dt);
    float to = 0, rate = 35.f;
    if(gas || back) {
      to   = gas ? prm.spinMax : -prm.spinMax;
      rate = 70.f;
      if(wh.contact)
        to *= std::clamp(1.f-0.25f*ls,0.f,1.f);
      }
    wh.gasSpin = braking ? 0.f : towards(wh.gasSpin,to,rate*dt);
    wh.spin    = std::fmod(wh.spin+(wh.turn+wh.gasSpin)*dt,2.f*Pi);
    }
  }

float Vehicle::rearSlip() const {
  float s = 0;
  for(int i:rearPair)
    if(i>=0)
      s = std::max(s,wheelSlip(size_t(i)));
  return s;
  }

bool Vehicle::wheelGround(size_t i, Vec3& at) const {
  if(i>=wheels.size() || !wheels[i].contact)
    return false;
  at = wheels[i].point;
  return true;
  }

float Vehicle::wheelSlip(size_t i, float* rim) const {
  if(i>=wheels.size()) {
    if(rim!=nullptr)
      *rim = 0;
    return 0;
    }
  const auto& wh = wheels[i];
  if(rim!=nullptr)
    *rim = wh.turn+wh.gasSpin;
  return std::min(std::abs(wh.turn+wh.gasSpin-wh.roll)/20.f,1.f);
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
  if(!settling && (rollOver(h) || asleep(h)))
    return;
  move(h);
  }

// RCCars.exe 0x509080, before every step (0x4f5e50). A car slower than 0.8 m/s whose forward axis is within 30
// degrees of level (or straight up or down), with its up axis at least 70 degrees from the level direction to the
// right of its forward axis (0x5092c0: on its roof, on its left side or upright, never on its right side), and a
// steering key held, the left one first (0x5091b0), gets only a torque about its forward axis instead of the step:
// momentFloorBackABS times (1 + how far the key is in), negative for the left key. Gravity and the momentum stay
// out of it: one Euler step moves it by its velocity and turns it (0x509460). Where that puts a sphere in the
// track it is tried 9 mm higher; still in, the car is left as it was and takes the step.
bool Vehicle::rollOver(float h) {
  constexpr float Deg = 180.f/3.14159265f;
  if(rollKey==0 || vel.length()>0.8f)
    return false;
  auto angle = [](const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(Vec3::dot(a,b),-1.f,1.f))*Deg;
    };
  const Vec3 fwd  = axis(2);
  const Vec3 flat = {fwd.x,0,fwd.z};
  if(flat.length()>1e-6f && angle(fwd,flat.normalized())>30.f)
    return false;
  const Vec3 side = Vec3{fwd.z,0,-fwd.x}.normalized();
  if(angle(axis(1),side)<70.f)
    return false;
  const float torque = prm.rollTorque*(1.f+std::abs(rollKey))*(rollKey>0 ? -1.f : 1.f);

  const Body was = st;
  Body to = st;
  to.x = st.x+vel*h;
  turned(st.q,omega,h,to.q);
  to.L = st.L+fwd*(torque*h);
  inside.clear();
  for(int i=0; i<2; ++i) {
    Body b = to;
    b.x.y += float(i)*0.009f;
    set(b);
    if(!penetrates(AllSpheres))
      return true;
    }
  set(was);
  return false;
  }

// gears and brakes (RCCars.exe, 0x4ee5e0), anew for every Euler step of the body that has a wheel on the ground:
// the brake key brakes going forwards and puts the reverse in once the car is down to GearSpeed (its whole
// velocity, sideways too), the throttle the same the other way round; the handbrake holds the car. Braking, the
// tires hold the car (Vehicle::tires) and the engine does not pull. The engine (0x4eea50) pulls with `drive`,
// cancelling the air and the bearings while it pulls forwards.
void Vehicle::engine() {
  constexpr float GearSpeed = 2.f;
  const float v  = speed();
  const float ls = vel.length();
  braking = handbrake;
  if(throttle>0 && gear<0) {
    if(ls<=GearSpeed) gear = 1; else braking = true;
    }
  else if(throttle<0 && gear>0) {
    if(ls<=GearSpeed) gear = -1; else braking = true;
    }
  drive = 0;
  climb = 0;
  if(!braking && throttle>0 && gear>0) {
    if(float a = prm.acceleration(v,throttle,boostOn); a>0.001f) {
      drive = a + prm.airDrag*ls*ls + prm.rolling;
      climb = prm.climbing(v,throttle);
      }
    }
  else if(!braking && throttle<0 && gear<0) {
    const float full = (boostOn ? 1.5f : 1.f)*prm.reverseAccel*(-throttle);
    if(ls<prm.reverseFullSpeed)
      drive = -full;
    else if(ls<prm.reverseSpeed)
      drive = -full*(prm.reverseSpeed-ls)/(prm.reverseSpeed-prm.reverseFullSpeed);
    }
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
// A car being placed (settling) only has gravity and the springs, and of them neither a push along the ground
// nor a turn about the vertical (0x4f0910 zeroes them).
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
    if(!springsOn || !wh.contact || l>prm.springLength)
      continue;
    const float k = springK*(prm.springLength-l);
    const float f = k - (h>0.001f ? prm.springDamping*wh.change/h : 0.f);
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
    if(wheelsOnGround()>0)
      engine();
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
    // deep sand and water (0x4eeea0): each wheel on the ground in them is held back by k * v^2 (the car's speed)
    // against the motion of the point it touches
    const float v2 = vel.length()*vel.length();
    for(auto& wh:wheels) {
      if(!wh.contact || wh.drag<=0)
        continue;
      Vec3 pv = pointVelocity(wh.point);
      if(const float l = pv.length(); l>=1e-6f)
        pv = pv*(1.f/l);
      const Vec3 f = pv*(-prm.mass*wh.drag*v2);
      force  = force+f;
      torque = torque+Vec3::cross(wh.point-c,f);
      }
    tires(h,force,torque);
    force  = force+frameForce;
    torque = torque+frameTorque;
    }
  else {
    force.x  = force.z = 0;
    torque.y = 0;
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
    // 0x4f5770: MAX_IMPULSE.ini
    if(const float p = st.P.length(); p>prm.limits.linear*prm.mass)
      st.P = st.P*(prm.limits.linear*prm.mass/p);
    if(const float l = st.L.length(); l>prm.limits.angular)
      st.L = st.L*(prm.limits.angular/l);
    set(st);

    // the wheels on the track; one keeps the car's stillness timer going (0x4f0230, 0x4f5980)
    if(wheelTouches())
      sinceTouch = 0;

    const Body from = st;
    // ours: a sphere already in the track (a wheel the spring has put by a wall) does not stop the move, it is
    // only pushed out of it
    findInside();
    float t = std::clamp(sweep(from,rest),0.f,rest);
    // a body sphere in the track, or else, after the springs have followed the ground, a wheel's where a spring
    // cannot take it in
    roofed = false;
    bool hit = penetrates(BodySpheres);
    if(!hit && suspension(t,false))
      hit = penetrates(WheelSpheres);
    // the springs push in the next part only after one that met nothing, with room above the wheels (0x4f5e50)
    springsOn = !hit && !roofed;
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

// RCCars.exe's wheels (0x4fb340): each is a sphere of radius r hanging `length` below the top of its spring, along
// the car's up axis (0x4ef680). After the body has moved, wheel by wheel:
//  - a sphere in the track is lifted out of it (0x4fb9e0): up to 5 times, by as much as takes the point of the track
//    it touches out of it along the car's up axis, plus 0.01 r, but no higher than springMin and by no more than
//    0.95 r in all. With the room above a wheel taken (0x4fbd70: the track within a sphere of 1.5 r over 1.5 r above
//    its centre, for any wheel) it goes up towards the slow rest at 0.95 r per second instead;
//  - a free one goes towards its rest: springLength, or the slow rest (springLength - 0.95 springSag) for a car
//    slower than 5 km/h after a move that met the track or with the room above a wheel taken. Down by r per second,
//    r (1 + 18 t) t s after the car last went slower than 5 km/h like that, 10 r from 0.5 s on, and 10 r while the
//    car is being placed, at most 0.99 r a step, and then lifted out of the track again (0x4fbc50) without ending
//    higher than it was; up by as much, as long as that does not put it in the track;
//  - a wheel whose centre would cross the track on the way stays where it was.
// A wheel that cannot be helped stops it: the rest keep their lengths and the step is taken as a hit (0x4f5e50).
// What each spring changed by is its damper's (0x4f0f80), none after such a stop.
bool Vehicle::suspension(float h, bool hit) {
  constexpr float Moving    = 1.388889f; // m/s, 5 km/h
  const float speed = velocity().length();

  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  const Vec3 up = axis(1);
  auto centre = [&](const Wheel& wh) { return m.point(wh.mount-Vec3{0,wh.length,0}); };
  PhysicsWorld::TrackTouch t;
  // ours: in by more than WheelSink, as for the step (Vehicle::penetrates); a wheel lifted along the car's up axis
  // off a slope only comes to touch it
  auto inside = [&](const Wheel& wh) { return owner.touchTrack(centre(wh),wh.radius-WheelSink,&t,1)>0; };

  // 0x4fb9e0: out of the track by at most `most`
  auto lift = [&](Wheel& wh, float most) {
    const float r = wh.radius, was = wh.length;
    if(most>r-1e-6f)
      return false;
    float moved = 0;
    int   i     = 0;
    for(; i<5; ++i) {
      if(!inside(wh))
        break;
      if(wh.length<prm.springMin+1e-4f)
        break;
      // along the up axis from 3 r under the point touched, to where it meets the sphere
      const Vec3  c = centre(wh), w = t.point-up*(3.f*r)-c;
      const float b = Vec3::dot(w,up), disc = b*b-(Vec3::dot(w,w)-r*r);
      if(disc<0)
        break;
      const Vec3 e = t.point-up*(3.f*r)+up*(-b-std::sqrt(disc));
      moved += (t.point-e).length();
      if(moved>most)
        break;
      wh.length = std::max(was-moved-0.01f*r,prm.springMin);
      }
    if(i<5 && !inside(wh))
      return true;
    wh.length = was;
    return false;
    };
  // 0x4fbc50: down by `by`, then out of the track again
  auto lower = [&](Wheel& wh, float by) {
    const float r = wh.radius, was = wh.length;
    if(by>r)
      by = 0.99f*r;
    wh.length = std::min(wh.length+std::min(by,0.99f*r),prm.springMax);
    if(lift(wh,0.99f*r))
      wh.length = std::max(wh.length,was);
    else
      wh.length = was;
    };
  auto towards = [](float from, float to, float by) {
    return std::abs(from-to)<by ? to : (from<to ? from+by : from-by);
    };

  // the room above the wheels (0x4fbd70)
  bool roof = false;
  roofed = false;
  for(auto& wh:wheels) {
    const float r = 1.5f*wh.radius;
    if(owner.touchTrack(centre(wh)+Vec3{0,r,0},r,&t,1)>0) {
      roof   = true;
      roofed = true;
      break;
      }
    }

  const float slowRest = prm.springLength-0.95f*prm.springSag;
  float       before[16];
  bool        stopped  = false;
  for(size_t i=0; i<wheels.size() && i<std::size(before); ++i)
    before[i] = wheels[i].length;
  for(size_t i=0; i<wheels.size() && i<std::size(before) && !stopped; ++i) {
    auto&       wh   = wheels[i];
    const float r    = wh.radius;
    const float was  = wh.length;
    const Vec3  from = centre(wh);
    if(inside(wh)) {
      if(!roof) {
        stopped = !lift(wh,0.95f*r);
        }
      else {
        wh.length = towards(wh.length,slowRest,0.95f*r*h);
        if(inside(wh)) {
          wh.length = was;
          stopped   = true;
          }
        else if(speed<Moving)
          movingTime = 0;
        }
      }
    else {
      float to = prm.springLength, by = r*h;
      if((!hit && !roof) || speed>=Moving) {
        if(movingTime>0.5f)
          by = 10.f*r*h;
        else if(movingTime>=0)
          by = r*h*(1.f+18.f*movingTime);
        }
      else {
        movingTime = 0;
        to         = slowRest;
        }
      if(settling)
        by = 10.f*r*h;
      by = std::clamp(by,0.f,0.99f*r);
      if(wh.length<to)
        lower(wh,by);
      if(to<wh.length) {
        const float b = wh.length;
        wh.length = towards(wh.length,to,by);
        if(inside(wh))
          wh.length = b;
        }
      }
    if(!stopped && owner.crossesTrack(from,centre(wh))) {
      wh.length = was;
      stopped   = true;
      }
    }

  for(size_t i=0; i<wheels.size(); ++i) {
    auto& wh = wheels[i];
    wh.change = stopped || i>=std::size(before) ? 0.f : wh.length-before[i];
    }
  return stopped;
  }

// RCCars.exe (0x4f0230, 0x4efe00): the wheels touching the track at the start of a part of the step, their spheres
// within 6 mm of it: where, and the normal there, for the springs, the engine and the tires of that part
bool Vehicle::wheelTouches() {
  PhysicsWorld::TrackTouch t;
  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  bool any = false;
  for(auto& wh:wheels) {
    wh.contact = owner.touchTrack(m.point(wh.mount-Vec3{0,wh.length,0}),wh.radius+TouchMargin,&t,1)>0;
    if(wh.contact) {
      wh.point  = t.point;
      wh.normal = t.normal;
      any       = true;
      }
    }
  return any;
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

bool Vehicle::collide(Vehicle& a, Vehicle& b) {
  // 0x533430 for the players' two cars: closer than 2.5 m and a pair of their spheres touching (0x533740)
  if((a.pose().translation()-b.pose().translation()).length()>=BallsReach)
    return false;
  std::vector<Ball> sa, sb;
  a.balls(sa);
  b.balls(sb);
  if(!ballsMeet(sa.data(),sa.size(),sb.data(),sb.size()))
    return false;
  a.touchedCar();
  b.touchedCar();
  bounce(a,b,a.centre(),b.centre());
  return true;
  }

void Vehicle::bounce(Vehicle& a, Vehicle& b, const Vec3& aCentre, const Vec3& bCentre) {
  Vec3 at, n;
  if(!contactOf(aCentre,bCentre,at,n))
    return;
  // 0x4f0750 on the line between the centres given, though each car is pushed about its own centre of mass
  exchange(a,b,at,n);
  }

void Vehicle::balls(std::vector<Ball>& out) const {
  auto& sp = const_cast<std::vector<Sphere>&>(sphereBuf[1]);
  spheres(sp,AllSpheres);
  out.clear();
  for(auto& s:sp)
    out.push_back({s.c,s.r});
  }

void Vehicle::ballsAt(const CarGeometry& g, const CarParams& p, const Mat4& pose, std::vector<Ball>& out) {
  out.clear();
  for(auto& gw:g.wheels)
    out.push_back({pose.point(gw.center),gw.front ? p.wheelRadius : p.rearWheelRadius});
  for(auto& b:p.body)
    out.push_back({pose.point(b.at),b.radius});
  }

void Vehicle::bump(const Vec3& point) {
  Vec3 at, n;
  if(contactOf(st.x,point,at,n))
    resolve({Touch{at,n,false,0}});
  }

// RCCars.exe gives the player's car a hit, shown as "Łup!" or "Wielkie Łup!" (msg_hits.csi) for a second and worth
// HitBoost turbo units, when it
//  - touches a prop of stone.sb faster than PropHitSpeed, at most every PropHitPause s (0x533ee0);
//  - knocks another car about (Opponents::collide);
//  - hits people, a guard faster than 10 km/h or a dog faster than 15 km/h: a great hit (Characters).
// Road traffic, crabs, spiders and gulls give nothing.
//
// What the car touches pushes it as in the original's handlers (0x534d00), once a frame:
//  - a prop (0x533ee0, Vehicle::meet): the two bounce apart along the line between their centres of mass (exchange);
//  - a person, a guard or a dog (0x534340, 0x5344f0, 0x5349d0, Vehicle::bump): the car alone, along the line from
//    the point touched to its centre of mass, through the middle, until it leaves at 0.05 m/s (0x4f0750);
//  - the road traffic (0x534880) and the other cars (Opponents::collide) have their own.
namespace {
constexpr float PropHitSpeed  = 1.944445f; // m/s, 7 km/h
constexpr float PropHitPause  = 0.3f;
}

void Vehicle::meet(Prop& p, uint32_t& rng) {
  // 0x533ee0: a hit when the car is fast enough and the last one was long enough ago
  if(sinceProp>PropHitPause && vel.length()>PropHitSpeed) {
    propHit   = true;
    sinceProp = 0;
    }
  objectNow = true;
  p.knock();
  // the prop's centre of mass a random 1 cm off (0x4bb5f0 -> a random direction), so that a car standing on a
  // prop's centre line does not push it straight on forever
  auto next = [&rng]() { rng = rng*1103515245u+12345u; return float((rng>>8)&0xffff)/65535.f*2.f-1.f; };
  Vec3 dir{next(),next(),next()};
  const float l = dir.length();
  dir = l>1e-6f ? dir*(0.01f/l) : Vec3{0,0.01f,0};
  Vec3 at, n;
  if(!contactOf(st.x,p.centre()+dir,at,n))
    return;
  // the sound of the touch: wood (objects -> 7, 0x4f68a0)
  note(stepTouch,pointVelocity(at)-p.velocity(),n,SoundSurface::Wood);
  exchange(*this,p,at,n);
  }

void Vehicle::hits(float dt) {
  lastHit    = Hit::None;
  sinceProp += dt;

  // the track and the props, from the steps since the last update
  touch          = stepTouch;
  stepTouch      = Contact{};

  carBumped     = carNow && !carBefore;
  carBefore = carNow;
  carNow    = false;

  if(propHit) {
    propHit = false;
    lastHit = Hit::Hit;
    addHitBoost();
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

void Vehicle::surfaces(float dt) {
  const auto& track = owner.track();
  const int   n     = int(wheels.size());

  // wheels: the solid surface under each one touching the ground, and how deep each one is in water
  const Mat4 m = pose();
  size_t count[size_t(Surface::Water)+1] = {};
  groundSnd   = SoundSurface::None;
  groundedNow = false;
  float  wetSum = 0;
  for(int i=0; i<n; ++i) {
    Wheel&       wh  = wheels[size_t(i)];
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
    // held back (0x4eeea0): a wheel in water by how deep it is, otherwise one on deep sand
    wh.drag = depth>0 ? prm.waterDrag*std::clamp((depth-0.3f*r)/(0.6f*r),0.f,1.f) : 0.f;
    if(!wh.contact)
      continue;
    const Surface s = track.surfaceBelow(wh.point+Vec3{0,0.02f,0},0.05f);
    count[size_t(s)]++;
    if(depth<=0 && s==Surface::DeepSand)
      wh.drag = prm.deepSandDrag;
    }
  const float wetNow = n>0 ? wetSum/float(n) : 0.f;

  // body: how much of its height (from the bottom of the wheels to the roof, antenna aside) is below the water surface
  const float height = std::max(geom.roof-geom.min.y,0.01f);
  const Vec3  centre = m.point(Vec3{(geom.bodyMin.x+geom.bodyMax.x)*0.5f,geom.min.y+height*0.5f,
                                    (geom.bodyMin.z+geom.bodyMax.z)*0.5f});
  float subNow = 0;
  if(auto level = track.waterLevel(centre,5.f))
    subNow = std::clamp((*level-(centre.y-height*0.5f))/height,0.f,1.f);

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
