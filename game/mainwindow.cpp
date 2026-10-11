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
  : Window(Maximized), device(device), resources(res), swapchain(device,hwnd()) {
  resetSwapchain();
  carScene = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
  loadCar(0);
  loadLevel(name);
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
    physics.reset();
    level   = std::move(next);
    camera  = level->startCamera();
    setupPhysics();
    setWindowTitle(("OpenRCCars - "+name).c_str());
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
  car   = std::make_unique<CarMesh>(device,resources,carScene,Cars[id]);
  setupPhysics();
  }

void MainWindow::setupPhysics() {
  if(level==nullptr || car==nullptr)
    return;
  carPose = level->carStart(car->bboxMin().y);
  vehicle.reset();
  if(physics==nullptr)
    physics = std::make_unique<rc::PhysicsWorld>(level->collision());
  const auto geom = rc::carGeometry(carScene,Cars[carId]);
  const auto prm  = rc::CarParams::load(resources.pack(),int32_t(carId+1),upgrade);
  vehicle  = std::make_unique<rc::Vehicle>(*physics,geom,prm,carPose);
  chaseYaw = level->startCamera().yaw;
  Log::i("car ",Cars[carId],", upgrade ",upgrade,": top speed ",prm.topSpeed*3.6f," km/h, grip ",prm.gripFront);
  }

void MainWindow::resetCar() {
  if(level==nullptr || vehicle==nullptr)
    return;
  carPose = level->carStart(car->bboxMin().y);
  vehicle->reset(carPose);
  upsideDown = 0;
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
    setupPhysics();
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
  if(e.key==Event::K_R && level) {
    camera = level->startCamera();
    resetCar();
    chaseYaw = camera.yaw;
    }
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
  if(vehicle!=nullptr) {
    rc::Vehicle::Input in;
    if(!freeCamera) {
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
    physics->step(dt);
    carPose = vehicle->pose();

    // on its roof or side for a while: back on the wheels
    upsideDown = carPose.vector({0,1,0}).y<0.3f ? upsideDown+dt : 0;
    if(upsideDown>2.f) {
      vehicle->recover();
      upsideDown = 0;
      }
    // fell off the world
    auto& grid = level->scene().collision;
    if(!grid.empty() && carPose.translation().y<grid.min.y-20.f)
      resetCar();
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

void MainWindow::render() {
  try {
    const uint64_t now = Application::tickCount();
    const float    dt  = float(now-lastTick)/1000.f;
    lastTick = now;
    tick(std::min(dt,0.1f));

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

    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{swapchain[swapchain.currentImage()],Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},
                         {zbuffer,1.f,Tempest::Preserve});
      if(level)
        level->mesh().draw(enc,push);
      if(car) {
        const auto parts = carPartPoses();
        car->draw(enc,viewProj,push,carPose,&parts);
        }
      if(level && showCollision)
        level->collisionView().draw(enc,push);
    }

    sync = device.submit(cmd);
    device.present(swapchain);

    fpsTime += dt;
    fpsCount++;
    if(fpsTime>0.25f) {
      std::string title = "OpenRCCars - "+(level ? level->name() : std::string())+
                          " - "+std::to_string(int(float(fpsCount)/fpsTime))+" fps";
      if(vehicle)
        title += " - "+std::to_string(int(std::round(std::abs(vehicle->speed())*3.6f)))+" km/h"+
                 " - turbo "+std::to_string(int(std::round(vehicle->boostLevel()*100.f)))+"%"+
                 (upgrade>0 ? " - upgrade "+std::to_string(upgrade) : std::string());
      setWindowTitle(title.c_str());
      fpsTime  = 0;
      fpsCount = 0;
      }
    }
  catch(const Tempest::SwapchainSuboptimal&) {
    resetSwapchain();
    update();
    }
  }
