#pragma once

#include <cmath>

namespace rc {

struct Vec2 {
  float x = 0, y = 0;
  };

struct Vec3 {
  float x = 0, y = 0, z = 0;

  Vec3  operator + (const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
  Vec3  operator - (const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
  Vec3  operator * (float k)       const { return {x*k, y*k, z*k}; }
  Vec3& operator +=(const Vec3& o)       { x+=o.x; y+=o.y; z+=o.z; return *this; }
  float length() const { return std::sqrt(x*x+y*y+z*z); }
  Vec3  normalized() const { float l = length(); return l>0 ? (*this)*(1.f/l) : Vec3{0,1,0}; }
  static float dot  (const Vec3& a, const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
  static Vec3  cross(const Vec3& a, const Vec3& b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
  };

// Row-major 4x4 matrix acting on column vectors: p' = M * p
struct Mat4 {
  float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

  static Mat4 identity() { return Mat4(); }
  static Mat4 translate(const Vec3& t) { Mat4 r; r.m[0][3]=t.x; r.m[1][3]=t.y; r.m[2][3]=t.z; return r; }
  static Mat4 scale(const Vec3& s)     { Mat4 r; r.m[0][0]=s.x; r.m[1][1]=s.y; r.m[2][2]=s.z; return r; }
  static Mat4 rotate(int axis, float degrees) {
    const float a = degrees*3.14159265358979f/180.f;
    const float c = std::cos(a), s = std::sin(a);
    const int   i = (axis+1)%3, j = (axis+2)%3;
    Mat4 r;
    r.m[i][i] = c; r.m[i][j] = -s;
    r.m[j][i] = s; r.m[j][j] =  c;
    return r;
    }

  Mat4 operator * (const Mat4& o) const {
    Mat4 r;
    for(int y=0; y<4; ++y)
      for(int x=0; x<4; ++x) {
        float v = 0;
        for(int k=0; k<4; ++k)
          v += m[y][k]*o.m[k][x];
        r.m[y][x] = v;
        }
    return r;
    }

  Vec3 point(const Vec3& p) const {
    return { m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3],
             m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3],
             m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3] };
    }
  Vec3 vector(const Vec3& p) const {
    return { m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z,
             m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z,
             m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z };
    }
  Vec3 translation() const { return {m[0][3], m[1][3], m[2][3]}; }

  // determinant of the 3x3 part; negative for mirrored transforms (scale -1 in the data)
  float determinant3() const {
    return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) -
           m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) +
           m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    }

  // inverse of a rotation + translation (no scale)
  Mat4 inverseRigid() const {
    Mat4 r;
    for(int y=0; y<3; ++y)
      for(int x=0; x<3; ++x)
        r.m[y][x] = m[x][y];
    const Vec3 t = r.vector(translation());
    r.m[0][3] = -t.x; r.m[1][3] = -t.y; r.m[2][3] = -t.z;
    return r;
    }

  // column-major copy, as expected by GLSL mat4
  void toColumnMajor(float out[16]) const {
    for(int c=0; c<4; ++c)
      for(int r=0; r<4; ++r)
        out[c*4+r] = m[r][c];
    }
  };

}
