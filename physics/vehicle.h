#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "formats/collision.h"
#include "formats/math3d.h"
#include "cargeometry.h"
#include "carparams.h"
#include "carrig.h"
#include "balls.h"

namespace rc {

class Vehicle;
class Prop;

// The physics of one track, all by RCCars.exe's own rules: the cars (Vehicle) and the props (Prop) move against
// the COLL triangles but the water (touchTrack, crossesTrack); each frame the objects near the players then meet
// each other by their spheres (0x533430, see step()).
class PhysicsWorld {
  public:
    static constexpr float Gravity = 10.f;    // m/s^2, as in RCCars.exe

    explicit PhysicsWorld(const CollisionMesh& track);
    ~PhysicsWorld();

    // the players' cars, model -> world (RCCars.exe's viewers, 0x487300): the props move only near them (0x50ec10)
    // and the objects meet only within 10 m of one (0x533340). None set: every car counts.
    void                     setViewers(std::vector<Mat4> poses) { viewers = std::move(poses); }
    // a frame: the cars as RCCars.exe moves them (Vehicle::advance), the props (Prop, 0x50cb00), then the cars and
    // the props that touch each other (0x533430): a car and a prop bounce apart along the line between their
    // centres of mass (0x533ee0), two props too (0x534240). Cars, people and road cars meet in their own classes.
    void                     step(float dt);

    // a point of the solid track (not water) within reach of a sphere (RCCars.exe 0x4549f0)
    struct TrackTouch {
      Vec3    point;     // the nearest point of a triangle
      Vec3    normal;    // from it to the sphere's centre
      float   distance = 0;
      uint8_t sound    = 0; // SoundSurface of the triangle
      };
    // the triangles closer than `reach` to `centre`, the nearest point of each, at most `max` of them, the nearest
    // first; returns how many
    int                      touchTrack(const Vec3& centre, float reach, TrackTouch* out, int max) const;
    // the segment from `from` to `to` goes through a solid triangle (0x4557e0)
    bool                     crossesTrack(const Vec3& from, const Vec3& to) const;

    const CollisionMesh&     track() const { return coll; }
    const std::vector<Prop*>& props() const { return propList; }
    // a road car's balls (`origin` its model's) touch the props (RCCars.exe 0x5348d0): each one touched is pushed
    // 1 m/s away from it, along the line through the two spheres' centres, and sounds
    void                     pushProps(const std::vector<Ball>& balls, const Vec3& origin);
    // the balls touch a prop
    bool                     touchesProp(const std::vector<Ball>& balls, const Vec3& origin) const;

  private:
    friend class Vehicle;
    friend class Prop;
    // the objects within 10 m of a viewer meet (0x533430)
    void                     meet();
    bool                     nearViewer(const Vec3& at, float reach) const;
    bool                     inFront(const Vec3& at) const;

    const CollisionMesh&                                  coll;
    std::vector<Vehicle*>                                 cars;        // Vehicle adds and removes itself
    std::vector<Prop*>                                    propList;    // Prop too
    std::vector<Mat4>                                     viewers;
    std::vector<Ball>                                     ballBuf[2];
    uint32_t                                              rngState = 20061013u;
    mutable std::vector<uint32_t>                         seen;        // touchTrack: triangle -> query it was last seen in
    mutable uint32_t                                      query = 0;
  };

// A car as RCCars.exe drives it (0x4f0910, 0x4f5e50): a rigid body of 1 kg whose body is a handful of spheres
// (CarParams::body), on wheels that are spheres hanging from springs (Vehicle::suspension). Each step the springs,
// the engine on the rear wheels and the tires (Vehicle::tires) push the body, which moves and stops at the track
// by RCCars.exe's own rules (Vehicle::move); other cars, props and people push it as the original does
// (Vehicle::bounce, PhysicsWorld::step, Vehicle::bump). Other objects find it by its spheres (balls()). The wheels
// stand on solid ground only: water (Surface::Water) is not solid, so the car wades through shallow water and
// sinks into deep water, slowed down by it, and counts as drowned once a point just above its origin has been
// under water for CarParams::drownTime (see CarParams::drownHeight).
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
    bool                       carBump()  const { return carBumped; }
    // the highest sound surface under the wheels touching the ground or standing in water (SoundSurface::Water)
    uint8_t                    groundSound() const { return groundSnd; }
    // a wheel touches the ground or stands in water
    bool                       grounded() const { return groundedNow; }

