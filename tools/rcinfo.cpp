// rcinfo - command line inspector for RC Cars data files (no GPU / Tempest dependency).
//
//   rcinfo <game_dir> pack                       list pack statistics
//   rcinfo <game_dir> extract <name> <out_file>  extract & decrypt a file from RCCars.pack
//   rcinfo <game_dir> csi <name> <out.tga>       decode a pack texture to TGA
//   rcinfo <game_dir> sb <RCCarsDB/x.sb>         scene statistics (meshes, faces, textures, markers)
//   rcinfo <game_dir> obj <RCCarsDB/x.sb> <out.obj>  export scene geometry (world space) to OBJ
//   rcinfo <game_dir> check                      decode every texture in the pack and every RCCarsDB/*.sb

#include "formats/pack.h"
#include "formats/csi.h"
#include "formats/scene.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>

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

int main(int argc, char** argv) {
  if(argc<3) {
    std::fprintf(stderr,"usage: rcinfo <game_dir> pack|extract|csi|sb|obj|check ...\n");
    return 1;
    }
  try {
    const fs::path game = argv[1];
    const std::string cmd = argv[2];
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
    if(cmd=="obj" && argc>4)
      return cmdObj(game/argv[3],argv[4]);
    std::fprintf(stderr,"unknown command\n");
    return 1;
    }
  catch(const std::exception& e) {
    std::fprintf(stderr,"error: %s\n",e.what());
    return 2;
    }
  }
