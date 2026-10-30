#include "props.h"

#include <algorithm>
#include <cmath>

#include "cargeometry.h"
#include "formats/pack.h"
#include "formats/settings.h"
#include "vehicle.h"

using namespace rc;

namespace {
constexpr float Pi = 3.14159265f;

SliderFile sliders(PackFile& pack, const std::string& name) {
  if(pack.find(name)==nullptr)
    return {};
  auto d = pack.read(name);
  return SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
  }

// RCCars.exe's sliders of the props (0x4fa2e0 ..): position 1..100 onto lo..hi, below 1 lo, above 100 hi
float slider(const SliderFile& f, std::string_view key, float lo, float hi) {
  auto s = f.find(key);
  if(s==nullptr || s->value<1)
    return lo;
  if(s->value>100)
    return hi;
  return lo+float(s->value-1)*(hi-lo)/99.f;
  }
}

PropShapes PropShapes::load(PackFile& pack) {
  PropShapes p;
  constexpr float RLo = 0.01f, RHi = 0.1f;
  // three spheres of one radius up the model's Y axis
  auto column = [&](int32_t shape, const char* file, const char* const keys[3], float lo, float hi) {
    const auto f = sliders(pack,std::string("Settings\\")+file);
    const float r = slider(f,"radius",RLo,RHi);
    for(int i=0; i<3; ++i)
      p.pieces[size_t(shape)].push_back({{0,keys[i]==nullptr ? 0.f : slider(f,keys[i],lo,hi),0},r});
    };
  static const char* const cylinder[3] = {nullptr,"shift1","shift2"};
  static const char* const shifts[3]   = {"shift0","shift1","shift2"};
  column(PropParams::Cylinder,"CYLINDER.ini",cylinder,-0.1f,0.1f);   // 0x4fa760
  column(PropParams::Bottle,  "BOTTLE.ini",  shifts,  -0.2f,0.4f);   // 0x4fa2e0
  column(PropParams::Shell,   "SHELL.ini",   shifts,  -0.3f,0.3f);   // 0x4fb050
  column(PropParams::Balloon, "BALLON.ini",  shifts,  -0.2f,0.2f);   // 0x4fa9c0

  // 0x4facb0: four spheres, two pairs across X at two heights
  {
    const auto  f  = sliders(pack,"Settings\\kettle.ini");
    const float r  = slider(f,"radius",RLo,RHi);
    const float y1 = slider(f,"shiftY1",-0.3f,0.3f), y2 = slider(f,"shiftY2",-0.3f,0.3f);
    const float x  = slider(f,"shiftX1",-0.3f,0.3f), z  = slider(f,"shiftZ1",-0.3f,0.3f);
    p.pieces[PropParams::Kettle] = {{{x,y1,z},r},{{-x,y1,z},r},{{x,y2,z},r},{{-x,y2,z},r}};
  }
  // 0x4fa5f0: four small spheres around the bottom, one in the middle, one at the top (FValues)
  {
    const auto  f  = sliders(pack,"Settings\\BUCKET.ini");
    const float rs = f.real("radiusSmall",0.04f), dy = f.real("dySmall",0.05f);
    const float dx = f.real("dxSmall",0.07f), dz = f.real("dzSmall",0.07f);
    p.pieces[PropParams::Bucket] = {{{dx,dy,dz},rs},{{dx,dy,-dz},rs},{{-dx,dy,dz},rs},{{-dx,dy,-dz},rs},
                                    {{0,f.real("dyMiddle",0.14f),0},f.real("radiusMiddle",0.13f)},
                                    {{0,f.real("dyUp",0.2f),0},f.real("radiusUp",0.12f)}};
  }
  // 0x50e190: the cone's own, a big sphere at the bottom, a small one at the top and four around the base
  {
    const float b = -0.0571f, s = 0.104f, r = 0.04018f;
    p.pieces[PropParams::Cone] = {{{0,0.0275f,0},0.123f},{{0,0.2228f,0},0.0517f},
                                  {{0,b,s},r},{{0,b,-s},r},{{s,b,0},r},{{-s,b,0},r}};
  }
  // 0x4f7760
  const auto mi = sliders(pack,"Settings\\MAX_IMPULSE.ini");
  p.maxImpLinear = mi.real("maxImpLinear",p.maxImpLinear);
  p.maxImpAng    = mi.real("maxImpAng",p.maxImpAng);
  return p;
  }

