#include "mainwindow.h"

#include <Tempest/Application>
#include <Tempest/SystemApi>
#include <Tempest/Except>
#include <Tempest/Log>

#include <cmath>
#include <iterator>
#include <sstream>
#include <string>

using namespace Tempest;

static const char* const Cars[] = {"Car1", "Car2", "Car3"}; // Overkill, Buggy, Hummer

static const char* const Tracks[] = {
  "beach_1",  "beach_2",  "beach_3",  "beach_4",
  "country_1","country_2","country_3","country_4",
  "urban_1",  "urban_2",
  };

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

MainWindow::MainWindow(Device& device, Resources& res, const std::string& name, bool dev)
  : Window(Maximized), device(device), resources(res), swapchain(device,hwnd()),
    library([&res](const std::string& path){ return res.readFile(path); }), devMode(dev) {
  setWindowTitle("OpenRCCars");
  resetSwapchain();
  carScene = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
  hud      = std::make_unique<Hud>(device,res);
  arrow    = std::make_unique<DirectionArrow>(device,res);
  effects  = std::make_unique<Effects>(device,res);
  sounds   = std::make_unique<Sounds>(res);
  characterView = std::make_unique<CharacterView>(device);
  trafficView   = std::make_unique<TrafficView>(device,res);
  chase    = ChaseCamera(ChaseCamera::Settings::load([&res](const std::string& f){ return res.pack().read(f); }));
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
  // the codes registered with -dev (0x480568, 0x482728, 0x4e9cc0, 0x4fcb40, 0x5313a8); stat, burst, road and the
  // first constr only flip flags nothing reads, so they are left out
  if(devMode)
    for(const char* c:{"DT0","DT1","DT2","DT3","DT4","DT5","DTX","CONSTR","C1","F","E1","E2","I"})
      devCodes.push_back({c});
  loadCar(0);
  loadLevel(name);
  sounds->startMusic();
  lastTick = Application::tickCount();
  }

MainWindow::~MainWindow() {
  device.waitIdle();
  }

void MainWindow::loadLevel(const std::string& name) {
  device.waitIdle();
  try {
    auto next = std::make_unique<Level>(device,resources,name);
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
    race        = std::make_unique<rc::Race>(*checkpoints,laps);
    resetVolumes = rc::ResetVolumes(level->scene());
    fog.setTarget(rc::FogParams::load(resources.pack(),name));
    hud->setTrack(name);
    sounds->setTrack(level->scene(),level->mesh().seaCoast());
    effects->setTrack(level->scene(),level->mesh().mainSea());
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
  car   = std::make_unique<ModelMesh>(device,resources,carScene,Cars[id]);
  if(carSound)
    carSound->stop();
  carSound = std::make_unique<rc::CarSound>(sounds->mixer(),sounds->library(),int(id));
  setupPhysics();
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
                                                [this](int32_t c){ return rc::CarParams::load(resources.pack(),c); });
    }
  catch(const std::exception& e) {
    Log::e("opponents: ",e.what());
    return;
    }
  opponents->setTrack(level->scene());
  for(size_t i=0; i<opponents->size(); ++i) {
    auto& o   = (*opponents)[i];
    auto& m   = aiMeshes[{o.car,o.skin}];
    if(m==nullptr)
      m = std::make_unique<ModelMesh>(device,resources,carScene,Cars[o.car],rc::Vec3{},o.skin);
    }
  // the channels stay in the mixer, so they are made once and reused
  while(aiSounds.size()<opponents->size())
    aiSounds.emplace_back(sounds->mixer(),sounds->library());
  Log::i("opponents (",rc::Opponents::levelName(aiLevel),"): ",opponents->size());
  }

