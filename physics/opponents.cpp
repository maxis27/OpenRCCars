#include "opponents.h"

#include <algorithm>
#include <cmath>

#include <BulletCollision/CollisionShapes/btCompoundShape.h>

#include "btconvert.h"
#include "vehicle.h"

using namespace rc;
using namespace rc::bt;

static const char* const CarModels[] = {"Car1", "Car2", "Car3"}; // Overkill, Buggy, Hummer

// when the replay goes on in the next recording, the gap between the two (0.1 - 0.25 m) fades away in this many seconds
static constexpr float SeamFade = 1.f;

static Vec3 flat(Vec3 v) {
  v.y = 0;
  return v;
  }

// RCCars.exe's autopilot of a knocked car (0x4fdb50 .. 0x4fe490), see Opponent::drive
namespace Autopilot {
constexpr float Window      = 6.f;     // m of the recording ahead where the nearest frame is looked for
constexpr float Aim         = 2.7f;    // m from the nearest frame to the one steered at
constexpr float BackOn      = 0.1f;    // m (across) from a frame to go on replaying from it ...
constexpr float BackOnAngle = 10.f;    // ... heading this many degrees at most from the recording's
constexpr float BackOnAfter = 2.f;     // s knocked before it may
constexpr float PutBack     = 5.f;     // s knocked: put back on the recording where the player does not see it
constexpr float MaxSteer    = 35.f;    // degrees
constexpr float SteerSpeed  = 90.f;    // degrees/s
constexpr float StuckSpeed  = 0.5555556f; // m/s (2 km/h)
constexpr float BackUpTime  = 1.5f;    // s of backing up when stuck
}

// the player sees a car this close in front (a guess: RCCars.exe asks every viewport, 0x487300(i, pos, 50, 100))
static constexpr float SeeDistance = 50.f;
// a knocked car back on the recording blends into it in this many seconds (0x503190)
static constexpr float ResumeFade  = 1.f;

// rigid transform inverse
static Mat4 inverse(const Mat4& m) {
  Mat4 r;
  for(int i=0; i<3; ++i)
    for(int j=0; j<3; ++j)
      r.m[i][j] = m.m[j][i];
  const Vec3 t = r.vector(m.translation());
  r.m[0][3] = -t.x; r.m[1][3] = -t.y; r.m[2][3] = -t.z;
  return r;
  }

// `a` and `b` mixed, `k` of `b`: the position along the line, the rotation by its forward and up axes
static Mat4 blendPose(const Mat4& a, const Mat4& b, float k) {
  auto mix = [k](const Vec3& x, const Vec3& y) { return x+(y-x)*k; };
  Vec3 f = mix(a.vector({0,0,1}),b.vector({0,0,1})).normalized();
  Vec3 u = mix(a.vector({0,1,0}),b.vector({0,1,0}));
  Vec3 l = Vec3::cross(u,f).normalized();
  u      = Vec3::cross(f,l);
  const Vec3 p = mix(a.translation(),b.translation());
  Mat4 m;
  m.m[0][0] = l.x; m.m[0][1] = u.x; m.m[0][2] = f.x; m.m[0][3] = p.x;
  m.m[1][0] = l.y; m.m[1][1] = u.y; m.m[1][2] = f.y; m.m[1][3] = p.y;
  m.m[2][0] = l.z; m.m[2][1] = u.z; m.m[2][2] = f.z; m.m[2][3] = p.z;
  return m;
  }

// signed angle in degrees from `from` to `to` around the vertical, + = to the left of `from` (towards +X when
// `from` is +Z), as RCCars.exe's 0x410150
static float yawAngle(const Vec3& from, const Vec3& to) {
  return std::atan2(from.z*to.x-from.x*to.z,from.x*to.x+from.z*to.z)*180.f/3.14159265f;
  }

// the box of a car's body: a btBoxShape, or the one child of a btCompoundShape (Vehicle)
static bool boxOf(const btRigidBody& b, btTransform& t, btVector3& half) {
  const btCollisionShape* s = b.getCollisionShape();
  t = b.getWorldTransform();
  if(s->getShapeType()==COMPOUND_SHAPE_PROXYTYPE) {
    auto c = static_cast<const btCompoundShape*>(s);
    if(c->getNumChildShapes()!=1)
      return false;
    t = t*c->getChildTransform(0);
    s = c->getChildShape(0);
    }
  if(s->getShapeType()!=BOX_SHAPE_PROXYTYPE)
    return false;
  half = static_cast<const btBoxShape*>(s)->getHalfExtentsWithMargin();
  return true;
  }

