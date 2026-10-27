#include "settings.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

using namespace rc;

namespace {

std::string_view trim(std::string_view s) {
  while(!s.empty() && (s.front()==' ' || s.front()=='\t' || s.front()=='\r'))
    s.remove_prefix(1);
  while(!s.empty() && (s.back()==' ' || s.back()=='\t' || s.back()=='\r'))
    s.remove_suffix(1);
  return s;
  }

bool startsWith(std::string_view s, std::string_view prefix) {
  return s.substr(0,prefix.size())==prefix;
  }

// text between the first and the last double quote: names like "DON"T TOUCH BELOW" contain quotes
std::string quoted(std::string_view s) {
  const size_t a = s.find('"'), b = s.rfind('"');
  if(a==std::string_view::npos || b<=a)
    return std::string(trim(s));
  return std::string(s.substr(a+1,b-a-1));
  }

template<class F>
void forEachLine(std::string_view text, F&& fn) {
  while(!text.empty()) {
    const size_t n = text.find('\n');
    fn(trim(text.substr(0,n)));
    if(n==std::string_view::npos)
      break;
    text.remove_prefix(n+1);
    }
  }

std::vector<float> numbers(std::string_view s) {
  std::vector<float> out;
  std::string        str(s);
  for(auto& c:str)
    if(c==',')
      c = ' ';
  std::istringstream in(str);
  float v;
  while(in>>v)
    out.push_back(v);
  return out;
  }

}

SliderFile SliderFile::parse(std::string_view text) {
  SliderFile ret;
  Slider*    cur = nullptr;
  forEachLine(text,[&](std::string_view l) {
    if(startsWith(l,"Key ")) {
      cur = &ret.keys[quoted(l.substr(4))];
      }
    else if(cur!=nullptr && startsWith(l,"Values ")) {
      auto v = numbers(l.substr(7));
      if(v.size()>=3) {
        cur->min   = int32_t(v[0]);
        cur->max   = int32_t(v[1]);
        cur->value = int32_t(v[2]);
        }
      }
    else if(cur!=nullptr && startsWith(l,"Mask ")) {
      cur->isFloat = trim(l.substr(5))=="float";
      }
    else if(cur!=nullptr && startsWith(l,"FValues ")) {
      auto v = numbers(l.substr(8));
      if(v.size()>=2) {
        cur->fmin = v[0];
        cur->fmax = v[1];
        }
      }
    });
  return ret;
  }

SliderFile SliderFile::parseCrs(const std::vector<uint8_t>& data) {
  SliderFile ret;
  Slider*    cur = nullptr;
  auto u16 = [&](size_t at) { return uint32_t(data[at] | data[at+1]<<8); };
  auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v,data.data()+at,4); return v; };
  // a 10-byte header, then the records; the end of each record is an offset from the start of the file
  for(size_t at=10; at+6<=data.size();) {
    const uint32_t tag = u16(at), end = u32(at+2);
    if(tag==0x0220) {     // a key: its records follow
      at += 6;
      continue;
      }
    if(end<=at || end>data.size())
      break;
    const size_t body = at+6;
    if(tag==0x4021) {
      std::string name;
      for(size_t i=body; i<end && data[i]!=0; ++i)
        name.push_back(char(data[i]^0x25));
      cur = &ret.keys[name];
      }
    else if(cur!=nullptr && end-body>=4 && (tag==0x3023 || tag==0x3024 || tag==0x3025)) {
      const int32_t v = int32_t(u32(body));
      (tag==0x3023 ? cur->min : tag==0x3024 ? cur->max : cur->value) = v;
      }
    at = end;
    }
  return ret;
  }

const SliderFile::Slider* SliderFile::find(std::string_view key) const {
  auto it = keys.find(key);
  return it==keys.end() ? nullptr : &it->second;
  }

int32_t SliderFile::raw(std::string_view key, int32_t def) const {
  auto s = find(key);
  return s ? s->value : def;
  }

float SliderFile::real(std::string_view key, float def) const {
  auto s = find(key);
  if(s==nullptr)
    return def;
  if(!s->isFloat || s->max==s->min)
    return float(s->value);
  const float k = float(s->value-s->min)/float(s->max-s->min);
  return s->fmin + (s->fmax-s->fmin)*k;
  }

