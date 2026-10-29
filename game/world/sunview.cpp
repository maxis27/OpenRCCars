#include "sunview.h"

#include "game/resources.h"
#include "shader.h"

using namespace Tempest;

SunView::SunView(Device& device, Resources& res)
  : device(device) {
  const char* names[5] = {"sun_disc","lens_flare_1","lens_flare_2","lens_flare_3","shine"};
  for(int i=0; i<5; ++i) {
    auto t = res.texture(names[i]);
    tex[i] = t!=nullptr ? &t->tex : &res.white();
    }
  auto sh   = AppShader::get("sprite.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("sprite.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::SrcAlpha);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  blend = device.pipeline(Topology::Triangles,st,vert,frag);
  // the flare: SrcAlpha, One (0x45c6e0 mode 3), over everything
  st.setZTestMode(RenderState::ZTestMode::Always);
  st.setBlendDest(RenderState::BlendMode::One);
  add = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void SunView::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::fx::LensFlare& flare) {
  const auto& quads = flare.quads();
  if(quads.empty() || w<=0 || h<=0)
    return;
  verts.clear();
  for(auto& q:quads) {
    auto corner = [&](float dx, float dy, float u, float v) {
      Vertex r;
      r.pos[0] = q.x+dx;
      r.pos[1] = q.y+dy;
      r.uv[0]  = u;
      r.uv[1]  = v;
      for(int c=0; c<4; ++c)
        r.color[c] = q.rgba[c];
      r.depth = q.depth;
      return r;
      };
    const Vertex a = corner(-q.halfW,-q.halfH,0,0), b = corner(q.halfW,-q.halfH,1,0),
                 c = corner(q.halfW,q.halfH,1,1),   d = corner(-q.halfW,q.halfH,0,1);
    for(const Vertex* v:{&a,&b,&c,&a,&c,&d})
      verts.push_back(*v);
    }
  vbo[frame%4] = device.vbo(BufferHeap::Upload,verts);
  struct Push {
    float    screen[2];
    uint32_t fogColor;
    } push = {{float(w),float(h)},0};
  for(size_t i=0; i<quads.size(); ++i) {
    enc.setPipeline(quads[i].additive ? add : blend);
    enc.setPushData(push);
    enc.setBinding(0,*tex[quads[i].texture],Sampler::bilinear(ClampMode::ClampToEdge));
    enc.draw(vbo[frame%4],i*6,6);
    }
  }
