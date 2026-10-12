#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "math3d.h"

namespace rc {

// CarProfiles\<track>\<name>.dat: a drive recorded in the original game, what its computer opponents replay
// (Scripts\ailayouts.ini names one per opponent). The same chunk format as the scenes:
//   0x50 root { 0x60 header {'a','i','p','q','r','f','g','h' u16, 't'..'w' texts "Time path FULL: 68.185939" ..},
//               0x63 [u32 frames][f32 duration][4 x u32][u32 frames], 0x64 frames, 0x65 [u32 kind][u32][f32] .. }
// Every frame ('g' = 136 bytes) is the car's whole state at one moment, about 100 a second:
//   f32 time, vec3 position, quaternion (w x y z), vec3 velocity, vec3 angular velocity, 4 x suspension length,
//   2 x 0, 4 x wheel spin?, 2 x 0, f32 ?, 4 x 0.0014, 2 x 0, u32 keys
// in world space of the track, the position being the model's origin. `keys` holds what the driver pressed:
// 0x01 throttle (with 0x10 most of the time), 0x20 marks an event whose number is in the upper 16 bits.
// A recording is one lap from the grid: "Time path FULL" is all of it, "CYCLE" the lap without the first
// seconds from the grid, so that its end (within 0.1 - 0.25 m) is where the cycle begins and it loops.
// The variants <name>_2, <name>_3 begin with the same seconds from the grid and drive the lap differently.
class CarProfile {
  public:
    struct Frame {
      float    time = 0;
      Vec3     position;
      float    rot[4] = {1,0,0,0};  // w x y z
      Vec3     velocity;
      Vec3     spin;                 // angular velocity
      float    suspension[4] = {};
      uint32_t keys = 0;
      };

    static CarProfile parse(const std::vector<uint8_t>& data);

    const std::vector<Frame>& frames()   const { return list; }
    bool                      empty()    const { return list.empty(); }
    float                     duration() const { return list.empty() ? 0.f : list.back().time; }
    // where the lap that loops begins: "Time path FULL" - "CYCLE", 0 if the header has no such texts
    float                     cycleStart() const { return loopStart; }

    // model -> world at `time` (seconds since the recording began), interpolated between the frames;
    // the first and last frame outside of the recording
    Mat4                      pose    (float time) const;
    Vec3                      velocity(float time) const;

  private:
    // the frame at or before `time` and how far towards the next one, 0..1
    size_t                    find(float time, float& t) const;

    std::vector<Frame>        list;
    float                     loopStart = 0;
  };

// Scripts\ailayouts.ini: the computer drivers ("AIPlayer<n>": car and skin) and, for each track ("AIRace<n>"),
// the opponents of a race (AI1 .. AI5: car, driver, recording, in which races, upgrades), the recording of
// the player's start ("PlayerPath") and a speed factor for all of them ("CoeffCommonOpponents").
// Written in the "Section Keys" format with #define constants and sums ("EASY + NORMAL + HARD").
struct AiLayout {
  enum Races : int32_t { Easy = 1, Normal = 2, Hard = 4 };

  struct Driver {
    int32_t     car  = 0;      // 0 Overkill, 1 Buggy, 2 Hummer
    int32_t     skin = 0;
    std::string face;
    };
  struct Opponent {
    int32_t     car    = 0;    // 0 Overkill, 1 Buggy, 2 Hummer
    int32_t     driver = 0;    // AIPlayer<n>
    int32_t     races  = 0;    // Races bits: in which races they start
    std::string path;          // "surf\over1": CarProfiles\surf\over1.dat
    int32_t     boost = 0, reson = 0, tires = 0;  // upgrade levels
    };
  struct Race {
    float                 coeff = 1;  // CoeffCommonOpponents
    std::string           playerPath;
    std::vector<Opponent> opponents;  // AI1 ..
    };

  std::map<int32_t,Driver> drivers;   // by AIPlayer number
  std::map<int32_t,Race>   races;     // by AIRace number

  static AiLayout parse(std::string_view text);
  };

// which AIRace<n> (and Track<n> in Scripts\championship.ini) a track is: 1 Surf (beach_1) .. 10 War path
// (beach_4), from the comments in championship.ini; 0 if unknown
int32_t aiRaceNumber(std::string_view track);

}
