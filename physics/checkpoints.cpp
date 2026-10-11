#include "checkpoints.h"

#include <cmath>

using namespace rc;

Checkpoints::Checkpoints(const SbScene& scene, const CollisionMesh& track) : coll(track) {
  for(int cp=1; ; ++cp) {
    const std::string name = "cp_"+std::to_string(cp);
    auto m = scene.marker(name);
    if(m==nullptr)
      break;
    points.push_back({name,m->position()});
    for(int sub=1; ; ++sub) {
      auto s = scene.marker(name+"_"+std::to_string(sub));
      if(s==nullptr)
        break;
      points.push_back({s->name,s->position()});
      }
    }
  if(!points.empty())
    points.push_back(points.front());
  }

void Checkpoints::reset(const Mat4& start) {
  startPose = start;
  target    = 1;
  lapCount  = 0;
  passed    = false;
  }

bool Checkpoints::update(const Vec3& car) {
  if(target>=points.size())
    return false;
  Vec3 d = points[target].position-car;
  d.y = 0;
  if(d.length()>=Radius)
    return false;
  passed = true;
  if(++target==points.size()) {
    // back on cp_1: the next lap
    ++lapCount;
    target = 1;
    }
  return true;
  }

Mat4 Checkpoints::respawn(float carBottom) const {
  if(!passed || points.size()<2)
    return startPose;
  const Vec3 at   = points[target-1].position;
  Vec3       dir  = points[target].position-at;
  dir.y = 0;
  const float yaw = (std::abs(dir.x)+std::abs(dir.z)>1e-3f) ? std::atan2(dir.x,dir.z)*180.f/3.14159265f : 0.f;
  Mat4 pose = Mat4::translate(at)*Mat4::rotate(1,yaw);
  if(auto g = coll.groundHeight(at+Vec3{0,1.f,0}))
    pose.m[1][3] = *g - carBottom;
  return pose;
  }
