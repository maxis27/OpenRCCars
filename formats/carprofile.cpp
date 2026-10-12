#include "carprofile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "chunk.h"

using namespace rc;

static Range rootChildren(const std::vector<uint8_t>& data, Chunk& root) {
  if(data.size()<8)
    throw FormatError("car profile: too short");
  Range top(data.data(),0,data.size());
  root = *top.begin();
  if(root.id!=0x50)
    throw FormatError("car profile: no root element");
  return root.children(2);
  }

CarProfile CarProfile::parse(const std::vector<uint8_t>& data) {
  Chunk root;
  CarProfile ret;
  size_t stride = 136;
  const Chunk* frames = nullptr;
  Chunk f;
  float full = 0, cycle = 0;
  for(auto& c:rootChildren(data,root)) {
    if(c.id==0x60) {
      for(auto& h:c.children(0)) {
        if(h.id=='g' && h.size()>=2)
          stride = size_t(h.data()[0]) | size_t(h.data()[1])<<8;
        // "Time path FULL: 68.185939", "Time path CYCLE: 65.022942"
        const std::string_view s = h.str();
        const size_t           colon = s.find(": ");
        if(colon!=std::string_view::npos && s.substr(0,10)=="Time path ") {
          const float v = std::strtof(std::string(s.substr(colon+2)).c_str(),nullptr);
          if(s.substr(10,4)=="FULL")
            full = v;
          if(s.substr(10,5)=="CYCLE")
            cycle = v;
          }
        }
      }
    if(c.id==0x64) {
      f      = c;
      frames = &f;
      }
    }
  if(frames==nullptr)
    throw FormatError("car profile: no frames");
  if(stride<136)
    throw FormatError("car profile: frames too small");
  const size_t n = frames->size()/stride;
  ret.list.resize(n);
  for(size_t i=0; i<n; ++i) {
    const size_t at = i*stride;
    auto&        fr = ret.list[i];
    fr.time      = frames->f32(at);
    fr.position  = {frames->f32(at+4),frames->f32(at+8),frames->f32(at+12)};
    for(int k=0; k<4; ++k)
      fr.rot[k] = frames->f32(at+16+4*size_t(k));
    fr.velocity  = {frames->f32(at+32),frames->f32(at+36),frames->f32(at+40)};
    fr.spin      = {frames->f32(at+44),frames->f32(at+48),frames->f32(at+52)};
    for(int k=0; k<4; ++k)
      fr.suspension[k] = frames->f32(at+56+4*size_t(k));
    fr.keys      = frames->u32(at+132);
    }
  // time must not go back
  for(size_t i=1; i<n; ++i)
    ret.list[i].time = std::max(ret.list[i].time,ret.list[i-1].time);
  if(full>0 && cycle>0 && cycle<=full)
    ret.loopStart = std::clamp(full-cycle,0.f,ret.duration());
  return ret;
  }

size_t CarProfile::find(float time, float& t) const {
  t = 0;
  if(list.empty() || time<=list.front().time)
    return 0;
  if(time>=list.back().time)
    return list.size()-1;
  auto it = std::upper_bound(list.begin(),list.end(),time,[](float v, const Frame& f){ return v<f.time; });
  const size_t i  = size_t(it-list.begin())-1;
  const float  dt = list[i+1].time-list[i].time;
  t = dt>0 ? (time-list[i].time)/dt : 0.f;
  return i;
  }

Mat4 CarProfile::pose(float time) const {
  if(list.empty())
    return Mat4();
  float        t = 0;
  const size_t i = find(time,t);
  const Frame& a = list[i];
  const Frame& b = list[std::min(i+1,list.size()-1)];
  // normalised linear interpolation of the rotation, along the shorter way
  float q[4], d = 0;
  for(int k=0; k<4; ++k)
    d += a.rot[k]*b.rot[k];
  const float s = d<0 ? -1.f : 1.f;
  float len = 0;
  for(int k=0; k<4; ++k) {
    q[k] = a.rot[k]+(b.rot[k]*s-a.rot[k])*t;
    len += q[k]*q[k];
    }
  len = len>0 ? 1.f/std::sqrt(len) : 1.f;
  const float w = q[0]*len, x = q[1]*len, y = q[2]*len, z = q[3]*len;
  Mat4 m;
  m.m[0][0] = 1-2*(y*y+z*z); m.m[0][1] = 2*(x*y-w*z);   m.m[0][2] = 2*(x*z+w*y);
  m.m[1][0] = 2*(x*y+w*z);   m.m[1][1] = 1-2*(x*x+z*z); m.m[1][2] = 2*(y*z-w*x);
  m.m[2][0] = 2*(x*z-w*y);   m.m[2][1] = 2*(y*z+w*x);   m.m[2][2] = 1-2*(x*x+y*y);
  const Vec3 p = a.position+(b.position-a.position)*t;
  m.m[0][3] = p.x; m.m[1][3] = p.y; m.m[2][3] = p.z;
  return m;
  }

Vec3 CarProfile::velocity(float time) const {
  if(list.empty() || time>list.back().time)
    return {};
  float        t = 0;
  const size_t i = find(time,t);
  const Frame& a = list[i];
  const Frame& b = list[std::min(i+1,list.size()-1)];
  return a.velocity+(b.velocity-a.velocity)*t;
  }