PropParams PropParams::parse(std::string_view script) {
  const auto keys = KeySections::parse(script);
  PropParams p;
  auto get = [&](std::string_view section, std::string_view key) -> const float* {
    auto& v = keys.get(section,key);
    return v.empty() ? nullptr : &v[0];
    };
  // 0x50c7b0: any other type is a sphere
  if(auto t = get("Geom","type"))
    p.shape = int32_t(*t);
  if(p.shape<Sphere || p.shape>Balloon)
    p.shape = Sphere;
  // 0x50d0d0: the shape's own values ..
  switch(p.shape) {
    case Sphere:   p.comY = 0;     p.mass = 0.5f; p.bounce = 0.7f; break;
    case Cylinder:
    case Balloon:  p.comY = 0;     p.mass = 0.1f; p.bounce = 0.3f; break;
    case Cone:     p.comY = 0.03f; p.mass = 1.f;  p.bounce = 0.3f; break;
    case Bucket:   p.comY = 0.1f;  p.mass = 0.3f; p.bounce = 0.3f; break;
    default:       p.comY = 0.02f; p.mass = 0.4f; p.bounce = 0.3f; break;
    }
  p.friction = 1.f;
  // .. unless the script has them
  if(auto v = get("Geom","shiftCenterMassOY"))
    p.comY = *v;
  if(auto v = get("Params","mass"))
    p.mass = std::max(*v,0.001f);
  if(auto v = get("Params","coeffHookNorm"))
    p.bounce = *v;
  if(auto v = get("Params","coeffFrict"))
    p.friction = *v;
  if(auto v = get("ExtForces","fWindBlow"))
    p.wind = *v!=0;
  return p;
  }

PropGeometry rc::propGeometry(const SbScene& sb, int32_t model, const Vec3& origin) {
  PropGeometry g;
  g.min = { 1e9f, 1e9f, 1e9f};
  g.max = {-1e9f,-1e9f,-1e9f};
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=model || m.positions.empty() || !carPart(sb,mi))
      continue;
    std::vector<bool> used(m.positions.size(),false);
    for(auto& f:m.faces) {
      if(f.pos.size()<3 || f.layer(LayerTag::Base)==nullptr)
        continue;
      for(auto pi:f.pos)
        if(pi<used.size())
          used[pi] = true;
      }
    for(size_t i=0; i<used.size(); ++i) {
      if(!used[i])
        continue;
      const Vec3 p = m.world.point(m.positions[i])-origin;
      g.points.push_back(p);
      g.min = {std::min(g.min.x,p.x),std::min(g.min.y,p.y),std::min(g.min.z,p.z)};
      g.max = {std::max(g.max.x,p.x),std::max(g.max.y,p.y),std::max(g.max.z,p.z)};
      }
    }
  return g;
  }

