#include "levelmesh.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>

#include "game/resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

struct BatchKey {
  int32_t model = -1;
  int32_t base = -1, light = -1, mask = -1, base2 = -1;
  bool operator<(const BatchKey& o) const {
    return std::tie(model,base,light,mask,base2) < std::tie(o.model,o.base,o.light,o.mask,o.base2);
    }
  };

}

std::vector<bool> LevelMesh::hiddenMeshes(const rc::SbScene& scene) {
  // "_LOD<n>" copies of the same thing are siblings under one parent. Objects keep their full detail mesh
  // without a suffix next to simplified _LOD1/_LOD2 copies; the sea is made only of LODs, where _LOD1 is the
  // coarsest and the highest number the finest, and groups far from the track have only _LOD1/_LOD2. The
  // original picks one per distance (WaterLOD); we draw the finest one of each group.
  auto lodOf = [](const std::string& name) {
    const size_t at = name.find("_LOD");
    return (at!=std::string::npos && at+4<name.size() && name[at+4]>='1' && name[at+4]<='9') ? name[at+4]-'0' : 0;
    };
  std::map<int32_t,int> finest;
  std::map<int32_t,bool> plain;
  for(auto& m:scene.meshes) {
    if(m.faces.empty())
      continue;
    if(const int l = lodOf(m.name))
      finest[m.parent] = std::max(finest[m.parent],l);
    else
      plain[m.parent] = true;
    }
  std::vector<bool> hidden(scene.meshes.size(),false);
  for(size_t i=0; i<scene.meshes.size(); ++i) {
    auto&     m = scene.meshes[i];
    const int l = lodOf(m.name);
    hidden[i] = l>0 && (plain[m.parent] || l<finest[m.parent]);
    }
  return hidden;
  }

