#include "objects.h"

#include <Tempest/Log>

#include "game/resources.h"
#include "physics/vehicle.h"

using namespace Tempest;

Objects::Objects(Device& device, Resources& res, rc::ObjectLibrary& library, const rc::SbScene& track,
                 rc::PhysicsWorld& world) {
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
    it.prop = std::make_unique<rc::Prop>(world,rc::propGeometry(*model.scene,model.id,model.origin),prm,at);
    items.push_back(std::move(it));
    }
  // the editor left most props a little above the ground: let them drop and come to rest before the race,
  // they then sleep until something touches them
  for(int i=0; i<int(2.f/rc::PhysicsWorld::FixedDt); ++i)
    world.step(rc::PhysicsWorld::FixedDt);
  Log::i("objects: ",items.size()," props, ",meshes.size()," models");
  }

Objects::~Objects() = default;

void Objects::update(float dt) {
  for(auto& it:items)
    it.prop->blow(dt);
  }

void Objects::afterStep(rc::PhysicsWorld& world) {
  for(auto& it:items)
    it.hit = false;
  const auto touched = rc::Prop::touched(world);
  for(auto& it:items)
    for(auto* p:touched)
      if(p==it.prop.get())
        it.hit = true;
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

void Objects::draw(Encoder<CommandBuffer>& enc, const rc::Mat4& viewProj, const LevelMesh::Push& frame,
                   bool shaded) const {
  for(auto& it:items) {
    it.mesh->draw(enc,viewProj,frame,it.prop->pose()*rc::Mat4::scale({it.scale,it.scale,it.scale}),nullptr,false,
                  shaded ? it.light.value() : -1.f);
    }
  }
