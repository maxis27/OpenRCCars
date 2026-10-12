#include "checkpoints.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace rc;

static Vec3 flat(Vec3 v) {
  v.y = 0;
  return v;
  }

// sub-checkpoint number of `name` = "<prefix><number>", 0 if it is not one
static int subNumber(const std::string& name, const std::string& prefix) {
  if(name.size()<=prefix.size() || name.compare(0,prefix.size(),prefix)!=0)
    return 0;
  char* end = nullptr;
  const long n = std::strtol(name.c_str()+prefix.size(),&end,10);
  return (*end=='\0' && n>0) ? int(n) : 0;
  }

Checkpoints::Checkpoints(const SbScene& scene, const CollisionMesh& track) : coll(track) {
  auto add = [this](const SbMarker& m, bool main) {
    Point p;
    p.name     = m.name;
    p.position = m.position();
    p.width    = m.number("width", DefaultWidth);
    p.height   = m.number("height",DefaultHeight);
    p.expand   = m.number("expand",DefaultExpand);
    p.main     = main;
    points.push_back(p);
    };
  for(int cp=1; ; ++cp) {
    const std::string name = "cp_"+std::to_string(cp);
    auto m = scene.marker(name);
    if(m==nullptr)
      break;
    add(*m,true);
    // cp_<n>_1, cp_<n>_2 .. by number: the numbering has gaps (country_1: cp_4_3, cp_4_5) and the file order
    // does not always follow it (country_1: cp_5_4 before cp_5_2)
    std::vector<std::pair<int,const SbMarker*>> sub;
    for(auto& s:scene.markers)
      if(int n = subNumber(s.name,name+"_"))
        sub.emplace_back(n,&s);
    std::sort(sub.begin(),sub.end(),[](auto& a, auto& b){ return a.first<b.first; });
    for(auto& [n,s]:sub)
      add(*s,false);
    }
  if(points.empty())
    return;
  points.push_back(points.front());

  // the direction along the route through each point: from the previous one towards the next one
  const size_t n = points.size();
  for(size_t i=0; i<n; ++i) {
    const Vec3 prev = points[(i>0) ? i-1 : (n>2 ? n-2 : 0)].position;
    const Vec3 next = points[(i+1<n) ? i+1 : (n>1 ? 1 : 0)].position;
    Vec3 d = flat(next-prev);
    points[i].dir = (d.length()>1e-3f) ? d*(1.f/d.length()) : Vec3{0,0,1};
    }
  for(size_t i=0; i<n; ++i)
    if(points[i].main)
      mains.push_back(i);
  distances.resize(n,0.f);
  for(size_t i=1; i<n; ++i)
    distances[i] = distances[i-1]+flat(points[i].position-points[i-1].position).length();
  for(auto& p:points) {
    p.ground = p.position.y;
    if(auto g = coll.groundHeight(p.position+Vec3{0,0.5f,0},0.5f+MaxDepth))
      p.ground = *g;
    }
  }

size_t Checkpoints::nextMain(size_t i) const {
  for(size_t m:mains)
    if(m>i)
      return m;
  return points.size()-1;
  }

size_t Checkpoints::mainPassed() const {
  for(size_t k=0; k<mains.size(); ++k)
    if(mains[k]==target)
      return k-1;
  return 0;
  }

void Checkpoints::reset(const Mat4& start) {
  startPose = start;
  target    = points.empty() ? 1 : nextMain(0);
  way       = 1;
  lastMain  = 0;
  lapCount  = 0;
  passed    = false;
  hasLast   = false;
  }

bool Checkpoints::passes(size_t i, const Vec3& from, const Vec3& to, float radius) const {
  const Vec3 c = points[i].position;
  if((to-c).length()<=radius)
    return true;
  // or on the way between two frames, at the move's point closest to the marker; not across a jump back to a
  // checkpoint (R, drowning)
  const Vec3  d  = to-from;
  const float dd = Vec3::dot(d,d);
  if(dd<1e-8f || dd>=25.f)
    return false;
  const float t = std::clamp(Vec3::dot(c-from,d)/dd,0.f,1.f);
  return (from+d*t-c).length()<=radius;
  }

bool Checkpoints::update(const Vec3& car) {
  const Vec3 from = hasLast ? last : car;
  last    = car;
  hasLast = true;
  if(target>=points.size())
    return false;
  // the arrow moves on past the sub-checkpoint it shows once the car crosses its line close by, or just comes near
  // it (round the outside of a hairpin). One at a time and in order: a later one passed early is a shortcut
  // (country_1 crosses cp_1_5 right behind the start)
  if(way<target) {
    const Point& p  = points[way];
    const float  f0 = Vec3::dot(flat(from-p.position),p.dir), f1 = Vec3::dot(flat(car-p.position),p.dir);
    const float  d  = flat(car-p.position).length();
    if((f0<0 && f1>=0 && d<GuideRadius && (car-from).length()<5.f) ||
       d<p.width*0.5f+NearMargin)
      ++way;
    }
  // the next main checkpoint, whatever the arrow shows, as in RCCars.exe
  if(!passes(target,from,car))
    return false;
  passed   = true;
  lastMain = target;
  way      = target+1;
  if(target+1==points.size()) {
    // back on cp_1: the next lap
    ++lapCount;
    lastMain = 0;
    way      = 1;
    }
  target = nextMain(lastMain);
  return true;
  }

Mat4 Checkpoints::respawn(float carBottom) {
  way     = lastMain+1;
  hasLast = false;
  if(!passed || points.size()<2)
    return startPose;
  const Vec3 at   = points[lastMain].position;
  Vec3       dir  = points[lastMain+1].position-at;
  dir.y = 0;
  const float yaw = (std::abs(dir.x)+std::abs(dir.z)>1e-3f) ? std::atan2(dir.x,dir.z)*180.f/3.14159265f : 0.f;
  Mat4 pose = Mat4::translate(at)*Mat4::rotate(1,yaw);
  if(auto g = coll.groundHeight(at+Vec3{0,1.f,0}))
    pose.m[1][3] = *g - carBottom;
  return pose;
  }
