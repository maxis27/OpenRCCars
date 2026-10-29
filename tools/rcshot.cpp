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
//   env RCSHOT_TIME=<seconds> is the moment of the water animation and of the moving scenery (beach_4's submarine
//   and radar; default: the drive time, else 0).
//   env RCSHOT_FOG=0 draws without the track's fog (Settings\fog_<track>.ini), RCSHOT_NOSUN=1 without the sun.
//   env RCSHOT_OBJECTS=0 hides the objects placed on the track (props, characters, road cars), RCSHOT_FEATSHADE=0
//   draws them without the brightness of the lightmap under them (rc::fx::FeatureShade).
//   env RCSHOT_ACTOR=<instance name> looks at a person or animal from RCSHOT_ACTOR_DIST m (default 2.5) after
//   RCSHOT_ACTOR_TIME seconds (default 1) with the car parked RCSHOT_ACTOR_CAR m in front of it (default 2), and
//   RCSHOT_ACTOR_SIDE degrees round it (default 30).
//   env RCSHOT_FX=<seconds> runs the track's effects (splashes, breaking waves, the guards' gunfire at the parked car)
//   that long, seen from the final camera, and draws them; the water's time goes on from RCSHOT_TIME.
//   env RCSHOT_NOSHADOW=1 leaves out the shadow under the car, RCSHOT_SHADE=<k> lights it with that brightness
//   instead of the lightmap's under it
//   env RCSHOT_TRAFFIC=<seconds> lets the road traffic run that long after the start of the race and, with
//   RCSHOT_TRAFFIC_CAR=<n>, looks at the n-th car on the road from RCSHOT_ACTOR_DIST m (default 8),
//   RCSHOT_ACTOR_SIDE degrees round it (default 30) and RCSHOT_ACTOR_RISE times that distance up (default 0.35).
//   env RCSHOT_UPGRADE=<0..3> fits the car with that level of every upgrade (as the game's U key), RCSHOT_LOD=1|2
//   draws its Car<n>LOD<n> model instead.

#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/Pixmap>
#include <Tempest/VulkanApi>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <algorithm>
#include <memory>
#include <string>

