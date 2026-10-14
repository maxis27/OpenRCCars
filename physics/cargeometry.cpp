#include "cargeometry.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

using namespace rc;

namespace {

bool startsWith(const std::string& s, std::string_view prefix) {
  return s.compare(0,prefix.size(),prefix)==0;
  }

bool endsWith(const std::string& s, std::string_view suffix) {
  return s.size()>=suffix.size() && s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;
  }

// springs fixed to the body: SPRING_FRONT_1 .. SPRING_REAR_4 (Overkill), Spring1_2 .. Spring6_2 (Buggy, Hummer)
bool strutNode(const std::string& n) {
  for(std::string_view p:{"SPRING_FRONT_","SPRING_REAR_"})
    if(n.size()==p.size()+1 && startsWith(n,p) && std::isdigit(uint8_t(n.back())))
      return true;
  return n.size()==9 && startsWith(n,"Spring") && std::isdigit(uint8_t(n[6])) && endsWith(n,"_2");
  }

// what RCCars.exe moves when drawing a car (0x504820, 0x505780, 0x5068e0): rigid axles, steering supports, the
// Buggy's arms and wheel carriers, and the springs between them and the body
bool rigNode(const std::string& n) {
  if(n=="FRONT_AXLE" || n=="REAR_AXLE" || n=="MIDDLE_AXLE")
    return true;
  if(startsWith(n,"AXLE_"))
    for(std::string_view e:{"_SUPPORT","_UP","_DOWN","_WHEEL"})
      if(endsWith(n,e))
        return true;
  return strutNode(n);
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

std::optional<std::string> rc::carNode(const SbScene& sb, size_t mesh) {
  if(!carPart(sb,mesh))
    return std::nullopt;
  for(int32_t i=int32_t(mesh); i>=0; i=sb.meshes[size_t(i)].parent) {
    auto& name = sb.meshes[size_t(i)].name;
    if(startsWith(name,"WHEEL_") || rigNode(name))
      return name;
    }
  return std::string();
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

  // suspension nodes
  auto node = [&](const std::string& name) -> int32_t {
    for(size_t i=0; i<sb.meshes.size(); ++i)
      if(sb.meshes[i].model==modelId && sb.meshes[i].name==name)
        return int32_t(i);
    return -1;
    };
  auto under = [&](int32_t mesh, int32_t above) {
    for(int32_t i=mesh; i>=0; i=sb.meshes[size_t(i)].parent)
      if(i==above)
        return true;
    return false;
    };
  auto pivot = [&](int32_t mesh) { return sb.meshes[size_t(mesh)].world.translation(); };
  for(const char* name:{"FRONT_AXLE","REAR_AXLE","MIDDLE_AXLE"}) {
    const int32_t a = node(name);
    if(a<0)
      continue;
    CarGeometry::Axle axle;
    axle.name   = name;
    axle.pivot  = pivot(a);
    axle.middle = axle.name=="MIDDLE_AXLE";
    for(size_t i=0; i<g.wheels.size(); ++i)
      if(under(node(g.wheels[i].name),a)) {
        g.wheels[i].axle = int(g.axles.size());
        (g.wheels[i].left ? axle.left : axle.right) = int(i);
        }
    g.axles.push_back(axle);
    }
  for(auto& w:g.wheels) {
    if(w.name.find("MIDDLE")!=std::string::npos)
      continue;
    const int32_t     wn   = node(w.name);
    const std::string base = std::string("AXLE_")+(w.front ? "FRONT" : "REAR")+(w.left ? "_LEFT" : "_RIGHT");
    const int32_t     sup  = node(base+"_SUPPORT");
    if(sup>=0 && under(wn,sup)) {
      w.support      = base+"_SUPPORT";
      w.supportPivot = pivot(sup);
      }
    const int32_t up = node(base+"_UP"), down = node(base+"_DOWN"), car = node(base+"_WHEEL");
    if(up>=0 && down>=0 && car>=0 && under(wn,car) && under(car,up)) {
      w.armUp        = base+"_UP";
      w.armDown      = base+"_DOWN";
      w.carrier      = base+"_WHEEL";
      w.upPivot      = pivot(up);
      w.downPivot    = pivot(down);
      w.carrierPivot = pivot(car);
      }
    }
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=modelId || !strutNode(m.name))
      continue;
    CarGeometry::Strut st;
    st.name  = m.name;
    st.pivot = m.world.translation();
    if(m.name[0]=='S' && m.name[1]=='p') {
      // Spring<n>_2 on the body reaches Spring<n>_1 (Hummer: on an axle, Buggy: Spring<n>_1_GRE1 on a lower arm)
      const std::string partner = "Spring"+std::string(1,m.name[6])+"_1";
      for(size_t pi=0; pi<sb.meshes.size() && st.anchor.empty(); ++pi) {
        auto& p = sb.meshes[pi];
        if(p.model!=modelId || (p.name!=partner && !startsWith(p.name,partner+"_")))
          continue;
        st.end = p.world.translation();
        for(int32_t i=int32_t(pi); i>=0 && st.anchor.empty(); i=sb.meshes[size_t(i)].parent) {
          auto& n = sb.meshes[size_t(i)].name;
          if(n=="FRONT_AXLE" || n=="REAR_AXLE" || n=="MIDDLE_AXLE" || (startsWith(n,"AXLE_") && endsWith(n,"_DOWN")))
            st.anchor = n;
          }
        }
      } else {
      // SPRING_FRONT_<n> / SPRING_REAR_<n> (Overkill): along its Z axis, as long as its meshes, to the axle
      const Vec3 axis = m.world.vector({0,0,1}).normalized();
      float      ext  = 0;
      for(size_t ci=0; ci<sb.meshes.size(); ++ci) {
        auto& c = sb.meshes[ci];
        if(c.model!=modelId || !under(int32_t(ci),int32_t(mi)))
          continue;
        for(auto& v:c.positions)
          ext = std::max(ext,Vec3::dot(c.world.point(v)-st.pivot,axis));
        }
      st.end    = st.pivot+axis*ext;
      st.anchor = m.name.find("FRONT")!=std::string::npos ? "FRONT_AXLE" : "REAR_AXLE";
      if(ext<=0 || node(st.anchor)<0)
        st.anchor.clear();
      }
    if(!st.anchor.empty())
      g.struts.push_back(st);
    }

  for(int n=1; n<=4; ++n) {
    const std::string name = "booster_"+std::to_string(n)+"_end";
    auto it = std::find_if(sb.meshes.begin(),sb.meshes.end(),[&](const SbMesh& m){ return m.model==modelId && m.name==name; });
    if(it==sb.meshes.end()) {
      g.exhausts.clear();
      break;
      }
    g.exhausts.push_back({it->world.translation(),it->world.vector({0,0,1}).normalized()});
    }
  return g;
  }
