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
    // height of the water surface over or under `at` (CollisionMesh::waterLevel) with the waves of `time`
    // seconds, where the picture has it
    std::optional<float> waterHeight(const rc::Vec3& at, float range, float time) const;

    // RCCars.exe 0x501180, the last step of the chase camera: an eye less than 7 cm above the water is put 14 cm
    // above it (the check looks 7 cm under the eye, 0x5013ce), so the camera never goes under water
    void                 keepAboveWater(Camera& camera, float time) const;

  private:
    std::string                levelName;
    rc::SbScene                sb;
    std::unique_ptr<LevelMesh> gpu;
    rc::CollisionMesh          coll;
    std::unique_ptr<CollisionView> collGpu;
  };
