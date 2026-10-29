#include "mainwindow.h"

#include <Tempest/Application>
#include <Tempest/SystemApi>
#include <Tempest/Except>
#include <Tempest/Log>

#include "formats/gamescript.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <thread>
#include <limits>
#include <sstream>
#include <string>

using namespace Tempest;

static const char* const Cars[] = {"Car1", "Car2", "Car3"}; // Overkill, Buggy, Hummer

static const char* const Tracks[] = {
  "beach_1",  "beach_2",  "beach_3",  "beach_4",
  "country_1","country_2","country_3","country_4",
  "urban_1",  "urban_2",
  };

// every texture the three cars may wear, as RCCars.exe reads them at the start (game_dbg.log: car_askin11..34,
// car_bskin, tire2_*, tire3_*, turbo_*, overkill_*, hum_turbo_*, cubemap_racer before the menus): those of Car.sb's
// models, the other liveries and tire levels ModelMesh swaps in, and the dirt laid over the paint
static std::vector<std::string> carTextures(const rc::SbScene& sb) {
  std::vector<std::string> ret;
  for(auto& t:sb.textures)
    if(t.embedded.empty())
      ret.push_back(t.name);
  for(char car='1'; car<='3'; ++car) {
    for(char skin='1'; skin<='4'; ++skin)
      for(const char* paint:{"car_askin","car_bskin"})
        ret.push_back(std::string(paint)+car+skin);
    for(char n='1'; n<='2'; ++n) {
      ret.push_back(std::string("dirt_car")+car+"_"+n);
      ret.push_back(std::string("dirt_car")+car+"_"+n+"a");
      ret.push_back(std::string("dirt_car")+car+"_"+n+"b");
      }
    }
  for(char level='1'; level<='4'; ++level)
    for(const char* tire:{"tire2_","tire3_"})
      ret.push_back(std::string(tire)+level);
  ret.push_back("cubemap_racer");
  return ret;
  }

// `Set <name> <value>` of a script next to RCCars.exe (Autoexec.gm, debug.gm), as a number; `def` when it is not
// there
static float scriptValue(const Resources& res, const char* file, const std::string& name, float def) {
  try {
    const auto        d = res.readFile(file);
    std::istringstream in(std::string(d.begin(),d.end()));
    std::string        line;
    while(std::getline(in,line)) {
      std::istringstream l(line);
      std::string        cmd, key;
      float              v = 0;
      if(!(l>>cmd>>key) || cmd!="Set")
        continue;
      if(key.size()>=2 && key.front()=='"' && key.back()=='"')
        key = key.substr(1,key.size()-2);
      if(key==name && (l>>v))
        def = v;
      }
    }
  catch(const std::exception&) {
    }
  return def;
  }

MainWindow::MainWindow(Device& device, Resources& res, const std::string& name, bool dev, bool inWindow)
  : Window(inWindow ? Normal : FullScreen), device(device), resources(res), swapchain(device,hwnd()),
    library([&res](const std::string& path){ return res.readFile(path); }), devMode(dev) {
  setWindowTitle("OpenRCCars");
  resetSwapchain();
  sounds   = std::make_unique<Sounds>(res);
  intro    = std::make_unique<Intro>(device,res);
  // RCCars.exe 0x4a2ae4 step 0x0f: the intro, not in the developer mode (0x147d204), nor when AutoRunIntro is 0 (the
  // game registers it as 1, 0x4902a2, Config.gm sets it to 1); not when going straight into a race either (ours)
  float autoRun = 1;
  for(const char* f:{"Config.gm","Autoexec.gm"})
    autoRun = scriptValue(res,f,"AutoRunIntro",autoRun);
  if(!devMode && name.empty() && autoRun!=0) {
    applyOptions();
    if(intro->start(*sounds)) {
      lastTick = Application::tickCount();
      return;
      }
    }
  init(name);
  }

void MainWindow::init(const std::string& name) {
  Resources& res = resources;
  // the start screen while the game loads (0x4a2ae4 step 0x13)
  presentStart();
  carScene = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
  res.preload(carTextures(carScene));
  hud      = std::make_unique<Hud>(device,res);
  fps      = std::make_unique<FpsCounter>(device,res);
  arrow    = std::make_unique<DirectionArrow>(device,res);
  effects  = std::make_unique<Effects>(device,res);
  effects->setQuality(quality);
  sunView  = std::make_unique<SunView>(device,res);
  antennaView = std::make_unique<AntennaView>(device,res,carScene);
  for(size_t i=0; i<antennaSettings.size(); ++i)
    antennaSettings[i] = rc::Antenna::Settings::load(res.pack(),i);
  flareSettings = rc::fx::LensFlare::Settings::load([&res](const std::string& f){ return res.pack().read(f); });
  menu     = std::make_unique<Menu>(device,res,*sounds);
  {
    const auto name = menu->gameText(40507);
    players = std::make_unique<Players>(res,name.empty() ? std::string_view("Gracz") : name);
    menu->setPlayers(players.get());
    // 0x4e8440: the one playing brings the race raced last
    if(auto* p = players->current()) {
      auto& st = menu->setup();
      st.track = size_t(std::clamp(p->lastTrack,0,int(Menu::TrackCount)-1));
      st.car   = size_t(std::clamp(p->lastCar,0,2));
      st.laps  = std::clamp(p->lastLaps|1,3,7);
      st.skill = std::clamp(p->lastSkill,0,2);
      }
  }
  characterView = std::make_unique<CharacterView>(device);
  trafficView   = std::make_unique<TrafficView>(device,res);
  carShadow     = std::make_unique<CarShadow>(res);
  carSpots      = std::make_unique<SpotView>(device);
  carLight      = rc::fx::CarLight::load(res.pack());
  chase    = ChaseCamera(ChaseCamera::Settings::load([&res](const std::string& f){ return res.pack().read(f); }));
  lodDistances = CarModel::Distances::load(res.pack());
  try {
    const auto d = res.pack().read("Settings\\AIEmu.ini");
    aiEmu.load(rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size())));
    }
  catch(const std::exception& e) {
    Log::e("Settings\\AIEmu.ini: ",e.what());
    }
  // the developer mode runs debug.gm after Autoexec.gm (0x48b26a), when there is one
  for(const char* f:{"Autoexec.gm","debug.gm"}) {
    if(!devMode && f!=std::string("Autoexec.gm"))
      continue;
    zeroSpeed = scriptValue(res,f,"ZeroSpeed",zeroSpeed);
    zeroAngle = scriptValue(res,f,"ZeroAngle",zeroAngle);
    fixedTime = scriptValue(res,f,"FixedTime",fixedTime);
    }
  // ShowFPS: a variable of the game's scripts (RCCars.exe 0x490383), Config.gm runs before Autoexec.gm
  for(const char* f:{"Config.gm","Autoexec.gm","debug.gm"})
    if(devMode || f!=std::string("debug.gm"))
      fps->visible = scriptValue(res,f,"ShowFPS",fps->visible ? 1.f : 0.f)!=0;
  // the codes registered with -dev (0x480568, 0x482728, 0x4e9cc0, 0x4fcb40, 0x5313a8); burst, road and the
  // first constr only flip flags nothing reads, so they are left out
  if(devMode)
    for(const char* c:{"DT0","DT1","DT2","DT3","DT4","DT5","DTX","CONSTR","C1","F","E1","E2","I","STAT"})
      devCodes.push_back({c});
  setCursorShape(CursorShape::Hidden);   // the menus draw the game's own
  applyOptions();                        // the quality the track is loaded with
  loadCar(0);
  if(name.empty()) {
    menu->open(Menu::Page::Main);
    }
  else {
    // -l: straight into the race, the menu's choice following it
    const size_t t = Menu::trackIndex(name);
    if(t<Menu::TrackCount)
      menu->setup().track = t;
    loadLevel(name);
    }
  menu->setDriverName(device.properties().name);
  applyOptions();
  sounds->startMusic();
  intro.reset();
  lastTick = Application::tickCount();
  }