// the two boxes, each grown by `grow` (Bullet's units), overlap (separating axis test)
static bool boxesMeet(const btRigidBody& a, const btRigidBody& b, float grow) {
  btTransform ta, tb;
  btVector3   ha, hb;
  if(!boxOf(a,ta,ha) || !boxOf(b,tb,hb))
    return false;
  ha += btVector3(grow,grow,grow);
  hb += btVector3(grow,grow,grow);
  const btMatrix3x3& A = ta.getBasis();
  const btMatrix3x3& B = tb.getBasis();
  const btVector3    d = tb.getOrigin()-ta.getOrigin();
  auto apart = [&](const btVector3& axis) {
    if(axis.length2()<1e-8f)
      return false;
    btScalar ra = 0, rb = 0;
    for(int i=0; i<3; ++i) {
      ra += ha[i]*btFabs(axis.dot(A.getColumn(i)));
      rb += hb[i]*btFabs(axis.dot(B.getColumn(i)));
      }
    return btFabs(d.dot(axis))>ra+rb;
    };
  for(int i=0; i<3; ++i)
    if(apart(A.getColumn(i)) || apart(B.getColumn(i)))
      return false;
  for(int i=0; i<3; ++i)
    for(int j=0; j<3; ++j)
      if(apart(A.getColumn(i).cross(B.getColumn(j))))
        return false;
  return true;
  }

float RouteTracker::update(const Vec3& car, size_t laps) {
  const auto& pts = route->route();
  if(pts.size()<2)
    return value = 0;
  if(laps!=lapsSeen) {
    lapsSeen = laps;
    seg      = 0;
    }
  // the nearest point on the segments ahead; a later segment only when clearly nearer than the current one, so
  // that the two legs of a hairpin are not mixed up
  auto nearest = [&](size_t i, float& along) {
    const Vec3  a   = flat(pts[i].position), ab = flat(pts[i+1].position)-a;
    const float len = ab.length();
    float t = len>1e-3f ? Vec3::dot(flat(car)-a,ab)/(len*len) : 0.f;
    t     = std::min(t,1.f);
    if(i>0)
      t = std::max(t,0.f);   // before cp_1 on the grid: behind the line, below zero
    along = route->along(i)+t*len;
    return (flat(car)-(a+ab*t)).length();
    };
  float  at   = 0;
  float  best = nearest(seg,at);
  size_t pick = seg;
  for(size_t i=seg+1; i+1<pts.size() && route->along(i)-route->along(seg)<Window; ++i) {
    float      a = 0;
    const float d = nearest(i,a);
    if(d<=(i==seg+1 ? best : best-2.f)) {
      best = d;
      pick = i;
      at   = a;
      }
    }
  // put back on the track (R, drowned): the nearest segment anywhere in the lap
  if(lost) {
    lost = false;
    for(size_t i=0; i+1<pts.size(); ++i) {
      float      a = 0;
      const float d = nearest(i,a);
      if(d<best) {
        best = d;
        pick = i;
        at   = a;
        }
      }
    }
  seg   = pick;
  value = float(laps)*route->lapLength()+at;
  return value;
  }

Opponent::Opponent(PhysicsWorld& world, const CarGeometry& g, std::vector<CarProfile> p, const Checkpoints& route,
                   const Spline& band, const CarParams& params)
  : owner(world), checkpoints(route), geom(g), prm(params), rig(g,params), profiles(std::move(p)), tracker(route), bandSpline(band) {
  // the lap's end: where each recording comes back to cp_1 in the second half of its loop, within the computer's
  // radius of it as in RCCars.exe (Checkpoints::passes)
  for(auto& r:profiles) {
    float line = r.duration();
    if(!route.empty()) {
      const size_t end  = route.route().size()-1;
      const float  half = r.cycleStart()+(r.duration()-r.cycleStart())*0.5f;
      const auto&  fr   = r.frames();
      for(size_t i=1; i<fr.size(); ++i)
        if(fr[i].time>=half && route.passes(end,fr[i-1].position,fr[i].position,Checkpoints::AiPassRadius)) {
          line = fr[i].time;
          break;
          }
      }
    lines.push_back(line);
    }

  float front = -1e9f, rear = 1e9f;
  for(auto& wh:geom.wheels) {
    front       = std::max(front,wh.center.z);
    rear        = std::min(rear, wh.center.z);
    wheelRadius = std::max(wheelRadius,wh.radius);
    }
  wheelBase = front>rear ? front-rear : 0.f;

  // the body: the chassis outline from the wheel hubs up, as Vehicle has it
  float hubY = geom.bodyMin.y;
  for(auto& wh:geom.wheels)
    hubY = std::max(hubY,wh.center.y);
  Vec3 bmin = geom.bodyMin, bmax = geom.bodyMax;
  bmin.y    = std::min(hubY,bmax.y-0.02f);
  boxCenter = (bmin+bmax)*0.5f;
  box    = std::make_unique<btBoxShape>(toBt((bmax-bmin)*0.5f)*S);
  motion = std::make_unique<btDefaultMotionState>();
  btRigidBody::btRigidBodyConstructionInfo ci(0.f,motion.get(),box.get());
  ci.m_friction    = 0.3f;
  ci.m_restitution = 0.1f;
  body = std::make_unique<btRigidBody>(ci);
  body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setUserIndex(PhysicsWorld::CarTag);
  restart();
  owner.bullet().addRigidBody(body.get());
  }