namespace {

// where the records of RCCars.crs are (ConfigFile::parse, withString)
struct CrsLayout {
  struct Value   { size_t at = 0, end = 0; };            // [u16 type] .. its trailing u32
  struct Section { size_t countAt = 0, end = 0; };       // its [u16 children], the end of its last child
  std::map<std::string,Value,std::less<>>   values;      // "section/key"
  std::map<std::string,Section,std::less<>> sections;
  size_t rootCountAt = 0, rootEnd = 0;
  bool   ok = false;
  };

CrsLayout crsLayout(const std::vector<uint8_t>& data) {
  CrsLayout ret;
  auto u16 = [&](size_t o) { return o+2<=data.size() ? uint32_t(data[o] | data[o+1]<<8) : 0u; };
  auto u32 = [&](size_t o) { uint32_t v = 0; if(o+4<=data.size()) std::memcpy(&v,data.data()+o,4); return v; };
  auto str = [&](size_t o, size_t n) {
    std::string s;
    for(size_t i=o; i<o+n && i<data.size() && data[i]!=0; ++i)
      s.push_back(char(data[i]));
    return s;
    };
  // the root: "CSRC", a version, the application's name and the file's description, then the sections
  if(data.size()<12 || std::memcmp(data.data(),"CSRC",4)!=0)
    return ret;
  size_t at = 12+u32(8);
  at += 4+u32(at);
  if(at+8>data.size())
    return ret;
  ret.rootCountAt = at;
  const uint32_t count = u16(at);
  at += 8;

  struct Open { std::string path; uint32_t left; };
  std::vector<Open> stack;   // the open sections and how many children are left
  stack.push_back({"",count});
  while(!stack.empty()) {
    if(stack.back().left==0) {
      if(stack.size()>1)
        ret.sections[stack.back().path].end = at;
      stack.pop_back();
      continue;
      }
    if(at+12>data.size() || std::memcmp(data.data()+at,"CSRC",4)!=0)
      return ret;
    --stack.back().left;
    const uint32_t len  = u32(at+4);
    const std::string name = str(at+8,len);
    at += 8+len;
    const std::string path = stack.back().path.empty() ? name : stack.back().path+"/"+name;
    const uint32_t type = u16(at), size = u16(at+2), cap = u16(at+4);
    if(size==0 && cap==0) {
      // a section
      ret.sections[path].countAt = at;
      at += 8;
      stack.push_back({path,type});
      continue;
      }
    ret.values[path] = {at,at+8+size+4};
    at += 8+size+4;
    }
  ret.rootEnd = at;
  ret.ok      = at<=data.size();
  return ret;
  }

// a value record: [u16 2][u16 length with the zero][u16 capacity][u16 0], the string, [u32 0]
std::vector<uint8_t> crsString(std::string_view value, uint16_t capacity) {
  const uint16_t n = uint16_t(std::min<size_t>(value.size()+1,0xffff));
  std::vector<uint8_t> r = {2,0,uint8_t(n&0xff),uint8_t(n>>8),uint8_t(capacity&0xff),uint8_t(capacity>>8),0,0};
  r.insert(r.end(),value.begin(),value.begin()+(n-1));
  r.insert(r.end(),5,0);
  return r;
  }

std::vector<uint8_t> crsName(std::string_view name) {
  std::vector<uint8_t> r = {'C','S','R','C'};
  const uint32_t n = uint32_t(name.size()+1);
  for(int i=0; i<4; ++i)
    r.push_back(uint8_t(n>>(8*i)));
  r.insert(r.end(),name.begin(),name.end());
  r.push_back(0);
  return r;
  }

}

ConfigFile ConfigFile::parse(const std::vector<uint8_t>& data) {
  ConfigFile ret;
  const auto lay = crsLayout(data);
  for(auto& [path,v]:lay.values) {
    const uint32_t type = uint32_t(data[v.at] | data[v.at+1]<<8);
    const uint32_t size = uint32_t(data[v.at+2] | data[v.at+3]<<8);
    if(type==1 && size==4) {
      int32_t i = 0;
      std::memcpy(&i,data.data()+v.at+8,4);
      ret.ints[path] = i;
      }
    if(type==2) {
      std::string s;
      for(size_t i=v.at+8; i<v.at+8+size && data[i]!=0; ++i)
        s.push_back(char(data[i]));
      ret.strings[path] = s;
      }
    }
  return ret;
  }

