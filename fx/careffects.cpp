#include "careffects.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace rc;
using namespace rc::fx;

namespace {

constexpr float Pi = 3.14159265f;

// Settings\<name>.ini if the game has it, otherwise the compiled Settings\<name>.crs
SliderFile sliders(const EffectSettings::Reader& read, const std::string& name) {
  try {
    const auto d = read("Settings\\"+name+".ini");
    return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(...) {
    }
  return SliderFile::parseCrs(read("Settings\\"+name+".crs"));
  }

CurveSpline curve(const EffectSettings::Reader& read, const std::string& name) {
  try {
    const auto d = read("Splines\\"+name+".spl");
    return CurveSpline::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(...) {
    return CurveSpline();
    }
  }

// RCCars.exe 0x532410: the slider's position over a divisor, 0 if the key is missing
float val(const SliderFile& f, std::string_view key, float div) {
  return float(f.raw(key,0))/div;
  }

// 0x5289c0: half the chord a plane `d` from the centre cuts out of a circle of radius r
float halfChord(float d, float r) {
  d = std::abs(d);
  return d>=r ? 0.f : std::sqrt(r*r-d*d);
  }

}

EffectSettings EffectSettings::load(const Reader& read) {
  EffectSettings s;

  const auto d = sliders(read,"car_dustx");
  auto& dust = s.dust;
  dust.gravityY   = val(d,"GravityY",-100);
  dust.dx         = val(d,"DX",10);
  dust.dy         = val(d,"DY",10);
  dust.twist      = val(d,"TwistSpeed",100);
  dust.scaleX     = val(d,"ScaleX",100);
  dust.scaleXDisp = val(d,"ScaleXDisp",100);
  dust.scaleY     = val(d,"ScaleY",100);
  dust.scaleYDisp = val(d,"ScaleYDisp",100);
  dust.grow       = val(d,"DynamicScale",10);
  dust.growFront  = val(d,"DynamicScaleForw",10);
  dust.speedAtt   = val(d,"SpeedAtt",100);
  dust.spinThresh = val(d,"WheelSpinThresh",100);
  dust.spinScale  = val(d,"WheelSpinScale",100);
  dust.spinSpeed  = val(d,"WheelSpinSpeed",100);
  dust.shiftX     = val(d,"ShiftX",100);
  dust.intensity  = curve(read,"dust_int");
  dust.speed      = curve(read,"dust_speed");
  // the surface types of the game, 4 (wood?) has no settings
  const std::pair<rc::Surface,const char*> surfaces[] = {
    {rc::Surface::Default, "surf_default"}, {rc::Surface::Sand,     "surf_sand"},
    {rc::Surface::WetSand, "surf_wetsand"}, {rc::Surface::DeepSand, "surf_dunesand"},
    {rc::Surface::Gravel,  "surf_gravel"},
    };
  for(auto& [id,name]:surfaces) {
    const auto f = sliders(read,name);
    auto&      t = dust.surfaces[size_t(id)];
    t.intScale = val(f,"DustX_IntScale",100);
    t.rgba[0]  = val(f,"DustX_ColorR",1);
    t.rgba[1]  = val(f,"DustX_ColorG",1);
    t.rgba[2]  = val(f,"DustX_ColorB",1);
    t.rgba[3]  = val(f,"DustX_ColorA",1);
    t.life     = val(f,"DustX_TimeLife",100);
    t.lifeDisp = val(f,"DustX_TimeLifeDisp",100);
    }

  const auto w = sliders(read,"water_spray");
  auto& spray = s.spray;
  spray.rgba[0]     = val(w,"ColorR",1);
  spray.rgba[1]     = val(w,"ColorG",1);
  spray.rgba[2]     = val(w,"ColorB",1);
  spray.rgba[3]     = val(w,"ColorA",1);
  spray.dx          = val(w,"DX",10);
  spray.dy          = val(w,"DY",10);
  spray.life        = val(w,"TimeLife",100);
  spray.lifeDisp    = val(w,"TimeLifeDisp",100);
  spray.shiftX      = val(w,"ShiftX",1000)-0.5f;
  spray.angle       = val(w,"AngleIncidence",1);
  spray.angleDisp   = val(w,"AngleIncidenceDisp",1);
  spray.angleSpan   = val(w,"AngleSpan",1);
  spray.velLen      = val(w,"VelLen",100);
  spray.velLenDisp  = val(w,"VelLenDisp",100);
  spray.scale       = val(w,"Scale",10);
  spray.timeScale   = val(w,"TimeScale",100);
  spray.oscillation = val(w,"OscillationLen",100);
  spray.speedDecay  = val(w,"SpeedDecay",100);
  spray.intensity   = curve(read,"wspr_int");
  return s;
  }

void CarState::sense(const CollisionMesh& track) {
  for(auto& w:wheels) {
    // the ground within a few centimetres of the tyre's bottom counts as touched
    const float tolerance = 0.03f;
    w.onGround = false;
    w.water    = track.waterLevel(w.hub,0.5f);
    if(auto g = track.groundHeight(w.hub+Vec3{0,w.radius,0},2.f*w.radius+tolerance)) {
      if(*g>=w.hub.y-w.radius-tolerance) {
        w.onGround = true;
        w.contact  = Vec3{w.hub.x,*g,w.hub.z};
        w.surface  = track.surfaceBelow(w.contact+Vec3{0,0.02f,0},0.05f);
        }
      }
    }
  }

CarEffects::CarEffects(const EffectSettings& settings, bool player)
  : set(settings), player(player) {
  }

void CarEffects::clear() {
  for(auto& w:wheels) {
    w.dust.clear();
    w.spray.clear();
    }
  }

void CarEffects::update(const CarState& car, const Vec3& camera, float dt, Random& rnd) {
  if(wheels.size()!=car.wheels.size())
    wheels.resize(car.wheels.size());
  for(size_t i=0; i<wheels.size(); ++i) {
    auto& fx = wheels[i];
    fx.front = car.wheels[i].front;
    // the computer's cars only have the rear wheels' effects
    if(!player && fx.front)
      continue;
    dust (fx,i,car,camera,dt,rnd);
    spray(fx,car.wheels[i],car,dt,rnd);
    }
  }

void CarEffects::dust(WheelFx& fx, size_t i, const CarState& car, const Vec3& camera, float dt, Random& rnd) {
  const auto& d = set.dust;
  const auto& w = car.wheels[i];

  // the particles there are first (0x52e960): they turn ever faster, fly on, slowing down sideways, and fall
  const float att = std::pow(d.speedAtt,dt);
  for(auto& p:fx.dust.particles()) {
    const float a = std::abs(p.angle*std::pow(std::abs(p.twist),dt));
    p.angle  = p.twist<0 ? -a : a;
    p.pos    = p.pos + p.vel*dt;
    p.vel.x *= att;
    p.vel.z *= att;
    p.vel.y += d.gravityY*dt;
    }
  fx.dust.age(dt);

  // then the wheel (0x52e320): dust where it touches dry ground, not in water, and only near the camera
  fx.rate = 0;
  if(!w.onGround || (w.water && *w.water>w.hub.y-w.radius))
    return;
  if((camera-w.contact).length()>=12.f)
    return;
  const auto& surf = d.surfaces[size_t(w.surface)];
  if(surf.intScale<1e-6f)
    return;
  fx.surface = &surf;
  Vec3 at = w.contact;

  // a spinning wheel throws the dust backwards along the car, otherwise it follows the car
  const Vec3 fwd = car.pose.vector({0,0,1});
  float speed = 0, k = 1;
  bool  back  = false;
  if((car.gas || car.brake) && w.slip>d.spinThresh) {
    fx.velocity = fwd*(-d.spinSpeed*w.rimSpeed);
    k     = d.spinScale;
    back  = w.rimSpeed<0;
    speed = std::abs(w.rimSpeed);
    }
  else {
    fx.velocity = car.velocity;
    speed = car.velocity.length()*3.6f;
    back  = Vec3::dot(fwd,car.velocity)<0;
    }
  // the dust comes from behind the car: the rear wheels going forward, the front ones reversing (in deep sand all)
  if(w.surface!=rc::Surface::DeepSand && back!=w.front)
    return;
  if(back)
    k *= w.front ? 0.5f : 0.25f;

  // front wheels: a little towards the other front wheel
  if(w.front) {
    for(auto& o:car.wheels)
      if(o.front && o.left!=w.left) {
        at = at + (o.hub-w.hub)*d.shiftX;
        break;
        }
    }
  if(speed>0.25f && !d.intensity.empty())
    fx.rate = d.intensity.at(speed)*k*0.66667f;
  fx.rate *= surf.intScale;
  fx.alpha = surf.rgba[3];
  fx.dust.moveTo(at);

  // new particles along the way the wheel went (0x52ee80: one when it stands still), as 0x52e810 sets them up
  size_t n = fx.dust.count(fx.rate,dt);
  if(n>0 && !fx.dust.moved())
    n = 1;
  const float v = fx.velocity.length();
  for(size_t j=0; j<n; ++j) {
    Particle* p = fx.dust.spawn(fx.dust.wayPoint(j,n));
    if(p==nullptr)
      break;
    p->life   = surf.life+rnd.sym()*surf.lifeDisp;
    if(!player)
      p->life *= 0.5f;
    p->left   = p->life;
    p->angle  = rnd.unit()*180.f;
    p->twist  = rnd.chance(50) ? -d.twist : d.twist;
    p->vel    = (v>=0.25f && !d.speed.empty()) ? fx.velocity*(d.speed.at(v)*0.01f) : Vec3{};
    p->scaleX = d.scaleX+rnd.sym()*d.scaleXDisp;
    p->scaleY = d.scaleY+rnd.sym()*d.scaleYDisp;
    for(int c=0; c<3; ++c)
      p->rgb[c] = surf.rgba[c]/255.f;
    }
  fx.dust.endFrame();
  }

void CarEffects::spray(WheelFx& fx, const CarState::Wheel& w, const CarState& car, float dt, Random& rnd) {
  const auto& s = set.spray;

  // the drops fly straight on, carried along with the car, and shrink (0x528780)
  Vec3 fwd = car.pose.vector({0,0,1});
  fwd.y = 0;
  fwd   = (std::abs(fwd.x)+std::abs(fwd.z)>1e-6f) ? fwd.normalized() : Vec3{0,0,1};
  const float carried = Vec3::dot(car.velocity*s.speedDecay,fwd);
  for(auto& p:fx.spray.particles()) {
    const float age = p.age();
    float k = 1;
    if(age>=0)
      k = age<=s.timeScale ? (s.timeScale>=1e-6f ? (s.scale-1.f)*age/s.timeScale+1.f : 0.f) : s.scale;
    p.scaleX = p.scaleY = k;
    p.base = p.base + p.vel*dt + fwd*(carried*dt);
    p.pos  = p.base + p.wobble*(rnd.sym()*s.oscillation);
    if(std::sqrt(p.vel.x*p.vel.x+p.vel.z*p.vel.z)<0.05f)
      p.vel.x = p.vel.z = 0;
    }
  fx.spray.age(dt);

  // a wheel cutting the water line faster than 15 km/h (0x527f70)
  fx.chord = 0;
  if(!w.water)
    return;
  const float r = w.radius;
  fx.chord = halfChord(*w.water-w.hub.y,r);
  const float speed = car.velocity.length()*3.6f;
  if(fx.chord<=0 || r<=0 || speed<=15.f || s.intensity.empty())
    return;
  const float rate = std::max(s.intensity.at(speed),0.f)*(2.f*fx.chord/r);
  fx.forward = fwd;
  fx.spray.moveTo(Vec3{w.hub.x,*w.water,w.hub.z});

  // drops thrown out sideways and up (0x528220); a third of them go the other way, shorter
  const Vec3 up  = {0,1,0};
  const Vec3 out = w.left ? Vec3{fwd.z,0,-fwd.x} : Vec3{-fwd.z,0,fwd.x};
  const size_t n = fx.spray.count(rate,dt);
  for(size_t j=0; j<n; ++j) {
    Particle* p = fx.spray.spawn(fx.spray.wayPoint(n,n));
    if(p==nullptr)
      break;
    Vec3 side = out*-1.f, along = fwd;
    p->life  = s.life+rnd.sym()*s.lifeDisp;
    p->angle = rnd.unit()*180.f;
    if(rnd.chance(50)) {
      side  = side*-1.f;
      along = along*-1.f;
      p->life *= 1.f/3.f;
      }
    p->left = p->life;
    const float f = rnd.sym()*fx.chord;
    const float a = (s.angle+rnd.sym()*s.angleDisp)*Pi/180.f;
    const float t = std::tan(s.angleSpan*Pi/180.f);
    const float k = fx.chord/(std::abs(t)>1e-6f ? t : 1e-6f);
    p->base   = p->base + along*f + side*s.shiftX;
    p->pos    = p->base;
    Vec3 vel  = side*(-std::cos(a)*k) + up*(std::sin(a)*k) + along*f;
    if(vel.length()>=1e-6f)
      vel = vel.normalized()*(s.velLen+rnd.sym()*s.velLenDisp);
    p->vel    = vel;
    p->wobble = along;
    for(int c=0; c<3; ++c)
      p->rgb[c] = s.rgba[c]/255.f;
    }
  fx.spray.endFrame();
  }

void CarEffects::collect(std::vector<Billboard>& out) const {
  // dust: sized by distance like everything else; RCCars.exe gives the sprite DX x scale / depth pixels on a 640
  // wide screen with a horizontal field of view of 80 degrees (0x459260, 0x405f70), which is this many metres
  const float metres = std::tan(40.f*Pi/180.f)/320.f;
  for(auto& w:wheels) {
    const float grow = w.front ? set.dust.growFront : set.dust.grow;
    for(auto& p:w.dust.particles()) {
      // 0x52e180: grows to `grow` times its size over the first second
      const float age = p.age();
      const float g   = age<0 ? 1.f : age>1.f ? grow : 1.f+(grow-1.f)*age;
      Billboard b;
      b.pos     = p.pos;
      b.halfW   = p.scaleX*set.dust.dx*g*metres;
      b.halfH   = p.scaleY*set.dust.dy*g*metres;
      b.angle   = p.angle*Pi/180.f;
      b.texture = Billboard::Dust;
      for(int c=0; c<3; ++c)
        b.rgba[c] = p.rgb[c];
      b.rgba[3] = w.alpha/255.f*(p.life>0 ? std::max(p.left,0.f)/p.life : 0.f);
      out.push_back(b);
      }
    // spray: the same number of pixels at any distance (emitter flag 0x80)
    for(auto& p:w.spray.particles()) {
      Billboard b;
      b.pos     = p.pos;
      b.halfW   = p.scaleX*set.spray.dx;
      b.halfH   = p.scaleY*set.spray.dy;
      b.angle   = p.angle*Pi/180.f;
      b.texture = Billboard::Droplet;
      b.pixels  = true;
      for(int c=0; c<3; ++c)
        b.rgba[c] = p.rgb[c];
      b.rgba[3] = set.spray.rgba[3]/255.f*(p.life>0 ? std::max(p.left,0.f)/p.life : 0.f);
      out.push_back(b);
      }
    }
  }
