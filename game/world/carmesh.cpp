#include "carmesh.h"

#include <Tempest/Log>

#include <algorithm>
#include <map>
#include <stdexcept>

#include "game/resources.h"
#include "physics/cargeometry.h"

using namespace Tempest;

CarMesh::CarMesh(Device& device, Resources& res, const rc::SbScene& sb, std::string_view model)
  :modelName(model), pso(device) {
  const int32_t modelId = sb.findModel(model);
  if(modelId<0)
    throw std::runtime_error("car model \""+std::string(model)+"\" not found");

  bbox[0] = { 1e9f, 1e9f, 1e9f};
  bbox[1] = {-1e9f,-1e9f,-1e9f};
  partList.push_back(Part{});

  // part of each mesh: body (0) or the WHEEL_* node above it; -1 = hidden
  auto partOf = [&](size_t mesh) -> int32_t {
    auto name = rc::carPart(sb,mesh);
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

  // 1. model space triangles, bucketed by part and texture
  std::map<std::pair<size_t,int32_t>,std::vector<LevelMesh::Vertex>> buckets;
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=modelId || m.positions.empty())
      continue;
    const int32_t part = partOf(mi);
    if(part<0)
      continue;
    // mirrored nodes (scale -1) flip the winding
    const bool mirrored = m.world.determinant3()<0;
    for(auto& f:m.faces) {
      const size_t n    = f.pos.size();
      auto         base = f.layer(rc::LayerTag::Base);
      if(n<3 || base==nullptr)
        continue;
      auto& out  = buckets[{size_t(part),base->texture}];
      auto  vert = [&](size_t corner) {
        LevelMesh::Vertex v = {};
        const uint32_t pi = f.pos[corner];
        if(pi>=m.positions.size())
          return v;
        const rc::Vec3 p  = m.world.point(m.positions[pi]);
        const rc::Vec3 nr = corner<f.normals.size() ? m.world.vector(f.normals[corner]).normalized() : rc::Vec3{0,1,0};
        v.pos[0]    = p.x;  v.pos[1]    = p.y;  v.pos[2]    = p.z;
        v.normal[0] = nr.x; v.normal[1] = nr.y; v.normal[2] = nr.z;
        if(corner<base->uv.size() && base->uv[corner]<m.uvs.size()) {
          v.uvBase[0] = m.uvs[base->uv[corner]].x;
          v.uvBase[1] = m.uvs[base->uv[corner]].y;
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

  // 2. wheel pivots: centre of the wheel's bounding box
  for(size_t p=1; p<partList.size(); ++p) {
    rc::Vec3 mn = { 1e9f, 1e9f, 1e9f}, mx = {-1e9f,-1e9f,-1e9f};
    for(auto& [key,verts]:buckets) {
      if(key.first!=p)
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
    const rc::Vec3 pivot = partList[key.first].pivot;
    Batch b;
    b.part  = key.first;
    b.first = all.size();
    b.count = verts.size();
    if(auto t = sb.texture(key.second,modelId)) {
      if(auto r = res.texture(t->name)) {
        b.tex  = &r->tex;
        b.mode = r->alpha;
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

  vbo = device.vbo(all);
  Log::i("car \"",modelName,"\": ",all.size()/3," triangles, ",partList.size()-1," wheels, size ",
         bbox[1].x-bbox[0].x," x ",bbox[1].y-bbox[0].y," x ",bbox[1].z-bbox[0].z);
  }

void CarMesh::draw(Encoder<CommandBuffer>& enc, const rc::Mat4& viewProj,
                   const LevelMesh::Push& frame, const rc::Mat4& world,
                   const std::vector<rc::Mat4>* partPoses) const {
  const rc::Vec3 camera = {frame.cameraPos[0],frame.cameraPos[1],frame.cameraPos[2]};
  const rc::Vec3 sun    = {frame.sunDir[0],   frame.sunDir[1],   frame.sunDir[2]};

  // the shader works in the space of the vertices, so camera and sun go into part space
  std::vector<LevelMesh::Push> push(partList.size(),frame);
  for(size_t i=0; i<partList.size(); ++i) {
    const bool     own = partPoses!=nullptr && i<partPoses->size();
    const rc::Mat4 m   = world*(own ? (*partPoses)[i] : rc::Mat4::translate(partList[i].pivot));
    const rc::Mat4 inv = m.inverseRigid();
    (viewProj*m).toColumnMajor(push[i].viewProj);
    const rc::Vec3 c = inv.point(camera);
    const rc::Vec3 s = inv.vector(sun);
    push[i].cameraPos[0] = c.x; push[i].cameraPos[1] = c.y; push[i].cameraPos[2] = c.z;
    push[i].sunDir[0]    = s.x; push[i].sunDir[1]    = s.y; push[i].sunDir[2]    = s.z;
    }

  const RenderPipeline* current = nullptr;
  for(auto& b:batches) {
    auto& p = pso.get(b.mode);
    if(current!=&p) {
      enc.setPipeline(p);
      current = &p;
      }
    LevelMesh::Push& pp = push[b.part];
    pp.flags = (b.mode==rc::AlphaMode::Cutout) ? uint32_t(LevelMesh::F_Cutout) : 0u;
    enc.setPushData(pp);
    enc.setBinding(0,*b.tex);
    enc.setBinding(1,*b.tex);
    enc.setBinding(2,*b.tex);
    enc.setBinding(3,*b.tex);
    enc.draw(vbo,b.first,b.count);
    }
  }
