#include "traffic.h"

#include <Tempest/Log>

#include "game/resources.h"

using namespace Tempest;

TrafficView::TrafficView(Device& device, Resources& res) : device(device), res(res) {
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

void TrafficView::draw(Encoder<CommandBuffer>& enc, const rc::Traffic& traffic, const rc::Mat4& viewProj,
                       const LevelMesh::Push& frame) {
  for(auto* c:traffic.cars()) {
    if(!c->visible)
      continue;
    if(auto* m = mesh(*c))
      m->draw(enc,viewProj,frame,c->world,&c->partPoses,c->lightsOn);
    }
  }