Opponent::~Opponent() {
  if(driven==nullptr)
    owner.bullet().removeRigidBody(body.get());
  }

void Opponent::restart() {
  if(driven!=nullptr) {
    driven.reset();
    owner.bullet().addRigidBody(body.get());
    }
  sinceCar     = 1e3f;
  shakeLeft    = 0;
  shotTime     = -1;
  resumeFade   = 0;
  cur          = 0;
  playTime     = 0;
  lapCount     = 0;
  lastRate     = 1;
  currentSpeed = 0;
  seam         = {};
  wheelTurn    = 0;
  steerAngle   = 0;
  finishTime   = -1;
  drownTimer   = 0;
  resets.reset();
  current      = profiles[0].pose(0);
  tracker.reset();
  tracker.update(current.translation(),0);
  place();
  // no motion carried over from where it was
  body->setInterpolationWorldTransform(body->getWorldTransform());
  body->setInterpolationLinearVelocity(btVector3(0,0,0));
  body->setInterpolationAngularVelocity(btVector3(0,0,0));
  }

void Opponent::update(float dt, float rate, float ahead, bool band, bool seen) {
  float r = rate;
  if(band && !bandSpline.empty())
    r *= bandSpline.at(ahead);
  lastRate = r;
  if(driven!=nullptr)
    drive(dt,seen);
  if(driven!=nullptr) {
    currentSpeed = driven->speed();
    tracker.update(driven->pose().translation(),lapCount);
    return;
    }
  const float before = playTime;
  playTime += dt*r;
  if(before<lines[cur] && playTime>=lines[cur])
    ++lapCount;

  // the end of a recorded lap: on in the next recording from where its loop begins
  if(playTime>profiles[cur].duration()) {
    const Vec3  was  = current.translation();
    const float over = playTime-profiles[cur].duration();
    cur      = (cur+1)%profiles.size();
    playTime = profiles[cur].cycleStart()+over;
    seam     = was-profiles[cur].pose(playTime).translation();
    if(seam.length()>2.f)
      seam = {};    // not a loop after all: no gliding across the track
    }

  const CarProfile& profile = profiles[cur];
  seam    = seam*std::max(0.f,1.f-dt/SeamFade);
  current = Mat4::translate(seam)*profile.pose(playTime);
  if(resumeFade>0) {
    current    = blendPose(current,current*resumeOffset,resumeFade);
    resumeFade = std::max(0.f,resumeFade-dt/ResumeFade);
    }
  const Vec3 v = profile.velocity(playTime);   // as recorded, without the replay rate
  const Vec3 f = current.vector({0,0,1});
  currentSpeed = Vec3::dot(v,f)*r;
  tracker.update(current.translation(),lapCount);

  // wheels: rolling with the speed; the front ones turned by the curve's radius (yaw rate x wheelbase / speed)
  wheelTurn += currentSpeed*dt/wheelRadius;
  float steer = 0;
  const float recorded = Vec3::dot(v,f);
  if(std::abs(recorded)>0.3f && wheelBase>0) {
    const auto& fr = profile.frames();
    auto it = std::lower_bound(fr.begin(),fr.end(),playTime,[](const CarProfile::Frame& a, float t){ return a.time<t; });
    if(it==fr.end())
      --it;
    const float yawRate = Vec3::dot(it->spin,current.vector({0,1,0}));
    steer = std::atan(yawRate*wheelBase/std::abs(recorded));
    }
  steer       = std::clamp(steer,-0.45f,0.45f);
  steerAngle += (steer-steerAngle)*std::min(1.f,dt*10.f);
  place();
  }

