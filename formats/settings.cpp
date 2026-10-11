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
