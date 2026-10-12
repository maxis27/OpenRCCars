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

  // the gates face along the route: from the previous checkpoint towards the next one
  const size_t n = points.size();
  for(size_t i=0; i<n; ++i) {
    const Vec3 prev = points[(i>0) ? i-1 : (n>2 ? n-2 : 0)].position;
    const Vec3 next = points[(i+1<n) ? i+1 : (n>1 ? 1 : 0)].position;
    Vec3 d = flat(next-prev);
    points[i].dir = (d.length()>1e-3f) ? d*(1.f/d.length()) : Vec3{0,0,1};
    }
  // the lower edge: the lowest ground across the gate, under whatever stands on it. The markers are often placed
  // on top of a prop (beach_3: cp_1_2 and cp_3_1 on a rock a metre high, the road beside it lower) or on a pier
  // with sand under it (beach_1: cp_2_2), and a car on the road beside the prop or on the sand passes the gate too
  for(auto& p:points) {
    const Vec3 side = {-p.dir.z,0,p.dir.x};
    p.bottom = p.position.y-1.f;
    for(float s:{-0.5f,-0.25f,0.f,0.25f,0.5f}) {
      Vec3 from = p.position+side*(s*p.width)+Vec3{0,0.5f,0};
      for(int layer=0; layer<8; ++layer) {
        auto g = coll.groundHeight(from,from.y-(p.position.y-MaxDepth));
        if(!g)
          break;
        p.bottom = std::min(p.bottom,*g-0.5f);
        from.y   = *g-0.05f;
        }
      }
    }
  }

void Checkpoints::reset(const Mat4& start) {
  startPose = start;
  target    = 1;
  lapCount  = 0;
  passed    = false;
  hasLast   = false;
  }

bool Checkpoints::passes(size_t i, const Vec3& from, const Vec3& to) const {
  const Point& p    = points[i];
  const Vec3   side = {-p.dir.z,0,p.dir.x};
  auto inGate = [&](const Vec3& at) {
    const Vec3 d = at-p.position;
    return std::abs(Vec3::dot(d,side))<=p.width*0.5f && at.y>=p.bottom && d.y<=p.height+0.5f;
    };
  // close enough in front of or behind the gate
  const float f1 = Vec3::dot(flat(to-p.position),p.dir);
  if(std::abs(f1)<=p.expand && inGate(to))
    return true;
  // drove through it between two frames; not across a jump back to a checkpoint (R, drowning)
  const float f0 = Vec3::dot(flat(from-p.position),p.dir);
  if(f0<0 && f1>=0 && (to-from).length()<5.f) {
    const float t = f0/(f0-f1);
    return inGate(from+(to-from)*t);
    }
  return false;
  }

bool Checkpoints::update(const Vec3& car) {
  const Vec3 from = hasLast ? last : car;
  last    = car;
  hasLast = true;
  if(target>=points.size() || !passes(target,from,car))
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
