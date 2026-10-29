#include "spots.h"

#include "shader.h"

using namespace Tempest;

SpotView::SpotView(Device& device) : device(device) {
  auto sh   = AppShader::get("marks.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("marks.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::Zero);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcColor);
  psoShadow = device.pipeline(Topology::Triangles,st,vert,frag);
  // the glow's blend is ours (white on black textures)
  st.setBlendSource(RenderState::BlendMode::One);
  st.setBlendDest  (RenderState::BlendMode::One);
  psoGlow   = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void SpotView::draw(Encoder<CommandBuffer>& enc, uint8_t frameId, const std::vector<Spot>& spots,
                    const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh& track) {
  verts.clear();
  batches.clear();
  for(auto& s:spots) {
    if(s.texture==nullptr)
      continue;
    tris.clear();
    rc::fx::layOnto(track,s.spot,tris);
    if(tris.empty())
      continue;
    if(batches.empty() || batches.back().texture!=s.texture || batches.back().glow!=s.glow)
      batches.push_back({s.texture,s.glow,verts.size(),0});
    for(auto& m:tris) {
      Vertex v = {};
      v.pos[0] = m.pos.x; v.pos[1] = m.pos.y; v.pos[2] = m.pos.z;
      v.uv[0]  = m.uv[0]; v.uv[1]  = m.uv[1];
      for(int c=0; c<4; ++c)
        v.color[c] = m.rgba[c];
      verts.push_back(v);
      }
    batches.back().count = verts.size()-batches.back().first;
    }
  if(verts.empty())
    return;
  auto& vbuf = vbo[frameId%4];
  vbuf = device.vbo(BufferHeap::Upload,verts);

  struct Push {
    float    viewProj[16];
    uint32_t mode;
    uint32_t fogColor;
    float    fogDensity;
    } push = {};
  viewProj.toColumnMajor(push.viewProj);
  push.mode       = 2;
  push.fogColor   = frame.fogColor;
  push.fogDensity = frame.cameraPos[3];
  const auto clamp = Sampler::bilinear(ClampMode::ClampToEdge);
  for(auto& b:batches) {
    enc.setPipeline(b.glow ? psoGlow : psoShadow);
    enc.setPushData(push);
    enc.setBinding(0,*b.texture,clamp);
    enc.setBinding(1,*b.texture,clamp);
    enc.draw(vbuf,b.first,b.count);
    }
  }