void MainWindow::applyOptions() {
  std::ifstream f(resources.gameDir()/"RCCars.crs",std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  const auto crs = rc::ConfigFile::parse(data);
  controls.load(crs);
  sounds->setOptions(crs);
  // 0x48f206: the frame rate's limit, a timer of 1000 / limit ms the frames wait for (0x48bc04)
  fpsLimit  = std::max(0,crs.integer("Video","FPS_Limit",0));
  showMap   = crs.integer("Cockpit","ShowMap",1)!=0;
  showArrow = crs.integer("Cockpit","DirectArrow",1)!=0;
  // 0x4c854b: leaving the video options turns RenderQual and SFXQual into the VIDEO_* switches (0x4acdb0)
  quality   = Quality::load(crs);
  if(effects)
    effects->setQuality(quality);
  if(characters)
    characters->setRenderQuality(quality.renderLevel());
  if(level)
    level->mesh().setFeatures(levelFeatures());
  }

LevelMesh::Features MainWindow::levelFeatures() const {
  LevelMesh::Features f;
  f.lightMaps = quality.lightMaps;
  f.envMap    = quality.envMap;
  f.coast     = quality.waterCoast;
  f.detail    = quality.detailer;
  return f;
  }

MainWindow::~MainWindow() {
  intro.reset();   // closed during the intro: its sound off while `sounds` is there
  saveGhosts();
  device.waitIdle();
  }

void MainWindow::loadLevel(const std::string& name) {
  device.waitIdle();
  try {
    auto next = std::make_unique<Level>(device,resources,name,quality.renderLevel());
    next->mesh().setFeatures(levelFeatures());
    vehicle.reset();
    opponents.reset();
    traffic.reset();
    characters.reset();
    objects.reset();
    physics.reset();
    race.reset();
    checkpoints.reset();
    level   = std::move(next);
    checkpoints = std::make_unique<rc::Checkpoints>(level->scene(),level->collision());
    lightProbe  = std::make_unique<rc::fx::LightmapProbe>(level->scene(),level->collision(),
                                                         [this](const std::string& f){ return resources.pack().read(f); });
    carShade.reset();
    aiShades.clear();
    race        = std::make_unique<rc::Race>(*checkpoints,laps);
    resetVolumes = rc::ResetVolumes(level->scene());
    fog.setTarget(rc::FogParams::load(resources.pack(),name));
    hud->setTrack(name);
    sounds->setTrack(level->scene(),level->mesh().seaCoast());
    effects->setTrack(level->scene(),level->mesh().mainSea());
    if(auto sun = level->scene().marker("SUN_AF"))
      sunFlare = rc::fx::LensFlare(flareSettings,sun->position());
    else
      sunFlare = rc::fx::LensFlare();
    camera  = level->startCamera();
    setupPhysics();
    }
  catch(const std::exception& e) {
    Log::e("unable to load track \"",name,"\": ",e.what());
    if(level==nullptr)
      throw; // nothing to show at all
    }
  }

void MainWindow::loadCar(size_t id) {
  device.waitIdle();
  carId = id;
  fitCar();
  if(carSound)
    carSound->stop();
  carSound = std::make_unique<rc::CarSound>(sounds->mixer(),sounds->library(),int(id));
  setupPhysics();
  }

void MainWindow::fitCar() {
  ModelMesh::CarLook look;
  look.boost     = boostLevel;
  look.resonator = resonatorLevel;
  look.tires     = tiresLevel;
  look.skin      = skin;
  look.antenna = !antennaView->has(carId);
  carAntenna.reset();
  car = std::make_unique<CarModel>(device,resources,carScene,carId,look);
  }

ModelMesh::CarLook MainWindow::aiLook(const rc::Opponent& o) {
  ModelMesh::CarLook look;
  look.skin      = o.skin;
  look.boost     = o.boost;
  look.resonator = o.reson;
  look.tires     = o.tires;
  return look;
  }

void MainWindow::setupOpponents() {
  for(auto& s:aiSounds)
    s.stop();
  opponents.reset();
  if(physics==nullptr || checkpoints==nullptr)
    return;
  try {
    opponents = std::make_unique<rc::Opponents>(*physics,[this](const std::string& f){ return resources.pack().read(f); },
                                                carScene,level->name(),*checkpoints,aiLevel,
                                                [this](int32_t c){ return rc::CarParams::load(resources.pack(),c); },
                                                raceMode==Menu::Mode::Demo);
    }
  catch(const std::exception& e) {
    Log::e("opponents: ",e.what());
    return;
    }
  opponents->setTrack(level->scene());
  for(size_t i=0; i<opponents->size(); ++i) {
    auto& o   = (*opponents)[i];
    auto& m   = aiMeshes[{o.car,o.skin,o.boost,o.reson,o.tires}];
    if(m==nullptr) {
      auto look = aiLook(o);
      look.antenna = !antennaView->has(size_t(o.car));
      m = std::make_unique<CarModel>(device,resources,carScene,size_t(o.car),look);
      }
    }
  // the channels stay in the mixer, so they are made once and reused
  while(aiSounds.size()<opponents->size())
    aiSounds.emplace_back(sounds->mixer(),sounds->library());
  Log::i("opponents (",rc::Opponents::levelName(aiLevel),"): ",opponents->size());
  }

void MainWindow::setupGhosts() {
  saveGhosts();
  ghosts.reset();
  for(auto& s:ghostSounds)
    s.stop();
  const bool normal = raceMode==Menu::Mode::Ghost;
  if(level==nullptr || (!normal && raceMode!=Menu::Mode::MultiGhost))
    return;
  // the track's folder of GhostRecords\ (Config.gm GhostPath, AIRace<n> GhostPath of Scripts\ailayouts.ini)
  const int32_t track = rc::aiRaceNumber(level->name());
  std::string   sub   = "track"+std::to_string(track);
  try {
    const auto d = resources.pack().read("Scripts\\ailayouts.ini");
    const auto l = rc::AiLayout::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    if(auto it = l.races.find(track); it!=l.races.end() && !it->second.ghostPath.empty())
      sub = it->second.ghostPath;
    }
  catch(const std::exception& e) {
    Log::e("ghosts: ",e.what());
    }
  rc::Ghosts::Look look;
  look.car       = int32_t(carId);
  look.boost     = boostLevel;
  look.resonator = resonatorLevel;
  look.tires     = tiresLevel;
  look.skin      = skin;
  ghosts = std::make_unique<rc::Ghosts>(normal ? rc::Ghosts::Kind::Normal : rc::Ghosts::Kind::Multi,look,
                                        resources.gameDir()/"GhostRecords"/sub,track,
                                        [this](int32_t c, rc::CarGeometry& g, rc::CarParams& p) {
                                          g = rc::carGeometry(carScene,Cars[std::clamp(c,0,2)]);
                                          p = rc::CarParams::load(resources.pack(),c+1);
                                          },
                                        [this](int32_t c) { return std::string(hud->gameText(uint32_t(40220+c))); });
  // every look a ghost may take in this race, now: built at the end of a lap, it held up that frame
  for(size_t i=0; i<ghosts->size(); ++i)
    carModel((*ghosts)[i].look);
  carModel(ghosts->ownLapLook());
  while(ghostSounds.size()<ghosts->size())
    ghostSounds.emplace_back(sounds->mixer(),sounds->library());
  Log::i("ghosts (",normal ? "normal" : "multi","): ",ghosts->size());
  }

void MainWindow::saveGhosts() {
  if(ghosts && !ghosts->save())
    Log::e("ghosts: can't write the laps to GhostRecords");
  }

void MainWindow::ghostBackToStart() {
  if(level==nullptr || vehicle==nullptr || car==nullptr || ghosts==nullptr)
    return;
  setFlying(false);
  carPose = level->carStart(car->full().bboxMin().y);
  vehicle->reset(carPose);
  // ours from the countdown, as the lap begins with "Jazda!" here (our tracks have no line to cross at the start)
  race->restart(carPose,true);
  ghosts->restart();
  effects->clear(true);
  besideDone = true;
  lastCarPos = carPose.translation();
  upsideDown = 0;
  shotTime   = -1;
  resetVolumes.reset();
  chase.reset(chaseCar());
  }

CarModel& MainWindow::carModel(const rc::Ghosts::Look& l) {
  auto& m = aiMeshes[{l.car,l.skin,l.boost,l.resonator,l.tires}];
  if(m==nullptr) {
    ModelMesh::CarLook look;
    look.skin      = l.skin;
    look.boost     = l.boost;
    look.resonator = l.resonator;
    look.tires     = l.tires;
    look.antenna   = !antennaView->has(size_t(l.car));
    device.waitIdle();
    m = std::make_unique<CarModel>(device,resources,carScene,size_t(std::clamp(l.car,0,2)),look);
    }
  return *m;
  }

void MainWindow::setupPhysics(bool restartRace, bool again) {
  if(level==nullptr || car==nullptr)
    return;
  carPose = level->carStart(car->full().bboxMin().y);
  vehicle.reset();
  if(physics==nullptr) {
    physics = std::make_unique<rc::PhysicsWorld>(level->collision());
    objects = std::make_unique<Objects>(device,resources,library,level->scene(),*physics);
    try {
      characters = std::make_unique<rc::Characters>(*physics,library,level->scene(),level->name(),
                                                    [this](const std::string& f){ return resources.pack().read(f); });
      characters->setRenderQuality(quality.renderLevel());
      Log::i("characters: ",characters->size());
      characterView->preload(resources,*characters);
      }
    catch(const std::exception& e) {
      Log::e("characters: ",e.what());
      }
    try {
      traffic = std::make_unique<rc::Traffic>(*physics,library,level->scene(),level->name(),
                                              [this](const std::string& f){ return resources.pack().read(f); });
      Log::i("road cars: ",traffic->size());
      trafficView->preload(*traffic);
      }
    catch(const std::exception& e) {
      Log::e("road cars: ",e.what());
      }
    }
  if(opponents==nullptr)
    setupOpponents();
  // with opponents the player starts on their grid slot (PlayerPath), next to them
  rc::Mat4 grid;
  if(opponents!=nullptr && opponents->playerStart(grid))
    carPose = grid;
  const auto geom = rc::carGeometry(carScene,Cars[carId]);
  const auto prm  = rc::CarParams::load(resources.pack(),int32_t(carId+1),boostLevel,resonatorLevel,tiresLevel);
  vehicle  = std::make_unique<rc::Vehicle>(*physics,geom,prm,carPose);
  flying   = false;
  arrow->reset();
  effects->clear();
  shotTime = -1;
  if(restartRace) {
    race->restart(carPose,again);
    sounds->stopAll();
    if(carSound)
      carSound->stop();
    wasWrongWay = false;
    besideDone  = true;
    if(opponents)
      opponents->restart();
    if(ghosts)
      ghosts->restart();
    if(characters)
      characters->restart();
    if(traffic)
      traffic->restart();
    playerFinish = -1;
    finishShown  = false;
    odometer     = 0;
    devFlew      = false;
    paused       = false;
    aiEmu.restart();
    sinceBumper.clear();
    }
  lastCarPos = carPose.translation();
  resetVolumes.reset();
  chase.reset(chaseCar());
  Log::i("car ",Cars[carId],", upgrades ",boostLevel,"/",resonatorLevel,"/",tiresLevel,": top speed ",prm.topSpeed*3.6f," km/h, grip ",prm.gripFront);
  }

void MainWindow::respawnCar() {
  if(level==nullptr || vehicle==nullptr)
    return;
  carPose = checkpoints->respawn(car->full().bboxMin().y);
  vehicle->reset(carPose);
  effects->clear(true);
  sounds->play("Checkpoint","cp_reset");
  besideDone = true;
  if(opponents)
    opponents->playerMoved();
  lastCarPos = carPose.translation();
  upsideDown = 0;
  shotTime   = -1;
  resetVolumes.reset();
  chase.reset(chaseCar());
  }

std::vector<rc::fx::Gunfire::Order> MainWindow::gunOrders() const {
  // the guards' and the BTR's bursts (message 0x261); cars: 0 the player's, 1.. the opponents
  std::vector<rc::fx::Gunfire::Order> r;
  auto add = [&r](const rc::Vec3& gun, int car) {
    r.push_back({gun,car==rc::Characters::PlayerCar ? size_t(0) : size_t(car)+1});
    };
  if(characters)
    for(auto& s:characters->shooting())
      add(s.gun,s.car);
  if(traffic)
    for(auto& s:traffic->shots())
      add(s.gun,s.car);
  return r;
  }

std::vector<rc::fx::Gunfire::Target> MainWindow::gunTargets() const {
  std::vector<rc::fx::Gunfire::Target> r;
  if(vehicle==nullptr)
    return r;
  rc::fx::Gunfire::Target t;
  t.pose     = vehicle->pose();
  t.velocity = vehicle->velocity();
  t.boxMin   = vehicle->geometry().bodyMin;
  t.boxMax   = vehicle->geometry().bodyMax;
  t.chance   = rc::fx::Gunfire::chanceFor(true,int32_t(carId));
  r.push_back(t);
  for(size_t i=0; opponents && i<opponents->size(); ++i) {
    const auto& o = (*opponents)[i];
    t.pose     = o.pose();
    t.velocity = o.velocity();
    t.boxMin   = o.geometry().bodyMin;
    t.boxMax   = o.geometry().bodyMax;
    t.chance   = rc::fx::Gunfire::chanceFor(false,o.car);
    r.push_back(t);
    }
  return r;
  }

ChaseCamera::Car MainWindow::chaseCar() const {
  ChaseCamera::Car c;
  if(const rc::Opponent* o = demoCar()) {
    // the demo's camera follows its active car (CameraFollow '$CAR' <n>)
    c.pose     = o->pose();
    c.centre   = c.pose.translation();
    c.velocity = o->velocity();
    c.spin     = o->localSpin().length();
    return c;
    }
  c.pose     = carPose;
  // where the car is drawn: the physics' own position runs in fixed steps and would shake the view against the car
  c.centre   = vehicle ? vehicle->drawnCentre() : carPose.translation();
  c.velocity = vehicle ? vehicle->velocity() : rc::Vec3{};
  c.spin     = vehicle ? vehicle->localSpin().length() : 0.f;
  // RCCars.exe 0x500c1d: the boost and the gas keys, held
  const bool control = race && !race->holdCar() && zeroCam==ZeroCam::Off && !flying;
  c.boost    = control && held(Controls::Boost) && held(Controls::Forw);
  // a person picking the car up holds the camera still (RCCars.exe 0x4f3640)
  c.grabbed  = characters && characters->grabbedPlayer();
  c.held     = characters && characters->holdingPlayer();
  return c;
  }

Hud::Info MainWindow::hudInfo() const {
  Hud::Info info;
  info.speed  = std::abs(vehicle->speed())*3.6f;
  info.turbo  = vehicle->boostLevel();
  info.turboMax = vehicle->params().boostTank/100.f;
  info.speedMax = vehicle->params().boostTopSpeed*3.6f;
  info.paused = paused && !menu->active();   // "Pauza" for P, not under the race's menu
  info.lowSignal = resetVolumes.lowSignal();
  info.carPos     = carPose.translation();
  info.carForward = carPose.vector({0,0,1});
  info.ghostRace  = ghosts!=nullptr;
  info.showMap    = showMap;
  info.showArrow  = showArrow;
  if(const rc::Opponent* o = demoCar()) {
    // the cockpit of the demo's car (0x4afdc0 draws the active one's); "Wciśnij <Enter> .." the first 10 s,
    // shown in every odd second of them
    const rc::Mat4 p = o->pose();
    info.demo       = true;
    info.speed      = std::abs(o->speed())*3.6f;
    info.turbo      = 0;
    info.speedMax   = 0;
    info.carPos     = p.translation();
    info.carForward = p.vector({0,0,1});
    info.lap        = int(o->laps())+1;
    const float t   = race->clockTime();
    info.demoHint   = t<10.f && t>1.f && int(t)%2==1;
    for(size_t i=0; i<opponents->size(); ++i)
      if(&(*opponents)[i]!=o) {
        const rc::Mat4 q = (*opponents)[i].pose();
        info.opponents.push_back({q.translation(),q.vector({0,0,1})});
        }
    return info;
    }
  for(size_t i=0; ghosts && i<ghosts->size(); ++i) {
    const auto& g = (*ghosts)[i];
    if(g.visible && !g.lap.empty())
      info.opponents.push_back({g.pose().translation(),g.pose().vector({0,0,1})});
    }
  if(opponents && opponents->size()>0) {
    info.place     = opponents->place(playerFinish);
    for(size_t i=0; i<opponents->size(); ++i) {
      const rc::Mat4 p = (*opponents)[i].pose();
      info.opponents.push_back({p.translation(),p.vector({0,0,1})});
      }
    }
  return info;
  }

float MainWindow::wayDirection() const {
  // the way on seen from the car (RCCars.exe 0x4e8f00, 0x410150), positive = on the left (+X)
  const rc::Vec3 f = carPose.vector({0,0,1});
  if(checkpoints->empty() || std::abs(f.x)+std::abs(f.z)<=1e-3f)
    return 0;
  const rc::Vec3 to = checkpoints->wayOn(carPose.translation());
  if(std::abs(to.x)+std::abs(to.z)<=1e-6f)
    return 0;
  float a = std::atan2(to.x,to.z)-std::atan2(f.x,f.z);
  while(a> 3.14159265f) a -= 6.2831853f;
  while(a<-3.14159265f) a += 6.2831853f;
  return a;
  }

DirectionArrow::Input MainWindow::arrowInput() const {
  DirectionArrow::Input in;
  in.direction = wayDirection();
  in.wrongWay  = race->wrongWay();
  const auto& route = checkpoints->route();
  if(route.empty())
    return in;
  const rc::Vec3 at   = carPose.translation();
  const size_t   next = checkpoints->next();
  in.left     = checkpoints->wayLeft(at);
  in.distance = (route[next].position-at).length();
  in.next     = next;
  return in;
  }

std::vector<rc::Mat4> MainWindow::partPoses(const ModelMesh& mesh, const rc::CarGeometry& geom,
                                            const rc::CarRig::Pose& pose) {
  std::vector<rc::Mat4> ret;
  for(auto& p:mesh.parts()) {
    rc::Mat4 m = pose.part(p.name,p.pivot);
    for(size_t i=0; i<geom.wheels.size() && !p.name.empty(); ++i)
      if(geom.wheels[i].name==p.name)
        m = pose.wheel(i);
    ret.push_back(m);
    }
  return ret;
  }

std::vector<rc::Mat4> MainWindow::carPartPoses(const ModelMesh& mesh) const {
  if(vehicle==nullptr) {
    std::vector<rc::Mat4> ret;
    for(auto& p:mesh.parts())
      ret.push_back(rc::Mat4::translate(p.pivot));
    return ret;
    }
  return partPoses(mesh,vehicle->geometry(),vehicle->rigPose());
  }

std::vector<rc::Mat4> MainWindow::opponentPartPoses(const ModelMesh& mesh, const rc::Opponent& o) const {
  return partPoses(mesh,o.geometry(),o.rigPose());
  }

void MainWindow::resetSwapchain() {
  swapchain.reset();
  zbuffer = device.zbuffer(TextureFormat::Depth32F,w(),h());
  }

void MainWindow::resizeEvent(SizeEvent&) {
  for(auto& f:fence)
    f.wait();
  resetSwapchain();
  }

void MainWindow::keyDownEvent(KeyEvent& e) {
  if(intro!=nullptr) {
    // 0x4a2f10: Esc ends the part of the intro playing
    if(e.key==Event::K_ESCAPE)
      intro->skip();
    return;
    }
  keys[e.key] = true;
  if(devMode && e.key==Event::K_F11)
    Tempest::SystemApi::exit();
  // F12: the frame rate line (ours; the original only has its ShowFPS variable)
  if(e.key==Event::K_F12) {
    fps->visible = !fps->visible;
    return;
    }
  if(menu->active()) {
    menu->keyDown(e.key,e.code);
    return;
    }
  // the demo (0x4c89a0, message 0x8c): Esc back to the main menu (0x48c500(2)), Enter the next demo
  // (NextDemoRace); no race menu and no pause (0x4ca960)
  if(demo.on) {
    if(e.key==Event::K_ESCAPE)
      endDemo();
    else if(e.key==Event::K_Return && !demo.next.empty())
      demoRequested = demo.next;
    return;
    }
  // Esc: the menu over the race (dlgINRACE), the race stopped meanwhile
  if(e.key==Event::K_ESCAPE) {
    if(level!=nullptr) {
      menu->open(Menu::Page::InRace);
      paused = true;
      }
    else
      menu->open(Menu::Page::Main);
    return;
    }
  // a key that goes on with a code already begun belongs to the code, not to our keys ("E1" does not switch the car)
  if(devCodeKey(e.key))
    return;
  const bool shift = keys[Event::K_LShift] || keys[Event::K_RShift];
  // the developer mode's keys of RCCars.exe (0x48c0b5): F9 flies the car, F11 quits at once
  if(devMode && e.key==Event::K_F9 && !shift) {
    if(level && vehicle && !paused)
      setFlying(!flying);
    return;
    }
  // our quick switches of the developer mode, the menus' choices without them:
  // F1..F10 - switch track (F9 flies, Shift+F9 is urban_1)
  if(devMode && e.key>=Event::K_F1 && e.key<=Event::K_F10) {
    const size_t id = size_t(e.key-Event::K_F1);
    if(id<std::size(Tracks))
      loadLevel(Tracks[id]);
    }
  // 1..3 - switch car
  if(devMode && e.key>=Event::K_1 && e.key<=Event::K_3)
    loadCar(size_t(e.key-Event::K_1));
  // U - next upgrade level (boosters, resonators and tires together)
  if(devMode && e.key==Event::K_U) {
    tiresLevel = (tiresLevel+1)%4;
    boostLevel = resonatorLevel = tiresLevel;
    device.waitIdle();
    fitCar();
    const rc::Mat4 at = carPose;
    setupPhysics(false);
    if(vehicle)
      vehicle->reset(at);   // same place, standing still
    }
  if(controls.is(Controls::Jump,e.key) && zeroCam==ZeroCam::Off && !flying)
    jumpPressed = true;
  // our debug keys, only in the developer mode (-dev)
  if(devMode && e.key==Event::K_T && vehicle && !flying)
    vehicle->recover();
  if(devMode && e.key==Event::K_G)
    showGates = !showGates;
  // "Zresetuj" (R): back to the last checkpoint, as in the original
  if(controls.is(Controls::Reset,e.key) && level && !paused) {
    // the ghost race's back to the start (0x4f6b20 -> 0x4f69e0)
    if(ghosts) {
      ghostBackToStart();
      return;
      }
    setFlying(false);
    respawnCar();
    }
  // Enter: the race again from the countdown ("Jeszcze raz"), L: 3, 5 or 7 laps (and the race again)
  if(e.key==Event::K_Return && level)
    setupPhysics(true,true);
  if(devMode && e.key==Event::K_L && level) {
    laps = (laps>=7) ? 3 : laps+2;
    race->setLaps(laps);
    setupPhysics(true,true);
    }
  // N (developer mode): straight to the next checkpoint, to try out laps and the finish
  if(devMode && e.key==Event::K_N && level && vehicle && !checkpoints->empty() && !race->holdCar()) {
    checkpoints->skipWay();
    devFlew = true;
    const auto&    p   = checkpoints->route()[checkpoints->next()];
    const float    yaw = std::atan2(p.dir.x,p.dir.z)*180.f/3.14159265f;
    rc::Mat4       at  = rc::Mat4::translate(p.position)*rc::Mat4::rotate(1,yaw);
    if(auto g = level->collision().groundHeight(p.position+rc::Vec3{0,1.f,0}))
      at.m[1][3] = *g-car->full().bboxMin().y;
    carPose = at;
    flightPose = at;
    vehicle->reset(carPose);
    lastCarPos = carPose.translation();
    if(opponents)
      opponents->playerMoved();
    }
  // O: the opponents off, easy, normal, hard (and the race again)
  if(devMode && e.key==Event::K_O && level) {
    aiLevel = rc::Opponents::Level((int(aiLevel)+1)%4);
    setupOpponents();
    setupPhysics(true,true);
    }
  // P, Pause: everything stops
  if((e.key==Event::K_P || e.key==Event::K_Pause) && level)
    paused = !paused;
  }

void MainWindow::keyUpEvent(KeyEvent& e) {
  keys[e.key] = false;
  }

void MainWindow::mouseDownEvent(MouseEvent& e) {
  if(intro!=nullptr)
    return;
  if(menu->active())
    menu->mouseDown(e.x,e.y);
  }

void MainWindow::mouseUpEvent(MouseEvent& e) {
  if(intro!=nullptr)
    return;
  if(menu->active())
    menu->mouseUp(e.x,e.y);
  }

void MainWindow::mouseMoveEvent(MouseEvent& e) {
  if(intro!=nullptr)
    return;
  if(menu->active())
    menu->mouseMove(e.x,e.y);
  }

void MainWindow::mouseWheelEvent(MouseEvent& e) {
  if(intro!=nullptr)
    return;
  if(menu->active())
    menu->mouseWheel(e.delta);
  }

void MainWindow::mouseDragEvent(MouseEvent& e) {
  mouseMoveEvent(e);
  }

void MainWindow::menuCommand(Menu::Command c) {
  switch(c) {
    case Menu::Command::None:
      return;
    case Menu::Command::Race:
      raceRequested = true;
      return;
    case Menu::Command::Demo:
      // message 0x26d (0x4c9d6b): the demo from beach_1
      demoRequested = "Beach_1";
      return;
    case Menu::Command::Quit:
      Tempest::SystemApi::exit();
      return;
    case Menu::Command::Resume:
      menu->close();
      paused = false;
      return;
    case Menu::Command::BackOnTrack:
      menu->close();
      paused = false;
      if(level && ghosts) {
        // "Wróć na start" (0x4c7690)
        ghostBackToStart();
        return;
        }
      if(level) {
        setFlying(false);
        respawnCar();
        }
      return;
    case Menu::Command::RaceAgain:
      menu->close();
      saveGhosts();   // ours: the ghosts stay, from their first frames
      if(level)
        setupPhysics(true,true);
      paused = false;
      return;
    case Menu::Command::Done:
      // 0x4c4629: "Wyjdź" records the race in the player's file (0x4c56c0), "Jeszcze raz?" does not
      if(level && playerFinish>=0 && !devFlew) {
        const size_t trackId = Menu::trackIndex(level->name());
        if(players->raceDone(int32_t(trackId),int32_t(carId),race->laps(),playerFinish,odometer))
          tell(20101,-1,Hud::formatTime(playerFinish));
        // and in the championship its prize; the last track won makes the champion (0x14a3a88)
        if(raceMode==Menu::Mode::Championship) {
          const int32_t place = menu->finishPlace();
          const int32_t prize = players->championshipDone(int32_t(trackId),int32_t(carId),place,menu->finishBonus());
          Log::i("championship: ",place+1,". place, $",prize);
          if(place==0 && trackId+1==Menu::TrackCount)
            menu->setChampionWon();
          }
        }
      [[fallthrough]];
    case Menu::Command::Abort:
      // back to the main menu; the track stays loaded under it, silent
      sounds->stopAll();
      if(carSound)
        carSound->stop();
      for(auto& s:aiSounds)
        s.stop();
      paused = false;
      // the ghost race has no finish: it is left from its menu, for its screen (0x4e07c0); the laps kept (0x4f17d0)
      saveGhosts();
      menu->open(c==Menu::Command::Done || ghosts!=nullptr ? menu->afterRace() : Menu::Page::Main);
      return;
    }
  }

void MainWindow::presentLoading(size_t track, float progress) {
  frameId = uint8_t((frameId+1)%MaxFramesInFlight);
  auto& sync = fence[frameId];
  auto& cmd  = commands[frameId];
  sync.wait();
  {
    auto enc = cmd.startEncoding(device);
    enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0,0,0,1),Tempest::Preserve}});
    menu->drawLoading(enc,frameId,int(w()),int(h()),track,progress);
  }
  sync = device.submit(cmd);
  device.present(swapchain);
  }

