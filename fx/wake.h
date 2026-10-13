#pragma once

#include <vector>

#include "formats/settings.h"
#include "marks.h"
#include "particles.h"

namespace rc::fx {

// Settings/wtr_trace.crs (0x529a20): the wake, the stretched bow waves behind the rear wheels and the body
struct WakeSettings {
  float forwZ = 0, backZ = 0, forwX = 0, backX = 0;   // m: the quad reaches this far ahead / behind, to the sides
  float forwA = 0, backA = 0;                         // opacity 0..255 of its front and back edge
  float animSpeed = 0, animMin = 0, animMax = 0;      // its texture is squeezed and stretched between min and max
  CurveSpline speed;                                  // Splines/wtr_text.spl: % a second the texture runs, by km/h
  // ForwZBody .. BackXBody are read too, but only used for a wheel numbered -1, which the game never asks for
  };

// Settings/whr_horse.crs (0x526ad0): foam ("white horses") around every wheel and the body in the water
struct FoamSettings {
  float forwZ = 0, forwX = 0;          // m, half the length (times how much of the wheel the water line cuts), half
                                       // the width (times the tyre's width factor)
  float forwZBody = 0, forwXBody = 0;  // the body's
  float forwA = 0;                     // opacity 0..255
  float normalOffset = 0;              // m over the water
  float animDelay = 0;                 // s between steps of the texture
  };

// The wake (wtr_trace, 0x528ce0 .. 0x529a20) and the foam (whr_horse, 0x526020 .. 0x526ad0) of the players' cars:
// one quad each on the water surface, around a wheel or the body cutting it, made anew every frame.
class Wake {
  public:
    // What RCCars.exe finds out about the water around a wheel or the body (0x531df0, 0x535130)
    struct Probe {
      bool  inWater = false;   // the water line within `limit` above the point, or the point under water
      float depth   = 0;       // the point's height over the water
      float waterY  = 0;
      float limit   = 0;       // the wheel's radius, half the body's height
      Vec3  pos;
      Vec3  normal{0,1,0};     // of the water
      Vec3  heading{0,0,1};    // wheels: where the wheel rolls (steered), its axle
      Vec3  axle{1,0,0};
      };
    struct Car {
      Vec3               velocity;            // m/s
      Vec3               right, up, forward;  // the car's X, Y and Z axes
      int                tyre = 0;            // 0..3
      std::vector<Probe> wheels;              // RCCars.exe's order: front left, front right, rear left, rear right,
                                              // then the middle ones
      Probe              body;                // the middle of the body
      Probe              bodyDeep;            // a quarter of the body's height lower (0x531df0)
      };

    Wake(const WakeSettings& wake, const FoamSettings& foam);

    void update(const Car& car, float dt, Random& rnd);
    void collect(std::vector<Mark>& out) const;
    void clear();

  private:
    struct Trace {
      bool  inWater = false;
      bool  forward = true;
      float scroll  = 0;            // texture offset along the wake
      float len[2]  = {0,0};        // its length in the texture at the front and back corners
      int   dir[2]  = {1,-1};       // growing or shrinking
      Mark  quad;
      };
    struct Horse {
      bool  inWater = false;
      float scroll = 0, timer = 0;
      Mark  quad;
      };

    void trace(Trace& t, const Probe& p, const Car& car, float dt, Random& rnd) const;
    void horse(Horse& h, const Probe& p, const Car& car, bool body, float dt) const;

    const WakeSettings& wakeSet;
    const FoamSettings& foamSet;
    std::vector<Trace>  traces;     // rear left, rear right, body
    std::vector<Horse>  horses;     // the wheels, then the body
  };

}
