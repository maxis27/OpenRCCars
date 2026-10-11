#include "camera.h"

#include <algorithm>
#include <cmath>

using namespace rc;

static constexpr float DegToRad = 3.14159265358979f/180.f;

Vec3 Camera::forward() const {
  const float y = yaw*DegToRad, p = pitch*DegToRad;
  return { std::sin(y)*std::cos(p), std::sin(p), std::cos(y)*std::cos(p) };
  }

Vec3 Camera::right() const {
  // right-handed: right = forward x up
  const float y = yaw*DegToRad;
  return { -std::cos(y), 0, std::sin(y) };
  }

void Camera::lookAt(const Vec3& target) {
  const Vec3 d = (target-position).normalized();
  yaw   = std::atan2(d.x,d.z)/DegToRad;
  pitch = std::asin(std::clamp(d.y,-1.f,1.f))/DegToRad;
  }

void Camera::move(float dForward, float dRight, float dUp) {
  position += forward()*dForward + right()*dRight + Vec3{0,dUp,0};
  }

void Camera::rotate(float dYaw, float dPitch) {
  yaw   += dYaw;
  pitch  = std::clamp(pitch+dPitch,-89.f,89.f);
  }

void Camera::chase(const Mat4& target, float distance, float height) {
  Vec3 dir = target.vector({0,0,1});
  dir.y = 0;
  dir   = dir.normalized();
  const Vec3 at = target.translation();
  position = at - dir*distance + Vec3{0,height,0};
  lookAt(at + Vec3{0,height*0.4f,0});
  }

Mat4 Camera::view() const {
  // rows of the rotation are the camera basis vectors (right-handed world: right = forward x up),
  // view space itself is +Z forward to match the projection
  const Vec3 f = forward();
  const Vec3 r = Vec3::cross(f,{0,1,0}).normalized();
  const Vec3 u = Vec3::cross(r,f);
  Mat4 m;
  m.m[0][0] = r.x; m.m[0][1] = r.y; m.m[0][2] = r.z; m.m[0][3] = -Vec3::dot(r,position);
  m.m[1][0] = u.x; m.m[1][1] = u.y; m.m[1][2] = u.z; m.m[1][3] = -Vec3::dot(u,position);
  m.m[2][0] = f.x; m.m[2][1] = f.y; m.m[2][2] = f.z; m.m[2][3] = -Vec3::dot(f,position);
  return m;
  }

Mat4 Camera::projection(float aspect, float zNear, float zFar) const {
  // like Tempest::Matrix4x4::perspective (depth 0..1, w = view z), but with Y pointing down
  // as Vulkan/DX12 clip space expects - Tempest does not flip the viewport for us
  const float g = 1.f/std::tan(fov*DegToRad*0.5f);
  const float k = zFar/(zFar-zNear);
  Mat4 m;
  m.m[0][0] = g/aspect;
  m.m[1][1] = -g;
  m.m[2][2] = k;  m.m[2][3] = -zNear*k;
  m.m[3][2] = 1;  m.m[3][3] = 0;
  return m;
  }
