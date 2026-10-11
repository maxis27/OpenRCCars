#include "levelmesh.h"

#include <Tempest/Log>

#include <algorithm>
#include <map>
#include <tuple>

#include "game/resources.h"
#include "shader.h"

using namespace Tempest;

namespace {

struct BatchKey {
  int32_t base = -1, light = -1, mask = -1, base2 = -1;
  bool operator<(const BatchKey& o) const {
    return std::tie(base,light,mask,base2) < std::tie(o.base,o.light,o.mask,o.base2);
    }
  };

}

bool LevelMesh::isHiddenMesh(const std::string& name) {
  // low detail copies, drawn by the original engine only at distance
  return name.find("_LOD1")!=std::string::npos || name.find("_LOD2")!=std::string::npos;
  }

LevelMesh::LevelMesh(Device& device, Resources& res, const rc::SbScene& scene) {
  createPipelines(device);

  bbox[0] = { 1e9f, 1e9f, 1e9f};
  bbox[1] = {-1e9f,-1e9f,-1e9f};

  // 1. bucket triangles by texture layers
  std::map<BatchKey,std::vector<Vertex>> buckets;
  for(auto& m:scene.meshes) {
    if(m.positions.empty())
      continue;
    if(isHiddenMesh(m.name)) {
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
      key.base  = base->texture;
      key.light = light ? light->texture : -1;
      if(mask && base2) {
        key.mask  = mask->texture;
        key.base2 = base2->texture;
        }
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
        bbox[0] = {std::min(bbox[0].x,p.x),std::min(bbox[0].y,p.y),std::min(bbox[0].z,p.z)};
        bbox[1] = {std::max(bbox[1].x,p.x),std::max(bbox[1].y,p.y),std::max(bbox[1].z,p.z)};
        return v;
        };
      // polygon -> triangle fan
      const Vertex v0 = vert(0);
      for(size_t i=1; i+1<n; ++i) {
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
    b.tex[0] = sceneTexture(res,scene,key.base,&b.mode);
    if(auto t = scene.texture(key.base); t!=nullptr && rc::PackFile::normalize(t->name).rfind("sky",0)==0) {
      b.sky   = true;
      b.flags = 0;
      }
    b.tex[1] = key.light>=0 ? sceneTexture(res,scene,key.light,nullptr) : nullptr;
    b.tex[2] = key.mask >=0 ? sceneTexture(res,scene,key.mask, nullptr) : nullptr;
    b.tex[3] = key.base2>=0 ? sceneTexture(res,scene,key.base2,nullptr) : nullptr;
    if(b.tex[1]!=nullptr)
      b.flags |= F_Lightmap;
    if(b.tex[2]!=nullptr && b.tex[3]!=nullptr) {
      b.flags |= F_Blend;
      b.mode   = rc::AlphaMode::Opaque; // base alpha is not used for transparency on blended terrain
      }
    if(b.mode==rc::AlphaMode::Cutout)
      b.flags |= F_Cutout;
    for(auto& t:b.tex)
      if(t==nullptr)
        t = &res.white();
    all.insert(all.end(),verts.begin(),verts.end());
    batches.push_back(b);
    }

  // opaque first, translucent last
  std::stable_sort(batches.begin(),batches.end(),[](const Batch& a, const Batch& b){
    return (a.mode==rc::AlphaMode::Translucent) < (b.mode==rc::AlphaMode::Translucent);
    });

  vbo            = device.vbo(all);
  info.triangles = all.size()/3;
  info.batches   = batches.size();
  Log::i("level: ",info.triangles," triangles, ",info.batches," batches, ",
         info.skippedMeshes," LOD meshes skipped, ",info.untextured," untextured faces");
  }

void LevelMesh::createPipelines(Device& device) {
  auto sh   = AppShader::get("level.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("level.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);

  // Faces of the original data are wound the other way than Tempest expects for front faces
  // (verified visually), so the "front" side is culled to drop back faces.
  RenderState opaque;
  opaque.setCullFaceMode(RenderState::CullMode::Front);
  opaque.setZTestMode(RenderState::ZTestMode::LEqual);
  psoOpaque = device.pipeline(Topology::Triangles,opaque,vert,frag);

  // alpha-tested foliage / fences are usually single quads seen from both sides
  RenderState cutout = opaque;
  cutout.setCullFaceMode(RenderState::CullMode::NoCull);
  psoCutout = device.pipeline(Topology::Triangles,cutout,vert,frag);

  RenderState blend = cutout;
  blend.setBlendSource(RenderState::BlendMode::SrcAlpha);
  blend.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  blend.setZWriteEnabled(false);
  psoBlend = device.pipeline(Topology::Triangles,blend,vert,frag);
  }

const Texture2d* LevelMesh::sceneTexture(Resources& res, const rc::SbScene& scene, int32_t index, rc::AlphaMode* mode) {
  if(mode!=nullptr)
    *mode = rc::AlphaMode::Opaque;
  auto t = scene.texture(index);
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

void LevelMesh::draw(Encoder<CommandBuffer>& enc, const Push& frame, bool drawSky) const {
  Push push = frame;
  const RenderPipeline* current = nullptr;
  for(auto& b:batches) {
    if(b.sky && !drawSky)
      continue;
    auto& pso = (b.mode==rc::AlphaMode::Translucent) ? psoBlend :
                (b.mode==rc::AlphaMode::Cutout)      ? psoCutout : psoOpaque;
    if(current!=&pso) {
      enc.setPipeline(pso);
      current = &pso;
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