void MainWindow::presentStart() {
  frameId = uint8_t((frameId+1)%MaxFramesInFlight);
  auto& sync = fence[frameId];
  auto& cmd  = commands[frameId];
  sync.wait();
  {
    auto enc = cmd.startEncoding(device);
    enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0,0,0,1),Tempest::Preserve}});
    intro->drawStart(enc,frameId,int(w()),int(h()));
  }
  sync = device.submit(cmd);
  device.present(swapchain);
  }

void MainWindow::renderIntro() {
  const uint64_t now = Application::tickCount();
  const float    dt  = float(now-lastTick)/1000.f;
  lastTick = now;
  intro->tick(dt);
  if(!intro->playing()) {
    // on to the start screen and the main menu
    init(std::string());
    return;
    }
  frameId = uint8_t((frameId+1)%MaxFramesInFlight);
  auto& sync = fence[frameId];
  auto& cmd  = commands[frameId];
  sync.wait();
  {
    auto enc = cmd.startEncoding(device);
    enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0,0,0,1),Tempest::Preserve}});
    intro->draw(enc,frameId,int(w()),int(h()));
  }
  sync = device.submit(cmd);
  device.present(swapchain);
  }

void MainWindow::startRace() {
  const Menu::Setup st    = menu->setup();
  const std::string track = Menu::trackName(st.track);
  menu->close();
  // 0x4c9f70: the player's quick race to come back to
  raceMode = st.mode;
  if(st.mode==Menu::Mode::Quick)
    players->raceStarted(int32_t(st.track),int32_t(st.car),st.laps,st.skill);
  presentLoading(st.track,0.f);
  // 0x4a4ca0 modes: the quick race's laps and skill; the training's 3 laps against the easy ones, the
  // championship's 5 against the experts (0x4a9770, 0x4e03b0)
  switch(st.mode) {
    case Menu::Mode::Quick:
      laps    = st.laps;
      aiLevel = st.skill<=0 ? rc::Opponents::Level::Easy :
                st.skill==1 ? rc::Opponents::Level::Normal : rc::Opponents::Level::Hard;
      break;
    case Menu::Mode::Training:
      laps    = 3;
      aiLevel = rc::Opponents::Level::Easy;
      break;
    case Menu::Mode::Championship:
      laps    = 5;
      aiLevel = rc::Opponents::Level::Hard;
      break;
    case Menu::Mode::Ghost:
    case Menu::Mode::MultiGhost:
      // 19 laps (0x4a4bc0) that never end it (0x4eaa20), alone with the ghosts
      laps    = std::numeric_limits<int>::max();
      aiLevel = rc::Opponents::Level::Off;
      break;
    case Menu::Mode::Demo:
      break;   // startDemo()'s
    }
  // the car as the player has it (0x4aca10)
  int32_t b = 0, r = 0, t = 0, sk = 0;
  if(auto* p = players->current()) {
    const auto& c = p->cars[std::min<size_t>(st.car,2)];
    if(c.bought) {
      b  = c.boost;
      r  = c.resonator;
      t  = c.tires;
      sk = c.skin;
      }
    }
  const bool sameCar = car!=nullptr && carId==st.car && boostLevel==b && resonatorLevel==r && tiresLevel==t && skin==sk;
  boostLevel     = b;
  resonatorLevel = r;
  tiresLevel     = t;
  skin           = sk;
  if(level!=nullptr && level->name()==track) {
    // the track already there: only the field, the car and the race again
    if(!sameCar) {
      device.waitIdle();
      carId = st.car;
      fitCar();
      if(carSound)
        carSound->stop();
      carSound = std::make_unique<rc::CarSound>(sounds->mixer(),sounds->library(),int(carId));
      }
    presentLoading(st.track,0.5f);
    race->setLaps(laps);
    setupOpponents();
    setupPhysics(true);
    }
  else {
    if(!sameCar) {
      device.waitIdle();
      carId = st.car;
      fitCar();
      if(carSound)
        carSound->stop();
      carSound = std::make_unique<rc::CarSound>(sounds->mixer(),sounds->library(),int(carId));
      }
    presentLoading(st.track,0.2f);
    try {
      loadLevel(track);
      }
    catch(const std::exception& e) {
      Log::e("race: ",e.what());
      }
    }
  presentLoading(st.track,1.f);
  if(level==nullptr || level->name()!=track) {
    menu->open(Menu::Page::Main);
    return;
    }
  setupGhosts();
  paused   = false;
  lastTick = Application::tickCount();
  }

