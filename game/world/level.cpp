#include "level.h"

#include <Tempest/Log>

#include <cmath>

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
  std::optional<float> best;
  for(auto& m:sb.meshes) {
    if(m.positions.empty() || LevelMesh::isHiddenMesh(m.name))
      continue;
    for(auto& f:m.faces) {
      if(f.pos.size()<3 || f.layer(rc::LayerTag::Base)==nullptr)
        continue;
      auto at = [&](size_t i) {
        return f.pos[i]<m.positions.size() ? m.world.point(m.positions[f.pos[i]]) : rc::Vec3{};
        };
      const rc::Vec3 a = at(0);
      for(size_t i=1; i+1<f.pos.size(); ++i) {
        const rc::Vec3 b = at(i), c = at(i+1);
        // barycentric coordinates in the XZ plane
        const float d = (b.z-c.z)*(a.x-c.x) + (c.x-b.x)*(a.z-c.z);
        if(std::abs(d)<1e-8f)
          continue;
        const float u = ((b.z-c.z)*(from.x-c.x) + (c.x-b.x)*(from.z-c.z))/d;
        const float v = ((c.z-a.z)*(from.x-c.x) + (a.x-c.x)*(from.z-c.z))/d;
        if(u<0 || v<0 || u+v>1)
          continue;
        const float y = a.y*u + b.y*v + c.y*(1-u-v);
        if(y<=from.y && (!best || y>*best))
          best = y;
        }
      }
    }
  return best;
  }
