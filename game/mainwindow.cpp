#include "mainwindow.h"

#include <Tempest/Application>
#include <Tempest/SystemApi>
#include <Tempest/Except>
#include <Tempest/Log>

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
  loadCar(Cars[0]);
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
    level   = std::move(next);
    camera  = level->startCamera();
    carPose = level->carStart(car->bboxMin().y);
    setWindowTitle(("OpenRCCars - "+name).c_str());
    }
  catch(const std::exception& e) {
    Log::e("unable to load track \"",name,"\": ",e.what());
    if(level==nullptr)
      throw; // nothing to show at all
    }
  }

void MainWindow::loadCar(const std::string& model) {
  device.waitIdle();
  car = std::make_unique<CarMesh>(device,resources,carScene,model);
  if(level)
    carPose = level->carStart(car->bboxMin().y);
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
    loadCar(Cars[e.key-Event::K_1]);
  if(e.key==Event::K_V) {
    freeCamera = !freeCamera;
    if(freeCamera)
      camera.chase(carPose,ChaseDistance,ChaseHeight); // free flight starts from the chase view
    }
  if(e.key==Event::K_R && level) {
    camera  = level->startCamera();
    carPose = level->carStart(car->bboxMin().y);
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
  if(!freeCamera) {
    camera.chase(carPose,ChaseDistance,ChaseHeight);
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
      if(car)
        car->draw(enc,viewProj,push,carPose);
    }

    sync = device.submit(cmd);
    device.present(swapchain);

    fpsTime += dt;
    fpsCount++;
    if(fpsTime>1.f) {
      std::string title = "OpenRCCars - "+(level ? level->name() : std::string())+
                          " - "+std::to_string(int(float(fpsCount)/fpsTime))+" fps";
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
