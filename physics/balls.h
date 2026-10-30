#pragma once

#include <cmath>
#include <cstddef>

#include "formats/math3d.h"

namespace rc {

// What RCCars.exe's objects collide with each other by (0x533430): each kind of object gives a handful of spheres
// (the cars 0x4ef9e0, the props 0x50dc50, people 0x511e30, guards 0x51a350, dogs 0x518820, road cars 0x516480).
struct Ball {
  Vec3  c;
  float r = 0;
  };

// Two objects whose origins are this far apart or more are not looked at (0x5335a0: 2.5 m)
constexpr float BallsReach = 2.5f;

// RCCars.exe 0x5335a0 / 0x533740: the first pair of balls, a's in order and b's in order for each, whose centres
// are no farther apart than their radii (each grown by `grow`) together. `at` gets the middle of the two centres,
// `normal` the way from b's centre to a's (up when they are on each other). False when no pair meets.
inline bool ballsMeet(const Ball* a, size_t na, const Ball* b, size_t nb, Vec3* at = nullptr,
                      Vec3* normal = nullptr, float grow = 0) {
  for(size_t i=0; i<na; ++i)
    for(size_t j=0; j<nb; ++j) {
      const Vec3  d = a[i].c-b[j].c;
      const float l = d.length();
      if(l>a[i].r+b[j].r+2.f*grow)
        continue;
      if(at!=nullptr)
        *at = l>=1e-6f ? (a[i].c+b[j].c)*0.5f : a[i].c;
      if(normal!=nullptr)
        *normal = l>=1e-6f ? d*(1.f/l) : Vec3{0,1,0};
      return true;
      }
  return false;
  }

// RCCars.exe 0x534be0: a contact of the line from `b` to `a` (two centres of mass, or a centre and a point touched):
// through its middle, the normal towards `a`. False when they are at the same place.
inline bool contactOf(const Vec3& a, const Vec3& b, Vec3& at, Vec3& normal) {
  const Vec3  d = a-b;
  const float l = d.length();
  at = (a+b)*0.5f;
  if(l<1e-6f)
    return false;
  normal = d*(1.f/l);
  return true;
  }

// the rotation of a Mat4 as a quaternion w, x, y, z
inline void toQuat(const Mat4& m, float q[4]) {
  const float t = m.m[0][0]+m.m[1][1]+m.m[2][2];
  if(t>0) {
    const float s = std::sqrt(t+1.f)*2.f;
    q[0] = 0.25f*s;
    q[1] = (m.m[2][1]-m.m[1][2])/s;
    q[2] = (m.m[0][2]-m.m[2][0])/s;
    q[3] = (m.m[1][0]-m.m[0][1])/s;
    }
  else if(m.m[0][0]>m.m[1][1] && m.m[0][0]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[0][0]-m.m[1][1]-m.m[2][2])*2.f;
    q[0] = (m.m[2][1]-m.m[1][2])/s;
    q[1] = 0.25f*s;
    q[2] = (m.m[0][1]+m.m[1][0])/s;
    q[3] = (m.m[0][2]+m.m[2][0])/s;
    }
  else if(m.m[1][1]>m.m[2][2]) {
    const float s = std::sqrt(1.f+m.m[1][1]-m.m[0][0]-m.m[2][2])*2.f;
    q[0] = (m.m[0][2]-m.m[2][0])/s;
    q[1] = (m.m[0][1]+m.m[1][0])/s;
    q[2] = 0.25f*s;
    q[3] = (m.m[1][2]+m.m[2][1])/s;
    }
  else {
    const float s = std::sqrt(1.f+m.m[2][2]-m.m[0][0]-m.m[1][1])*2.f;
    q[0] = (m.m[1][0]-m.m[0][1])/s;
    q[1] = (m.m[0][2]+m.m[2][0])/s;
    q[2] = (m.m[1][2]+m.m[2][1])/s;
    q[3] = 0.25f*s;
    }
  }

inline Mat4 fromQuat(const float q[4]) {
  const float w = q[0], x = q[1], y = q[2], z = q[3];
  Mat4 m;
  m.m[0][0] = 1-2*(y*y+z*z); m.m[0][1] = 2*(x*y-w*z);   m.m[0][2] = 2*(x*z+w*y);
  m.m[1][0] = 2*(x*y+w*z);   m.m[1][1] = 1-2*(x*x+z*z); m.m[1][2] = 2*(y*z-w*x);
  m.m[2][0] = 2*(x*z-w*y);   m.m[2][1] = 2*(y*z+w*x);   m.m[2][2] = 1-2*(x*x+y*y);
  return m;
  }

}
