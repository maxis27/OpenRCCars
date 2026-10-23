#include "checkpoints.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace rc;

static Vec3 flat(Vec3 v) {
  v.y = 0;
  return v;
  }

Checkpoints::Checkpoints(const SbScene& scene, const CollisionMesh& track) : coll(track) {
  // the folder's properties stand in for those a marker lacks
  float defWidth = DefaultWidth, defHeight = DefaultHeight, defExpand = DefaultExpand;
  if(auto f = scene.folder("Checkpoints")) {
    defWidth  = f->number("width", defWidth);
    defHeight = f->number("height",defHeight);
    defExpand = f->number("expand",defExpand);
    }
  auto add = [&](const SbMarker& m, bool main) {
    Point p;
    p.name     = m.name;
    p.position = m.position();
    p.width    = m.number("width", defWidth);
    p.height   = m.number("height",defHeight);
    p.expand   = m.number("expand",defExpand);
    p.main     = main;
    points.push_back(p);
    };
  for(int cp=1; cp<=8; ++cp) {
    const std::string name = "cp_"+std::to_string(cp);
    auto m = scene.marker(name);
    if(m==nullptr)
      break;
    add(*m,true);
    // cp_<n>_1, cp_<n>_2 .. by number, up to the first one missing (country_1 has cp_4_3 and cp_4_5: the game
    // leaves cp_4_5 out); the file order does not always follow the numbers (country_1: cp_5_4 before cp_5_2)
    for(int k=1; ; ++k) {
      auto s = scene.marker(name+"_"+std::to_string(k));
      if(s==nullptr)
        break;
      add(*s,false);
      }
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
  // the corridor (0x4e9980): the box of each piece takes the sizes of the point it starts from
  for(size_t i=0; i+1<n; ++i) {
    const Point& a = points[i];
    Piece        c;
    const Vec3   d = points[i+1].position-a.position;
    c.origin = a.position;
    c.length = d.length();
    c.dir    = c.length>1e-6f ? d*(1.f/c.length) : Vec3{0,0,1};
    c.up     = Vec3{0,1,0}-c.dir*c.dir.y;
    c.up     = c.up.length()>1e-6f ? c.up*(1.f/c.up.length()) : Vec3{1,0,0};
    c.side   = Vec3::cross(c.dir,c.up);
    c.expand = a.expand;
    c.width  = a.width;
    c.height = a.height;
    c.along  = corridorLength;
    corridorLength += c.length;
    pieces.push_back(c);
    }
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
  in        = In::Nowhere;
  piece     = 0;
  frac      = 0;
  before    = true;
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
  locate(car);
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
  return poseAt(lastMain,carBottom);
  }

Mat4 Checkpoints::poseAt(size_t i, float carBottom) const {
  const Vec3 at   = points[i].position;
  Vec3       dir  = points[std::min(i+1,points.size()-1)].position-at;
  dir.y = 0;
  const float yaw = (std::abs(dir.x)+std::abs(dir.z)>1e-3f) ? std::atan2(dir.x,dir.z)*180.f/3.14159265f : 0.f;
  Mat4 pose = Mat4::translate(at)*Mat4::rotate(1,yaw);
  if(auto g = coll.groundHeight(at+Vec3{0,1.f,0}))
    pose.m[1][3] = *g - carBottom;
  return pose;
  }

// RCCars.exe 0x4ebb40: inside the box of the piece, and how far along it (0..1)
bool Checkpoints::inside(const Piece& p, const Vec3& car, float& f) const {
  const Vec3  r = car-p.origin;
  const float a = Vec3::dot(r,p.dir);
  if(std::abs(Vec3::dot(r,p.up))>p.height || std::abs(Vec3::dot(r,p.side))>p.width)
    return false;
  if(a<-p.expand || a>=p.length+p.expand)
    return false;
  f = p.length>0 ? std::clamp(a,0.f,p.length)/p.length : 0.f;
  return true;
  }

size_t Checkpoints::mainBefore(size_t i) const {
  size_t r = 0;
  for(size_t m:mains)
    if(m<i)
      r = m;
  return r;
  }

float Checkpoints::alongTo(size_t i) const {
  return i<pieces.size() ? pieces[i].along : corridorLength;
  }

// RCCars.exe 0x4eba30: the pieces from the last main checkpoint to the next one first, the last of them first;
// then all the others from cp_1 on. Found nowhere, the car stays on the piece it was found on last (0x4eb670),
// how far along it as it is now seen from it.
void Checkpoints::locate(const Vec3& car) {
  if(pieces.empty())
    return;
  const size_t from = mainBefore(target);
  float        f    = 0;
  bool         found = false;
  for(size_t i=target; i-->from && !found; )
    if(inside(pieces[i],car,f)) {
      in = In::Ahead; piece = i; frac = f; found = true;
      }
  for(size_t i=0; i<pieces.size() && !found; ++i)
    if((i<from || i>=target) && inside(pieces[i],car,f)) {
      in = In::Elsewhere; piece = i; frac = f; found = true;
      }
  if(!found && in!=In::Nowhere) {
    const Piece& p = pieces[piece];
    frac = p.length>0 ? std::clamp(Vec3::dot(car-p.origin,p.dir)/p.length,0.f,1.f) : 0.f;
    }
  if(in==In::Elsewhere) {
    // 0x4eb990: ahead of the car or behind it, whichever is nearer round the lap
    float d = alongTo(target)-(pieces[piece].along+pieces[piece].length*frac);
    if(std::abs(d)>0.5f*corridorLength)
      d -= d>=0 ? corridorLength : -corridorLength;
    before = d>=0;
    }
  }

Vec3 Checkpoints::wayOn(const Vec3& car) const {
  const size_t n = pieces.size();
  if(n>0 && target<points.size()) {
    if(in==In::Ahead) {
      // a piece on once nine tenths of this one are behind; past the last one before the checkpoint: straight
      const size_t i = piece+(frac>0.9f ? 1 : 0);
      if(i<target)
        return pieces[i].dir;
      }
    else if(in==In::Elsewhere) {
      if(before)
        return pieces[(piece+(frac>0.9f ? 1 : 0))%n].dir;
      // past the next main checkpoint: back along the corridor, on the piece before within the first tenth of
      // this one; right after the checkpoint itself straight to it
      if(frac>=0.1f)
        return pieces[piece].dir*-1.f;
      if(piece!=target%n)
        return pieces[(piece+n-1)%n].dir*-1.f;
      }
    }
  return target<points.size() ? points[target].position-car : Vec3{0,0,1};
  }

float Checkpoints::wayLeft(const Vec3& car) const {
  if(pieces.empty() || target>=points.size())
    return 1.f;
  const float to   = alongTo(target);
  const float span = to-alongTo(mainBefore(target));
  float       left = 0;
  if(in==In::Ahead)
    left = to-(pieces[piece].along+pieces[piece].length*frac);
  else if(in==In::Elsewhere) {
    float d = to-(pieces[piece].along+pieces[piece].length*frac);
    if(std::abs(d)>0.5f*corridorLength)
      d -= d>=0 ? corridorLength : -corridorLength;
    left = std::abs(d);
    }
  else {
    // not in the corridor yet (0x4eb670): a rough guess from the distance to the checkpoint
    const float at = (to-(points[target].position-car).length())*0.75f;
    left = (to-at)*1.7f;
    }
  if(left<0)
    return 0;
  if(left>span)
    return 1;
  return std::abs(span)<1e-6f ? 0.f : left/span;
  }