Prop::Prop(PhysicsWorld& world, const PropShapes& shapes, const PropGeometry& g, const PropParams& prm,
           const Mat4& placed)
  : owner(world), maxP(shapes.maxImpLinear), maxL(shapes.maxImpAng) {
  // the placement may carry a scale (crabs are placed at 2x); the body only takes rotation + translation
  Mat4 start = placed;
  for(int c=0; c<3; ++c) {
    const Vec3 axis = Vec3{start.m[0][c],start.m[1][c],start.m[2][c]}.normalized();
    start.m[0][c] = axis.x; start.m[1][c] = axis.y; start.m[2][c] = axis.z;
    }
  mass     = prm.mass;
  friction = prm.friction;
  bounce   = prm.bounce;
  wind     = prm.wind;
  com      = {0,prm.comY,0};

  const Vec3 size = g.empty() ? Vec3{0.1f,0.1f,0.1f} : g.max-g.min;
  if(prm.shape==PropParams::Sphere) {
    // 0x50d0d0: 0.9 of the radius of a sphere as big as the bounding box's diagonal over the root of 3
    pieces = {{{},size.length()*0.5f/std::sqrt(3.f)*0.9f}};
    }
  else {
    pieces = shapes.pieces[size_t(prm.shape)];
    }

  // 0x50cd10: the inertia of a solid of the mass, r half the box's diagonal across X and Z over the root of 2,
  // h its height
  const float r = std::sqrt(size.x*size.x+size.z*size.z)*0.5f/std::sqrt(2.f), h = std::abs(size.y);
  if(prm.shape==PropParams::Sphere)
    inertiaBody = Vec3{1,1,1}*(0.4f*mass*r*r);                            // 0x475220
  else if(prm.shape==PropParams::Cone)
    inertiaBody = {(0.15f*r*r+0.1f*h*h)*mass,(0.15f*r*r+0.1f*h*h)*mass,0.3f*mass*r*r};  // 0x475410
  else
    inertiaBody = {(3.f*r*r+h*h)*mass/12.f,0.5f*mass*r*r,(3.f*r*r+h*h)*mass/12.f};    // 0x475360
  for(float* v:{&inertiaBody.x,&inertiaBody.y,&inertiaBody.z})
    *v = std::max(*v,1e-9f);

  Body b;
  b.x = start.point(com);
  toQuat(start,b.q);
  set(b);

  // 0x50cea0: down onto the track, each sphere from 1 cm above its centre straight down (20 m); none may find the
  // ground steeper than 80 degrees. The lowest the body may be put is where every sphere is 1 mm off the ground
  // along the slope under it.
  balls(ballBuf[0]);
  float lift = -2e6f;
  bool  ok   = !ballBuf[0].empty();
  for(auto& s:ballBuf[0]) {
    const auto hit = owner.track().raycast(s.c+Vec3{0,0.01f,0},{0,-1,0},20.f);
    if(!hit) {
      ok = false;
      break;
      }
    const float a = std::acos(std::clamp(std::abs(hit->normal.y),0.f,1.f));
    if(a>80.f*Pi/180.f) {
      ok = false;
      break;
      }
    lift = std::max(lift,(0.01f-hit->distance)+(s.r+0.001f)/std::cos(a));
    }
  if(ok) {
    b.x.y += lift;
    set(b);
    // standing within 10 degrees of upright: asleep from the start (0x50f130)
    if(axis(1).y>std::cos(10.f*Pi/180.f))
      stillTime = calmTime = touchTime = 2e6f;
    }
  owner.propList.push_back(this);
  }

Prop::~Prop() {
  owner.propList.erase(std::remove(owner.propList.begin(),owner.propList.end(),this),owner.propList.end());
  }

void Prop::set(const Body& b) {
  st = b;
  const float l = std::sqrt(st.q[0]*st.q[0]+st.q[1]*st.q[1]+st.q[2]*st.q[2]+st.q[3]*st.q[3]);
  if(l>=1e-6f)
    for(float& v:st.q)
      v /= l;
  rot   = fromQuat(st.q);
  vel   = st.P*(1.f/mass);
  omega = invInertia(st.L);
  }

Vec3 Prop::invInertia(const Vec3& v) const {
  const Vec3 l = {Vec3::dot(axis(0),v)/inertiaBody.x,Vec3::dot(axis(1),v)/inertiaBody.y,
                  Vec3::dot(axis(2),v)/inertiaBody.z};
  return axis(0)*l.x+axis(1)*l.y+axis(2)*l.z;
  }