int32_t ConfigFile::integer(std::string_view section, std::string_view key, int32_t def) const {
  auto it = ints.find(std::string(section)+"/"+std::string(key));
  return it==ints.end() ? def : it->second;
  }

std::string ConfigFile::string(std::string_view section, std::string_view key, std::string_view def) const {
  auto it = strings.find(std::string(section)+"/"+std::string(key));
  return it==strings.end() ? std::string(def) : it->second;
  }

std::vector<uint8_t> ConfigFile::withString(const std::vector<uint8_t>& data, std::string_view section,
                                            std::string_view key, std::string_view value, uint16_t capacity) {
  const auto lay = crsLayout(data);
  if(!lay.ok)
    return data;
  std::vector<uint8_t> ret = data;
  auto bump = [&ret](size_t at) {
    const uint16_t n = uint16_t((ret[at] | ret[at+1]<<8)+1);
    ret[at]   = uint8_t(n&0xff);
    ret[at+1] = uint8_t(n>>8);
    };
  const std::string path = std::string(section)+"/"+std::string(key);
  if(auto v = lay.values.find(path); v!=lay.values.end()) {
    const uint16_t cap = uint16_t(data[v->second.at+4] | data[v->second.at+5]<<8);
    const auto rec = crsString(value,std::max(cap,uint16_t(value.size()+1)));
    ret.erase(ret.begin()+std::ptrdiff_t(v->second.at),ret.begin()+std::ptrdiff_t(v->second.end));
    ret.insert(ret.begin()+std::ptrdiff_t(v->second.at),rec.begin(),rec.end());
    return ret;
    }
  auto rec = crsName(key);
  const auto val = crsString(value,std::max(capacity,uint16_t(value.size()+1)));
  rec.insert(rec.end(),val.begin(),val.end());
  if(auto s = lay.sections.find(section); s!=lay.sections.end()) {
    ret.insert(ret.begin()+std::ptrdiff_t(s->second.end),rec.begin(),rec.end());
    bump(s->second.countAt);
    return ret;
    }
  auto sec = crsName(section);
  sec.insert(sec.end(),{1,0,0,0,0,0,0,0});
  sec.insert(sec.end(),rec.begin(),rec.end());
  ret.insert(ret.begin()+std::ptrdiff_t(lay.rootEnd),sec.begin(),sec.end());
  bump(lay.rootCountAt);
  return ret;
  }

std::vector<uint8_t> ConfigFile::withInteger(const std::vector<uint8_t>& data, std::string_view section,
                                             std::string_view key, int32_t value) {
  const auto lay = crsLayout(data);
  if(!lay.ok)
    return data;
  // a value record: [u16 1][u16 4][u16 0][u16 0], the integer, [u32 0]
  std::vector<uint8_t> rec = {1,0,4,0,0,0,0,0};
  for(int i=0; i<4; ++i)
    rec.push_back(uint8_t(uint32_t(value)>>(8*i)));
  rec.insert(rec.end(),4,0);
  std::vector<uint8_t> ret = data;
  auto bump = [&ret](size_t at) {
    const uint16_t n = uint16_t((ret[at] | ret[at+1]<<8)+1);
    ret[at]   = uint8_t(n&0xff);
    ret[at+1] = uint8_t(n>>8);
    };
  const std::string path = std::string(section)+"/"+std::string(key);
  if(auto v = lay.values.find(path); v!=lay.values.end()) {
    ret.erase(ret.begin()+std::ptrdiff_t(v->second.at),ret.begin()+std::ptrdiff_t(v->second.end));
    ret.insert(ret.begin()+std::ptrdiff_t(v->second.at),rec.begin(),rec.end());
    return ret;
    }
  auto named = crsName(key);
  named.insert(named.end(),rec.begin(),rec.end());
  if(auto s = lay.sections.find(section); s!=lay.sections.end()) {
    ret.insert(ret.begin()+std::ptrdiff_t(s->second.end),named.begin(),named.end());
    bump(s->second.countAt);
    return ret;
    }
  auto sec = crsName(section);
  sec.insert(sec.end(),{1,0,0,0,0,0,0,0});
  sec.insert(sec.end(),named.begin(),named.end());
  ret.insert(ret.begin()+std::ptrdiff_t(lay.rootEnd),sec.begin(),sec.end());
  bump(lay.rootCountAt);
  return ret;
  }

