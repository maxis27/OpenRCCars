#include "opponents.h"

#include <algorithm>
#include <cmath>

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
                   const Spline& band)
  : owner(world), geom(g), profiles(std::move(p)), tracker(route), bandSpline(band) {
  // the lap's end: where each recording comes back to cp_1 in the second half of its loop, through the gate or
  // past it close by as the player's laps count (Checkpoints::passes; on beach_2 the drivers turn round just
  // before its line)
  for(auto& r:profiles) {
    float line = r.duration();
    if(!route.empty()) {
      const size_t end  = route.route().size()-1;
      const float  half = r.cycleStart()+(r.duration()-r.cycleStart())*0.5f;
      const auto&  fr   = r.frames();
      for(size_t i=1; i<fr.size(); ++i)
        if(fr[i].time>=half && route.passes(end,fr[i-1].position,fr[i].position)) {
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
  owner.bullet().removeRigidBody(body.get());
  }

void Opponent::restart() {
  cur          = 0;
  playTime     = 0;
  lapCount     = 0;
  lastRate     = 1;
  currentSpeed = 0;
  seam         = {};
  wheelTurn    = 0;
  steerAngle   = 0;
  finishTime   = -1;
  current      = profiles[0].pose(0);
  tracker.reset();
  tracker.update(current.translation(),0);
  place();
  // no motion carried over from where it was
  body->setInterpolationWorldTransform(body->getWorldTransform());
  body->setInterpolationLinearVelocity(btVector3(0,0,0));
  body->setInterpolationAngularVelocity(btVector3(0,0,0));
  }

void Opponent::update(float dt, float rate, float ahead, bool band) {
  float r = rate;
  if(band && !bandSpline.empty())
    r *= bandSpline.at(ahead);
  lastRate = r;
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

Mat4 Opponent::wheelPose(size_t i) const {
  auto&       wh  = geom.wheels[i];
  const float deg = 180.f/3.14159265f;
  return Mat4::translate(wh.center) *
         Mat4::rotate(1,wh.front ? steerAngle*deg : 0.f) *
         Mat4::rotate(0,wheelTurn*deg);
  }

Opponents::Opponents(PhysicsWorld& world, const Reader& read, const SbScene& carScene, const std::string& track,
                     const Checkpoints& route, Level level)
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
    auto car = std::make_unique<Opponent>(world,carGeometry(carScene,CarModels[o.car]),std::move(laps),route,band);
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

void Opponents::restart() {
  player.reset();
  for(auto& c:cars)
    c->restart();
  }

void Opponents::update(float dt, bool racing, const Vec3& playerPos, size_t playerLaps, bool playerDone,
                       float raceTime, int laps) {
  player.update(playerPos,playerLaps);
  for(auto& c:cars) {
    if(!racing) {
      c->place();
      continue;
      }
    c->update(dt,baseRate,c->progress()-player.progress(),!playerDone);
    if(c->finishTime<0 && int(c->laps())>=laps)
      c->finishTime = raceTime;
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
