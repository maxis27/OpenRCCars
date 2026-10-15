#include "characters.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "btconvert.h"
#include "formats/settings.h"
#include "vehicle.h"

using namespace rc;
using namespace rc::bt;

namespace {

constexpr float Pi = 3.14159265f;

// RCCars.exe constants of the people (0x5107a0, 0x510f40, 0x511470, 0x5111f0, 0x510b90, 0x510b30)
constexpr float PeopleActive   = 50.f;     // m from the player's car: the person is there (RenderQual 2)
constexpr float KickReach      = 0.8f;     // m, horizontal: kick or grab a car this close ...
constexpr float KickHeight     = 0.6f;     // ... within this height
constexpr float GrabChance     = 0.2f;     // rand < 0.2: grab instead of kicking, with
constexpr float GrabRoom       = 2.2f;     //   this much room above
constexpr float RushDistance   = 4.f;      // m: a car driving at the person ...
constexpr float RushAngle      = 25.f;     // ... within this angle of where the person faces ...
constexpr float RushSpeed      = 4.166667f;// ... faster than 15 km/h gets kicked
constexpr float KickWait       = 0.5f;     // s into the kick before it can hit
constexpr float KickDistance   = 1.1f;     // m: the car is still there
constexpr float KickFrom = 0.65f, KickTo = 1.f; // s of Pendal: the foot meets the car
constexpr float KickOut        = 4.f;      // m/s away from the person
constexpr float KickUp         = 7.f;      // m/s up
constexpr float GrabFrom = 0.6f, GrabTo = 1.f;  // s of Brosok: the hand takes the car ...
constexpr float GrabDistance   = 1.4f;     // ... if it is this close
constexpr float ThrowAt        = 2.f;      // s of Brosok: the car leaves the hand
constexpr float ThrowUp        = 4.f;      // m/s
constexpr float ThrowForward   = 7.f;      // m/s
constexpr float TurnToCar      = 2.f*Pi;   // rad/s (360 degrees)
constexpr float PathTurn       = Pi;       // rad/s (180 degrees), the heading towards the path's direction
constexpr float OtherCarSize   = 0.25f;    // m: an opponent's car around its centre, to tell what it touches
constexpr float HurtSpeed      = 15.f;     // km/h: a car faster than this hurts a person or a dog
constexpr float DogPushOut     = 0.5f;     // m/s: a dog touched pushes the car off itself ..
constexpr float DogPushUp      = 0.12f;    // .. and up
constexpr float DogPainPause   = 0.5f;     // s without a hit before the dog whines again (ours)
constexpr float GuardHitSpeed  = 10.f;     // km/h: hitting a guard
constexpr float HitPause       = 2.f;      // s between two "Wielkie Łup!" for the same kind
constexpr float HitBoost       = 5.f;      // turbo units for one
constexpr float BodyRadius     = 0.2f;     // people: 3 spheres 0, 0.2 and 0.4 m above the feet (0x511e30)

// the animals (0x512b30 seagull, 0x514100 crab, 0x515570 spider, 0x519400 dog)
constexpr float GullScare      = 4.f;      // m: a car, a person or a dog this close scares a seagull off
constexpr float GullArrive     = 0.25f;
constexpr float GullWalkTurn   = 0.5f, GullFlyTurn = 3.3f; // rad/s
constexpr float GullFlySpeed   = 10.f;     // SpeedFlight is a share of this
constexpr float CrabSpeed      = 0.6f;     // m/s
constexpr float CrabTurn       = 0.5f;     // rad/s
constexpr float CrabArrive     = 0.1f;
constexpr float CrabSize       = 0.25f;    // m: a car's centre this close runs it over
constexpr float SpiderSpeed    = 0.25f, SpiderFlee = 1.65f, SpiderTurn = 0.67f;
constexpr float SpiderScare    = 1.8f, SpiderArrive = 0.05f;
constexpr float DogSees        = 25.f;     // m: the dog is there (cylinder round its place, 10 m high)
constexpr float DogSpeed       = 1.7f;     // m/s
constexpr float DogTurnRate    = 6.f;      // per s, how fast its turning changes
constexpr float DogCloseIn     = 0.33f;    // per s: how fast it moves its target nearer to / further from the car
constexpr float DogSlowCar     = 6.f;      // km/h: a car slower than this lets the dog close in while it barks
constexpr float DogRadius      = 0.2f;

float horiz(const Vec3& v) { return std::sqrt(v.x*v.x+v.z*v.z); }

float wrap(float a) {
  while(a> Pi) a -= 2*Pi;
  while(a<-Pi) a += 2*Pi;
  return a;
  }

Vec3 facing(float yaw) { return {std::sin(yaw),0,std::cos(yaw)}; }

// "name(string) = "beach_1_man"" or "clother(int) = 1" of an INST script
std::string scriptValue(const std::string& text, std::string_view key) {
  size_t at = text.find(key);
  if(at==std::string::npos)
    return {};
  at = text.find('=',at);
  if(at==std::string::npos)
    return {};
  ++at;
  while(at<text.size() && std::isspace(uint8_t(text[at])))
    ++at;
  if(at<text.size() && text[at]=='"') {
    const size_t end = text.find('"',at+1);
    return end==std::string::npos ? std::string() : text.substr(at+1,end-at-1);
    }
  size_t end = at;
  while(end<text.size() && !std::isspace(uint8_t(text[end])))
    ++end;
  return text.substr(at,end-at);
  }

}

struct Characters::Body {
  std::unique_ptr<btSphereShape>        sphere;
  std::unique_ptr<btCompoundShape>      shape;
  std::unique_ptr<btDefaultMotionState> motion;
  std::unique_ptr<btRigidBody>          rigid;
  bool                                  inWorld = false;
  };

struct Characters::State {
  enum class Act : uint8_t { None, Kick, Throw };

  Actor             actor;
  const SbInstance* inst  = nullptr;
  Vec3              home;
  float             homeYaw = 0;
  float             scale   = 1;
  Vec3              pos;
  float             yaw     = 0;
  bool              active  = false;
  bool              waitExit = false;  // crab: run over, back once the player has been away