void MainWindow::startDemo(const std::string& name) {
  const size_t      trackId = Menu::trackIndex(name);
  if(trackId>=Menu::TrackCount) {
    Log::e("demo: no track \"",name,"\"");
    endDemo();
    return;
    }
  const std::string track = Menu::trackName(trackId);
  menu->close();
  // 0x4c9d6b: mode 6 (0x4a4c90), the race's settings as they are; the demo's flag (0x200) comes with the script's
  // EnterDemo
  raceMode = Menu::Mode::Demo;
  demo     = Demo();
  laps     = rc::Race::DefaultLaps;
  aiLevel  = rc::Opponents::Level::Normal;
  sounds->stopAll();
  if(carSound)
    carSound->stop();
  presentLoading(trackId,0.f);
  if(level!=nullptr && level->name()==track) {
    presentLoading(trackId,0.5f);
    race->setLaps(laps);
    setupOpponents();
    setupPhysics(true);
    }
  else {
    presentLoading(trackId,0.2f);
    try {
      loadLevel(track);
      race->setLaps(laps);
      }
    catch(const std::exception& e) {
      Log::e("demo: ",e.what());
      }
    }
  presentLoading(trackId,1.f);
  setupGhosts();
  if(level==nullptr || level->name()!=track || vehicle==nullptr || opponents==nullptr || opponents->size()==0) {
    Log::e("demo: can't start it on ",track);
    endDemo();
    return;
    }

  // the track's "Scene Scripts" DemoEntry (beach_1, country_3, country_4)
  const rc::SbScript* entry = nullptr;
  for(auto& sc:level->scene().scripts)
    if(sc.name=="DemoEntry")
      entry = &sc;
  if(entry==nullptr) {
    Log::e("demo: ",track," has no DemoEntry script");
    endDemo();
    return;
    }
  const auto script = rc::GameScript::parse(entry->text);
  for(auto& l:script.lines) {
    if(l.command=="Set" && l.args.size()>=2 && l.args[0]=="NextDemoRace")
      demo.next = l.args[1];
    // the car's address: '$CAR' and its number (0x4e18b0, 0x4e23f0 type 9)
    if(l.command=="SetActive" && l.args.size()>=2 && l.args[0]=="$CAR")
      demo.focus = std::atoi(l.args[1].c_str());
    if(l.command=="LoadMap" && !l.args.empty()) {
      demo.loadAt  = l.time;
      demo.loadMap = l.args[0];
      }
    }
  if(demo.focus<0 || size_t(demo.focus)>=opponents->size())
    demo.focus = 0;
  demo.on = true;
  opponents->follow(demo.focus);
  // "0 StartRace" without a countdown; the player's car held above the track's box, out of everything
  race->startNow();
  const auto& box = level->scene().collision;
  demo.park = rc::Mat4::translate({box.min.x,box.max.y+500.f,box.min.z});
  carPose   = demo.park;
  vehicle->hold(demo.park);
  chase.reset(chaseCar());
  paused   = false;
  lastTick = Application::tickCount();
  Log::i("demo: ",track,", following ",opponentName(size_t(demo.focus)),", next ",demo.next," after ",demo.loadAt," s");
  }

void MainWindow::endDemo() {
  demo = Demo();
  if(opponents)
    opponents->follow(-1);
  raceMode = Menu::Mode::Quick;
  sounds->stopAll();
  if(carSound)
    carSound->stop();
  for(auto& s:aiSounds)
    s.stop();
  paused = false;
  // the track stays loaded under the main menu, as after a race left; the next race sets its field up again
  menu->open(Menu::Page::Main);
  }

const rc::Opponent* MainWindow::demoCar() const {
  if(!demo.on || opponents==nullptr || demo.focus<0 || size_t(demo.focus)>=opponents->size())
    return nullptr;
  return &(*opponents)[size_t(demo.focus)];
  }

