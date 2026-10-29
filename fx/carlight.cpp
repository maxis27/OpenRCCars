#include "carlight.h"

#include <algorithm>
#include <cmath>

#include "formats/pack.h"

using namespace rc;
using namespace rc::fx;

CarLight CarLight::load(PackFile& pack) {
  CarLight l;
  auto ini = [&pack](const char* name) {
    try {
      auto d = pack.read(std::string("Settings\\")+name);
      return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
      }
    catch(const std::exception&) {
      return SliderFile();
      }
    };
  const SliderFile light = ini("carLight.ini");
  l.ambient = float(std::clamp(light.raw("AmbientI",138),0,255))/255.f;
  l.direct  = float(std::clamp(light.raw("DirectI", 128),0,255))/255.f;
  const float az = float(light.raw("AngleAzimuth",0))  *3.14159265f/180.f;
  const float el = float(light.raw("AngleElevation",75))*3.14159265f/180.f;
  l.toLight = Vec3{std::sin(az)*std::cos(el),std::sin(el),std::cos(az)*std::cos(el)};

  const SliderFile shade = ini("car_lmshd.ini");
  l.alphaVel   = float(shade.raw("alphavel",127))/100.f;
  l.deltaAlpha = float(shade.raw("deltaalpha",5))/100.f;
  l.maxDist    = float(shade.raw("maxdist",25));
  try {
    l.curve = Spline::parse(pack.read("Splines\\light_car.bspl"));
    }
  catch(const std::exception&) {
    }
  try {
    l.featCurve = Spline::parse(pack.read("Splines\\light_feat.bspl"));
    }
  catch(const std::exception&) {
    }
  return l;
  }

LightmapProbe::LightmapProbe(const SbScene& scene, const CollisionMesh& track, Read read)
  :scene(scene), track(track), read(std::move(read)) {
  }

const Image* LightmapProbe::texture(int32_t index, int32_t model) {
  auto key = std::make_pair(index,model);
  if(auto it = images.find(key); it!=images.end())
    return it->second.w>0 ? &it->second : nullptr;
  Image& img = images[key];
  const SbTexture* t = scene.texture(index,model);
  if(t==nullptr)
    return nullptr;
  try {
    if(!t->embedded.empty()) {
      img = decodeCsi(t->embedded.data(),t->embedded.size());
      }
    else {
      for(const char* dir:{"Textures.1\\","Textures.2\\","Textures.3\\"}) {
        try {
          auto d = read(std::string(dir)+t->name+".csi");
          img = decodeCsi(d.data(),d.size());
          break;
          }
        catch(const std::exception&) {
          }
        }
      }
    }
  catch(const std::exception&) {
    img = Image();
    }
  return img.w>0 && img.h>0 ? &img : nullptr;
  }

std::optional<float> LightmapProbe::at(uint32_t triangle, const Vec3& point) {
  const auto& tris = track.triangles();
  if(triangle>=tris.size())
    return std::nullopt;
  const auto& tr = tris[triangle];
  if(tr.mesh>=scene.meshes.size())
    return std::nullopt;
  const SbMesh& m = scene.meshes[tr.mesh];
  if(tr.face>=m.faces.size())
    return std::nullopt;
  const SbFace& f  = m.faces[tr.face];
  const auto*   lm = f.layer(LayerTag::Lightmap);
  if(lm==nullptr || f.pos.size()<3 || lm->uv.size()<3)
    return std::nullopt;
  for(int i=0; i<3; ++i)
    if(f.pos[i]>=m.positions.size() || lm->uv[i]>=m.uvs.size())
      return std::nullopt;
  const Image* img = texture(lm->texture,m.model);
  if(img==nullptr)
    return std::nullopt;

  // the point in the face's first three corners: p - c = s (a - c) + t (b - c), 0x410480
  const Vec3  a = m.world.point(m.positions[f.pos[0]]);
  const Vec3  b = m.world.point(m.positions[f.pos[1]]);
  const Vec3  c = m.world.point(m.positions[f.pos[2]]);
  const Vec3  e0 = a-c, e1 = b-c, p = point-c;
  const float d00 = Vec3::dot(e0,e0), d01 = Vec3::dot(e0,e1), d11 = Vec3::dot(e1,e1);
  const float det = d00*d11-d01*d01;
  if(std::abs(det)<1e-12f)
    return std::nullopt;
  const float p0 = Vec3::dot(p,e0), p1 = Vec3::dot(p,e1);
  const float s  = (p0*d11-p1*d01)/det;
  const float t  = (p1*d00-p0*d01)/det;
  const Vec2& ua = m.uvs[lm->uv[0]];
  const Vec2& ub = m.uvs[lm->uv[1]];
  const Vec2& uc = m.uvs[lm->uv[2]];
  const float u = uc.x + s*(ua.x-uc.x) + t*(ub.x-uc.x);
  const float v = uc.y + s*(ua.y-uc.y) + t*(ub.y-uc.y);

  // truncated (ftol), held inside the picture
  const int      x  = std::clamp(int(u*float(img->w)),0,int(img->w)-1);
  const int      y  = std::clamp(int(v*float(img->h)),0,int(img->h)-1);
  const uint8_t* px = &img->rgba[(size_t(y)*img->w+size_t(x))*4];
  return (float(px[0])+float(px[1])+float(px[2]))/(3.f*255.f);
  }

std::optional<float> LightmapProbe::below(const Vec3& from, float length) {
  auto hit = track.raycast(from,{0,-1,0},length);
  if(!hit)
    return std::nullopt;
  return at(hit->triangle,hit->point);
  }

std::optional<float> LightmapProbe::ground(const Vec3& from, float length) {
  auto hit = track.raycast(from,{0,-1,0},length);
  if(!hit)
    return std::nullopt;
  return at(hit->triangle,hit->point).value_or(1.f);
  }

void FeatureShade::update(const Spline& curve, const Settings& cfg, float dt, const Sample& sample) {
  bool look = dt<=0;
  if(look) {
    timer = 0;
    }
  else {
    timer -= dt;
    if(timer<=0) {
      timer = cfg.interval;
      look  = true;
      }
    }
  if(look) {
    const auto light = sample();
    if(!light)
      return;
    target = curve.empty() ? *light : std::clamp(curve.at(*light*255.f)/255.f,0.f,1.f);
    }
  if(dt<=0) {
    k = target;
    return;
    }
  if(std::abs(k-target)<=cfg.delta)
    return;
  const float step = cfg.speed*dt;
  k = target>k ? std::min(k+step,target) : std::max(k-step,target);
  }

void CarShade::update(const CarLight& cfg, float dt, bool near, std::optional<float> light) {
  if(!near) {
    reset();
    return;
    }
  const bool first = !primed;
  if(first) {
    k      = 1;
    primed = true;
    }
  if(!light)
    return;
  const float target = cfg.curve.empty() ? *light : cfg.curve.at(*light*255.f)/255.f;
  if(first) {
    k = target;
    return;
    }
  if(std::abs(k-target)<=cfg.deltaAlpha)
    return;
  const float step = cfg.alphaVel*dt;
  k = target>k ? std::min(k+step,target) : std::max(k-step,target);
  }