  // animation
  int32_t           seq  = -1;
  float             t    = 0;
  float             rate = 1;
  bool              loop = true;
  bool              done = false;
  // sequences by role
  int32_t           sStand = -1, sWalk = -1, sKick = -1, sThrow = -1, sHurt = -1, sShoot = -1;
  int32_t           sLook = -1, sTakeOff = -1, sFly = -1, sRun = -1, sRunFast = -1, sHide = -1, sAttack = -1;
  int32_t           sHit = -1;
  int32_t           hand = -1;         // bone: LHANDeff (the car in a person's hand)

  // people
  PeoplePath        path;
  float             pathTime = 0;
  Vec3              lastPos;
  Act               act      = Act::None;
  float             actTime  = 0;
  bool              holding  = false;
  float             noHit    = 0;      // s the car can't hurt this one (just kicked or thrown)
  float             sinceHurt = 1e3f;  // s since a car hurt this one: the voice once per hit
  std::string       voice;

  // animals
  int               state = 0;
  float             timer = 0;
  Vec3              target;
  float             turn  = 0;         // dog: turning input, smoothed
  float             reach = 1;         // dog: 0..1, how far in front of the car it wants to be
  bool              dead  = false;     // spider
  float             climb = 0;         // seagull: vertical speed
  std::vector<Vec3> holes;             // spider

  // vulture
  FlightPath        flight;
  float             flightTime = 0;
  Mat4              orient;            // its rotation along the path

  std::unique_ptr<Body> body;
  };

Characters::Characters(PhysicsWorld& world, ObjectLibrary& library, const SbScene& trackScene,
                       std::string_view name, Reader pack)
  :owner(world), lib(library), track(trackScene), trackName(name) {
  load(pack);
  for(auto& in:track.instances) {
    auto* m = lib.find(in);
    if(m==nullptr || m->kind!=ObjectLibrary::Kind::Character)
      continue;
    auto s = std::make_unique<State>();
    s->inst = &in;
    s->actor.model = m;
    s->actor.name  = in.name;
    try {
      setup(*s,in,pack);
      }
    catch(const std::exception& e) {
      std::fprintf(stderr,"character %s: %s\n",in.name.c_str(),e.what());
      continue;
      }
    view.push_back(&s->actor);
    states.push_back(std::move(s));
    }
  restart();
  }

Characters::~Characters() {
  for(auto& s:states)
    if(s->body && s->body->inWorld)
      owner.bullet().removeRigidBody(s->body->rigid.get());
  }

void Characters::load(const Reader& pack) {
  auto ini = [&](const char* file) {
    try {
      const auto d = pack(std::string("Settings\\")+file);
      return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
      }
    catch(const std::exception&) {
      return SliderFile();
      }
    };
  const SliderFile people = ini("People.ini"), gull = ini("seagull.ini"), spider = ini("spider.ini");
  const SliderFile dog = ini("dog.ini"), vulture = ini("vulture.ini");
  auto pc = [](const SliderFile& f, const char* key, float def) { // 1/100 of the slider
    return f.find(key) ? float(f.raw(key))*0.01f : def;
    };
  prm.speedForw  = pc(people,"speedForw",prm.speedForw);
  prm.gullRun    = pc(gull,"SpeedRun",prm.gullRun);
  prm.gullFlight = pc(gull,"SpeedFlight",prm.gullFlight);
  prm.gullHeight = pc(gull,"FlightHeight",prm.gullHeight);
  prm.gullAngle  = gull.find("VertAngleMax") ? float(gull.raw("VertAngleMax")) : prm.gullAngle;
  prm.gullIdle   = pc(gull,"TimeIdle",prm.gullIdle);
  prm.gullTurn   = pc(gull,"TimeTurn",prm.gullTurn);
  prm.spiderRad  = pc(spider,"CdtRad",prm.spiderRad);
  prm.spiderFloor     = spider.find("HeightFloor")     ? float(spider.raw("HeightFloor"))*0.001f     : prm.spiderFloor;
  prm.spiderFloorDead = spider.find("HeightFloorDead") ? float(spider.raw("HeightFloorDead"))*0.001f : prm.spiderFloorDead;
  prm.dogCone    = dog.find("ConeAngle") ? float(dog.raw("ConeAngle")) : prm.dogCone;
  prm.dogNear    = pc(dog,"RadNear",prm.dogNear);
  prm.dogFarNear = pc(dog,"RadFar_Near",prm.dogFarNear);
  prm.dogFarFar  = pc(dog,"RadFar_Far",prm.dogFarFar);
  prm.dogReach   = pc(dog,"RadReach",prm.dogReach);
  prm.dogAttack  = pc(dog,"TimeAttack",prm.dogAttack);
  prm.dogAnim    = pc(dog,"AniSpeed",prm.dogAnim);
  prm.vultureVel   = pc(vulture,"FlyVel",prm.vultureVel);
  prm.vultureScale = pc(vulture,"Scale",prm.vultureScale);
  if(vulture.find("ShiftY"))
    prm.vultureShiftY = (float(vulture.raw("ShiftY"))-100.f)*0.1f;
  }