void Opponent::place() {
  const btTransform t = toBt(current*Mat4::translate(boxCenter));
  motion->setWorldTransform(t);
  body->setWorldTransform(t);
  }

CarRig::Pose Opponent::rigPose() const {
  if(driven!=nullptr)
    return driven->rigPose();
  const float deg = 180.f/3.14159265f;
  std::vector<CarRig::Wheel> in(geom.wheels.size());
  for(size_t i=0; i<in.size(); ++i) {
    in[i].steer = geom.wheels[i].front ? steerAngle*deg : 0.f;
    in[i].spin  = wheelTurn*deg;
    }
  return rig.pose(in);
  }

Mat4 Opponent::wheelPose(size_t i) const {
  return rigPose().wheel(i);
  }

Mat4 Opponent::pose() const {
  return driven!=nullptr ? driven->pose() : current;
  }

float Opponent::speed() const {
  return driven!=nullptr ? driven->speed() : currentSpeed;
  }

btRigidBody& Opponent::chassis() {
  return driven!=nullptr ? driven->chassis() : *body;
  }

Vec3 Opponent::velocity() const {
  if(driven!=nullptr)
    return driven->velocity();
  return profiles[cur].velocity(playTime)*lastRate;
  }

Vec3 Opponent::localSpin() const {
  return driven!=nullptr ? driven->localSpin() : Vec3{};
  }

Vec3 Opponent::framePoint(size_t i) const {
  return profiles[cur].frames()[i].position;
  }

void Opponent::knock() {
  if(driven!=nullptr)
    return;
  const Vec3 v = velocity();
  owner.bullet().removeRigidBody(body.get());
  driven = std::make_unique<Vehicle>(owner,geom,prm,current);
  driven->place(current,v,Vec3{});
  driveTime  = 0;
  sinceTouch = 0;
  stuckTime  = 0;
  backTime   = 0;
  backing    = false;
  steerDeg   = 0;
  resumeFade = 0;
  // the frame it was at
  const auto& fr = profiles[cur].frames();
  nearFrame = 0;
  while(nearFrame+1<fr.size() && fr[nearFrame+1].time<=playTime)
    ++nearFrame;
  }

void Opponent::setTrack(const Vec3& mn, const Vec3& mx, const ResetVolumes& volumes) {
  hasTrack = true;
  boxMin   = mn;
  boxMax   = mx;
  resets   = volumes;
  resets.reset();
  }

Opponent::Back Opponent::trackRules(float dt) {
  if(!hasTrack)
    return Back::None;
  const Vec3  at    = pose().translation();
  const bool  out   = at.x<boxMin.x || at.x>boxMax.x || at.z<boxMin.z || at.z>boxMax.z || at.y<boxMin.y-0.1f;
  const float drown = Vehicle::drownRate(owner.track(),at,prm);
  drownTimer = drown>0 ? drownTimer+drown*dt : 0.f;
  // 0x508ae0: shot, back after a while
  if(shotTime>=0)
    shotTime += dt;
  // 0x4f5e50: stuck in the track
  Back back = out                             ? Back::OutOfBox :
              drownTimer>prm.drownTime        ? Back::Drowned  :
              resets.tick(at,dt)              ? Back::Volume   :
              shotTime>Vehicle::ShotHold      ? Back::Shot     :
              (knocked() && driven->stuck())  ? Back::Stuck    : Back::None;
  for(size_t i=0; back==Back::None && knocked() && i<resets.volumes().size(); ++i)
    if(resets.volumes()[i].volume->contains(at))
      back = Back::Volume;
  if(back!=Back::None)
    putBack();
  return back;
  }

void Opponent::shotFrom(const Vec3& gun) {
  knock();
  driven->shotFrom(gun);
  if(shotTime<0)
    shotTime = 0;
  }

