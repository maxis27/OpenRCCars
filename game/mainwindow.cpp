#include "mainwindow.h"

#include <Tempest/Application>
#include <Tempest/SystemApi>
#include <Tempest/Except>
#include <Tempest/Log>

#include <cmath>
#include <iterator>
#include <string>

using namespace Tempest;

static const char* const Cars[] = {"Car1", "Car2", "Car3"}; // Overkill, Buggy, Hummer

static const char* const Tracks[] = {
  "beach_1",  "beach_2",  "beach_3",  "beach_4",
  "country_1","country_2","country_3","country_4",
  "urban_1",  "urban_2",
  };

MainWindow::MainWindow(Device& device, Resources& res, const std::string& name)
  : Window(Maximized), device(device), resources(res), swapchain(device,hwnd()),
    library([&res](const std::string& path){ return res.readFile(path); }) {
  setWindowTitle("OpenRCCars");
  resetSwapchain();
  carScene = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
  hud      = std::make_unique<Hud>(device,res);
  arrow    = std::make_unique<DirectionArrow>(device,res);
  sounds   = std::make_unique<Sounds>(res);
  characterView = std::make_unique<CharacterView>(device);
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
    characters.reset();
    objects.reset();
    physics.reset();
    race.reset();
    checkpoints.reset();
    level   = std::move(next);
    checkpoints = std::make_unique<rc::Checkpoints>(level->scene(),level->collision());
    race        = std::make_unique<rc::Race>(*checkpoints,laps);
    hud->setTrack(name);
    sounds->setTrack(level->scene());
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
  arrow->reset();
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
    playerFinish = -1;
    paused       = false;
    }
  lastCarPos = carPose.translation();
  chaseYaw = level->startCamera().yaw;
  Log::i("car ",Cars[carId],", upgrade ",upgrade,": top speed ",prm.topSpeed*3.6f," km/h, grip ",prm.gripFront);
  }

void MainWindow::respawnCar() {
  if(level==nullptr || vehicle==nullptr)
    return;
  carPose = checkpoints->respawn(car->bboxMin().y);
  vehicle->reset(carPose);
  sounds->play("Checkpoint","cp_reset");
  besideDone = true;
  if(opponents)
    opponents->playerMoved();
  lastCarPos = carPose.translation();
  upsideDown = 0;
  const rc::Vec3 f = carPose.vector({0,0,1});
  chaseYaw = std::atan2(f.x,f.z)*180.f/3.14159265f;
  }

