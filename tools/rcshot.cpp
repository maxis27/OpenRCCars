// rcshot - offscreen render of a track to PNG (handy for testing without a window / on CI).
//
//   rcshot <game_dir> <track> <out.png> [width height] [x y z yaw pitch]
//   the player's car stands on the start; without camera arguments the camera follows it.
//   env RCSHOT_CAR=Car1|Car2|Car3|none picks the car (default Car1), none = track only, start camera behind cp_1.
//   env RCSHOT_NOSKY=1 hides the sky dome (useful for top-down views).
//   env RCSHOT_COLL=1 draws the collision overlay (key K in the game).
//   env RCSHOT_DRIVE=<seconds> drives the car (physics) with full throttle first, RCSHOT_STEER=<-1..1> steering
//   (positive = left) for the last half of that time, RCSHOT_JUMP=<seconds> jumps at that moment,
//   RCSHOT_BOOST=1 holds the boost, RCSHOT_START="x z yaw" puts the car somewhere else first.
//   env RCSHOT_TIME=<seconds> is the moment of the water animation (default: the drive time, else 0).
//   env RCSHOT_FOG=0 draws without the track's fog (Settings\fog_<track>.ini).
//   env RCSHOT_OBJECTS=0 hides the objects placed on the track (props, characters, road cars).
//   env RCSHOT_ACTOR=<instance name> looks at a person or animal from RCSHOT_ACTOR_DIST m (default 2.5) after
//   RCSHOT_ACTOR_TIME seconds (default 1) with the car parked RCSHOT_ACTOR_CAR m in front of it (default 2), and
//   RCSHOT_ACTOR_SIDE degrees round it (default 30).

#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/Pixmap>
#include <Tempest/VulkanApi>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "game/resources.h"
#include "game/world/modelmesh.h"
#include "game/world/level.h"
#include "game/world/objects.h"
#include "physics/characters.h"
#include "physics/vehicle.h"
#include "game/world/characters.h"
#include "formats/fog.h"

using namespace Tempest;

