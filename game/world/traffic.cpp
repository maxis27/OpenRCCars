#include "traffic.h"

#include <Tempest/Log>

#include "fx/groundspots.h"
#include "game/resources.h"
#include "shader.h"

using namespace Tempest;

TrafficView::TrafficView(Device& device, Resources& res) : device(device), res(res) {
  auto find = [&res](const char* name) {
    auto t = res.texture(name);
    return t!=nullptr ? &t->tex : nullptr;
    };
  shadowTex = find("wheel_shd");
  glowTex   = find("shex_wht");

  auto sh   = AppShader::get("marks.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("marks.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);
  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual);
  st.setZWriteEnabled(false);
  // a shadow takes its colour off what is under it, a glow adds it (white on black textures; the blends are ours,
  // RCCars.exe's modes 4 and 5 of 0x45c410)
  st.setBlendSource(RenderState::BlendMode::Zero);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcColor);
  psoShadow = device.pipeline(Topology::Triangles,st,vert,frag);
  st.setBlendSource(RenderState::BlendMode::One);
  st.setBlendDest  (RenderState::BlendMode::One);
  psoGlow   = device.pipeline(Topology::Triangles,st,vert,frag);
  }

TrafficView::~TrafficView() = default;

const ModelMesh* TrafficView::mesh(const rc::Traffic::Car& car) {
  auto& m = meshes[{car.model,car.textures}];
  if(m==nullptr) {
    ModelMesh::Options opt;
    opt.parts       = car.parts;
    opt.textures    = car.textures;
    opt.lights      = car.lights;
    opt.hidden      = car.hidden;
    opt.lightAxis   = car.lightAxis;
    opt.modelledLit = car.modelledLit;
    try {
      m = std::make_unique<ModelMesh>(device,res,*car.model->scene,car.model->name,car.model->origin,opt);
      }
    catch(const std::exception& e) {
      Log::e("road car ",car.name,": ",e.what());
      return nullptr;
      }
    }
  return m.get();
  }

void TrafficView::draw(Encoder<CommandBuffer>& enc, uint8_t frameId, const rc::Traffic& traffic,
                       const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh* track) {
  for(auto* c:traffic.cars()) {
    if(!c->visible)
      continue;
    if(auto* m = mesh(*c))
      m->draw(enc,viewProj,frame,c->world,&c->partPoses,c->lightsOn);
    }
  if(track!=nullptr)
    drawSpots(enc,frameId,traffic,viewProj,frame,*track);
  }

void TrafficView::drawSpots(Encoder<CommandBuffer>& enc, uint8_t frameId, const rc::Traffic& traffic,
                            const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh& track) {
  for(auto& t:tris)
    t.clear();
  for(auto* c:traffic.cars())
    for(auto& s:c->spots) {
      rc::fx::GroundSpot g;
      g.pos    = s.pos;
      g.radius = s.radius;
      g.along  = s.along;
      rc::fx::layOnto(track,g,tris[s.kind==rc::Traffic::Spot::Glow ? 1 : 0]);
      }
  if(tris[0].empty() && tris[1].empty())
    return;

  verts.clear();
  for(auto& t:tris)
    for(auto& m:t) {
      Vertex v = {};
      v.pos[0] = m.pos.x; v.pos[1] = m.pos.y; v.pos[2] = m.pos.z;
      v.uv[0]  = m.uv[0]; v.uv[1]  = m.uv[1];
      for(int c=0; c<4; ++c)
        v.color[c] = m.rgba[c];
      verts.push_back(v);
      }
  auto& vbuf = vbo[frameId%2];
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
  const Texture2d* tex[2] = {shadowTex,glowTex};
  size_t first = 0;
  for(int i=0; i<2; ++i) {
    const size_t n = tris[i].size();
    if(n>0 && tex[i]!=nullptr) {
      enc.setPipeline(i==0 ? psoShadow : psoGlow);
      enc.setPushData(push);
      enc.setBinding(0,*tex[i],clamp);
      enc.setBinding(1,*tex[i],clamp);
      enc.draw(vbuf,first,n);
      }
    first += n;
    }
  }