void MainWindow::tickDemo(float dt) {
  fog.tick(dt);
  const bool racing = !race->holdCar();
  vehicle->hold(demo.park);
  opponents->collide(*vehicle,dt);
  opponents->update(dt,racing,demo.park.translation(),0,false,race->clockTime(),race->laps());
  if(objects)
    objects->update(dt);
  physics->step(dt);
  if(objects)
    objects->afterStep(*physics);
  vehicle->hold(demo.park);
  std::vector<rc::Characters::Car> others;
  for(size_t i=0; i<opponents->size(); ++i) {
    const rc::Mat4 p = (*opponents)[i].pose();
    others.push_back({p.translation(),p.vector({0,0,1})*(*opponents)[i].speed()});
    }
  if(characters)
    characters->update(dt,nullptr,others);
  if(traffic)
    traffic->update(dt,racing,nullptr,camera.position,others);
  for(size_t i=0; i<others.size(); ++i)
    if(others[i].hit)
      (*opponents)[i].knock();
  // the race's clock: the time in the cockpit and the script's
  race->update(dt,demo.park.translation(),demo.park.vector({0,0,1}));
  {
    effects->tick(dt,*vehicle,opponents.get(),level->collision(),camera.position,false,false);
    const rc::fx::View view = {camera.position,camera.viewProj(float(w())/float(std::max(1,h())))};
    std::vector<rc::fx::Footprints::Walker> walkers;
    if(characters)
      for(auto* a:characters->actors())
        walkers.push_back({a->world,a->soles});
    effects->tickWorld(dt,view,time,[this](const rc::Vec3& p){ return level->waterHeight(p,10.f,time); },
                       gunOrders(),gunTargets(),level->collision(),walkers);
  }
  for(auto& h:effects->gunHits())
    if(h.target!=0 && h.target-1<opponents->size())
      (*opponents)[h.target-1].shotFrom(h.gun);
  tickSounds(dt);

  // the script's LoadMap: the next track's demo
  if(demo.loadAt>=0 && race->clockTime()>=demo.loadAt && !demo.loadMap.empty() && demoRequested.empty())
    demoRequested = demo.loadMap;

  if(zeroCam!=ZeroCam::Off) {
    tickZeroCamera(dt);
    return;
    }
  const ChaseCamera::Car c = chaseCar();
  chase.update(c,dt);
  chase.apply(c,&level->collision(),dt,camera);
  level->keepAboveWater(camera,time);
  }

void MainWindow::drawCarPreview(Encoder<CommandBuffer>& enc, const Menu::Preview& p) {
  const size_t id = std::min<size_t>(p.car,2);
  const std::array<int32_t,4> want = {p.boost,p.resonator,p.tires,p.skin};
  if(previewCars[id]==nullptr || previewLook[id]!=want) {
    ModelMesh::CarLook look;
    look.boost     = p.boost;
    look.resonator = p.resonator;
    look.tires     = p.tires;
    look.skin      = p.skin;
    device.waitIdle();
    previewCars[id] = std::make_unique<CarModel>(device,resources,carScene,id,look);
    previewLook[id] = want;
    }
  if(p.alpha<=0)
    return;
  // the original turns the car in a little scene of its own (AnimPreview, 0x4a9e70): here a camera above the
  // front, the car filling the rectangle
  auto&          mesh   = previewCars[id]->full();
  const rc::Vec3 lo     = mesh.bboxMin(), hi = mesh.bboxMax();
  const rc::Vec3 centre = (lo+hi)*0.5f;
  const float    radius = (hi-lo).length()*0.5f;
  Camera cam;
  cam.fov      = 30.f;
  cam.pitch    = -25.f;
  cam.yaw      = 0;
  const float dist = radius/std::sin(cam.fov*0.5f*3.14159265f/180.f)*0.9f;
  cam.position = rc::Vec3{0,0,0}-cam.forward()*dist;
  const float    sw = float(std::max<uint32_t>(1,w())), sh = float(std::max<uint32_t>(1,h()));
  rc::Mat4 fit;   // the view's -1..1 onto the rectangle
  fit.m[0][0] = p.w/sw; fit.m[0][3] = (p.x+p.w*0.5f)*2.f/sw-1.f;
  fit.m[1][1] = p.h/sh; fit.m[1][3] = (p.y+p.h*0.42f)*2.f/sh-1.f;
  const rc::Mat4 viewProj = fit*cam.viewProj(p.w/std::max(1.f,p.h));
  const rc::Mat4 world    = rc::Mat4::rotate(1,p.yaw)*rc::Mat4::translate(rc::Vec3{0,0,0}-centre);

  LevelMesh::Push push;
  viewProj.toColumnMajor(push.viewProj);
  const rc::Vec3 eye = cam.position;
  push.cameraPos[0] = eye.x; push.cameraPos[1] = eye.y; push.cameraPos[2] = eye.z; push.cameraPos[3] = 0;
  push.sunDir[0] = 0.4f; push.sunDir[1] = 0.8f; push.sunDir[2] = 0.3f; push.sunDir[3] = 1.0f;
  push.time = 0;
  mesh.setDirt(0);
  mesh.draw(enc,viewProj,push,world);
  }

void MainWindow::tick(float dt) {
  sounds->tick();
  const bool stopped = zeroCam==ZeroCam::Detached && !frameStep;
  sounds->setPaused(paused || stopped);
  if(paused)
    return;
  if(stopped) {
    tickZeroCamera(dt);
    return;
    }
  if(frameStep) {
    frameStep = false;
    dt        = fixedTime>0 ? fixedTime/1000.f : lastDt;
    }
  else
    lastDt = dt;
  if(demo.on) {
    tickDemo(dt);
    return;
    }
  fog.tick(dt);
  if(vehicle!=nullptr) {
    rc::Vehicle::Input in;
    if(race->holdCar() || shotTime>=0 || resetVolumes.lowSignal()) {
      // waiting for the start, shot (RCCars.exe 0x4f6b20) or in a reset volume (0x4f6c87): no keys, Brake held
      in.handbrake = true;
      }
    else if(zeroCam==ZeroCam::Off && !flying) {
      // the keys of the options' "Dostosuj sterowanie" (RCCars.crs Player1 and its layout)
      const bool up    = held(Controls::Forw);
      const bool down  = held(Controls::Back);
      const bool left  = held(Controls::Left);
      const bool right = held(Controls::Right);
      in.throttle  = (up ? 1.f : 0.f) - (down ? 1.f : 0.f);
      in.steer     = (left ? 1.f : 0.f) - (right ? 1.f : 0.f);
      in.handbrake = held(Controls::Brake);
      in.boost     = held(Controls::Boost);
      in.jump      = jumpPressed;
      }
    jumpPressed = false;
    if(flying) {
      // RCCars.exe 0x4f39e0 -> 0x482900: moved by hand instead of the physics, at rest where it is put
      if(zeroCam==ZeroCam::Off)
        fly(dt);
      vehicle->hold(flightPose);
      }
    else
      vehicle->update(in,dt);
    if(ghosts) {
      // 0x4f6ea0 -> 0x502b90: the player's car recorded; the ghosts on their laps
      const uint32_t pressed = (in.throttle>0 ? 0x01u : 0u) | (in.throttle<0 ? 0x02u : 0u);
      ghosts->record(dt,*vehicle,pressed,!race->holdCar() && !flying);
      ghosts->update(dt);
      }
    auto hit = vehicle->hit();
    if(opponents)
      hit = std::max(hit,opponents->collide(*vehicle,dt));
    if(hit!=rc::Vehicle::Hit::None) {
      hud->hit(hit==rc::Vehicle::Hit::Great);
      Log::i(hit==rc::Vehicle::Hit::Great ? "great hit" : "hit"," at ",Hud::formatTime(race->time()));
      }
    if(opponents)
      opponents->update(dt,!race->holdCar(),carPose.translation(),checkpoints->laps(),playerFinish>=0,race->clockTime(),
                        race->laps());
    if(objects)
      objects->update(dt);
    physics->step(dt);
    if(objects)
      objects->afterStep(*physics);
    if(flying)
      vehicle->hold(flightPose);  // the step's gravity and bumps undone
    // the people and animals and the road cars, after the step: a person holding the car puts it in the hand
    std::vector<rc::Characters::Car> others;
    for(size_t i=0; opponents && i<opponents->size(); ++i) {
      const rc::Mat4 p = (*opponents)[i].pose();
      others.push_back({p.translation(),p.vector({0,0,1})*(*opponents)[i].speed()});
      }
    if(characters && characters->update(dt,vehicle.get(),others)) {
      hud->hit(true);
      Log::i("great hit (character) at ",Hud::formatTime(race->time()));
      }
    if(traffic)
      traffic->update(dt,!race->holdCar(),vehicle.get(),camera.position,others);
    // an opponent that ran into someone or something drives on by itself (RCCars.exe 0x534340, 0x534880 ..
    // -> 0x4fd9d0)
    for(size_t i=0; i<others.size(); ++i)
      if(others[i].hit)
        (*opponents)[i].knock();
    tellNews(dt,others);
    carPose = vehicle->pose();
    const rc::Vec3 at = carPose.translation();
    const size_t   was = checkpoints->next();
    const auto     ev  = race->update(dt,at,carPose.vector({0,0,1}));
    // the way driven (0x4f1c80: every step, not while the car is put back); ours also not while it flies
    if(!race->holdCar() && playerFinish<0 && !flying)
      odometer += (at-lastCarPos).length();
    const size_t trackId = Menu::trackIndex(level->name());
    const bool   lapEnd  = (ev==rc::Race::Event::Lap || ev==rc::Race::Event::BestLap || ev==rc::Race::Event::Finish) &&
                           !race->lapTimes().empty();
    // the ghost race (0x4fbeb0): "Jazda!" is our first time over the line, a lap the next
    bool ghostTook = false;
    if(ghosts && ev==rc::Race::Event::Go)
      ghosts->start();
    if(ghosts && lapEnd) {
      ghostTook = ghosts->lapDone(race->lapTimes().back());
      for(size_t i=0; i<ghosts->size(); ++i)
        carModel((*ghosts)[i].look);
      }
    // the player's laps and race into the player's file (0x4c5560, 0x4c56c0), told in the chat when they are the
    // best (Language/Game 20100, 20101), and when the own ghost takes it (0x4fbeb0)
    if(lapEnd) {
      const float lap  = race->lapTimes().back();
      bool        best = ghostTook;
      if(!devFlew && players->lapDone(int32_t(trackId),int32_t(carId),lap))
        best = true;
      if(best)
        tell(20100,-1,Hud::formatTime(lap));
      }
    if(ev==rc::Race::Event::Finish) {
      playerFinish = race->time();
      if(opponents && opponents->size()>0)
        Log::i("finished ",opponents->place(playerFinish),". of ",opponents->size()+1," in ",Hud::formatTime(playerFinish));
      }
    // RCCars.sb LevelFinish ("3 ShowTable", 0x4a5100): the table 3 s after the finish
    if(playerFinish>=0 && !finishShown && race->sinceFinish()>=3.f && !menu->active()) {
      finishShown = true;
      menu->open(Menu::Page::Finish);
      }
    if(menu->page()==Menu::Page::Finish)
      menu->setFinish(finishRows(),vehicle->hits());
    // the ghost race's "najlepsze okrążenie" for the lap the own ghost took (0x4afbb0 10), only then
    if(ghosts && (ev==rc::Race::Event::Lap || ev==rc::Race::Event::BestLap))
      hud->event(ghostTook ? rc::Race::Event::BestLap : rc::Race::Event::Lap);
    else
      hud->event(ev);
    // 0x4eaa50: every lap but the last, with its number in words; 0x4eaad0: the finish
    if(ev==rc::Race::Event::Lap || ev==rc::Race::Event::BestLap)
      tell(20108,-1,hud->gameText(uint32_t(race->lapTimes().size()+3)));
    if(ev==rc::Race::Event::Finish)
      tell(20106,-1);
    // with "3" the original plays cp_start (Checkpoint channel) and prestart (Prestart channel) together
    if(ev==rc::Race::Event::Count) {
      sounds->play("Checkpoint","cp_start");
      sounds->play("Prestart","prestart");
      }
    if(checkpoints->next()!=was) {
      sounds->play("Checkpoint","cp");
      vehicle->addBoost(rc::CarParams::BoostPerCheckpoint);
      Log::i("checkpoint ",checkpoints->route()[was].name," passed at ",Hud::formatTime(race->time()),
             " (",at.x," ",at.y," ",at.z,")");
      }
    lastCarPos = at;
    arrow->tick(dt,arrowInput());
    {
      const bool driving = !race->holdCar() && zeroCam==ZeroCam::Off && !flying;
      const bool gas     = driving && held(Controls::Forw);
      const bool brake   = driving && held(Controls::Back);
      effects->tick(dt,*vehicle,opponents.get(),level->collision(),camera.position,gas,brake);
      const rc::fx::View view = {camera.position,camera.viewProj(float(w())/float(std::max(1,h())))};
      std::vector<rc::fx::Footprints::Walker> walkers;
      if(characters)
        for(auto* a:characters->actors())
          walkers.push_back({a->world,a->soles});
      effects->tickWorld(dt,view,time,[this](const rc::Vec3& p){ return level->waterHeight(p,10.f,time); },
                         gunOrders(),gunTargets(),level->collision(),walkers);
    }
    // what the bullets hit (RCCars.exe 0x508560): the player's car is held and put back 2 s after the first hit
    // (0x508ae0), an opponent is knocked off its recording and put back alike
    for(auto& h:effects->gunHits()) {
      if(h.target!=0 && opponents && h.target-1<opponents->size())
        tell(20102,int(h.target-1));   // 0x5085e9: every hit, "<name> prowadzi"
      if(h.target==0) {
        vehicle->shotFrom(h.gun);
        shotSound = true;   // car_cdt_bullet on every hit (0x50859f)
        tell(20102,-1);
        if(shotTime<0) {
          shotTime = 0;
          Log::i("shot at ",Hud::formatTime(race->time()));
          }
        }
      else if(opponents && h.target-1<opponents->size())
        (*opponents)[h.target-1].shotFrom(h.gun);
      }
    if(shotTime>=0)
      shotTime += dt;

    // on its roof or side for a while: back on the wheels (not while flown: RCCars.exe skips the car's own update)
    upsideDown = !flying && carPose.vector({0,1,0}).y<0.3f ? upsideDown+dt : 0;
    if(upsideDown>2.f) {
      vehicle->recover();
      upsideDown = 0;
      }
    // back to the last checkpoint (RCCars.exe 0x508160): out of the track's collision box (sideways, or more
    // than 0.1 m under it), drowned, a while in a reset volume or stuck in the track (0x4f5e50); never while F9
    // flies the car (0x5082c0)
    const auto&    grid = level->scene().collision;
    const rc::Vec3 pos  = carPose.translation();
    const bool     out  = !grid.empty() && (pos.x<grid.min.x || pos.x>grid.max.x || pos.z<grid.min.z ||
                                            pos.z>grid.max.z || pos.y<grid.min.y-0.1f);
    const bool inVolume = resetVolumes.tick(pos,dt);
    if(!flying && (out || vehicle->drowned() || vehicle->stuck() || inVolume || shotTime>rc::Vehicle::ShotHold)) {
      // 0x508ac5, 0x509727
      if(!out && vehicle->drowned())
        tell(20105,-1);
      else if(!out && inVolume)
        tell(20107,-1);
      respawnCar();
      }
    tickSounds(dt);
    }

  if(zeroCam!=ZeroCam::Off) {
    tickZeroCamera(dt);
    return;
    }
  if(flying) {
    // 0x500ce9, 0x501183: rigid behind the car, nothing hides it, under water too
    chase.applyRigid(chaseCar(),camera);
    return;
    }
  const ChaseCamera::Car c = chaseCar();
  chase.update(c,dt);
  chase.apply(c,&level->collision(),dt,camera);
  level->keepAboveWater(camera,time);
  }

