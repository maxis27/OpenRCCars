#pragma once

#include "formats/math3d.h"

// Free-fly camera. Scene data is right-handed with Y up (exported from Maya) - verified against the
// track map from the game menu (Textures.1/shot_beach1_L.csi). yaw=0 looks along +Z.
class Camera {
  public:
    rc::Vec3 position = {0,5,0};
    float    yaw      = 0;     // degrees, rotation around +Y
    float    pitch    = 0;     // degrees, positive looks up
    float    fov      = 65.f;

    rc::Vec3 forward() const;
    rc::Vec3 right()   const;

    void     lookAt(const rc::Vec3& target);
    void     move(float dForward, float dRight, float dUp);
    void     rotate(float dYaw, float dPitch);

    rc::Mat4 view() const;
    rc::Mat4 projection(float aspect, float zNear = 0.05f, float zFar = 600.f) const;
    rc::Mat4 viewProj(float aspect) const { return projection(aspect)*view(); }
  };
