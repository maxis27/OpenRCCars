#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "formats/actorpath.h"
#include "formats/math3d.h"
#include "formats/objects.h"
#include "formats/scene.h"
#include "formats/skeleton.h"
#include "balls.h"


namespace rc {

class PhysicsWorld;
class Vehicle;

// The people and animals of a track (INST nodes of people.sb and AIChars.sb models) as RCCars.exe runs them. Each
// kind is a game class of its own there (gmpRegisterClass): PEOP 0x50fda0 (Man, Woman, RepairMan), GUAM 0x51a3d0
// (Guard), GULL 0x512360 (Seagull), CRAB 0x513880, SPDR 0x514d40 (Spider), DOG$ 0x5188f0, VULT 0x51af80 (Vulture).
// Their tuning comes from Settings\People.ini, seagull.ini, spider.ini, dog.ini, guard.ini and vulture.ini.
//
//  People walk round a recorded path (Profiles\<name>.dat, PeoplePath) and are there only while the player's car is
//   within 50 m (0x5107a0, the "high" render quality distance). A car that comes within 0.8 m of their feet gets
//   kicked (Pendal) or, one time in five and with 2.2 m free above, picked up and thrown (Brosok); so does a car
//   driving at them faster than 15 km/h from up to 4 m ahead (0x511690). A car hitting them faster than 15 km/h
//   hurts them (Hurt, their voice) and is "Wielkie Łup!" for the player (0x534340 -> 0x510b30).
//  Guards stand still and turn to face the player while the car is in their visibilityVolume, shoot while it is in
//   their guardVolume and kick like people; hitting one faster than 10 km/h is "Wielkie Łup!" (0x5344f0).
//  Seagulls appear where the player can see them (visibilityVolume) at a random spot of their inhabitVolume, look
//   around, walk to another spot, and take off towards their flightDir marker when a car, a person or a dog comes
//   within 4 m (0x512b30).
//  Crabs come out between their landhole1 and seahole1 markers when the player is near, face the car, then run
//   sideways into the sea hole (0x514100); a car running one over squashes it (0x534670).
//  Spiders run from one of their holes (hole1, hole2 ..) to another, flee from cars within 1.8 m and die under
//   the wheels (0x515570, 0x534770).
//  Dogs chase the player's car while it is in their attackVolume, to a point in front of it, and bark (0x519400);
//   hitting one faster than 15 km/h is "Wielkie Łup!" and makes it whine (0x5349d0).
//  Vultures circle high over country_2 along recorded camera paths (Profiles\vlt_country_2_<1|2>.gpf, 0x51b450).
class Characters {
  public:
    using Reader = std::function<std::vector<uint8_t>(const std::string&)>;

    enum class Kind : uint8_t { Person, Guard, Seagull, Crab, Spider, Dog, Vulture };

    // what the renderer and the sounds need of one character
    struct Actor {
      Kind                      kind = Kind::Person;
      std::string               name;           // INST name
      const ObjectLibrary::Model* model = nullptr;
      const Skeleton*           skeleton = nullptr;
      bool                      visible = false;
      Mat4                      world;          // model -> world, with the placement's scale
      int32_t                   seq  = -1;      // Skeleton::sequences() index, -1: the file's pose
      float                     time = 0;       // s into the sequence
      // textures the model wears instead of its own ("clother" in the INST script)
      std::vector<std::pair<std::string,std::string>> textures;
      // what the character asks of its sound channel (the SNCH named like its model) in this update, as RCCars.exe
      // calls 0x494400 each frame: `sound` to play (empty: nothing; the sound's flags decide whether it restarts,
      // waits or leaves what plays alone) or `silence` to stop it
      //  - people: their voice once, when the animation turns to Hurt (0x510714); guards never use theirs
      //  - seagull: seagull_vzliot while it takes off (start_flight), the "seagull" loop after it, silence while
      //    the player cannot see it (0x5127bb)
      //  - dog, while the player is near: dog_pain while a car faster than 15 km/h touches it (0x5349d0), else
      //    dog_attack while it barks at the car (attack; it only starts when the channel is silent), else silence
      //    (0x518fa2)
      std::string               sound;
      bool                      silence = false;
      // a person walking where the path leaves footprints (RCCars.exe 0x50f930: moving, path state 1..3 or 5, not
      // RepairMan); fx::Footprints puts them down
      bool                      soles = false;
      };

    // another car on the track (the opponents)
    struct Car {
      Vec3 pos;
      Vec3 velocity;
      bool hit = false;   // set by update(): it ran into someone, which knocks it off its recording (0x4fd9d0)
      bool roadCar = false; // set by Traffic::update: it touched a road car (0x534880)
      };

    // `pack` reads RCCars.pack (Profiles, Settings); a character that cannot be set up is left out (logged)
    Characters(PhysicsWorld& world, ObjectLibrary& library, const SbScene& track, std::string_view trackName,
               Reader pack);
    ~Characters();

    // after PhysicsWorld::step. `player` may be null. Returns true when the player hit someone hard enough for
    // "Wielkie Łup!" (the turbo tank has got its 5 units already)
    bool                        update(float dt, Vehicle* player, std::vector<Car>& cars);
    bool                        update(float dt, Vehicle* player) { std::vector<Car> none; return update(dt,player,none); }
    // back to the start of the race
    void                        restart();
    // the options' RenderQual (0..2): how far from the player's car the people are there (0x5100d0)
    void                        setRenderQuality(int32_t q) { renderQuality = std::clamp(q,0,2); }