void Characters::setup(State& s, const SbInstance& in, const Reader& pack) {
  const auto& model = *s.actor.model;
  const std::string& n = model.name;
  Kind kind;
  if(n=="Man" || n=="Woman" || n=="RepairMan") kind = Kind::Person;
  else if(n=="Guard")   kind = Kind::Guard;
  else if(n=="Seagull") kind = Kind::Seagull;
  else if(n=="Crab")    kind = Kind::Crab;
  else if(n=="Spider")  kind = Kind::Spider;
  else if(n=="Dog")     kind = Kind::Dog;
  else if(n=="Vulture") kind = Kind::Vulture;
  else throw std::runtime_error("unknown character model \""+n+"\"");
  s.actor.kind = kind;

  auto& sk = skeletons[&model];
  if(sk==nullptr)
    sk = std::make_unique<Skeleton>(*model.scene,model.id);
  s.actor.skeleton = sk.get();
  auto seq = [&](const char* name) { return sk->sequence(name); };

  const Mat4& w = in.world;
  s.home    = w.translation();
  const Vec3 f = w.vector({0,0,1});
  s.homeYaw = std::atan2(f.x,f.z);
  s.scale   = Vec3{w.m[0][0],w.m[1][0],w.m[2][0]}.length();

  auto need = [&](const void* p, const char* what) {
    if(p==nullptr)
      throw std::runtime_error(std::string(what)+" not found");
    };
  const std::string clother = scriptValue(in.script,"clother");
  const bool        dressed = !clother.empty() && clother!="0";

  switch(kind) {
    case Kind::Person: {
      s.sStand = seq("Stand"); s.sWalk = seq("Walk"); s.sKick = seq("Pendal");
      s.sThrow = seq("Brosok"); s.sHurt = seq("Hurt");
      s.hand   = sk->bone("LHANDeff");
      s.voice  = n=="Woman" ? "woman_voice" : "man_voice";
      if(dressed)  // 0x5101c0
        s.actor.textures = {{"man_1","man1_1"},{"man_2","man2_2"}};
      const std::string path = scriptValue(in.script,"name");
      if(!path.empty())
        s.path = PeoplePath::load(pack("Profiles\\"+path+".dat"));
      makeBody(s);
      break;
      }
    case Kind::Guard: {
      s.sStand = seq("stand"); s.sKick = seq("kick"); s.sShoot = seq("shoot");
      s.voice  = "man_voice";
      need(in.volume("visibilityVolume"),"visibilityVolume");
      need(in.volume("guardVolume"),"guardVolume");
      if(dressed)  // 0x51a7d0
        s.actor.textures = {{"military_man_1","military_man2_1"},{"military_man_2","military_man2_2"}};
      makeBody(s);
      break;
      }
    case Kind::Seagull:
      s.sStand = seq("flight_idle"); s.sLook = seq("look_around"); s.sWalk = seq("walk");
      s.sTakeOff = seq("start_flight"); s.sFly = seq("flight");
      need(in.volume("visibilityVolume"),"visibilityVolume");
      need(in.volume("inhabitVolume"),"inhabitVolume");
      need(in.marker("flightDir"),"flightDir marker");
      break;
    case Kind::Crab:
      s.sWalk = seq("WALK"); s.sHit = seq("HIT");
      need(in.volume("visibilityVolume"),"visibilityVolume");
      need(in.marker("seahole1"),"seahole1 marker");
      need(in.marker("landhole1"),"landhole1 marker");
      break;
    case Kind::Spider:
      s.sRun = seq("run"); s.sRunFast = seq("runQuick"); s.sHide = seq("hide");
      need(in.volume("visibilityVolume"),"visibilityVolume");
      for(int i=1; i<=32; ++i) {
        const auto* m = in.marker("hole"+std::to_string(i));
        if(m==nullptr)
          break;
        s.holes.push_back(m->position());
        }
      if(s.holes.size()<2)
        throw std::runtime_error("too few holes");
      break;
    case Kind::Dog:
      s.sRun = seq("run"); s.sStand = seq("stand"); s.sAttack = seq("attack");
      need(in.volume("attackVolume"),"attackVolume");
      if(dressed)  // 0x518e20
        s.actor.textures = {{"dog","dog_1"}};
      makeBody(s);
      break;
    case Kind::Vulture: {
      // vultures take the two paths of the track in turn, the third and fourth start half way round (0x51b290)
      const int k = vultures++;
      s.flight = FlightPath::load(pack("Profiles\\vlt_"+trackName+"_"+std::to_string(k%2+1)+".gpf"));
      s.flightTime = k>1 ? s.flight.duration()*0.5f : 0.f;
      s.scale = prm.vultureScale;
      break;
      }
    }
  }

void Characters::makeBody(State& s) {
  auto b = std::make_unique<Body>();
  const bool dog = s.actor.kind==Kind::Dog;
  b->sphere = std::make_unique<btSphereShape>((dog ? DogRadius : BodyRadius)*S);
  b->shape  = std::make_unique<btCompoundShape>();
  for(int i=0; i<(dog ? 1 : 3); ++i) {
    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(0,(dog ? DogRadius : 0.2f*float(i))*S,0));
    b->shape->addChildShape(t,b->sphere.get());
    }
  btTransform start;
  start.setIdentity();
  b->motion = std::make_unique<btDefaultMotionState>(start);
  btRigidBody::btRigidBodyConstructionInfo ci(0,b->motion.get(),b->shape.get());
  ci.m_friction    = 0.5f;
  ci.m_restitution = 0.2f;
  b->rigid = std::make_unique<btRigidBody>(ci);
  b->rigid->setCollisionFlags(b->rigid->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
  b->rigid->setActivationState(DISABLE_DEACTIVATION);
  b->rigid->setUserIndex(PhysicsWorld::CharacterTag);
  s.body = std::move(b);
  }

void Characters::placeBody(State& s) {
  if(s.body==nullptr)
    return;
  auto& b = *s.body;
  const bool want = s.actor.visible && !s.holding && s.noHit<=0;
  if(want!=b.inWorld) {
    if(want)
      owner.bullet().addRigidBody(b.rigid.get());
    else
      owner.bullet().removeRigidBody(b.rigid.get());
    b.inWorld = want;
    }
  const btTransform t = toBt(Mat4::translate(s.pos)*Mat4::rotate(1,s.yaw*180.f/Pi));
  b.motion->setWorldTransform(t);
  if(!b.inWorld)
    b.rigid->setWorldTransform(t);
  }