void Prop::applyImpulse(const Vec3& j, const Vec3& at) {
  st.P = st.P+j;
  st.L = st.L+Vec3::cross(at-st.x,j);
  set(st);
  }

Mat4 Prop::pose() const {
  Mat4 m = rot*Mat4::translate(com*-1.f);
  m.m[0][3] += st.x.x; m.m[1][3] += st.x.y; m.m[2][3] += st.x.z;
  return m;
  }

void Prop::balls(std::vector<Ball>& out) const {
  out.clear();
  const Mat4 m = pose();
  for(auto& p:pieces)
    out.push_back({m.point(p.at),p.r});
  }

void Prop::forces(float h, Vec3& force, Vec3& torque) const {
  // 0x50d670: gravity and the air at the centre of mass (no wind: that is a network game's)
  force = {0,-PhysicsWorld::Gravity*mass,0};
  if(const float v = vel.length(); v>0.01f)
    force = force+vel*((-0.1f*h-0.003f*v*v)/v);
  // on the track (within 1 cm): the spin brought towards none at 100 a second (0x49d820)
  torque = {};
  auto& sp = const_cast<std::vector<Ball>&>(ballBuf[1]);
  balls(sp);
  PhysicsWorld::TrackTouch t;
  for(auto& s:sp)
    if(owner.touchTrack(s.c,s.r+0.01f,&t,1)>0) {
      auto toward = [&](float a) {
        const float k = 100.f*h;
        return std::abs(a)<k ? -a : (a<0 ? k : -k);
        };
      torque = {toward(st.L.x),toward(st.L.y),toward(st.L.z)};
      break;
      }
  }

void Prop::advance(const Body& from, float h) {
  // 0x50dba0: the derivative (0x50da80) at the start, again halfway along it, and the step along that one
  auto derivative = [&](Body& d) {
    Vec3 f, tq;
    forces(h,f,tq);
    d.x = vel;
    const Vec3& w = omega;
    const float* q = st.q;
    d.q[0] = 0.5f*(-w.x*q[1]-w.y*q[2]-w.z*q[3]);
    d.q[1] = 0.5f*( w.x*q[0]+w.y*q[3]-w.z*q[2]);
    d.q[2] = 0.5f*( w.y*q[0]+w.z*q[1]-w.x*q[3]);
    d.q[3] = 0.5f*( w.z*q[0]+w.x*q[2]-w.y*q[1]);
    d.P = f;
    d.L = tq;
    };
  auto along = [](const Body& b, const Body& d, float t) {
    Body r = b;
    r.x = b.x+d.x*t;
    r.P = b.P+d.P*t;
    r.L = b.L+d.L*t;
    for(int i=0; i<4; ++i)
      r.q[i] = b.q[i]+d.q[i]*t;
    return r;
    };
  set(from);
  Body d;
  derivative(d);
  set(along(from,d,0.5f*h));
  derivative(d);
  set(along(from,d,h));
  }

void Prop::touches(float margin, std::vector<Touch>& out) const {
  constexpr size_t MaxTouches = 50;
  out.clear();
  auto& sp = const_cast<std::vector<Ball>&>(ballBuf[1]);
  balls(sp);
  PhysicsWorld::TrackTouch t[5];
  for(auto& s:sp) {
    const int n = owner.touchTrack(s.c,s.r+margin,t,5);
    for(int i=0; i<n && out.size()<MaxTouches; ++i)
      out.push_back({s.c-t[i].normal*s.r,t[i].normal});
    }
  }

