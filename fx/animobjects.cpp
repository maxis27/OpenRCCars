#include "animobjects.h"

#include <cmath>

#include "formats/scene.h"
#include "formats/settings.h"

using namespace rc;
using namespace rc::fx;

AnimatedObjects::Settings AnimatedObjects::Settings::load(const Reader& read) {
  Settings s;
  const auto d = read("Settings\\submarine.ini");
  const auto f = SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
  // 0x52c2f0: each key only if the file has it
  if(f.find("Y0"))
    s.y0 = float(f.raw("Y0"))*-0.01f;
  if(f.find("DY"))
    s.dy = float(f.raw("DY"))*0.01f;
  if(f.find("T"))
    s.t  = float(f.raw("T"))*0.01f;
  return s;
  }

AnimatedObjects::AnimatedObjects(const SbScene& scene, const Settings& settings)
  : set(settings) {
  const size_t n = scene.meshes.size();
  groupOf.assign(n,0);
  // the children of each node in the file's order; RCCars.exe links them the other way round (0x44ed80): its
  // first child is our last, its next sibling our previous one
  std::vector<std::vector<int32_t>> children(n);
  for(size_t i=0; i<n; ++i)
    if(int32_t p = scene.meshes[i].parent; p>=0 && size_t(p)<n)
      children[size_t(p)].push_back(int32_t(i));
  auto firstChild = [&](int32_t node) {
    return children[size_t(node)].empty() ? -1 : children[size_t(node)].back();
    };
  auto nextSibling = [&](int32_t node) {
    const int32_t p = scene.meshes[size_t(node)].parent;
    if(p<0)
      return -1;
    auto& c = children[size_t(p)];
    for(size_t i=1; i<c.size(); ++i)
      if(c[i]==node)
        return c[i-1];
    return -1;
    };
  auto parentWorld = [&](int32_t node) {
    const int32_t p = scene.meshes[size_t(node)].parent;
    return p>=0 ? scene.meshes[size_t(p)].world : Mat4();
    };
  auto add = [&](int32_t node, Item it) {
    if(node<0 || groupOf[size_t(node)]!=0)
      return;
    it.parent  = parentWorld(node);
    it.toLocal = scene.meshes[size_t(node)].world.inverse();
    items.push_back(it);
    groupOf[size_t(node)] = int32_t(items.size());
    };

  for(size_t i=0; i<n; ++i) {
    auto& m = scene.meshes[i];
    if(m.name.find("SUBMARINE")!=std::string::npos && items.size()<10)
      add(int32_t(i),Item{Item::Submarine});
    if(m.name.find("radar_ZRK")==std::string::npos)
      continue;
    const int32_t centre = firstChild(int32_t(i));
    if(centre<0 || scene.meshes[size_t(centre)].name.find("center")==std::string::npos ||
       scene.meshes[size_t(centre)].positions.empty())
      continue; // "bad radar format - no radar initializing"
    int32_t s = nextSibling(centre);
    if(s<0)
      continue;
    // the middle of the centre's box, in the radar node's space
    auto& c = scene.meshes[size_t(centre)];
    Vec3 lo = c.positions[0], hi = c.positions[0];
    for(auto& p:c.positions) {
      lo = {std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
      hi = {std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
      }
    Item it{Item::Radar};
    it.centre = c.local.point((lo+hi)*0.5f);
    if(const int32_t sc = firstChild(s); sc>=0) {
      add(sc,it);
      s = nextSibling(sc);
      }
    if(s>=0)
      add(s,it);
    }

  // the children go with their parents
  for(size_t i=0; i<n; ++i)
    for(int32_t p=scene.meshes[i].parent; p>=0 && groupOf[i]==0; p=scene.meshes[size_t(p)].parent)
      groupOf[i] = groupOf[size_t(p)];
  }

Mat4 AnimatedObjects::pose(int32_t g, float time) const {
  if(g<=0 || size_t(g)>items.size())
    return Mat4();
  const Item& it = items[size_t(g-1)];
  Mat4 local;
  if(it.kind==Item::Submarine) {
    local = Mat4::translate({0,set.y0+set.dy*std::cos(time*set.t),0});
    }
  else {
    // 0x52b860: rotation by 60 t degrees (0x40cc60), round the centre
    const float a = std::fmod(time*60.f,360.f);
    local = Mat4::translate(it.centre)*Mat4::rotate(1,a)*Mat4::translate(it.centre*-1.f);
    }
  return it.parent*local*it.toLocal;
  }

std::vector<Mat4> AnimatedObjects::poses(float time) const {
  std::vector<Mat4> r(items.size()+1);
  for(size_t g=1; g<r.size(); ++g)
    r[g] = pose(int32_t(g),time);
  return r;
  }
