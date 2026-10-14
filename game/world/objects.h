#pragma once

#include <map>
#include <memory>
#include <vector>

#include "formats/objects.h"
#include "physics/props.h"
#include "modelmesh.h"

class Resources;

namespace rc { class PhysicsWorld; }

// Objects placed on one track (INST nodes). For now the props from stone.sb: cans, bottles, balls, buckets,
// cones ..., rigid bodies the car knocks around. Characters, animals and road traffic (people.sb, AIChars.sb,
// Roadcar.sb) move along paths, with animations and AI: rc::Characters and rc::Traffic, drawn by CharacterView and
// TrafficView.
class Objects {
  public:
    Objects(Tempest::Device& device, Resources& res, rc::ObjectLibrary& library, const rc::SbScene& track,
            rc::PhysicsWorld& world);
    ~Objects();

    void   draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const rc::Mat4& viewProj,
                const LevelMesh::Push& frame) const;
    size_t size() const { return items.size(); }

  private:
    struct Item {
      const ModelMesh*          mesh  = nullptr;
      float                     scale = 1;       // of the placement (the rigid body has none)
      std::unique_ptr<rc::Prop> prop;
      };

    std::map<const rc::ObjectLibrary::Model*,std::unique_ptr<ModelMesh>> meshes;
    std::vector<Item>                                                    items;
  };