float Prop::sweep(const Body& from, float rest) {
  // 0x50e950: the whole way, unless a sphere's centre goes through the track on it
  auto& a = ballBuf[0];
  set(from);
  balls(a);
  advance(from,rest);
  std::vector<Ball> b;
  balls(b);
  bool through = false;
  for(size_t i=0; i<a.size() && !through; ++i)
    through = owner.crossesTrack(a[i].c,b[i].c);
  if(!through)
    return rest;
  // then no further than a point of a sphere goes 0.9 of its radius at its velocity, halved until no sphere has
  // moved more than 0.99 of it; nothing after 10 halvings
  set(from);
  float h = 2e6f;
  for(auto& s:a) {
    const float v = pointVelocity(s.c).length();
    h = std::min(h,v>0.001f ? 0.9f*s.r/v : 1.f);
    }
  h = std::min(h,rest);
  for(int i=0; i<10; ++i) {
    advance(from,h);
    balls(b);
    bool far = false;
    for(size_t k=0; k<a.size() && !far; ++k)
      far = (b[k].c-a[k].c).length()>0.99f*a[k].r;
    if(!far)
      return h;
    h *= 0.5f;
    }
  set(from);
  return 0;
  }

float Prop::impact(const Body& from, float hi, std::vector<Touch>& out) {
  // 0x50e670: 0.2 of the way from free to stuck the first time, halfway after, until a sphere is within 1 mm of
  // the track there (5 tries); then the touches within 1 mm, or failing that 1 cm, where it was last free
  float lo = 0, w = 0.8f;
  bool  found = false;
  for(int i=0; i<5 && !found; ++i) {
    const float t = lo*w+(1.f-w)*hi;
    advance(from,t);
    touches(0,out);
    if(!out.empty()) {
      hi = t;
      w  = 0.8f;
      continue;
      }
    lo = t;
    w  = 0.5f;
    touches(0.001f,out);
    found = !out.empty();
    }
  advance(from,lo);
  if(!found)
    touches(0.01f,out);
  return lo;
  }

void Prop::resolve(const std::vector<Touch>& at, float h) {
  for(int pass=0; pass<5; ++pass) {
    bool again = false;
    for(auto& t:at)
      if(Vec3::dot(pointVelocity(t.point),t.normal)<=0.02f) {
        again = true;
        collide(t,h);
        }
    if(!again)
      break;
    }
  }

namespace {
// RCCars.exe 0x475c80: the hit of a body with the (still) track at a point, worked out over the impulse as
// Mirtich does, with Coulomb friction and Poisson's restitution: in the touch's own axes (the normal last), from the
// velocity of the point (u) and the matrix K that turns an impulse there into its change (u = K p).
struct Collision {
  float u[3];         // the point's velocity, tangent, tangent, normal (0x18)
  float mu = 0, e = 0;
  float wc = 0, wr = 0;   // work along the normal while it presses in, and while it springs back (0x10, 0x14)
  bool  pressing = true;  // 0x04
  float K[3][3], Kinv[3][3];

