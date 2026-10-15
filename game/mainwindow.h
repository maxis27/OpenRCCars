#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Event>
#include <Tempest/Fence>
#include <Tempest/Swapchain>
#include <Tempest/Window>
#include <Tempest/ZBuffer>

#include <array>
#include <map>
#include <memory>

#include "arrow.h"
#include "formats/fog.h"
#include "camera.h"
#include "chasecamera.h"
#include "effects.h"
#include "hud.h"
#include "physics/characters.h"
#include "physics/traffic.h"
#include "physics/checkpoints.h"
#include "physics/opponents.h"
#include "physics/race.h"
#include "physics/resetvolumes.h"
#include "physics/vehicle.h"
#include "resources.h"
#include "sounds.h"
#include "world/characters.h"
#include "world/traffic.h"
#include "world/modelmesh.h"
#include "world/level.h"
#include "world/objects.h"

class MainWindow : public Tempest::Window {
  public:
    // `dev`: the -dev switch, RCCars.exe's developer mode (0x147d204); our debug keys work only with it
    MainWindow(Tempest::Device& device, Resources& res, const std::string& level, bool dev = false);
    ~MainWindow() override;

  private:
    enum { MaxFramesInFlight = 2 };

    void render() override;
    void resizeEvent    (Tempest::SizeEvent&  e) override;
    void keyDownEvent   (Tempest::KeyEvent&   e) override;
    void keyUpEvent     (Tempest::KeyEvent&   e) override;
    void mouseDownEvent (Tempest::MouseEvent& e) override;
    void mouseDragEvent (Tempest::MouseEvent& e) override;
    void mouseWheelEvent(Tempest::MouseEvent& e) override;

    void resetSwapchain();
    void tick(float dt);
    void loadLevel(const std::string& name);
    void loadCar(size_t id);
    void respawnCar();    // back to the last checkpoint passed (R, drowned, out of the track, reset volume)
    // the car on the start line and the race from the countdown; `again`: "Jeszcze raz", no delay before "3"
    void setupPhysics(bool restartRace = true, bool again = false);
    void setupOpponents();                      // the opponents of the track for aiLevel
    // engines, checkpoint voice: once per frame after the physics
    void tickSounds(float dt);
    static std::vector<rc::Mat4> partPoses(const ModelMesh& mesh, const rc::CarGeometry& geom, const rc::CarRig::Pose& pose);
    std::vector<rc::Mat4> carPartPoses() const;
    std::vector<rc::Mat4> opponentPartPoses(const ModelMesh& mesh, const rc::Opponent& o) const;
    Hud::Info             hudInfo() const;
    // the bursts fired in this update and the cars they can hit (fx::Gunfire)
    std::vector<rc::fx::Gunfire::Order>  gunOrders()  const;
    std::vector<rc::fx::Gunfire::Target> gunTargets() const;
    ChaseCamera::Car      chaseCar() const;
    // the way on for the arrow: the point it shows, relative to the car's heading, radians, positive = left
    float                 wayDirection() const;
    DirectionArrow::Input arrowInput() const;
    // F9 in the developer mode: the car flown by hand, see `flying`
    void                  setFlying(bool on);
    void                  fly(float dt);

    Tempest::Device&        device;
    Resources&              resources;
    Tempest::Swapchain      swapchain;
    Tempest::ZBuffer        zbuffer;

    std::unique_ptr<Level>  level;
    rc::SbScene             carScene;       // RCCarsDB/Car.sb, all cars
    rc::ObjectLibrary       library;        // stone.sb ...: models of the objects placed on tracks
    std::unique_ptr<ModelMesh> car;
    size_t                  carId    = 0;   // 0..2: Car1..Car3
    int32_t                 upgrade  = 0;   // 0..3, Scripts/upgrades.ini column
    rc::Mat4                carPose;
    // declared after `level`: the physics world refers to the level's collision mesh
    std::unique_ptr<rc::PhysicsWorld> physics;
    std::unique_ptr<Objects>          objects; // props: bodies in `physics`
    std::unique_ptr<rc::Characters>   characters; // people and animals: bodies in `physics`
    std::unique_ptr<CharacterView>    characterView;
    std::unique_ptr<rc::Traffic>      traffic;    // road cars: bodies in `physics`
    std::unique_ptr<TrafficView>      trafficView;
    std::unique_ptr<rc::Vehicle>      vehicle;
    std::unique_ptr<rc::Checkpoints>  checkpoints;
    std::unique_ptr<rc::Race>         race;
    rc::ResetVolumes                  resetVolumes; // refers to the level's scene
    rc::Fog                           fog;
    // declared after `checkpoints` and `physics`: they refer to both
    std::unique_ptr<rc::Opponents>    opponents;
    rc::Opponents::Level              aiLevel = rc::Opponents::Level::Normal; // O: off, easy, normal, hard
    std::map<std::pair<int32_t,int32_t>,std::unique_ptr<ModelMesh>> aiMeshes; // by car and skin
    float                             playerFinish = -1;  // race time of the player's finish
    int                     laps     = rc::Race::DefaultLaps; // L: 3, 5 or 7 as in the original
    bool                    paused   = false;
    rc::Vec3                lastCarPos;     // for the car's velocity
    std::unique_ptr<Hud>    hud;
    std::unique_ptr<DirectionArrow> arrow;
    std::unique_ptr<Effects>        effects;    // dust and spray of the cars
    std::unique_ptr<Sounds> sounds;
    std::unique_ptr<rc::CarSound>   carSound;   // the player's engine
    std::vector<rc::AiCarSound>     aiSounds;   // one per opponent
    float                   airborne   = 0;     // s without a wheel on the ground
    bool                    wasWrongWay = false;
    size_t                  besideTarget = 0;   // cp_beside: the checkpoint watched,
    float                   besideMin    = 1e9f; // how close the car came to it
    bool                    besideDone   = false;
    bool                    jumpPressed = false;
    float                   upsideDown = 0; // seconds on the roof or side, then the car is put back
    float                   shotTime   = -1; // seconds since a bullet hit the car (held, then put back), < 0 none
    // RCCars.exe's developer mode (-dev, 0x147d204) and its F9 (0x147d254, the "ZERO" mode of 0x482900): the
    // player's car leaves the physics and is flown by hand, the camera rigid behind it, and nothing puts the car
    // back on the track
    const bool              devMode;
    bool                    flying     = false;
    rc::Mat4                flightPose;     // the flown car, model -> world
    float                   zeroSpeed  = 15.f; // m/s: "Set ZeroSpeed 15" in Autoexec.gm, 100 without it
    float                   zeroAngle  = 90.f; // degrees/s: ZeroAngle, Autoexec.gm does not set it
    Camera                  camera;
    ChaseCamera             chase;          // the camera behind the car (Settings\Camera.ini)
    bool                    freeCamera = false; // V (developer mode)
    bool                    showCollision = false; // K: collision debug overlay (developer mode)
    bool                    showGates     = false; // G: checkpoint gates outlined (developer mode)

    Tempest::CommandBuffer  commands[MaxFramesInFlight];
    Tempest::Fence          fence[MaxFramesInFlight];
    uint8_t                 frameId = 0;

    std::array<bool,256>    keys = {};
    Tempest::Point          mousePos;
    float                   speed    = 8.f; // m/s
    uint64_t                lastTick = 0;
    float                   time     = 0; // seconds since start, animates the water
  };
