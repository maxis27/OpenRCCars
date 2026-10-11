#include "level.h"

#include <Tempest/Log>

#include "game/resources.h"

Level::Level(Tempest::Device& device, Resources& res, const std::string& name)
  :levelName(name) {
  sb  = rc::SbScene::load(res.readFile(std::filesystem::path("RCCarsDB")/(name+".sb")));
  gpu = std::make_unique<LevelMesh>(device,res,sb);
  Tempest::Log::i("level \"",name,"\": ",sb.meshes.size()," meshes, ",sb.textures.size()," textures, ",
                  sb.markers.size()," markers");
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
