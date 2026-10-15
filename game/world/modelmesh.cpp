#include "modelmesh.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "formats/settings.h"
#include "game/resources.h"
#include "physics/cargeometry.h"

using namespace Tempest;

namespace {

// How strong the environment map (ScenePipelines::environment) is on a mesh, 0 = none. RCCars.exe gives it to the
// meshes flagged so in the scene (SbMesh::flags 0x20000), to those named *GRE1* or *GRE2* and to the car's body,
// ENVIR_CAR_BODY_body (else ENVIR_CAR_BODY) (0x48e4b0, 0x4df7b0, 0x507130). On the cars the name picks the strength
// from Settings/carEnvMap.ini (0x4f9d00, 0x5080b0, 0x507d80): the body EnvMapBody, *GRE1* (chrome) EnvMapGRE1,
// *GRE2* (lights, bumpers) EnvMapGRE2, each /255; anything else gets the full map.
float environmentOf(const rc::SbMesh& m, bool car, rc::PackFile& pack) {
  const bool gre1 = m.name.find("GRE1")!=std::string::npos;
  const bool gre2 = m.name.find("GRE2")!=std::string::npos;
  const bool body = m.name=="ENVIR_CAR_BODY_body" || m.name=="ENVIR_CAR_BODY";
  if(!gre1 && !gre2 && !body && (m.flags & 0x20000)==0)
    return 0;
  if(!car)
    return 1;
  const int kind = (gre1 ? 2 : 0) | (gre2 ? 3 : 0) | (body ? 1 : 0);
  if(kind==0)
    return 1;
  static const char* const keys[] = {"", "EnvMapBody", "EnvMapGRE1", "EnvMapGRE2"};
  static const int         defs[] = {255, 112, 255, 137};
  int v = defs[kind];
  try {
    auto d = pack.read("Settings\\carEnvMap.ini");
    auto f = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    if(auto k = f.find(keys[kind]))
      v = std::clamp(k->value,0,255);
    }
  catch(const std::exception&) {
    }
  return float(v)/255.f;
  }

}

ModelMesh::ModelMesh(Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
                     const rc::Vec3& origin, int skin)
  :ModelMesh(device,res,sb,model,origin,Options(),skin) {
  }

ModelMesh::ModelMesh(Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
                     const rc::Vec3& origin, const Options& opt)
  :ModelMesh(device,res,sb,model,origin,opt,0) {
  }