void Characters::restart() {
  carKmh = lastKmh = 0;
  for(auto& p:states) {
    State& s = *p;
    s.pos      = s.home;
    s.yaw      = s.homeYaw;
    s.lastPos  = s.pos;
    s.active   = false;
    s.waitExit = false;
    s.seq      = -1;
    s.t        = 0;
    s.act      = State::Act::None;
    s.holding  = false;
    s.noHit    = 0;
    s.pathTime = 0;
    s.state    = 0;
    s.dead     = false;
    s.actor.visible = s.actor.kind==Kind::Person || s.actor.kind==Kind::Vulture;
    if(!s.path.empty()) {
      const auto pt = s.path.at(0);
      s.pos = pt.pos;
      if(horiz(pt.dir)>1e-4f)
        s.yaw = std::atan2(pt.dir.x,pt.dir.z);
      s.lastPos = s.pos;
      }
    play(s,s.sStand>=0 ? s.sStand : s.sWalk,true);
    finish(s);
    }
  sincePerson = sinceDog = sinceGuard = 1e3f;
  }

float Characters::random() {
  return std::uniform_real_distribution<float>(0.f,1.f)(rng);
  }

float Characters::ground(const Vec3& at, float fallback) const {
  if(auto h = owner.track().groundHeight(at+Vec3{0,0.5f,0},5.f))
    return *h;
  return fallback;
  }

void Characters::play(State& s, int32_t seq, bool loop, float rate) {
  if(seq<0)
    return;
  if(s.seq!=seq) {
    s.seq  = seq;
    s.t    = rate<0 ? s.actor.skeleton->sequences()[size_t(seq)].duration : 0.f;
    s.done = false;
    }
  s.loop = loop;
  s.rate = rate;
  }

void Characters::animate(State& s, float dt) {
  if(s.seq<0)
    return;
  const float len = s.actor.skeleton->sequences()[size_t(s.seq)].duration;
  s.t += dt*s.rate;
  if(s.loop) {
    if(len>0) {
      s.t = std::fmod(s.t,len);
      if(s.t<0)
        s.t += len;
      }
    }
  else if(s.t>=len || s.t<0) {
    s.t    = std::clamp(s.t,0.f,len);
    s.done = true;
    }
  }

void Characters::turnTo(State& s, const Vec3& dir, float maxRad) {
  if(horiz(dir)<1e-5f)
    return;
  const float d = wrap(std::atan2(dir.x,dir.z)-s.yaw);
  s.yaw = wrap(s.yaw+std::clamp(d,-maxRad,maxRad));
  }

void Characters::finish(State& s) {
  const Mat4 turn = s.actor.kind==Kind::Vulture ? s.orient : Mat4::rotate(1,s.yaw*180.f/Pi);
  s.actor.world = Mat4::translate(s.pos)*turn*Mat4::scale({s.scale,s.scale,s.scale});
  s.actor.seq   = s.seq;
  s.actor.time  = s.t;
  placeBody(s);
  }

bool Characters::hitBy(State& s, Vehicle* player, float kmh) {
  if(player==nullptr || s.body==nullptr || !s.body->inWorld)
    return false;
  if(carKmh<=kmh)
    return false;
  const btCollisionObject* car  = &player->chassis();
  const btCollisionObject* body = s.body->rigid.get();
  auto& disp = *owner.bullet().getDispatcher();
  for(int i=0; i<disp.getNumManifolds(); ++i) {
    const btPersistentManifold* m = disp.getManifoldByIndexInternal(i);
    if(!((m->getBody0()==car && m->getBody1()==body) || (m->getBody0()==body && m->getBody1()==car)))
      continue;
    for(int c=0; c<m->getNumContacts(); ++c)
      if(m->getContactPoint(c).getDistance()<=0.01f*S)
        return true;
    }
  return false;
  }

bool Characters::touches(Car& c, const Vec3& at, float radius, float height) {
  if(horiz(c.pos-at)>=radius+OtherCarSize || c.pos.y<at.y-0.3f || c.pos.y>at.y+height+0.3f)
    return false;
  c.hit = true;
  return true;
  }

bool Characters::update(float dt, Vehicle* player, std::vector<Car>& cars) {
  greatHit = false;
  grabbed  = false;
  carried  = false;
  guns.clear();
  // the speed the car hit with: the step that brought it into contact has already stopped it
  const float nowKmh = player ? player->velocity().length()*3.6f : 0.f;
  carKmh   = std::max(nowKmh,lastKmh);
  lastKmh  = nowKmh;
  sincePerson += dt;
  sinceDog    += dt;
  sinceGuard  += dt;
  for(auto& p:states) {
    State& s = *p;
    s.actor.sound.clear();
    switch(s.actor.kind) {
      case Kind::Person:  person (s,dt,player,cars); break;
      case Kind::Guard:   guard  (s,dt,player,cars); break;
      case Kind::Seagull: seagull(s,dt,player,cars); break;
      case Kind::Crab:    crab   (s,dt,player,cars); break;
      case Kind::Spider:  spider (s,dt,player,cars); break;
      case Kind::Dog:     dog    (s,dt,player,cars); break;
      case Kind::Vulture: vulture(s,dt);             break;
      }
    finish(s);
    }
  return greatHit;
  }

// --- people (0x510400) ---

