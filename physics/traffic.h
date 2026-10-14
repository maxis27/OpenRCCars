#pragma once

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "characters.h"
#include "formats/actorpath.h"
#include "formats/math3d.h"
#include "formats/objects.h"
#include "formats/scene.h"

namespace rc {

class PhysicsWorld;
class Vehicle;

// The road traffic of a track (RCCars.exe class RDCR, 0x516650): the trucks, trailers, Hummers and the BTR of
// RCCarsDB/Roadcar.sb, placed on the track by INST nodes that all stand at one spot out of sight (beach_2, beach_3:
// Hammer, Trailer1-2, Truck1-3; beach_4: Btr; urban_1: Hammer).
//
//  Settings\rdc_<track>.ini (0x516330; no file, no traffic): Freq/100 s between two cars, Disp/100 of that either
//   way at random, Accel/10 how fast the recorded path is played, Wheel the wheels' spin (degrees/s), Slow: the
//   quiet kind (urban_1's Hummer, beach_4's BTR: a softer push and the roadcar_slow sound).
//  Once the race runs (message 0x259) a car leaves every Freq +- Disp s (0x5160f0): the first parked one in INST
//   order, along Profiles\rdc_<track>_forw.gpf and _back.gpf in turn (the first of the two at random). It drives
//   the path (RoadPath) at Accel/10 times the recorded speed and parks again at its end (0x516db0); with no car
//   parked the next one leaves as soon as one is back.
//  Pose (0x516db0, 0x44c960 -> 0x408460): the instance's up, the path's direction as forward, the path's position;
//   then pitched by -atan(tilt/4.2) degrees about its right axis and let down by 3.15/sqrt(tilt^2+4.2^2) m. The
//   model's root node (rotated in Roadcar.sb) is replaced by this.
//  Wheels (0x516f50): they spin Wheel degrees/s while the car moves; the two front ones of the steering models
//   (Truck, Trailer, Hammer) turn by steer*150 degrees, at most 30. The BTR's eight wheels spin about their axle.
//   Cars further than 60 m from the camera are neither animated nor drawn.
//  Collision (0x516480, 0x534880 -> 0x5165a0): any car touching one is knocked off its recording (opponents) and,
//   unless being reset, pushed 6 m/s away from it and 1.5 m/s up (Slow: 2 and 0.5).
//  Head lights (0x517460, 0x5187d0): on inside the track's roadcar_* volumes (the tunnels), shown by moving the
//   texture coordinates of the fara*_body nodes by half the texture.
//  Sound (0x517530): Slow cars play roadcar_slow all the time; the others roadcar or roadcar_siren (even chance)
//   once a camera is within 20 m of them, not lower than 0.25 m below them, when nothing plays.
//  BTR (beach_4): its turret (Cylinder02_1) turns towards a car in front of it and fires bursts (message 0x261 to
//   BURST_MNG) at it, see Turret in traffic.cpp; roadcar.ini: TowerVel/100 degrees/s, TowerShootT.
class Traffic {
  public:
    using Reader = std::function<std::vector<uint8_t>(const std::string&)>;

    enum class Kind : uint8_t { Trailer, Truck, Hammer, Btr };

    // what the renderer and the sounds need of one car
    struct Car {
      std::string                 name;          // INST name
      const ObjectLibrary::Model* model = nullptr;
      Kind                        kind  = Kind::Truck;
      bool                        onRoad  = false; // driving its path; parked out of sight otherwise
      bool                        visible = false; // on the road and within 60 m of the camera: drawn
      Mat4                        world;           // model space (ObjectLibrary) -> world
    Mat4                        pose;            // the car on the road: +Z forward, +Y up
      // ModelMesh::Options: the moving nodes (wheels, the turret) and the pose in model space of the body (0) and
    // of each of them (1, 2 ..)
      std::vector<std::string>    parts;
      std::vector<Mat4>           partPoses;
      std::vector<std::string>    lights;          // head light nodes
    std::vector<std::string>    hidden;          // nodes not drawn, with everything below them
      int                         lightAxis   = 0;
      bool                        modelledLit = false;
      bool                        lightsOn    = false;
      // textures the model wears instead of its own ("body" and "box" in the INST script, 0x517cd0)
      std::vector<std::pair<std::string,std::string>> textures;
      // a sound of the Roadcar channel to start now (0 roadcar, 1 roadcar_siren), -1 none
      int32_t                     sound = -1;
      };

    // a burst the BTR fires this update (message 0x261): from its gun at a car, Characters::PlayerCar or an index
    // of the opponents (Characters::Car)
    struct Shot {
      Vec3 gun, target;
      int  car = Characters::PlayerCar;
      };

    // no traffic (size() 0) on tracks without Settings\rdc_<track>.ini
    Traffic(PhysicsWorld& world, ObjectLibrary& library, const SbScene& track, std::string_view trackName,
            Reader pack);
    ~Traffic();

    // after PhysicsWorld::step. `racing`: the race has started (no traffic before); `camera`: where the view is;
    // `player` may be null; the opponents touching a car get Characters::Car::hit
    void                       update(float dt, bool racing, Vehicle* player, const Vec3& camera,
                                      std::vector<Characters::Car>& cars);
    // every car parked, back to the start of the race
    void                       restart();

    const std::vector<Car*>&   cars()  const { return view; }
    size_t                     size()  const { return view.size(); }
    bool                       slow()  const { return prm.slow; }
    const std::vector<Shot>&   shots() const { return fired; }
    // cars sent off so far, and the time between the last two (for rcinfo)
    int                        departures() const { return sent; }
    float                      lastDelay()  const { return delay; }

  private:
    struct State;
    struct Settings {
      float freq   = 1.f;    // s
      float disp   = 0.1f;
      float accel  = 1.f;    // times the recorded speed
      float wheel  = 0.f;    // degrees/s
      bool  slow   = false;
      float towerVel   = 13.62f; // degrees/s
      float towerShoot = 1.f;    // s, TowerShootT/10 (read, not used by 0x518050)
      };

    void   setup(State& s, const SbInstance& in);
    void   depart();
    void   drive(State& s, float dt, const Vec3& camera, Vehicle* player, std::vector<Characters::Car>& cars);
    void   turret(State& s, float dt, Vehicle* player, const std::vector<Characters::Car>& cars);
    void   placeBody(State& s);
    bool   touches(const State& s, const Vec3& p, float margin) const;
    bool   playerTouches(const State& s, const Vehicle& player) const;
    float  random();

    PhysicsWorld&                       owner;
    ObjectLibrary&                      lib;
    const SbScene&                      track;
    Settings                            prm;
    RoadPath                            paths[2]; // forw, back
    std::vector<const SbVolume*>        lightZones;
    std::vector<std::unique_ptr<State>> states;
    std::vector<Car*>                   view;
    std::vector<Shot>                   fired;
    std::mt19937                        rng{20061014};
    float                               clock   = 0;  // s since the race started
    float                               waited  = 0;  // s since the last car left
    float                               delay   = 0;  // s until the next one
    bool                                forward = true;
    int                                 sent    = 0;
  };

}
