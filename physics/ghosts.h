#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "formats/carprofile.h"
#include "cargeometry.h"
#include "carparams.h"
#include "carrig.h"

namespace rc {

class Vehicle;

// The ghost race, "Wyścig duchów" (RCCars.exe game modes 2 "Zwykły" and 3 "Multi", flags 0x40 and 0x80 of
// 0x1499dd0, chosen by RCCars.crs Player/GhostMode): the player drives laps alone, 19 of them that never end the
// race (0x4a4bc0, 0x4eaa20), against ghosts, cars replaying the player's laps (CarProfile, at the recorded speed:
// 0x503880 with no rubber band). Nothing touches a ghost: the cars, the people and the bullets leave it out
// (0x533940, 0x533970, 0x5359f0, 0x513100), and it is never knocked off its lap (car+0x4c).
//  - The player's car is recorded all the time (0x4f6ea0 -> 0x502b90): a frame at least 0.02 s after the last one,
//    at most 100000 of them (0x501f80).
//  - The first time over the line (the lap counter 1, 0x4fbeb0) only starts the recording and the ghosts over.
//  - Every lap after that: the player's own ghost (the one of the player's car, 0x4f1dd0) shows up, and takes the
//    lap when it has none or the lap is faster than its own (its last frame's time), the player's car's look with
//    the next skin (+1, of 4); then all the ghosts with a lap start it from its first frame, and the recording
//    starts over. Until then a ghost waits at its lap's first frame (car+0x44), and it stays at its last frame when
//    it has driven all of it.
//  - "Wróć na start" and R (0x4c7690, 0x4f6b20 -> 0x4f69e0): the player back to the start, the ghosts stopped at
//    their first frames, the recording and the lap counter from the beginning.
// In "Zwykły" there is only the own ghost, its lap for as long as the race lasts. In "Multi" there is a ghost of
// each car (0x4a5320) whose lap is in GhostRecords\<AIRace<n> GhostPath>\Track<n>_Car<m>.scg (0x4a9170), the
// cars without a file left out but the player's; the files are written when the race is left (0x4f17d0), so that
// the player races the best laps of all three cars, again and again.
class Ghosts {
  public:
    enum class Kind : uint8_t { Normal, Multi };   // "Zwykły", "Multi"

    // a car as it looks: 0 Overkill, 1 Buggy, 2 Hummer, the upgrade levels 0..3 and the skin 0..3
    struct Look {
      int32_t car = 0, boost = 0, resonator = 0, tires = 0, skin = 0;
      bool operator==(const Look& o) const {
        return car==o.car && boost==o.boost && resonator==o.resonator && tires==o.tires && skin==o.skin;
        }
      };

    class Ghost {
      public:
        Ghost(const CarGeometry& geometry, const CarParams& params);

        std::string           name;            // Language/Game 40220 + car: "Duch - MT"
        Look                  look;
        CarProfile            lap;             // what it drives, empty: none yet
        std::filesystem::path file;            // Multi: where its lap is kept
        bool                  own     = false; // the player's car's: takes the player's laps
        bool                  visible = false; // flag 2 of the car cleared: drawn
        bool                  running = false; // car+0x44: driving its lap, else waiting at its first frame

        // model -> world
        const Mat4&           pose()  const { return current; }
        // m/s forward
        float                 speed() const { return currentSpeed; }
        // the wheels as recorded: the springs' lengths and the steering, spinning with the speed
        CarRig::Pose          rigPose() const;
        const CarGeometry&    geometry() const { return geom; }
        // shown as `f` says, `dt` s after the last one: another player's car of the network game (net::RemoteCar)
        void                  put(const CarProfile::Frame& f, float dt);

      private:
        friend class Ghosts;
        // to `t` seconds of its lap
        void                  show(float t, float dt);

        CarGeometry           geom;
        CarParams             prm;
        CarRig                rig;
        float                 clock        = 0;
        Mat4                  current;
        float                 currentSpeed = 0;
        float                 wheelTurn    = 0;   // rad
        float                 springs[6]   = {};  // m, as recorded
        float                 steer        = 0;   // degrees
        float                 wheelRadius  = 0.05f;
      };

    // the car's geometry and driving parameters of car 0..2 (only the wheels' are used); its name
    using Rig  = std::function<void(int32_t car, CarGeometry& geometry, CarParams& params)>;
    using Name = std::function<std::string(int32_t car)>;

    // `player` the player's car as the player has it; `folder` the track's in GhostRecords, `track` its AIRace
    // number (1..10)
    Ghosts(Kind kind, const Look& player, const std::filesystem::path& folder, int32_t track, const Rig& rig,
           const Name& name);

    Kind                 kind() const { return mode; }
    // the first time over the line (lap counter 1): the recording and the ghosts with a lap from the beginning
    void                 start();
    // a lap done in `time` s (lap counter 2 ..): true if the own ghost took it
    bool                 lapDone(float time);
    // how the own ghost looks once it takes one of the player's laps: the player's car in the next skin
    Look                 ownLapLook() const;
    // back to the start (0x4f69e0): the recording over, the ghosts waiting at their first frames
    void                 restart();
    // `dt` s of the player's car; `keys` what was pressed (CarProfile::Frame::keys); `racing` false before the
    // start: nothing is recorded
    void                 record(float dt, const Vehicle& car, uint32_t keys, bool racing);
    // the car's state as a recording keeps it (0x4f5180), at `time` s
    static CarProfile::Frame frameOf(const Vehicle& car, float time, uint32_t keys);
    // the ghosts that drive on by `dt` s
    void                 update(float dt);
    // Multi: the laps into their files (0x4f17d0); false if one of them could not be written
    bool                 save() const;

    size_t               size() const { return list.size(); }
    Ghost&               operator[](size_t i) { return list[i]; }
    const Ghost&         operator[](size_t i) const { return list[i]; }
    // the recording of the lap being driven
    const CarProfile&    recording() const { return rec; }

    static constexpr float  FrameStep = 0.02f;   // 0x55458c
    static constexpr size_t MaxFrames = 100000;  // 0x501f80

  private:
    Ghost*               ownGhost();

    Kind                 mode;
    Look                 playerLook;
    std::vector<Ghost>   list;
    CarProfile           rec;
    float                recTime = 0;   // s since the recording began (profile +4)
  };

}
