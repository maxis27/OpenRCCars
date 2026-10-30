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

  const auto e = sliders(read,"exhausted_gas");
  auto& ex = s.exhaust;
  ex.gravityY     = val(e,"GravityY",-100);
  ex.dx           = val(e,"DX",10);
  ex.dy           = val(e,"DY",10);
  ex.twist        = val(e,"TwistSpeed",100);
  ex.scaleX       = val(e,"ScaleX",100);
  ex.scaleXDisp   = val(e,"ScaleXDisp",100);
  ex.scaleY       = val(e,"ScaleY",100);
  ex.scaleYDisp   = val(e,"ScaleYDisp",100);
  ex.grow         = val(e,"DynamicScale",100);
  ex.speed        = val(e,"Speed",100);
  ex.speedAtt     = val(e,"SpeedAtt",100);
  ex.life         = val(e,"TimeLife",100);
  ex.lifeDisp     = val(e,"TimeLifeDisp",100);
  ex.explodeTime  = val(e,"ExplodeTime",100);
  ex.explodeColor = val(e,"ExplodeColor",100);
  ex.explodeAlpha = val(e,"ExplodeAlpha",100);
  ex.wheelSpin    = val(e,"WheelSpin",100);
  ex.intensity    = curve(read,"eg_int");

  const auto t = sliders(read,"car_trace");
  s.tracks.timeLife   = val(t,"timeLife",1);
  s.tracks.maxLen     = val(t,"maxLen",100);
  s.tracks.scaleCoeff = val(t,"scaleCoeff",100);
  s.tracks.maxHeight  = val(t,"maxHeight",1000);

  const auto k = sliders(read,"wtr_trace");
  auto& wk = s.wake;
  wk.forwZ     = val(k,"ForwZ",100);
  wk.backZ     = val(k,"BackZ",100);
  wk.forwX     = val(k,"ForwX",100);
  wk.backX     = val(k,"BackX",100);
  wk.forwA     = float(int(val(k,"ForwA",1)) & 0xff);
  wk.backA     = float(int(val(k,"BackA",1)) & 0xff);
  wk.animSpeed = val(k,"AnimDUVSpeed",100);
  wk.animMin   = val(k,"AnimDUVMin",100);
  wk.animMax   = val(k,"AnimDUVMax",100);
  wk.speed     = curve(read,"wtr_text");

  const auto h = sliders(read,"whr_horse");
  auto& fo = s.foam;
  fo.forwZ        = val(h,"ForwZ",100);
  fo.forwX        = val(h,"ForwX",100);
  fo.forwZBody    = val(h,"ForwZBody",100);
  fo.forwXBody    = val(h,"ForwXBody",100);
  fo.forwA        = float(int(val(h,"ForwA",1)) & 0xff);
  fo.normalOffset = val(h,"NormalOffset",1000);
  fo.animDelay    = val(h,"AnimDelay",100);
  return s;
  }

void CarState::sense(const CollisionMesh& track) {
  for(auto& w:wheels) {
    // the ground within a few centimetres of the tyre's bottom counts as touched
    const float tolerance = 0.03f;
    w.onGround = false;
    w.water    = track.waterLevel(w.hub,0.5f);
    if(auto g = track.raycast(w.hub+Vec3{0,w.radius,0},Vec3{0,-1,0},2.f*w.radius+tolerance)) {
      if(g->point.y>=w.hub.y-w.radius-tolerance) {
        w.onGround = true;
        w.contact  = Vec3{w.hub.x,g->point.y,w.hub.z};
        w.normal   = g->normal.y<0 ? g->normal*-1.f : g->normal;
        w.triangle = g->triangle;
        w.sound    = track.triangles()[g->triangle].sound;
        w.surface  = track.surfaceBelow(w.contact+Vec3{0,0.02f,0},0.05f);
        }
      }
    }
  bodyWater = track.waterLevel(body,std::max(bodyHeight,0.5f));
  }

CarEffects::CarEffects(const EffectSettings& settings, bool player)
  : set(settings), player(player), tracks(settings.tracks,player), wake(settings.wake,settings.foam) {
  }

void CarEffects::clear(bool keep) {
  for(auto& w:wheels) {
    w.dust.clear();
    w.spray.clear();
    }
  exhaust.clear();
  explode  = 0;
  boostWas = false;
  grey     = 255;
  wake.clear();
  if(keep) {
    tracks.cut();
    return;
    }
  tracks.clear();
  dirtAmount = 0;
  }