ModelMesh::ModelMesh(Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
                     const rc::Vec3& origin, const Options& opt, int skin)
  :modelName(model), pso(device) {
  const int32_t modelId = sb.findModel(model);
  if(modelId<0)
    throw std::runtime_error("model \""+std::string(model)+"\" not found");

  bbox[0] = { 1e9f, 1e9f, 1e9f};
  bbox[1] = {-1e9f,-1e9f,-1e9f};
  partList.push_back(Part{});
  const bool named = !opt.parts.empty();
  for(auto& n:opt.parts) {
    Part p{n,{}};
    for(auto& m:sb.meshes)
      if(m.model==modelId && m.name==n) {
        p.pivot = m.world.translation()-origin;
        break;
        }
    partList.push_back(p);
    }
  // the nearest node above (or at) `mesh` named in `names`, -1 if none
  auto under = [&](size_t mesh, const std::vector<std::string>& names) -> int32_t {
    for(int32_t i=int32_t(mesh); i>=0; i=sb.meshes[size_t(i)].parent) {
      auto it = std::find(names.begin(),names.end(),sb.meshes[size_t(i)].name);
      if(it!=names.end())
        return int32_t(it-names.begin());
      }
    return -1;
    };

  // part of each mesh: body (0) or the WHEEL_* or suspension node above it (rc::carNode); -1 = hidden
  auto partOf = [&](size_t mesh) -> int32_t {
    if(named) {
      auto& self = sb.meshes[mesh].name;
      if(self.find("_LOD1")!=std::string::npos || self.find("_LOD2")!=std::string::npos)
        return -1;
      if(under(mesh,opt.hidden)>=0)
        return -1;
      return under(mesh,opt.parts)+1;
      }
    auto name = rc::carNode(sb,mesh);
    if(!name)
      return -1;
    if(name->empty())
      return 0;
    auto it = std::find_if(partList.begin(),partList.end(),[&](const Part& p){ return p.name==*name; });
    if(it==partList.end()) {
      partList.push_back(Part{*name,{}});
      it = partList.end()-1;
      }
    return int32_t(it-partList.begin());
    };

  // 1. model space triangles, bucketed by part, texture and environment map
  const bool car = model.size()>3 && model.rfind("Car",0)==0 && model[3]>='1' && model[3]<='9';
  // key: part, texture, environment map, head light variant (Batch::lit)
  std::map<std::tuple<size_t,int32_t,float,int8_t>,std::vector<LevelMesh::Vertex>> buckets;
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=modelId || m.positions.empty())
      continue;
    const int32_t part = partOf(mi);
    if(part<0)
      continue;
    // mirrored nodes (scale -1) flip the winding
    const bool  mirrored = m.world.determinant3()<0;
    const float env      = m.faces.empty() ? 0.f : environmentOf(m,car,res.pack());
    const bool  light    = !opt.lights.empty() && under(mi,opt.lights)>=0;
    for(int8_t variant=(light ? 0 : -1); variant<=(light ? 1 : -1); ++variant)
    for(auto& f:m.faces) {
      const size_t n    = f.pos.size();
      auto         base = f.layer(rc::LayerTag::Base);
      if(n<3 || base==nullptr)
        continue;
      // a head light switched the other way than modelled shows the other half of its texture
      const float shift = variant<0 || (variant==1)==opt.modelledLit ? 0.f : (opt.modelledLit ? 0.5f : -0.5f);
      auto& out  = buckets[{size_t(part),base->texture,env,variant}];
      auto  vert = [&](size_t corner) {
        LevelMesh::Vertex v = {};
        const uint32_t pi = f.pos[corner];
        if(pi>=m.positions.size())
          return v;
        const rc::Vec3 p  = m.world.point(m.positions[pi])-origin;
        const rc::Vec3 nr = corner<f.normals.size() ? m.world.vector(f.normals[corner]).normalized() : rc::Vec3{0,1,0};
        v.pos[0]    = p.x;  v.pos[1]    = p.y;  v.pos[2]    = p.z;
        v.normal[0] = nr.x; v.normal[1] = nr.y; v.normal[2] = nr.z;
        if(corner<base->uv.size() && base->uv[corner]<m.uvs.size()) {
          v.uvBase[0] = m.uvs[base->uv[corner]].x;
          v.uvBase[1] = m.uvs[base->uv[corner]].y;
          v.uvBase[opt.lightAxis==1 ? 1 : 0] += shift;
          }
        bbox[0] = {std::min(bbox[0].x,p.x),std::min(bbox[0].y,p.y),std::min(bbox[0].z,p.z)};
        bbox[1] = {std::max(bbox[1].x,p.x),std::max(bbox[1].y,p.y),std::max(bbox[1].z,p.z)};
        return v;
        };
      const LevelMesh::Vertex v0 = vert(0);
      for(size_t i=1; i+1<n; ++i) {
        out.push_back(v0);
        out.push_back(vert(mirrored ? i+1 : i));
        out.push_back(vert(mirrored ? i   : i+1));
        }
      }
    }

  // 2. part pivots: centre of the part's bounding box (a wheel's hub)
  for(size_t p=1; p<partList.size() && !named; ++p) {
    rc::Vec3 mn = { 1e9f, 1e9f, 1e9f}, mx = {-1e9f,-1e9f,-1e9f};
    for(auto& [key,verts]:buckets) {
      if(std::get<0>(key)!=p)
        continue;
      for(auto& v:verts) {
        mn = {std::min(mn.x,v.pos[0]),std::min(mn.y,v.pos[1]),std::min(mn.z,v.pos[2])};
        mx = {std::max(mx.x,v.pos[0]),std::max(mx.y,v.pos[1]),std::max(mx.z,v.pos[2])};
        }
      }
    partList[p].pivot = (mn+mx)*0.5f;
    }

  // 3. one vertex buffer, part-relative positions
  std::vector<LevelMesh::Vertex> all;
  for(auto& [key,verts]:buckets) {
    const auto [part,texture,env,lit] = key;
    const rc::Vec3 pivot = partList[part].pivot;
    Batch b;
    b.part   = part;
    b.lit    = lit;
    b.first  = all.size();
    b.count  = verts.size();
    b.envMap = env;
    if(auto t = sb.texture(texture,modelId)) {
      std::string name = t->name;
      for(auto& [from,to]:opt.textures)
        if(name==from && res.texture(to)!=nullptr)
          name = to;
      if(skin>0 && skin<4 && name.rfind("car_",0)==0 && name.find("skin")!=std::string::npos && name.back()=='1') {
        std::string other = name;
        other.back() = char('1'+skin);
        if(res.texture(other)!=nullptr)
          name = other;
        }
      if(auto r = res.texture(name)) {
        b.tex  = &r->tex;
        b.mode = r->alpha;
        }
      // car_askin<car><skin>, car_bskin<car><skin>: dirt_car1_<n>, dirt_car<car>_<n>a / _<n>b (0x531450)
      if(name.size()==11 && (name.rfind("car_askin",0)==0 || name.rfind("car_bskin",0)==0)) {
        const char car = name[9];
        for(int n=0; n<2; ++n) {
          std::string d = std::string("dirt_car")+car+"_"+char('1'+n);
          if(car!='1')
            d += name[4];
          if(auto r = res.texture(d))
            b.dirt[n] = &r->tex;
          }
        if(b.dirt[0]==nullptr || b.dirt[1]==nullptr)
          b.dirt[0] = b.dirt[1] = nullptr;
        }
      }
    if(b.tex==nullptr)
      b.tex = &res.white();
    for(auto v:verts) {
      v.pos[0] -= pivot.x; v.pos[1] -= pivot.y; v.pos[2] -= pivot.z;
      all.push_back(v);
      }
    batches.push_back(b);
    }
  std::stable_sort(batches.begin(),batches.end(),[](const Batch& a, const Batch& b){
    return (a.mode==rc::AlphaMode::Translucent) < (b.mode==rc::AlphaMode::Translucent);
    });

  envTex = &ScenePipelines::environment(res);
  vbo    = device.vbo(all);
  Log::d("model \"",modelName,"\": ",all.size()/3," triangles, ",partList.size()-1," moving parts, size ",
         bbox[1].x-bbox[0].x," x ",bbox[1].y-bbox[0].y," x ",bbox[1].z-bbox[0].z);
  }

