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
#include <optional>

#include "arrow.h"
#include "formats/fog.h"
#include "camera.h"
#include "chasecamera.h"
#include "effects.h"
#include "fpscounter.h"
#include "hud.h"
#include "menu.h"
#include "players.h"
#include "physics/aiemu.h"
#include "physics/characters.h"
#include "physics/traffic.h"
#include "physics/checkpoints.h"
#include "physics/opponents.h"
#include "physics/ghosts.h"
#include "physics/race.h"
#include "physics/resetvolumes.h"
#include "physics/vehicle.h"
#include "resources.h"
#include "sounds.h"
#include "world/characters.h"
#include "world/traffic.h"
#include "world/carshadow.h"
#include "world/sunview.h"
#include "world/antennaview.h"
#include "physics/antenna.h"
#include "fx/lensflare.h"
#include "fx/carlight.h"
#include "world/carmodel.h"
#include "world/modelmesh.h"
#include "world/level.h"
#include "world/objects.h"

class MainWindow : public Tempest::Window {
  public:
    // `dev`: the -dev switch, RCCars.exe's developer mode (0x147d204); our debug keys work only with it.
    // `level`: straight into a race of that track, without the menus; empty: the main menu
    // `inWindow`: a 800x600 window like the original's menu in a window, otherwise full screen (without changing
    // the screen's resolution, unlike RCCars.exe 0x4a6840, which shows its menus in 800x600)
    MainWindow(Tempest::Device& device, Resources& res, const std::string& level, bool dev = false,
               bool inWindow = false);
    ~MainWindow() override;

  private:
    enum { MaxFramesInFlight = 2 };

    void render() override;
    void resizeEvent    (Tempest::SizeEvent&  e) override;
    void keyDownEvent   (Tempest::KeyEvent&   e) override;
    void keyUpEvent     (Tempest::KeyEvent&   e) override;
    void mouseDownEvent (Tempest::MouseEvent& e) override;
    void mouseUpEvent   (Tempest::MouseEvent& e) override;
    void mouseMoveEvent (Tempest::MouseEvent& e) override;
    void mouseDragEvent (Tempest::MouseEvent& e) override;
    void mouseWheelEvent(Tempest::MouseEvent& e) override;

    void resetSwapchain();
    // the menus: what the player chose, and the front end drawn instead of the race
    void menuCommand(Menu::Command c);
    // the race of the menu's Setup, from the loading screen
    void startRace();
    void renderMenu();
    // one frame of the loading screen
    void presentLoading(size_t track, float progress);
    // the turning car of the quick race in the rectangle Menu::carPreview gives
    void drawCarPreview(Tempest::Encoder<Tempest::CommandBuffer>& enc, const Menu::Preview& p);
    void tick(float dt);
    void loadLevel(const std::string& name);
    void loadCar(size_t id);
    void fitCar();                              // the car's model with its upgrades
    static ModelMesh::CarLook aiLook(const rc::Opponent& o);
    void respawnCar();    // back to the last checkpoint passed (R, drowned, out of the track, reset volume)
    // the car on the start line and the race from the countdown; `again`: "Jeszcze raz", no delay before "3"
    void setupPhysics(bool restartRace = true, bool again = false);
    void shadeCars(float dt);                   // carShade, aiShades from the lightmaps under the cars
    void tickAntennas(float dt);                // the cars' springy antennas
    void setupOpponents();                      // the opponents of the track for aiLevel
    // the ghost race's ghosts (raceMode Ghost, MultiGhost), none in the other races; those of the race before
    // written first (Multi)
    void setupGhosts();
    void saveGhosts();
    // the ghost race's "Wróć na start" and R (RCCars.exe 0x4f69e0): the car at the start, the lap from the beginning
    void ghostBackToStart();
    // the model of a car that looks so, made the first time it is asked for
    CarModel& carModel(const rc::Ghosts::Look& look);
    // engines, checkpoint voice: once per frame after the physics
    void tickSounds(float dt);
    static std::vector<rc::Mat4> partPoses(const ModelMesh& mesh, const rc::CarGeometry& geom, const rc::CarRig::Pose& pose);
    std::vector<rc::Mat4> carPartPoses(const ModelMesh& mesh) const;
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
    // RCCars.exe 0x482900: what the "ZERO" keys ask for this frame, in the moved thing's own axes (+X left, +Y up,
    // +Z forward), degrees (yaw positive = left, pitch positive = nose down); flies the car (F9) and the ZERO camera
    struct ZeroInput {
      rc::Vec3 step;
      float    yaw = 0, pitch = 0;
      bool     level = false;
      };
    ZeroInput             zeroInput(float dt) const;
    // the race's news on the chat console (RCCars.exe 0x4ad140, Language/Game/*.tbl 20102 ..): `car` -1 is the
    // player, otherwise the opponent
    void                  tell(uint32_t id, int car, std::string_view what = {});
    // an opponent's name as the game shows it
    std::string           opponentName(size_t i) const;
    // the cars of the finish table, as they stand now
    std::vector<Menu::FinishRow> finishRows() const;
    void                  tellNews(float dt, const std::vector<rc::Characters::Car>& others);
    // the developer mode's typed codes (RCCars.exe 0x47a660 / 0x47a6b0), see DevCode
    bool                  devCodeKey(Tempest::Event::KeyType key);
    void                  devCode(const std::string& code);
    void                  tickZeroCamera(float dt);