#include "game/effects.h"
#include "game/chasecamera.h"
#include "game/resources.h"
#include "game/world/modelmesh.h"
#include "game/world/level.h"
#include "game/world/objects.h"
#include "physics/characters.h"
#include "physics/vehicle.h"
#include "game/world/characters.h"
#include "game/world/traffic.h"
#include "game/world/carshadow.h"
#include "game/world/sunview.h"
#include "game/world/carmodel.h"
#include "game/world/antennaview.h"
#include "physics/antenna.h"
#include "fx/carlight.h"
#include "physics/traffic.h"
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
    ChaseCamera chase(ChaseCamera::Settings::load([&res](const std::string& f){ return res.pack().read(f); }));
    ChaseCamera::Car chaseCar;

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
    std::unique_ptr<AntennaView> antennaView;
    std::optional<rc::Antenna>   antenna;     // the full model's springy antenna (RCSHOT_ANTENNA=0: as modelled)
    size_t                       antennaCar = 0;
    std::function<void(const rc::Mat4&,float)> antennaStep;
    if(carName==nullptr || std::string(carName)!="none") {
      carSb   = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      ModelMesh::CarLook look;
      std::string        model = carName ? carName : "Car1";
      antennaCar  = size_t(std::clamp(std::atoi(model.c_str()+3)-1,0,2));
      antennaView = std::make_unique<AntennaView>(device,res,carSb);
      const char* ant = std::getenv("RCSHOT_ANTENNA");
      look.antenna = !antennaView->has(antennaCar) || std::getenv("RCSHOT_LOD")!=nullptr || (ant!=nullptr && std::string(ant)=="0");
      if(const char* u = std::getenv("RCSHOT_UPGRADE"))
        look.boost = look.resonator = look.tires = std::atoi(u);
      if(const char* l = std::getenv("RCSHOT_LOD")) {
        model    += std::string("LOD")+l;
        look.lod  = true;
        }
      car     = std::make_unique<ModelMesh>(device,res,carSb,model,look);
      carPose = level.carStart(car->bboxMin().y);
      const auto antennaSettings = rc::Antenna::Settings::load(res.pack(),antennaCar);
      if(!look.antenna) antennaStep = [&,antennaSettings](const rc::Mat4& pose, float dt) {
        if(!antenna || (antenna->mountPoint()-pose.point(antennaView->mount(antennaCar))).length()>1.f) {
          antenna.emplace(antennaSettings,pose,antennaView->mount(antennaCar),antennaView->rest(antennaCar));
          return;
          }
        std::optional<float> ceiling;
        if(auto hit = level.collision().raycast(antenna->mountPoint(),{0,1,0},antenna->reach()))
          ceiling = hit->distance;
        antenna->step(dt,pose,ceiling);
        };
      if(const char* start = std::getenv("RCSHOT_START")) {
        // "x z yaw [y]": somewhere else on the track, standing on the ground (the highest, or the first under y:
        // in a tunnel)
        float x = 0, z = 0, yaw = 0, y = 1000.f;
        std::sscanf(start,"%f %f %f %f",&x,&z,&yaw,&y);
        carPose = rc::Mat4::translate({x,0,z})*rc::Mat4::rotate(1,yaw);
        if(auto g = level.groundHeight({x,y,z}))
          carPose.m[1][3] = *g - car->bboxMin().y;
        }
      if(const char* drive = std::getenv("RCSHOT_DRIVE")) {
        const char*      steer = std::getenv("RCSHOT_STEER");
        const char*      jump  = std::getenv("RCSHOT_JUMP");
        bool             jumped = false;
        const float      time  = float(std::atof(drive));
        const int        carNo = std::atoi(car->name().c_str()+3);
        rc::Vehicle      veh(world,rc::carGeometry(carSb,car->name()),rc::CarParams::load(res.pack(),carNo),carPose);
        chaseCar.pose   = carPose;
        chaseCar.centre = carPose.translation();
        chase.reset(chaseCar);
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
          antennaStep(veh.pose(),1.f/60.f);
          chaseCar.pose     = veh.pose();
          chaseCar.centre   = veh.centre();
          chaseCar.velocity = veh.velocity();
          chaseCar.spin     = veh.localSpin().length();
          chaseCar.boost    = in.boost;
          chase.update(chaseCar,1.f/60.f);
          if(t+1.f/60.f<time)
            chase.apply(chaseCar,&level.collision(),1.f/60.f,cam);
          }
        carPose = veh.pose();
        const rc::CarRig::Pose rig = veh.rigPose();
        for(auto& p:car->parts()) {
          rc::Mat4 m = rig.part(p.name,p.pivot);
          for(size_t i=0; i<veh.geometry().wheels.size() && !p.name.empty(); ++i)
            if(veh.geometry().wheels[i].name==p.name)
              m = rig.wheel(i);
          parts.push_back(m);
          }
        const rc::Vec3 at = carPose.translation();
        Log::i("drove ",time," s: ",veh.speed()*3.6f," km/h, ",veh.wheelsOnGround()," wheels on the ground, at ",
               at.x," ",at.y," ",at.z,", on ",rc::surfaceName(veh.surface()),", ",int(veh.submerged()*100.f),
               "% under water");
        }
      // the game's chase camera (game/chasecamera.h), settled behind a parked car
      if(std::getenv("RCSHOT_DRIVE")==nullptr) {
        chaseCar.pose   = carPose;
        chaseCar.centre = carPose.translation();
        chase.reset(chaseCar);
        }
      chase.apply(chaseCar,&level.collision(),1.f/60.f,cam);
      level.keepAboveWater(cam,waterTime());
      // RCSHOT_VIEW=<degrees>,<metres>[,<height>]: close to the car instead, from that side (0 behind, 90 its left),
      // the camera <height> times <metres> up (0.25)
      if(const char* v = std::getenv("RCSHOT_VIEW")) {
        const rc::Vec3 f = carPose.point({0,0,1})-carPose.translation();
        const float yaw  = float(std::atof(v))/57.29578f+std::atan2(f.x,f.z);
        const char* d    = std::strchr(v,',');
        const float dist = d ? float(std::atof(d+1)) : 0.8f;
        const char* u    = d ? std::strchr(d+1,',') : nullptr;  // optional: the height, times `dist`
        const float up   = u ? float(std::atof(u+1)) : 0.25f;
        const rc::Vec3 at = carPose.point({0,0.1f,0});
        cam.position = at+rc::Vec3{-std::sin(yaw),0,-std::cos(yaw)}*dist+rc::Vec3{0,dist*up,0};
        cam.lookAt(at);
        }
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

    // road traffic
    std::unique_ptr<rc::Traffic> traffic;
    if(chars)
      traffic = std::make_unique<rc::Traffic>(world,library,level.scene(),level.name(),
                                              [&res](const std::string& f){ return res.pack().read(f); });
    if(const char* run = std::getenv("RCSHOT_TRAFFIC"); run!=nullptr && traffic && traffic->size()>0) {
      auto env = [](const char* n, float def) { const char* v = std::getenv(n); return v ? float(std::atof(v)) : def; };
      std::vector<rc::Characters::Car> none;
      const float time = float(std::atof(run));
      for(float t=0; t<time; t+=1.f/60.f) {
        world.step(1.f/60.f);
        traffic->update(1.f/60.f,true,nullptr,cam.position,none);
        }
      const rc::Traffic::Car* look = nullptr;
      int n = 0;
      for(auto* c:traffic->cars()) {
        if(!c->onRoad)
          continue;
        const rc::Vec3 at = c->world.translation();
        Log::i("traffic: ",c->name," at ",at.x," ",at.y," ",at.z);
        if(const char* k = std::getenv("RCSHOT_TRAFFIC_CAR"); k!=nullptr && n++==std::atoi(k))
          look = c;
        }
      Log::i("traffic: ",traffic->departures()," sent in ",time," s");
      if(look!=nullptr) {
        const rc::Vec3 at   = look->world.translation();
        const rc::Vec3 f    = look->pose.vector({0,0,1});
        const float    side = env("RCSHOT_ACTOR_SIDE",30.f)*3.14159265f/180.f;
        const float    yaw  = std::atan2(f.x,f.z)+side;
        const float    dist = env("RCSHOT_ACTOR_DIST",8.f);
        cam.position = at+rc::Vec3{std::sin(yaw),0,std::cos(yaw)}*dist+rc::Vec3{0,dist*env("RCSHOT_ACTOR_RISE",0.35f),0};
        cam.lookAt(at+rc::Vec3{0,1.f,0});
        // once more, so the car near the camera is posed (wheels, lights) and drawn
        world.step(1.f/60.f);
        traffic->update(1.f/60.f,true,nullptr,cam.position,none);
        }
      }
    TrafficView trafficView(device,res);

    if(argc>10) {
      cam.position = {float(std::atof(argv[6])),float(std::atof(argv[7])),float(std::atof(argv[8]))};
      cam.yaw      = float(std::atof(argv[9]));
      cam.pitch    = float(std::atof(argv[10]));
      }
    Log::i("camera: ",cam.position.x," ",cam.position.y," ",cam.position.z," yaw=",cam.yaw," pitch=",cam.pitch);

    // the track's effects, run for a while in front of the camera
    std::unique_ptr<Effects> fx;
    float                    fxTime = 0;
    if(const char* f = std::getenv("RCSHOT_FX")) {
      fx = std::make_unique<Effects>(device,res);
      fx->setTrack(level.scene(),level.mesh().mainSea());
      if(carSb.models.empty())
        carSb = rc::SbScene::load(res.readFile("RCCarsDB/Car.sb"));
      std::unique_ptr<rc::Vehicle> veh;
      if(car)
        veh = std::make_unique<rc::Vehicle>(world,rc::carGeometry(carSb,"Car1"),rc::CarParams::load(res.pack(),1),carPose);
      const rc::fx::View     view = {cam.position,cam.viewProj(float(w)/float(h))};
      const float time = float(std::atof(f));
      int bursts = 0, hits = 0;
      for(float t=0; t<time; t+=1.f/60.f) {
        const float water = waterTime()+t;
        if(veh) {
          veh->hold(carPose);
          world.step(1.f/60.f);
          }
        if(chars && veh)
          chars->update(1.f/60.f,veh.get());
        // the guards shoot at the parked car (target 0)
        std::vector<rc::fx::Gunfire::Order>  orders;
        std::vector<rc::fx::Gunfire::Target> targets;
        if(veh) {
          for(auto& s:chars ? chars->shooting() : std::vector<rc::Characters::Shot>{})
            orders.push_back({s.gun,0});
          rc::fx::Gunfire::Target tg;
          tg.pose   = veh->pose();
          tg.boxMin = veh->geometry().bodyMin;
          tg.boxMax = veh->geometry().bodyMax;
          tg.chance = rc::fx::Gunfire::chanceFor(true,0);
          targets.push_back(tg);
          }
        bursts += int(orders.size());
        std::vector<rc::fx::Footprints::Walker> walkers;
        if(chars)
          for(auto* a:chars->actors())
            walkers.push_back({a->world,a->soles});
        fx->tickWorld(1.f/60.f,view,water,[&level,water](const rc::Vec3& p){ return level.waterHeight(p,10.f,water); },
                      orders,targets,level.collision(),walkers);
        fx->gunSounds();
        hits += int(fx->gunHits().size());
        }
      fxTime = time;
      const rc::Vec3 at = carPose.translation();
      Log::i("effects: ",time," s, ",bursts," bursts at the car (",at.x," ",at.y," ",at.z,"), ",hits," hits, ",
             fx->footprintCount()," footprints, ",fx->smokeCount()," puffs of smoke");
      if(chars)
        for(auto* a:chars->actors())
          if(a->kind==rc::Characters::Kind::Person) {
            const rc::Vec3 p = a->world.translation();
            Log::i("  ",a->name," now at ",p.x," ",p.y," ",p.z,a->soles ? ", leaving footprints" : "");
            }
      }

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
    push.time = waterTime()+fxTime;

    CarShadow                   carShadow(res);
    if(car) {
      // the car's light as it is under it, at once (RCSHOT_SHADE=<k> sets the brightness instead)
      const auto        light = rc::fx::CarLight::load(res.pack());
      rc::fx::LightmapProbe probe(level.scene(),level.collision(),[&res](const std::string& f){ return res.pack().read(f); });
      rc::fx::CarShade  shade;
      shade.update(light,0,true,probe.below(carPose.translation()+rc::Vec3{0,0.01f,0},1.5f));
      float k = shade.value();
      if(const char* s = std::getenv("RCSHOT_SHADE"))
        k = float(std::atof(s));
      Log::i("car brightness ",k);
      lightCar(*car,std::getenv("RCSHOT_LOD")==nullptr,light,k);
      }
    // the props', people's, guards' and road cars' brightness as it is where they stand, at once
    // (rc::fx::FeatureShade; RCSHOT_FEATSHADE=0 leaves them in the light of the other models)
    std::vector<float> actorLight, roadLight;
    if(const char* fs = std::getenv("RCSHOT_FEATSHADE"); fs==nullptr || std::string(fs)!="0") {
      const auto        light = rc::fx::CarLight::load(res.pack());
      rc::fx::LightmapProbe probe(level.scene(),level.collision(),[&res](const std::string& f){ return res.pack().read(f); });
      auto under = [&probe](const rc::Vec3& at) {
        return [&probe,at]{ return probe.ground(at+rc::Vec3{0,0.1f,0},2.f); };
        };
      if(objects)
        objects->shade(-1.f,light.featCurve,probe);
      if(chars)
        for(auto* a:chars->actors()) {
          float k = -1;
          if(a->kind==rc::Characters::Kind::Person || a->kind==rc::Characters::Kind::Guard) {
            rc::fx::FeatureShade s;
            s.update(light.featCurve,rc::fx::FeatureShade::Guard,-1.f,under(a->world.translation()));
            k = s.value();
            Log::i("brightness of ",a->name,": ",k);
            }
          actorLight.push_back(k);
          }
      if(traffic)
        for(auto* c:traffic->cars()) {
          rc::fx::FeatureShade s;
          s.update(light.featCurve,rc::fx::FeatureShade::Guard,-1.f,under(c->world.translation()));
          roadLight.push_back(s.value());
          }
      }
    // the sun and its flare as they are after half a second
    rc::fx::LensFlare sunFlare;
    if(auto sun = level.scene().marker("SUN_AF"); sun!=nullptr && std::getenv("RCSHOT_NOSUN")==nullptr)
      sunFlare = rc::fx::LensFlare(rc::fx::LensFlare::Settings::load([&res](const std::string& f){ return res.pack().read(f); }),
                                   sun->position());
    for(int i=0; i<30 && sunFlare.has(); ++i)
      sunFlare.update(1.f/60.f,cam.position,cam.forward(),viewProj,int(w),int(h),[&level](const rc::Vec3& a, const rc::Vec3& b) {
        return level.collision().raycast(a,b-a,0.98f).has_value();
        });
    SunView                     sunView(device,res);
    SpotView                    carSpots(device);
    std::vector<SpotView::Spot> carSpotList;
    CommandBuffer cmd;
    {
      auto enc = cmd.startEncoding(device);
      enc.setFramebuffer({{color,Vec4(0.62f,0.72f,0.82f,1.f),Tempest::Preserve}},{depth,1.f,Tempest::Preserve});
      const bool sky = std::getenv("RCSHOT_NOSKY")==nullptr;
      const auto poses = level.animated().poses(push.time);
      level.mesh().draw(enc,push,LevelMesh::Pass::Opaque,viewProj,poses,sky);
      if(objects)
        objects->draw(enc,viewProj,push);
      if(car) {
        if(std::getenv("RCSHOT_NOSHADOW")==nullptr) {
          carSpotList.assign(1,carShadow.spot(size_t(std::max(0,std::atoi(car->name().c_str()+3)-1)),carPose));
          carSpots.draw(enc,0,carSpotList,viewProj,push,level.collision());
          }
        car->draw(enc,viewProj,push,carPose,parts.empty() ? nullptr : &parts);
        if(antennaStep) {
          // parked: a second to settle where it stands
          if(!antenna)
            for(int i=0; i<=60; ++i)
              antennaStep(carPose,1.f/60.f);
          antennaView->clear();
          antennaView->add(antennaCar,antenna->joints(),car->lighting());
          antennaView->draw(enc,0,push);
          }
        }
      if(chars)
        charView.draw(enc,0,res,*chars,push,actorLight);
      if(fx)
        fx->drawTracks(enc,0,viewProj,cam.position);
      if(traffic)
        trafficView.draw(enc,0,*traffic,viewProj,push,&level.collision(),roadLight);
      level.mesh().draw(enc,push,LevelMesh::Pass::Translucent,viewProj,poses,sky);
      if(fx)
        fx->draw(enc,0,int(w),int(h),viewProj,cam.position);
      sunView.draw(enc,0,int(w),int(h),sunFlare);
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
