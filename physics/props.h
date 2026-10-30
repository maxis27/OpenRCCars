#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "formats/math3d.h"
#include "formats/scene.h"
#include "balls.h"

namespace rc {

class PackFile;
class PhysicsWorld;

// What RCCars.exe reads for the props from Settings (0x4f7760, slider files, see SliderFile):
//  - the spheres of each shape (PropParams::Shape) but the sphere and the cone: CYLINDER.ini (0x4fa760), BOTTLE.ini
//    (0x4fa2e0), BUCKET.ini (0x4fa5f0), SHELL.ini (0x4fb050), kettle.ini (0x4facb0), BALLON.ini (0x4fa9c0): radii
//    and shifts, a slider position v of 1..100 standing for lo + (v-1) (hi-lo)/99 (BUCKET: its FValues);
//  - MAX_IMPULSE.ini: the most momentum (maxImpLinear m/s times the mass) and angular momentum (maxImpAng) a body
//    keeps at the start of each part of a step (0x50ed70 for the props, 0x4f5770 for the cars).
struct PropShapes {
  struct Piece {
    Vec3  at;      // the model's axes, from its origin
    float r = 0;
    };
  std::array<std::vector<Piece>,9> pieces;   // by PropParams::Shape; the sphere's depends on the model
  float maxImpLinear = 20.f, maxImpAng = 1.f; // the code's own values, without the file

  static PropShapes load(PackFile& pack);
  };

// Physics of a prop from the SCRI of its model in RCCarsDB/stone.sb (RCCars.exe 0x50c7b0, 0x50d0d0):
//   Geom:      type (1 sphere, 2 cylinder, 3 cone, 4 bottle, 5 bucket, 6 shell, 7 kettle, 8 balloon; missing or
//              anything else: a sphere), shiftCenterMassOY (m, the centre of mass above the model's origin)
//   Params:    mass (kg), coeffHookNorm (bounce), coeffFrict (friction)
//   ExtForces: fWindBlow (the tumbleweed "Klubok": blown about by the wind, 0x50d3f0, but only in a network game,
//              0x4a0b00, so never here)
// What a SCRI leaves out comes from the shape: centre of mass, mass, friction, bounce
//   sphere 0, 0.5, 1, 0.7; cylinder and balloon 0, 0.1, 1, 0.3; cone 0.03, 1, 1, 0.3;
//   bottle, shell and kettle 0.02, 0.4, 1, 0.3; bucket 0.1, 0.3, 1, 0.3.
struct PropParams {
  enum Shape : int32_t { Sphere = 1, Cylinder = 2, Cone = 3, Bottle = 4, Bucket = 5, Shell = 6, Kettle = 7,
                         Balloon = 8 };
  int32_t shape    = Sphere;
  float   comY     = 0;
  float   mass     = 0.5f;
  float   bounce   = 0.7f;
  float   friction = 1.f;
  bool    wind     = false;

  static PropParams parse(std::string_view script);
  };

// Outline of a model's drawn faces (the same faces the renderer shows: no _LOD copies), relative to `origin`
// (ObjectLibrary::Model::origin).
struct PropGeometry {
  std::vector<Vec3> points;
  Vec3              min, max;
  bool              empty() const { return points.empty(); }
  };

PropGeometry propGeometry(const SbScene& sb, int32_t model, const Vec3& origin = {});

// A prop as RCCars.exe moves it (class STON, 0x50c700 ..): a rigid body whose shape is a few spheres (0x50dc50),
// placed resting on the track (0x50cea0). The game moves it only while a player's car is near and in front of it
// (0x50ec10) and it is awake; each frame in up to 4 parts (0x50f190), as the cars move, but by the midpoint method
// (0x50dba0), with friction in the hits with the track (0x475c80). Cars and other props push it (PhysicsWorld).
class Prop {
  public:
    // `start`: where the track places it (a scale in it is ignored)
    Prop(PhysicsWorld& world, const PropShapes& shapes, const PropGeometry& geometry, const PropParams& params,
         const Mat4& start);
    ~Prop();

    // model -> world
    Mat4        pose() const;
    // where the model's origin is (what the original measures distances to the prop from)
    Vec3        origin() const { return pose().translation(); }
    // the spheres, world (0x50dc50)
    void        balls(std::vector<Ball>& out) const;

    // something knocked it since the last call: its sound plays (prop+0x274, set by the contact handlers 0x533ee0,
    // 0x534240, 0x5348d0; 0x50cb00 plays the sound and clears it)
    bool        takeKnock() { const bool k = knocked; knocked = false; return k; }
    void        knock() { knocked = true; }

    // what the contacts push (0x4754a0, 0x4756c0)
    float       invMass() const { return 1.f/mass; }
    Vec3        centre() const { return st.x; }
    Vec3        velocity() const { return vel; }
    Vec3        invInertia(const Vec3& v) const;
    void        applyImpulse(const Vec3& impulse, const Vec3& at);

  private:
    friend class PhysicsWorld;
    struct Body {
      Vec3  x, P, L;
      float q[4] = {1,0,0,0};   // w, x, y, z
      };
    // a point of the track a sphere touches (0x50e380): on the sphere, the normal out of the track
    struct Touch {
      Vec3 point, normal;
      };

    // 0x50f190: a frame of `dt` s, `near`: a player's car is near (0x50ec10)
    void        step(float dt, bool near);
    // the body at `b`, with what follows from it: rotation, velocities, world inverse inertia (0x50d990)
    void        set(const Body& b);
    // the forces now (0x50d670) for a part of `h` s
    void        forces(float h, Vec3& force, Vec3& torque) const;
    // the body moved by `h` s from `from` by the midpoint method (0x50dba0), set
    void        advance(const Body& from, float h);
    // 0x50e950: as far as `rest` s, or less where a sphere would go through the track on the way
    float       sweep(const Body& from, float rest);
    // 0x50e670: the moment of the hit between `from` and `hi` s from it, with the touches there
    float       impact(const Body& from, float hi, std::vector<Touch>& out);
    // the points of the track within `margin` of the spheres (0x50e380), at most 50
    void        touches(float margin, std::vector<Touch>& out) const;
    // 0x50e810: each touch met with friction and a bounce (0x475c80), up to 5 passes
    void        resolve(const std::vector<Touch>& at, float h);
    void        collide(const Touch& t, float h);
    // the stillness timers (0x50efa0); true when it falls asleep (0x50f350)
    bool        asleep(float dt);
    Vec3        pointVelocity(const Vec3& p) const { return vel+Vec3::cross(omega,p-st.x); }
    Vec3        axis(int i) const { return {rot.m[0][i],rot.m[1][i],rot.m[2][i]}; }

    PhysicsWorld&           owner;
    std::vector<PropShapes::Piece> pieces;   // model axes from the origin
    Vec3                    com;             // model space
    float                   mass = 0.5f, friction = 1.f, bounce = 0.7f;
    float                   maxP = 20.f, maxL = 1.f;
    Vec3                    inertiaBody;     // about the model's axes at the centre of mass
    bool                    wind = false;
    Body                    st;
    Mat4                    rot;
    Vec3                    vel, omega;
    float                   stillTime = 0, calmTime = 0, sinceTouch = 0, touchTime = 0;  // 0x224, 0x228, 0x22c, 0x230
    bool                    knocked = false;
    std::vector<Ball>       ballBuf[2];
    std::vector<Touch>      touchBuf;
  };

}
