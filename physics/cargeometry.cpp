#include "cargeometry.h"

#include <algorithm>
#include <map>
#include <stdexcept>

using namespace rc;

namespace {

bool startsWith(const std::string& s, std::string_view prefix) {
  return s.compare(0,prefix.size(),prefix)==0;
  }

struct Box {
  Vec3 mn = { 1e9f, 1e9f, 1e9f};
  Vec3 mx = {-1e9f,-1e9f,-1e9f};
  void add(const Vec3& p) {
    mn = {std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
    mx = {std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)};
    }
  bool empty() const { return mn.x>mx.x; }
  };

}

std::optional<std::string> rc::carPart(const SbScene& sb, size_t mesh) {
  // low detail copies, drawn by the original engine only at distance
  auto& self = sb.meshes[mesh].name;
  if(self.find("_LOD1")!=std::string::npos || self.find("_LOD2")!=std::string::npos)
    return std::nullopt;
  std::string part;
  for(int32_t i=int32_t(mesh); i>=0; i=sb.meshes[size_t(i)].parent) {
    auto& name = sb.meshes[size_t(i)].name;
    if(startsWith(name,"UPGRADES"))
      return std::nullopt; // boosters and compressors, shown only once bought
    if(part.empty() && startsWith(name,"WHEEL_"))
      part = name;
    }
  return part;
  }

CarGeometry rc::carGeometry(const SbScene& sb, std::string_view model) {
  const int32_t modelId = sb.findModel(model);
  if(modelId<0)
    throw std::runtime_error("car model \""+std::string(model)+"\" not found");

  Box                       body, all, solid;
  std::map<std::string,Box> wheels;
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=modelId || m.positions.empty())
      continue;
    auto part = carPart(sb,mi);
    if(!part)
      continue;
    Box& box = part->empty() ? body : wheels[*part];
    const bool antenna = startsWith(m.name,"ANTENNA");
    for(auto& f:m.faces) {
      // only faces that are drawn (have a texture), like ModelMesh
      if(f.pos.size()<3 || f.layer(LayerTag::Base)==nullptr)
        continue;
      for(auto pi:f.pos) {
        if(pi>=m.positions.size())
          continue;
        const Vec3 p = m.world.point(m.positions[pi]);
        box.add(p);
        all.add(p);
        if(!antenna)
          solid.add(p);
        }
      }
    }

  CarGeometry g;
  g.bodyMin = body.mn;
  g.bodyMax = body.mx;
  g.min     = all.mn;
  g.max     = all.mx;
  g.roof    = solid.mx.y;
  for(auto& [name,box]:wheels) {
    if(box.empty())
      continue;
    CarGeometry::Wheel w;
    w.name   = name;
    w.center = (box.mn+box.mx)*0.5f;
    w.radius = std::max(box.mx.y-box.mn.y,box.mx.z-box.mn.z)*0.5f;
    w.width  = box.mx.x-box.mn.x;
    w.front  = name.find("FRONT")!=std::string::npos;
    w.left   = name.find("LEFT")!=std::string::npos;
    g.wheels.push_back(w);
    }
  return g;
  }
