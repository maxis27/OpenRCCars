#include "actorpath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "chunk.h"

using namespace rc;

template<class T>
static T read(const std::vector<uint8_t>& d, size_t at) {
  T v;
  std::memcpy(&v,d.data()+at,sizeof(T));
  return v;
  }

static Vec3 readVec(const std::vector<uint8_t>& d, size_t at) {
  return {read<float>(d,at),read<float>(d,at+4),read<float>(d,at+8)};
  }

static Vec3 lerp(const Vec3& a, const Vec3& b, float k) {
  return a+(b-a)*k;
  }

static float wrap(float t, float duration) {
  if(duration<=0)
    return 0;
  t = std::fmod(t,duration);
  return t<0 ? t+duration : t;
  }

PeoplePath PeoplePath::load(const std::vector<uint8_t>& file) {
  if(file.size()<16)
    throw FormatError("people path: too short");
  const uint32_t n = read<uint32_t>(file,0);
  if(size_t(n)*32+16>file.size())
    throw FormatError("people path: truncated");
  PeoplePath p;
  for(uint32_t i=0; i+1<n || (i==0 && n>0); i+=2) {
    const size_t at = 16+size_t(i)*32;
    Point pt;
    pt.time  = read<float>(file,at);
    pt.pos   = readVec(file,at+4);
    pt.dir   = readVec(file,at+16);
    pt.state = read<float>(file,at+28);
    p.points.push_back(pt);
    }
  return p;
  }

PeoplePath::Point PeoplePath::at(float time) const {
  if(points.empty())
    return {};
  time = wrap(time,duration());
  auto it = std::upper_bound(points.begin(),points.end(),time,[](float t, const Point& p){ return t<p.time; });
  if(it==points.begin())
    return points.front();
  if(it==points.end())
    return points.back();
  const Point& a = *(it-1);
  const Point& b = *it;
  const float  k = b.time>a.time ? (time-a.time)/(b.time-a.time) : 0.f;
  Point r;
  r.time  = time;
  r.pos   = lerp(a.pos,b.pos,k);
  r.dir   = lerp(a.dir,b.dir,k);
  r.state = a.state;
  return r;
  }

FlightPath FlightPath::load(const std::vector<uint8_t>& file) {
  if(file.size()<0x28 || read<uint16_t>(file,0)!=0x101)
    throw FormatError("gpf: not a path");
  const uint32_t keys   = read<uint32_t>(file,0x18);
  const uint32_t dim    = read<uint32_t>(file,0x1c);
  const uint32_t values = read<uint32_t>(file,0x20);
  const size_t   table  = 0x28+size_t(values)*4;
  if(dim<9 || table+size_t(keys)*8>file.size())
    throw FormatError("gpf: truncated");
  FlightPath p;
  for(uint32_t i=0; i<keys; ++i) {
    const float    t   = read<float>(file,table+size_t(i)*8);
    const uint32_t ofs = read<uint32_t>(file,table+size_t(i)*8+4);
    if(size_t(ofs)+9>values)
      throw FormatError("gpf: bad key");
    const size_t at = 0x28+size_t(ofs)*4;
    p.keys.push_back({t,readVec(file,at),readVec(file,at+12),readVec(file,at+24)});
    }
  return p;
  }

FlightPath::Key FlightPath::at(float time) const {
  if(keys.empty())
    return {};
  time = wrap(time,duration());
  auto it = std::upper_bound(keys.begin(),keys.end(),time,[](float t, const Key& k){ return t<k.time; });
  if(it==keys.begin())
    return keys.front();
  if(it==keys.end())
    return keys.back();
  const Key&  a = *(it-1);
  const Key&  b = *it;
  const float k = b.time>a.time ? (time-a.time)/(b.time-a.time) : 0.f;
  return {time,lerp(a.pos,b.pos,k),lerp(a.forward,b.forward,k),lerp(a.up,b.up,k)};
  }

RoadPath RoadPath::load(const std::vector<uint8_t>& file) {
  if(file.size()<0x28 || read<uint16_t>(file,0)!=0x101)
    throw FormatError("gpf: not a path");
  const uint32_t keys   = read<uint32_t>(file,0x18);
  const uint32_t dim    = read<uint32_t>(file,0x1c);
  const uint32_t values = read<uint32_t>(file,0x20);
  const size_t   table  = 0x28+size_t(values)*4;
  if(dim<8 || table+size_t(keys)*8>file.size())
    throw FormatError("gpf: truncated");
  RoadPath p;
  for(uint32_t i=0; i<keys; ++i) {
    const float    t   = read<float>(file,table+size_t(i)*8);
    const uint32_t ofs = read<uint32_t>(file,table+size_t(i)*8+4);
    if(size_t(ofs)+8>values)
      throw FormatError("gpf: bad key");
    // the recordings of beach_2 and beach_3 start with keys going back in time (to -3.7 s); RCCars.exe's cursor
    // (0x472db0) walks over everything up to the time asked for, so these are never reached
    if(!p.keys.empty() && t<=p.keys.back().time)
      continue;
    const size_t at = 0x28+size_t(ofs)*4;
    p.keys.push_back({t,readVec(file,at),readVec(file,at+12),read<float>(file,at+24),read<float>(file,at+28)});
    }
  return p;
  }

RoadPath::Key RoadPath::at(float time) const {
  if(keys.empty())
    return {};
  auto it = std::upper_bound(keys.begin(),keys.end(),time,[](float t, const Key& k){ return t<k.time; });
  if(it==keys.begin())
    return keys.front();
  if(it==keys.end())
    return keys.back();
  const Key&  a = *(it-1);
  const Key&  b = *it;
  const float k = b.time>a.time ? (time-a.time)/(b.time-a.time) : 0.f;
  return {time,lerp(a.pos,b.pos,k),lerp(a.dir,b.dir,k),a.steer+(b.steer-a.steer)*k,a.tilt+(b.tilt-a.tilt)*k};
  }