void MainWindow::setFlying(bool on) {
  if(on==flying || vehicle==nullptr)
    return;
  flying = on;
  if(flying) {
    devFlew = true;
    // 0x4f39e0, the first frame: upright where it is, facing where it went across the ground (or its nose)
    const rc::Vec3 v = vehicle->velocity();
    rc::Vec3       f = std::abs(v.x)+std::abs(v.z)>1e-3f ? rc::Vec3{v.x,0,v.z} : carPose.vector({0,0,1});
    if(std::abs(f.x)+std::abs(f.z)<=1e-3f)
      f = {0,0,1};
    const float yaw = std::atan2(f.x,f.z)*180.f/3.14159265f;
    flightPose = rc::Mat4::translate(carPose.translation())*rc::Mat4::rotate(1,yaw);
    vehicle->hold(flightPose);
    carPose = flightPose;
    Log::i("dev: the car flies (F9)");
    }
  else {
    // the physics again from where it was left, at rest
    vehicle->reset(carPose);
    upsideDown = 0;
    resetVolumes.reset();
    chase.reset(chaseCar());
    Log::i("dev: the car drives (F9)");
    }
  }

std::string MainWindow::opponentName(size_t i) const {
  // AIPlayer<n>: Language/Game 40499+n ("MC Zgryw" ..., 0x4a9ce0)
  const auto& o = (*opponents)[i];
  if(o.driver>0) {
    const auto t = hud->gameText(uint32_t(40499+o.driver));
    if(!t.empty())
      return std::string(t);
    }
  return o.name;
  }

std::vector<Menu::FinishRow> MainWindow::finishRows() const {
  // 0x4c47e0 with the race's standings (0x4ea2d0): the finished by their times, then the others by how far they are;
  // the time of one still racing is the race's clock, its speed the way along the route so far over it (for the
  // first minute drawn from 7 m/s, 0x4eac90), of one finished the laps over its time
  std::vector<Menu::FinishRow> rows;
  const float clock = race->clockTime();
  const float lap   = checkpoints->lapLength();
  auto avg = [&](float way, float t, bool done) {
    if(t<1e-6f)
      return 0.f;
    float v = (done ? lap*float(race->laps()) : way)/t;
    if(!done && t<60.f)
      v = (t/60.f)*v+(1.f-t/60.f)*7.f;
    return v*3.6f;
    };
  const auto st = opponents ? opponents->standings(playerFinish)
                            : std::vector<rc::Opponents::Standing>{{"",playerFinish,true,-1}};
  for(auto& s:st) {
    Menu::FinishRow r;
    r.player = s.player;
    if(s.player) {
      auto* p   = players->current();
      r.name    = p!=nullptr ? p->name : std::string(hud->gameText(40507));
      r.time    = playerFinish>=0 ? playerFinish : clock;
      r.bestLap = race->lapTimes().empty() ? -1.f : race->bestLap();
      r.speed   = avg(opponents ? opponents->playerProgress() : 0.f,r.time,playerFinish>=0);
      }
    else {
      const auto& o = (*opponents)[size_t(s.car)];
      const bool done = o.finishTime>=0;
      r.name    = opponentName(size_t(s.car));
      r.faceKey = "AIPlayer:"+o.face;
      r.face    = players->faceByName(o.face);
      r.time    = done ? o.finishTime : clock;
      r.bestLap = o.bestLap;
      r.speed   = avg(o.progress(),r.time,done);
      }
    rows.push_back(std::move(r));
    }
  return rows;
  }

void MainWindow::tell(uint32_t id, int car, std::string_view what) {
  // the player's name: the one playing (Players\\<name>), the table's word for a player without one
  std::string who;
  if(car>=0 && opponents!=nullptr && size_t(car)<opponents->size())
    who = opponentName(size_t(car));
  else if(auto* p = players->current())
    who = p->name;
  else
    who = std::string(hud->gameText(40507));
  hud->tell(id,who,what);
  }

void MainWindow::tellNews(float dt, const std::vector<rc::Characters::Car>& others) {
  const bool racing = !race->holdCar();
  // 0x511634 / 0x510e6b: kicked or thrown by someone
  if(characters && characters->kickedPlayer())
    tell(20103,-1);
  // 0x5165a0: a road car's push, at most every 2 s per car (car+0x5704 counts up from the car's start)
  sinceBumper.resize(others.size()+1,0.f);
  for(auto& t:sinceBumper)
    t += dt;
  auto bumper = [&](size_t i, int car) {
    if(sinceBumper[i]>2.f) {
      tell(20104,car);
      sinceBumper[i] = 0;
      }
    };
  if(traffic && traffic->bumpedPlayer())
    bumper(0,-1);
  for(size_t i=0; i<others.size(); ++i)
    if(others[i].roadCar)
      bumper(i+1,int(i));
  if(opponents==nullptr)
    return;
  // the opponents' laps, finish and returns to the track (Opponents::update)
  for(auto& n:opponents->news()) {
    switch(n.what) {
      case rc::Opponents::News::Lap:      tell(20108,int(n.car),hud->gameText(uint32_t(n.laps+3))); break;
      case rc::Opponents::News::Finished: tell(20106,int(n.car)); break;
      case rc::Opponents::News::Drowned:  tell(20105,int(n.car)); break;
      case rc::Opponents::News::Volume:   tell(20107,int(n.car)); break;
      }
    }
  // what the opponents seem to run into (AiEmu)
  std::vector<rc::AiEmu::Spot> spots;
  if(characters)
    for(size_t i=0; i<characters->actors().size(); ++i) {
      const auto* a = characters->actors()[i];
      if(a->kind==rc::Characters::Kind::Person || a->kind==rc::Characters::Kind::Dog)
        spots.push_back({a->kind==rc::Characters::Kind::Dog ? rc::AiEmu::Trap::Dog : rc::AiEmu::Trap::Person,
                         uint32_t(i),a->world.translation(),a->visible});
      }
  if(traffic)
    for(size_t i=0; i<traffic->cars().size(); ++i) {
      const auto* c = traffic->cars()[i];
      spots.push_back({rc::AiEmu::Trap::RoadCar,uint32_t(i),c->pose.translation(),c->onRoad});
      }
  std::vector<rc::AiEmu::Car> cars;
  for(size_t i=0; i<opponents->size(); ++i)
    cars.push_back({(*opponents)[i].pose().translation(),racing && (*opponents)[i].finishTime<0});
  for(auto& e:aiEmu.update(dt,race->time(),spots,cars)) {
    const uint32_t id = e.kind==rc::AiEmu::Trap::Dog ? 20109 : e.kind==rc::AiEmu::Trap::Person ? 20103 : 20104;
    tell(id,int(e.car));
    }
  }

MainWindow::ZeroInput MainWindow::zeroInput(float dt) const {
  // RCCars.exe 0x482900 with its keys (0x482320): Up/Down forward and back, Left/Right turn, Alt with them up/down
  // and sideways (also ","/"." in the original), A/Z the nose up/down, End level again, Shift twice as fast
  // (ZeroSpeed m/s, ZeroAngle degrees/s). W/S move too; A is the nose here as in the original, so only the arrows
  // turn.
  const bool  alt   = keys[Event::K_LAlt] || keys[Event::K_RAlt];
  const bool  fast  = keys[Event::K_LShift] || keys[Event::K_RShift];
  const float move  = zeroSpeed*(fast ? 2.f : 1.f)*dt;
  const float turn  = zeroAngle*(fast ? 2.f : 1.f)*dt;
  const bool  up    = keys[Event::K_Up]    || keys[Event::K_W];
  const bool  down  = keys[Event::K_Down]  || keys[Event::K_S];
  const bool  left  = keys[Event::K_Left];
  const bool  right = keys[Event::K_Right];

  ZeroInput in;
  if(alt) {
    in.step.y += up    ? move : 0.f;
    in.step.y -= down  ? move : 0.f;
    in.step.x += left  ? move : 0.f;
    in.step.x -= right ? move : 0.f;
    }
  else {
    in.step.z += up    ? move : 0.f;
    in.step.z -= down  ? move : 0.f;
    in.yaw    += left  ? turn : 0.f;
    in.yaw    -= right ? turn : 0.f;
    }
  in.pitch -= keys[Event::K_A] ? turn : 0.f;   // nose up
  in.pitch += keys[Event::K_Z] ? turn : 0.f;   // nose down
  in.level  = keys[Event::K_End];
  return in;
  }

