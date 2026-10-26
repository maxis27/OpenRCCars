#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "formats/math3d.h"

namespace rc {

class PackFile;
struct SbScene;

// The cars' springy antenna (RCCars.exe 0x4ff2d0 sets it up, 0x4fe9d0 moves it, 0x4fe5f0 feels for a ceiling;
// the engine's chain of points 0x44cc10). The antenna is a chain of nPoints points chainLength apart: the first
// two sit on the car (the mount and one link straight up the car's Y axis), the others are free. Every 3 ms
// (0x4fe9d0 cuts the frame into steps of at most 0.003 s, the car's pose in between interpolated):
// - the mount points' speed and acceleration come from how they moved (0x44cd00); a free point gets its weight,
//   mass x (0,-10,0) (0x44cdd0), the air against its speed, sqrt(volume x link) x sin(link, speed) x windFriction
//   (0x44ce80), and the tip a push of -100 (1 + sin a) along the car's Z axis (0x4ff260), a coming from the ceiling;
// - each free link bends back towards the direction of the link before it with stiffness x angle / length, across
//   the link, the point before pushed the other way (0x44d080); its sideways speed against that link's frame is
//   damped by damping / length (0x44d6b0);
// - the links' lengths hold (0x44dd80: a chain of Lagrange multipliers solved as one tridiagonal system), the
//   points move with half of the step's acceleration (v += a dt/2, p += v dt, 0x44d8e0) and are put back at their
//   lengths, their speed along a link made the one of the point before (0x44d970); each point's frame is the one
//   before turned the shortest way onto its link (0x44e2f0).
// RCCars.exe runs it with the car's x and z halved (0x4fe9d0 halves them going in and doubles them coming out), so
// the antenna feels half the car's sideways motion; done the same here.
// The ceiling (0x4fe5f0, the player's car only): a ray up from the mount; if something is closer than the
// tallest height of CacheData\car_antenna_<Car>_H2F.data + 0.1 m, that table gives the a which holds the tip
// under it; otherwise a is the table's last. 0x4ffe30 built the table by letting the antenna settle for 10 s, then
// for every a from pi/2 up in steps of pi/100, 10 s each, while the top of the drawn antenna (the box of the
// Car<n>Antenna instance, skinned and bent as AntennaView draws it) rose above the mount. Car2's table is that, to
// the millimetre; Car1's and Car3's were made with older sliders (chainLength 20, not 25, stiffness 25, not 10:
// that gives both to the millimetre), and the game uses them as they
// are. While under something it plays Antenna_scratch (Sounds::tickAntenna).
class Antenna {
  public:
    struct Settings {               // Settings\Antenna_Car<n>.ini (0x500230): /100, 10 above 1000
      int32_t points       = 4;
      float   chainLength  = 0.25f;
      float   mass         = 0.25f;
      float   stiffness    = 20.f;
      float   damping      = 1.16f;
      float   windFriction = 4.6f;
      float   volume       = 0.03f;
      struct Height { float angle, height; };
      std::vector<Height> h2f;      // CacheData\car_antenna_Car<n>_H2F.data, by height
      // `car` 0..2: Car1 (Overkill) .. Car3 (Hummer)
      static Settings load(PackFile& pack, size_t car);
      float freeAngle() const;      // the push's angle without a ceiling
      float angleUnder(float ceiling) const;
      float reach() const;          // how far up the ceiling is looked for
      };

    // `mount`: car space point the antenna stands on (mountOf); `rest`: car space rotation of the first point's
    // frame (0x4ff2d0: a quarter turn about z, x up the car)
    Antenna(const Settings& settings, const Mat4& carPose, const Vec3& mount, const Mat4& rest);

    // the car moved to `carPose` in `dt` s; `ceiling`: how far above the mount something is, if anything
    void step(float dt, const Mat4& carPose, std::optional<float> ceiling = std::nullopt);
    // world transform of the joints jnt3_1 .. jnt3_<points-1>: on the points, x along the next link
    std::vector<Mat4> joints() const;
    // the mount, world space
    Vec3   mountPoint() const { return mountWorld; }
    float  reach() const { return set.reach(); }

    // RCCars.exe 0x4ff2d0: the point the antenna stands on, from the box of the car model's ANTENNA (else
    // Antennaa) node: its largest x and z and its smallest y, in model space; false if there is no such node
    static bool mountOf(const SbScene& scene, int32_t model, Vec3& out);

  private:
    struct Point {
      Vec3 pos, vel, acc, force;
      Mat4 frame;               // rotation and pos; x along the link to the next point
      };

    void substep(float h, const Mat4& car, const Vec3& zAxis, float angle);
    void rebuildFrames(std::vector<Point>& s) const;

    Settings            set;
    Vec3                mount;      // car space
    Mat4                restRot;    // car space rotation of the first frame
    float               link = 0;   // chainLength / (points - 1)
    std::vector<Point>  prev, cur;  // the last two states, x and z halved
    Mat4                lastCar;
    Vec3                mountWorld;
  };

}
