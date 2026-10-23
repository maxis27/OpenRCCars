#include "lensflare.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "formats/settings.h"

using namespace rc;
using namespace rc::fx;

namespace {

constexpr float Eps       = 1e-6f;  // 0x554380
constexpr float ViewPlane = 0.1f;   // the camera's view plane distance (0x500760 -> 0x406160)

}

LensFlare::Settings LensFlare::Settings::load(const Reader& read) {
  Settings s;
  std::vector<uint8_t> d;
  try {
    d = read("Settings\\LENS_FLARE.ini");
    }
  catch(...) {
    return s;
    }
  const auto f = SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
  auto val = [&f](const char* key, float def, float div) {
    return f.find(key) ? float(f.raw(key))/div : def;
    };
  s.timeShootRay = val("TimeShootRay",s.timeShootRay,100);
  s.camOffset    = val("CamOffset",s.camOffset,100);
  s.fadeTime     = val("FadeTime",s.fadeTime,100);
  // the first two lie behind the point the line starts from (0x479b70 negates them)
  for(int i=0; i<4; ++i) {
    const std::string k = "Dist"+std::to_string(i+1);
    s.dist[i] = val(k.c_str(),std::abs(s.dist[i]),100)*(i<2 ? -1.f : 1.f);
    }
  for(int i=0; i<5; ++i) {
    const std::string k = "Size"+std::to_string(i+1);
    s.size[i] = val(k.c_str(),s.size[i],100);
    }
  const char* c1[] = {"ColorR1","ColorG1","ColorB1"};
  const char* c3[] = {"ColorR3","ColorG3","ColorB3"};
  const char* cs[] = {"ColorRSun","ColorGSun","ColorBSun"};
  for(int i=0; i<3; ++i) {
    s.rgb1[i]   = val(c1[i],s.rgb1[i],1);
    s.rgb3[i]   = val(c3[i],s.rgb3[i],1);
    s.rgbSun[i] = val(cs[i],s.rgbSun[i],1);
    }
  s.sizeSun = val("SizeSun",s.sizeSun,100);
  return s;
  }

LensFlare::LensFlare(const Settings& settings, std::optional<Vec3> sun)
  : set(settings), sun(sun) {
  }

void LensFlare::clear() {
  state = Off;
  fade  = 0;
  shoot = 0;
  seen  = false;
  out.clear();
  }

void LensFlare::update(float dt, const Vec3& eye, const Vec3& forward, const Mat4& vp, int w, int h,
                       const Blocked& blocked) {
  out.clear();
  if(!sun || w<=0 || h<=0)
    return;

  // 0x479250: the ray, then the fade
  shoot -= dt;
  if(shoot<0) {
    shoot = set.timeShootRay;
    seen  = !blocked(eye,*sun);
    }
  if(seen) {
    switch(state) {
      case Off:       state = FadingIn; fade = set.fadeTime; break;
      case FadingIn:  fade -= dt; if(fade<0) state = On; break;
      case FadingOut: state = FadingIn; fade = set.fadeTime-fade; break;
      case On:        break;
      }
    }
  else {
    switch(state) {
      case Off:       break;
      case FadingIn:  state = FadingOut; fade = set.fadeTime-fade; break;
      case FadingOut: fade -= dt; if(fade<0) state = Off; break;
      case On:        state = FadingOut; fade = set.fadeTime; break;
      }
    }

  // where a point is on the screen, if it is at least `near` in front of the camera
  struct Screen { float x, y, depth; };
  auto project = [&](const Vec3& p, float near = ViewPlane) -> std::optional<Screen> {
    const float z = vp.m[3][0]*p.x+vp.m[3][1]*p.y+vp.m[3][2]*p.z+vp.m[3][3];
    if(z<near)
      return std::nullopt;
    const Vec3 c = vp.point(p);
    return Screen{(c.x/z*0.5f+0.5f)*float(w),(c.y/z*0.5f+0.5f)*float(h),c.z/z};
    };

  const auto at = project(*sun);
  if(!at) {
    state = Off;
    return;
    }
  // the sizes are shares of the screen's width: of a 4:3 screen as high as this one, so that a wide screen shows
  // what the original's 4:3 picture showed
  const float vpW = std::min(float(w),float(h)*4.f/3.f);

  // 0x479070: the disc, a little in front of the sky it stands on
  Quad disc;
  disc.texture = Quad::SunDisc;
  disc.x       = at->x;
  disc.y       = at->y;
  disc.halfW   = vpW*set.sizeSun*0.5f;
  disc.halfH   = disc.halfW;
  for(int i=0; i<3; ++i)
    disc.rgba[i] = set.rgbSun[i]/255.f;
  disc.rgba[3] = 1;
  if(auto d = project(eye+(*sun-eye)*0.99f))
    disc.depth = d->depth;
  out.push_back(disc);

  if(state==Off)
    return;
  // the opacity by how far the sun is from the middle of the screen (0x479250, a byte)
  const float cx = float(w)*0.5f, cy = float(h)*0.5f;
  const float ax = 255.f*(1.f-std::abs(at->x-cx)/(cx+disc.halfW));
  const float ay = 255.f*(1.f-std::abs(at->y-cy)/(cy+disc.halfH));
  const float base = std::floor(std::clamp(std::min(ax,ay),0.f,255.f));
  if(base<Eps) {
    state = Off;
    return;
    }
  // 0x4797c0: the fade
  float k = 1;
  if(state==FadingOut)
    k = set.fadeTime>Eps ? std::clamp(fade/set.fadeTime,0.f,1.f) : 0.f;
  else if(state==FadingIn)
    k = set.fadeTime>Eps ? std::clamp(1.f-fade/set.fadeTime,0.f,1.f) : 1.f;
  const float alpha = std::floor(base*k)/255.f;

  // the line from C, the view plane (+ CamOffset) in front of the camera, to the sun
  const Vec3 c   = eye+forward*(ViewPlane+set.camOffset);
  Vec3       dir = *sun-c;
  if(dir.length()<Eps)
    return;
  dir = dir.normalized();
  static const Quad::Texture tex[5] = {Quad::Flare1,Quad::Flare3,Quad::Flare2,Quad::Flare3,Quad::Shine};
  for(int i=0; i<5; ++i) {
    // the pieces behind C come nearer than the view plane: 0x406f20 projects them all the same
    std::optional<Screen> p = i<4 ? project(c+dir*set.dist[i],Eps) : at;
    if(!p)
      continue;
    Quad q;
    q.texture  = tex[i];
    q.x        = p->x;
    q.y        = p->y;
    q.halfW    = q.halfH = vpW*set.size[i]*0.5f;
    q.additive = true;
    const float* rgb = i==0 ? set.rgb1 : i==2 ? set.rgb3 : nullptr;
    for(int j=0; j<3; ++j)
      q.rgba[j] = rgb!=nullptr ? rgb[j]/255.f : 1.f;
    q.rgba[3]  = alpha;
    q.depth    = 0;
    out.push_back(q);
    }
  }