void Opponent::putBack() {
  // the last main checkpoint passed, from where the car is along the route; its grid slot before the first one
  const auto& route = checkpoints.route();
  size_t      last  = std::min(tracker.segment(),route.empty() ? size_t(0) : route.size()-1);
  while(last>0 && !route[last].main)
    --last;
  const Mat4 to = (route.empty() || (last==0 && lapCount==0)) ? profiles[0].pose(0)
                                                              : checkpoints.poseAt(last,geom.min.y);
  knock();
  driven->reset(to);
  ++putBacks;
  shotTime   = -1;
  driveTime  = 0;
  sinceTouch = 0;
  stuckTime  = 0;
  backTime   = 0;
  backing    = false;
  steerDeg   = 0;
  drownTimer = 0;
  resets.reset();
  tracker.relocate();
  // the nearest frame of the recording (0x4fd980), not past the one it was at and not back over the lap's line
  const auto& fr   = profiles[cur].frames();
  const bool  over = playTime>=lines[cur];
  const size_t was = std::min(nearFrame,fr.size()-1);
  float       bd   = 1e30f;
  for(size_t i=0; i<=was; ++i) {
    if(over && fr[i].time<lines[cur])
      continue;
    if(const float d = (fr[i].position-to.translation()).length(); d<bd) {
      bd        = d;
      nearFrame = i;
      }
    }
  playTime = fr[nearFrame].time;
  }

void Opponent::resume(size_t frame) {
  const auto& fr = profiles[cur].frames();
  frame = std::min(frame,fr.size()-1);
  const float t = fr[frame].time;
  if(playTime<lines[cur] && t>=lines[cur])
    ++lapCount;
  playTime = t;
  // from where the knocked car is into the recording; more than 2 m apart it jumps there (0x503190)
  const Mat4 was = driven->pose();
  const Mat4 rec = profiles[cur].pose(playTime);
  seam         = {};
  resumeOffset = inverse(rec)*was;
  resumeFade   = (was.translation()-rec.translation()).length()<=2.f ? 1.f : 0.f;
  current      = was;
  driven.reset();
  owner.bullet().addRigidBody(body.get());
  place();
  body->setInterpolationWorldTransform(body->getWorldTransform());
  }

// RCCars.exe 0x4fe1f0: a knocked car drives at the frame of its recording Autopilot::Aim ahead of the nearest one,
// the speed and the wheels set as 0x4fddd0 does; it backs up for a moment when stuck (0x4fe490). It replays again
// from a frame it is back on (0x4fdb50), or is put back on the nearest frame where the player does not see it.
void Opponent::drive(float dt, bool seen) {
  using namespace Autopilot;
  driveTime  += dt;
  sinceTouch += dt;
  if(driven->contact().surface!=0)
    sinceTouch = 0;   // the body against the track or a prop

  const Mat4  m   = driven->pose();
  const Vec3  at  = m.translation();
  const Vec3  fwd = m.vector({0,0,1});
  const Vec3  vel = driven->velocity();
  const float spd = vel.length();

  // the nearest frame within Window ahead of the last one (0x4fd8b0); past the end on into the next recording
  {
    const auto& fr   = profiles[cur].frames();
    const Vec3  from = framePoint(nearFrame);
    size_t      best = nearFrame;
    float       bd   = (flat(framePoint(nearFrame)-at)).length();
    for(size_t i=nearFrame+1; i<fr.size() && (framePoint(i)-from).length()<=Window; ++i)
      if(const float d = flat(framePoint(i)-at).length(); d<bd) {
        bd   = d;
        best = i;
        }
    if(playTime<lines[cur] && fr[best].time>=lines[cur])
      ++lapCount;
    nearFrame = best;
    playTime  = fr[best].time;
    if(nearFrame+1>=fr.size() && profiles.size()>0) {
      cur       = (cur+1)%profiles.size();
      const auto& nx = profiles[cur].frames();
      nearFrame = 0;
      while(nearFrame+1<nx.size() && nx[nearFrame+1].time<=profiles[cur].cycleStart())
        ++nearFrame;
      playTime  = nx[nearFrame].time;
      }
  }
  const auto& fr = profiles[cur].frames();
  size_t aim = nearFrame;
  while(aim+1<fr.size() && (framePoint(aim)-framePoint(nearFrame)).length()<=Aim)
    ++aim;

  // stuck: barely moving, or pressed against something, for a while (0x4fe490)
  backTime += dt;
  if(!backing) {
    stuckTime = (spd<StuckSpeed || sinceTouch<dt*1.5f) ? stuckTime+dt : 0.f;
    if(stuckTime>2.f || (stuckTime>0.5f && sinceTouch<0.5f)) {
      backing  = true;
      backTime = 0;
      }
    }
  else if(backTime>BackUpTime) {
    backing   = false;
    stuckTime = 0;
    }

  Vehicle::Input in;
  if(backing) {
    in.throttle = -1;
    if(backTime>0.5f)
      steerDeg += std::clamp(5.f-steerDeg,-30.f*dt,30.f*dt);
    }
  else {
    // back on the recording (0x4fdb50)
    if(driveTime>PutBack && !seen) {
      resume(nearFrame);
      return;
      }
    if(driveTime>BackOnAfter) {
      const Vec3 rv = fr[nearFrame].velocity;
      if(flat(framePoint(nearFrame)-at).length()<BackOn && std::abs(yawAngle(rv,vel))<BackOnAngle) {
        resume(nearFrame);
        return;
        }
      if(flat(framePoint(aim)-at).length()<BackOn) {
        resume(aim);
        return;
        }
      }

    // at the aim (0x4fddd0): slower the nearer it is and the more it has to turn
    const Vec3  to    = framePoint(aim)-at;
    const float dist  = to.length();
    const float angle = yawAngle(fwd,to);
    const float a     = std::abs(angle);
    float want = dist<1.f ? 4.f : dist<10.f ? 4.f+(dist-1.f)*(2.f/3.f) : 10.f;
    want *= a<=20.f ? 1.f-a*0.025f : 0.5f;
    if(spd<want)
      in.throttle = spd<want*0.5f ? 1.f : 1.f-(spd-want*0.5f)/(want*0.5f);
    float s = 0;
    if(angle>30.f)
      s = MaxSteer;
    else if(angle<-30.f)
      s = -MaxSteer;
    else if(angle>0.5f)
      s = (angle-0.5f)*(MaxSteer/29.5f);
    else if(angle<-0.5f)
      s = (angle+0.5f)*(MaxSteer/29.5f);
    // the first second it turns hard
    if(driveTime<1.f && std::abs(s)<30.f)
      s = s>=0 ? 30.f : -30.f;
    steerDeg += std::clamp(s-steerDeg,-SteerSpeed*dt,SteerSpeed*dt);
    }
  in.steer = std::clamp(steerDeg/std::max(prm.steerAngle,1.f),-1.f,1.f);
  driven->update(in,dt);
  }