// the moment of the water animation: RCSHOT_TIME, else the drive time, else 0
static float waterTime() {
  if(const char* t = std::getenv("RCSHOT_TIME"))
    return float(std::atof(t));
  if(const char* d = std::getenv("RCSHOT_DRIVE"))
    return float(std::atof(d));
  return 0;
  }

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

    // objects first: they settle on the track before the car drives in
    rc::PhysicsWorld         world(level.collision());
    rc::ObjectLibrary        library([&](const std::string& p){ return res.readFile(p); });
    std::unique_ptr<Objects> objects;
    if(const char* o = std::getenv("RCSHOT_OBJECTS"); o==nullptr || std::string(o)!="0")
      objects = std::make_unique<Objects>(device,res,library,level.scene(),world);

    const char* carName = std::getenv("RCSHOT_CAR");
    std::unique_ptr<ModelMesh> car;
    rc::Mat4                 carPose;
    rc::SbScene              carSb;
    std::vector<rc::Mat4>    parts;
    if(carName==nullptr || std::string(carName)!="none") {
      carSb   = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      car     = std::make_unique<ModelMesh>(device,res,carSb,carName ? carName : "Car1");
      carPose = level.carStart(car->bboxMin().y);
      if(const char* start = std::getenv("RCSHOT_START")) {
        // "x z yaw": somewhere else on the track, standing on the ground
        float x = 0, z = 0, yaw = 0;
        std::sscanf(start,"%f %f %f",&x,&z,&yaw);
        carPose = rc::Mat4::translate({x,0,z})*rc::Mat4::rotate(1,yaw);
        if(auto g = level.groundHeight({x,1000.f,z}))
          carPose.m[1][3] = *g - car->bboxMin().y;
        }
      if(const char* drive = std::getenv("RCSHOT_DRIVE")) {
        const char*      steer = std::getenv("RCSHOT_STEER");
        const char*      jump  = std::getenv("RCSHOT_JUMP");
        bool             jumped = false;
        const float      time  = float(std::atof(drive));
        const int        carNo = std::atoi(car->name().c_str()+3);
        rc::Vehicle      veh(world,rc::carGeometry(carSb,car->name()),rc::CarParams::load(res.pack(),carNo),carPose);
        for(float t=0; t<time; t+=1.f/60.f) {
          rc::Vehicle::Input in;
          in.throttle = 1;
          in.boost    = std::getenv("RCSHOT_BOOST")!=nullptr;
          in.steer    = (steer!=nullptr && t>time*0.5f) ? float(std::atof(steer)) : 0.f;
          if(jump!=nullptr && !jumped && t>=float(std::atof(jump)))
            in.jump = jumped = true;
          veh.update(in,1.f/60.f);
          if(veh.hit()!=rc::Vehicle::Hit::None)
            Log::i(veh.hit()==rc::Vehicle::Hit::Great ? "great hit" : "hit"," at ",t," s, ",veh.boostTank()," turbo");
          world.step(1.f/60.f);
          }
        carPose = veh.pose();
        for(auto& p:car->parts()) {
          rc::Mat4 m = rc::Mat4::translate(p.pivot);
          for(size_t i=0; i<veh.geometry().wheels.size(); ++i)
            if(veh.geometry().wheels[i].name==p.name)
              m = veh.wheelPose(i);
          parts.push_back(m);
          }
        const rc::Vec3 at = carPose.translation();
        Log::i("drove ",time," s: ",veh.speed()*3.6f," km/h, ",veh.wheelsOnGround()," wheels on the ground, at ",
               at.x," ",at.y," ",at.z,", on ",rc::surfaceName(veh.surface()),", ",int(veh.submerged()*100.f),
               "% under water");
        }
      // chase view from behind, level with the horizon
      const rc::Vec3 f = carPose.vector({0,0,1});
      cam.chase(rc::Mat4::translate(carPose.translation())*rc::Mat4::rotate(1,std::atan2(f.x,f.z)*57.29578f),
                ChaseDistance,ChaseHeight);
      level.keepAboveWater(cam,waterTime());
      }
    // people and animals
    std::unique_ptr<rc::Characters> chars;
    if(const char* o = std::getenv("RCSHOT_OBJECTS"); o==nullptr || std::string(o)!="0")
      chars = std::make_unique<rc::Characters>(world,library,level.scene(),level.name(),
                                               [&res](const std::string& f){ return res.pack().read(f); });
    if(const char* who = std::getenv("RCSHOT_ACTOR"); who!=nullptr && chars) {
      auto env = [](const char* n, float def) { const char* v = std::getenv(n); return v ? float(std::atof(v)) : def; };
      rc::Characters::Actor* a = nullptr;
      for(auto* x:chars->actors())
        if(x->name==who)
          a = x;
      if(a==nullptr)
        throw std::runtime_error(std::string("no character ")+who);
      if(carSb.models.empty())
        carSb = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      const auto  geom = rc::carGeometry(carSb,"Car1");
      rc::Vehicle veh(world,geom,rc::CarParams::load(res.pack(),1),rc::Mat4::translate({0,-500,0}));
      const float time = env("RCSHOT_ACTOR_TIME",1.f);
      for(float t=0; t<time; t+=1.f/60.f) {
        // the car stays parked in front of the character, wherever it goes
        const rc::Vec3 at = a->world.translation();
        const rc::Vec3 f  = a->world.vector({0,0,1});
        const float    d  = env("RCSHOT_ACTOR_CAR",2.f);
        rc::Vec3       p  = at+rc::Vec3{f.x,0,f.z}.normalized()*d;
        if(auto g = level.groundHeight({p.x,at.y+2.f,p.z}))
          p.y = *g-geom.min.y;
        veh.hold(rc::Mat4::translate(p));
        world.step(1.f/60.f);
        chars->update(1.f/60.f,&veh);
        }
      carPose = veh.pose();
      const rc::Vec3 at   = a->world.translation();
      const rc::Vec3 f    = a->world.vector({0,0,1});
      const float    side = env("RCSHOT_ACTOR_SIDE",30.f)*3.14159265f/180.f;
      const float    yaw  = std::atan2(f.x,f.z)+side;
      const float    dist = env("RCSHOT_ACTOR_DIST",2.5f);
      cam.position = at+rc::Vec3{std::sin(yaw),0,std::cos(yaw)}*dist+rc::Vec3{0,dist*0.35f,0};
      cam.lookAt(at+rc::Vec3{0,a->kind==rc::Characters::Kind::Person || a->kind==rc::Characters::Kind::Guard ? 0.8f : 0.1f,0});
      Log::i(a->name," at ",at.x," ",at.y," ",at.z,", ",a->seq>=0 ? a->skeleton->sequences()[size_t(a->seq)].name : "-",
             " ",a->time," s, ",a->visible ? "visible" : "hidden");
      }
    else if(chars) {
      // a moment with the car where it is, so the characters near it show up
      if(carSb.models.empty())
        carSb = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      rc::Vehicle veh(world,rc::carGeometry(carSb,"Car1"),rc::CarParams::load(res.pack(),1),carPose);
      for(int i=0; i<2; ++i) {
        veh.hold(carPose);
        chars->update(1.f/60.f,&veh);
        }
      }
    CharacterView charView(device);

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
    if(const char* f = std::getenv("RCSHOT_FOG"); f==nullptr || std::string(f)!="0") {
      // the fog as it is once its fade-in is over (no fade here)
      const auto fog = rc::FogParams::load(res.pack(),argv[2]);
      push.cameraPos[3] = fog.density;
      push.fogColor     = fog.color & 0xffffffu;
      }
    push.sunDir[0] = 0.4f; push.sunDir[1] = 0.8f; push.sunDir[2] = 0.3f;
    push.sunDir[3] = 1.0f;
    if(car) {
      const rc::Vec3 at = carPose.translation();
      push.carPos[0] = at.x; push.carPos[1] = at.y; push.carPos[2] = at.z;
      }
    push.time = waterTime();

    CommandBuffer cmd;
    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{color,Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},{depth,1.f,Tempest::Preserve});
      const bool sky = std::getenv("RCSHOT_NOSKY")==nullptr;
      level.mesh().draw(enc,push,LevelMesh::Pass::Opaque,sky);
      if(objects)
        objects->draw(enc,viewProj,push);
      if(car)
        car->draw(enc,viewProj,push,carPose,parts.empty() ? nullptr : &parts);
      if(chars)
        charView.draw(enc,0,res,*chars,push);
      level.mesh().draw(enc,push,LevelMesh::Pass::Translucent,sky);
      if(std::getenv("RCSHOT_COLL")!=nullptr)
        level.collisionView().draw(enc,push);
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
