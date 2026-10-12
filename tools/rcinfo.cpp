// rcinfo - command line inspector for RC Cars data files (no GPU / Tempest dependency).
//
//   rcinfo <game_dir> pack                       list pack statistics
//   rcinfo <game_dir> extract <name> <out_file>  extract & decrypt a file from RCCars.pack
//   rcinfo <game_dir> csi <name> <out.tga>       decode a pack texture to TGA
//   rcinfo <game_dir> sb <RCCarsDB/x.sb>         scene statistics (meshes, faces, textures, markers)
//   rcinfo <game_dir> obj <RCCarsDB/x.sb> <out.obj>  export scene geometry (world space) to OBJ
//   rcinfo <game_dir> coll <RCCarsDB/x.sb> [out.obj]  collision grid statistics + self-test, optional OBJ export
//   rcinfo <game_dir> drive <RCCarsDB/x.sb> [car 1-3] [upgrade 0-3]  headless test drive on a track (physics)
//   rcinfo <game_dir> race <RCCarsDB/x.sb> [laps]  checkpoint gates + a race driven along the route (no physics)
//   rcinfo <game_dir> gates <RCCarsDB/x.sb> [car]  every checkpoint gate driven through under physics
//   rcinfo <game_dir> objects <RCCarsDB/x.sb>    objects placed on a track (INST): model, kind, physics, settling test
//   rcinfo <game_dir> check                      decode every texture in the pack and every RCCarsDB/*.sb

#include "formats/pack.h"
#include "formats/csi.h"
#include "formats/scene.h"
#include "formats/collision.h"
#include "formats/objects.h"
#include "formats/water.h"
#include "physics/checkpoints.h"
#include "physics/race.h"
#include "physics/props.h"
#include "physics/vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>

namespace fs = std::filesystem;

