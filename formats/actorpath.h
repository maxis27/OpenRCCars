#pragma once

#include <cstdint>
#include <vector>

#include "math3d.h"

namespace rc {

// Profiles\<name>.dat in RCCars.pack: where a person walks (people.sb characters, their INST script names it:
// Section Keys "Path" name(string) = "beach_1_man"). Recorded in the editor every few hundredths of a second:
//   u32 count, f32 duration (s), u32 count, u32 (unused), then count records of 32 bytes:
//   f32 time (s), vec3 position (world, the feet), vec3 direction the person faces, f32 state (1, 2, 5, 7: the
//   footprints RCCars.exe leaves for 1..3 and 5).
// RCCars.exe (0x5118f0) keeps every other record (0, 2, 4 ..: n/2 of them); the person goes round the path for
// ever (0x511b50): position and direction linear between records, the heading turned towards the direction by at
// most 180 degrees per second.
struct PeoplePath {
  struct Point {
    float time = 0;
    Vec3  pos;
    Vec3  dir;
    float state = 0;
    };
  std::vector<Point> points;

  bool  empty()    const { return points.empty(); }
  float duration() const { return points.empty() ? 0.f : points.back().time; }
  // position and direction at `time` s (wrapped round the path)
  Point at(float time) const;

  static PeoplePath load(const std::vector<uint8_t>& file); // throws FormatError
  };

// Profiles\<name>.gpf: a recorded camera path ("ZEROCAM"), used for the vultures (vlt_<track>_<1|2>.gpf):
//   u16 0x101, char[18] name, u32 0, u32 key count, u32 values per key (9), u32 value count, f32 duration (s),
//   then the values (per key: position, forward, up) and per key: f32 time, u32 index of its first value.
// RCCars.exe (0x472db0) interpolates the values of the two keys around a time linearly.
struct FlightPath {
  struct Key {
    float time = 0;
    Vec3  pos, forward, up;
    };
  std::vector<Key> keys;

  bool  empty()    const { return keys.empty(); }
  float duration() const { return keys.empty() ? 0.f : keys.back().time; }
  Key   at(float time) const;   // wrapped round the path

  static FlightPath load(const std::vector<uint8_t>& file); // throws FormatError
  };

// Profiles\rdc_<track>_forw.gpf and rdc_<track>_back.gpf: the two ways the road traffic drives (name "ROADCAR"),
// the same file layout as FlightPath with 8 values per key: position, the direction the car faces (level), the
// steering (x 150 = the front wheels' angle in degrees, at most 30) and a tilt (RCCars.exe 0x516f50: the body is
// pitched by atan(tilt/4.2) and let down by 0.75*cos of that). The position is 0.75 m above the car's origin.
// RCCars.exe (0x472db0) interpolates the values of the two keys around a time linearly; a car is done with the path
// once the time goes past the last key (0x472d70, 0x472d40), it does not go round.
struct RoadPath {
  struct Key {
    float time  = 0;
    Vec3  pos, dir;
    float steer = 0;
    float tilt  = 0;
    };
  std::vector<Key> keys;

  bool  empty()    const { return keys.empty(); }
  float duration() const { return keys.empty() ? 0.f : keys.back().time; }
  Key   at(float time) const;   // clamped to the first and the last key

  static RoadPath load(const std::vector<uint8_t>& file); // throws FormatError
  };

}
