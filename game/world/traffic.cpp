#include "traffic.h"

#include <Tempest/Log>

#include "game/resources.h"

using namespace Tempest;

TrafficView::TrafficView(Device& device, Resources& res) : device(device), res(res), spotView(device) {
  auto find = [&res](const char* name) {
    auto t = res.texture(name);
    return t!=nullptr ? &t->tex : nullptr;
    };
  shadowTex = find("wheel_shd");
  glowTex   = find("shex_wht");
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

void TrafficView::preload(const rc::Traffic& traffic) {
  for(auto* c:traffic.cars())
    mesh(*c);
  }

void TrafficView::draw(Encoder<CommandBuffer>& enc, uint8_t frameId, const rc::Traffic& traffic,
                       const rc::Mat4& viewProj, const LevelMesh::Push& frame, const rc::CollisionMesh* track) {
  for(auto* c:traffic.cars()) {
    if(!c->visible)
      continue;
    if(auto* m = mesh(*c))
      m->draw(enc,viewProj,frame,c->world,&c->partPoses,c->lightsOn);
    }
  if(track==nullptr)
    return;
  // the wheels' shadows, then the glow of the head lights
  spots.clear();
  for(auto kind:{rc::Traffic::Spot::Shadow,rc::Traffic::Spot::Glow})
    for(auto* c:traffic.cars())
      for(auto& s:c->spots) {
        if(s.kind!=kind)
          continue;
        SpotView::Spot g;
        g.spot.pos    = s.pos;
        g.spot.radius = s.radius;
        g.spot.along  = s.along;
        g.glow        = kind==rc::Traffic::Spot::Glow;
        g.texture     = g.glow ? glowTex : shadowTex;
        spots.push_back(g);
        }
  spotView.draw(enc,frameId,spots,viewProj,frame,*track);
  }