Opponents::Opponents(PhysicsWorld& world, const Reader& read, const SbScene& carScene, const std::string& track,
                     const Checkpoints& route, Level level, const Params& params)
  : owner(world), lvl(level), player(route) {
  const int32_t raceId = aiRaceNumber(track);
  if(raceId==0 || route.empty())
    return;
  auto text = [&read](const std::string& file) {
    const auto d = read(file);
    return std::string(d.begin(),d.end());
    };
  AiLayout layout;
  try {
    layout = AiLayout::parse(text("Scripts\\ailayouts.ini"));
    }
  catch(...) {
    return;
    }
  auto race = layout.races.find(raceId);
  if(race==layout.races.end())
    return;
  try {
    startProfile = CarProfile::parse(read("CarProfiles\\"+race->second.playerPath+".dat"));
    }
  catch(...) {
    startProfile = CarProfile();
    }
  if(level==Level::Off)
    return;

  // Scripts/aiCommonCoeffs.ini: "CommonCoeffs" easy, normal, hard (and ultra, not offered)
  float diff = 1;
  try {
    const auto  coeffs = KeySections::parse(text("Scripts\\aiCommonCoeffs.ini"));
    const auto& c      = coeffs.get("CommonCoeffs",level==Level::Easy ? "easy" : level==Level::Hard ? "hard" : "normal");
    if(!c.empty())
      diff = c[0];
    }
  catch(...) {
    }
  baseRate = race->second.coeff*diff;

  const int32_t bit = level==Level::Easy ? AiLayout::Easy : level==Level::Hard ? AiLayout::Hard : AiLayout::Normal;
  const auto&   ops = race->second.opponents;
  for(size_t i=0; i<ops.size(); ++i) {
    const auto& o = ops[i];
    if((o.races & bit)==0 || o.car<0 || o.car>2 || o.path.empty())
      continue;
    // "surf\over1": over1.dat, over1_2.dat, over1_3.dat ..
    std::vector<CarProfile> laps;
    for(int v=1; v<10; ++v) {
      try {
        auto lap = CarProfile::parse(read("CarProfiles\\"+o.path+(v>1 ? "_"+std::to_string(v) : "")+".dat"));
        if(lap.empty())
          break;
        laps.push_back(std::move(lap));
        }
      catch(...) {
        break;
        }
      }
    if(laps.empty())
      continue;
    Spline band;
    try {
      // the opponent's number picks its rubber band (FuncWaitAccel1 .. 5)
      band = Spline::parse(read("Splines\\FuncWaitAccel"+std::to_string(i+1)+"_A.bspl"));
      }
    catch(...) {
      }
    const CarParams prm = params ? params(o.car+1) : CarParams{};
    auto car = std::make_unique<Opponent>(world,carGeometry(carScene,CarModels[o.car]),std::move(laps),route,band,prm);
    car->car = o.car;
    if(auto d = layout.drivers.find(o.driver); d!=layout.drivers.end()) {
      // the face picture names the driver: "MCJocker.tga"
      const std::string& f = d->second.face;
      car->name = f.substr(0,f.rfind('.'));
      car->skin = d->second.skin;
      }
    if(car->name.empty())
      car->name = "AI"+std::to_string(i+1);
    cars.push_back(std::move(car));
    }
  }

