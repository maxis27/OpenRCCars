#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "formats/math3d.h"
#include "formats/scene.h"

class btCollisionShape;
class btCompoundShape;
class btRigidBody;
struct btDefaultMotionState;

namespace rc {

class PhysicsWorld;

// Physics of a prop from the SCRI of its model in RCCarsDB/stone.sb:
//   Geom:     type (1 sphere, 2 cylinder, 3 cone, 4 bottle, 5 bucket, 6 shell, 7 kettle, 8 balloon),
//             shiftCenterMassOY (m, centre of mass above the model's origin)
//   Params:   mass (kg), coeffHookNorm (bounce: 0.7 balls, 0.3 the rest), coeffFrict
//   ExtForces fWindBlow (tumbleweed "Klubok": rolls in the wind; not simulated yet)
struct PropParams {
  enum Shape : int32_t { Hull = 0, Sphere = 1, Cylinder = 2, Cone = 3, Bottle = 4, Bucket = 5, Shell = 6,
                         Kettle = 7, Balloon = 8 };
  int32_t shape    = Hull;
  float   comY     = 0;
  float   mass     = 0.3f;
  float   bounce   = 0.3f;
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

// A prop in the track's Bullet world: a rigid body the cars push around. Shapes: a sphere or a cylinder (along
// model Y) fitted to the bounding box, a convex hull of the model for everything else.
class Prop {
  public:
    // `start`: where the track places it (a scale in it is ignored)
    Prop(PhysicsWorld& world, const PropGeometry& geometry, const PropParams& params, const Mat4& start);
    ~Prop();

    // model -> world
    Mat4        pose() const;

  private:
    // to the start, at rest
    void                                  reset();

    PhysicsWorld&                         owner;
    Mat4                                  start; // rotation + translation
    Vec3                                  com;   // model space
    std::unique_ptr<btCollisionShape>     child;
    std::unique_ptr<btCompoundShape>      shape;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody>          rigid;
  };

}
