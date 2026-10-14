#include "traffic.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "btconvert.h"
#include "formats/settings.h"
#include "vehicle.h"

using namespace rc;
using namespace rc::bt;

namespace {

constexpr float Pi = 3.14159265f;

// RCCars.exe constants of the road traffic (0x516db0, 0x516f50, 0x5165a0, 0x517530)
constexpr float SeenDistance = 60.f;   // m from a camera (horizontal): animated and drawn
constexpr float MovedBy      = 0.01f;  // m in some axis since the last update: the car moves, the wheels spin
constexpr float SteerScale   = 150.f;  // degrees of the front wheels per unit of the path's steering ..
constexpr float SteerMax     = 30.f;   // .. at most
constexpr float TiltScale    = 1.f/4.2f; // the pitch is atan(tilt/4.2) ..
constexpr float TiltDrop     = 3.15f;  // .. and the car is let down by 3.15/sqrt(tilt^2+4.2^2) (0.75 m level)
constexpr float PushOut      = 6.f,  PushUp     = 1.5f; // m/s given to a car touching one
constexpr float SlowPushOut  = 2.f,  SlowPushUp = 0.5f; // .. a Slow one
constexpr float OtherCarSize = 0.25f;  // m: an opponent's car around its centre, to tell what it touches
constexpr float SoundNear    = 20.f;   // m (horizontal): a camera this close starts roadcar / roadcar_siren ..
constexpr float SoundBelow   = -0.25f; // .. unless it is lower than this under the car
constexpr float LightProbe   = 0.2f;   // m above the car: the point tested against the roadcar_* volumes
constexpr float WheelRadius[2] = {0.35f, 0.45f}; // of the wheel's height: collision sphere, steering / BTR (0x517940)

// the BTR's turret (0x517f10, 0x518050, 0x5184b0 and the sector watcher 0x535720 .. 0x5359f0); the zones and the
// way it turns are read from the code as far as they go, the turning speed while aiming is inferred
constexpr float TowerSweep   = 50.f;   // degrees either way while it searches
constexpr float TowerReach   = 20.f;   // m: a car in front of the gun (within TowerCone) is locked on ..
constexpr float TowerCone    = 15.f;   // degrees either way
constexpr float TowerHeight  = 5.f;    // m above and below
constexpr float TowerLose    = 25.f;   // m: .. and kept until it is further than this
constexpr float TowerFirst   = 0.33f;  // s aiming before the first burst
constexpr float TowerNext    = 1.25f;  // s between the bursts
constexpr int   TowerBursts  = 4;      // then it looks for a target again (3 more after the first, 0x5359b0)

struct ModelInfo {
  const char* name;
  Traffic::Kind kind;
  const char* box;          // collision box node (0x517940: flagged 4)
  const char* wheels[8];    // the first two steer on the steering models
  const char* lights[2];
  const char* turret;
  int         lightAxis;    // 0 u, 1 v (table +0x1a4)
  bool        lit;          // modelled with the lights on (+0x1a8)
  bool        steers;       // +0x1ac
  };

// RCCars.exe's model table at 0x574230 (0x1b0 bytes each)
const ModelInfo Models[] = {
  {"Trailer",Traffic::Kind::Trailer,"cube1",{"cube2","cube5","cube3","cube4"},{"fara01_body","fara02_body"},"",1,true,true},
  {"Truck",  Traffic::Kind::Truck,  "cube1",{"cube2","cube5","cube3","cube4"},{"fara01_body","fara02_body"},"",1,true,true},
  {"Hammer", Traffic::Kind::Hammer, "Box00",{"Box01","Box02","Box03","Box04"},{"Fara01_body","Fara02_body"},"",0,false,true},
  {"Btr",    Traffic::Kind::Btr,    "cube1",{"cyl1_3","cyl1_1","cyl1_2","cyl1","cyl2","cyl3","cyl4","cyl5"},{"",""},
             "Cylinder02_1",0,false,false},
  };

float horiz(const Vec3& v) { return std::sqrt(v.x*v.x+v.z*v.z); }

// rotation by `degrees` about `axis` (unit), right handed
Mat4 rotateAbout(const Vec3& axis, float degrees) {
  const float a = degrees*Pi/180.f, c = std::cos(a), s = std::sin(a), t = 1.f-c;
  const float x = axis.x, y = axis.y, z = axis.z;
  Mat4 r;
  r.m[0][0] = t*x*x+c;   r.m[0][1] = t*x*y-s*z; r.m[0][2] = t*x*z+s*y;
  r.m[1][0] = t*x*y+s*z; r.m[1][1] = t*y*y+c;   r.m[1][2] = t*y*z-s*x;
  r.m[2][0] = t*x*z-s*y; r.m[2][1] = t*y*z+s*x; r.m[2][2] = t*z*z+c;
  return r;
  }

std::string scriptString(const std::string& script, std::string_view key) {
  // "body(int)  = 1" in Section Keys "Model"
  const auto at = script.find(std::string(key)+"(");
  if(at==std::string::npos)
    return {};
  const auto eq = script.find('=',at);
  if(eq==std::string::npos)
    return {};
  size_t b = eq+1;
  while(b<script.size() && (script[b]==' ' || script[b]=='\t' || script[b]=='"'))
    ++b;
  size_t e = b;
  while(e<script.size() && script[e]!='\n' && script[e]!='\r' && script[e]!='"' && script[e]!=' ')
    ++e;
  return script.substr(b,e-b);
  }

struct Box {
  Vec3 mn = { 1e9f, 1e9f, 1e9f}, mx = {-1e9f,-1e9f,-1e9f};
  void add(const Vec3& p) {
    mn = {std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
    mx = {std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)};
    }
  bool empty() const { return mn.x>mx.x; }
  Vec3 centre() const { return (mn+mx)*0.5f; }
  Vec3 size()   const { return mx-mn; }
  };

}

