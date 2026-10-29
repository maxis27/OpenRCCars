#include "level.h"

#include <Tempest/Log>

#include <cmath>

#include "game/resources.h"
#include "formats/water.h"

Level::Level(Tempest::Device& device, Resources& res, const std::string& name, int32_t renderQuality)
  :levelName(name) {
  sb  = rc::SbScene::load(res.readFile(std::filesystem::path("RCCarsDB")/(name+".sb")));
  coll    = rc::CollisionMesh(sb);
  coll.setSeas(rc::Seas::load(sb,res.pack(),name));
  anim    = rc::fx::AnimatedObjects(sb,rc::fx::AnimatedObjects::Settings::load([&](const std::string& f) {
    return res.pack().find(f)!=nullptr ? res.pack().read(f) : std::vector<uint8_t>();
    }));
  gpu     = std::make_unique<LevelMesh>(device,res,sb,&coll,name,&anim.groups(),renderQuality);
  collGpu = std::make_unique<CollisionView>(device,coll,sb);
  Tempest::Log::i("level \"",name,"\": ",sb.meshes.size()," meshes, ",sb.textures.size()," textures, ",
                  sb.markers.size()," markers, ",coll.triangles().size()," collision triangles");
  if(coll.empty())
    Tempest::Log::e("level \"",name,"\": no COLL node, the car has nothing to stand on");
  }

Camera Level::startCamera() const {
  Camera cam;
  auto cp1  = sb.marker("cp_1");
  auto next = sb.marker("cp_1_1");
  if(next==nullptr)
    next = sb.marker("cp_2");
  if(cp1!=nullptr && next!=nullptr) {
    const rc::Vec3 a   = cp1->position();
    const rc::Vec3 b   = next->position();
    rc::Vec3       dir = b-a;
    dir.y = 0;
    dir   = dir.normalized();
    cam.position = a - dir*4.f + rc::Vec3{0,1.5f,0};
    cam.lookAt(b + rc::Vec3{0,0.3f,0});
    return cam;
    }
  // fallback: overview of the bounding box
  const rc::Vec3 mn = gpu->bboxMin(), mx = gpu->bboxMax();
  const rc::Vec3 c  = (mn+mx)*0.5f;
  cam.position = {c.x, mx.y+10.f, mn.z};
  cam.lookAt(c);
  return cam;
  }

rc::Mat4 Level::carStart(float carBottom) const {
  rc::Mat4 pose;
  if(auto p = sb.instance("Player")) {
    pose = p->world;
    }
  else if(auto cp1 = sb.marker("cp_1")) {
    // no start marker: on cp_1, facing the next checkpoint like the start camera
    pose = rc::Mat4::translate(cp1->position())*rc::Mat4::rotate(1,startCamera().yaw);
    }
  const rc::Vec3 at = pose.translation();
  if(auto g = groundHeight(at+rc::Vec3{0,1.f,0}))
    pose.m[1][3] = *g - carBottom;
  return pose;
  }

std::optional<float> Level::groundHeight(const rc::Vec3& from) const {
  return coll.groundHeight(from);
  }

std::optional<float> Level::waterHeight(const rc::Vec3& at, float range, float time) const {
  uint32_t mesh = 0;
  auto     y    = coll.waterLevel(at,range,&mesh);
  if(!y)
    return std::nullopt;
  return *y + gpu->waveLift(mesh,{at.x,*y,at.z},time);
  }

void Level::keepAboveWater(Camera& camera, float time) const {
  constexpr float Probe = 0.07f;
  const rc::Vec3  at    = camera.position-rc::Vec3{0,Probe,0};
  // the original casts down from 10 m above, through 20 m
  if(auto w = waterHeight(at,10.f,time); w && at.y<*w)
    camera.position.y = *w+2.f*Probe;
  }