void MainWindow::fly(float dt) {
  const ZeroInput in = zeroInput(dt);
  flightPose = flightPose*rc::Mat4::translate(in.step)*rc::Mat4::rotate(1,in.yaw)*rc::Mat4::rotate(0,in.pitch);
  if(in.level) {
    // level again: only the heading kept
    rc::Vec3 f = flightPose.vector({0,0,1});
    if(std::abs(f.x)+std::abs(f.z)>1e-3f)
      flightPose = rc::Mat4::translate(flightPose.translation())*
                   rc::Mat4::rotate(1,std::atan2(f.x,f.z)*180.f/3.14159265f);
    }
  }

bool MainWindow::devCodeKey(Event::KeyType key) {
  char c = 0;
  if(key>=Event::K_A && key<=Event::K_Z)
    c = char('A'+(key-Event::K_A));
  else if(key>=Event::K_0 && key<=Event::K_9)
    c = char('0'+(key-Event::K_0));
  if(c==0 || devCodes.empty())
    return false;
  const uint64_t now = Application::tickCount();
  bool           own = false;
  std::string    done;
  for(auto& d:devCodes) {
    if(d.at>0 && now>d.last+2000)
      d.at = 0;   // 0x47a7a0: too slow
    if(d.text[d.at]!=c) {
      d.at = 0;   // 0x47a77e: starts over (the key itself is not tried as a first letter)
      continue;
      }
    own |= d.at>0;
    d.last = now;
    if(d.text[++d.at]==0) {
      d.at = 0;
      done = d.text;
      }
    }
  if(!done.empty())
    devCode(done);
  return own;
  }

void MainWindow::devCode(const std::string& code) {
  Log::i("dev: code ",code);
  if(code.size()==3 && code[0]=='D' && code[1]=='T') {
    // 0x532490, 0x531cb8: the car's dirt texture held at that level (as many as the car has), dtx lets it go
    forcedDirt = code[2]=='X' ? -1 : std::min(code[2]-'0',2);
    return;
    }
  if(code=="CONSTR") {
    // 0x4a4710 -> 0x4a40c4: outlines of the track's faces drawn over it (the collision overlay of K here)
    showCollision = !showCollision;
    return;
    }
  if(code=="STAT") {
    // 0x4eae30 flips the flag 0x4eaf03 draws the checkpoints by: the route's corridors and spheres round the main
    // checkpoints (1.6 m, the one the car has to touch, red; 4 m and 1.12 m too); here the gates of G
    showGates = !showGates;
    return;
    }
  if(code=="C1") {
    // 0x501a60: the view gets the race camera back
    zeroCam = ZeroCam::Off;
    chase.reset(chaseCar());
    return;
    }
  if(code=="E1") {
    zeroCam = ZeroCam::Fly;
    return;
    }
  if(code=="E2") {
    // the second view gets the ZERO camera; the only view here is left without a camera driver
    zeroCam = ZeroCam::Still;
    return;
    }
  if(code=="F" && zeroCam==ZeroCam::Detached) {
    // 0x482eb0: the stopped game runs one frame (0x48bce0, FixedTime ms or the last frame's time)
    frameStep = true;
    return;
    }
  if(code=="I" || code=="F") {
    // 0x482cd0 "I": a camera of its own where the view is, the game stopped; again: back, the game goes on
    if(zeroCam==ZeroCam::Detached) {
      zeroCam = ZeroCam::Off;
      chase.reset(chaseCar());
      }
    else
      zeroCam = ZeroCam::Detached;
    }
  }

void MainWindow::tickZeroCamera(float dt) {
  if(zeroCam!=ZeroCam::Fly && zeroCam!=ZeroCam::Detached)
    return;
  const ZeroInput in = zeroInput(dt);
  // the camera has no roll: up and down along the world's up, the nose as the camera's pitch
  camera.move(in.step.z,-in.step.x,in.step.y);
  camera.rotate(-in.yaw,-in.pitch);
  if(in.level)
    camera.pitch = 0;
  }

void MainWindow::tickPlayerSounds(float dt) {
  const bool racing = race->phase()==rc::Race::Phase::Racing;
  const bool control = !race->holdCar() && zeroCam==ZeroCam::Off && !flying;
  const float fwd   = vehicle->speed();
  airborne = !vehicle->grounded() ? airborne+dt : 0.f;

  rc::CarSound::Input in;
  in.speed    = std::abs(fwd)*3.6f;
  in.gas      = control && held(Controls::Forw);
  in.back     = control && held(Controls::Back);
  in.braking  = (in.back && fwd*3.6f>1.f) || (control && held(Controls::Brake));
  in.rearSlip = vehicle->rearSlip();
  in.airborne = airborne>0.05f;
  in.racing   = racing;
  in.boost    = vehicle->boosting();
  in.jump     = control && held(Controls::Jump);
  in.inWater  = vehicle->submerged()>0;
  in.ground   = vehicle->groundSound();
  in.contact  = {vehicle->contact().surface,vehicle->contact().hit,vehicle->contact().impact};
  in.carBump  = vehicle->carBump();
  in.objBump  = (characters && characters->bumpedPlayer()) || (traffic && traffic->bumpedPlayer());
  in.shot     = shotSound;
  shotSound   = false;
  carSound->update(in,dt);
  }

void MainWindow::tickSounds(float dt) {
  const bool racing = race->phase()==rc::Race::Phase::Racing;
  if(!demo.on)
    tickPlayerSounds(dt);

  // the computer's cars and the water, heard from the camera
  const rc::Vec3 ear   = camera.position;
  const rc::Vec3 right = camera.right();
  sounds->tickAmbient(ear,right,dt,level->collision().highestWater(ear,10.f));
  if(characters)
    sounds->tickCharacters(*characters,ear,right);
  if(objects)
    sounds->tickProps(objects->sounds(),ear,right);
  for(auto& g:effects->gunSounds())
    sounds->playAt(g.kind==rc::fx::Gunfire::Sound::Burst ? "burst" : "bullet",g.at,ear,right);
  if(traffic)
    sounds->tickTraffic(*traffic,ear,right);
  for(size_t i=0; ghosts && i<ghosts->size() && i<ghostSounds.size(); ++i) {
    // a ghost's engine, as an opponent's
    const auto&    g  = (*ghosts)[i];
    const rc::Vec3 at = g.pose().translation();
    const rc::Vec3 to = at-ear;
    const float    d  = to.length();
    ghostSounds[i].update(std::abs(g.speed())*3.6f,racing && g.visible && g.running,d,
                          d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
    }
  for(size_t i=0; opponents && i<opponents->size() && i<aiSounds.size(); ++i) {
    const auto&    o  = (*opponents)[i];
    const rc::Vec3 to = o.pose().translation()-ear;
    const float    d  = to.length();
    const rc::Vec3 at = o.pose().translation();
    const auto     wl = level->collision().waterLevel(at,0.5f);
    aiSounds[i].update(std::abs(o.speed())*3.6f,racing,d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f,
                       wl && *wl>at.y);
    }

  if(demo.on)
    return;
  // "wrong way" is said once each time the warning comes up
  if(race->wrongWay() && !wasWrongWay)
    sounds->play("Checkpoint","cp_wrongway");
  wasWrongWay = race->wrongWay();

  // cp_beside: the car came close to the next checkpoint (4 m) but missed it and is driving off (5 m) (0x4eb5be)
  if(!checkpoints->empty() && racing) {
    const size_t next = checkpoints->next();
    const float  d    = (checkpoints->route()[next].position-carPose.translation()).length();
    if(next!=besideTarget || besideDone) {
      besideTarget = next;
      besideMin    = d;
      besideDone   = d<4.f;  // already close (just put back here): nothing to say until it moves off and back
      }
    besideMin = std::min(besideMin,d);
    if(!besideDone && besideMin<4.f && d>5.f) {
      sounds->play("Checkpoint","cp_beside");
      besideDone = true;
      }
    }
  }

void MainWindow::renderMenu() {
  frameId = uint8_t((frameId+1)%MaxFramesInFlight);
  auto& sync = fence[frameId];
  auto& cmd  = commands[frameId];
  sync.wait();
  {
    auto enc = cmd.startEncoding(device);
    enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0,0,0,1),Tempest::Preserve}},
                       {zbuffer,1.f,Tempest::Discard});
    const auto preview = menu->carPreview();
    menu->draw(enc,frameId,int(w()),int(h()),[&](){
      if(preview)
        drawCarPreview(enc,*preview);
      });
    fps->draw(enc,frameId,int(w()),int(h()));
  }
  fps->drawDone();
  sync = device.submit(cmd);
  device.present(swapchain);
  fps->presented();
  }

// RCCars.exe 0x531ff0 (rc::fx::CarLight): the lightmap under the first four wheels touching the track, for the
// player's car the one under the car if none does. The opponents on their recordings have no wheel contacts of
// ours: theirs are looked for under their wheels as drawn.
// RenderQual and the car's LOD index (0x501c10: -1 an opponent nearer than DistLOD1SwitchOn, 0 the full model, 1 and 2
// the LODs) decide whether its antenna springs: where the index is below -1, 0, 1 for low, medium, high (0x5728b0,
// 0x4ff2e8). So on high every full model's antenna springs, on medium only the near opponents', on low none; the
// others keep the modelled one
bool MainWindow::springyAntenna(const rc::Vec3& pos, bool opponent) const {
  static const int32_t count[3] = {-1,0,1};
  const float dist  = (pos-camera.position).length();
  const int32_t lod = opponent && dist<lodDistances.lod0 ? -1 : dist<lodDistances.lod1 ? 0 : dist<lodDistances.lod2 ? 1 : 2;
  return lod<count[quality.renderLevel()];
  }

void MainWindow::tickAntennas(float dt) {
  // RCCars.exe 0x4fe9d0 every frame for every car; set up again (0x4ff2d0) where the car was put somewhere else
  auto follow = [this,dt](std::optional<rc::Antenna>& a, size_t id, const rc::Mat4& pose, std::optional<float> ceiling,
                          bool opponent) {
    if(!antennaView->has(id) || !springyAntenna(pose.translation(),opponent)) {
      a.reset();
      return;
      }
    const rc::Vec3 mount = pose.point(antennaView->mount(id));
    if(!a || (a->mountPoint()-mount).length()>1.f) {
      a.emplace(antennaSettings[id],pose,antennaView->mount(id),antennaView->rest(id));
      return;
      }
    a->step(dt,pose,ceiling);
    };
  if(car) {
    // 0x4fe5f0: the player's antenna feels for a ceiling straight up from where it stands
    std::optional<float> ceiling;
    if(level && carAntenna)
      if(auto hit = level->collision().raycast(carAntenna->mountPoint(),{0,1,0},carAntenna->reach()))
        ceiling = hit->distance;
    follow(carAntenna,carId,carPose,ceiling,false);
    // 0x4fe5f0: and scratches along it
    sounds->tickAntenna(carAntenna && vehicle ? int(carId) : -1,ceiling.has_value(),
                       vehicle ? vehicle->velocity().length()*3.6f : 0.f);
    }
  else {
    carAntenna.reset();
    sounds->tickAntenna(-1,false,0.f);
    }
  const size_t n = opponents ? opponents->size() : 0;
  aiAntennas.resize(n);
  for(size_t i=0; i<n; ++i) {
    const auto& o = (*opponents)[i];
    follow(aiAntennas[i],size_t(o.car),o.pose(),std::nullopt,true);
    }
  }

