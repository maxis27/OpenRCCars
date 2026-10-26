#include "carmodel.h"

#include <Tempest/Log>

#include <algorithm>
#include <string>

#include "formats/pack.h"
#include "formats/settings.h"

using namespace Tempest;

CarModel::Distances CarModel::Distances::load(rc::PackFile& pack) {
  Distances d;
  try {
    auto data = pack.read("Settings\\LOD.ini");
    auto f    = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(data.data()),data.size()));
    // slider positions 1..100: the first distance 2 .. 12 m, the steps to the next ones 0 .. 10 m
    auto step = [&](std::string_view key, int32_t def) {
      return float(std::clamp(f.raw(key,def),1,100)-1)*(100.f/99.f)*0.1f;
      };
    const float lod0 = 2.f+step("DistLOD1SwitchOn",1);
    d.lod1 = lod0+step("Delta12",60);
    d.lod2 = d.lod1+step("Delta23",10);
    }
  catch(const std::exception& e) {
    Log::e("LOD.ini: ",e.what());
    }
  return d;
  }

CarModel::CarModel(Device& device, Resources& res, const rc::SbScene& sb, size_t car,
                   const ModelMesh::CarLook& look) {
  const std::string name = "Car"+std::to_string(car+1);
  lods[0] = std::make_unique<ModelMesh>(device,res,sb,name,look);
  for(size_t i=1; i<lods.size(); ++i) {
    ModelMesh::CarLook l = look;
    l.lod     = true;
    l.antenna = true;   // their own, standing still
    try {
      lods[i] = std::make_unique<ModelMesh>(device,res,sb,name+"LOD"+std::to_string(i),l);
      }
    catch(const std::exception& e) {
      Log::e(e.what());
      }
    }
  }

ModelMesh& CarModel::at(float distance, const Distances& d) {
  // 0x501c10: the full model up to the second distance (the first only tells the player's car from the
  // opponents' there), LOD1 to the third, LOD2 beyond
  size_t i = distance>=d.lod2 ? 2 : distance>=d.lod1 ? 1 : 0;
  while(i>0 && lods[i]==nullptr)
    --i;
  return *lods[i];
  }

void lightCar(ModelMesh& mesh, bool nearest, const rc::fx::CarLight& light, float shade) {
  if(!nearest) {
    mesh.setLight(1);
    return;
    }
  // Direct3D's colours: 0..255, held at 255
  mesh.setLight(std::min(light.ambient*shade,1.f),std::min(light.direct*shade,1.f),light.toLight);
  }