    Tempest::Device&        device;
    Resources&              resources;
    Tempest::Swapchain      swapchain;
    Tempest::ZBuffer        zbuffer;

    std::unique_ptr<Level>  level;
    rc::SbScene             carScene;       // RCCarsDB/Car.sb, all cars
    rc::ObjectLibrary       library;        // stone.sb ...: models of the objects placed on tracks
    std::unique_ptr<CarModel> car;
    CarModel::Distances     lodDistances;
    size_t                  carId    = 0;   // 0..2: Car1..Car3
    // the player's car as it is in the player's file: the upgrade levels 0..3 (Scripts/upgrades.ini columns) and
    // the skin 0..3
    int32_t                 boostLevel = 0, resonatorLevel = 0, tiresLevel = 0, skin = 0;
    Menu::Mode              raceMode = Menu::Mode::Quick;
    rc::Mat4                carPose;
    // declared after `level`: the physics world refers to the level's collision mesh
    std::unique_ptr<rc::PhysicsWorld> physics;
    std::unique_ptr<Objects>          objects; // props: bodies in `physics`
    std::unique_ptr<rc::Characters>   characters; // people and animals: bodies in `physics`
    std::unique_ptr<CharacterView>    characterView;
    std::unique_ptr<rc::Traffic>      traffic;    // road cars: bodies in `physics`
    std::unique_ptr<TrafficView>      trafficView;
    std::unique_ptr<CarShadow>        carShadow;
    std::unique_ptr<SpotView>         carSpots;
    std::vector<SpotView::Spot>       carSpotList;
    // how the cars are lit (carLight.ini, car_lmshd.ini): the level's lightmaps under them make them darker
    rc::fx::CarLight                       carLight;
    std::unique_ptr<rc::fx::LightmapProbe> lightProbe;
    rc::fx::CarShade                       carShade;
    std::vector<rc::fx::CarShade>          aiShades;
    std::unique_ptr<rc::Vehicle>      vehicle;
    std::unique_ptr<rc::Checkpoints>  checkpoints;
    std::unique_ptr<rc::Race>         race;
    rc::ResetVolumes                  resetVolumes; // refers to the level's scene
    rc::Fog                           fog;
    // declared after `checkpoints` and `physics`: they refer to both
    std::unique_ptr<rc::Opponents>    opponents;
    rc::Opponents::Level              aiLevel = rc::Opponents::Level::Normal; // O: off, easy, normal, hard
    std::map<std::array<int32_t,5>,std::unique_ptr<CarModel>> aiMeshes; // by car, skin and upgrades
    std::unique_ptr<rc::Ghosts>       ghosts;    // the ghost race's
    std::vector<rc::AiCarSound>       ghostSounds;
    float                             playerFinish = -1;  // race time of the player's finish
    bool                              finishShown  = false; // the finish table opened (3 s after it)
    float                             odometer = 0;       // the way the player's car went this race (car+0x5d38)
    bool                              devFlew  = false;   // F9 or N this race: its times are not the player's
    rc::AiEmu                         aiEmu;              // what the opponents seem to run into
    // s since each car's last "kochaś zderzaków" (RCCars.exe car+0x5704): the player's, then the opponents'
    std::vector<float>                sinceBumper;
    int                     laps     = rc::Race::DefaultLaps; // L: 3, 5 or 7 as in the original
    bool                    paused   = false;
    rc::Vec3                lastCarPos;     // for the car's velocity
    std::unique_ptr<Hud>    hud;
    std::unique_ptr<FpsCounter> fps;   // ShowFPS, F12
    std::unique_ptr<Menu>   menu;
    std::unique_ptr<Players> players;   // Players\ and the one playing
    bool                    raceRequested = false;  // "Wyścig": the loading screen at the next frame
    std::array<std::unique_ptr<CarModel>,3> previewCars; // the menus' car, by car
    std::array<std::array<int32_t,4>,3>     previewLook;  // and its upgrades and skin