float CarEffects::dirt() const {
  // 0x531c20: dirt_car<n>_1 and _2 over the clean car, by thirds of 3000 .. here halves, there being two textures
  return std::clamp(std::min(dirtAmount,2999.f)*2.f/3000.f,0.f,2.f);
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
    // what is switched off is not there at all (RCCars.exe does not set it up: 0x52e000, 0x527e20)
    if(features.dust)
      dust(fx,i,car,camera,dt,rnd);
    else
      fx.dust.clear();
    if(features.water)
      spray(fx,car.wheels[i],car,dt,rnd);
    else
      fx.spray.clear();
    }
  if(features.exhaust) {
    smoke(car,dt,rnd);
    }
  else {
    exhaust.clear();
    explode = 0;
    }
  marks(car,dt,rnd);
  }

void CarEffects::setFeatures(const Features& f) {
  if(!f.tracks && features.tracks)
    tracks.clear();
  if(!f.water && features.water)
    wake.clear();
  features = f;
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
  if(!w.onGround)
    return;
  if(w.water && *w.water>w.hub.y-w.radius) {
    // the water washes the dirt off (0x52e320)
    dirtAmount = std::max(dirtAmount-350.f*dt,0.f);
    return;
    }
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
    // half of it below high SFXQual (0x52e5bc)
    k     = d.spinScale*(features.fullSpin ? 1.f : 0.5f);
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
  // and the dust settles on the car (0x52e320: a tenth of the particles a second, 0.6 with a flag at 0x14ef0f8 we
  // have not seen set)
  if(dirtAmount<3000.f)
    dirtAmount += fx.rate*0.1f*dt;
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

void CarEffects::smoke(const CarState& car, float dt, Random& rnd) {
  const auto& e = set.exhaust;

  // the puffs move like the dust (0x530800)
  const float att = std::pow(e.speedAtt,dt);
  for(auto& p:exhaust.particles()) {
    const float a = std::abs(p.angle*std::pow(std::abs(p.twist),dt));
    p.angle  = p.twist<0 ? -a : a;
    p.pos    = p.pos + p.vel*dt;
    p.vel.x *= att;
    p.vel.z *= att;
    p.vel.y += e.gravityY*dt;
    }
  exhaust.age(dt);
  // only the players' cars smoke (0x531650), from the end of the booster bought (0x50bcc0)
  if(!player || !car.exhaust)
    return;

  // 0x5303c0: how much smoke. The turbo coming on above 10 km/h makes a big dark puff for ExplodeTime; a spinning
  // rear wheel smokes (our car has no wheel spin), the turbo above 5 km/h more, the gas less
  float x = car.velocity.length()*3.6f;
  puff = boostSmoke = false;
  bool spin = false;
  if(explode<=0) {
    if(!boostWas && car.boost && x>10.f)
      explode = e.explodeTime;
    else {
      float slip = 0;
      for(auto& w:car.wheels)
        if(w.slot==2 || w.slot==3)
          slip = std::max(slip,w.slip);
      if((car.gas || car.brake) && slip>e.wheelSpin) {
        x    = 2.f*std::abs(slip);
        spin = true;
        }
      else if(car.boost && x>5.f)
        boostSmoke = true;
      }
    boostWas = car.boost;
    }
  else {
    x = 50;
    explode -= dt;
    }
  float rate = 0;
  if(!e.intensity.empty()) {
    const float n = e.intensity.at(x);
    if(explode>0)
      rate = 2.f*n;
    else if(spin)
      rate = n;
    else if(x>0.25f) {
      if(boostSmoke)
        rate = n*1.5f;
      else if(car.gas)
        rate = n*0.75f;
      }
    }
  puff = explode>0;
  // the puff darkens the smoke at 512 a second down to ExplodeColor, after it it clears up at 256 a second
  if(puff)
    grey = std::max(grey-dt*512.f,e.explodeColor*255.f);
  else
    grey = std::min(grey+dt*256.f,255.f);

  // 0x5306d0: the new puffs
  exhaust.moveTo(car.exhaust->pos);
  const size_t n = exhaust.count(rate,dt);
  for(size_t j=0; j<n; ++j) {
    Particle* p = exhaust.spawn(car.exhaust->pos);
    if(p==nullptr)
      break;
    p->life = e.life+rnd.sym()*e.lifeDisp;
    if(puff) {
      p->life *= 3.f;
      p->alpha = e.explodeAlpha;
      }
    else if(spin)
      p->life *= 2.5f;
    else if(boostSmoke)
      p->life *= 1.5f;
    p->left   = p->life;
    p->angle  = rnd.unit()*180.f;
    p->twist  = rnd.chance(50) ? -e.twist : e.twist;
    p->vel    = car.exhaust->dir*e.speed+car.velocity;
    p->scaleX = e.scaleX+rnd.sym()*e.scaleXDisp;
    p->scaleY = e.scaleY+rnd.sym()*e.scaleYDisp;
    for(float& c:p->rgb)
      c = grey/255.f;
    }
  exhaust.endFrame();
  }

void CarEffects::marks(const CarState& car, float dt, Random& rnd) {
  const Vec3 forward = car.pose.vector({0,0,1});

  // the tyre tracks of RCCars.exe's wheels 0..3
  std::array<TyreTracks::Wheel,4> tw;
  for(auto& w:car.wheels) {
    if(w.slot<0 || w.slot>=4)
      continue;
    auto& t = tw[size_t(w.slot)];
    // in the water: the water line within a wheel's radius over the hub, or the hub under water (0x531df0)
    const bool wet = w.water && w.hub.y-*w.water<w.radius;
    t.touching = w.onGround && !wet;
    t.contact  = w.contact;
    t.normal   = w.normal;
    t.triangle = w.triangle;
    t.sound    = w.sound;
    }
  if(features.tracks)
    tracks.update(tw,car.velocity,forward,car.upgrade,dt);

  if(!player || !features.water)
    return;
  // the wake and the foam: what the water is like at each wheel and at the body (0x531df0)
  Wake::Car wc;
  wc.velocity = car.velocity;
  wc.right    = car.pose.vector({1,0,0});
  wc.up       = car.pose.vector({0,1,0});
  wc.forward  = forward;
  wc.tyre     = car.upgrade;
  auto probe = [](const Vec3& at, const std::optional<float>& water, float limit) {
    Wake::Probe p;
    p.pos   = at;
    p.limit = limit;
    if(water) {
      p.waterY  = *water;
      p.depth   = at.y-*water;
      p.inWater = p.depth<limit;
      }
    return p;
    };
  std::vector<const CarState::Wheel*> bySlot;
  for(auto& w:car.wheels)
    if(w.slot>=0) {
      if(bySlot.size()<=size_t(w.slot))
        bySlot.resize(size_t(w.slot)+1,nullptr);
      bySlot[size_t(w.slot)] = &w;
      }
  for(auto* w:bySlot) {
    if(w==nullptr) {
      wc.wheels.push_back(Wake::Probe{});
      continue;
      }
    Wake::Probe p = probe(w->hub,w->water,w->radius);
    p.axle    = w->axle;
    p.heading = Vec3::cross(w->axle,wc.up).normalized();
    wc.wheels.push_back(p);
    }
  wc.body     = probe(car.body,car.bodyWater,car.bodyHeight*0.5f);
  // the same a quarter of the body's height lower, under water only within that quarter
  wc.bodyDeep = wc.body;
  wc.bodyDeep.limit  = wc.body.limit*0.5f;
  wc.bodyDeep.depth -= wc.bodyDeep.limit;
  wc.bodyDeep.inWater = wc.body.inWater && std::abs(wc.bodyDeep.depth)<wc.bodyDeep.limit;
  wake.update(wc,dt,rnd);
  }

void CarEffects::collect(std::vector<Mark>& out, const Vec3& camera) const {
  if(features.tracks)
    tracks.collect(out,camera);
  if(player && features.water)
    wake.collect(out);
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
  // exhaust: the dust's texture (0x530180), growing by DynamicScale - 1 its size a second (0x530330)
  for(auto& p:exhaust.particles()) {
    const float g = (set.exhaust.grow-1.f)*p.age()+1.f;
    Billboard b;
    b.pos     = p.pos;
    b.halfW   = p.scaleX*set.exhaust.dx*g*metres;
    b.halfH   = p.scaleY*set.exhaust.dy*g*metres;
    b.angle   = p.angle*Pi/180.f;
    b.texture = Billboard::Dust;
    for(int c=0; c<3; ++c)
      b.rgba[c] = p.rgb[c];
    b.rgba[3] = p.alpha*(p.life>0 ? std::max(p.left,0.f)/p.life : 0.f);
    out.push_back(b);
    }
  }