namespace {

std::string_view trim(std::string_view s) {
  while(!s.empty() && std::isspace(uint8_t(s.front())))
    s.remove_prefix(1);
  while(!s.empty() && std::isspace(uint8_t(s.back())))
    s.remove_suffix(1);
  return s;
  }

// "AI12Path" -> prefix "AI", number 12, rest "Path"
bool splitNumbered(std::string_view key, std::string_view prefix, int32_t& n, std::string_view& rest) {
  if(key.size()<=prefix.size() || key.substr(0,prefix.size())!=prefix)
    return false;
  size_t i = prefix.size();
  n = 0;
  while(i<key.size() && std::isdigit(uint8_t(key[i])))
    n = n*10+(key[i++]-'0');
  rest = key.substr(i);
  return i>prefix.size();
  }

}

AiLayout AiLayout::parse(std::string_view text) {
  AiLayout ret;
  std::map<std::string,float,std::less<>> defines;
  // "EASY + NORMAL + HARD", "0.85", "BUGGY"
  auto number = [&defines](std::string_view v) {
    float sum = 0;
    while(!v.empty()) {
      const size_t     plus = v.find('+');
      std::string_view term = trim(v.substr(0,plus));
      if(auto it = defines.find(term); it!=defines.end())
        sum += it->second;
      else if(!term.empty())
        sum += std::strtof(std::string(term).c_str(),nullptr);
      v = plus==std::string_view::npos ? std::string_view() : v.substr(plus+1);
      }
    return sum;
    };

  int32_t driver = 0, race = 0;
  while(!text.empty()) {
    const size_t     eol  = text.find('\n');
    std::string_view line = text.substr(0,eol);
    text = eol==std::string_view::npos ? std::string_view() : text.substr(eol+1);
    if(auto c = line.find("//"); c!=std::string_view::npos)
      line = line.substr(0,c);
    line = trim(line);
    if(line.empty())
      continue;

    if(line.substr(0,8)=="#define ") {
      line = trim(line.substr(8));
      const size_t sp = line.find_first_of(" \t");
      if(sp!=std::string_view::npos)
        defines[std::string(line.substr(0,sp))] = number(line.substr(sp+1));
      continue;
      }
    if(line.substr(0,8)=="Section ") {
      const size_t q0 = line.find('"'), q1 = line.rfind('"');
      std::string_view name = (q0!=std::string_view::npos && q1>q0) ? line.substr(q0+1,q1-q0-1) : std::string_view();
      std::string_view rest;
      driver = race = 0;
      int32_t n = 0;
      if(splitNumbered(name,"AIPlayer",n,rest) && rest.empty())
        driver = n;
      else if(splitNumbered(name,"AIRace",n,rest) && rest.empty())
        race = n;
      continue;
      }
    if(line.substr(0,3)=="End") {
      driver = race = 0;
      continue;
      }

    // Key (type) = value
    const size_t eq = line.find('=');
    if(eq==std::string_view::npos)
      continue;
    std::string_view key = trim(line.substr(0,eq));
    if(auto p = key.find('('); p!=std::string_view::npos)
      key = trim(key.substr(0,p));
    std::string_view value = trim(line.substr(eq+1));
    std::string      str;
    if(value.size()>=2 && value.front()=='"') {
      const size_t q = value.find('"',1);
      str = std::string(value.substr(1,q==std::string_view::npos ? std::string_view::npos : q-1));
      }

    if(driver>0) {
      auto& d = ret.drivers[driver];
      if(key=="Car")  d.car  = int32_t(number(value));
      if(key=="Skin") d.skin = int32_t(number(value));
      if(key=="Face") d.face = str;
      }
    if(race>0) {
      auto&            r = ret.races[race];
      int32_t          n = 0;
      std::string_view rest;
      if(key=="CoeffCommonOpponents")
        r.coeff = number(value);
      else if(key=="PlayerPath")
        r.playerPath = str;
      else if(splitNumbered(key,"AI",n,rest) && n>0 && n<=16) {
        if(r.opponents.size()<size_t(n))
          r.opponents.resize(size_t(n));
        auto& o = r.opponents[size_t(n-1)];
        if(rest=="Car")   o.car    = int32_t(number(value));
        if(rest=="Ref")   o.driver = int32_t(number(value));
        if(rest=="Races") o.races  = int32_t(number(value));
        if(rest=="Path")  o.path   = str;
        if(rest=="Boost") o.boost  = int32_t(number(value));
        if(rest=="Reson") o.reson  = int32_t(number(value));
        if(rest=="Tires") o.tires  = int32_t(number(value));
        }
      }
    }
  return ret;
  }

int32_t rc::aiRaceNumber(std::string_view track) {
  static const char* const Order[] = {
    "beach_1",   // Surf
    "country_3", // Fishers
    "beach_3",   // Fort
    "urban_2",   // AAD
    "country_2", // Mines
    "beach_2",   // Camping
    "urban_1",   // Avax (AWACS)
    "country_1", // Rancho
    "country_4", // Silo
    "beach_4",   // War path (Nuclear)
    };
  for(size_t i=0; i<std::size(Order); ++i)
    if(track==Order[i])
      return int32_t(i+1);
  return 0;
  }