    const std::vector<Actor*>&  actors() const { return view; }
    // a burst a guard fires in this update (message 0x261 to BURST_MNG, fx::Gunfire): from its gun at a car,
    // PlayerCar or an index of `cars`
    static constexpr int        PlayerCar = -1;
    struct Shot {
      Vec3 gun;
      int  car = PlayerCar;
      };
    const std::vector<Shot>&    shooting() const { return guns; }
    // someone took the player's car in their hand in this update (0x5111f0 -> 0x4f3640: the chase camera holds still)
    bool                        grabbedPlayer() const { return grabbed; }
    // someone holds the player's car after this update (0x4f3690 lets go of it when thrown)
    bool                        holdingPlayer() const { return carried; }
    size_t                      size()   const { return view.size(); }
    // the player's car ran into someone in this update hard enough for car_cdt_obj: a person or a guard faster than
    // 10 km/h (0x534340, 0x5344f0), a dog faster than 15 km/h (0x5349d0)
    bool                        bumpedPlayer() const { return bumped; }
    // someone kicked or threw the player's car in this update (0x511634, 0x510e6b: "<name> zadziera ze złym
    // chłopcem" on the race's chat console)
    bool                        kickedPlayer() const { return kicked; }

  private:
    struct Body;
    struct State;
    struct Settings {
      float speedForw     = 0.3f;   // People: not used by the path
      float peopleRate    = 1.f;    // MAX_IMPULSE.ini coeffPeople (0.5..1.5): 1.116, people's time per second (0x51044c)
      float gullRun       = 0.21f, gullFlight = 0.25f, gullHeight = 1.5f, gullAngle = 45.f;
      float gullIdle      = 0.22f, gullTurn = 2.57f;
      float spiderRad     = 0.23f, spiderFloor = 0.01f, spiderFloorDead = 0;
      float dogCone       = 45.f, dogNear = 0.35f, dogFarNear = 0.5f, dogFarFar = 3.f, dogReach = 0.3f;
      float dogAttack     = 1.5f, dogAnim = 1.24f;
      float vultureVel    = 1.02f, vultureScale = 0.32f, vultureShiftY = -4.4f;
      };

    void        load(const Reader& pack);
    void        setup(State& s, const SbInstance& in, const Reader& pack);
    void        makeBody(State& s);
    void        placeBody(State& s);

    void        person (State& s, float dt, Vehicle* player, std::vector<Car>& cars);
    void        guard  (State& s, float dt, Vehicle* player, std::vector<Car>& cars);
    void        seagull(State& s, float dt, Vehicle* player, const std::vector<Car>& cars);
    void        crab   (State& s, float dt, Vehicle* player, std::vector<Car>& cars);
    void        spider (State& s, float dt, Vehicle* player, std::vector<Car>& cars);
    void        dog    (State& s, float dt, Vehicle* player, std::vector<Car>& cars);
    void        vulture(State& s, float dt);

    // kick (Pendal) and grab-and-throw (Brosok) of people and guards: true while one of them goes on
    bool        attack (State& s, float dt, Vehicle* player);
    // the car hit the character: true when the car touches its body faster than `kmh`
    bool        hitBy  (State& s, Vehicle* player, float kmh);
    // the player's car (carBalls) touches its body; `at` the middle of the two spheres that meet
    bool        meets  (const State& s, Vec3* at = nullptr) const;
    // another car touches the character (within `radius` of `at`, `height` above it): marks it Car::hit
    static bool touches(Car& c, const Vec3& at, float radius, float height);
    void        play   (State& s, int32_t seq, bool loop, float rate = 1.f);
    void        animate(State& s, float dt);
    void        turnTo (State& s, const Vec3& dir, float maxRad);
    float       ground (const Vec3& at, float fallback) const;
    float       random();
    void        finish (State& s);

    PhysicsWorld&                                 owner;
    ObjectLibrary&                                lib;
    const SbScene&                                track;
    std::string                                   trackName;
    Settings                                      prm;
    std::map<const ObjectLibrary::Model*,std::unique_ptr<Skeleton>> skeletons;
    std::vector<std::unique_ptr<State>>           states;
    std::vector<Actor*>                           view;
    std::vector<Shot>                             guns;
    std::mt19937                                  rng{20061013};
    float                                         sincePerson = 1e3f; // s since the last "Wielkie Łup!" per kind
    float                                         sinceDog    = 1e3f;
    float                                         sinceGuard  = 1e3f;
    bool                                          greatHit    = false;
    bool                                          grabbed     = false;
    bool                                          carried     = false;
    bool                                          bumped      = false;
    bool                                          kicked      = false;
    float                                         carKmh      = 0;    // the player's speed before this step
    float                                         lastKmh     = 0;
    int                                           vultures    = 0;
    std::vector<Ball>                             carBalls;           // the player's car this update
    Vec3                                          carAt;
    std::vector<Ball>                             ballBuf;
    int32_t                                       renderQuality = 2;
  };

}