void Characters::person(State& s, float dt, Vehicle* player, std::vector<Car>& cars) {
  s.noHit = std::max(0.f,s.noHit-dt);
  // the path goes on only while the person stands or walks (0x511b50)
  const bool free = s.seq<0 || s.seq==s.sStand || s.seq==s.sWalk;
  if(!s.path.empty() && free) {
    s.pathTime += dt;
    const auto pt = s.path.at(s.pathTime);
    s.pos = pt.pos;
    turnTo(s,pt.dir,PathTurn*dt);
    }
  const float moved = horiz(s.pos-s.lastPos)/std::max(dt,1e-4f);
  s.lastPos = s.pos;

  const Vec3 car = player ? player->pose().translation() : Vec3{0,1e6f,0};
  s.active = player!=nullptr && (car-s.pos).length()<PeopleActive;
  s.actor.visible = s.active;

  // hit by a car faster than 15 km/h: hurt (0x510b30); the player's hit is "Wielkie Łup!"
  bool hurt = false;
  if(s.noHit<=0 && hitBy(s,player,HurtSpeed)) {
    hurt = true;
    if(sincePerson>HitPause) {
      sincePerson = 0;
      greatHit    = true;
      player->addBoost(HitBoost);
      }
    }
  if(s.active)
    for(auto& c:cars)
      if(touches(c,s.pos,BodyRadius,0.6f) && c.velocity.length()*3.6f>HurtSpeed)
        hurt = true;
  if(hurt && s.seq!=s.sHurt && s.sHurt>=0) {
    if(s.holding)
      s.holding = false;
    s.act = State::Act::None;
    play(s,s.sHurt,false);
    s.actor.sound = s.voice;   // the voice when the animation turns to Hurt (0x510714)
    }

  if(s.active && s.seq!=s.sHurt)
    attack(s,dt,player);

  // Brosok, Pendal and Hurt play once, then the person stands (0x50f780)
  if(s.done && (s.seq==s.sHurt || s.seq==s.sKick || s.seq==s.sThrow)) {
    s.act = State::Act::None;
    play(s,s.sStand,true);
    }
  if(s.seq<0 || s.seq==s.sStand || s.seq==s.sWalk)
    play(s,moved>0.01f && s.sWalk>=0 ? s.sWalk : s.sStand,true);
  animate(s,dt);
  }

bool Characters::attack(State& s, float dt, Vehicle* player) {
  if(player==nullptr)
    return false;
  const bool guard = s.actor.kind==Kind::Guard;
  const Mat4 carPose = player->pose();
  const Vec3 car  = carPose.translation();
  const Vec3 to   = car-s.pos;
  const float dist = horiz(to);

  if(s.act==State::Act::None) {
    const bool idle = s.seq<0 || s.seq==s.sStand || s.seq==s.sWalk || s.seq==s.sShoot;
    if(!idle || std::abs(to.y)>KickHeight)
      return false;
    if(dist<KickReach) {
      // 0x510f40: a guard always kicks; people kick four times in five, otherwise grab the car if there is room
      s.act = State::Act::Kick;
      if(!guard && random()<=GrabChance && s.sThrow>=0) {
        const Vec3 from = s.pos+Vec3{0,0.3f,0};
        if(!owner.track().raycast(from,{0,1,0},GrabRoom))
          s.act = State::Act::Throw;
        }
      }
    else if(!guard && dist<RushDistance && player->velocity().length()>RushSpeed) {
      const float a = std::abs(wrap(std::atan2(to.x,to.z)-s.yaw))*180.f/Pi;
      if(a<RushAngle)
        s.act = State::Act::Kick;
      }
    if(s.act==State::Act::None)
      return false;
    s.actTime = 0;
    play(s,s.act==State::Act::Kick ? s.sKick : s.sThrow,false);
    return true;
    }

  if(s.act==State::Act::Kick) {
    // 0x511470: turn to the car; half a second in, the foot meets it if it is still there
    turnTo(s,to,TurnToCar*dt);
    s.actTime += dt;
    if(s.actTime>=KickWait) {
      if(dist>KickDistance)
        s.actTime = 0;
      else if(s.seq==s.sKick && s.t>KickFrom && s.t<KickTo) {
        Vec3 out = to;
        out.y = 0;
        out = horiz(out)>1e-4f ? out*(1.f/horiz(out)) : facing(s.yaw);
        player->addVelocity(out*KickOut+Vec3{0,KickUp,0}-player->velocity());
        s.actTime = 0;
        s.noHit   = 0.5f;
        }
      }
    if(s.done)
      s.act = State::Act::None;
    return true;
    }

  // Throw: 0x5111f0 takes the car in the left hand, 0x510b90 throws it
  if(!s.holding) {
    turnTo(s,to,TurnToCar*dt);
    if(s.seq==s.sThrow && s.t>GrabFrom && s.t<GrabTo && dist<GrabDistance && s.hand>=0)
      s.holding = grabbed = true;
    }
  if(s.holding) {
    std::vector<Mat4> pose;
    s.actor.skeleton->pose(s.seq,s.t,pose);
    const Mat4 world = Mat4::translate(s.pos)*Mat4::rotate(1,s.yaw*180.f/Pi)*Mat4::scale({s.scale,s.scale,s.scale});
    const Vec3 at    = world.point(pose[size_t(s.hand)].translation());
    player->hold(Mat4::translate(at-Vec3{0,0.15f,0})*Mat4::rotate(1,s.yaw*180.f/Pi));
    if(s.t>ThrowAt || s.done) {
      s.holding = false;
      s.noHit   = 0.5f;
      player->addVelocity(facing(s.yaw)*ThrowForward+Vec3{0,ThrowUp,0}-player->velocity());
      }
    }
  carried = carried || s.holding;
  if(s.done) {
    s.act     = State::Act::None;
    s.holding = false;
    }
  return true;
  }

// --- guard (0x51a950) ---

void Characters::guard(State& s, float dt, Vehicle* player, std::vector<Car>& cars) {
  s.noHit = std::max(0.f,s.noHit-dt);
  const Vec3 car  = player ? player->pose().translation() : Vec3{0,1e6f,0};
  const bool sees = player!=nullptr && s.inst->volume("visibilityVolume")->contains(car);
  const bool aims = player!=nullptr && s.inst->volume("guardVolume")->contains(car);
  s.actor.visible = sees || aims;
  if(!s.actor.visible) {
    s.act = State::Act::None;
    play(s,s.sStand,true);
    return;
    }
  for(auto& c:cars)
    touches(c,s.pos,BodyRadius,0.6f);
  if(s.noHit<=0 && hitBy(s,player,GuardHitSpeed) && sinceGuard>HitPause) {
    sinceGuard = 0;
    greatHit   = true;
    player->addBoost(HitBoost);
    }
  if(!attack(s,dt,player)) {
    // turns to face the car (0x510a40, 360 degrees per second) and shoots while it is in the guard volume
    turnTo(s,car-s.pos,TurnToCar*dt);
    play(s,aims && s.sShoot>=0 ? s.sShoot : s.sStand,true);
    // the gun, 1.5 m above its feet (0x51a950 sends BURST_MNG the message 0x261 from there)
    if(aims)
      guns.push_back(s.pos+Vec3{0,1.5f,0});
    }
  else if(s.done) {
    s.act = State::Act::None;
    play(s,s.sStand,true);
    }
  animate(s,dt);
  }