static std::vector<uint8_t> readFile(const fs::path& p) {
  std::ifstream f(p,std::ios::binary);
  if(!f)
    throw std::runtime_error("unable to open "+p.string());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

static void writeFile(const fs::path& p, const void* d, size_t n) {
  std::ofstream f(p,std::ios::binary);
  f.write(reinterpret_cast<const char*>(d),std::streamsize(n));
  }

static void writeTga(const fs::path& p, const rc::Image& img) {
  std::vector<uint8_t> out(18+img.rgba.size());
  out[2]  = 2;  // uncompressed true-color
  out[12] = uint8_t(img.w); out[13] = uint8_t(img.w>>8);
  out[14] = uint8_t(img.h); out[15] = uint8_t(img.h>>8);
  out[16] = 32;
  out[17] = 0x28; // top-left origin, 8 alpha bits
  for(size_t i=0; i<size_t(img.w)*img.h; ++i) {
    out[18+i*4+0] = img.rgba[i*4+2];
    out[18+i*4+1] = img.rgba[i*4+1];
    out[18+i*4+2] = img.rgba[i*4+0];
    out[18+i*4+3] = img.rgba[i*4+3];
    }
  writeFile(p,out.data(),out.size());
  }

static int cmdPack(rc::PackFile& pack) {
  std::map<std::string,size_t> dirs;
  uint64_t total = 0;
  for(auto& e:pack.entries()) {
    dirs[e.name.substr(0,e.name.find('\\'))]++;
    total += e.size;
    }
  std::printf("entries: %zu, %.1f MB\n",pack.entries().size(),double(total)/1024.0/1024.0);
  for(auto& [d,n]:dirs)
    std::printf("  %-14s %zu\n",d.c_str(),n);
  return 0;
  }

static int cmdSb(const fs::path& path, rc::PackFile& pack) {
  auto scene = rc::SbScene::load(readFile(path));
  size_t verts=0, faces=0, tris=0, withGeom=0;
  std::map<int,size_t> layerCount;
  for(auto& m:scene.meshes) {
    verts += m.positions.size();
    faces += m.faces.size();
    if(!m.positions.empty())
      withGeom++;
    for(auto& f:m.faces) {
      tris += f.pos.size()>=3 ? f.pos.size()-2 : 0;
      layerCount[int(f.layers.size())]++;
      }
    }
  std::printf("meshes: %zu (%zu with geometry)\nvertices: %zu\nfaces: %zu (%zu triangles)\n",
              scene.meshes.size(),withGeom,verts,faces,tris);
  for(auto& [n,c]:layerCount)
    std::printf("  faces with %d texture layer(s): %zu\n",n,c);

  size_t found=0, embedded=0;
  std::vector<std::string> missing;
  for(auto& t:scene.textures) {
    if(!t.embedded.empty()) { embedded++; continue; }
    if(pack.find("Textures.1\\"+t.name+".csi")) found++; else missing.push_back(t.name);
    }
  std::printf("textures: %zu (in pack: %zu, embedded: %zu, missing: %zu)\n",
              scene.textures.size(),found,embedded,missing.size());
  for(auto& m:missing)
    std::printf("  missing: %s\n",m.c_str());
  std::printf("markers: %zu\n",scene.markers.size());
  for(auto& m:scene.markers) {
    auto p = m.position();
    std::printf("  %-16s %8.2f %8.2f %8.2f\n",m.name.c_str(),p.x,p.y,p.z);
    }
  return 0;
  }

static int cmdCheck(const fs::path& game, rc::PackFile& pack) {
  size_t ok = 0, failed = 0;
  std::map<int,size_t> modes;
  for(auto& e:pack.entries()) {
    if(e.name.size()<4 || e.name.compare(e.name.size()-4,4,".csi")!=0)
      continue;
    try {
      auto d   = pack.read(e);
      auto img = rc::decodeCsi(d.data(),d.size());
      modes[int(img.alpha)]++;
      ok++;
      }
    catch(const std::exception& ex) {
      std::printf("FAIL %s: %s\n",e.name.c_str(),ex.what());
      failed++;
      }
    }
  std::printf("textures: %zu ok, %zu failed (opaque %zu, cutout %zu, translucent %zu)\n",
              ok,failed,modes[0],modes[1],modes[2]);

  size_t scenes = 0;
  for(auto& f:fs::directory_iterator(game/"RCCarsDB")) {
    if(f.path().extension()!=".sb")
      continue;
    try {
      auto sc = rc::SbScene::load(readFile(f.path()));
      for(auto& t:sc.textures)
        if(!t.embedded.empty())
          rc::decodeCsi(t.embedded.data(),t.embedded.size());
      if(!sc.collision.empty()) {
        rc::CollisionMesh coll(sc);
        std::printf("  %-16s collision: %zu triangles\n",f.path().filename().string().c_str(),coll.triangles().size());
        }
      scenes++;
      }
    catch(const std::exception& ex) {
      std::printf("FAIL %s: %s\n",f.path().filename().string().c_str(),ex.what());
      failed++;
      }
    }
  std::printf("scenes: %zu ok\n",scenes);
  return failed==0 ? 0 : 3;
  }

static int cmdObj(const fs::path& path, const fs::path& out) {
  auto scene = rc::SbScene::load(readFile(path));
  std::ofstream f(out);
  size_t base = 1;
  for(auto& m:scene.meshes) {
    if(m.positions.empty())
      continue;
    f << "o " << m.name << "\n";
    for(auto& p:m.positions) {
      auto w = m.world.point(p);
      f << "v " << w.x << " " << w.y << " " << w.z << "\n";
      }
    for(auto& fc:m.faces) {
      f << "f";
      for(auto i:fc.pos)
        f << " " << (base+i);
      f << "\n";
      }
    base += m.positions.size();
    }
  return 0;
  }

// brute force reference for CollisionMesh::raycast
static std::optional<float> groundBrute(const rc::CollisionMesh& coll, const rc::Vec3& from) {
  std::optional<float> best;
  for(auto& t:coll.triangles()) {
    if(t.surface==rc::Surface::Water)
      continue;
    const rc::Vec3 &a = t.a, &b = t.b, &c = t.c;
    const float d = (b.z-c.z)*(a.x-c.x) + (c.x-b.x)*(a.z-c.z);
    if(std::abs(d)<1e-12f)
      continue;
    const float u = ((b.z-c.z)*(from.x-c.x) + (c.x-b.x)*(from.z-c.z))/d;
    const float v = ((c.z-a.z)*(from.x-c.x) + (a.x-c.x)*(from.z-c.z))/d;
    if(u<0 || v<0 || u+v>1)
      continue;
    const float y = a.y*u + b.y*v + c.y*(1-u-v);
    if(y<=from.y && (!best || y>*best))
      best = y;
    }
  return best;
  }

static int cmdColl(const fs::path& path, const char* objOut) {
  auto scene = rc::SbScene::load(readFile(path));
  auto& g = scene.collision;
  if(g.empty()) {
    std::printf("no COLL in this scene\n");
    return 0;
    }
  rc::CollisionMesh coll(scene);
  const rc::Vec3 cs = coll.cellSize();
  size_t faces = 0, untextured = 0, maxCell = 0, usedCells = 0;
  std::map<std::pair<uint32_t,uint32_t>,bool> seen;
  for(auto& t:coll.triangles()) {
    if(seen.emplace(std::make_pair(t.mesh,t.face),true).second) {
      faces++;
      if(scene.meshes[t.mesh].faces[t.face].layer(rc::LayerTag::Base)==nullptr)
        untextured++;
      }
    }
  for(uint32_t x=0; x<g.dims[0]; ++x)
    for(uint32_t y=0; y<g.dims[1]; ++y)
      for(uint32_t z=0; z<g.dims[2]; ++z) {
        size_t n = 0;
        coll.forEachInCell(x,y,z,[&](const rc::CollisionMesh::Triangle&){ ++n; });
        maxCell = std::max(maxCell,n);
        usedCells += n>0 ? 1 : 0;
        }
  std::printf("grid: %u x %u x %u cells of %.3f x %.3f x %.3f, from (%.2f %.2f %.2f) to (%.2f %.2f %.2f)\n",
              g.dims[0],g.dims[1],g.dims[2],cs.x,cs.y,cs.z,g.min.x,g.min.y,g.min.z,g.max.x,g.max.y,g.max.z);
  std::printf("entries: %zu, non-empty cells: %zu, max triangles in a cell: %zu\n",g.entries.size(),usedCells,maxCell);
  std::printf("collision: %zu faces (%zu without texture), %zu triangles\n",faces,untextured,coll.triangles().size());

  // what the drivable ground (up to ~35 degrees) is made of; LOD copies of the sea count several times
  std::map<rc::Surface,double> area;
  for(auto& t:coll.triangles())
    if(std::abs(t.normal.y)>0.82f)
      area[t.surface] += 0.5*rc::Vec3::cross(t.b-t.a,t.c-t.a).length();
  std::printf("ground surfaces:");
  for(auto& [s,a]:area)
    std::printf(" %s %.0f m2,",rc::surfaceName(s),a);
  std::printf("\n");

  auto ground = [&](const char* what, const rc::Vec3& p) {
    auto h = coll.groundHeight(p+rc::Vec3{0,1.f,0});
    if(h)
      std::printf("ground under %-8s (%.2f %.2f %.2f): %.3f\n",what,p.x,p.y,p.z,*h);
    else
      std::printf("ground under %-8s (%.2f %.2f %.2f): none\n",what,p.x,p.y,p.z);
    };
  if(auto p = scene.instance("Player"))
    ground("Player",p->world.translation());
  if(auto m = scene.marker("cp_1"))
    ground("cp_1",m->position());

  // self-test: grid raycast vs brute force over all triangles at random points
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> rx(g.min.x,g.max.x), rz(g.min.z,g.max.z), ry(g.min.y,g.max.y);
  size_t samples = 2000, hits = 0, mismatch = 0;
  for(size_t i=0; i<samples; ++i) {
    const rc::Vec3 p = {rx(rng),ry(rng),rz(rng)};
    auto a = coll.groundHeight(p);
    auto b = groundBrute(coll,p);
    // queries are limited to the grid volume, like the grid itself: parts of faces below it are not found
    if(b && *b<g.min.y)
      b.reset();
    if(a.has_value()!=b.has_value() || (a && std::abs(*a-*b)>1e-3f)) {
      if(mismatch<5)
        std::printf("  mismatch at (%.3f %.3f %.3f): grid %s, brute %s\n",p.x,p.y,p.z,
                    a ? std::to_string(*a).c_str() : "none",b ? std::to_string(*b).c_str() : "none");
      mismatch++;
      }
    hits += a ? 1 : 0;
    }
  std::printf("raycast self-test: %zu samples, %zu hit ground, %zu mismatches\n",samples,hits,mismatch);

  if(objOut!=nullptr) {
    std::ofstream f(objOut);
    for(auto& t:coll.triangles())
      f << "v " << t.a.x << " " << t.a.y << " " << t.a.z << "\n"
        << "v " << t.b.x << " " << t.b.y << " " << t.b.z << "\n"
        << "v " << t.c.x << " " << t.c.y << " " << t.c.z << "\n";
    for(size_t i=0; i<coll.triangles().size(); ++i)
      f << "f " << i*3+1 << " " << i*3+2 << " " << i*3+3 << "\n";
    }
  return mismatch==0 ? 0 : 3;
  }

// the props of a track (stone.sb) as rigid bodies in `world`, left to settle on the ground like the game does
struct TrackProp {
  const rc::SbInstance*     instance = nullptr;
  std::unique_ptr<rc::Prop> body;
  rc::Vec3                  rest;     // where it came to rest
  };

static std::vector<TrackProp> placeProps(const fs::path& game, const rc::SbScene& scene, rc::PhysicsWorld& world) {
  rc::ObjectLibrary      lib([&](const std::string& p){ return readFile(game/p); });
  std::vector<TrackProp> ret;
  for(auto& p:lib.place(scene)) {
    if(p.model->kind!=rc::ObjectLibrary::Kind::Prop)
      continue;
    const rc::SbScript* s = p.model->scene->script(p.model->id);
    TrackProp tp;
    tp.instance = p.instance;
    tp.body     = std::make_unique<rc::Prop>(world,rc::propGeometry(*p.model->scene,p.model->id,p.model->origin),
                                             rc::PropParams::parse(s ? s->text : std::string()),p.instance->world);
    ret.push_back(std::move(tp));
    }
  for(int i=0; i<int(2.f/rc::PhysicsWorld::FixedDt); ++i)
    world.step(rc::PhysicsWorld::FixedDt);
  for(auto& p:ret)
    p.rest = p.body->pose().translation();
  return ret;
  }

// headless test drive: an autopilot steers straight at the next checkpoint (cp_1, cp_1_1 .. cp_2 ..) for up to
// two minutes or one lap, backing up when stuck. It knows nothing about the track's shape, so it gets stuck
// where the straight line to the next checkpoint runs into a wall or a post. The test fails if the car does not
// reach even the first checkpoint after the start or leaves the track (falls through or out of the collision grid).
static int cmdDrive(const fs::path& game, const fs::path& track, rc::PackFile& pack, int car, int upgrade) {
  auto scene = rc::SbScene::load(readFile(game/track));
  auto cars  = rc::SbScene::load(readFile(game/"RCCarsDB"/"Car.sb"));
  rc::CollisionMesh coll(scene);
  coll.setSeas(rc::Seas::load(scene,pack,track.stem().string()));
  const std::string model = "Car"+std::to_string(car);
  const auto geom = rc::carGeometry(cars,model);
  const auto prm  = rc::CarParams::load(pack,car,upgrade);

  std::printf("%s: %zu wheels, size %.3f x %.3f x %.3f m\n",model.c_str(),geom.wheels.size(),
              geom.max.x-geom.min.x,geom.max.y-geom.min.y,geom.max.z-geom.min.z);
  for(auto& w:geom.wheels)
    std::printf("  %-18s hub (%.3f %.3f %.3f) radius %.3f width %.3f\n",w.name.c_str(),
                w.center.x,w.center.y,w.center.z,w.radius,w.width);
  std::printf("boost: top %.1f km/h, accel %.1f m/s^2 at 0, tank %.0f units (+%.2f/s, -%.2f/s while boosting)\n",
              prm.boostTopSpeed*3.6f,prm.acceleration(0,1,true),prm.boostTank,prm.boostCharge,prm.boostDrain);
  std::printf("params: top %.1f km/h, accel %.1f m/s^2 at 0, steer %.1f deg, spring %.0f/s^2 damp %.1f/%.1f rest %.3f m,"
              " grip %.2f/%.2f, drag %.4f, rolling %.2f, deep sand %.3f, water %.3f\n",prm.topSpeed*3.6f,
              prm.acceleration(0),prm.steerAngle,prm.suspStiffness,prm.suspDamping,prm.suspCompression,prm.suspRest,
              prm.gripFront,prm.gripRear,prm.airDrag,prm.rolling,prm.deepSandDrag,prm.waterDrag);

  // checkpoints in driving order: cp_1, cp_1_1, cp_1_2 .., cp_2, .. and back to cp_1
  rc::Checkpoints cps(scene,coll);
  if(cps.empty()) {
    std::printf("no checkpoints\n");
    return 3;
    }
  const auto& route = cps.route();

  rc::Mat4 start;
  if(auto p = scene.instance("Player"))
    start = p->world;
  if(auto h = coll.groundHeight(start.translation()+rc::Vec3{0,1.f,0}))
    start.m[1][3] = *h - geom.min.y;
  cps.reset(start);

  rc::PhysicsWorld world(coll);
  auto             props = placeProps(game,scene,world);
  rc::Vehicle      veh(world,geom,prm,start);
  const float dt = 1.f/60.f, limit = 120.f;
  size_t flips = 0, reverses = 0, drowned = 0, passed = 0;
  float  maxSpeed = 0, stuck = 0, backing = 0, upsideDown = 0, t = 0;
  bool   fell = false;
  int    frame = 0;
  std::map<rc::Surface,float> onSurface;
  // the autopilot drives at every point in turn (the arrow moves on earlier, and cutting corners lands it in the sea)
  size_t   aim  = 1;
  rc::Vec3 prev = veh.pose().translation();
  for(; t<limit && cps.laps()==0; t+=dt, ++frame) {
    const rc::Mat4 m = veh.pose();
    const rc::Vec3 p = m.translation(), f = m.vector({0,0,1});
    const size_t   next = cps.next();
    if(cps.passes(aim,prev,p) || (route[aim].position-p).length()<1.5f)
      aim = (aim+1<route.size()) ? aim+1 : 1;
    prev = p;
    if(cps.update(p)) {
      std::printf("%6.2f reached %-6s (%7.2f %6.2f %7.2f)\n",t,route[next].name.c_str(),p.x,p.y,p.z);
      ++passed;
      continue;
      }
    rc::Vec3 to = route[aim].position-p;
    to.y = 0;
    // signed angle from the car's heading to the target, positive = target on the left (+X)
    const float want  = std::atan2(to.x,to.z), have = std::atan2(f.x,f.z);
    float       angle = want-have;
    while(angle> 3.14159265f) angle -= 6.2831853f;
    while(angle<-3.14159265f) angle += 6.2831853f;

    rc::Vehicle::Input in;
    in.steer    = std::clamp(angle/0.35f,-1.f,1.f);
    in.throttle = std::abs(angle)>1.0f ? 0.5f : 1.f;
    // stuck against something: back up for a moment, steering the other way
    stuck = (std::abs(veh.speed())<0.5f && backing<=0) ? stuck+dt : 0;
    if(stuck>1.5f) {
      backing = 1.2f;
      stuck   = 0;
      ++reverses;
      }
    if(backing>0) {
      backing    -= dt;
      in.throttle = -1;
      in.steer    = -in.steer;
      }
    // on its side or roof: put it back on the wheels, as the game does
    upsideDown = m.vector({0,1,0}).y<0.3f ? upsideDown+dt : 0;
    if(upsideDown>1.5f) {
      veh.recover();
      upsideDown = 0;
      ++flips;
      }
    veh.update(in,dt);
    world.step(dt);
    maxSpeed = std::max(maxSpeed,veh.speed());
    onSurface[veh.surface()] += dt;
    // in the sea for too long: back to the last checkpoint, as the game does
    if(veh.drowned()) {
      const rc::Vec3 at = veh.pose().translation();
      std::printf("%6.2f drowned at (%.2f %.2f %.2f), back to %s\n",t,at.x,at.y,at.z,
                  route[cps.lastPassed()].name.c_str());
      veh.reset(cps.respawn(geom.min.y));
      aim  = cps.guide();
      prev = veh.pose().translation();
      ++drowned;
      continue;
      }
    const rc::Vec3 now = veh.pose().translation();
    if(!std::isfinite(now.y) || now.y<scene.collision.min.y-1.f) {
      std::printf("%6.2f fell off the track at (%.2f %.2f %.2f)\n",t,now.x,now.y,now.z);
      fell = true;
      break;
      }
    if(frame%60==0)
      std::printf("%6.2f pos (%7.2f %6.2f %7.2f) speed %5.1f km/h wheels %zu on %s -> %s\n",t,p.x,p.y,p.z,
                  veh.speed()*3.6f,veh.wheelsOnGround(),rc::surfaceName(veh.surface()),route[next].name.c_str());
    }
  const bool lap = cps.laps()>0;
  std::printf("%s: %zu of %zu checkpoints in %.1f s, max speed %.1f km/h, %zu times backed up, %zu times put back"
              " on the wheels, drowned %zu times\n",lap ? "lap finished" : "lap not finished",passed,cps.mainCount(),t,
              maxSpeed*3.6f,reverses,flips,drowned);
  size_t knocked = 0;
  for(auto& p:props) {
    const rc::Vec3 at = p.body->pose().translation();
    if((at-p.rest).length()>0.1f) {
      std::printf("  knocked %-18s %.2f m away\n",p.instance->name.c_str(),(at-p.rest).length());
      ++knocked;
      }
    }
  std::printf("props: %zu of %zu knocked away\n",knocked,props.size());
  std::printf("time on:");
  for(auto& [s,v]:onSurface)
    std::printf(" %s %.1f s,",rc::surfaceName(s),v);
  std::printf("\n");
  return (!fell && passed>0) ? 0 : 3;
  }

// Every gate under real physics: the car is put down a few metres before the gate, facing it, and drives at it with
// full throttle, steering at the marker. A gate counts as working when Checkpoints::passes() fires on the way. The
// car starts on the straight line from the previous checkpoint, 12 m out and then closer, and then on the gate's
// own axis, so that a rock or a wall between two checkpoints far apart does not fail the gate. Where it never
// counts, the run reports where the car crossed the gate plane or how close it got.
static int cmdGates(const fs::path& game, const fs::path& track, rc::PackFile& pack, int car) {
  auto scene = rc::SbScene::load(readFile(game/track));
  auto cars  = rc::SbScene::load(readFile(game/"RCCarsDB"/"Car.sb"));
  rc::CollisionMesh coll(scene);
  coll.setSeas(rc::Seas::load(scene,pack,track.stem().string()));
  const auto geom = rc::carGeometry(cars,"Car"+std::to_string(car));
  const auto prm  = rc::CarParams::load(pack,car,0);
  rc::Checkpoints cps(scene,coll);
  if(cps.empty()) {
    std::printf("no checkpoints\n");
    return 3;
    }
  const auto& route = cps.route();
  rc::PhysicsWorld world(coll);
  auto             props = placeProps(game,scene,world);
  rc::Vehicle      veh(world,geom,prm,rc::Mat4());
  const float dt = 1.f/60.f;
  size_t ok = 0;
  for(size_t i=1; i<route.size(); ++i) {
    const auto&    g    = route[i];
    const rc::Vec3 side = {-g.dir.z,0,g.dir.x};
    rc::Vec3 line = g.position-route[i-1].position;
    line.y = 0;
    line   = line*(1.f/line.length());
    std::vector<rc::Vec3> starts;
    for(float run:{12.f,8.f,5.f,3.f})
      starts.push_back(g.position-line*std::min(run,(g.position-route[i-1].position).length()));
    for(float run:{8.f,5.f,3.f})
      for(float s:{0.f,-0.25f,0.25f})
        starts.push_back(g.position-g.dir*run+side*(s*g.width));

    bool        passed = false;
    int         tries  = 0;
    float       t      = 0, closest = 1e9f;
    rc::Vec3    near;
    std::string miss;
    for(const rc::Vec3& at:starts) {
      auto h = coll.groundHeight(at+rc::Vec3{0,1.f,0});
      if(!h || std::abs(*h-g.bottom)>4.f)
        continue;
      rc::Vec3 to = g.position-at;
      const float yaw = std::atan2(to.x,to.z)*180.f/3.14159265f;
      rc::Mat4 pose = rc::Mat4::translate(at)*rc::Mat4::rotate(1,yaw);
      pose.m[1][3] = *h - geom.min.y;
      veh.reset(pose);
      for(int k=0; k<30; ++k)
        world.step(dt);
      ++tries;
      rc::Vec3 last = veh.pose().translation();
      float stuck = 0, backing = 0;
      for(t=0; t<12.f && !passed; t+=dt) {
        const rc::Mat4 m = veh.pose();
        const rc::Vec3 p = m.translation(), f = m.vector({0,0,1});
        to   = g.position-p;
        to.y = 0;
        float angle = std::atan2(to.x,to.z)-std::atan2(f.x,f.z);
        while(angle> 3.14159265f) angle -= 6.2831853f;
        while(angle<-3.14159265f) angle += 6.2831853f;
        rc::Vehicle::Input in;
        in.steer    = std::clamp(angle/0.35f,-1.f,1.f);
        in.throttle = std::abs(angle)>1.0f ? 0.5f : 1.f;
        stuck = (std::abs(veh.speed())<0.5f && backing<=0) ? stuck+dt : 0;
        if(stuck>1.5f) { backing = 1.2f; stuck = 0; }
        if(backing>0) { backing -= dt; in.throttle = -1; in.steer = -in.steer; }
        if(m.vector({0,1,0}).y<0.3f)
          veh.recover();
        veh.update(in,dt);
        world.step(dt);
        const rc::Vec3 now = veh.pose().translation();
        if(now.y<g.bottom-10.f)
          break; // fell through the world where the car was put down
        passed = cps.passes(i,last,now);
        const float f0 = rc::Vec3::dot(last-g.position,g.dir), f1 = rc::Vec3::dot(now-g.position,g.dir);
        if(!passed && f0<0 && f1>=0 && (now-last).length()<5.f) {
          char buf[160];
          std::snprintf(buf,sizeof(buf),"crossed the plane %.2f m to the side, %.2f m above the marker (gate %.2f..%.2f)",
                        rc::Vec3::dot(now-g.position,side),now.y-g.position.y,g.bottom-g.position.y,g.height+0.5f);
          miss = buf;
          }
        if((now-g.position).length()<closest) {
          closest = (now-g.position).length();
          near    = {rc::Vec3::dot(now-g.position,g.dir),rc::Vec3::dot(now-g.position,side),now.y-g.position.y};
          }
        last = now;
        }
      if(passed)
        break;
      }
    if(passed) {
      ++ok;
      std::printf("  %-8s passed in %5.2f s (start %d)\n",g.name.c_str(),t,tries);
      continue;
      }
    if(miss.empty()) {
      char buf[160];
      std::snprintf(buf,sizeof(buf),"never reached it: %.1f m away at best (%.1f in front, %.1f to the side, %.1f above)",
                    closest,near.x,near.y,near.z);
      miss = buf;
      }
    std::printf("  %-8s MISSED after %d starts, %s\n",g.name.c_str(),tries,miss.c_str());
    }
  std::printf("%zu of %zu gates passed under physics\n",ok,route.size()-1);
  return ok+1==route.size() ? 0 : 3;
  }

// The race logic on its own: the car glides along the checkpoint markers (straight lines between them, on the
// ground below) at a steady speed, so every gate must be passed and every lap counted. Then the same car
// turns round after the start to check the wrong way warning, and passes one gate outside to check it is missed.
static int cmdRace(const fs::path& game, const fs::path& track, int laps) {
  auto scene = rc::SbScene::load(readFile(game/track));
  rc::CollisionMesh coll(scene);
  rc::Checkpoints   cps(scene,coll);
  if(cps.empty()) {
    std::printf("no checkpoints\n");
    return 3;
    }
  const auto& route = cps.route();
  float length = 0;
  std::printf("gates:\n");
  for(size_t i=0; i+1<route.size(); ++i) {
    const auto& p = route[i];
    const float d = (route[i+1].position-p.position).length();
    length += d;
    const auto  g = coll.groundHeight(p.position+rc::Vec3{0,1.f,0});
    std::printf("  %-8s (%7.2f %6.2f %7.2f) width %4.1f height %3.1f expand %3.1f, %5.1f above the ground,"
                " %5.1f m to the next\n",p.name.c_str(),p.position.x,p.position.y,p.position.z,p.width,p.height,
                p.expand,g ? p.position.y-*g : 99.f,d);
    }
  std::printf("%zu points, %zu of them main checkpoints, %.0f m a lap along the markers\n",route.size()-1,
              cps.mainCount(),length);

  rc::Mat4 start;
  if(auto p = scene.instance("Player"))
    start = p->world;
  const rc::Vec3 lift  = {0,0.15f,0}; // body centre above the ground
  const float    speed = 8.f, dt = 1.f/60.f;

  // 1. a whole race along the route
  rc::Race race(cps,laps);
  race.restart(start);
  rc::Vec3 pos = start.translation()+lift;
  size_t   at  = 1, events = 0;
  float    t   = 0;
  int      wrongFrames = 0;
  while(race.phase()!=rc::Race::Phase::Finished && t<60.f*float(laps)*4.f) {
    rc::Vec3 vel;
    if(!race.holdCar()) {
      rc::Vec3 to = route[at].position+lift-pos;
      if(to.length()<speed*dt) {
        at = (at+1<route.size()) ? at+1 : 1;
        to = route[at].position+lift-pos;
        }
      vel = to*(speed/std::max(to.length(),1e-3f));
      pos = pos+vel*dt;
      }
    // on the ground where there is some under the line between the markers
    rc::Vec3 car = pos;
    if(auto g = coll.groundHeight(pos+rc::Vec3{0,1.5f,0}))
      car.y = *g+lift.y;
    const auto ev = race.update(dt,car,vel);
    t += dt;
    wrongFrames += race.wrongWay() ? 1 : 0;
    if(ev==rc::Race::Event::None)
      continue;
    ++events;
    if(ev==rc::Race::Event::Go)
      std::printf("%7.2f go\n",t);
    if(ev==rc::Race::Event::Lap || ev==rc::Race::Event::BestLap || ev==rc::Race::Event::Finish)
      std::printf("%7.2f lap %zu: %.2f s%s\n",t,race.lapTimes().size(),race.lapTimes().back(),
                  ev==rc::Race::Event::BestLap ? " (best)" : ev==rc::Race::Event::Finish ? " - finish" : "");
    }
  const bool finished = race.phase()==rc::Race::Phase::Finished;
  std::printf("race %s: %d laps in %.2f s, best lap %.2f s, %zu events, wrong way for %d frames\n",
              finished ? "finished" : "NOT finished",race.laps(),race.time(),race.bestLap(),events,wrongFrames);

  // 2. turn round right after the start: the warning must come, and go once heading for the checkpoint again
  race.restart(start);
  pos = start.translation()+lift;
  const rc::Vec3 back = (pos-route[1].position)*(1.f/(pos-route[1].position).length());
  float wrongAt = -1, rightAt = -1;
  for(t=0; t<12.f; t+=dt) {
    rc::Vec3 vel;
    if(!race.holdCar())
      vel = (t<7.f) ? back*speed : back*-speed;
    pos = pos+vel*dt;
    race.update(dt,pos,vel);
    if(race.wrongWay() && wrongAt<0)
      wrongAt = t;
    if(!race.wrongWay() && wrongAt>=0 && rightAt<0)
      rightAt = t;
    }
  std::printf("driving backwards from %.1f s: wrong way from %.2f s, cleared at %.2f s after turning at 7 s\n",
              rc::Race::CountdownTime,wrongAt,rightAt);

  // 3. past the first main gate outside its width (its sub-checkpoints counted as driven): not counted
  race.restart(start);
  cps.skipWay();
  const auto&    g    = route[cps.next()];
  const rc::Vec3 side = rc::Vec3{-g.dir.z,0,g.dir.x}*(g.width*0.5f+1.f);
  bool counted = false;
  for(int i=-60; i<=60; ++i) {
    const rc::Vec3 p = g.position+lift+side+g.dir*(float(i)*0.1f);
    counted |= cps.update(p);
    }
  std::printf("passing %s 1 m outside its width: %s\n",g.name.c_str(),counted ? "COUNTED" : "not counted");

  // 4. along the route, but beside the gates of the sub-checkpoints (2 m outside them): every lap still counts,
  // and the arrow (guide) only moves forward
  race.restart(start);
  size_t laps4 = 0, guideBack = 0;
  {
    auto via = [&](size_t i) {
      const auto& p = route[i];
      return p.main ? p.position+lift : p.position+lift+rc::Vec3{-p.dir.z,0,p.dir.x}*(p.width*0.5f+2.f)+p.dir*0.3f;
      };
    cps.update(route[0].position+lift);
    for(int lap=0; lap<2; ++lap)
      for(size_t i=1; i<route.size(); ++i) {
        const rc::Vec3 a = via(i-1), b = via(i);
        const int steps = int((b-a).length()/0.1f)+1;
        for(int k=1; k<=steps; ++k) {
          rc::Vec3 q = a+(b-a)*(float(k)/float(steps));
          if(auto h = coll.groundHeight(q+rc::Vec3{0,1.5f,0}))
            q.y = *h+lift.y;
          const size_t before = cps.guide();
          cps.update(q);
          if(cps.guide()<before && cps.guide()!=1)
            ++guideBack;
          }
        }
    laps4 = cps.laps();
  }
  std::printf("beside the sub-checkpoints: %zu of 2 laps, the arrow went back %zu times\n",laps4,guideBack);

  const bool ok = finished && wrongFrames==0 && wrongAt>0 && rightAt>7.f && !counted && laps4==2 && guideBack==0;
  std::printf("%s\n",ok ? "ok" : "FAILED");
  return ok ? 0 : 3;
  }

static int cmdObjects(const fs::path& game, const fs::path& track) {
  auto scene = rc::SbScene::load(readFile(game/track));
  rc::ObjectLibrary lib([&](const std::string& p){ return readFile(game/p); });
  rc::CollisionMesh coll(scene);

  size_t missing = 0, placed = 0;
  for(auto& in:scene.instances) {
    if(in.name=="Player")
      continue;
    auto m = lib.find(in);
    if(m==nullptr) {
      std::printf("  ?         %-20s model \"%s\" not found\n",in.name.c_str(),in.model.c_str());
      ++missing;
      continue;
      }
    ++placed;
    const auto geo  = rc::propGeometry(*m->scene,m->id,m->origin);
    const auto at   = in.world.translation();
    const auto size = geo.max-geo.min;
    std::printf("  %-9s %-20s %-10s at %7.2f %6.2f %7.2f  size %.2f x %.2f x %.2f",
                rc::ObjectLibrary::kindName(m->kind),in.name.c_str(),m->name.c_str(),at.x,at.y,at.z,size.x,size.y,size.z);
    if(m->kind==rc::ObjectLibrary::Kind::Prop) {
      const rc::SbScript* s = m->scene->script(m->id);
      const auto prm = rc::PropParams::parse(s ? s->text : std::string());
      std::printf("  shape %d mass %.2f bounce %.1f",prm.shape,prm.mass,prm.bounce);
      }
    std::printf("\n");
    }

  // the editor left most props a little above the ground: how far each drops before it comes to rest
  rc::PhysicsWorld world(coll);
  auto   props = placeProps(game,scene,world);
  size_t lost  = 0;
  for(auto& p:props) {
    const rc::Vec3 d = p.rest-p.instance->world.translation();
    const bool     off = !std::isfinite(p.rest.y) || p.rest.y<scene.collision.min.y;
    std::printf("  settled %-20s dropped %5.2f m, slid %5.2f m, at %7.2f %6.2f %7.2f%s\n",p.instance->name.c_str(),-d.y,
                std::sqrt(d.x*d.x+d.z*d.z),p.rest.x,p.rest.y,p.rest.z,off ? "  FELL THROUGH" : "");
    lost += off ? 1 : 0;
    }
  std::printf("%zu objects (%zu props, %zu fell through the track), %zu models not found\n",placed,props.size(),lost,missing);
  return (missing==0 && lost==0) ? 0 : 3;
  }

int main(int argc, char** argv) {
  if(argc<3) {
    std::fprintf(stderr,"usage: rcinfo <game_dir> pack|extract|csi|sb|obj|coll|objects|check|drive|race|gates ...\n");
    return 1;
    }
  try {
    const fs::path game = argv[1];
    const std::string cmd = argv[2];
    if(cmd=="coll" && argc>3)
      return cmdColl(game/argv[3],argc>4 ? argv[4] : nullptr);
    if(cmd=="race" && argc>3)
      return cmdRace(game,argv[3],argc>4 ? std::atoi(argv[4]) : rc::Race::DefaultLaps);
    if(cmd=="objects" && argc>3)
      return cmdObjects(game,argv[3]);
    if(cmd=="obj" && argc>4)
      return cmdObj(game/argv[3],argv[4]);

    rc::PackFile pack(game/"RCCars.pack");
    if(cmd=="pack")
      return cmdPack(pack);
    if(cmd=="check")
      return cmdCheck(game,pack);
    if(cmd=="extract" && argc>4) {
      auto d = pack.read(argv[3]);
      writeFile(argv[4],d.data(),d.size());
      return 0;
      }
    if(cmd=="csi" && argc>4) {
      auto d = pack.read(argv[3]);
      writeTga(argv[4],rc::decodeCsi(d.data(),d.size()));
      return 0;
      }
    if(cmd=="sb" && argc>3)
      return cmdSb(game/argv[3],pack);
    if(cmd=="gates" && argc>3)
      return cmdGates(game,argv[3],pack,argc>4 ? std::atoi(argv[4]) : 1);
    if(cmd=="drive" && argc>3)
      return cmdDrive(game,argv[3],pack,argc>4 ? std::atoi(argv[4]) : 1,argc>5 ? std::atoi(argv[5]) : 0);
    std::fprintf(stderr,"unknown command\n");
    return 1;
    }
  catch(const std::exception& e) {
    std::fprintf(stderr,"error: %s\n",e.what());
    return 2;
    }
  }
