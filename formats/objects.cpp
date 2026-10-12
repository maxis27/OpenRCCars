#include "objects.h"

#include <cctype>

using namespace rc;

namespace {

bool equalNoCase(std::string_view a, std::string_view b) {
  if(a.size()!=b.size())
    return false;
  for(size_t i=0; i<a.size(); ++i)
    if(std::tolower(uint8_t(a[i]))!=std::tolower(uint8_t(b[i])))
      return false;
  return true;
  }

}

ObjectLibrary::ObjectLibrary(Reader r) : read(std::move(r)) {
  files.push_back(File{"RCCarsDB/stone.sb",   Kind::Prop,      nullptr});
  files.push_back(File{"RCCarsDB/Roadcar.sb", Kind::Vehicle,   nullptr});
  files.push_back(File{"RCCarsDB/people.sb",  Kind::Character, nullptr});
  files.push_back(File{"RCCarsDB/AIChars.sb", Kind::Character, nullptr});
  }

ObjectLibrary::~ObjectLibrary() = default;

const SbScene* ObjectLibrary::scene(File& f) {
  if(!f.tried) {
    f.tried = true;
    f.scene = std::make_unique<SbScene>(SbScene::load(read(f.path)));
    }
  return f.scene.get();
  }

const ObjectLibrary::Model* ObjectLibrary::find(const SbInstance& in) {
  if(in.model.empty() || in.name=="Player")
    return nullptr;
  const std::string key = in.source+"/"+in.model;
  if(auto it = models.find(key); it!=models.end())
    return it->second.get();

  std::unique_ptr<Model> found;
  for(auto& f:files) {
    // "RoadCar" names Roadcar.sb; without a source every library is searched
    if(!in.source.empty() && !equalNoCase(std::string("RCCarsDB/")+in.source+".sb",f.path))
      continue;
    const SbScene* sb = scene(f);
    const int32_t  id = sb->findModel(in.model);
    if(id<0)
      continue;
    found        = std::make_unique<Model>();
    found->scene = sb;
    found->id    = id;
    found->name  = in.model;
    found->kind  = f.kind;
    for(auto& m:sb->meshes)
      if(m.model==id && m.parent<0) {
        found->origin = m.world.translation();
        break;
        }
    break;
    }
  auto& slot = models[key];
  slot = std::move(found);
  return slot.get();
  }

std::vector<ObjectLibrary::Placed> ObjectLibrary::place(const SbScene& track) {
  std::vector<Placed> ret;
  for(auto& in:track.instances)
    if(auto m = find(in))
      ret.push_back(Placed{&in,m});
  return ret;
  }

const char* ObjectLibrary::kindName(Kind k) {
  switch(k) {
    case Kind::Prop:      return "prop";
    case Kind::Character: return "character";
    case Kind::Vehicle:   return "vehicle";
    }
  return "?";
  }