// --- seagull (0x512b30) ---

void Characters::seagull(State& s, float dt, Vehicle* player, const std::vector<Car>& cars) {
  const SbVolume& vis  = *s.inst->volume("visibilityVolume");
  const SbVolume& land = *s.inst->volume("inhabitVolume");
  const Vec3      car  = player ? player->pose().translation() : Vec3{0,1e6f,0};
  const bool      sees = player!=nullptr && vis.contains(car);

  // a random spot of the inhabit volume (a parallelogram: corners 0, 1, 2), in the half away from where it is
  auto spot = [&](bool away) {
    const auto& d = land.data;
    if(d.size()<8)
      return s.home;
    const Vec3 a = {d[2],0,d[3]}, b = {d[4],0,d[5]}, c = {d[6],0,d[7]};
    const Vec3 e1 = (a-b)*0.5f, e2 = (c-b)*0.5f, mid = b+e1+e2;
    float u = random()*2.f-1.f, v = random()*2.f-1.f;
    if(away) {
      const Vec3 r = s.pos-mid;
      // the side of the gull along each edge, then the other one
      u = std::abs(u)*((r.x*e1.x+r.z*e1.z)>0 ? -1.f : 1.f);
      v = std::abs(v)*((r.x*e2.x+r.z*e2.z)>0 ? -1.f : 1.f);
      }
    Vec3 p = mid+e1*u+e2*v;
    p.y = ground(Vec3{p.x,std::max(d[1],s.home.y+2.f),p.z},s.home.y);
    return p;
    };

  if(!sees) {
    s.active = false;
    s.actor.visible = false;
    return;
    }
  if(!s.active) {
    // 1: looking round for TimeTurn seconds, at a random spot on the ground
    s.active = true;
    s.state  = 1;
    s.timer  = prm.gullTurn;
    s.pos    = spot(false);
    s.climb  = 0;
    s.yaw    = random()*2*Pi;
    }
  s.actor.visible = true;

  if(s.state==1 || s.state==2) {
    // 0x513100: people, dogs and cars within 4 m scare it
    bool scared = horiz(car-s.pos)<GullScare;
    for(auto& c:cars)
      scared |= horiz(c.pos-s.pos)<GullScare;
    for(auto& o:states)
      if((o->actor.kind==Kind::Person || o->actor.kind==Kind::Dog) && o->actor.visible)
        scared |= horiz(o->pos-s.pos)<GullScare;
    if(scared) {
      s.state = 0x10;
      s.timer = prm.gullIdle;
      s.actor.sound = "seagull_vzliot";
      }
    }

  float speed = 0;
  switch(s.state) {
    case 1:
      s.timer -= dt;
      if(s.timer<0) {
        s.state  = 2;
        s.target = spot(true);
        }
      break;
    case 2: {
      const Vec3 to = s.target-s.pos;
      if(horiz(to)<GullArrive) {
        s.state = 1;
        s.timer = prm.gullTurn;
        break;
        }
      const float a = wrap(std::atan2(to.x,to.z)-s.yaw);
      if(std::abs(a)>0.05f)
        s.yaw = wrap(s.yaw+(a>0 ? 1.f : -1.f)*std::min(GullWalkTurn*dt,std::abs(a)));
      speed = prm.gullRun;
      break;
      }
    case 0x10:
      s.timer -= dt;
      if(s.timer<0) {
        s.state  = 4;
        s.target = s.inst->marker("flightDir")->position();
        }
      break;
    case 4: {
      const Vec3  to = s.target-s.pos;
      const float a  = wrap(std::atan2(to.x,to.z)-s.yaw);
      if(std::abs(a)>=0.1f)
        s.yaw = wrap(s.yaw+(a>0 ? 1.f : -1.f)*std::min(GullFlyTurn*dt,std::abs(a)));
      // climbing at VertAngleMax off the ground, level at FlightHeight
      const float h     = s.pos.y-ground(s.pos,s.pos.y-100.f);
      const float angle = h<0 ? prm.gullAngle : (h<=prm.gullHeight ? prm.gullAngle*(1.f-h/prm.gullHeight) : 0.f);
      const float v     = prm.gullFlight*GullFlySpeed;
      speed = v*std::cos(angle*Pi/180.f);
      s.pos.y += v*std::sin(angle*Pi/180.f)*dt;
      break;
      }
    }
  s.pos += facing(s.yaw)*(speed*dt);
  if(s.state!=4)
    s.pos.y = ground(s.pos,s.pos.y);

  // 0x5137c0: 1 look_around, 2 walk, 4 flight, otherwise start_flight
  switch(s.state) {
    case 1:  play(s,s.sLook,true);    break;
    case 2:  play(s,s.sWalk,true);    break;
    case 4:  play(s,s.sFly,true);     break;
    default: play(s,s.sTakeOff,false); break;
    }
  animate(s,dt);
  }

// --- crab (0x514100) ---

