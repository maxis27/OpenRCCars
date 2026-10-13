#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "marks.h"

namespace rc::fx {

// Settings/car_trace.ini (read at 0x5300e0)
struct TrackSettings {
  float timeLife   = 0;   // s a track stays (it fades out over the last quarter)
  float maxLen     = 0;   // m: a straight stretch is not stretched longer than this
  float scaleCoeff = 0;   // texture repeats a metre along the track
  float maxHeight  = 0;   // m: a point that bends the track more than this from the straight starts a new piece.
                          // The file has the key twice as "maxLen" and no "maxHeight", so the game reads 0 here
  };

// Tyre tracks (RCCars.exe car_trace, 0x52f050 .. 0x5300d0): every wheel leaves a strip of quads behind on sand,
// wet sand, trampled sand, gravel and wood, kept in a ring of points per wheel (32 for the front wheels, 48 for the
// rear ones, half that for the computer's cars, which only have the rear wheels' tracks). A point holds where the
// tyre touched, its two edges, and how far along the strip it is; while the car goes straight on over one triangle
// of the ground, the newest point is moved on instead of adding another.
class TyreTracks {
  public:
    // RCCars.exe's wheels: 0/1 front left/right, 2/3 rear left/right
    struct Wheel {
      bool     touching = false;  // on the ground, not in the water
      Vec3     contact;
      Vec3     normal{0,1,0};     // of the ground there
      uint32_t triangle = 0;      // the ground's triangle (CollisionMesh)
      uint8_t  sound    = 0;      // SoundSurface of the ground
      };

    TyreTracks(const TrackSettings& settings, bool player);

    // after the physics: `velocity` of the car (m/s), `forward` its +Z axis, `tyre` the tyres bought (0..3)
    void update(const std::array<Wheel,4>& wheels, const Vec3& velocity, const Vec3& forward, int tyre, float dt);
    // quads within 20 m of the camera (0x52fd00)
    void collect(std::vector<Mark>& out, const Vec3& camera) const;
    // the car was put somewhere else: the strips end where they are
    void cut();
    void clear();

  private:
    struct Point {
      float life = 0;
      float intensity = 0;
      Vec3  pos, normal, side;
      bool  end    = true;    // the last point of its strip (0xb)
      bool  broken = false;   // the car changed direction here (0xc)
      bool  forward = true;   // going forward (0xd)
      float dist = 0;         // along the strip (0xe)
      int   tyre = 0;         // texture (0xf)
      Vec3  left, right;      // the strip's edges here
      float v = 0;            // texture coordinate along the strip
      };
    struct Input {
      Vec3  pos, normal, side;
      float intensity = 0;
      bool  forward = true;
      int   tyre = 0;
      float width = 1;
      };
    struct Ring {
      std::vector<Point> pts;
      int      tail = 0, head = -1;
      bool     linked = false;    // touched the ground last frame (0x0)
      uint32_t triangle = 0;      // where (0x4/0x8)

      int  next(int i) const;
      int  prev(int i) const;
      };

    void add  (Ring& r, const Input& in, bool newStrip, bool sameTriangle) const;
    void write(Ring& r, int at, const Input& in, int linkFrom) const;
    void step (Ring& r, const Input& in, bool link) const;

    const TrackSettings& set;
    bool                 player = false;
    std::array<Ring,4>   rings;
  };

}