void ModelMesh::draw(Encoder<CommandBuffer>& enc, const rc::Mat4& viewProj,
                   const LevelMesh::Push& frame, const rc::Mat4& world,
                   const std::vector<rc::Mat4>* partPoses, bool lightsOn) const {
  const rc::Vec3 camera = {frame.cameraPos[0],frame.cameraPos[1],frame.cameraPos[2]};
  const rc::Vec3 sun    = {frame.sunDir[0],   frame.sunDir[1],   frame.sunDir[2]};

  // the shader works in the space of the vertices, so camera and sun go into part space
  std::vector<LevelMesh::Push> push(partList.size(),frame);
  for(size_t i=0; i<partList.size(); ++i) {
    const bool     own = partPoses!=nullptr && i<partPoses->size();
    const rc::Mat4 m   = world*(own ? (*partPoses)[i] : rc::Mat4::translate(partList[i].pivot));
    // general inverse: objects placed larger (crabs) and the cars' stretched springs (CarRig) are scaled
    const rc::Mat4 inv = m.inverse();
    (viewProj*m).toColumnMajor(push[i].viewProj);
    const rc::Vec3 c = inv.point(camera);
    const rc::Vec3 s = inv.vector(sun);
    push[i].cameraPos[0] = c.x; push[i].cameraPos[1] = c.y; push[i].cameraPos[2] = c.z;
    push[i].sunDir[0]    = s.x; push[i].sunDir[1]    = s.y; push[i].sunDir[2]    = s.z;
    }

  const RenderPipeline* current = nullptr;
  for(auto& b:batches) {
    if(b.lit>=0 && (b.lit==1)!=lightsOn)
      continue;
    auto& p = pso.get(b.mode);
    if(current!=&p) {
      enc.setPipeline(p);
      current = &p;
      }
    LevelMesh::Push& pp = push[b.part];
    pp.flags  = (b.mode==rc::AlphaMode::Cutout) ? uint32_t(LevelMesh::F_Cutout) : 0u;
    pp.envMap = b.envMap;
    const bool dirty = dirt>0 && b.dirt[0]!=nullptr;
    if(dirty) {
      pp.flags |= LevelMesh::F_Dirt;
      pp.time   = dirt;
      }
    enc.setPushData(pp);
    enc.setBinding(0,*b.tex);
    enc.setBinding(1,*b.tex);
    enc.setBinding(2,dirty ? *b.dirt[0] : *b.tex);
    enc.setBinding(3,dirty ? *b.dirt[dirt<1 ? 0 : 1] : *b.tex);
    enc.setBinding(4,pso.noWater);
    enc.setBinding(5,*envTex);
    enc.draw(vbo,b.first,b.count);
    }
  }
