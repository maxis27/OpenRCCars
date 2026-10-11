#pragma once

#include <memory>
#include <string>

#include "formats/scene.h"
#include "game/camera.h"
#include "levelmesh.h"

class Resources;

// One race track: RCCarsDB/<name>.sb, e.g. beach_1 (Surf), country_3 (Fishers) ...
class Level {
  public:
    Level(Tempest::Device& device, Resources& res, const std::string& name);

    const std::string&  name()  const { return levelName; }
    const rc::SbScene&  scene() const { return sb; }
    const LevelMesh&    mesh()  const { return *gpu; }

    // camera behind the first checkpoint, looking along the track
    Camera              startCamera() const;

  private:
    std::string                levelName;
    rc::SbScene                sb;
    std::unique_ptr<LevelMesh> gpu;
  };