  float row(int i, const float d[3]) const { return K[i][0]*d[0]+K[i][1]*d[1]+K[i][2]*d[2]; }
  // the sliding stays stuck once it stops (0x475c30)
  bool  stable() const { return Kinv[0][2]*Kinv[0][2]+Kinv[1][2]*Kinv[1][2]<Kinv[2][2]*Kinv[2][2]*mu*mu; }
  // the way the friction pushes while it slides (0x476840): against the sliding, mu of the normal
  bool  slideDir(float d[3]) const {
    const float l = std::sqrt(u[0]*u[0]+u[1]*u[1]);
    if(l<1e-4f)
      return false;
    d[0] = -mu*u[0]/l;
    d[1] = -mu*u[1]/l;
    d[2] = 1.f;
    return true;
    }
  // 0x475a40: the one way it can start to slide when stopping does not hold (sticking is not stable): the angle
  // whose push changes the sliding along itself; 0 if there is not exactly one
  float slideAngle(float* sn = nullptr, float* cs = nullptr) const {
    auto g = [&](float a) {
      const float c = std::cos(a), s = std::sin(a);
      return mu*(K[1][1]-K[0][0])*s*c+mu*K[0][1]*(c*c-s*s)+K[0][2]*s-K[1][2]*c;
      };
    auto good = [&](float a) {
      const float c = std::cos(a), s = std::sin(a);
      return c*K[0][2]+s*K[1][2]-mu*(c*c*K[0][0]+s*s*K[1][1]+2.f*s*c*K[0][1])>0;
      };
    constexpr int N = 720;
    int   found = 0;
    float angle = 0;
    float a0 = -Pi, g0 = g(a0);
    for(int i=1; i<=N; ++i) {
      const float a1 = -Pi+2.f*Pi*float(i)/float(N), g1 = g(a1);
      if((g0<=0)!=(g1<=0)) {
        float lo = a0, hi = a1, glo = g0;
        for(int k=0; k<30; ++k) {
          const float m = 0.5f*(lo+hi), gm = g(m);
          if((gm<=0)==(glo<=0)) { lo = m; glo = gm; } else hi = m;
          }
        const float a = 0.5f*(lo+hi);
        if(good(a)) {
          ++found;
          angle = a;
          }
        }
      a0 = a1;
      g0 = g1;
      }
    if(found!=1)
      return 0;
    if(sn!=nullptr) *sn = std::sin(angle);
    if(cs!=nullptr) *cs = std::cos(angle);
    return angle;
    }
  // the sliding has stopped (0x4768d0): it stays stopped (0x476900), or slides on the one way it can (0x476970);
  // either way the rest of the bounce follows at once
  void stick() {
    if(stable()) {
      u[0] = u[1] = 0;
      if(pressing) {
        wc += -(u[2]*u[2])*Kinv[2][2]*0.5f;
        u[2] = 0;
        }
      const float v = u[2]*u[2]+2.f*(-(e*wc)-wr)/Kinv[2][2];
      u[2] = std::sqrt(std::max(v,0.f));
      return;
      }
    const float a = slideAngle();
    const float d[3] = {-mu*std::cos(a),-mu*std::sin(a),1.f};
    const float kz = row(2,d);
    if(pressing) {
      if(std::abs(kz)>1e-6f) {
        u[0] += -u[2]*(row(0,d)/kz);
        u[1] += -u[2]*(row(1,d)/kz);
        wc   += -(u[2]*u[2])/(2.f*kz);
        }
      u[2] = 0;
      }
    const float was = u[2];
    const float v   = u[2]*u[2]+2.f*(-(e*wc)-wr)*kz;
    if(v>0)
      u[2] = std::sqrt(v);
    if(std::abs(kz)>1e-6f) {
      u[0] += (u[2]-was)*(row(0,d)/kz);
      u[1] += (u[2]-was)*(row(1,d)/kz);
      }
    }
  // one step of the sliding along the normal velocity by `du` (0x475d37 ..); false when it stops sliding
  bool slideBy(float du, bool& stuck) {
    float d[3];
    if(!slideDir(d)) { stuck = true; return false; }
    const float kz = row(2,d);
    if(std::abs(kz)<1e-4f) { stuck = true; return false; }
    const float ux = u[0]+row(0,d)*du/kz, uy = u[1]+row(1,d)*du/kz;
    wc   += u[2]*du/kz;
    u[2] += du;
    if(u[0]*ux<0 && u[1]*uy<0) {
      u[0] = u[1] = 0;
      stuck = true;
      return false;
      }
    u[0] = u[0]*ux<0 ? 0.f : ux;
    u[1] = u[1]*uy<0 ? 0.f : uy;
    return true;
    }
  };

// a matrix row by row and its inverse (0x40b1f0)
bool invert3(const float m[3][3], float out[3][3]) {
  const float det = m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                  + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
  if(std::abs(det)<1e-12f)
    return false;
  const float k = 1.f/det;
  out[0][0] =  (m[1][1]*m[2][2]-m[1][2]*m[2][1])*k;
  out[0][1] = -(m[0][1]*m[2][2]-m[0][2]*m[2][1])*k;
  out[0][2] =  (m[0][1]*m[1][2]-m[0][2]*m[1][1])*k;
  out[1][0] = -(m[1][0]*m[2][2]-m[1][2]*m[2][0])*k;
  out[1][1] =  (m[0][0]*m[2][2]-m[0][2]*m[2][0])*k;
  out[1][2] = -(m[0][0]*m[1][2]-m[0][2]*m[1][0])*k;
  out[2][0] =  (m[1][0]*m[2][1]-m[1][1]*m[2][0])*k;
  out[2][1] = -(m[0][0]*m[2][1]-m[0][1]*m[2][0])*k;
  out[2][2] =  (m[0][0]*m[1][1]-m[0][1]*m[1][0])*k;
  return true;
  }
}