Opponents::~Opponents() = default;

const char* Opponents::levelName(Level l) {
  switch(l) {
    case Level::Off:    return "off";
    case Level::Easy:   return "easy";
    case Level::Normal: return "normal";
    case Level::Hard:   return "hard";
    }
  return "";
  }

bool Opponents::playerStart(Mat4& pose) const {
  if(cars.empty() || startProfile.empty())
    return false;
  pose = startProfile.pose(0);
  return true;
  }

void Opponents::setTrack(const SbScene& track) {
  if(track.collision.empty())
    return;
  const ResetVolumes volumes(track);
  for(auto& c:cars)
    c->setTrack(track.collision.min,track.collision.max,volumes);
  }

void Opponents::restart() {
  player.reset();
  playerSinceCar = 1e3f;
  for(auto& c:cars)
    c->restart();
  }

void Opponents::update(float dt, bool racing, const Vec3& playerPos, size_t playerLaps, bool playerDone,
                       float raceTime, int laps) {
  player.update(playerPos,playerLaps);
  told.clear();
  for(size_t i=0; i<cars.size(); ++i) {
    auto& c = cars[i];
    if(!racing) {
      if(!c->knocked())
        c->place();
      continue;
      }
    const Vec3 to   = c->pose().translation()-playerPose.translation();
    const bool seen = to.length()<SeeDistance && Vec3::dot(to,playerPose.vector({0,0,1}))>0;
    const size_t was = c->laps();
    c->update(dt,baseRate,c->progress()-player.progress(),!playerDone,seen);
    const auto back = c->trackRules(dt);
    if(back==Opponent::Back::Drowned)
      told.push_back({News::Drowned,i});
    if(back==Opponent::Back::Volume)
      told.push_back({News::Volume,i});
    if(c->finishTime<0 && int(c->laps())>=laps) {
      c->finishTime = raceTime;
      told.push_back({News::Finished,i});
      }
    else if(c->finishTime<0 && c->laps()>was)
      told.push_back({News::Lap,i,c->laps()});
    }
  }

std::vector<Opponents::Standing> Opponents::standings(float playerFinish) const {
  struct Entry {
    Standing s;
    float    progress;
    };
  std::vector<Entry> all;
  all.push_back({{"",playerFinish,true},player.progress()});
  for(auto& c:cars)
    all.push_back({{c->name,c->finishTime,false},c->progress()});
  std::stable_sort(all.begin(),all.end(),[](const Entry& a, const Entry& b) {
    const bool fa = a.s.time>=0, fb = b.s.time>=0;
    if(fa!=fb)
      return fa;
    if(fa)
      return a.s.time<b.s.time;
    return a.progress>b.progress;
    });
  std::vector<Standing> ret;
  for(auto& e:all)
    ret.push_back(e.s);
  return ret;
  }

int Opponents::place(float playerFinish) const {
  const auto s = standings(playerFinish);
  for(size_t i=0; i<s.size(); ++i)
    if(s[i].player)
      return int(i+1);
  return 1;
  }