LevelMesh::LevelMesh(Device& device, Resources& res, const rc::SbScene& scene, const rc::CollisionMesh* ground)
  :pso(device) {
  bbox[0] = { 1e9f, 1e9f, 1e9f};
  bbox[1] = {-1e9f,-1e9f,-1e9f};

  // 1. bucket triangles by texture layers
  std::map<BatchKey,std::vector<Vertex>> buckets;
  const auto hidden = hiddenMeshes(scene);
  for(size_t mi=0; mi<scene.meshes.size(); ++mi) {
    auto& m = scene.meshes[mi];
    if(m.positions.empty())
      continue;
    if(hidden[mi]) {
      info.skippedMeshes++;
      continue;
      }
    for(auto& f:m.faces) {
      const size_t n = f.pos.size();
      if(n<3)
        continue;
      auto base  = f.layer(rc::LayerTag::Base);
      auto light = f.layer(rc::LayerTag::Lightmap);
      auto mask  = f.layer(rc::LayerTag::Alpha);
      auto base2 = f.layer(rc::LayerTag::Base2);
      if(base==nullptr) {
        info.untextured++;  // helper geometry without material
        continue;
        }
      BatchKey key;
      key.model = m.model;
      key.base  = base->texture;
      key.light = light ? light->texture : -1;
      if(mask) {
        key.mask  = mask->texture;
        key.base2 = base2 ? base2->texture : -1;
        }
      // water (texture "sea") carries its depth above the solid ground in uvMask.x, see F_Water
      const bool water = ground!=nullptr && mask==nullptr &&
                         rc::surfaceOf(scene.texture(base->texture,m.model))==rc::Surface::Water;
      if(water)
        key.mask = -2; // own batch
      // lowers `p` to the water surface at the shore (CollisionMesh::shoreHeight) and returns its depth
      auto shapeWater = [&](float* p) {
        const rc::Vec3 at    = {p[0],p[1],p[2]};
        const auto     floor = ground->groundHeight(at+rc::Vec3{0,0.05f,0},50.f); // not from higher up: rocks overhang the sea
        p[1] = ground->shoreHeight(at);
        return floor ? p[1]-*floor : 100.f;
        };
      // the depth is interpolated per vertex: split big sea triangles near the shore so that it follows the seabed
      std::function<void(std::vector<Vertex>&,const Vertex&,const Vertex&,const Vertex&,int)> subdivideWater;
      subdivideWater = [&](std::vector<Vertex>& dst, const Vertex& a, const Vertex& b, const Vertex& c, int level) {
        auto len = [](const Vertex& x, const Vertex& y) { return std::hypot(x.pos[0]-y.pos[0],x.pos[2]-y.pos[2]); };
        const float shallowest = std::min({a.uvMask[0],b.uvMask[0],c.uvMask[0]});
        if(level==0 || shallowest>1.f || std::max({len(a,b),len(b,c),len(c,a)})<0.3f) {
          dst.push_back(a); dst.push_back(b); dst.push_back(c);
          return;
          }
        auto mid = [&](const Vertex& x, const Vertex& y) {
          Vertex v;
          auto lerp = [](const float* s, const float* t, float* o, int k) { for(int i=0;i<k;++i) o[i] = (s[i]+t[i])*0.5f; };
          lerp(x.pos,y.pos,v.pos,3);       lerp(x.normal,y.normal,v.normal,3);
          lerp(x.uvBase,y.uvBase,v.uvBase,2); lerp(x.uvLight,y.uvLight,v.uvLight,2);
          lerp(x.uvBase2,y.uvBase2,v.uvBase2,2);
          v.uvMask[1] = (x.uvMask[1]+y.uvMask[1])*0.5f; // the scene's height, before shapeWater
          v.pos[1]    = v.uvMask[1];
          v.uvMask[0] = shapeWater(v.pos);
          return v;
          };
        const Vertex ab = mid(a,b), bc = mid(b,c), ca = mid(c,a);
        subdivideWater(dst,a,ab,ca,level-1);
        subdivideWater(dst,ab,b,bc,level-1);
        subdivideWater(dst,ca,bc,c,level-1);
        subdivideWater(dst,ab,bc,ca,level-1);
        };
      auto& out = buckets[key];

      auto uv = [&](const rc::SbFace::Layer* l, size_t corner, float* dst) {
        dst[0] = dst[1] = 0;
        if(l==nullptr || corner>=l->uv.size() || l->uv[corner]>=m.uvs.size())
          return;
        dst[0] = m.uvs[l->uv[corner]].x;
        dst[1] = m.uvs[l->uv[corner]].y;
        };
      auto vert = [&](size_t corner) {
        Vertex v = {};
        const uint32_t pi = f.pos[corner];
        if(pi>=m.positions.size())
          return v;
        const rc::Vec3 p = m.world.point(m.positions[pi]);
        const rc::Vec3 nr = corner<f.normals.size() ? m.world.vector(f.normals[corner]).normalized() : rc::Vec3{0,1,0};
        v.pos[0] = p.x;  v.pos[1] = p.y;  v.pos[2] = p.z;
        v.normal[0] = nr.x; v.normal[1] = nr.y; v.normal[2] = nr.z;
        uv(base, corner,v.uvBase);
        uv(light,corner,v.uvLight);
        uv(mask, corner,v.uvMask);
        uv(base2,corner,v.uvBase2);
        if(water) {
          v.uvMask[1] = v.pos[1];
          v.uvMask[0] = shapeWater(v.pos);
          }
        if(mask!=nullptr && base2==nullptr)
          v.pos[1] += 0.01f; // shore foam lies on the sand, lift it to keep it from z-fighting
        bbox[0] = {std::min(bbox[0].x,p.x),std::min(bbox[0].y,p.y),std::min(bbox[0].z,p.z)};
        bbox[1] = {std::max(bbox[1].x,p.x),std::max(bbox[1].y,p.y),std::max(bbox[1].z,p.z)};
        return v;
        };
      // polygon -> triangle fan
      const Vertex v0 = vert(0);
      for(size_t i=1; i+1<n; ++i) {
        if(water) {
          subdivideWater(out,v0,vert(i),vert(i+1),3);
          continue;
          }
        out.push_back(v0);
        out.push_back(vert(i));
        out.push_back(vert(i+1));
        }
      }
    }

  // 2. resolve textures, build one vertex buffer
  std::vector<Vertex> all;
  for(auto& [key,verts]:buckets) {
    Batch b;
    b.first  = all.size();
    b.count  = verts.size();
    b.tex[0] = sceneTexture(res,scene,key.base,key.model,&b.mode);
    if(auto t = scene.texture(key.base,key.model); t!=nullptr && rc::PackFile::normalize(t->name).rfind("sky",0)==0) {
      b.sky   = true;
      b.flags = 0;
      }
    b.tex[1] = key.light>=0 ? sceneTexture(res,scene,key.light,key.model,nullptr) : nullptr;
    b.tex[2] = key.mask >=0 ? sceneTexture(res,scene,key.mask, key.model,nullptr) : nullptr;
    if(key.mask==-2) {
      b.flags |= F_Water;
      b.mode   = rc::AlphaMode::Translucent;
      }
    b.tex[3] = key.base2>=0 ? sceneTexture(res,scene,key.base2,key.model,nullptr) : nullptr;
    if(b.tex[1]!=nullptr)
      b.flags |= F_Lightmap;
    if(b.tex[2]!=nullptr && b.tex[3]!=nullptr) {
      b.flags |= F_Blend;
      b.mode   = rc::AlphaMode::Opaque; // base alpha is not used for transparency on blended terrain
      }
    else if(b.tex[2]!=nullptr) {
      // a mask without a second texture fades the base out: the foam along the shore (coast_base / coast)
      b.flags |= F_Mask;
      b.mode   = rc::AlphaMode::Translucent;
      }
    if(b.mode==rc::AlphaMode::Cutout)
      b.flags |= F_Cutout;
    for(auto& t:b.tex)
      if(t==nullptr)
        t = &res.white();
    all.insert(all.end(),verts.begin(),verts.end());
    batches.push_back(b);
    }

  // opaque first, then the water, then the rest of the translucent batches: the foam is surf on top of the water
  auto order = [](const Batch& b) { return b.mode!=rc::AlphaMode::Translucent ? 0 : (b.flags & F_Water) ? 1 : 2; };
  std::stable_sort(batches.begin(),batches.end(),[&](const Batch& a, const Batch& b){
    return order(a) < order(b);
    });

  vbo            = device.vbo(all);
  info.triangles = all.size()/3;
  info.batches   = batches.size();
  Log::i("level: ",info.triangles," triangles, ",info.batches," batches, ",
         info.skippedMeshes," LOD meshes skipped, ",info.untextured," untextured faces");
  }

