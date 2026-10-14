#include "tracks.h"

#include <algorithm>
#include <cmath>

#include "formats/collision.h"

using namespace rc;
using namespace rc::fx;

namespace {

// 0x5738c8: the width of each of the four tyres (0x50bde0), times 5 cm on either side of the middle
constexpr float TyreWidth[4] = {0.95f, 1.07f, 1.25f, 1.35f};

// 0x52fb60: how a new point C goes on from the strip's last two, A and B
enum class Bend { Straight, Bent, Ahead, Behind, TooLong, Same };
Bend bend(const Vec3& a, const Vec3& b, const Vec3& c, const TrackSettings& s) {
  const Vec3  ab  = b-a, ac = c-a;
  const float ab2 = Vec3::dot(ab,ab);
  if(ab2<1e-6f)
    return Bend::Straight;
  const float ac2 = Vec3::dot(ac,ac);
  if(std::abs(ab2-ac2)<0.01f || ac2<1e-6f)
    return Bend::Same;
  if(s.maxLen*s.maxLen<ac2)
    return Bend::TooLong;
  const float d = Vec3::dot(ac,ab);
  if(d<ab2)
    return Bend::Behind;
  const Vec3 off = ab-ac*(d/ac2);
  if(s.maxHeight*s.maxHeight<Vec3::dot(off,off))
    return Bend::Bent;
  return Bend::Straight;
  }

}

int TyreTracks::Ring::next(int i) const {
  // 0x530050: the point after i, -1 past the newest
  const int cap = int(pts.size());
  const int j   = (i+1)%cap;
  if(tail<head)
    return (tail<j && j<=head) ? j : -1;
  if(head<tail)
    return (j<=head || tail<j) ? j : -1;
  return -1;
  }

int TyreTracks::Ring::prev(int i) const {
  // 0x530090: the point before i, -1 before the oldest
  const int cap = int(pts.size());
  const int j   = (i==0 ? cap : i)-1;
  if(tail<head)
    return (tail<=j && j<head) ? j : -1;
  if(head<tail)
    return (j<head || tail<=j) ? j : -1;
  return -1;
  }

TyreTracks::TyreTracks(const TrackSettings& settings, bool player)
  : set(settings), player(player) {
  for(size_t i=0; i<rings.size(); ++i) {
    size_t cap = i<2 ? 32 : 48;
    if(!player)
      cap /= 2;
    rings[i].pts.resize(cap);
    }
  }

void TyreTracks::clear() {
  for(auto& r:rings) {
    r.tail   = 0;
    r.head   = -1;
    r.linked = false;
    }
  }

void TyreTracks::cut() {
  for(auto& r:rings)
    r.linked = false;
  }

void TyreTracks::write(Ring& r, int at, const Input& in, int linkFrom) const {
  // 0x52f990
  Point& p = r.pts[size_t(at)];
  p.end       = true;
  p.broken    = false;
  p.pos       = in.pos;
  p.intensity = in.intensity;
  p.normal    = in.normal;
  p.side      = in.side;
  p.life      = set.timeLife;
  p.dist      = linkFrom<0 ? 0.f : (in.pos-r.pts[size_t(linkFrom)].pos).length()+r.pts[size_t(linkFrom)].dist;
  p.forward   = in.forward;
  p.tyre      = in.tyre;
  p.v         = set.scaleCoeff*p.dist;
  if(p.v>1000.f)
    p.v -= 1000.f;
  p.left      = in.pos+in.side*( 0.05f*in.width);
  p.right     = in.pos+in.side*(-0.05f*in.width);
  }

void TyreTracks::step(Ring& r, const Input& in, bool link) const {
  // 0x52f900: a new newest point, the oldest one overwritten when the ring is full
  const int last = r.head;
  if(link)
    r.pts[size_t(last)].end = false;
  const int cap = int(r.pts.size());
  r.head = (r.head+1)%cap;
  if(r.tail==r.head)
    r.tail = (r.tail+1)%cap;
  write(r,r.head,in,link ? last : -1);
  }

void TyreTracks::add(Ring& r, const Input& in, bool newStrip, bool sameTriangle) const {
  // 0x52f700
  if(r.head<0) {
    r.tail = r.head = 0;
    write(r,0,in,-1);
    return;
    }
  if(newStrip) {
    step(r,in,false);
    return;
    }
  if(r.pts[size_t(r.head)].forward!=in.forward) {
    // reversing: the strip breaks here, and goes on from a new point (the original then carries on below)
    r.pts[size_t(r.head)].broken = true;
    step(r,in,true);
    r.pts[size_t(r.head)].broken = true;
    }
  const int before = r.prev(r.head);
  if(before<0 || r.pts[size_t(before)].end) {
    step(r,in,true);
    return;
    }
  const Point& a = r.pts[size_t(before)];
  if(sameTriangle && Vec3::dot(in.side,a.side)>=0) {
    switch(bend(a.pos,r.pts[size_t(r.head)].pos,in.pos,set)) {
      case Bend::Same:
        return;
      case Bend::Straight:
        write(r,r.head,in,before);   // the newest point moves on
        return;
      default:
        break;
      }
    }
  step(r,in,true);
  }

