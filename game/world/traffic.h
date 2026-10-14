#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "physics/traffic.h"
#include "modelmesh.h"

class Resources;

// The road traffic of rc::Traffic on screen: one ModelMesh per model and livery, its wheels, the BTR's turret and
// the head lights posed by rc::Traffic.
class TrafficView {
  public:
    TrafficView(Tempest::Device& device, Resources& res);
    ~TrafficView();

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const rc::Traffic& traffic, const rc::Mat4& viewProj,
              const LevelMesh::Push& frame);

  private:
    using Key = std::pair<const rc::ObjectLibrary::Model*,std::vector<std::pair<std::string,std::string>>>;
    const ModelMesh* mesh(const rc::Traffic::Car& car);

    Tempest::Device&                            device;
    Resources&                                  res;
    std::map<Key,std::unique_ptr<ModelMesh>>    meshes;
  };