KeySections KeySections::parse(std::string_view text) {
  KeySections ret;
  std::map<std::string,std::vector<float>,std::less<>>* cur = nullptr;
  forEachLine(text,[&](std::string_view l) {
    if(startsWith(l,"Section ")) {
      cur = &ret.sections[quoted(l)];
      return;
      }
    if(l=="End") {
      cur = nullptr;
      return;
      }
    const size_t eq = l.find('=');
    if(cur==nullptr || eq==std::string_view::npos)
      return;
    // "OverkillUpgrades (float[4]) = ..." or "OverkillUpgrades_SPL_OY(float[4]) = ..."
    std::string_view name = l.substr(0,std::min(l.find('('),eq));
    (*cur)[std::string(trim(name))] = numbers(l.substr(eq+1));
    });
  return ret;
  }

const std::vector<float>& KeySections::get(std::string_view section, std::string_view key) const {
  static const std::vector<float> none;
  auto s = sections.find(section);
  if(s==sections.end())
    return none;
  auto k = s->second.find(key);
  return k==s->second.end() ? none : k->second;
  }

Spline Spline::parse(const std::vector<uint8_t>& data) {
  // control points: [5b][80][u32 end = pos+18][f32 0][f32 x][f32 y]
  std::vector<Point> pts;
  for(size_t i=0; i+18<=data.size(); ++i) {
    if(data[i]!=0x5b || data[i+1]!=0x80)
      continue;
    uint32_t end;
    std::memcpy(&end,data.data()+i+2,4);
    if(end!=i+18)
      continue;
    Point p;
    std::memcpy(&p.x,data.data()+i+10,4);
    std::memcpy(&p.y,data.data()+i+14,4);
    pts.push_back(p);
    i = end-1;
    }
  std::stable_sort(pts.begin(),pts.end(),[](const Point& a, const Point& b){ return a.x<b.x; });
  return Spline(std::move(pts));
  }

float Spline::at(float x) const {
  if(pts.empty())
    return 0;
  if(x<=pts.front().x)
    return pts.front().y;
  if(x>=pts.back().x)
    return pts.back().y;
  for(size_t i=1; i<pts.size(); ++i) {
    if(x>pts[i].x)
      continue;
    const Point& a = pts[i-1];
    const Point& b = pts[i];
    const float  k = (b.x>a.x) ? (x-a.x)/(b.x-a.x) : 0.f;
    return a.y + (b.y-a.y)*k;
    }
  return pts.back().y;
  }

CurveSpline CurveSpline::parse(std::string_view text) {
  // "3", then 3 x 4 points; a trailing "0 0" closes the file
  std::vector<float> v;
  forEachLine(text,[&](std::string_view l) {
    for(float f:numbers(l))
      v.push_back(f);
    });
  CurveSpline ret;
  if(v.empty())
    return ret;
  const size_t segments = size_t(std::max(v[0],0.f));
  if(v.size()<1+segments*8)
    return ret;
  auto slope = [](float x0, float y0, float x1, float y1) {
    return std::abs(x1-x0)<1e-6f ? 0.f : (y1-y0)/(x1-x0);
    };
  for(size_t i=0; i<segments; ++i) {
    const float* p = &v[1+i*8];  // start, handle, handle, end
    if(ret.keys.empty())
      ret.keys.push_back({p[0],p[1],0,0});
    ret.keys.back().out = slope(p[0],p[1],p[2],p[3]);
    ret.keys.push_back({p[6],p[7],slope(p[4],p[5],p[6],p[7]),0});
    }
  return ret;
  }

float CurveSpline::at(float x) const {
  if(keys.empty())
    return 0;
  if(x<=keys.front().x)
    return keys.front().y;
  if(x>=keys.back().x)
    return keys.back().y;
  size_t i = 1;
  while(i+1<keys.size() && x>keys[i].x)
    ++i;
  const Key&  a  = keys[i-1];
  const Key&  b  = keys[i];
  const float dx = b.x-a.x;
  if(std::abs(dx)<1e-6f)
    return (a.y+b.y)*0.5f;
  // RCCars.exe 0x40f830: Hermite with the slopes in units of y per x
  const float t = (x-a.x)/dx, t2 = t*t, t3 = t2*t;
  const float h = 2*t3-3*t2;
  return (h+1)*a.y - h*b.y + dx*a.out*(t3-2*t2+t) + dx*b.in*(t3-t2);
  }