    // model -> world
    Mat4                       pose()  const;
    // centre of mass, world; its velocity (m/s); the angular velocity in the car's own axes (rad/s, x pitch, z roll)
    Vec3                       centre()    const;
    // the centre of mass where pose() draws it: between the last two fixed steps, like the model on screen
    Vec3                       drawnCentre() const;
    Vec3                       velocity()  const;
    Vec3                       localSpin() const;
    // an impulse (kg m/s) at the world point `at`
    void                       applyImpulse(const Vec3& impulse, const Vec3& at);
    float                      invMass() const { return 1.f/prm.mass; }
    // world inverse inertia times `v`
    Vec3                       invInertia(const Vec3& v) const;
    // RCCars.exe 0x4f0750 for one contact of two bodies (cars, props) at `at`, its normal `n` out of b towards a
    // (0x534be0): over up to 10 passes, the first always, while they part at most at 0.02 m/s (their centres'
    // velocities, those parting at less than 1 m/s counted as not parting), each pass pushes them apart by an
    // impulse that would part them at twice the speed they meet at, at least 0.2 m/s (0x5543ec, 0x5544f0), equal
    // and opposite (0x4f0840)
    template<class A, class B>
    static void                exchange(A& a, B& b, const Vec3& at, const Vec3& n);
    // RCCars.exe 0x533990 for two cars that touch: pushed apart along the line from `bCentre` to `aCentre` (their
    // centres of mass, one of them maybe lifted), through the middle of it, so that they part at least as fast as
    // they met (0x534be0, 0x4f0750)
    static void                bounce(Vehicle& a, Vehicle& b, const Vec3& aCentre, const Vec3& bCentre);
    // two players' cars (the split screen): bounce() when their spheres touch after the last step; true if they do
    static bool                collide(Vehicle& a, Vehicle& b);
    // its spheres now, world (RCCars.exe 0x4ef9e0: the wheels', then the body's)
    void                       balls(std::vector<Ball>& out) const;
    // the spheres of a car of `geometry` and `params` standing at `pose` (model -> world), its wheels where the model
    // has them: an opponent replaying its recording
    static void                ballsAt(const CarGeometry& geometry, const CarParams& params, const Mat4& pose,
                                       std::vector<Ball>& out);
    // RCCars.exe 0x534340, 0x5344f0, 0x5349d0: a person, a guard or a dog touched at `point` stops the car along the
    // line from it to the car's centre of mass, through its middle, until it leaves at 0.05 m/s (0x4f0750)
    void                       bump(const Vec3& point);
    // another car touched it (Opponents::collide, collide()): for carBump()
    void                       touchedCar() { carNow = true; }
    // a prop touched it since the last call (an opponent's autopilot counts it as a collision, 0x533ee0 -> 0x4430)
    bool                       takeObjectTouch() { const bool t = objectNow; objectNow = false; return t; }
    // stuck for StuckSteps steps in a row: the original puts the car back at the last checkpoint (0x508b30)
    static constexpr int       StuckSteps = 5;
    bool                       stuck() const { return stuckFor>=StuckSteps; }
    // adds `dv` (m/s, world) to the velocity: a kick (people.sb characters)
    void                       addVelocity(const Vec3& dv);
    // a bullet of a guard or the BTR hit the car (RCCars.exe 0x508560 -> 0x4f3470): pushed ShotPushOut m/s away from
    // `gun` and ShotPushUp m/s up. The game then holds the car (no keys, handbrake on, 0x4f6b20) and puts it back
    // at the last checkpoint ShotHold s after the first hit (0x508ae0), more hits in between only push it
    static constexpr float     ShotPushOut = 6.5f, ShotPushUp = 3.25f, ShotHold = 2.f;
    void                       shotFrom(const Vec3& gun);
    // carried by someone (a person's hand): put at `pose` (model -> world), at rest; addVelocity() throws it afterwards
    void                       hold(const Mat4& pose);
    // forward speed, m/s (negative when reversing)
    float                      speed() const;
    // turbo tank, 0..1 of a full one
    float                      boostLevel() const { return tank/prm.boostTank; }
    // turbo tank in units (CarParams::boostTank when full)
    float                      boostTank() const { return tank; }
    // adds units to the turbo tank, up to a full one (the game gives CarParams::BoostPerCheckpoint per checkpoint)
    void                       addBoost(float units);
    // a hit's HitBoost units, counted (car+0x5748, 0x4f65fe ..): the championship's bonus for hits
    void                       addHitBoost() { addBoost(HitBoost); ++hitCount; }
    int32_t                    hits() const { return hitCount; }
    bool                       boosting() const { return boostOn; }
    // what most of the wheels touching the ground stand on (Water when the car is in water), as of the last update
    Surface                    surface() const { return under; }
    // how deep the body is in water: 0 dry .. 1 fully under water
    float                      submerged() const { return sub; }
    // under water for a while: the original puts the car back on the track (last checkpoint)
    bool                       drowned() const { return underWater>prm.drownTime; }
    // how fast a car with its origin at `origin` drowns (RCCars.exe 0x508a10): 0 out of the water, 1 under it,
    // 4 deep under it; drowned once the time multiplied by it passes CarParams::drownTime
    static float               drownRate(const CollisionMesh& track, const Vec3& origin, const CarParams& prm);
    bool                       onGround() const;
    size_t                     wheelsOnGround() const;