void MainWindow::setupPhysics(bool restartRace, bool again) {
  if(level==nullptr || car==nullptr)
    return;
  carPose = level->carStart(car->bboxMin().y);
  vehicle.reset();
  if(physics==nullptr) {
    physics = std::make_unique<rc::PhysicsWorld>(level->collision());
    objects = std::make_unique<Objects>(device,resources,library,level->scene(),*physics);
    try {
      characters = std::make_unique<rc::Characters>(*physics,library,level->scene(),level->name(),
                                                    [this](const std::string& f){ return resources.pack().read(f); });
      Log::i("characters: ",characters->size());
      }
    catch(const std::exception& e) {
      Log::e("characters: ",e.what());
      }
    try {
      traffic = std::make_unique<rc::Traffic>(*physics,library,level->scene(),level->name(),
                                              [this](const std::string& f){ return resources.pack().read(f); });
      Log::i("road cars: ",traffic->size());
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
  const auto prm  = rc::CarParams::load(resources.pack(),int32_t(carId+1),upgrade);
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
    if(characters)
      characters->restart();
    if(traffic)
      traffic->restart();
    playerFinish = -1;
    paused       = false;
    aiEmu.restart();
    sinceBumper.clear();
    }
  lastCarPos = carPose.translation();
  resetVolumes.reset();
  chase.reset(chaseCar());
  Log::i("car ",Cars[carId],", upgrade ",upgrade,": top speed ",prm.topSpeed*3.6f," km/h, grip ",prm.gripFront);
  }

void MainWindow::respawnCar() {
  if(level==nullptr || vehicle==nullptr)
    return;
  carPose = checkpoints->respawn(car->bboxMin().y);
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
  c.pose     = carPose;
  // where the car is drawn: the physics' own position runs in fixed steps and would shake the view against the car
  c.centre   = vehicle ? vehicle->drawnCentre() : carPose.translation();
  c.velocity = vehicle ? vehicle->velocity() : rc::Vec3{};
  c.spin     = vehicle ? vehicle->localSpin().length() : 0.f;
  // RCCars.exe 0x500c1d: the boost and the gas keys, held
  const bool control = race && !race->holdCar() && zeroCam==ZeroCam::Off && !flying;
  c.boost    = control && (keys[Event::K_LShift] || keys[Event::K_RShift]) && (keys[Event::K_W] || keys[Event::K_Up]);
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
  info.paused = paused;
  info.lowSignal = resetVolumes.lowSignal();
  info.carPos     = carPose.translation();
  info.carForward = carPose.vector({0,0,1});
  if(opponents && opponents->size()>0) {
    info.place     = opponents->place(playerFinish);
    info.standings = opponents->standings(playerFinish);
    for(size_t i=0; i<opponents->size(); ++i) {
      const rc::Mat4 p = (*opponents)[i].pose();
      info.opponents.push_back({p.translation(),p.vector({0,0,1})});
      }
    }
  return info;
  }

float MainWindow::wayDirection() const {
  // the point the arrow shows seen from the car, positive = on the left (+X)
  const auto&    route = checkpoints->route();
  const rc::Vec3 f     = carPose.vector({0,0,1});
  if(route.empty() || std::abs(f.x)+std::abs(f.z)<=1e-3f)
    return 0;
  const rc::Vec3 to = route[checkpoints->guide()].position-carPose.translation();
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
  // RCCars.exe 0x4eb670: the way still to go to the next main checkpoint over the way from the last one to it,
  // along the route: to the point the arrow shows, then from point to point
  const rc::Vec3 at   = carPose.translation();
  const size_t   next = checkpoints->next(), guide = checkpoints->guide();
  const size_t   last = checkpoints->lastPassed();
  const float    from = last+1<route.size() ? checkpoints->along(last) : 0.f;
  const rc::Vec3 d    = route[guide].position-at;
  const float    left = std::sqrt(d.x*d.x+d.z*d.z)+checkpoints->along(next)-checkpoints->along(guide);
  const float    way  = checkpoints->along(next)-from;
  in.left     = way>1e-3f ? left/way : 1.f;
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

std::vector<rc::Mat4> MainWindow::carPartPoses() const {
  if(vehicle==nullptr) {
    std::vector<rc::Mat4> ret;
    for(auto& p:car->parts())
      ret.push_back(rc::Mat4::translate(p.pivot));
    return ret;
    }
  return partPoses(*car,vehicle->geometry(),vehicle->rigPose());
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
  keys[e.key] = true;
  if(e.key==Event::K_ESCAPE)
    Tempest::SystemApi::exit();
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
  if(devMode && e.key==Event::K_F11)
    Tempest::SystemApi::exit();
  // F1..F10 - switch track (in the developer mode F9 flies, Shift+F9 is urban_1)
  if(e.key>=Event::K_F1 && e.key<=Event::K_F10) {
    const size_t id = size_t(e.key-Event::K_F1);
    if(id<std::size(Tracks))
      loadLevel(Tracks[id]);
    }
  // 1..3 - switch car
  if(e.key>=Event::K_1 && e.key<=Event::K_3)
    loadCar(size_t(e.key-Event::K_1));
  // U - next upgrade level (boosters, resonators and tires together)
  if(e.key==Event::K_U) {
    upgrade = (upgrade+1)%4;
    const rc::Mat4 at = carPose;
    setupPhysics(false);
    if(vehicle)
      vehicle->reset(at);   // same place, standing still
    }
  if(e.key==Event::K_Space && zeroCam==ZeroCam::Off && !flying)
    jumpPressed = true;
  // our debug keys, only in the developer mode (-dev)
  if(devMode && e.key==Event::K_T && vehicle && !flying)
    vehicle->recover();
  if(devMode && e.key==Event::K_G)
    showGates = !showGates;
  // R: back to the last checkpoint, as in the original
  if(e.key==Event::K_R && level && !paused) {
    setFlying(false);
    respawnCar();
    }
  // Enter: the race again from the countdown ("Jeszcze raz"), L: 3, 5 or 7 laps (and the race again)
  if(e.key==Event::K_Return && level)
    setupPhysics(true,true);
  if(e.key==Event::K_L && level) {
    laps = (laps>=7) ? 3 : laps+2;
    race->setLaps(laps);
    setupPhysics(true,true);
    }
  // N (developer mode): straight to the next checkpoint, to try out laps and the finish
  if(devMode && e.key==Event::K_N && level && vehicle && !checkpoints->empty() && !race->holdCar()) {
    checkpoints->skipWay();
    const auto&    p   = checkpoints->route()[checkpoints->next()];
    const float    yaw = std::atan2(p.dir.x,p.dir.z)*180.f/3.14159265f;
    rc::Mat4       at  = rc::Mat4::translate(p.position)*rc::Mat4::rotate(1,yaw);
    if(auto g = level->collision().groundHeight(p.position+rc::Vec3{0,1.f,0}))
      at.m[1][3] = *g-car->bboxMin().y;
    carPose = at;
    flightPose = at;
    vehicle->reset(carPose);
    lastCarPos = carPose.translation();
    if(opponents)
      opponents->playerMoved();
    }
  // O: the opponents off, easy, normal, hard (and the race again)
  if(e.key==Event::K_O && level) {
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
  fog.tick(dt);
  if(vehicle!=nullptr) {
    rc::Vehicle::Input in;
    if(race->holdCar() || shotTime>=0 || resetVolumes.lowSignal()) {
      // waiting for the start, shot (RCCars.exe 0x4f6b20) or in a reset volume (0x4f6c87): no keys, Brake held
      in.handbrake = true;
      }
    else if(zeroCam==ZeroCam::Off && !flying) {
      const bool up    = keys[Event::K_W] || keys[Event::K_Up];
      const bool down  = keys[Event::K_S] || keys[Event::K_Down];
      const bool left  = keys[Event::K_A] || keys[Event::K_Left];
      const bool right = keys[Event::K_D] || keys[Event::K_Right];
      in.throttle  = (up ? 1.f : 0.f) - (down ? 1.f : 0.f);
      in.steer     = (left ? 1.f : 0.f) - (right ? 1.f : 0.f);
      in.handbrake = keys[Event::K_B];
      in.boost     = keys[Event::K_LShift] || keys[Event::K_RShift];
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
    auto hit = vehicle->hit();
    if(opponents)
      hit = std::max(hit,opponents->collide(*vehicle,dt));
    if(hit!=rc::Vehicle::Hit::None) {
      hud->hit(hit==rc::Vehicle::Hit::Great);
      Log::i(hit==rc::Vehicle::Hit::Great ? "great hit" : "hit"," at ",Hud::formatTime(race->time()));
      }
    if(opponents)
      opponents->update(dt,!race->holdCar(),carPose.translation(),checkpoints->laps(),playerFinish>=0,race->time(),
                        race->laps());
    physics->step(dt);
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
    const auto     ev  = race->update(dt,at,(at-lastCarPos)*(1.f/std::max(dt,1e-4f)));
    if(ev==rc::Race::Event::Finish) {
      playerFinish = race->time();
      if(opponents && opponents->size()>0)
        Log::i("finished ",opponents->place(playerFinish),". of ",opponents->size()+1," in ",Hud::formatTime(playerFinish));
      }
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
      const bool gas     = driving && (keys[Event::K_W] || keys[Event::K_Up]);
      const bool brake   = driving && (keys[Event::K_S] || keys[Event::K_Down]);
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

void MainWindow::tell(uint32_t id, int car, std::string_view what) {
  // the player's name: the original's profile (Players\\<name>), ours is the table's word for a player
  const std::string who = car<0 || opponents==nullptr || size_t(car)>=opponents->size()
                        ? std::string(hud->gameText(40507)) : (*opponents)[size_t(car)].name;
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

void MainWindow::tickSounds(float dt) {
  const bool racing = race->phase()==rc::Race::Phase::Racing;
  const bool control = !race->holdCar() && zeroCam==ZeroCam::Off && !flying;
  const float fwd   = vehicle->speed();
  airborne = !vehicle->grounded() ? airborne+dt : 0.f;

  rc::CarSound::Input in;
  in.speed    = std::abs(fwd)*3.6f;
  in.gas      = control && (keys[Event::K_W] || keys[Event::K_Up]);
  in.back     = control && (keys[Event::K_S] || keys[Event::K_Down]);
  in.braking  = (in.back && fwd*3.6f>1.f) || (control && keys[Event::K_B]);
  in.airborne = airborne>0.05f;
  in.racing   = racing;
  in.boost    = vehicle->boosting();
  in.jump     = control && keys[Event::K_Space];
  in.inWater  = vehicle->submerged()>0;
  in.ground   = vehicle->groundSound();
  in.contact  = {vehicle->contact().surface,vehicle->contact().hit,vehicle->contact().impact};
  in.carBump  = vehicle->carBump();
  in.objBump  = (characters && characters->bumpedPlayer()) || (traffic && traffic->bumpedPlayer());
  in.shot     = shotSound;
  shotSound   = false;
  carSound->update(in,dt);

  // the computer's cars and the water, heard from the camera
  const rc::Vec3 ear   = camera.position;
  const rc::Vec3 right = camera.right();
  sounds->tickAmbient(ear,right,dt,level->collision().highestWater(ear,10.f));
  if(characters)
    sounds->tickCharacters(*characters,ear,right);
  for(auto& g:effects->gunSounds())
    sounds->playAt(g.kind==rc::fx::Gunfire::Sound::Burst ? "burst" : "bullet",g.at,ear,right);
  if(traffic)
    sounds->tickTraffic(*traffic,ear,right);
  for(size_t i=0; opponents && i<opponents->size() && i<aiSounds.size(); ++i) {
    const auto&    o  = (*opponents)[i];
    const rc::Vec3 to = o.pose().translation()-ear;
    const float    d  = to.length();
    const rc::Vec3 at = o.pose().translation();
    const auto     wl = level->collision().waterLevel(at,0.5f);
    aiSounds[i].update(std::abs(o.speed())*3.6f,racing,d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f,
                       wl && *wl>at.y);
    }

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

void MainWindow::render() {
  try {
    const uint64_t now = Application::tickCount();
    const float    dt  = float(now-lastTick)/1000.f;
    lastTick = now;
    if(!paused && (zeroCam!=ZeroCam::Detached || frameStep))
      time  += zeroCam==ZeroCam::Detached ? (fixedTime>0 ? fixedTime/1000.f : lastDt) : std::min(dt,0.1f);
    tick(std::min(dt,0.1f));
    hud->tick(std::min(dt,0.1f));

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
    if(car) {
      const rc::Vec3 at = carPose.translation();
      push.carPos[0] = at.x; push.carPos[1] = at.y; push.carPos[2] = at.z;
      }

    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},
                         {zbuffer,1.f,Tempest::Preserve});
      if(level)
        level->mesh().draw(enc,push,LevelMesh::Pass::Opaque);
      if(objects)
        objects->draw(enc,viewProj,push);
      if(characters)
        characterView->draw(enc,frameId,resources,*characters,push);
      if(traffic)
        trafficView->draw(enc,frameId,*traffic,viewProj,push,level ? &level->collision() : nullptr);
      if(car) {
        const auto parts = carPartPoses();
        car->setDirt(forcedDirt>=0 ? float(forcedDirt) : effects->dirt());
        car->draw(enc,viewProj,push,carPose,&parts);
        }
      if(opponents) {
        for(size_t i=0; i<opponents->size(); ++i) {
          const auto& o = (*opponents)[i];
          auto        m = aiMeshes.find({o.car,o.skin});
          if(m==aiMeshes.end())
            continue;
          const auto parts = opponentPartPoses(*m->second,o);
          m->second->draw(enc,viewProj,push,o.pose(),&parts);
          }
        }
      effects->setFog(push.cameraPos[3],push.fogColor);
      if(vehicle)
        effects->drawTracks(enc,frameId,viewProj,camera.position);
      if(level)
        level->mesh().draw(enc,push,LevelMesh::Pass::Translucent);
      if(vehicle)
        effects->draw(enc,frameId,int(w()),int(h()),viewProj,camera.position);
      if(level && showCollision)
        level->collisionView().draw(enc,push);
      if(race && vehicle) {
        Hud::Info info = hudInfo();
        info.gates    = showGates;
        info.viewProj = viewProj;
        hud->draw(enc,frameId,int(w()),int(h()),*race,info);
        // the direction arrow over everything, in a depth buffer of its own
        if(race->phase()!=rc::Race::Phase::Finished && !checkpoints->empty()) {
          enc.setFramebuffer({{swapchain[swapchain.currentImage()],Tempest::Preserve,Tempest::Preserve}},
                             {zbuffer,1.f,Tempest::Discard});
          arrow->draw(enc,int(w()),int(h()));
          }
        }
    }

    sync = device.submit(cmd);
    device.present(swapchain);
    }
  catch(const Tempest::SwapchainSuboptimal&) {
    resetSwapchain();
    update();
    }
  }