void Characters::crab(State& s, float dt, Vehicle* player, std::vector<Car>& cars) {
  const Vec3 car  = player ? player->pose().translation() : Vec3{0,1e6f,0};
  const bool sees = player!=nullptr && s.inst->volume("visibilityVolume")->contains(car);
  if(!sees) {
    s.active   = false;
    s.waitExit = false;
    s.actor.visible = false;
    return;
    }
  if(s.waitExit) {
    s.actor.visible = false;
    return;
    }
  const Vec3 sea  = s.inst->marker("seahole1")->position();
  const Vec3 land = s.inst->marker("landhole1")->position();
  if(!s.active) {
    // comes out somewhere between the holes, side on to the car, and threatens it (HIT) first
    const float k = random();
    s.pos    = sea*k+land*(1.f-k);
    s.pos.y  = ground(s.pos+Vec3{0,0.5f,0},s.pos.y);
    const Vec3 to = car-s.pos;
    s.yaw    = std::atan2(to.z,-to.x);
    s.active = true;
    s.seq    = -1;
    play(s,s.sHit,false);
    }
  s.actor.visible = true;

  // run over (0x534670), by any car
  bool squashed = horiz(car-s.pos)<CrabSize*s.scale && std::abs(car.y-s.pos.y)<0.3f;
  for(auto& c:cars)
    squashed |= touches(c,s.pos,CrabSize*s.scale-OtherCarSize,0.1f);
  if(squashed) {
    s.active   = false;
    s.waitExit = true;
    s.actor.visible = false;
    return;
    }

  if(s.seq==s.sHit && !s.done) {
    const Vec3 to = car-s.pos;
    const Vec3 side = {to.z,0,-to.x};
    const float a = wrap(std::atan2(side.x,side.z)-s.yaw);
    if(std::abs(a)>0.05f)
      s.yaw = wrap(s.yaw+(a>0 ? 1.f : -1.f)*std::min(CrabTurn*dt,std::abs(a)));
    animate(s,dt);
    return;
    }

  // to the sea hole, forwards or backwards, whichever way it faces
  const Vec3 to = sea-s.pos;
  if(horiz(to)<CrabArrive) {
    s.active = false;   // gone into the hole; another one comes out
    s.actor.visible = false;
    return;
    }
  Vec3        dir   = facing(s.yaw);
  float       speed = CrabSpeed;
  if(dir.x*to.x+dir.z*to.z<0) {
    dir   = dir*-1.f;
    speed = -CrabSpeed;
    }
  const float a = wrap(std::atan2(to.x,to.z)-std::atan2(dir.x,dir.z));
  if(std::abs(a)>0.05f)
    s.yaw = wrap(s.yaw+(a>0 ? 1.f : -1.f)*std::min(CrabTurn*dt,std::abs(a)));
  s.pos += facing(s.yaw)*(speed*dt);
  s.pos.y = ground(s.pos,s.pos.y);
  play(s,s.sWalk,true,speed<0 ? -1.f : 1.f);
  animate(s,dt);
  }

// --- spider (0x515570) ---

void Characters::spider(State& s, float dt, Vehicle* player, std::vector<Car>& cars) {
  const Vec3 car  = player ? player->pose().translation() : Vec3{0,1e6f,0};
  const bool sees = player!=nullptr && s.inst->volume("visibilityVolume")->contains(car);
  if(!sees) {
    s.active = false;
    s.actor.visible = false;
    return;
    }
  if(!s.active) {
    // out of one hole, off to another one
    const size_t n    = s.holes.size();
    const size_t from = size_t(random()*float(n))%n;
    size_t       to   = size_t(random()*float(n))%n;
    while(to==from)
      to = (to+1)%n;
    s.pos    = s.holes[from];
    s.target = s.holes[to];
    const Vec3 d = s.target-s.pos;
    s.yaw    = std::atan2(d.x,d.z);
    s.active = true;
    s.dead   = false;
    s.seq    = -1;
    }
  s.actor.visible = true;
  const float floor = ground(s.pos,s.pos.y);

  if(!s.dead && horiz(car-s.pos)<prm.spiderRad+0.1f && std::abs(car.y-s.pos.y)<0.3f)
    s.dead = true;  // under the wheels (0x534770), of any car
  if(s.actor.visible)
    for(auto& c:cars)
      if(!s.dead && touches(c,s.pos,prm.spiderRad+0.1f-OtherCarSize,0.1f))
        s.dead = true;
  if(s.dead) {
    s.pos.y = floor+prm.spiderFloorDead;
    play(s,s.sHide,false);
    animate(s,dt);
    return;
    }

  // the nearest car within 1.8 m: away from it, across its way (0x5158e0)
  const Vec3* fear = nullptr;
  Vec3        fearVel;
  float       best = SpiderScare;
  if(player && horiz(car-s.pos)<best) {
    best = horiz(car-s.pos);
    fear = &car;
    fearVel = player->velocity();
    }
  for(auto& c:cars)
    if(horiz(c.pos-s.pos)<best) {
      best = horiz(c.pos-s.pos);
      fear = &c.pos;
      fearVel = c.velocity;
      }

  Vec3  dir;
  float speed;
  if(fear!=nullptr) {
    Vec3 v = fearVel;
    if(horiz(v)<1e-3f)
      v = s.pos-*fear;
    dir = Vec3{v.z,0,-v.x};
    if(dir.x*(s.pos.x-fear->x)+dir.z*(s.pos.z-fear->z)<0)
      dir = dir*-1.f;
    speed = SpiderFlee;
    play(s,s.sRunFast,true,3.f);
    }
  else {
    dir = s.target-s.pos;
    if(horiz(dir)<SpiderArrive) {
      s.active = false;   // into the hole; another one comes out
      s.actor.visible = false;
      return;
      }
    speed = SpiderSpeed;
    play(s,s.sRun,true);
    }
  const float a = wrap(std::atan2(dir.x,dir.z)-s.yaw);
  if(std::abs(a)>0.05f)
    s.yaw = wrap(s.yaw+(a>0 ? 1.f : -1.f)*std::min(SpiderTurn*dt,std::abs(a)));
  s.pos  += facing(s.yaw)*(speed*dt);
  s.pos.y = ground(s.pos,s.pos.y)+prm.spiderFloor;
  animate(s,dt);
  }

// --- dog (0x519400) ---

