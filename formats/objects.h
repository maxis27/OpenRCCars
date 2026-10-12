#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "scene.h"

namespace rc {

// Objects placed on a track by INST nodes come from other scenes, by model name (SbInstance::model):
//   RCCarsDB/stone.sb    props the cars knock around ("Stones" folders): cans, bottles, balls, buckets, cones ...
//   RCCarsDB/people.sb   Man, Woman, Guard, RepairMan
//   RCCarsDB/AIChars.sb  animals: Seagull, Crab, Spider, Dog, Vulture
//   RCCarsDB/Roadcar.sb  big cars: Truck, Trailer, Btr, Hammer (the instance names the scene: source "RoadCar")
//   RCCarsDB/Car.sb      the player's car ('Player'), handled separately
// The scenes are loaded on first use. The instance places the model's root node: in stone.sb the models stand
// side by side (the cone's root node is at 3.88, 0.10, -3.84, a ball's 0.12 above the ground at its centre) and
// the game ignores where; with the root's position as the origin the shiftCenterMassOY of each prop's SCRI
// lands where it should (0 for balls and cans = their centre, 0.03 for the cone = near its heavy base).
class ObjectLibrary {
  public:
    enum class Kind : uint8_t {
      Prop,       // stone.sb: rigid body with the physics from its SCRI (PropParams)
      Character,  // people.sb, AIChars.sb: animated actors with AI (shown standing still for now)
      Vehicle,    // Roadcar.sb
      };

    struct Model {
      const SbScene* scene = nullptr;
      int32_t        id    = -1;    // SbScene::models
      std::string    name;
      Kind           kind  = Kind::Prop;
      Vec3           origin;        // position of the model's root node in its scene: model space origin
      };

    struct Placed {
      const SbInstance* instance = nullptr;
      const Model*      model    = nullptr;
      };

    using Reader = std::function<std::vector<uint8_t>(const std::string& path)>;

    explicit ObjectLibrary(Reader read);
    ~ObjectLibrary();

    // nullptr for the player's car and models that are nowhere to be found
    const Model*        find(const SbInstance& in);
    // every instance of `track` with a model (the player's car aside), in scene order
    std::vector<Placed> place(const SbScene& track);

    static const char*  kindName(Kind k);

  private:
    struct File {
      const char*              path;
      Kind                     kind;
      std::unique_ptr<SbScene> scene;
      bool                     tried = false;
      };

    const SbScene*      scene(File& f);

    Reader                                     read;
    std::vector<File>                          files;
    std::map<std::string,std::unique_ptr<Model>,std::less<>> models;
  };

}