struct Traffic::State {
  Car          car;
  const ModelInfo* info = nullptr;
  Vec3         up {0,1,0};        // the instance's up (its matrix stays as placed: 0x516cdc saves it at +0x2e4)
  Mat4         rootInv;           // undoes the rotation of the model's root node
  Vec3         home;              // where it is parked
  // per part (Car::parts): node origin (model space) and its axes
  struct Part {
    Vec3 pivot;
    Vec3 axis[3];
    float flip = 1;               // mirrored node: rotations turn the other way
    bool  steers = false;
    float spin = 0;               // degrees per degree of the wheel angle: +1, or -1 for the BTR's left side
    int   spinAxis = 2;
    };
  std::vector<Part> parts;
  int          turretPart = -1;
  Vec3         barrel {0,0,1};    // model space, level: where the gun points when the turret is not turned

  bool         parked = true;
  const RoadPath* path = nullptr;
  float        start  = 0;        // clock when it left
  Vec3         last;              // position at the last update
  float        wheel  = 0;        // degrees
  float        steer  = 0;
  float        tilt   = 0;
  bool         moving = false;
  bool         heard  = false;    // the camera has been near since the sound started (ours: one start per pass)

  // turret
  float        towerAngle = 0;    // degrees, about the model's up
  float        towerDir   = 1;    // sweep direction
  int          lockedOn   = -1;   // -2 the player, >=0 an opponent, -1 none
  float        lockTime   = 0;
  int          bursts     = 0;
  bool         searching  = false;

  // collision
  std::unique_ptr<btBoxShape>         boxShape;
  std::unique_ptr<btSphereShape>      wheelShape[8];
  std::unique_ptr<btCompoundShape>    shape;
  std::unique_ptr<btDefaultMotionState> motion;
  std::unique_ptr<btRigidBody>        rigid;
  bool         inWorld = false;
  Box          box;                   // model space: body box
  std::vector<std::pair<Vec3,float>>  spheres; // model space: wheels
  };

