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
// body hits); wheels do not use it: they are rays cast through the COLL grid (CollisionMesh::raycast) and
// against the props (Prop) standing on the track.
//
// Bullet is tuned for objects of about a metre and the cars are ~0.4 m long, so the simulation runs
// Scale times larger than the data, with gravity scaled the same way: motion looks identical, only the
// numbers inside Bullet are bigger. Nothing outside this file and vehicle.cpp sees scaled values.
class PhysicsWorld {
  public:
    static constexpr float Scale   = 10.f;
    static constexpr float Gravity = 10.f;    // m/s^2, as in RCCars.exe
    static constexpr float FixedDt = 1.f/120.f;

    explicit PhysicsWorld(const CollisionMesh& track);
    ~PhysicsWorld();

    // advances the simulation in fixed steps of FixedDt (at most 8 per call)
    void                     step(float dt);

    // btCollisionObject::getUserIndex() of props (Prop), the only bodies suspension rays hit besides the track
    static constexpr int     PropTag = 1;
    struct PropHit {
      btRigidBody* body = nullptr;
      Vec3         point, normal;
      float        fraction = 1;   // along the segment, 0..1
      };
    // nearest prop on the segment from `from` to `to`
    PropHit                  rayProps(const Vec3& from, const Vec3& to);

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

// A car on btRaycastVehicle: chassis box + four suspension rays. The wheels stand on solid ground only: water
// (Surface::Water) is not solid, so the car wades through shallow water and sinks into deep water, slowed down
// by it, and counts as drowned once it has been fully under water for CarParams::drownTime.
class Vehicle {
  public:
    struct Input {
      float throttle  = 0;     // -1 (brake / reverse) .. 1
      float steer     = 0;     // -1 (right) .. 1 (left)
      bool  handbrake = false;
      bool  jump      = false; // pressed this frame: jumps with a wheel on the ground, or puts a tipped-over car back
      bool  boost     = false; // held: uses the turbo tank
      };

    // `start` places the model like ModelMesh::draw expects (model -> world, rotation + translation)
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
    // turbo tank, 0..1 of a full one
    float                      boostLevel() const { return tank/prm.boostTank; }
    // turbo tank in units (CarParams::boostTank when full)
    float                      boostTank() const { return tank; }
    // adds units to the turbo tank, up to a full one (the game gives CarParams::BoostPerCheckpoint per checkpoint)
    void                       addBoost(float units);
    bool                       boosting() const { return boostOn; }
    // what most of the wheels touching the ground stand on (Water when the car is in water), as of the last update
    Surface                    surface() const { return under; }
    // how deep the body is in water: 0 dry .. 1 fully under water
    float                      submerged() const { return sub; }
    // fully under water for a while: the original puts the car back on the track (last checkpoint)
    bool                       drowned() const { return underWater>=prm.drownTime; }
    bool                       onGround() const;
    size_t                     wheelsOnGround() const;

    const CarGeometry&         geometry() const { return geom; }
    const CarParams&           params()   const { return prm; }
    // pose of geometry().wheels[i] in model space: hub position after suspension, steering and spin
    Mat4                       wheelPose(size_t i) const;

  private:
    class Raycaster;

    // surface under the wheels, water: slows the car down, tracks drowning
    void                                  surfaces(float dt);

    PhysicsWorld&                         owner;
    CarGeometry                           geom;
    CarParams                             prm;
    Vec3                                  com;       // centre of mass, model space
    float                                 steer = 0; // steering position, see CarParams::steerCurve; + = left
    float                                 jumpTimer = 0;
    float                                 tank = 0;
    float                                 boostTimer = 0;     // s since the start or since the tank ran dry
    bool                                  boostLock = false;  // ran dry with the key held
    bool                                  boostOn = false;
    Surface                               under = Surface::Default;
    float                                 sub   = 0;
    float                                 wet   = 0;          // share of the wheels in water, 0..1
    float                                 underWater = 0;     // s fully under water

    std::unique_ptr<btBoxShape>           box;
    std::unique_ptr<btCompoundShape>      shape;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody>          body;
    std::unique_ptr<Raycaster>            raycaster;
    std::unique_ptr<btRaycastVehicle>     vehicle;
  };

}
