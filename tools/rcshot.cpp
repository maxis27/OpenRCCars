// rcshot - offscreen render of a track to PNG (handy for testing without a window / on CI).
//
//   rcshot <game_dir> <track> <out.png> [width height] [x y z yaw pitch]
//   the player's car stands on the start; without camera arguments the camera follows it.
//   env RCSHOT_CAR=Car1|Car2|Car3|none picks the car (default Car1), none = track only, start camera behind cp_1.
//   env RCSHOT_NOSKY=1 hides the sky dome (useful for top-down views).

#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/Pixmap>
#include <Tempest/VulkanApi>

#include <cstdlib>
#include <memory>
#include <string>

#include "game/resources.h"
#include "game/world/carmesh.h"
#include "game/world/level.h"

using namespace Tempest;

int main(int argc, const char** argv) {
  if(argc<4) {
    Log::e("usage: rcshot <game_dir> <track> <out.png> [width height] [x y z yaw pitch]");
    return 1;
    }
  try {
    const uint32_t w = argc>5 ? uint32_t(std::atoi(argv[4])) : 1280;
    const uint32_t h = argc>5 ? uint32_t(std::atoi(argv[5])) : 720;

    VulkanApi api{ApiFlags::NoFlags};
    Device    device{api};
    Log::i("GPU: ",device.properties().name);

    Resources res(device,argv[1]);
    Level     level(device,res,argv[2]);
    Camera    cam = level.startCamera();

    const char* carName = std::getenv("RCSHOT_CAR");
    std::unique_ptr<CarMesh> car;
    rc::Mat4                 carPose;
    rc::SbScene              carSb;
    if(carName==nullptr || std::string(carName)!="none") {
      carSb   = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      car     = std::make_unique<CarMesh>(device,res,carSb,carName ? carName : "Car1");
      carPose = level.carStart(car->bboxMin().y);
      cam.chase(carPose,ChaseDistance,ChaseHeight);
      }
    if(argc>10) {
      cam.position = {float(std::atof(argv[6])),float(std::atof(argv[7])),float(std::atof(argv[8]))};
      cam.yaw      = float(std::atof(argv[9]));
      cam.pitch    = float(std::atof(argv[10]));
      }
    Log::i("camera: ",cam.position.x," ",cam.position.y," ",cam.position.z," yaw=",cam.yaw," pitch=",cam.pitch);

    auto color = device.attachment(TextureFormat::RGBA8,w,h);
    auto depth = device.zbuffer(TextureFormat::Depth32F,w,h);

    LevelMesh::Push push;
    const rc::Mat4  viewProj = cam.viewProj(float(w)/float(h));
    viewProj.toColumnMajor(push.viewProj);
    push.cameraPos[0] = cam.position.x;
    push.cameraPos[1] = cam.position.y;
    push.cameraPos[2] = cam.position.z;
    push.cameraPos[3] = 250.f;
    push.sunDir[0] = 0.4f; push.sunDir[1] = 0.8f; push.sunDir[2] = 0.3f;
    push.sunDir[3] = 1.0f;

    CommandBuffer cmd;
    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{color,Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},{depth,1.f,Tempest::Preserve});
      level.mesh().draw(enc,push,std::getenv("RCSHOT_NOSKY")==nullptr);
      if(car)
        car->draw(enc,viewProj,push,carPose);
    }
    auto sync = device.submit(cmd);
    sync.wait();

    auto pm = device.readPixels(color);
    pm.save(argv[3]);
    Log::i("saved ",argv[3]);
    return 0;
    }
  catch(const std::exception& e) {
    Log::e("error: ",e.what());
    return 2;
    }
  }