Traffic::Traffic(PhysicsWorld& world, ObjectLibrary& library, const SbScene& track, std::string_view trackName,
                 Reader pack)
  :owner(world), lib(library), track(track) {
  auto ini = [&](const std::string& file, bool& found) {
    try {
      const auto d = pack("Settings\\"+file);
      found = true;
      return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
      }
    catch(const std::exception&) {
      found = false;
      return SliderFile();
      }
    };
  bool found = false;
  const SliderFile rdc = ini("rdc_"+std::string(trackName)+".ini",found);
  if(!found)
    return;
  bool any = false;
  const SliderFile road = ini("roadcar.ini",any);
  // 0x516330: Freq and Disp are hundredths, Accel tenths, Wheel as it is, Slow 0/1
  if(rdc.find("Freq"))  prm.freq  = float(rdc.raw("Freq"))*0.01f;
  if(rdc.find("Disp"))  prm.disp  = float(rdc.raw("Disp"))*0.01f;
  if(rdc.find("Accel")) prm.accel = float(rdc.raw("Accel"))*0.1f;
  if(rdc.find("Wheel")) prm.wheel = float(rdc.raw("Wheel"));
  if(rdc.find("Slow"))  prm.slow  = rdc.raw("Slow")!=0;
  // 0x517660
  if(road.find("TowerVel"))    prm.towerVel   = float(road.raw("TowerVel"))*0.01f;
  if(road.find("TowerShootT")) prm.towerShoot = float(road.raw("TowerShootT"))*0.1f;

  const char* dirs[2] = {"forw","back"};
  for(int i=0; i<2; ++i)
    paths[i] = RoadPath::load(pack("Profiles\\rdc_"+std::string(trackName)+"_"+dirs[i]+".gpf"));

  for(auto& v:track.volumes)
    if(v.name.rfind("roadcar_",0)==0)
      lightZones.push_back(&v);

  for(auto& p:library.place(track)) {
    if(p.model==nullptr || p.model->kind!=ObjectLibrary::Kind::Vehicle)
      continue;
    auto s = std::make_unique<State>();
    s->car.name  = p.instance->name;
    s->car.model = p.model;
    try {
      setup(*s,*p.instance);
      }
    catch(const std::exception& e) {
      std::fprintf(stderr,"road car %s: %s\n",p.instance->name.c_str(),e.what());
      continue;
      }
    // 0x516070: at most 64 of them
    if(states.size()>=64)
      break;
    view.push_back(&s->car);
    states.push_back(std::move(s));
    }
  restart();
  }

Traffic::~Traffic() {
  for(auto& s:states)
    if(s->inWorld)
      owner.bullet().removeRigidBody(s->rigid.get());
  }

