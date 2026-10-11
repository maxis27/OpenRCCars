#pragma once

#include <memory>
#include <optional>
#include <string>

#include "formats/collision.h"
#include "formats/scene.h"
#include "game/camera.h"
#include "collisionview.h"
#include "levelmesh.h"

class Resources;

// One race track: RCCarsDB/<name>.sb, e.g. beach_1 (Surf), country_3 (Fishers) ...
class Level {
  public:
    Level(Tempest::Device& device, Resources& res, const std::string& name);

    const std::string&  name()  const { return levelName; }
    const rc::SbScene&  scene() const { return sb; }
    const LevelMesh&    mesh()  const { return *gpu; }
    // track collision from the COLL node (render faces the game collides with), and its debug overlay
    const rc::CollisionMesh& collision()     const { return coll; }
    const CollisionView&     collisionView() const { return *collGpu; }

    // camera behind the first checkpoint, looking along the track
    Camera              startCamera() const;

    // start pose of the player's car (INST 'Player', +Z along the track), lowered or raised so that
    // a model whose lowest point is at `carBottom` (model space Y) stands on the ground
    rc::Mat4            carStart(float carBottom) const;

    // highest collision surface directly below `from`
    std::optional<float> groundHeight(const rc::Vec3& from) const;

  private:
    std::string                levelName;
    rc::SbScene                sb;
    std::unique_ptr<LevelMesh> gpu;
    rc::CollisionMesh          coll;
    std::unique_ptr<CollisionView> collGpu;
  };
