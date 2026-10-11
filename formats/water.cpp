#include "water.h"

#include <algorithm>
#include <map>

#include "pack.h"
#include "scene.h"

using namespace rc;

namespace {

// slider position clamped to 0..max, as the original's loaders do
float slider(const SliderFile& f, std::string_view key, float def) {
  auto s = f.find(key);
  if(s==nullptr)
    return def;
  return std::clamp(float(s->value),0.f,float(std::max(s->max,0)));
  }

}

std::optional<WaterWaves> WaterWaves::parse(const SliderFile& f) {
  if(f.find("amp")==nullptr || f.find("amp2")==nullptr)
    return std::nullopt;
  WaterWaves w;
  w.amp               = slider(f,"amp",0)*0.01f;
  w.angle             = slider(f,"angle",0)*10.f;
  w.speed             = slider(f,"speed",0)*0.01f;
  w.period            = slider(f,"period",0)*0.1f;
  w.amp2              = slider(f,"amp2",0)*0.01f;
  w.period2           = slider(f,"period2",0)*0.1f;
  w.length2           = std::max(slider(f,"length2",100)*0.01f,0.01f);
  w.center            = {slider(f,"posX",500)-500.f, 0, slider(f,"posZ",500)-500.f};
  w.offset            = slider(f,"offset",50)*0.01f-0.5f;
  w.magnetOffset      = slider(f,"magnetOffset",50)*0.01f-0.5f;
  w.magnetRadius      = slider(f,"magnetRadius",0)*0.05f;
  w.alphaRadius       = slider(f,"alphaRadius",0)*0.1f;
  w.distAlphaClampMin = slider(f,"distAlphaClampMin",0);
  w.distAlphaClampMax = slider(f,"distAlphaClampMax",0);
  w.colorMin          = slider(f,"colorMin",255);
  w.colorMax          = slider(f,"colorMax",255);
  return w;
  }

CoastFoam CoastFoam::parse(const SliderFile& f) {
  CoastFoam c;
  c.heightOn  = (slider(f,"HeightOn",100)-100.f)*0.01f;
  c.scrollVel = slider(f,"ScrollVel",0)*0.01f;
  c.alphaMin  = slider(f,"AlphaMin",0)/255.f;
  c.alphaMax  = slider(f,"AlphaMax",255)/255.f;
  return c;
  }

Seas Seas::load(const SbScene& scene, PackFile& pack, std::string_view track) {
  Seas ret;
  ret.ofMesh.assign(scene.meshes.size(),-1);
  if(track.empty())
    return ret;
  std::map<std::string,int32_t> byName; // node name -> sea, -1 none
  auto lookup = [&](const std::string& name) {
    auto it = byName.find(name);
    if(it!=byName.end())
      return it->second;
    int32_t     id   = -1;
    std::string file = "Settings\\WLOD_"+std::string(track)+"_"+name+".ini";
    if(pack.find(file)!=nullptr) {
      auto d = pack.read(file);
      if(auto w = WaterWaves::parse(SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size())))) {
        id = int32_t(ret.waves.size());
        ret.waves.push_back(*w);
        ret.names.push_back(name);
        }
      }
    byName.emplace(name,id);
    return id;
    };
  for(size_t mi=0; mi<scene.meshes.size(); ++mi)
    for(int32_t p=int32_t(mi); p>=0 && ret.ofMesh[mi]<0; p=scene.meshes[size_t(p)].parent)
      ret.ofMesh[mi] = lookup(scene.meshes[size_t(p)].name);
  return ret;
  }
