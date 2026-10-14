#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formats/math3d.h"
#include "cargeometry.h"
#include "carparams.h"

namespace rc {

// How RCCars.exe draws a car's wheels and suspension (0x504820 Overkill, 0x505780 Buggy, 0x5068e0 Hummer). It
// never moves a wheel by its spring: it turns the nodes the wheels hang from, as far as they may turn, so that
// the wheels come close to where the springs put them and stay on their axles:
//  - a rigid axle (FRONT_AXLE, REAR_AXLE) swings about its node by atan(drop / lenAxe), drop = how far its two
//    wheels are below where they are modelled, at most 30 degrees down and 90 up, and rolls by
//    atan(4 * (left height - right height)); the Hummer's axles turn exactly as far as the wheels went (at most
//    20 degrees down), and its MIDDLE_AXLE moves up and down instead of swinging, as far as the front axle's
//    wheels go at 20 degrees
//  - the Buggy's arms (AXLE_*_UP, AXLE_*_DOWN) turn so that the wheel carrier between them (AXLE_*_WHEEL) is at
//    the wheel's height, within angleProcUpMax up and angleProcDownMax down; the carrier stays upright
//  - the front wheels steer with their supports (AXLE_FRONT_*_SUPPORT) or, without them, by themselves
//  - the springs between the body and the axles or arms turn and stretch to keep reaching them
class CarRig {
  public:
    struct Wheel {
      float lift  = 0;  // m: how far the spring has put the hub above where it is modelled (< 0 below)
      float steer = 0;  // degrees, + = left
      float spin  = 0;  // degrees
      };

    // the poses of one moment, model space
    class Pose {
      public:
        // geometry().wheels[i]: hub position, steering and spin
        const Mat4& wheel(size_t i) const { return wheels[i]; }
        // a part named by rc::carNode() with its modelled centre `pivot`; the body: translate(pivot)
        Mat4        part(std::string_view name, const Vec3& pivot) const;

      private:
        std::vector<Mat4>                         wheels;
        std::vector<std::pair<std::string,Mat4>>  nodes; // how each moving node is moved from where it is modelled
        friend class CarRig;
      };

    CarRig(const CarGeometry& geom, const CarParams& prm);

    Pose pose(const std::vector<Wheel>& wheels) const;

  private:
    CarGeometry        geom;
    float              axleLength;
    float              droopMax;       // degrees an axle may swing down
    float              armUp, armDown; // degrees
    float              middleTravel;   // m the middle axle may move up or down
    bool               hummer = false;
  };

}
