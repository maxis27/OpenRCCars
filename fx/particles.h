#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "formats/math3d.h"

namespace rc::fx {

// The sprites the game draws, one per particle: a square turned to face the camera.
struct Billboard {
  enum Texture : uint8_t {
    Dust,      // Textures "dust": dust and exhaust puffs
    Droplet,   // Textures "particle" (8x8 blob), the default of RCCars.exe's particle system: water spray
    };
  Vec3    pos;
  float   halfW = 0, halfH = 0; // metres, or with `pixels` pixels of a 640 wide screen (scaled with the screen)
  float   angle = 0;            // radians, turned in the screen plane
  float   rgba[4] = {1,1,1,1};
  Texture texture = Dust;
  bool    pixels  = false;      // the same size on the screen at any distance
  };

// rand() of the original: 0..32767
class Random {
  public:
    explicit Random(uint32_t seed = 1) : gen(seed) {}
    float unit() { return float(gen()%32768u)*(1.f/32767.f); }  // 0..1
    float sym()  { return unit()*2.f-1.f; }                     // -1..1
    bool  chance(float percent) { return unit()*100.f<percent; }

  private:
    std::minstd_rand gen;
  };

struct Particle {
  Vec3  pos, vel;
  float life   = 0;      // s, at birth
  float left   = 0;      // s still to live
  float scaleX = 1, scaleY = 1;
  float angle  = 0;      // degrees
  float twist  = 0;
  float rgb[3] = {1,1,1};
  float alpha  = 1;      // exhaust: the turbo's puff is fainter
  Vec3  base, wobble;    // water spray: the position without its shaking, and the shaking's axis

  float age() const { return life-left; }
  };

// The emitter of RCCars.exe's particle system (0x477c70, 0x4776e0, 0x478270): it makes `rate` particles a second,
// the fractions carried over to the next frame, spread along the way it moved since the last frame. A particle
// lives until its time is up; an emitter holds at most `capacity` of them.
class Emitter {
  public:
    explicit Emitter(size_t capacity) : capacity(capacity) {}

    // places the emitter; the first time also its last position
    void   moveTo(const Vec3& at);
    // how many particles to make in `dt` at `rate` a second
    size_t count(float rate, float dt);
    // where the i-th of n new particles starts: spread from the last position to the current one
    Vec3   wayPoint(size_t i, size_t n) const;
    bool   moved() const { return (at-last).length()>=1e-6f; }
    // makes room and returns a new particle at `at`, or nullptr when full
    Particle* spawn(const Vec3& at);
    // ages the particles by `dt`, drops the dead ones
    void   age(float dt);
    // the next frame starts from here
    void   endFrame() { last = at; }

    std::vector<Particle>&       particles()       { return live; }
    const std::vector<Particle>& particles() const { return live; }
    void   clear() { live.clear(); carry = 0; placed = false; }

  private:
    std::vector<Particle> live;
    size_t                capacity = 0;
    Vec3                  at, last;
    bool                  placed = false;
    float                 carry  = 0;
  };

}