void Characters::dog(State& s, float dt, Vehicle* player, std::vector<Car>& cars) {
  const Vec3 car = player ? player->pose().translation() : Vec3{0,1e6f,0};
  const Vec3 rel = car-s.home;
  const bool sees = player!=nullptr && horiz(rel)<DogSees && rel.y>-5.f && rel.y<5.f;
  if(!sees) {
    s.active = false;
    s.actor.visible = false;
    return;
    }
  if(!s.active) {
    // back at its place, standing
    s.active = true;
    s.pos    = s.home;
    s.pos.y  = ground(s.pos,s.pos.y);
    s.yaw    = s.homeYaw;
    s.state  = 1;
    s.reach  = 1;
    s.turn   = 0;
    s.timer  = 0;
    }
  s.actor.visible = true;

  // any touch pushes the car off the dog: 0.5 m/s away from it and 0.12 m/s up (0x5349d0 -> 0x4f3470)
  s.sinceHurt += dt;
  bool hurt = false;
  for(auto& c:cars)
    hurt |= touches(c,s.pos,DogRadius,0.4f) && c.velocity.length()*3.6f>HurtSpeed;
  if(hitBy(s,player,-1.f)) {
    Vec3 away = player->pose().translation()-s.pos;
    away.y = 0;
    away = horiz(away)>1e-4f ? away*(1.f/horiz(away)) : Vec3{};
    player->addVelocity(away*DogPushOut+Vec3{0,DogPushUp,0});
    }
  if(hitBy(s,player,HurtSpeed)) {
    hurt = true;
    if(sinceDog>HitPause) {
      sinceDog = 0;
      greatHit = true;
      player->addBoost(HitBoost);
      }
    }
  // whines once a hit, not on every step the car stays on it
  if(hurt && s.sinceHurt>DogPainPause)
    s.actor.sound = "dog_pain";
  if(hurt)
    s.sinceHurt = 0;

  const bool chase = s.inst->volume("attackVolume")->contains(car);
  bool run = false;
  if(!chase) {
    s.state = 1;
    }
  else {
    const Vec3 vel     = player->velocity();
    const float carKmh = vel.length()*3.6f;
    // how far in front of the car: closer while it barks at a slow car
    if(s.state!=0x20 || carKmh>=DogSlowCar)
      s.reach = std::min(1.f,s.reach+DogCloseIn*dt);
    else
      s.reach = std::max(0.f,s.reach-DogCloseIn*dt);
    bool reached;
    Vec3 aim = car;
    if(s.state==0x20 && s.timer<prm.dogAttack) {
      s.timer += dt;
      reached = true;
      }
    else {
      // 0x519b80: a point in front of the car, within ConeAngle of its heading, RadNear .. RadFar away
      const Vec3 at  = car+vel*dt;
      Vec3       fwd = player->pose().vector({0,0,1});
      fwd.y = 0;
      fwd   = horiz(fwd)>1e-4f ? fwd*(1.f/horiz(fwd)) : Vec3{0,0,1};
      Vec3 v = s.pos-at;
      v.y = 0;
      const float yawF = std::atan2(fwd.x,fwd.z);
      const float side = wrap(std::atan2(v.x,v.z)-yawF);
      const float cone = prm.dogCone*Pi/180.f;
      if(std::abs(side)>cone)
        v = facing(yawF+(side>0 ? cone : -cone));
      const float farR = prm.dogFarNear+(prm.dogFarFar-prm.dogFarNear)*s.reach;
      const float len  = horiz(v);
      if(len<prm.dogNear)
        v = len>1e-4f ? v*(prm.dogNear/len) : fwd*prm.dogNear;
      else if(len>farR)
        v = v*(farR/len);
      aim     = at+v;
      reached = horiz(aim-s.pos)<prm.dogReach;
      if(!reached)
        s.state = 2;
      else if(s.state!=0x20) {
        s.state = 0x20;
        s.timer = 0;
        s.actor.sound = "dog_attack";
        }
      if(reached)
        aim = car;
      }
    run = !reached;

    // turning (0x519400): a little for a small angle, more the further round, most for a point behind it
    const Vec3  to = aim-s.pos;
    const float a  = wrap(std::atan2(to.x,to.z)-s.yaw);
    const bool  behind = std::abs(a)>Pi*0.5f;
    float want = 0;
    if(std::abs(a)>0.05f || behind) {
      float mag;
      if(!behind)
        mag = std::abs(a)<=1.f ? (std::abs(a)-0.05f)*3.0526f+0.1f : 3.f;
      else
        mag = std::abs(a)<=1.f ? 5.f-2.f*std::abs(a) : 3.f;
      want = (a>0 ? 1.f : -1.f)*mag;
      }
    else
      s.turn = 0;
    s.turn += std::clamp(want-s.turn,-DogTurnRate*dt,DogTurnRate*dt);
    s.yaw   = wrap(s.yaw+std::clamp(s.turn*dt,-std::abs(a),std::abs(a)));
    }

  if(run)
    s.pos += facing(s.yaw)*(DogSpeed*dt);
  s.pos.y = ground(s.pos,s.pos.y);
  // anims: run, stand, attack (0x51a100), at AniSpeed
  if(s.state==0x20)
    play(s,s.sAttack,true,prm.dogAnim);
  else if(run)
    play(s,s.sRun,true,prm.dogAnim);
  else
    play(s,s.sStand,true,prm.dogAnim);
  animate(s,dt);
  }

// --- vulture (0x51b450) ---

void Characters::vulture(State& s, float dt) {
  s.actor.visible = !s.flight.empty();
  if(s.flight.empty())
    return;
  s.flightTime += dt*prm.vultureVel;
  const auto k  = s.flight.at(s.flightTime);
  s.pos = k.pos+Vec3{0,prm.vultureShiftY,0};
  s.yaw = std::atan2(k.forward.x,k.forward.z);
  // the path's forward and up (0x44c960)
  const Vec3 f = k.forward.normalized();
  const Vec3 r = Vec3::cross(k.up,f).normalized();
  const Vec3 u = Vec3::cross(f,r);
  s.orient = Mat4();
  s.orient.m[0][0] = r.x; s.orient.m[0][1] = u.x; s.orient.m[0][2] = f.x;
  s.orient.m[1][0] = r.y; s.orient.m[1][1] = u.y; s.orient.m[1][2] = f.y;
  s.orient.m[2][0] = r.z; s.orient.m[2][1] = u.z; s.orient.m[2][2] = f.z;
  }
