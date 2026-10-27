#include "ghosts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "vehicle.h"

using namespace rc;

namespace {

// model -> world rotation as a quaternion (w x y z), the way CarProfile::pose reads it back
void toQuaternion(const Mat4& m, float q[4]) {
  const float tr = m.m[0][0]+m.m[1][1]+m.m[2][2];
  float w, x, y, z;
  if(tr>0) {
    const float s = std::sqrt(tr+1.f)*2.f;
    w = 0.25f*s;
    x = (m.m[2][1]-m.m[1][2])/s;
    y = (m.m[0][2]-m.m[2][0])/s;
    z = (m.m[1][0]-m.m[0][1])/s;
    }
  else if(m.m[0][0]>m.m[1][1] && m.m[0][0]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[0][0]-m.m[1][1]-m.m[2][2])*2.f;
    w = (m.m[2][1]-m.m[1][2])/s;
    x = 0.25f*s;
    y = (m.m[0][1]+m.m[1][0])/s;
    z = (m.m[0][2]+m.m[2][0])/s;
    }
  else if(m.m[1][1]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[1][1]-m.m[0][0]-m.m[2][2])*2.f;
    w = (m.m[0][2]-m.m[2][0])/s;
    x = (m.m[0][1]+m.m[1][0])/s;
    y = 0.25f*s;
    z = (m.m[1][2]+m.m[2][1])/s;
    }
  else {
    const float s = std::sqrt(1.f+m.m[2][2]-m.m[0][0]-m.m[1][1])*2.f;
    w = (m.m[1][0]-m.m[0][1])/s;
    x = (m.m[0][2]+m.m[2][0])/s;
    y = (m.m[1][2]+m.m[2][1])/s;
    z = 0.25f*s;
    }
  const float len = std::sqrt(w*w+x*x+y*y+z*z);
  q[0] = w/len; q[1] = x/len; q[2] = y/len; q[3] = z/len;
  }

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary);
  if(!f)
    return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
  }

}

Ghosts::Ghost::Ghost(const CarGeometry& geometry, const CarParams& params)
  : geom(geometry), prm(params), rig(geometry,params) {
  for(auto& w:geom.wheels)
    wheelRadius = std::max(wheelRadius,w.radius);
  for(float& s:springs)
    s = prm.springMount.y;
  }

void Ghosts::Ghost::show(float t, float dt) {
  clock = t;
  if(lap.empty())
    return;
  current = lap.pose(t);
  const Vec3 v = lap.velocity(t);
  currentSpeed = running && t<lap.duration() ? Vec3::dot(v,current.vector({0,0,1})) : 0.f;
  const auto& f = lap.frameAt(t);
  for(size_t i=0; i<6; ++i)
    springs[i] = f.suspension[i];
  steer      = f.steer;
  wheelTurn += currentSpeed*dt/wheelRadius;
  }

CarRig::Pose Ghosts::Ghost::rigPose() const {
  const float deg = 180.f/3.14159265f;
  std::vector<CarRig::Wheel> in(geom.wheels.size());
  float rearZ = 1e9f;
  for(auto& gw:geom.wheels)
    if(!gw.front)
      rearZ = std::min(rearZ,gw.center.z);
  for(size_t i=0; i<in.size(); ++i) {
    const auto& gw = geom.wheels[i];
    // Vehicle's steerBy: the front wheels all of it, the Hummer's middle ones a third
    const bool  rear = !gw.front && gw.center.z<=rearZ+1e-3f;
    const float by   = gw.front ? 1.f : (rear ? 0.f : 1.f/3.f);
    // a frame keeps 6 springs (the Hummer's 6 wheels)
    const float len  = i<6 ? springs[i] : prm.springMount.y;
    in[i].lift  = len>0 ? prm.springMount.y-len : 0.f;
    in[i].steer = steer*by;
    in[i].spin  = wheelTurn*deg;
    }
  return rig.pose(in);
  }