    const CarGeometry&         geometry() const { return geom; }
    const CarParams&           params()   const { return prm; }
    // how the wheels and the suspension are drawn now, model space (CarRig)
    CarRig::Pose               rigPose() const;
    // pose of geometry().wheels[i] in model space as drawn: hub position, steering and spin
    Mat4                       wheelPose(size_t i) const;
    // what a recording keeps of the wheels (RCCars.exe 0x4f5180): spring i's length and what it changed by in the
    // last step (m, car+0x595c, 0x5970), and the front wheels' angle (degrees, + = left, 0x5c6c)
    float                      springLength(size_t i) const { return wheels[i].length; }
    float                      springChange(size_t i) const { return wheels[i].change; }
    float                      steering() const { return steerDeg; }
    // how much wheel i spins against the ground (RCCars.exe 0x5019a0): the rad/s it turns faster or slower than it
    // rolls, over 20, up to 1. `rim` gets the rad/s it turns at, + forwards.
    float                      wheelSlip(size_t i, float* rim = nullptr) const;
    // where wheel i touches the track (world), false in the air
    bool                       wheelGround(size_t i, Vec3& at) const;
    // the more spinning of the two rear wheels (the original's 2 and 3), for the wheelspin sound and smoke
    float                      rearSlip() const;

  private:
    friend class PhysicsWorld;
    struct Wheel {
      Vec3        mount;              // model space: the top of the spring, CarParams::springMount above the hub
      float       radius   = 0;
      bool        front    = false;   // steered
      bool        driven   = false;   // the rear pair: the engine pulls on these
      float       steerBy  = 0;       // share of the steering angle (the Hummer's middle wheels 1/3)
      float       length   = 0;       // m: the sphere's centre is this far below the mount, along the car's up axis
      float       change   = 0;       // m: what `length` changed by in the last suspension pass (0x5970)
      bool        contact  = false;
      Vec3        point, normal;      // world: the point of the track the sphere touches, the normal there
      float       spin     = 0;       // rad, for drawing (0x5938)
      float       drag     = 0;       // 1/m: deep sand or water holding it back, k v^2 (0x4eeea0)
      // how fast it turns (RCCars.exe 0x4ef280), rad/s, + forwards: as the ground under it rolls it (0x5940), what
      // it follows of that (0x593c) and what the gas adds on top (0x5944)
      float       roll     = 0;
      float       turn     = 0;
      float       gasSpin  = 0;
      };

