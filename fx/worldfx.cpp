#include "worldfx.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace rc;
using namespace rc::fx;

namespace {

constexpr float Pi = 3.14159265f;
constexpr float Eps = 1e-6f;   // RCCars.exe's small number (0x554380)

// RCCars.exe sizes its sprites DX x scale / depth pixels on a 640 wide screen with a horizontal field of view of
// 80 degrees (0x459260, 0x405f70), which is this many metres a pixel at 1 m (as fx/careffects.cpp)
const float Metres = std::tan(40.f*Pi/180.f)/320.f;

// Settings\<name>.ini if the game has it, otherwise the compiled Settings\<name>.crs (as fx/careffects.cpp)
SliderFile sliders(const WorldSettings::Reader& read, const std::string& name) {
  try {
    const auto d = read("Settings\\"+name+".ini");
    return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(...) {
    }
  return SliderFile::parseCrs(read("Settings\\"+name+".crs"));
  }

// RCCars.exe 0x532410: the slider's position over a divisor, 0 if the key is missing
float val(const SliderFile& f, std::string_view key, float div) {
  return float(f.raw(key,0))/div;
  }

WorldSettings::Drops drops(const SliderFile& f) {
  WorldSettings::Drops d;
  d.rgb[0]     = val(f,"ColorR",1);
  d.rgb[1]     = val(f,"ColorG",1);
  d.rgb[2]     = val(f,"ColorB",1);
  d.dx         = val(f,"DX",10);
  d.dy         = val(f,"DY",10);
  d.life       = val(f,"TimeLife",100);
  d.lifeDisp   = val(f,"TimeLifeDisp",100);
  d.angleSpan  = val(f,"AngleSpan",1);
  d.velLen     = val(f,"VelLen",100);
  d.velLenDisp = val(f,"VelLenDisp",100);
  d.scale      = val(f,"Scale",10);
  d.timeScale  = val(f,"TimeScale",100);
  d.intensity  = val(f,"Intensity",1);
  d.emitLife   = val(f,"EmitLife",100);
  d.splashLen  = val(f,"SplashLen",100);
  d.velAtt     = val(f,"VelAttenuation",100);
  d.alphaAtt   = val(f,"AlphaAttenuation",100);
  return d;
  }

WorldSettings::Puff puff(const SliderFile& f) {
  WorldSettings::Puff p;
  p.intensity  = val(f,"Intensity",1);
  p.timeEmit   = val(f,"TimeEmit",100);
  p.life       = val(f,"TimePart",100);
  p.speedY     = val(f,"SpeedY",100);
  p.speedDecay = val(f,"SpeedDecay",100);
  p.dx         = val(f,"DX",10);
  p.dy         = val(f,"DY",10);
  p.twist      = val(f,"TwistSpeed",100);
  p.scaleX     = val(f,"ScaleX",100);
  p.scaleXDisp = val(f,"ScaleXDisp",100);
  p.scaleY     = val(f,"ScaleY",100);
  p.scaleYDisp = val(f,"ScaleYDisp",100);
  p.grow       = val(f,"DynamicScale",10);
  p.growTime   = val(f,"DynamicScaleTime",100);
  p.rgba[0]    = val(f,"ColorR",1);
  p.rgba[1]    = val(f,"ColorG",1);
  p.rgba[2]    = val(f,"ColorB",1);
  p.rgba[3]    = val(f,"ColorA",1);
  return p;
  }

// a value going from 1 to `to` over `time` s of `age` (0x527080, 0x52cbc0, 0x52c5e0)
float ramp(float age, float time, float to) {
  if(age<0)
    return 1;
  if(age>time)
    return to;
  if(std::abs(time)<Eps)
    return 0;
  return (to-1.f)*age/time+1.f;
  }

// opaque until `share` of the life, then fading out to the end (0x527230, 0x52cc70)
float fadeOut(const Particle& p, float share) {
  const float age = p.age(), hold = p.life*share;
  if(age<hold)
    return 1;
  if(age>=p.life || std::abs(p.life-hold)<Eps)
    return 0;
  return 1.f-(age-hold)/(p.life-hold);
  }

// a random direction (0x4105f0: a point of the cube -0.5..0.5, made a unit vector)
Vec3 randomDir(Random& rnd) {
  const Vec3 v = {rnd.unit()-0.5f,rnd.unit()-0.5f,rnd.unit()-0.5f};
  return v.length()<Eps ? Vec3{0,1,0} : v.normalized();
  }

// a random direction at most `deg` degrees off `axis` (0x4106d0)
Vec3 randomInCone(const Vec3& axis, float deg, Random& rnd) {
  Vec3 side = Vec3::cross(axis,randomDir(rnd));
  if(side.length()<Eps)
    side = Vec3::cross(axis,{1,0,0});
  side = side.normalized();
  const Vec3  up = Vec3::cross(side,axis);
  const float a  = rnd.unit()*deg*Pi/180.f;
  return axis*std::cos(a)+up*std::sin(a);
  }

// the puffs (bullet_dust, bullet_smoke): turning ever faster, rising and slowing down every frame (0x52c6f0)
void movePuffs(Emitter& e, const WorldSettings::Puff& s, float dt) {
  // RCCars.exe multiplies the speed by SpeedDecay once a frame; taken as frames of 1/60 s
  const float decay = std::pow(std::max(s.speedDecay,0.f),dt*60.f);
  for(auto& p:e.particles()) {
    const float a = std::abs(p.angle*std::pow(std::abs(p.twist),dt));
    p.angle = p.twist<0 ? -a : a;
    p.pos   = p.pos + p.vel*dt;
    p.vel   = p.vel*decay;
    }
  e.age(dt);
  }

void collectPuffs(const Emitter& e, const WorldSettings::Puff& s, std::vector<Billboard>& out) {
  for(auto& p:e.particles()) {
    // 0x52c5e0: grows to DynamicScale times its size over DynamicScaleTime of its life
    const float g = ramp(p.age(),p.life*s.growTime,s.grow);
    Billboard b;
    b.pos     = p.pos;
    b.halfW   = p.scaleX*s.dx*g*Metres;
    b.halfH   = p.scaleY*s.dy*g*Metres;
    b.angle   = p.angle*Pi/180.f;
    b.texture = Billboard::Dust;
    for(int c=0; c<3; ++c)
      b.rgba[c] = s.rgba[c]/255.f;
    b.rgba[3] = s.rgba[3]/255.f*(p.life>0 ? std::max(p.left,0.f)/p.life : 0.f);
    out.push_back(b);
    }
  }

void collectDrops(const Emitter& e, const WorldSettings::Drops& s, std::vector<Billboard>& out) {
  for(auto& p:e.particles()) {
    Billboard b;
    b.pos     = p.pos;
    b.halfW   = p.scaleX*s.dx*Metres;
    b.halfH   = p.scaleY*s.dy*Metres;
    b.angle   = p.angle*Pi/180.f;
    b.texture = Billboard::Droplet;
    for(int c=0; c<3; ++c)
      b.rgba[c] = s.rgb[c]/255.f;
    b.rgba[3] = fadeOut(p,s.alphaAtt);
    out.push_back(b);
    }
  }

}

WorldSettings WorldSettings::load(const Reader& read) {
  WorldSettings s;
  s.splash  = drops(sliders(read,"water_splash"));
  s.explode = drops(sliders(read,"bullet_explode"));
  // BULLET_EXPLODE has no SplashLen nor VelAttenuation: its drops keep their speed (0x52cbc0)
  s.explode.velAtt = 1;

  const auto m = sliders(read,"water_splash_mng");
  s.splashes.delay     = val(m,"TimeDelay",100);
  s.splashes.delayDisp = val(m,"TimeDelayDisp",100);
  s.splashes.radius    = val(m,"Radius",100);
  s.splashes.activeRad = val(m,"ActiveRad",100);

  s.bulletDust  = puff(sliders(read,"bullet_dust"));
  s.bulletSmoke = puff(sliders(read,"bullet_smoke"));

  const auto b = sliders(read,"burst_mng");
  s.burst.shots     = std::max(1,int(b.raw("NShoots",6)));
  s.burst.timeShoot = val(b,"TimeShoot",100);
  s.burst.offset    = val(b,"Offset",100);
  s.burst.radius    = val(b,"Radius",100);

  // whole numbers, scaled at the reading (0x525c10)
  const auto w = sliders(read,"waterLOD_wave");
  auto& wv = s.waves;
  wv.timeLong  = float(w.raw("TimeLong",200))*0.01f;
  wv.timeShort = float(w.raw("TimeShort",50))*0.01f;
  wv.angle     = float(w.raw("Angle",5));
  wv.len       = float(w.raw("Len",350))*0.01f;
  wv.activeRad = float(w.raw("ActiveRad",40));
  const auto n = sliders(read,"water_wave_new");
  wv.lifeLong      = val(n,"TimeLifeLong",100);
  wv.lifeShort     = val(n,"TimeLifeShort",100);
  wv.lifeShortDisp = val(n,"TimeLifeShortDisp",100);
  wv.halfLen       = val(n,"Len",100);
  wv.height        = val(n,"Height",100);
  wv.dHeight       = val(n,"DHeight",100)-1.f;
  wv.animSpeed     = val(n,"AnimSpeed",100);
  wv.animWait      = val(n,"AnimWait",100);
  wv.decTime       = val(n,"DecTime",100);
  wv.incTime       = val(n,"IncTime",100);

  // read as whole numbers over 100 (0x50fcb0)
  const auto p = sliders(read,"peop_sole");
  s.sole.delay  = val(p,"TimeDelay",100);
  s.sole.shiftX = val(p,"ShiftX",100);
  s.sole.shiftY = val(p,"ShiftY",100);
  s.sole.size   = val(p,"Size",100);
  return s;
  }

bool View::onScreen(const Vec3& p) const {
  const float z = viewProj.m[3][0]*p.x+viewProj.m[3][1]*p.y+viewProj.m[3][2]*p.z+viewProj.m[3][3];
  if(z<=Eps)
    return false;
  const Vec3 c = viewProj.point(p);
  return std::abs(c.x/z)<=1.f && std::abs(c.y/z)<=1.f;
  }

// --- water splashes ---

WaterSplashes::WaterSplashes(const WorldSettings& settings, const std::vector<Vec3>& at)
  : set(&settings) {
  for(auto& p:at)
    markers.push_back({p});
  pool.resize(markers.size()*2);
  }

void WaterSplashes::clear() {
  for(auto& m:markers)
    m.active = false;
  for(auto& s:pool) {
    s.drops.clear();
    s.emit = -1;
    }
  }

void WaterSplashes::update(const View& view, float dt, Random& rnd) {
  if(set==nullptr)
    return;
  const auto& s  = set->splash;
  const Vec3  up = {0,1,0};
  for(auto& sp:pool) {
    // the drops (0x527080): moving on, their speed the first one times what is left of it, growing
    for(auto& p:sp.drops.particles()) {
      const float age = p.age();
      p.scaleX = p.scaleY = ramp(age,s.timeScale,s.scale);
      p.pos = p.pos + p.vel*dt;
      p.vel = p.wobble*ramp(age,p.life,s.velAtt);
      }
    sp.drops.age(dt);
    if(sp.emit<0)
      continue;

    // the splash faces the camera (0x526d40), and creeps towards it by 0.3 % a frame (taken as frames of 1/60 s)
    const Vec3 d    = sp.at-view.eye;
    Vec3       side = Vec3::cross(d,up);
    side   = side.length()>Eps ? side.normalized() : Vec3{1,0,0};
    sp.at  = sp.at-d*std::min(0.003f*dt*60.f,1.f);
    sp.drops.moveTo(sp.at);
    // the drops (0x526ea0): side by side along SplashLen, fanned out up to AngleSpan from the vertical
    const size_t n = sp.drops.count(s.intensity,dt);
    const float  t = std::tan(s.angleSpan*Pi/180.f);
    const float  k = s.splashLen/(std::abs(t)>Eps ? t : Eps);
    for(size_t i=0; i<n; ++i) {
      const float r  = rnd.sym()*s.splashLen;
      Particle*   p  = sp.drops.spawn(sp.at+side*r);
      if(p==nullptr)
        break;
      p->life   = s.life+rnd.sym()*s.lifeDisp;
      p->left   = p->life;
      p->angle  = rnd.unit()*180.f;
      Vec3 v    = side*r+up*k;
      if(v.length()>=Eps)
        v = v.normalized()*(s.velLen+rnd.sym()*s.velLenDisp);
      p->vel    = v;
      p->wobble = v;
      }
    sp.drops.endFrame();
    sp.emit -= dt;
    }

  // the markers (0x527a40): on the screen and near enough, one splashes every TimeDelay +- TimeDelayDisp s
  const auto& m = set->splashes;
  for(auto& mk:markers) {
    const float dx = mk.pos.x-view.eye.x, dz = mk.pos.z-view.eye.z;
    if(std::sqrt(dx*dx+dz*dz)>m.activeRad || !view.onScreen(mk.pos)) {
      mk.active = false;
      continue;
      }
    if(!mk.active) {
      mk.active = true;
      mk.timer  = 0;
      }
    mk.timer -= dt;
    if(mk.timer>=0)
      continue;
    auto free = std::find_if(pool.begin(),pool.end(),[](const Splash& sp){ return sp.emit<0; });
    if(free==pool.end())
      continue;   // the original tries again next frame
    Vec3 dir = randomDir(rnd);
    dir.y = 0;
    dir   = dir.length()>Eps ? dir.normalized() : Vec3{1,0,0};
    free->at   = mk.pos+dir*m.radius;
    free->emit = s.emitLife;
    free->drops.clear();
    free->drops.moveTo(free->at);
    mk.timer = m.delay+rnd.sym()*m.delayDisp;
    }
  }

void WaterSplashes::collect(std::vector<Billboard>& out) const {
  if(set==nullptr)
    return;
  for(auto& sp:pool)
    collectDrops(sp.drops,set->splash,out);
  }

// --- shore waves ---

ShoreWaves::ShoreWaves(const WorldSettings& settings, const std::vector<Vec3>& at, const WaterWaves& sea)
  : set(&settings), sea(sea) {
  for(auto& p:at)
    markers.push_back({p});
  }

void ShoreWaves::clear() {
  for(auto& w:pool)
    w.live = false;
  for(auto& m:markers)
    m.timeLong = m.timeShort = 0;
  }

bool ShoreWaves::spawn(const Vec3& at, const Vec3& heading, bool longWave, Random& rnd) {
  // a free one of the 32 (0x529c60)
  auto w = std::find_if(pool.begin(),pool.end(),[](const Wave& w){ return !w.live; });
  if(w==pool.end())
    return false;
  const auto& s = set->waves;
  *w = Wave();
  w->live      = true;
  w->life      = longWave ? s.lifeLong : s.lifeShort+rnd.sym()*s.lifeShortDisp;
  w->left      = w->life;
  w->frame     = rnd.unit();
  w->start     = at;
  w->heading   = heading;
  w->pos       = at;
  return true;
  }

bool ShoreWaves::place(Wave& w, const Water& water) const {
  // 0x51ed00: carried in to the sea's centre by its wave 2, at period2 x length2 m/s
  const float dx = w.start.x-sea.center.x, dz = w.start.z-sea.center.z;
  const float d  = std::sqrt(dx*dx+dz*dz);
  Vec3 p = w.start;
  if(d>=Eps) {
    const float nd = d-sea.period2*sea.length2*w.travelled;
    p = {sea.center.x+dx/d*nd,sea.center.y,sea.center.z+dz/d*nd};
    }
  // on the water (0x5259f0 -> 0x535130), else it is gone
  const auto y = water(p);
  if(!y)
    return false;
  p.y   = *y;
  w.pos = p;
  // the water's normal, from its heights around
  const float e = 0.25f;
  const auto  x0 = water(p-Vec3{e,0,0}), x1 = water(p+Vec3{e,0,0});
  const auto  z0 = water(p-Vec3{0,0,e}), z1 = water(p+Vec3{0,0,e});
  if(x0 && x1 && z0 && z1)
    w.normal = Vec3{*x0-*x1,2.f*e,*z0-*z1}.normalized();
  else
    w.normal = {0,1,0};
  return true;
  }

void ShoreWaves::update(const View& view, float time, float dt, const Water& water, Random& rnd) {
  if(set==nullptr)
    return;
  const auto& s = set->waves;

  // the waves (0x529dd0)
  for(auto& w:pool) {
    if(!w.live)
      continue;
    w.left -= dt;
    if(w.left<0) {
      w.live = false;
      continue;
      }
    w.travelled += dt;
    if(!place(w,water)) {
      w.live = false;
      continue;
      }
    w.side = Vec3{-w.heading.z,0,w.heading.x};
    w.side = w.side.length()>Eps ? w.side.normalized() : Vec3{1,0,0};
    // fading in until IncTime of the life, out from DecTime
    const float inc = s.incTime*w.life, dec = s.decTime*w.life, age = w.life-w.left;
    if(age<dec)
      w.fade = age<0 ? 0.f : age>inc ? 1.f : std::abs(inc)<Eps ? 0.f : age/inc;
    else
      w.fade = age>w.life || std::abs(w.life-dec)<Eps ? 0.f : 1.f-(age-dec)/(w.life-dec);
    // the texture moves along the crest in steps
    w.anim += dt;
    if(w.anim>s.animWait) {
      w.frame += s.animSpeed*s.animWait;
      w.anim  -= s.animWait;
      }
    }

  // the markers (0x525700): on the screen and within ActiveRad m of the camera
  for(auto& m:markers) {
    const float dx = m.pos.x-view.eye.x, dz = m.pos.z-view.eye.z;
    if(std::sqrt(dx*dx+dz*dz)>s.activeRad || !view.onScreen(m.pos)) {
      m.timeLong = m.timeShort = 0;
      continue;
      }
    const float ox = m.pos.x-sea.center.x, oz = m.pos.z-sea.center.z;
    const float d  = std::sqrt(ox*ox+oz*oz);
    m.timeLong -= dt;
    if(m.timeLong<0 && d>=Eps && sea.length2>Eps) {
      // a long wave on the crest of wave 2 next beyond the marker, turned by up to Angle (0x525990, 0x51eb40)
      const float k     = 1.f/sea.length2;
      const float wave  = 2.f*Pi/k;
      const float shift = (Pi*0.5f-sea.period2*time)/k;
      const float crest = float(int((d-shift)/wave+1.f))*wave+shift;
      const float a     = rnd.sym()*s.angle*Pi/180.f;
      const float cx = ox/d, cz = oz/d;
      const Vec3  dir   = {cx*std::cos(a)-cz*std::sin(a),0,cx*std::sin(a)+cz*std::cos(a)};
      if(spawn({sea.center.x+dir.x*crest,sea.center.y,sea.center.z+dir.z*crest},dir,true,rnd))
        m.timeLong = s.timeLong;
      }
    m.timeShort -= dt;
    if(m.timeShort<0) {
      // a short one anywhere within Len of the marker, heading away from the centre (0x5258a0)
      Vec3 at  = {m.pos.x+rnd.sym()*s.len,m.pos.y,m.pos.z+rnd.sym()*s.len};
      Vec3 dir = {at.x-sea.center.x,0,at.z-sea.center.z};
      dir = dir.length()>Eps ? dir.normalized() : Vec3{};
      if(spawn(at,dir,false,rnd))
        m.timeShort = s.timeShort;
      }
    }
  }

void ShoreWaves::collect(std::vector<Mark>& out, const Vec3& eye) const {
  if(set==nullptr)
    return;
  const auto& s = set->waves;
  for(auto& w:pool) {
    if(!w.live || w.fade<=0)
      continue;
    // 0x52a030: a quad on the crest, turned to the camera around it, standing up out of the water
    Vec3 t = eye-w.pos;
    t = t-w.side*Vec3::dot(t,w.side);
    Vec3 up = Vec3::cross(w.side,t);
    if(up.length()<Eps)
      continue;
    up = up.normalized();
    // seen from the sea, the other way round: up out of the water either way
    if(Vec3::dot(t,w.heading)<0)
      up = up*-1.f;
    const Vec3 sink = w.normal*(Vec3::dot(up,w.normal)*s.dHeight);
    const Vec3 a    = w.pos-w.side*s.halfLen+sink;
    const Vec3 b    = w.pos+w.side*s.halfLen+sink;
    const Vec3 top  = up*(s.height*w.fade);
    Mark m;
    m.kind = Mark::Wave;
    const Vec3  pos[4] = {a,b,b+top,a+top};
    const float u[4]   = {w.frame,w.frame+1.f,w.frame+1.f,w.frame};
    const float v[4]   = {1,1,0,0};
    const float u2[4]  = {0,1,1,0};
    for(int i=0; i<4; ++i) {
      m.v[i].pos    = pos[i];
      m.v[i].uv[0]  = u[i];
      m.v[i].uv[1]  = v[i];
      m.v[i].uv2[0] = u2[i];
      m.v[i].uv2[1] = v[i];
      m.v[i].rgba[3] = w.fade;
      }
    out.push_back(m);
    }
  }

// --- gunfire ---

Gunfire::Gunfire(const WorldSettings& settings)
  : set(&settings) {
  }

void Gunfire::clear() {
  timer = -1;
  shot  = -1;
  for(auto& g:grit) {
    g.drops.clear();
    g.emit = -1;
    }
  for(auto& d:dust) {
    d.puffs.clear();
    d.emit = -1;
    }
  smoke.puffs.clear();
  smoke.emit = -1;
  holes.clear();
  heard.clear();
  }

std::vector<Gunfire::Sound> Gunfire::sounds() {
  std::vector<Sound> r;
  r.swap(heard);
  return r;
  }

void Gunfire::puffs(Puffs& p, const WorldSettings::Puff& s, const Vec3& at, float dt, Random& rnd) {
  movePuffs(p.puffs,s,dt);
  if(p.emit<0)
    return;
  p.puffs.moveTo(at);
  const size_t n = p.puffs.count(s.intensity,dt);
  for(size_t i=0; i<n; ++i) {
    // 0x52c680: a puff rising at SpeedY, turning one way or the other
    Particle* q = p.puffs.spawn(at);
    if(q==nullptr)
      break;
    q->life   = s.life;
    q->left   = s.life;
    q->angle  = rnd.unit()*180.f;
    q->twist  = rnd.chance(50) ? -s.twist : s.twist;
    q->vel    = {0,s.speedY,0};
    q->scaleX = s.scaleX+rnd.sym()*s.scaleXDisp;
    q->scaleY = s.scaleY+rnd.sym()*s.scaleYDisp;
    }
  p.puffs.endFrame();
  p.emit -= dt;
  }

void Gunfire::update(const std::vector<Vec3>& guns, const Target* car, const CollisionMesh& track, float dt,
                     Random& rnd) {
  if(set==nullptr)
    return;
  const auto& ex = set->explode;

  // the grit thrown up (0x52cbc0): straight on, shrinking
  for(auto& g:grit) {
    for(auto& p:g.drops.particles()) {
      p.scaleX = p.scaleY = ramp(p.age(),ex.timeScale,ex.scale);
      p.pos    = p.pos+p.vel*dt;
      }
    g.drops.age(dt);
    if(g.emit<0)
      continue;
    g.drops.moveTo(g.at);
    const size_t n = g.drops.count(ex.intensity,dt);
    for(size_t i=0; i<n; ++i) {
      // 0x52cb40 after 0x4778c0: in a cone of AngleSpan around the vertical
      Particle* p = g.drops.spawn(g.at);
      if(p==nullptr)
        break;
      p->life  = ex.life+rnd.sym()*ex.lifeDisp;
      p->left  = p->life;
      p->angle = rnd.unit()*10.f;
      p->vel   = randomInCone({0,1,0},ex.angleSpan,rnd)*(ex.velLen+rnd.sym()*ex.velLenDisp);
      }
    g.drops.endFrame();
    g.emit -= dt;
    }
  for(size_t i=0; i<dust.size(); ++i)
    puffs(dust[i],set->bulletDust,dustAt[i],dt,rnd);
  // the smoke of a car that was hit comes from its body
  Vec3 body;
  if(car!=nullptr)
    body = car->pose.point((car->boxMin+car->boxMax)*0.5f);
  puffs(smoke,set->bulletSmoke,body,dt,rnd);
  if(car==nullptr)
    smoke.emit = -1;

  // the holes stay 8 s, fading out over the last 4 (0x4789f0)
  for(auto& h:holes)
    h.left -= dt;
  holes.erase(std::remove_if(holes.begin(),holes.end(),[](const Hole& h){ return h.left<0; }),holes.end());
  for(auto& h:holes)
    for(auto& v:h.quad.v)
      v.rgba[3] = std::min(1.f,h.left/4.f);

  // the burst (0x52da00). RCCars.exe starts it again on every message from a guard, which comes every frame
  // while the car is in the guard's guardVolume; here a new one follows when the last has ended
  const auto& b     = set->burst;
  const float total = float(b.shots-1)*b.timeShoot+Eps;
  if(timer<0) {
    shot = -1;
    if(guns.empty() || car==nullptr)
      return;
    // 0x52de30: the sweep goes across the car from +r to -r, r random; the gun sounds
    const Vec3 r = randomDir(rnd);
    gun    = guns.front();
    sweepA = r*b.radius;
    sweepB = r*-b.radius;
    timer  = total;
    heard.push_back({Sound::Burst,gun});
    }
  if(car==nullptr) {
    timer = -1;
    return;
    }
  // 0x52ddd0: the shot due now, and where along the sweep it goes
  const int idx = std::abs(b.timeShoot)>Eps ? int((total-timer)/b.timeShoot) : 0;
  if(idx!=shot) {
    shot = idx;
    shoot(*car,track,rnd);
    }
  timer -= dt;
  }

void Gunfire::shoot(const Target& car, const CollisionMesh& track, Random& rnd) {
  const auto& b = set->burst;
  const float t = float(shot)/float(b.shots);
  const Vec3  sweep = sweepA*(1.f-t)+sweepB*t;

  // 0x52dc50: at the car, a little ahead of it, plus the sweep; the ray reaches 1.5 times as far, at least 10 m
  const Vec3  fwd   = car.pose.vector({0,0,1}).normalized();
  const float speed = Vec3::dot(car.velocity,fwd);
  const Vec3  aim   = car.pose.translation()+fwd*(speed*b.offset*0.5f)+sweep;
  Vec3        dir   = aim-gun;
  const float len   = dir.length();
  if(len<Eps)
    return;
  dir = dir*(1.f/len);
  const float reach = std::max(10.f,1.5f*len);
  auto hit = track.raycast(gun,dir,reach);

  // the car's body in the way (0x508290): it smokes (0x508560 -> 0x52d0d0)
  bool hitCar = false;
  Vec3 at, normal{0,1,0};
  {
    const Mat4 inv  = car.pose.inverse();
    const Vec3 o    = inv.point(gun);
    const Vec3 d    = inv.vector(dir);
    float      t0   = 0, t1 = hit ? hit->distance : reach;
    int        axis = -1;
    float      sgn  = 0;
    const float lo[3] = {car.boxMin.x,car.boxMin.y,car.boxMin.z}, hi[3] = {car.boxMax.x,car.boxMax.y,car.boxMax.z};
    const float oo[3] = {o.x,o.y,o.z}, dd[3] = {d.x,d.y,d.z};
    bool inside = true;
    for(int i=0; i<3 && inside; ++i) {
      if(std::abs(dd[i])<Eps) {
        inside = oo[i]>=lo[i] && oo[i]<=hi[i];
        continue;
        }
      float a = (lo[i]-oo[i])/dd[i], c = (hi[i]-oo[i])/dd[i];
      if(a>c)
        std::swap(a,c);
      if(a>t0) {
        t0   = a;
        axis = i;
        sgn  = dd[i]>0 ? -1.f : 1.f;
        }
      t1 = std::min(t1,c);
      inside = t0<=t1;
      }
    if(inside && axis>=0) {
      hitCar = true;
      at     = gun+dir*t0;
      Vec3 n;
      (axis==0 ? n.x : axis==1 ? n.y : n.z) = sgn;
      normal = car.pose.vector(n).normalized();
      }
    }
  if(!hitCar) {
    if(!hit)
      return;
    at     = hit->point;
    normal = hit->normal;
    }

  // 0x52db60: grit and dust where it hit, a hole in the ground, the bullet's sound
  auto g = std::find_if(grit.begin(),grit.end(),[](const Grit& g){ return g.emit<0; });
  if(g==grit.end())
    g = grit.begin();
  g->at   = at;
  g->emit = set->explode.emitLife;
  g->drops.moveTo(at);
  for(size_t i=0; i<dust.size(); ++i)
    if(dust[i].emit<0 || i+1==dust.size()) {
      dust[i].emit = set->bulletDust.timeEmit;
      dustAt[i]    = at;
      break;
      }
  if(hitCar)
    smoke.emit = set->bulletSmoke.timeEmit;
  else if(holes.size()<256) {
    // bullet_footprint, 5 cm each way, lying on the ground
    Vec3 tx = Vec3::cross(normal,std::abs(normal.y)<0.9f ? Vec3{0,1,0} : Vec3{1,0,0}).normalized();
    const float a = rnd.unit()*2.f*Pi;
    const Vec3  ty0 = Vec3::cross(normal,tx);
    tx = tx*std::cos(a)+ty0*std::sin(a);
    const Vec3  ty = Vec3::cross(normal,tx);
    const float h  = 0.05f;
    const Vec3  c  = at+normal*0.01f;
    Hole hole;
    hole.left      = 8.f;
    hole.quad.kind = Mark::Decal;
    const Vec3  corner[4] = {c-tx*h-ty*h,c+tx*h-ty*h,c+tx*h+ty*h,c-tx*h+ty*h};
    const float u[4] = {0,1,1,0}, v[4] = {0,0,1,1};
    for(int i=0; i<4; ++i) {
      hole.quad.v[i].pos    = corner[i];
      hole.quad.v[i].uv[0]  = hole.quad.v[i].uv2[0] = u[i];
      hole.quad.v[i].uv[1]  = hole.quad.v[i].uv2[1] = v[i];
      }
    holes.push_back(hole);
    }
  heard.push_back({Sound::Bullet,at});
  }

void Gunfire::collect(std::vector<Billboard>& out) const {
  if(set==nullptr)
    return;
  for(auto& g:grit)
    collectDrops(g.drops,set->explode,out);
  for(auto& d:dust)
    collectPuffs(d.puffs,set->bulletDust,out);
  collectPuffs(smoke.puffs,set->bulletSmoke,out);
  }

void Gunfire::collect(std::vector<Mark>& out) const {
  for(auto& h:holes)
    out.push_back(h.quad);
  }

namespace {
// what 0x50fb91 hands the shine decal of a footprint: 30 s, in over 0.2 s, out over the last 10 s
constexpr float SoleLife    = 30.f;
constexpr float SoleFadeIn  = 0.2f;
constexpr float SoleFadeOut = 10.f;
constexpr size_t Decals     = 256;    // 0x478440
}

Footprints::Footprints(const WorldSettings& settings)
  :set(&settings) {
  }

void Footprints::update(const std::vector<Walker>& walkers, float dt) {
  // the decals age first (0x4789f0), a new one starts at 0
  for(auto& p:prints)
    p.age += dt;
  prints.erase(std::remove_if(prints.begin(),prints.end(),[](const Print& p){ return p.age>SoleLife; }),prints.end());
  if(set==nullptr)
    return;
  feet.resize(walkers.size());
  for(size_t i=0; i<walkers.size(); ++i) {
    // the timer runs down all the time; a step once it is out and the person walks (0x50f94c)
    Foot& f = feet[i];
    f.timer -= dt;
    if(f.timer>0 || !walkers[i].walking)
      continue;
    place(walkers[i].world,f.left);
    f.timer = set->sole.delay;
    f.left  = !f.left;
    }
  }

void Footprints::place(const Mat4& world, bool left) {
  if(prints.size()>=Decals)   // all the slots taken: no footprint (0x47846e)
    return;
  const auto& s = set->sole;
  const Vec3 up{0,1,0};
  // the feet, ShiftX along the model's X axis (as it is in the matrix, scale and all) to the right or the left, and
  // ShiftY up
  Vec3 at = world.translation()+world.vector({1,0,0})*(left ? -s.shiftX : s.shiftX)+up*s.shiftY;
  // the decal's "t": the model's Z axis laid flat and turned round (0x50fa5c); (1,0,0) if it points straight up
  Vec3 z = world.vector({0,0,1});
  z = z-up*Vec3::dot(z,up);
  const float len = z.length();
  const Vec3 t = len>Eps ? z*(-1.f/len) : Vec3{-1,0,0};
  const Vec3 b = Vec3::cross(up,t);
  // 0x478770: the corners at +-size along t and b; the texture's v runs along t (the toes, v = 0, where the person
  // faces), u against b; the left foot is the right one mirrored (0x50fbc0)
  const float h = s.size;
  const Vec3  corner[4] = {at+t*h+b*h, at-t*h+b*h, at-t*h-b*h, at+t*h-b*h};
  const float u[4] = {0,0,1,1}, v[4] = {1,0,0,1};
  Print p;
  p.quad.kind = Mark::Sole;
  for(int i=0; i<4; ++i) {
    auto& c = p.quad.v[i];
    c.pos    = corner[i];
    c.uv[0]  = c.uv2[0] = left ? 1.f-u[i] : u[i];
    c.uv[1]  = c.uv2[1] = v[i];
    }
  prints.push_back(p);
  }

void Footprints::collect(std::vector<Mark>& out) const {
  for(auto& p:prints) {
    // the decal's opacity (0x4789f0): up over the first 0.2 s, down over the last 10 s, as a byte
    float a = 1;
    if(p.age<SoleFadeIn)
      a = p.age/SoleFadeIn;
    else if(p.age>SoleLife-SoleFadeOut)
      a = 1.f-(p.age-(SoleLife-SoleFadeOut))/SoleFadeOut;
    const float f = std::floor(std::clamp(a,0.f,1.f)*255.f)/255.f;
    // 0x50fbc0: the grey (1 - opacity) x 128 with the opacity, which the tracks' shader and blend turn into the
    // texture where the footprint is new and the ground as it is where it has faded
    const float g = std::floor((1.f-f)*128.f)/255.f;
    Mark m = p.quad;
    for(auto& c:m.v) {
      c.rgba[0] = c.rgba[1] = c.rgba[2] = g;
      c.rgba[3] = f;
      }
    out.push_back(m);
    }
  }

void Footprints::clear() {
  prints.clear();
  feet.clear();
  }