void MainWindow::shadeCars(float dt) {
  if(lightProbe==nullptr)
    return;
  constexpr size_t Wheels = 4;
  auto near = [this](const rc::Mat4& pose) {
    return (pose.translation()-camera.position).length()<carLight.maxDist;
    };
  auto average = [](float sum, int n) { return n>0 ? std::optional<float>(sum/float(n)) : std::nullopt; };
  // 0x4572c0 finds no light under the cars without the track's lightmaps (VIDEO_LightMaps)
  auto probe = [this](const rc::Vec3& at, float reach) {
    return quality.lightMaps ? lightProbe->below(at,reach) : std::nullopt;
    };
  auto contacts = [&probe](const rc::Vehicle& v, float& sum, int& n) {
    rc::Vec3 at;
    for(size_t i=0; i<Wheels; ++i)
      if(v.wheelGround(i,at))
        if(auto l = probe(at+rc::Vec3{0,0.05f,0},0.1f)) {
          sum += *l;
          ++n;
          }
    };
  if(vehicle) {
    float sum = 0;
    int   n   = 0;
    contacts(*vehicle,sum,n);
    auto light = average(sum,n);
    if(!light)
      light = probe(carPose.translation()+rc::Vec3{0,0.01f,0},1.5f);
    carShade.update(carLight,dt,near(carPose),light);
    }
  if(opponents) {
    aiShades.resize(opponents->size());
    for(size_t i=0; i<opponents->size(); ++i) {
      auto&      o    = (*opponents)[i];
      const auto pose = o.pose();
      float      sum  = 0;
      int        n    = 0;
      if(auto* v = o.vehicle()) {
        contacts(*v,sum,n);
        }
      else {
        const auto& wheels = o.geometry().wheels;
        for(size_t w=0; w<std::min(Wheels,wheels.size()); ++w) {
          const rc::Vec3 hub = pose.point(o.wheelPose(w).translation());
          if(auto l = probe(hub,wheels[w].radius+0.1f)) {
            sum += *l;
            ++n;
            }
          }
        }
      aiShades[i].update(carLight,dt,near(pose),average(sum,n));
      }
    }
  }

void MainWindow::render() {
  try {
    if(intro!=nullptr) {
      renderIntro();
      return;
      }
    if(raceRequested) {
      raceRequested = false;
      startRace();
      }
    if(!demoRequested.empty())
      startDemo(std::exchange(demoRequested,std::string()));
    if(fpsLimit>0) {
      const uint64_t period = 1000/uint64_t(fpsLimit);
      const uint64_t t      = Application::tickCount();
      if(frameDue>t)
        std::this_thread::sleep_for(std::chrono::milliseconds(frameDue-t));
      frameDue = std::max(frameDue,Application::tickCount()-period)+period;
      }
    fps->frameStart();
    const uint64_t now = Application::tickCount();
    const float    dt  = float(now-lastTick)/1000.f;
    lastTick = now;
    // the main menu and the quick race instead of the race; the race's menu over the stopped race
    const bool front = menu->active() && menu->page()!=Menu::Page::InRace && menu->page()!=Menu::Page::Finish;
    menu->tick(std::min(dt,0.1f));
    menuCommand(menu->take());
    if(menu->takeOptionsChanged())
      applyOptions();
    if(front) {
      sounds->tick();
      sounds->setPaused(false);
      fps->logicDone();
      renderMenu();
      return;
      }
    if(!paused && (zeroCam!=ZeroCam::Detached || frameStep))
      time  += zeroCam==ZeroCam::Detached ? (fixedTime>0 ? fixedTime/1000.f : lastDt) : std::min(dt,0.1f);
    tick(std::min(dt,0.1f));
    hud->tick(std::min(dt,0.1f));
    shadeCars(std::min(dt,0.1f));
    tickAntennas(paused ? 0.f : std::min(dt,0.1f));
    fps->logicDone();

    frameId = uint8_t((frameId+1)%MaxFramesInFlight);
    auto& sync = fence[frameId];
    auto& cmd  = commands[frameId];
    sync.wait();

    LevelMesh::Push push;
    const rc::Mat4  viewProj = camera.viewProj(float(w())/float(std::max(1,h())));
    viewProj.toColumnMajor(push.viewProj);
    push.cameraPos[0] = camera.position.x;
    push.cameraPos[1] = camera.position.y;
    push.cameraPos[2] = camera.position.z;
    push.cameraPos[3] = fog.current().density;
    push.fogColor     = fog.current().color & 0xffffffu;
    push.sunDir[0] = 0.4f; push.sunDir[1] = 0.8f; push.sunDir[2] = 0.3f;
    push.sunDir[3] = 1.0f;                     // lightmap scale
    push.time      = time;
    if(car && quality.renderLevel()>0) {
      // the sea see-through near the car, not on low RenderQual (0x523b00 -> 0x5240c0)
      const rc::Vec3 at = demoCar()!=nullptr ? demoCar()->pose().translation() : carPose.translation();
      push.carPos[0] = at.x; push.carPos[1] = at.y; push.carPos[2] = at.z;
      }

    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},
                         {zbuffer,1.f,Tempest::Preserve});
      const auto poses = level ? level->animated().poses(time) : std::vector<rc::Mat4>();
      if(level)
        level->mesh().draw(enc,push,LevelMesh::Pass::Opaque,viewProj,poses);
      if(objects)
        objects->draw(enc,viewProj,push);
      if(characters)
        characterView->draw(enc,frameId,resources,*characters,push);
      if(traffic)
        // the wheels' shadows and the head lights' glow with VIDEO_RoadCarShadow (0x5171d6, 0x517293)
        trafficView->draw(enc,frameId,*traffic,viewProj,push,
                          level && quality.roadCarShadow ? &level->collision() : nullptr);
      if(car && level && !demo.on && quality.carShadow) {
        carSpotList.assign(1,carShadow->spot(carId,carPose));
        carSpots->draw(enc,frameId,carSpotList,viewProj,push,level->collision());
        }
      if(car && !demo.on) {
        auto&      mesh  = car->at((carPose.translation()-camera.position).length(),lodDistances);
        const auto parts = carPartPoses(mesh);
        mesh.setDirt(forcedDirt>=0 ? float(forcedDirt) : effects->dirt());
        lightCar(mesh,&mesh==&car->full() && quality.light,carLight,carShade.value());
        mesh.setReflections(quality.envMap);
        mesh.showAntenna(!(&mesh==&car->full() && carAntenna));
        mesh.draw(enc,viewProj,push,carPose,&parts);
        antennaView->clear();
        if(&mesh==&car->full() && carAntenna)
          antennaView->add(carId,carAntenna->joints(),mesh.lighting());
        }
      else {
        antennaView->clear();
        }
      if(opponents) {
        for(size_t i=0; i<opponents->size(); ++i) {
          const auto& o = (*opponents)[i];
          auto        m = aiMeshes.find({o.car,o.skin,o.boost,o.reson,o.tires});
          if(m==aiMeshes.end())
            continue;
          auto&       mesh  = m->second->at((o.pose().translation()-camera.position).length(),lodDistances);
          const auto  parts = opponentPartPoses(mesh,o);
          lightCar(mesh,&mesh==&m->second->full() && quality.light,carLight,
                   i<aiShades.size() ? aiShades[i].value() : 1.f);
          mesh.setReflections(quality.envMap);
          const bool springs = &mesh==&m->second->full() && i<aiAntennas.size() && aiAntennas[i];
          mesh.showAntenna(!springs);
          mesh.draw(enc,viewProj,push,o.pose(),&parts);
          if(springs)
            antennaView->add(size_t(o.car),aiAntennas[i]->joints(),mesh.lighting());
          }
        }
      for(size_t i=0; ghosts && i<ghosts->size(); ++i) {
        // the ghosts: cars like the others (0x4f27a0), hidden until they have a lap (flag 2)
        const auto& g = (*ghosts)[i];
        auto        m = aiMeshes.find({g.look.car,g.look.skin,g.look.boost,g.look.resonator,g.look.tires});
        if(!g.visible || g.lap.empty() || m==aiMeshes.end())
          continue;
        auto&       mesh  = m->second->at((g.pose().translation()-camera.position).length(),lodDistances);
        const auto  parts = partPoses(mesh,g.geometry(),g.rigPose());
        lightCar(mesh,&mesh==&m->second->full() && quality.light,carLight,1.f);
        mesh.setReflections(quality.envMap);
        mesh.showAntenna(true);
        mesh.draw(enc,viewProj,push,g.pose(),&parts);
        }
      antennaView->draw(enc,frameId,push);
      effects->setFog(push.cameraPos[3],push.fogColor);
      if(vehicle)
        effects->drawTracks(enc,frameId,viewProj,camera.position);
      if(level)
        level->mesh().draw(enc,push,LevelMesh::Pass::Translucent,viewProj,poses);
      if(vehicle)
        effects->draw(enc,frameId,int(w()),int(h()),viewProj,camera.position);
      if(level && sunFlare.has()) {
        // the sun and its flare (RCCars.exe 0x479250): a ray from the camera to the sun every TimeShootRay s
        const rc::CollisionMesh& coll = level->collision();
        sunFlare.update(paused ? 0.f : std::min(dt,0.1f),camera.position,camera.forward(),viewProj,int(w()),int(h()),
                        [&coll](const rc::Vec3& from, const rc::Vec3& to) {
                          const rc::Vec3 d = to-from;
                          return coll.raycast(from,d,0.98f).has_value(); // not the sky the sun stands on
                          });
        sunView->draw(enc,frameId,int(w()),int(h()),sunFlare);
        }
      if(level && showCollision)
        level->collisionView().draw(enc,push);
      if(race && vehicle) {
        Hud::Info info = hudInfo();
        info.gates    = showGates;
        info.viewProj = viewProj;
        hud->draw(enc,frameId,int(w()),int(h()),*race,info);
        // the direction arrow over everything, in a depth buffer of its own
        if(race->phase()!=rc::Race::Phase::Finished && !checkpoints->empty() && showArrow && !demo.on) {
          enc.setFramebuffer({{swapchain[swapchain.currentImage()],Tempest::Preserve,Tempest::Preserve}},
                             {zbuffer,1.f,Tempest::Discard});
          arrow->draw(enc,int(w()),int(h()));
          }
        }
      if(menu->active())
        menu->draw(enc,frameId,int(w()),int(h()),{});
      fps->draw(enc,frameId,int(w()),int(h()));
    }
    fps->drawDone();

    sync = device.submit(cmd);
    device.present(swapchain);
    fps->presented();
    }
  catch(const Tempest::SwapchainSuboptimal&) {
    resetSwapchain();
    update();
    }
  }
