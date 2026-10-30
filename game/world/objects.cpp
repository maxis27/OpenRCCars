#include "objects.h"

#include <Tempest/Log>

#include "game/resources.h"
#include "physics/vehicle.h"

using namespace Tempest;

Objects::Objects(Device& device, Resources& res, rc::ObjectLibrary& library, const rc::SbScene& track,
                 rc::PhysicsWorld& world) {
  // the spheres of the shapes (Settings\*.ini, 0x4f7760); each prop is placed resting on the track, asleep
  // (0x50cea0): no settling before the race
  const rc::PropShapes shapes = rc::PropShapes::load(res.pack());
  for(auto& p:library.place(track)) {
    auto& model = *p.model;
    // characters, animals and road traffic move in the original (animations, AI, paths): not yet
    if(model.kind!=rc::ObjectLibrary::Kind::Prop)
      continue;
    auto& mesh  = meshes[&model];
    if(mesh==nullptr)
      mesh = std::make_unique<ModelMesh>(device,res,*model.scene,model.name,model.origin);

    Item it;
    const rc::Mat4& at = p.instance->world;
    it.mesh   = mesh.get();
    it.model  = &model.name;
    it.scale  = rc::Vec3{at.m[0][0],at.m[1][0],at.m[2][0]}.length();
    const rc::SbScript*  s   = model.scene->script(model.id);
    const rc::PropParams prm = rc::PropParams::parse(s ? s->text : std::string());
    it.prop = std::make_unique<rc::Prop>(world,shapes,rc::propGeometry(*model.scene,model.id,model.origin),prm,at);
    items.push_back(std::move(it));
    }
  Log::i("objects: ",items.size()," props, ",meshes.size()," models");
  }

Objects::~Objects() = default;

void Objects::afterStep() {
  for(auto& it:items)
    it.hit = it.prop->takeKnock();
  }

std::vector<Objects::Sound> Objects::sounds() const {
  std::vector<Sound> ret;
  ret.reserve(items.size());
  for(auto& it:items)
    ret.push_back({it.model,it.prop->pose().translation(),it.hit});
  return ret;
  }

void Objects::shade(float dt, const rc::Spline& curve, rc::fx::LightmapProbe& probe) {
  for(auto& it:items) {
    const rc::Vec3 at = it.prop->pose().translation();
    it.light.update(curve,rc::fx::FeatureShade::Prop,dt,[&]{ return probe.ground(at+rc::Vec3{0,0.1f,0},2.f); });
    }
  }

void Objects::draw(Encoder<CommandBuffer>& enc, const rc::Mat4& viewProj, const LevelMesh::Push& frame) const {
  for(auto& it:items) {
    it.mesh->draw(enc,viewProj,frame,it.prop->pose()*rc::Mat4::scale({it.scale,it.scale,it.scale}),nullptr,false,
                  it.light.value());
    }
  }
