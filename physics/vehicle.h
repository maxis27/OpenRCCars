#pragma once

#include <memory>
#include <vector>

#include "formats/collision.h"
#include "formats/math3d.h"
#include "cargeometry.h"
#include "carparams.h"

class btBroadphaseInterface;
class btCollisionDispatcher;
class btBoxShape;
class btCompoundShape;
class btDefaultCollisionConfiguration;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btRaycastVehicle;
class btSequentialImpulseConstraintSolver;
class btTriangleIndexVertexArray;
class btBvhTriangleMeshShape;
struct btDefaultMotionState;

namespace rc {

// Bullet world of one track. The track is a static triangle mesh made of the COLL triangles (what the car
// body hits); wheels do not use it: they are rays cast through the COLL grid (CollisionMesh::raycast).
//
// Bullet is tuned for objects of about a metre and the cars are ~0.4 m long, so the simulation runs
// Scale times larger than the data, with gravity scaled the same way: motion looks identical, only the
// numbers inside Bullet are bigger. Nothing outside this file and vehicle.cpp sees scaled values.
class PhysicsWorld {
  public:
    static constexpr float Scale   = 10.f;
    static constexpr float Gravity = 9.81f;   // m/s^2
    static constexpr float FixedDt = 1.f/120.f;

    explicit PhysicsWorld(const CollisionMesh& track);
    ~PhysicsWorld();

    // advances the simulation in fixed steps of FixedDt (at most 8 per call)
    void                     step(float dt);

    const CollisionMesh&     track() const { return coll; }
    btDiscreteDynamicsWorld& bullet()      { return *world; }
    btRigidBody&             ground()      { return *groundBody; }

  private:
    const CollisionMesh&                                  coll;
    std::unique_ptr<btDefaultCollisionConfiguration>      config;
    std::unique_ptr<btCollisionDispatcher>                dispatcher;
    std::unique_ptr<btBroadphaseInterface>                broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver>  solver;
    std::unique_ptr<btDiscreteDynamicsWorld>              world;

    std::vector<float>                                    vertices;
    std::vector<int>                                      indices;
    std::unique_ptr<btTriangleIndexVertexArray>           meshData;
    std::unique_ptr<btBvhTriangleMeshShape>               meshShape;
    std::unique_ptr<btRigidBody>                          groundBody;
  };

// A car on btRaycastVehicle: chassis box + four suspension rays.
class Vehicle {
  public:
    struct Input {
      float throttle  = 0;     // -1 (brake / reverse) .. 1
      float steer     = 0;     // -1 (right) .. 1 (left)
      bool  handbrake = false;
      bool  jump      = false; // pressed this frame; works with at least two wheels on the ground
      bool  boost     = false; // held: uses the turbo tank
      };

    // `start` places the model like CarMesh::draw expects (model -> world, rotation + translation)
    Vehicle(PhysicsWorld& world, const CarGeometry& geometry, const CarParams& params, const Mat4& start);
    ~Vehicle();

    void                       reset(const Mat4& start);
    // back on the wheels where it is, facing the same way (after a roll-over)
    void                       recover();
    // call once per frame before PhysicsWorld::step
    void                       update(const Input& in, float dt);

    // model -> world
    Mat4                       pose()  const;
    // forward speed, m/s (negative when reversing)
    float                      speed() const;
    // turbo tank, 0..1
    float                      boostLevel() const { return tank; }
    bool                       onGround() const;
    size_t                     wheelsOnGround() const;

    const CarGeometry&         geometry() const { return geom; }
    const CarParams&           params()   const { return prm; }
    // pose of geometry().wheels[i] in model space: hub position after suspension, steering and spin
    Mat4                       wheelPose(size_t i) const;

  private:
    class Raycaster;

    PhysicsWorld&                         owner;
    CarGeometry                           geom;
    CarParams                             prm;
    Vec3                                  com;       // centre of mass, model space
    float                                 steer = 0; // smoothed, -1..1
    float                                 jumpCooldown = 0;
    float                                 tank = 1;

    std::unique_ptr<btBoxShape>           box;
    std::unique_ptr<btCompoundShape>      shape;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody>          body;
    std::unique_ptr<Raycaster>            raycaster;
    std::unique_ptr<btRaycastVehicle>     vehicle;
  };

}