    // RCCars.exe's rigid body (car data 0x57f4, state 0x5884): the centre of mass, the orientation, the momentum and
    // the angular momentum
    struct Body {
      Vec3  x, P, L;
      float q[4] = {1,0,0,0};   // w, x, y, z
      };
    // a collision sphere, world (0x4ef9e0: the wheels', then the body's)
    struct Sphere {
      Vec3  c;
      float r     = 0;
      bool  wheel = false;
      };
    // a point of the track a sphere touches (0x4efe00)
    struct Touch {
      Vec3    point;     // of the sphere, towards the track
      Vec3    normal;    // out of the track, towards the sphere's centre
      bool    wheel = false;
      uint8_t sound = 0;
      };
    enum Which : uint8_t { BodySpheres = 1, WheelSpheres = 2, AllSpheres = 3 };

    // the body at `b`, with what follows from it: rotation, velocities, the inverse inertia
    void                                  set(const Body& b);
    // Euler step of `h` s from `from` (0x4f5590) under the forces there (0x4f5400 -> forces())
    Body                                  advanced(const Body& from, float h);
    void                                  spheres(std::vector<Sphere>& out, Which which) const;
    // a sphere is inside the track (0x4efdc0), but those that were at the start of the move part
    bool                                  penetrates(Which which) const;
    void                                  findInside();
    // the points of the track within `margin` of the spheres (0x4f01f0), at most 50
    void                                  touches(float margin, std::vector<Touch>& out) const;
    // the move: steps of the body through the track (0x4f5e50) and what it does where it hits it
    void                                  move(float h);
    float                                 sweep(const Body& from, float rest);           // 0x4f5b60
    float                                 impact(const Body& from, float hi, std::vector<Touch>& out); // 0x4f0270
    void                                  slide(const std::vector<Touch>& at, float t);  // 0x4f0560
    void                                  resolve(const std::vector<Touch>& at);         // 0x4f0750
    void                                  unstick();
    Vec3                                  pointVelocity(const Vec3& p) const;
    // the inertia times `v`
    Vec3                                  inertia(const Vec3& v) const;
    Vec3                                  axis(int i) const { return {rot.m[0][i],rot.m[1][i],rot.m[2][i]}; }

    // the frame of `dt` s in RCCars.exe's steps (0x4f6ea0, see PhysicsWorld::step)
    void                                  advance(float dt);
    // one step of RCCars.exe's car physics (0x4f5e50)
    void                                  step(float h);
    // after the steps of a frame of `dt` s: how fast the wheels turn and spin (0x4ef280)
    void                                  wheelSpin(float dt);
    // the forces on the body as it is now, for an Euler step of `h` s (0x4f0910) -> moveForce / moveTorque
    void                                  engine();
    void                                  forces(float h);
    // the stillness timers (0x4f59a0); true when the car is held still (0x4f6610)
    bool                                  asleep(float h);
    // a sphere (`which`) is within `margin` of the track (0x4efe00)
    bool                                  near(Which which, float margin) const;
    // the spring lengths after the ground under the wheels, after a move of `h` s, `hit` after one that met the
    // track (0x4fb340); true when the ground is higher than a spring lets its wheel go
    bool                                  suspension(float h, bool hit);
    bool                                  wheelTouches();
    // the grip of the tires (0x4edac0): the forces at up to one front and one rear wheel that stop the car sliding
    // sideways (or, braking, sliding at all), added to `force` / `torque`
    void                                  tires(float h, Vec3& force, Vec3& torque);
    // surface under the wheels, water: slows the car down, tracks drowning
    void                                  surfaces(float dt);
    // the contacts of the last physics steps -> lastHit
    void                                  hits(float dt);
    // RCCars.exe 0x533ee0: it touches prop `p`
    void                                  meet(Prop& p, uint32_t& rng);
    // a contact with the track (or with a prop: wood) that the car meets with velocity `v` there -> `into`
    static void                           note(Contact& into, const Vec3& v, const Vec3& normal, uint8_t surface);
    // the steering angle of wheel `w`, degrees, + = left
    float                                 steerAngle(const Wheel& w) const;
    // the rear tires slide: braking with the wheels turned further than CarParams::driftAngle
    bool                                  drifting() const;

