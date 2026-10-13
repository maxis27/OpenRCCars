#include "collisionview.h"

#include <cmath>

#include "shader.h"

using namespace Tempest;

CollisionView::CollisionView(Device& device, const rc::CollisionMesh& coll, const rc::SbScene& scene) {
  std::vector<Vertex> verts;
  verts.reserve(coll.triangles().size()*3);
  for(auto& t:coll.triangles()) {
    const auto& face = scene.meshes[t.mesh].faces[t.face];
    // normals follow the winding of the data, which is not consistent across meshes: judge by |y|
    const float up = std::abs(t.normal.y);
    float c[4];
    if(face.layer(rc::LayerTag::Base)==nullptr) {
      c[0] = 0.2f; c[1] = 0.45f; c[2] = 1.0f; c[3] = 0.45f;
      }
    else if(coll.isWater(t)) {   // not solid
      c[0] = 0.1f; c[1] = 0.95f; c[2] = 1.0f; c[3] = 0.40f;
      }
    else if(t.surface==rc::Surface::Water) { // waterfall: neither solid nor water
      c[0] = 0.8f; c[1] = 0.8f;  c[2] = 0.8f; c[3] = 0.30f;
      }
    else if(t.surface==rc::Surface::DeepSand && up>0.5f) {
      c[0] = 0.9f; c[1] = 0.3f;  c[2] = 0.9f; c[3] = 0.45f;
      }
    else if(up>0.82f) { // < ~35 deg
      c[0] = 0.2f; c[1] = 0.9f;  c[2] = 0.3f; c[3] = 0.30f;
      }
    else if(up>0.5f) {  // < 60 deg
      c[0] = 1.0f; c[1] = 0.85f; c[2] = 0.2f; c[3] = 0.35f;
      }
    else {
      c[0] = 1.0f; c[1] = 0.25f; c[2] = 0.2f; c[3] = 0.40f;
      }
    const rc::Vec3* p[3] = {&t.a,&t.b,&t.c};
    for(int i=0; i<3; ++i) {
      Vertex v = {};
      v.pos[0] = p[i]->x; v.pos[1] = p[i]->y; v.pos[2] = p[i]->z;
      // the sea is drawn lowered (WLOD offset): show it where the car meets it, not at the scene's height
      if(coll.isWater(t))
        v.pos[1] = coll.waterSurface(t.mesh,*p[i]);
      v.bary[i] = 1.f;
      for(int k=0; k<4; ++k)
        v.color[k] = c[k];
      verts.push_back(v);
      }
    }
  if(!verts.empty())
    vbo = device.vbo(verts);

  auto sh   = AppShader::get("collision.vert.sprv");
  auto vert = device.shader(sh.data,sh.len);
  sh        = AppShader::get("collision.frag.sprv");
  auto frag = device.shader(sh.data,sh.len);

  RenderState st;
  st.setCullFaceMode(RenderState::CullMode::NoCull);
  st.setZTestMode(RenderState::ZTestMode::LEqual); // same vertices as the level, so coplanar faces pass
  st.setZWriteEnabled(false);
  st.setBlendSource(RenderState::BlendMode::SrcAlpha);
  st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
  pso = device.pipeline(Topology::Triangles,st,vert,frag);
  }

void CollisionView::draw(Encoder<CommandBuffer>& enc, const LevelMesh::Push& frame) const {
  if(vbo.size()==0)
    return;
  enc.setPipeline(pso);
  enc.setPushData(frame);
  enc.draw(vbo);
  }
