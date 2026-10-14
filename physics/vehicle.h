#pragma once

#include <memory>
#include <vector>

#include "formats/collision.h"
#include "formats/math3d.h"
#include "cargeometry.h"
#include "carparams.h"

class btBroadphaseInterface;
class btCollisionDispatcher;
class btSphereShape;
class btCompoundShape;
class btDefaultCollisionConfiguration;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btPersistentManifold;
class btCollisionObject;
class btSequentialImpulseConstraintSolver;
class btTriangleIndexVertexArray;
class btBvhTriangleMeshShape;
struct btDefaultMotionState;

namespace rc {

// Bullet world of one track. The track is a static triangle mesh made of the COLL triangles but the water: what
// the cars' bodies and wheels (Vehicle) and the props (Prop) standing on the track hit.
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

    // btCollisionObject::getUserIndex() of props (Prop)
    static constexpr int     PropTag = 1;
    // the same of the cars (Vehicle, Opponent)
    static constexpr int     CarTag  = 2;

    const CollisionMesh&     track() const { return coll; }
    btDiscreteDynamicsWorld& bullet()      { return *world; }
    btRigidBody&             ground()      { return *groundBody; }
    // the sound surface (SoundSurface) of a triangle of ground()'s mesh, by Bullet's triangle index
    uint8_t                  groundSound(int triangle) const;

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
    std::vector<uint32_t>                                 solidTris;   // ground mesh triangle -> CollisionMesh one
  };

// A car as RCCars.exe drives it (0x4f0910, 0x4f5e50): a rigid body of 1 kg whose body is a handful of spheres
// (CarParams::body), on wheels that are spheres hanging from springs (Vehicle::suspension). Each step the springs,
// the engine on the rear wheels and the tires (Vehicle::tires) push the body; Bullet moves it and stops the body
// spheres at the track and the props. The wheels stand on solid ground only: water (Surface::Water) is not
// solid, so the car wades through shallow water and sinks into deep water, slowed down by it, and counts as
// drowned once it has been fully under water for CarParams::drownTime.
class Vehicle {
  public:
    struct Input {
      float throttle  = 0;     // -1 (brake / reverse) .. 1
      float steer     = 0;     // -1 (right) .. 1 (left)
      bool  handbrake = false; // the wheels locked, as the original holds the car before the start
      bool  jump      = false; // pressed this frame: jumps with a wheel on the ground, or puts a tipped-over car back
      bool  boost     = false; // held: uses the turbo tank
      };

    // `start` places the model like ModelMesh::draw expects (model -> world, rotation + translation)
    Vehicle(PhysicsWorld& world, const CarGeometry& geometry, const CarParams& params, const Mat4& start);
    ~Vehicle();

    void                       reset(const Mat4& start);
    // exactly at `pose` (model -> world), moving with `velocity` (m/s) and `spin` (rad/s, world axes); the springs
    // at rest
    void                       place(const Mat4& pose, const Vec3& velocity, const Vec3& spin);
    // back on the wheels where it is, facing the same way (after a roll-over)
    void                       recover();
    // call once per frame before PhysicsWorld::step
    void                       update(const Input& in, float dt);

    // RCCars.exe's "Łup!" / "Wielkie Łup!" (msg_hits.csi): what the car hit since the last update, see Vehicle::hits.
    // Every hit has already added HitBoost units to the turbo tank. Hits on other cars are Opponents::collide's.
    enum class Hit : uint8_t { None, Hit, Great };
    static constexpr float     HitBoost = 5.f;
    Hit                        hit() const { return lastHit; }

    // For the sounds, as RCCars.exe keeps them (car state 0x50acf0, contacts 0x4f6940 / 0x4f68a0):
    // the body's strongest contact with the track or a prop in the last update, if any. `hit` when the car moves
    // straight into it (more than 145 degrees between the contact normal and the car's velocity there), otherwise a
    // touch; `impact` is the speed along the normal in km/h. Props count as wood (RCCars.exe: objects -> 7).
    struct Contact {
      uint8_t surface = 0;  // SoundSurface, 0 = no contact
      bool    hit     = false;
      float   impact  = 0;
      };
    const Contact&             contact()  const { return touch; }
    // touched another car in the last update, after not touching one in the update before
    bool                       carBump()  const { return bump; }
    // the highest sound surface under the wheels touching the ground or standing in water (SoundSurface::Water)
    uint8_t                    groundSound() const { return groundSnd; }
    // a wheel touches the ground or stands in water
    bool                       grounded() const { return groundedNow; }