ScenePipelines::ScenePipelines(Device& device) {
  auto sh   = AppShader::get("level.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("level.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);

  // Faces of the original data are wound the other way than Tempest expects for front faces
  // (verified visually), so the "front" side is culled to drop back faces.
  RenderState stOpaque;
  stOpaque.setCullFaceMode(RenderState::CullMode::Front);
  stOpaque.setZTestMode(RenderState::ZTestMode::LEqual);
  opaque = device.pipeline(Topology::Triangles,stOpaque,vert,frag);

  // alpha-tested foliage / fences are usually single quads seen from both sides
  RenderState stCutout = stOpaque;
  stCutout.setCullFaceMode(RenderState::CullMode::NoCull);
  cutout = device.pipeline(Topology::Triangles,stCutout,vert,frag);

  RenderState stBlend = stCutout;
  stBlend.setBlendSource(RenderState::BlendMode::SrcAlpha);
  stBlend.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  stBlend.setZWriteEnabled(false);
  blend = device.pipeline(Topology::Triangles,stBlend,vert,frag);
  }

const RenderPipeline& ScenePipelines::get(rc::AlphaMode mode) const {
  return (mode==rc::AlphaMode::Translucent) ? blend :
         (mode==rc::AlphaMode::Cutout)      ? cutout : opaque;
  }

const Texture2d* LevelMesh::sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, int32_t model, rc::AlphaMode* mode) {
  if(mode!=nullptr)
    *mode = rc::AlphaMode::Opaque;
  auto t = scene.texture(index,model);
  if(t==nullptr)
    return nullptr;

  if(!t->embedded.empty()) {
    for(auto& [idx,slot]:embeddedIndex)
      if(idx==index)
        return &embedded[slot];
    try {
      auto tex = res.decode(t->embedded);
      embedded.push_back(std::move(tex.tex));
      embeddedIndex.emplace_back(index,embedded.size()-1);
      return &embedded.back();
      }
    catch(const std::exception& e) {
      Log::e("embedded texture \"",t->name,"\": ",e.what());
      return nullptr;
      }
    }

  auto r = res.texture(t->name);
  if(r==nullptr)
    return nullptr;
  if(mode!=nullptr)
    *mode = r->alpha;
  return &r->tex;
  }

void LevelMesh::draw(Encoder<CommandBuffer>& enc, const Push& frame, Pass pass, bool drawSky) const {
  Push push = frame;
  const RenderPipeline* current = nullptr;
  for(auto& b:batches) {
    if(b.sky && !drawSky)
      continue;
    if((b.mode==rc::AlphaMode::Translucent)!=(pass==Pass::Translucent))
      continue;
    auto& p = pso.get(b.mode);
    if(current!=&p) {
      enc.setPipeline(p);
      current = &p;
      }
    push.flags = b.flags;
    enc.setPushData(push);
    enc.setBinding(0,*b.tex[0]);
    enc.setBinding(1,*b.tex[1]);
    enc.setBinding(2,*b.tex[2]);
    enc.setBinding(3,*b.tex[3]);
    enc.draw(vbo,b.first,b.count);
    }
  }