void Traffic::setup(State& s, const SbInstance& in) {
  const auto& model = *s.car.model;
  for(auto& m:Models)
    if(model.name==m.name)
      s.info = &m;
  if(s.info==nullptr)
    throw std::runtime_error("Animation instance "+in.name+" is not roadcar");
  const ModelInfo& info = *s.info;
  s.car.kind = info.kind;

  const SbScene& sb = *model.scene;
  auto node = [&](std::string_view name) -> const SbMesh* {
    for(auto& m:sb.meshes)
      if(m.model==model.id && m.name==name)
        return &m;
    return nullptr;
    };
  // bounding box (model space) of a node and everything below it
  auto boxOf = [&](const SbMesh* n) {
    Box b;
    if(n==nullptr)
      return b;
    const auto self = int32_t(n-sb.meshes.data());
    for(size_t i=0; i<sb.meshes.size(); ++i) {
      bool below = false;
      for(int32_t k=int32_t(i); k>=0 && !below; k=sb.meshes[size_t(k)].parent)
        below = k==self;
      if(!below)
        continue;
      auto& m = sb.meshes[i];
      for(auto& p:m.positions)
        b.add(m.world.point(p)-model.origin);
      }
    return b;
    };

  // the root node's rotation is replaced by the car's own pose
  for(auto& m:sb.meshes)
    if(m.model==model.id && (m.parent<0 || sb.meshes[size_t(m.parent)].model!=model.id)) {
      Mat4 r = m.world;
      r.m[0][3] = r.m[1][3] = r.m[2][3] = 0;
      s.rootInv = r.inverseRigid();
      break;
      }
  s.up   = in.world.vector({0,1,0});
  s.home = in.world.translation();

  // wheels, then the turret
  for(int i=0; i<8; ++i) {
    if(info.wheels[i]==nullptr || info.wheels[i][0]==0)
      continue;
    const SbMesh* n = node(info.wheels[i]);
    if(n==nullptr)
      throw std::runtime_error(std::string("bad roadcar model - can't find object=")+info.wheels[i]);
    State::Part p;
    p.pivot = n->world.translation()-model.origin;
    for(int a=0; a<3; ++a)
      p.axis[a] = n->world.vector({a==0 ? 1.f : 0.f,a==1 ? 1.f : 0.f,a==2 ? 1.f : 0.f}).normalized();
    p.flip   = n->world.determinant3()<0 ? -1.f : 1.f;
    p.steers = info.steers && i<2;
    // 0x516f50: the steering models spin their wheels about the wheel's z, the BTR about its x, the first four
    // wheels backwards
    p.spinAxis = info.steers ? 2 : 0;
    p.spin     = info.steers || i>=4 ? 1.f : -1.f;
    s.parts.push_back(p);
    s.car.parts.push_back(info.wheels[i]);
    // collision sphere at the wheel's centre (0x516480)
    const Box wb = boxOf(n);
    if(!wb.empty())
      s.spheres.push_back({wb.centre(),wb.size().y*WheelRadius[info.steers ? 0 : 1]});
    }
  if(info.turret[0]!=0) {
    if(const SbMesh* n = node(info.turret)) {
      State::Part p;
      p.pivot = n->world.translation()-model.origin;
      for(int a=0; a<3; ++a)
        p.axis[a] = n->world.vector({a==0 ? 1.f : 0.f,a==1 ? 1.f : 0.f,a==2 ? 1.f : 0.f}).normalized();
      s.turretPart = int(s.parts.size());
      s.parts.push_back(p);
      s.car.parts.push_back(info.turret);
      // the barrel: the longer side of the turret from its pivot, along the model's length
      const Box tb = boxOf(n);
      s.barrel = {0,0,std::abs(tb.mx.z-p.pivot.z)>=std::abs(tb.mn.z-p.pivot.z) ? 1.f : -1.f};
      }
    }
  s.car.partPoses.resize(s.parts.size()+1); // [0]: the body
  for(size_t i=0; i<s.parts.size(); ++i)
    s.car.partPoses[i+1] = Mat4::translate(s.parts[i].pivot);

  for(auto* l:info.lights)
    if(l!=nullptr && l[0]!=0)
      s.car.lights.push_back(l);
  s.car.lightAxis   = info.lightAxis;
  s.car.modelledLit = info.lit;
  s.car.lightsOn    = false;   // 0x516ceb -> 0x517460(car, 0): switched off when set up

  // not drawn (node flag 4, 0x4516b0): the collision box with everything below it and each wheel's <wheel>_body
  s.car.hidden.push_back(info.box);
  for(auto* w:info.wheels)
    if(w!=nullptr && w[0]!=0)
      s.car.hidden.push_back(std::string(w)+"_body");

  // 0x517cd0: Section Keys "Model" body(int) 1/2 and box(int) 1 pick other textures
  // (the model wears either variant already: replaced whichever it is)
  auto wear = [&](const std::string& tex, const std::string& to) {
    for(const char* v:{"","_01","_02"})
      s.car.textures.push_back({tex+v,to});
    };
  const std::string body = scriptString(in.script,"body");
  if(body=="1" || body=="2") {
    wear("trailer_mashineria","trailer_mashineria_0"+body);
    wear("trailer_txt","trailer_txt_0"+body);
    }
  const std::string box = scriptString(in.script,"box");
  if(!box.empty())
    wear("treiler_box",box=="1" ? "treiler_box_02" : "treiler_box_01");

  // collision: the body box and the wheels' spheres, a kinematic body moved along the path
  s.box = boxOf(node(info.box));
  s.shape = std::make_unique<btCompoundShape>();
  btTransform t;
  if(!s.box.empty()) {
    const Vec3 half = s.box.size()*0.5f;
    s.boxShape = std::make_unique<btBoxShape>(btVector3(half.x,half.y,half.z)*S);
    t.setIdentity();
    t.setOrigin(toBt(s.box.centre())*S);
    s.shape->addChildShape(t,s.boxShape.get());
    }
  for(size_t i=0; i<s.spheres.size() && i<8; ++i) {
    s.wheelShape[i] = std::make_unique<btSphereShape>(s.spheres[i].second*S);
    t.setIdentity();
    t.setOrigin(toBt(s.spheres[i].first)*S);
    s.shape->addChildShape(t,s.wheelShape[i].get());
    }
  btTransform start;
  start.setIdentity();
  s.motion = std::make_unique<btDefaultMotionState>(start);
  btRigidBody::btRigidBodyConstructionInfo ci(0,s.motion.get(),s.shape.get());
  ci.m_friction    = 0.5f;
  ci.m_restitution = 0.2f;
  s.rigid = std::make_unique<btRigidBody>(ci);
  s.rigid->setCollisionFlags(s.rigid->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
  s.rigid->setActivationState(DISABLE_DEACTIVATION);
  s.rigid->setUserIndex(PhysicsWorld::TrafficTag);
  }

void Traffic::restart() {
  // 0x515ed0 (message 0x27e): every car parked, the next one leaves at once, in a random direction
  clock   = 0;
  waited  = 0;
  delay   = 0;
  forward = (rng()&1)!=0;
  sent    = 0;
  fired.clear();
  for(auto& p:states) {
    State& s = *p;
    s.parked = true;
    s.path   = nullptr;
    s.car.onRoad  = false;
    s.car.visible = false;
    s.car.sound   = -1;
    s.car.lightsOn = false;
    s.car.world   = Mat4::translate(s.home)*s.rootInv;
    s.towerAngle = 0;
    s.towerDir   = 1;
    s.lockedOn   = -1;
    s.searching  = false;
    s.heard      = false;
    placeBody(s);
    }
  }

float Traffic::random() {
  return float(rng()%32768u)/32768.f;
  }

void Traffic::depart() {
  // 0x5160f0: the first parked car, the two paths in turn
  for(auto& p:states) {
    State& s = *p;
    if(!s.parked)
      continue;
    s.parked = false;
    s.path   = &paths[forward ? 0 : 1];
    s.start  = clock;
    s.wheel  = 0;
    s.last   = s.path->at(0).pos;
    s.heard  = false;
    forward  = !forward;
    waited   = 0;
    // Freq +- Freq*Disp, evenly
    delay    = prm.freq + prm.freq*prm.disp*(2.f*random()-1.f);
    ++sent;
    return;
    }
  }

void Traffic::update(float dt, bool racing, Vehicle* player, const Vec3& camera, std::vector<Characters::Car>& cars) {
  fired.clear();
  if(states.empty())
    return;
  if(racing) {
    clock  += dt;
    waited += dt;
    if(waited>delay)
      depart();
    }
  for(auto& p:states) {
    State& s = *p;
    s.car.sound = -1;
    if(!s.parked)
      drive(s,dt,camera,player,cars);
    placeBody(s);
    }
  }

void Traffic::drive(State& s, float dt, const Vec3& camera, Vehicle* player, std::vector<Characters::Car>& cars) {
  const float t = (clock-s.start)*prm.accel;
  if(s.path==nullptr || t>s.path->duration()) {
    // 0x516db0: back in the queue
    s.parked = true;
    s.car.onRoad  = false;
    s.car.visible = false;
    s.car.world   = Mat4::translate(s.home)*s.rootInv;
    return;
    }
  const RoadPath::Key k = s.path->at(t);
  s.car.onRoad = true;

  // 0x44c960: x = up x forward, y = the instance's up, z = the path's direction, at the path's position
  Vec3 fwd = k.dir;
  fwd.y = 0;
  fwd = fwd.length()>1e-4f ? fwd.normalized() : Vec3{0,0,1};
  const Vec3 up    = s.up;
  const Vec3 right = Vec3::cross(up,fwd);
  Mat4 m;
  m.m[0][0] = right.x; m.m[0][1] = up.x; m.m[0][2] = fwd.x; m.m[0][3] = k.pos.x;
  m.m[1][0] = right.y; m.m[1][1] = up.y; m.m[1][2] = fwd.y; m.m[1][3] = k.pos.y;
  m.m[2][0] = right.z; m.m[2][1] = up.z; m.m[2][2] = fwd.z; m.m[2][3] = k.pos.z;

  // 0x516f50: the wheels spin while the car moves
  const Vec3 d = k.pos-s.last;
  s.moving = std::abs(d.x)>=MovedBy || std::abs(d.y)>=MovedBy || std::abs(d.z)>=MovedBy;
  s.last   = k.pos;
  if(s.moving)
    s.wheel = std::fmod(s.wheel+dt*prm.wheel,360.f);

  // seen from a camera within 60 m: animated and drawn, otherwise only moved along
  s.car.visible = horiz(camera-k.pos)<SeenDistance;
  if(s.car.visible) {
    s.steer = std::clamp(k.steer*SteerScale,-SteerMax,SteerMax);
    s.tilt  = k.tilt;
    const float pitch = -std::atan(k.tilt*TiltScale)*180.f/Pi;
    m = m*Mat4::rotate(0,pitch);
    m.m[1][3] -= TiltDrop/std::sqrt(k.tilt*k.tilt+1.f/(TiltScale*TiltScale));

    for(size_t i=0; i<s.parts.size(); ++i) {
      const State::Part& p = s.parts[i];
      if(int(i)==s.turretPart)
        continue;
      Mat4 r = rotateAbout(p.axis[p.spinAxis],p.flip*p.spin*s.wheel);
      if(p.steers)
        r = rotateAbout(p.axis[1],p.flip*s.steer)*r;
      s.car.partPoses[i+1] = Mat4::translate(p.pivot)*r;
      }
    }
  s.car.pose  = m;
  s.car.world = m*s.rootInv;

  // head lights in the tunnels (0x5187d0)
  bool lights = false;
  const Vec3 probe = k.pos+Vec3{0,LightProbe,0};
  for(auto* v:lightZones)
    lights |= v->contains(probe);
  s.car.lightsOn = lights && !s.car.lights.empty();

  // the sound (0x517530): Slow ones all the time, the others once when a camera comes near
  if(prm.slow) {
    s.car.sound = 0;
    }
  else {
    const Vec3 rel = camera-k.pos;
    const bool near = horiz(rel)<SoundNear && rel.y>SoundBelow;
    if(near && !s.heard)
      s.car.sound = random()<0.5f ? 1 : 0;
    s.heard = near;
    }

  // the other cars touching it are knocked off their recordings (0x534880 -> 0x4fd9d0)
  for(auto& c:cars)
    if(touches(s,c.pos,OtherCarSize))
      c.hit = true;

  // the player's car is pushed off (0x5165a0 -> 0x4f3470), each update it touches
  if(player!=nullptr && s.inWorld && playerTouches(s,*player)) {
    Vec3 away = player->pose().translation()-k.pos;
    away.y = 0;
    away = horiz(away)>1e-4f ? away*(1.f/horiz(away)) : Vec3{};
    const float out = prm.slow ? SlowPushOut : PushOut;
    const float up2 = prm.slow ? SlowPushUp  : PushUp;
    player->addVelocity(away*out+Vec3{0,up2,0});
    }

  if(s.turretPart>=0)
    turret(s,dt,player,cars);
  }

// The BTR's turret. RCCars.exe watches two zones around the gun (0x5184b0: a wedge 20 m long and 15 degrees
// either way of where it points, 5 m up and down; and a 25 m circle) with any car ($CAR) as a target
// (0x535720 .. 0x5359f0): a car in the wedge is locked on; after 0.33 s of aiming a burst is fired at it (message
// 0x261 to BURST_MNG, the guards' gunfire), then one every 1.25 s, four in all, as long as it stays within 25 m.
// Without a target in reach the turret keeps still; once it has lost one it sweeps 50 degrees either way at
// TowerVel degrees/s (0x518121) looking for the next. While aiming it turns towards the target at up to twice that
// (inferred from 0x518325).
void Traffic::turret(State& s, float dt, Vehicle* player, const std::vector<Characters::Car>& cars) {
  const State::Part& tp = s.parts[size_t(s.turretPart)];
  const Vec3 gun   = s.car.world.point(tp.pivot);
  const Vec3 upW   = s.car.world.vector(tp.axis[1]).normalized();
  auto aim = [&]() {
    const Mat4 r = rotateAbout(tp.axis[1],s.towerAngle);
    Vec3 b = s.car.world.vector(r.vector(s.barrel));
    b.y = 0;
    return b.length()>1e-4f ? b.normalized() : Vec3{0,0,1};
    };
  auto target = [&](int id, Vec3& at) {
    if(id==-2 && player!=nullptr) {
      at = player->pose().translation();
      return true;
      }
    if(id>=0 && size_t(id)<cars.size()) {
      at = cars[size_t(id)].pos;
      return true;
      }
    return false;
    };
  auto inWedge = [&](const Vec3& at) {
    Vec3 to = at-gun;
    if(std::abs(to.y)>TowerHeight)
      return false;
    to.y = 0;
    const float dist = to.length();
    if(dist>TowerReach || dist<1e-3f)
      return false;
    return Vec3::dot(to*(1.f/dist),aim())>=std::cos(TowerCone*Pi/180.f);
    };

  // the lock is lost beyond 25 m
  Vec3 at;
  if(s.lockedOn!=-1 && (!target(s.lockedOn,at) || horiz(at-gun)>TowerLose)) {
    s.lockedOn  = -1;
    s.searching = true;
    }
  if(s.lockedOn==-1) {
    // the nearest car in the wedge
    float best = 1e9f;
    if(player!=nullptr && inWedge(player->pose().translation())) {
      s.lockedOn = -2;
      best = horiz(player->pose().translation()-gun);
      }
    for(size_t i=0; i<cars.size(); ++i)
      if(inWedge(cars[i].pos) && horiz(cars[i].pos-gun)<best) {
        s.lockedOn = int(i);
        best = horiz(cars[i].pos-gun);
        }
    if(s.lockedOn!=-1) {
      s.lockTime = 0;
      s.bursts   = 0;
      }
    }

  if(s.lockedOn!=-1 && target(s.lockedOn,at)) {
    // turn towards it
    Vec3 to = at-gun;
    to.y = 0;
    const Vec3  b     = aim();
    const float cross = Vec3::dot(Vec3::cross(b,to),upW);
    const float err   = std::atan2(cross,Vec3::dot(b,to))*180.f/Pi;
    const float step  = 2.f*prm.towerVel*dt;
    s.towerAngle += std::clamp(err,-step,step);
    s.lockTime   += dt;
    if(s.lockTime>=(s.bursts==0 ? TowerFirst : TowerNext)) {
      s.lockTime = 0;
      fired.push_back({gun,at});
      if(++s.bursts>=TowerBursts) {
        s.lockedOn  = -1;
        s.searching = true;
        }
      }
    }
  else if(s.searching) {
    s.towerAngle += s.towerDir*prm.towerVel*dt;
    if(std::abs(s.towerAngle)>TowerSweep) {
      s.towerAngle = std::clamp(s.towerAngle,-TowerSweep,TowerSweep);
      s.towerDir   = -s.towerDir;
      }
    }
  s.car.partPoses[size_t(s.turretPart)+1] = Mat4::translate(tp.pivot)*rotateAbout(tp.axis[1],s.towerAngle);
  }

void Traffic::placeBody(State& s) {
  const bool want = !s.parked;
  if(want!=s.inWorld) {
    if(want)
      owner.bullet().addRigidBody(s.rigid.get());
    else
      owner.bullet().removeRigidBody(s.rigid.get());
    s.inWorld = want;
    }
  const btTransform t = toBt(s.car.world);
  s.motion->setWorldTransform(t);
  if(!s.inWorld)
    s.rigid->setWorldTransform(t);
  }

bool Traffic::touches(const State& s, const Vec3& p, float margin) const {
  const Vec3 l = s.car.world.inverseRigid().point(p);
  auto inBox = [&](const Box& b) {
    return !b.empty() && l.x>b.mn.x-margin && l.x<b.mx.x+margin && l.y>b.mn.y-margin && l.y<b.mx.y+margin &&
           l.z>b.mn.z-margin && l.z<b.mx.z+margin;
    };
  if(inBox(s.box))
    return true;
  for(auto& [c,r]:s.spheres)
    if((l-c).length()<r+margin)
      return true;
  return false;
  }

bool Traffic::playerTouches(const State& s, const Vehicle& player) const {
  const btCollisionObject* car  = &player.chassis();
  const btCollisionObject* body = s.rigid.get();
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