Hud::Info MainWindow::hudInfo() const {
  Hud::Info info;
  info.speed  = std::abs(vehicle->speed())*3.6f;
  info.turbo  = vehicle->boostLevel();
  info.turboMax = vehicle->params().boostTank/100.f;
  info.speedMax = vehicle->params().boostTopSpeed*3.6f;
  info.paused = paused;
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

std::vector<rc::Mat4> MainWindow::carPartPoses() const {
  std::vector<rc::Mat4> ret;
  for(auto& p:car->parts()) {
    rc::Mat4 m = rc::Mat4::translate(p.pivot);
    if(vehicle!=nullptr && !p.name.empty()) {
      auto& wheels = vehicle->geometry().wheels;
      for(size_t i=0; i<wheels.size(); ++i)
        if(wheels[i].name==p.name)
          m = vehicle->wheelPose(i);
      }
    ret.push_back(m);
    }
  return ret;
  }

std::vector<rc::Mat4> MainWindow::opponentPartPoses(const ModelMesh& mesh, const rc::Opponent& o) const {
  std::vector<rc::Mat4> ret;
  auto& wheels = o.geometry().wheels;
  for(auto& p:mesh.parts()) {
    rc::Mat4 m = rc::Mat4::translate(p.pivot);
    for(size_t i=0; i<wheels.size() && !p.name.empty(); ++i)
      if(wheels[i].name==p.name)
        m = o.wheelPose(i);
    ret.push_back(m);
    }
  return ret;
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
  // F1..F10 - switch track
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
  if(e.key==Event::K_T && vehicle)
    vehicle->recover();
  if(e.key==Event::K_Space && !freeCamera)
    jumpPressed = true;
  if(e.key==Event::K_V) {
    freeCamera = !freeCamera;
    if(freeCamera)
      camera.chase(carPose,ChaseDistance,ChaseHeight); // free flight starts from the chase view
    }
  if(e.key==Event::K_K)
    showCollision = !showCollision;
  if(e.key==Event::K_G)
    showGates = !showGates;
  // R: back to the last checkpoint, as in the original
  if(e.key==Event::K_R && level && !paused)
    respawnCar();
  // Enter: the race again from the countdown ("Jeszcze raz"), L: 3, 5 or 7 laps (and the race again)
  if(e.key==Event::K_Return && level)
    setupPhysics(true,true);
  if(e.key==Event::K_L && level) {
    laps = (laps>=7) ? 3 : laps+2;
    race->setLaps(laps);
    setupPhysics(true,true);
    }
  // N (debug): straight to the next checkpoint, to try out laps and the finish
  if(e.key==Event::K_N && level && vehicle && !checkpoints->empty() && !race->holdCar()) {
    checkpoints->skipWay();
    const auto&    p   = checkpoints->route()[checkpoints->next()];
    const float    yaw = std::atan2(p.dir.x,p.dir.z)*180.f/3.14159265f;
    rc::Mat4       at  = rc::Mat4::translate(p.position)*rc::Mat4::rotate(1,yaw);
    if(auto g = level->collision().groundHeight(p.position+rc::Vec3{0,1.f,0}))
      at.m[1][3] = *g-car->bboxMin().y;
    carPose = at;
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

void MainWindow::mouseDownEvent(MouseEvent& e) {
  mousePos = e.pos();
  }

void MainWindow::mouseDragEvent(MouseEvent& e) {
  const auto d = e.pos()-mousePos;
  mousePos = e.pos();
  if(freeCamera)
    camera.rotate(float(d.x)*0.2f,-float(d.y)*0.2f);
  }

void MainWindow::mouseWheelEvent(MouseEvent& e) {
  speed = (e.delta>0) ? speed*1.25f : speed/1.25f;
  }

void MainWindow::tick(float dt) {
  sounds->tick();
  sounds->setPaused(paused);
  if(paused)
    return;
  if(vehicle!=nullptr) {
    rc::Vehicle::Input in;
    if(race->holdCar()) {
      in.handbrake = true;  // waiting for the start
      }
    else if(!freeCamera) {
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
    // the people and animals, after the step: a person holding the car puts it in the hand
    if(characters) {
      std::vector<rc::Characters::Car> others;
      for(size_t i=0; opponents && i<opponents->size(); ++i) {
        const rc::Mat4 p = (*opponents)[i].pose();
        others.push_back({p.translation(),p.vector({0,0,1})*(*opponents)[i].speed()});
        }
      if(characters->update(dt,vehicle.get(),others)) {
        hud->hit(true);
        Log::i("great hit (character) at ",Hud::formatTime(race->time()));
        }
      }
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

    // on its roof or side for a while: back on the wheels
    upsideDown = carPose.vector({0,1,0}).y<0.3f ? upsideDown+dt : 0;
    if(upsideDown>2.f) {
      vehicle->recover();
      upsideDown = 0;
      }
    // drowned or fell off the world: back to the last checkpoint
    auto& grid = level->scene().collision;
    if(vehicle->drowned() || (!grid.empty() && carPose.translation().y<grid.min.y-20.f)) {
      respawnCar();
      }
    tickSounds(dt);
    }

  if(!freeCamera) {
    // follow the heading with a lag, ignoring roll and pitch
    const rc::Vec3 f = carPose.vector({0,0,1});
    if(std::abs(f.x)+std::abs(f.z)>1e-3f) {
      const float yaw = std::atan2(f.x,f.z)*180.f/3.14159265f;
      float d = yaw-chaseYaw;
      while(d> 180.f) d -= 360.f;
      while(d<-180.f) d += 360.f;
      chaseYaw += d*std::min(1.f,dt*5.f);
      }
    const rc::Mat4 target = rc::Mat4::translate(carPose.translation())*rc::Mat4::rotate(1,chaseYaw);
    camera.chase(target,ChaseDistance,ChaseHeight);
    return;
    }
  const float s = speed*dt*((keys[Event::K_LShift] || keys[Event::K_RShift]) ? 4.f : 1.f);
  float f = 0, r = 0, u = 0;
  if(keys[Event::K_W]) f += s;
  if(keys[Event::K_S]) f -= s;
  if(keys[Event::K_D]) r += s;
  if(keys[Event::K_A]) r -= s;
  if(keys[Event::K_E] || keys[Event::K_Space]) u += s;
  if(keys[Event::K_Q] || keys[Event::K_C])     u -= s;
  camera.move(f,r,u);

  const float rs = 90.f*dt;
  if(keys[Event::K_Left])  camera.rotate(-rs,0);
  if(keys[Event::K_Right]) camera.rotate( rs,0);
  if(keys[Event::K_Up])    camera.rotate(0, rs);
  if(keys[Event::K_Down])  camera.rotate(0,-rs);
  }

void MainWindow::tickSounds(float dt) {
  const bool racing = race->phase()==rc::Race::Phase::Racing;
  const bool control = !race->holdCar() && !freeCamera;
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
  carSound->update(in,dt);

  // the computer's cars and the water, heard from the camera
  const rc::Vec3 ear   = camera.position;
  const rc::Vec3 right = camera.right();
  sounds->tickAmbient(ear,right,dt);
  if(characters)
    sounds->tickCharacters(*characters,ear,right);
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
    if(!paused)
      time  += std::min(dt,0.1f);
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
    push.cameraPos[3] = 250.f;                 // fog distance
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
      if(car) {
        const auto parts = carPartPoses();
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
      if(level)
        level->mesh().draw(enc,push,LevelMesh::Pass::Translucent);
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