void Prop::collide(const Touch& t, float h) {
  constexpr float MinSpeed = 0.05f;   // m/s the point leaves the track at, at least (0x50e810 -> 0x475c80)
  // the touch's axes (0x408760): two along the track, the normal
  const Vec3 n  = t.normal;
  const Vec3 a  = std::abs(n.y)<0.9f ? Vec3{0,1,0} : Vec3{1,0,0};
  const Vec3 t1 = Vec3::cross(a,n).normalized();
  const Vec3 t2 = Vec3::cross(n,t1);
  const Vec3 ax[3] = {t1,t2,n};

  Collision c;
  c.mu = friction;
  c.e  = bounce;
  const Vec3 r = t.point-st.x;
  const Vec3 v = pointVelocity(t.point);
  for(int i=0; i<3; ++i)
    c.u[i] = Vec3::dot(v,ax[i]);
  // 0x476420: K = 1/m - [r]x I^-1 [r]x, in the touch's axes
  for(int j=0; j<3; ++j) {
    const Vec3 dv = ax[j]*(1.f/mass)+Vec3::cross(invInertia(Vec3::cross(r,ax[j])),r);
    for(int i=0; i<3; ++i)
      c.K[i][j] = Vec3::dot(ax[i],dv);
    }
  if(!invert3(c.K,c.Kinv))
    return;
  const float u0[3] = {c.u[0],c.u[1],c.u[2]};

  bool done  = false;
  bool stuck = false;
  if(std::abs(c.u[2])<=MinSpeed) {
    // 0x4758b0: barely moving into it: off it at the bounce of the speed, at least MinSpeed, and the sliding slowed
    // by mu g h (stopped if it is less)
    c.u[2] = std::max(-(c.e*c.u[2]),MinSpeed);
    const float slow = h*c.mu*10.f;
    const float l    = std::sqrt(c.u[0]*c.u[0]+c.u[1]*c.u[1]);
    if(l<slow) {
      if(c.stable()) {
        c.u[0] = c.u[1] = 0;
        }
      else {
        float sn = slow, cs = MinSpeed;
        c.slideAngle(&sn,&cs);
        c.u[0] = cs*0.01f;
        c.u[1] = sn*0.01f;
        }
      }
    else {
      c.u[0] -= slow*c.u[0]/l;
      c.u[1] -= slow*c.u[1]/l;
      }
    done = true;
    }
  if(!done) {
    // pressing in, in 1000 steps of the normal velocity up to 0
    const float du = std::abs(c.u[2])*0.001f;
    for(int i=0; i<1000 && !stuck; ++i)
      c.slideBy(du,stuck);
    if(stuck) {
      c.stick();
      done = true;
      }
    else {
      // springing back: the normal velocity the work of the bounce gives, step by step while it barely moves ..
      c.pressing = false;
      float target = -(c.wc*c.e)*(c.K[2][2]-std::sqrt(c.K[2][0]*c.K[2][0]+c.K[2][1]*c.K[2][1])*c.mu)*2.f;
      target = std::sqrt(std::max(target,0.f));
      if(target<du) {
        done = true;
        }
      else {
        const int n2 = int(target/du+0.5f);
        for(int i=0; i<n2 && !done; ++i) {
          float d[3];
          if(!c.slideDir(d)) { stuck = true; break; }
          const float kz = c.row(2,d);
          if(std::abs(kz)<1e-4f) { stuck = true; break; }
          const float ux = c.u[0]+c.row(0,d)*du/kz, uy = c.u[1]+c.row(1,d)*du/kz;
          c.u[2] += du;
          if(c.u[2]>0.0005f) {
            c.u[0] = ux;
            c.u[1] = uy;
            break;
            }
          if(target<c.u[2]) { done = true; break; }
          if(c.u[0]*ux<0 && c.u[1]*uy<0) { c.u[0] = c.u[1] = 0; stuck = true; break; }
          c.u[0] = c.u[0]*ux<0 ? 0.f : ux;
          c.u[1] = c.u[1]*uy<0 ? 0.f : uy;
          }
        if(stuck) {
          c.stick();
          done = true;
          }
        }
      // .. then by the work left, in 1000 steps
      if(!done && -(c.wc*c.e)>=0) {
        const float dw = -(c.wc*c.e)*0.001f;
        for(int i=0; i<1000; ++i) {
          float d[3];
          if(!c.slideDir(d) || std::abs(c.u[2])<1e-4f) { stuck = true; break; }
          const float ux = c.u[0]+c.row(0,d)*dw/c.u[2], uy = c.u[1]+c.row(1,d)*dw/c.u[2];
          c.u[2] += c.row(2,d)*dw/c.u[2];
          c.wr   += dw;
          if(c.u[0]*ux<0 && c.u[1]*uy<0) { c.u[0] = c.u[1] = 0; stuck = true; break; }
          c.u[0] = c.u[0]*ux<0 ? 0.f : ux;
          c.u[1] = c.u[1]*uy<0 ? 0.f : uy;
          }
        if(stuck)
          c.stick();
        }
      }
    }
  // the impulse that makes the velocity change so (p = K^-1 du), leaving at MinSpeed at least
  c.u[2] = std::max(c.u[2],MinSpeed);
  float du[3];
  for(int i=0; i<3; ++i)
    du[i] = c.u[i]-u0[i];
  Vec3 p;
  for(int i=0; i<3; ++i)
    p = p+ax[i]*(c.Kinv[i][0]*du[0]+c.Kinv[i][1]*du[1]+c.Kinv[i][2]*du[2]);
  applyImpulse(p,t.point);
  }

