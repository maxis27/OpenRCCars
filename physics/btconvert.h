#pragma once

// Conversions between rc math and Bullet, with PhysicsWorld::Scale applied to positions (only for the
// physics/*.cpp files; nothing outside them sees Bullet's units).

#include <btBulletDynamicsCommon.h>

#include "formats/math3d.h"
#include "vehicle.h"

namespace rc::bt {

constexpr float S = PhysicsWorld::Scale;

inline btVector3 toBt(const Vec3& v)   { return btVector3(v.x,v.y,v.z); }
inline Vec3      fromBt(const btVector3& v) { return {float(v.x()),float(v.y()),float(v.z())}; }

// rotation + translation of a Mat4 (unscaled) -> scaled btTransform
inline btTransform toBt(const Mat4& m) {
  btMatrix3x3 basis(m.m[0][0],m.m[0][1],m.m[0][2],
                    m.m[1][0],m.m[1][1],m.m[1][2],
                    m.m[2][0],m.m[2][1],m.m[2][2]);
  return btTransform(basis,toBt(m.translation())*S);
  }

inline Mat4 fromBt(const btTransform& t) {
  Mat4 m;
  const btMatrix3x3& b = t.getBasis();
  for(int r=0; r<3; ++r)
    for(int c=0; c<3; ++c)
      m.m[r][c] = float(b[r][c]);
  const btVector3 o = t.getOrigin()/S;
  m.m[0][3] = float(o.x()); m.m[1][3] = float(o.y()); m.m[2][3] = float(o.z());
  return m;
  }

}