    // model -> world
    Mat4                       pose()  const;
    // centre of mass, world; its velocity (m/s); the angular velocity in the car's own axes (rad/s, x pitch, z roll)
    Vec3                       centre()    const;
    Vec3                       velocity()  const;
    Vec3                       localSpin() const;
    // an impulse (kg m/s) at the world point `at`
    void                       push(const Vec3& impulse, const Vec3& at);
    // the Bullet body, tagged PhysicsWorld::CarTag
    btRigidBody&               chassis() { return *body; }
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
    class Action;
    struct Wheel {
      Vec3        mount;              // model space: the top of the spring, CarParams::springMount above the hub
      float       radius   = 0;
      bool        front    = false;   // steered
      bool        driven   = false;   // the rear pair: the engine pulls on these
      float       steerBy  = 0;       // share of the steering angle (the Hummer's middle wheels 1/3)
      float       length   = 0;       // m: the sphere's centre is this far below the mount, along the car's up axis
      float       rate     = 0;       // m/s: how fast `length` changed in the last step
      bool        contact  = false;
      Vec3        point, normal;      // world: where the sphere touches the ground, the ground's normal there
      const btCollisionObject* ground = nullptr;
      float       spin     = 0;       // rad, for drawing
      };

    // one step of RCCars.exe's car physics, called by Bullet on every fixed step of PhysicsWorld
    void                                  step(float h);
    // the spring lengths after the ground under the wheels (0x4fb340)
    void                                  suspension(float h);
    // the grip of the tires (0x4edac0): the forces at up to one front and one rear wheel that stop the car sliding
    // sideways (or, braking, sliding at all), added to `force` / `torque`
    void                                  tires(float h, Vec3& force, Vec3& torque);
    // surface under the wheels, water: slows the car down, tracks drowning
    void                                  surfaces(float dt);
    // the contacts of the last physics steps -> lastHit
    void                                  hits(float dt);
    // one manifold of the body with the track or a prop -> touch
    void                                  contacts(const btPersistentManifold& m, const btCollisionObject* other);
    // the steering angle of wheel `w`, degrees, + = left
    float                                 steerAngle(const Wheel& w) const;
    // the rear tires slide: braking with the wheels turned further than CarParams::driftAngle
    bool                                  drifting() const;

    PhysicsWorld&                         owner;
    CarGeometry                           geom;
    CarParams                             prm;
    Vec3                                  com;       // centre of mass, model space
    std::vector<Wheel>                    wheels;    // in the order of geometry().wheels
    int                                   frontPair[2] = {-1,-1};  // the original's wheels 0, 1 (front) ...
    int                                   rearPair[2]  = {-1,-1};  // ... and 2, 3 (rear)
    float                                 springK = 0; // N/m of every spring
    float                                 steer = 0; // steering position, see CarParams::steerCurve; + = left
    float                                 steerDeg = 0;   // the front wheels' angle, degrees
    int                                   gear = 1;       // 1 forward, -1 reverse
    bool                                  braking = false; // the wheels hold the car instead of the engine pulling
    bool                                  brakeKey = false;
    float                                 drive = 0;      // m/s^2 the engine pulls with (backwards < 0)
    float                                 movingTime = 0; // s since the car last went slower than 5 km/h
    float                                 jumpTimer = 0;
    float                                 tank = 0;
    float                                 boostTimer = 0;     // s since the start or since the tank ran dry
    bool                                  boostLock = false;  // ran dry with the key held
    bool                                  boostOn = false;
    Surface                               under = Surface::Default;
    float                                 sub   = 0;
    float                                 wet   = 0;          // share of the wheels in water, 0..1
    float                                 underWater = 0;     // s fully under water

    Hit                                   lastHit   = Hit::None;
    Contact                               touch;
    bool                                  bump      = false;
    bool                                  carBefore = false;  // touched a car in the update before
    uint8_t                               groundSnd = 0;
    bool                                  groundedNow = false;
    bool                                  wheelProp = false;  // a wheel stood on a prop
    float                                 sinceProp = 1e3f;   // s since the last hit on a prop

    std::vector<std::unique_ptr<btSphereShape>> spheres;
    std::unique_ptr<btSphereShape>        wheelShape[2];      // front, rear: what the wheels are swept with
    std::unique_ptr<btCompoundShape>      shape;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody>          body;
    std::unique_ptr<Action>               action;
  };

}