bool Prop::asleep(float dt) {
  // 0x50efa0: slower than 2 km/h (the faster of its velocity and its momentum), turning slower than 1 rad/s, and
  // touching the track (in the air for 0.1 s the timer starts over)
  stillTime   = std::max(vel.length(),st.P.length())<0.5555556f ? stillTime+dt : 0.f;
  calmTime    = omega.length()<1.f ? calmTime+dt : 0.f;
  sinceTouch += std::min(dt,0.095f);
  touchTime   = sinceTouch>=0.1f ? 0.f : touchTime+dt;
  // 0x50f350: all of it for 2 s, touching for 1 s
  if(stillTime>2.f && calmTime>2.f && touchTime>1.f) {
    st.P = st.L = {};
    set(st);
    return true;
    }
  return false;
  }

void Prop::step(float dt, bool near) {
  // 0x50f190: only near a player's car, and awake (a prop in the wind is never put to sleep)
  if(!near)
    return;
  if(!wind && asleep(dt)) {
    sinceTouch = 0;
    return;
    }
  float rest = dt;
  for(int part=0; part<4 && rest>1e-4f; ++part) {
    // 0x50ed70
    if(const float p = st.P.length(); p>maxP*mass)
      st.P = st.P*(maxP*mass/p);
    if(const float l = st.L.length(); l>maxL)
      st.L = st.L*(maxL/l);
    set(st);
    const Body from = st;
    float h = std::clamp(sweep(from,rest),0.f,rest);
    touches(0,touchBuf);
    if(!touchBuf.empty()) {
      sinceTouch = 0;
      h = impact(from,h,touchBuf);
      resolve(touchBuf,h);
      }
    rest -= h;
    }
  }