    PhysicsWorld&                         owner;
    Body                                  st;
    Mat4                                  rot;        // rotation of st
    Vec3                                  vel, omega; // m/s, rad/s, world
    Vec3                                  inertiaBody; // kg m^2, about the car's own axes
    Vec3                                  frameForce, frameTorque; // pushed by update() for the whole frame (water, sand)
    Vec3                                  moveForce, moveTorque;   // the forces of the step move() runs
    std::vector<Sphere>                   sphereBuf[2];
    std::vector<Touch>                    touchBuf;
    std::vector<char>                     inside;     // spheres() (all) in the track at the start of the move part
    Contact                               stepTouch;  // the strongest contact with the track since the last update
    int                                   stuckFor = 0;
    CarGeometry                           geom;
    CarParams                             prm;
    CarRig                                rig;
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
    float                                 throttle = 0;   // Input::throttle of the frame, for Vehicle::engine
    bool                                  handbrake = false;
    float                                 drive = 0;      // m/s^2 the engine pulls with (backwards < 0)
    float                                 climb = 0;      // m/s^2 of CarParams::climb while it pulls forwards
    float                                 movingTime = 0; // s since the springs last took the slow rest (car data 0x8)
    bool                                  springsOn  = true; // off for the part after a hit (car data 0x48)
    bool                                  roofed     = false; // no room above a wheel in the last suspension pass
    bool                                  settling   = false; // placed: gravity and springs only (car data +0xa4)
    bool                                  keysHeld   = false; // throttle, brake, steering or turbo held
    float                                 slowFor    = 0;     // s slower than SleepSpeed      (car data 0x5c74)
    float                                 calmFor    = 0;     // s turning slower than SleepSpin (0x5c78)
    float                                 sinceTouch = 2e6f;  // s since a wheel or a hit touched the track (0x5c7c)
    float                                 touchFor   = 0;     // s touching it all along (0x5c80)
    float                                 jumpTimer = 0;
    float                                 tank = 0;
    int32_t                               hitCount = 0;
    float                                 boostTimer = 0;     // s since the start or since the tank ran dry
    bool                                  boostLock = false;  // ran dry with the key held
    bool                                  boostOn = false;
    Surface                               under = Surface::Default;
    float                                 sub   = 0;
    float                                 wet   = 0;          // share of the wheels in water, 0..1
    float                                 underWater = 0;     // drowning timer, s (see CarParams::drownTime)

    Hit                                   lastHit   = Hit::None;
    Contact                               touch;
    bool                                  carBumped = false;
    bool                                  carBefore = false;  // touched a car in the update before
    uint8_t                               groundSnd = 0;
    bool                                  groundedNow = false;
    float                                 sinceProp = 1e3f;   // s since the last hit on a prop
    bool                                  propHit   = false;  // a prop hit since the last update
    bool                                  carNow    = false;  // a car touched it since the last update
    bool                                  objectNow = false;  // a prop touched it (takeObjectTouch)
  };

template<class A, class B>
void Vehicle::exchange(A& a, B& b, const Vec3& at, const Vec3& n) {
  constexpr float StillSpeed = 0.02f;   // m/s (0x55458c)
  const Vec3 ra = at-a.centre(), rb = at-b.centre();
  for(int pass=0; pass<10; ++pass) {
    float vn = Vec3::dot(a.velocity()-b.velocity(),n);   // 0x4f04e0
    if(vn>0 && vn<1.f)
      vn = 0;
    if(pass>0 && vn>StillSpeed)
      break;
    const float dv = std::max(-2.f*vn,0.2f);
    const float k  = a.invMass()+Vec3::dot(Vec3::cross(a.invInertia(Vec3::cross(ra,n)),ra),n)
                   + b.invMass()+Vec3::dot(Vec3::cross(b.invInertia(Vec3::cross(rb,n)),rb),n);
    if(k<1e-6f)
      return;
    const Vec3 j = n*(dv/k);
    a.applyImpulse(j,at);
    b.applyImpulse(j*-1.f,at);
    }
  }

}