void TyreTracks::update(const std::array<Wheel,4>& wheels, const Vec3& velocity, const Vec3& forward, int tyre, float dt) {
  tyre = std::clamp(tyre,0,3);
  // 0x52f1a0: the players' cars have all four, the computer's only the rear wheels
  for(size_t i=(player ? 0 : 2); i<rings.size(); ++i) {
    Ring& r = rings[i];
    // 0x52f310: the points age; once one past the oldest has run out, the oldest goes
    for(int j=r.tail; j>=0 && r.head>=0; j=r.next(j)) {
      Point& p = r.pts[size_t(j)];
      if(p.life>0)
        p.life -= dt;
      if(p.life<0 && r.prev(j)>=0) {
        if(r.tail==r.head)
          r.head = -1;
        else
          r.tail = (r.tail+1)%int(r.pts.size());
        }
      }

    // a track where the tyre touches the ground, not in water, at 1 km/h or more
    const Wheel& w = wheels[i];
    const float  speed = velocity.length()*3.6f;
    float intensity = 0;
    if(speed>=1.f && w.touching) {
      switch(w.sound) {
        case SoundSurface::Sand:     intensity = 0.7f; break;
        case SoundSurface::WetSand:  intensity = 1.0f; break;
        case SoundSurface::Trampled: intensity = 0.3f; break;
        case SoundSurface::Gravel:
        case SoundSurface::Wood:     intensity = 0.6f; break;
        default: break;
        }
      }
    if(intensity<=0) {
      r.linked = false;
      continue;
      }
    if(i<2)
      intensity *= 0.5f;

    const Vec3 dir = velocity.normalized();
    Input in;
    in.pos       = w.contact+w.normal*0.01f;
    in.normal    = w.normal;
    in.side      = Vec3::cross(w.normal,dir);
    in.intensity = intensity;
    in.forward   = Vec3::dot(forward,dir)>0;
    in.tyre      = tyre;
    in.width     = TyreWidth[tyre];
    const bool newStrip = !r.linked;
    const bool same     = r.linked && r.triangle==w.triangle;
    r.linked = true;
    add(r,in,newStrip,same);
    r.triangle = w.triangle;
    }
  }

void TyreTracks::collect(std::vector<Mark>& out, const Vec3& camera) const {
  // 0x52fd00 / 0x52ff20: a quad from each point to the next; a strip's first point and the end of a strip that is
  // not the newest one are clear, and the last quarter of the life fades out. The quads of one car climb by 2e-5 m
  // each, so that crossing tracks do not flicker
  float lift = 0;
  for(size_t i=0; i<rings.size(); ++i) {
    const Ring&  r = rings[i];
    const Point* last = nullptr;
    float        lastFade = 0;
    for(int j=r.tail; j>=0 && r.head>=0; j=r.next(j)) {
      const Point& p = r.pts[size_t(j)];
      float fade;
      if(last==nullptr && !p.broken)
        fade = 0;
      else if(p.end && j!=r.head && !p.broken)
        fade = 0;
      else {
        const float quarter = set.timeLife*0.25f;
        if(p.life<0)
          fade = 0;
        else if(p.life>quarter)
          fade = p.intensity;
        else
          fade = std::abs(quarter)<1e-6f ? 0.f : p.intensity*p.life/quarter;
        }
      if(last!=nullptr && (camera-p.left).length()<20.f) {
        Mark m;
        m.kind    = Mark::Track;
        m.texture = uint8_t(last->tyre);
        auto corner = [](MarkVertex& v, const Vec3& pos, float u, float vv, float f, float y) {
          v.pos   = Vec3{pos.x,pos.y+y,pos.z};
          v.uv[0] = u;
          v.uv[1] = vv;
          // grey (1-fade)/2 plus the texture times fade (see game/effects.cpp)
          const float g = std::floor((1.f-f)*128.f)/255.f;
          v.rgba[0] = v.rgba[1] = v.rgba[2] = g;
          v.rgba[3] = std::floor(f*255.f)/255.f;
          };
        corner(m.v[0],last->left, 0,last->v,lastFade,lift);
        corner(m.v[1],last->right,1,last->v,lastFade,lift);
        lift += 2e-5f;
        corner(m.v[2],p.right,1,p.v,fade,lift);
        corner(m.v[3],p.left, 0,p.v,fade,lift);
        out.push_back(m);
        }
      if(!p.end && !p.broken) {
        last     = &p;
        lastFade = fade;
        }
      else {
        last = nullptr;
        }
      }
    }
  }
