#pragma once

#include "formats/math3d.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Camera;

namespace rc {
class CollisionMesh;
}

// The camera behind the player's car, as RCCars.exe drives it (0x5007a0 every frame, 0x501180 when it is drawn),
// with the numbers of Settings\Camera.ini (read by 0x4f9da0).
//
// The camera keeps its own eye. Every frame it looks at it from a frame on the car: the car's centre, with the car's
// heading laid flat (when the nose points less than 30 degrees from straight up, the heading of the nose and the
// roof's back half, 0x500840). In that frame the eye has a distance across the ground, a height and an angle round
// the car (0 = straight behind); each of them is pulled by a "spring" (0x4722c0, Spring below) and the eye is put
// back where they say, looking at the car's centre (0x471ca0):
//   - distance: defDistXZ at once (no lag), plus up to half of it more with speed (from 4 m/s, all of it at 8 m/s,
//     reached at 1 m/s) and a fifth more while the boost and the gas are held (at 0.5 m/s, back at 0.3 m/s);
//   - height: defDistY, kept within 75 % of it either way, coming back at accDistY m/s;
//   - angle: kept within 20 degrees, coming back at accAlpha*200 degrees/s times (angle/20)^2, only 30 when the
//     car spins faster than 180 degrees/s.
// What is drawn is then turned (0x501180, 0x500e60): when the ground hides the line from 15 cm over the car to 6 cm
// under the eye (0x500d60), the eye swings up round the car by CDT_AngleUp degrees (at CDT_AngleUpSpeed degrees/s),
// and the view looks up by VisTurn degrees plus up to CDT_AngleUp_DIR_ADD while it is swung up (at
// CDT_AngleUp_SPEED_DIR_ADD degrees/s). Those turns are only for the picture: the springs keep the unturned eye.
class ChaseCamera {
  public:
    // Settings\Camera.ini, "CAMERA" sliders
    struct Settings {
      float distXZ     = 0.7929f;  // defDistXZ  1..100 -> 0.5..1.5 m
      float distY      = 0.3636f;  // defDistY   1..100 -> 0.1..1.0 m
      float accDistY   = 0.7141f;  // accDistY   1..100 -> 0.1..2.0 m/s
      float accAlpha   = 0.4949f;  // accAlpha   1..100 -> 0..1 (x200 degrees/s)
      float visTurn    = 11.818f;  // VisTurn    1..100 -> -30..30 degrees, positive looks up
      float angleUp    = 29.09f;   // CDT_AngleUp, degrees
      float angleUpSpd = 29.39f;   // CDT_AngleUpSpeed, degrees/s
      float dirAdd     = 13.13f;   // CDT_AngleUp_DIR_ADD, degrees
      float dirAddSpd  = 1.51f;    // CDT_AngleUp_SPEED_DIR_ADD, degrees/s

      // defaults above if the file is missing
      static Settings load(const std::function<std::vector<uint8_t>(const std::string&)>& read);
      };

    // RCCars.exe 0x4722c0: pulls `v` toward `target`. Outside target-below .. target+above it is put back at once
    // (at maxRate/s), inside it moves on at `rate`/s, scaled by (distance to target / band)^exp when exp is not 0.
    // Nothing happens within `tol` of the target. Returns the change.
    struct Spring {
      float target = 0, tol = 0, below = 0, above = 0, rate = 0, maxRate = 0, exp = 0;
      float step(float v, float dt) const;
      };

    struct Car {
      rc::Mat4  pose;          // model -> world, for the heading
      rc::Vec3  centre;        // centre of mass: the camera's frame and what it looks at
      rc::Vec3  velocity;      // m/s
      float spin  = 0;     // angular speed, rad/s
      bool  boost = false; // boost and gas held
      };

    ChaseCamera();
    explicit ChaseCamera(const Settings& s) : cfg(s) {}

    const Settings& settings() const { return cfg; }

    // starts over right behind the car (RCCars.exe passes dt = 2e6 for that, 0x508b50)
    void reset(const Car& car);
    void update(const Car& car, float dt);
    // the view: `camera` gets the eye and where it looks; `coll` hides the car (nullptr: never)
    void apply(const Car& car, const rc::CollisionMesh* coll, float dt, Camera& camera);
    // the developer mode's F9 camera (0x500ce9, 0x501183): the springs set to DevDistXZ behind and DevDistY over
    // the car with no give at all, no swing up round the ground, only VisTurn; the next update() goes on from it
    static constexpr float DevDistXZ = 1.0443f;
    static constexpr float DevDistY  = 0.4091f;
    void applyRigid(const Car& car, Camera& camera);

  private:
    rc::Vec3     heading(const Car& car);

    Settings cfg;
    bool     valid    = false;
    rc::Vec3     eye;
    rc::Vec3     lastHeading = {0,0,1};
    float    extraSpeed = 0;   // car +0x6e0
    float    extraBoost = 0;   // car +0x6e4
    float    swing      = 0;   // car +0x6ec, degrees the eye is swung up
    float    lookUp     = 0;   // car +0x6f0, degrees added to VisTurn
  };