Ghosts::Ghosts(Kind kind, const Look& player, const std::filesystem::path& folder, int32_t track, const Rig& makeRig,
               const Name& name)
  : mode(kind), playerLook(player) {
  auto make = [&](int32_t car) -> Ghost& {
    CarGeometry g;
    CarParams   p;
    makeRig(car,g,p);
    list.emplace_back(g,p);
    Ghost& gh = list.back();
    gh.name     = name(car);
    gh.look     = player;
    gh.look.car = car;
    gh.own      = car==player.car;
    return gh;
    };
  if(kind==Kind::Normal) {
    // 0x4a5320 mode 2: the player and the own ghost, no lap yet: hidden (0x4f1dd0), the player's look
    make(player.car);
    return;
    }
  // mode 3: a ghost of each car, its lap from its file; the others' without a file left out (0x4a5320)
  for(int32_t car=0; car<3; ++car) {
    const auto file = folder/("Track"+std::to_string(track)+"_Car"+std::to_string(car+1)+".scg");
    const auto data = readFile(file);
    CarProfile lap;
    if(!data.empty()) {
      try {
        lap = CarProfile::parse(data);
        }
      catch(const std::exception&) {
        lap = CarProfile();
        }
      }
    if(lap.empty() && car!=player.car)
      continue;
    Ghost& gh = make(car);
    gh.file = file;
    if(!lap.empty()) {
      // 0x4f15c0: as it was driven, the first skin (0x4a9170)
      gh.lap            = std::move(lap);
      gh.look.boost     = std::clamp(gh.lap.boost,0,3);
      gh.look.resonator = std::clamp(gh.lap.resonator,0,3);
      gh.look.tires     = std::clamp(gh.lap.tires,0,3);
      gh.look.skin      = 0;
      gh.visible        = true;
      }
    }
  restart();
  }

Ghosts::Ghost* Ghosts::ownGhost() {
  for(auto& g:list)
    if(g.own)
      return &g;
  return nullptr;
  }

void Ghosts::start() {
  // 0x4fbeb0 with the lap counter at 1
  for(auto& g:list)
    if(!g.lap.empty()) {
      g.running = true;
      g.show(0,0);
      }
  rec.clear();
  recTime = 0;
  }

bool Ghosts::lapDone(float time) {
  bool took = false;
  if(Ghost* own = ownGhost()) {
    own->visible = true;
    // no lap of its own yet, or slower than this one
    if(!rec.empty() && (own->lap.empty() || time<own->lap.duration())) {
      std::swap(own->lap,rec);
      own->lap.car       = playerLook.car;
      own->lap.boost     = playerLook.boost;
      own->lap.resonator = playerLook.resonator;
      own->lap.tires     = playerLook.tires;
      own->look          = playerLook;
      own->look.skin     = (playerLook.skin+1)%4;
      took = true;
      }
    own->running = true;
    }
  for(auto& g:list)
    if(!g.lap.empty()) {
      g.running = true;
      g.show(0,0);
      }
  rec.clear();
  recTime = 0;
  return took;
  }

void Ghosts::restart() {
  // 0x4f69e0, and the race's beginning (0x4f27a0): at the first frame (0x503880), waiting
  for(auto& g:list) {
    g.running   = false;
    g.wheelTurn = 0;
    g.show(0,0);
    }
  rec.clear();
  recTime = 0;
  }

void Ghosts::record(float dt, const Vehicle& car, uint32_t keys, bool racing) {
  if(!racing)
    return;
  recTime += dt;
  const auto& fr = rec.frames();
  if(fr.size()>=MaxFrames || (!fr.empty() && recTime-fr.back().time<FrameStep))
    return;
  CarProfile::Frame f;
  const Mat4 pose = car.pose();
  f.time     = recTime;
  f.position = pose.translation();
  toQuaternion(pose,f.rot);
  f.velocity = car.velocity();
  f.spin     = pose.vector(car.localSpin());
  for(size_t i=0; i<6 && i<car.geometry().wheels.size(); ++i) {
    f.suspension[i] = car.springLength(i);
    f.change[i]     = car.springChange(i);
    }
  f.steer = car.steering();
  f.keys  = keys;
  rec.add(f);
  }

void Ghosts::update(float dt) {
  for(auto& g:list)
    if(g.running && !g.lap.empty())
      g.show(std::min(g.clock+dt,g.lap.duration()),dt);
  }

bool Ghosts::save() const {
  if(mode!=Kind::Multi)
    return true;
  bool ok = true;
  for(auto& g:list) {
    if(g.file.empty() || g.lap.empty())
      continue;
    std::error_code ec;
    std::filesystem::create_directories(g.file.parent_path(),ec);
    CarProfile lap = g.lap;
    lap.car = g.look.car;
    const auto data = lap.save();
    std::ofstream f(g.file,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
    ok = ok && bool(f);
    }
  return ok;
  }