    std::unique_ptr<DirectionArrow> arrow;
    std::unique_ptr<Effects>        effects;    // dust and spray of the cars
    rc::fx::LensFlare::Settings     flareSettings;
    rc::fx::LensFlare               sunFlare;   // the track's sun (SUN_AF) and its lens flare
    std::unique_ptr<SunView>        sunView;
    std::unique_ptr<AntennaView>    antennaView;
    std::array<rc::Antenna::Settings,3> antennaSettings;
    std::optional<rc::Antenna>      carAntenna; // the player's
    std::vector<std::optional<rc::Antenna>> aiAntennas; // one per opponent
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
    bool                    shotSound  = false; // a bullet hit the car since the last tickSounds (car_cdt_bullet)
    // RCCars.exe's developer mode (-dev, 0x147d204) and its F9 (0x147d254, the "ZERO" mode of 0x482900): the
    // player's car leaves the physics and is flown by hand, the camera rigid behind it, and nothing puts the car
    // back on the track
    const bool              devMode;
    bool                    flying     = false;
    rc::Mat4                flightPose;     // the flown car, model -> world
    // RCCars.exe 0x4822c0: section "Debug", ZeroSpeed 100 and ZeroAngle 90 unless the scripts set them
    // (Autoexec.gm: "Set ZeroSpeed 15")
    float                   zeroSpeed  = 100.f; // m/s
    float                   zeroAngle  = 90.f;  // degrees/s
    float                   fixedTime  = 0;    // ms, FixedTime: the step of F (0: the last frame's)
    // Codes typed during the game in the developer mode (RCCars.exe 0x47a660 registers them, 0x47a6b0 checks each
    // key): letters and digits in a row, any case, at most 2 s apart (0x47a7a0); a wrong key starts the code over
    struct DevCode {
      const char* text;
      uint8_t     at   = 0;  // letters matched so far
      uint64_t    last = 0;  // ms, the last one
      };
    std::vector<DevCode>    devCodes;
    // the "ZERO" camera of the codes I, F, E1, E2 (0x482cd0, 0x482eb0), moved like the F9 car (0x482900)
    enum class ZeroCam : uint8_t {
      Off,
      Fly,       // E1: the view's camera flown by hand, the game goes on; C1 gives the race camera back
      Still,     // E2: the view without a camera driver (0x487320 with 0): it stays where it is
      Detached,  // I: a camera of its own and the game stopped (0x4411e0); F runs one frame, I again goes back
      };
    ZeroCam                 zeroCam     = ZeroCam::Off;
    bool                    frameStep   = false; // F: one frame of the stopped game
    float                   lastDt      = 1.f/60.f;
    int                     forcedDirt  = -1;    // dt0..dt5 (0x532490): the car's dirt held at that level, dtx: -1
    Camera                  camera;
    ChaseCamera             chase;          // the camera behind the car (Settings\Camera.ini)
    bool                    showCollision = false; // K: collision debug overlay (developer mode)
    bool                    showGates     = false; // G: checkpoint gates outlined (developer mode)

    Tempest::CommandBuffer  commands[MaxFramesInFlight];
    Tempest::Fence          fence[MaxFramesInFlight];
    uint8_t                 frameId = 0;

    std::array<bool,256>    keys = {};
    // the car's keys, the frame rate's limit (0 none) and the cockpit's parts, as RCCars.crs has them
    Controls                controls;
    int32_t                 fpsLimit  = 0;
    uint64_t                frameDue  = 0;
    bool                    showMap   = true, showArrow = true;
    // RCCars.crs read again: the options the game follows
    void                    applyOptions();
    bool                    held(Controls::Action a) const { return controls.held(a,keys.data()); }
    uint64_t                lastTick = 0;
    float                   time     = 0; // seconds since start, animates the water
  };