// RCCars.exe 0x533990 for two cars that touch, once a frame: both knocked off their recordings (0x4fd9d0) and pushed
// apart by impulses along the line between their centres of mass, through the middle (Vehicle::bounce). When the
// player is one of them and both last hit a car more than 2 s ago, the slower one's centre counts 0.1 m higher
// (the opponent's when they go about as fast), so the one hit is also lifted. A car is knocked already when it is
// about to be touched, so that two cars meet and not a car and a recording.
Vehicle::Hit Opponents::collide(Vehicle& pl, float dt) {
  constexpr float CarHitPause = 2.f;    // s both cars since their last hit (0x56f4)
  constexpr float ShakeTime   = 0.5f;   // s of the window (0x5334b0)
  constexpr float Lift        = 0.1f;   // m

  playerPose      = pl.pose();
  playerSinceCar += dt;
  for(auto& c:cars)
    c->sinceCar += dt;

  // about to be touched: by the player or by a knocked car, within two frames at their speeds
  for(auto& c:cars) {
    if(c->knocked())
      continue;
    auto near = [&](btRigidBody& other, const Vec3& v) {
      const float grow = ((v-c->velocity()).length()*2.f*dt+0.01f)*S;
      return boxesMeet(c->chassis(),other,grow);
      };
    bool hit = near(pl.chassis(),pl.velocity());
    for(auto& o:cars)
      if(!hit && o!=c && o->knocked())
        hit = near(o->chassis(),o->velocity());
    if(hit)
      c->knock();
    }

  // the cars touching: -1 the player, else the opponent's index
  auto who = [&](const btCollisionObject* b) -> int {
    if(b==&pl.chassis())
      return -1;
    for(size_t i=0; i<cars.size(); ++i)
      if(b==&cars[i]->chassis())
        return int(i);
    return -2;
    };
  auto& disp = *owner.bullet().getDispatcher();
  for(int k=0; k<disp.getNumManifolds(); ++k) {
    const btPersistentManifold* m = disp.getManifoldByIndexInternal(k);
    bool touching = false;
    for(int c=0; c<m->getNumContacts() && !touching; ++c)
      touching = m->getContactPoint(c).getDistance()<=0;
    if(!touching)
      continue;
    const int a = who(m->getBody0()), b = who(m->getBody1());
    if(a<-1 || b<-1 || a==b)
      continue;
    for(int i:{a,b})
      if(i>=0) {
        cars[size_t(i)]->knock();
        cars[size_t(i)]->touched();
        }
    float&     sa = a<0 ? playerSinceCar : cars[size_t(a)]->sinceCar;
    float&     sb = b<0 ? playerSinceCar : cars[size_t(b)]->sinceCar;
    const bool fresh = sa>CarHitPause && sb>CarHitPause;
    if(fresh) {
      sa = 0;
      sb = 0;
      }
    Vehicle* va = a<0 ? &pl : cars[size_t(a)]->vehicle();
    Vehicle* vb = b<0 ? &pl : cars[size_t(b)]->vehicle();
    if(va==nullptr || vb==nullptr)
      continue;
    Vec3 ca = va->centre(), cb = vb->centre();
    if(fresh && (a<0 || b<0)) {
      // the slower one by more than a tenth, or about as fast, the opponent
      const float fa = flat(va->velocity()).length(), fb = flat(vb->velocity()).length();
      const bool  liftA = fa*1.1f<fb ? true : (fb*1.1f<fa ? false : b<0);
      (liftA ? ca : cb).y += Lift;
      }
    Vehicle::bounce(*va,*vb,ca,cb);
    if(!fresh || (a>=0 && b>=0))
      continue;
    // the player hit opponent `o`: judged
    Opponent& o = *cars[size_t(a<0 ? b : a)];
    o.shakeLeft = ShakeTime;
    o.shake     = Vec3{};
    }

  // how hard the player knocked them: the opponent's average rotation in the window, in its own axes, x the
  // pitch and z the roll (0x4f6460)
  Vehicle::Hit got = Vehicle::Hit::None;
  for(auto& c:cars) {
    if(c->shakeLeft<=0)
      continue;
    c->shake     += c->localSpin()*std::min(dt,c->shakeLeft);
    c->shakeLeft -= dt;
    if(c->shakeLeft>0)
      continue;
    const Vec3  a  = c->shake*(1.f/ShakeTime);
    const float ax = std::abs(a.x), az = std::abs(a.z), al = a.length();
    const auto  h  = (ax>3.f || az>5.f || al>7.f)   ? Vehicle::Hit::Great :
                     (ax>0.8f || az>1.f || al>2.f) ? Vehicle::Hit::Hit : Vehicle::Hit::None;
    if(h!=Vehicle::Hit::None) {
      pl.addBoost(Vehicle::HitBoost);
      got = std::max(got,h);
      }
    }
  return got;
  }
