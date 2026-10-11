// rcinfo - command line inspector for RC Cars data files (no GPU / Tempest dependency).
//
//   rcinfo <game_dir> pack                       list pack statistics
//   rcinfo <game_dir> extract <name> <out_file>  extract & decrypt a file from RCCars.pack
//   rcinfo <game_dir> csi <name> <out.tga>       decode a pack texture to TGA
//   rcinfo <game_dir> sb <RCCarsDB/x.sb>         scene statistics (meshes, faces, textures, markers)
//   rcinfo <game_dir> obj <RCCarsDB/x.sb> <out.obj>  export scene geometry (world space) to OBJ
//   rcinfo <game_dir> coll <RCCarsDB/x.sb> [out.obj]  collision grid statistics + self-test, optional OBJ export
//   rcinfo <game_dir> check                      decode every texture in the pack and every RCCarsDB/*.sb

#include "formats/pack.h"
#include "formats/csi.h"
#include "formats/scene.h"
#include "formats/collision.h"

#include <cstdio>
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

int main(int argc, char** argv) {
  if(argc<3) {
    std::fprintf(stderr,"usage: rcinfo <game_dir> pack|extract|csi|sb|obj|coll|check ...\n");
    return 1;
    }
  try {
    const fs::path game = argv[1];
    const std::string cmd = argv[2];
    if(cmd=="coll" && argc>3)
      return cmdColl(game/argv[3],argc>4 ? argv[4] : nullptr);
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
    std::fprintf(stderr,"unknown command\n");
    return 1;
    }
  catch(const std::exception& e) {
    std::fprintf(stderr,"error: %s\n",e.what());
    return 2;
    }
  }
