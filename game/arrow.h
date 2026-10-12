#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <cstddef>

class Resources;

// The direction arrow at the bottom of the cockpit, as RCCars.exe draws it (0x4b0fa0, 0x4b1360, 0x4e8f00): model
// "Arrow" of RCCarsDB/cockpit.sb (RCCars.sb folder "Cockpit"), two chevrons one behind the other. Their tops
// extru9 and extru9_3 take a colour, the sides (extru9_*_body) keep the silver stripe of coc_str_2; no lighting.
// The model is seen by its own camera (Settings/arrow.ini) in a square of the screen, turned about its upright axis
// towards the way on.
//
// The colour goes from green through yellow to red as the car uses up the way from the last main checkpoint to the
// next one, and is red on the wrong way. The arrow turns at 90 degrees a second (ten times faster, as does the
// colour, right after a checkpoint until it points the new way) and blinks on the wrong way and once the car has
// come within 5 m of the next checkpoint without passing it.
class DirectionArrow {
  public:
    struct Input {
      float  direction = 0;   // the way on relative to the car's heading, radians, positive = left
      float  left      = 1;   // the part of the way to the next main checkpoint still to go, 0..1
      float  distance  = 0;   // metres straight to the next main checkpoint
      size_t next      = 0;   // the next main checkpoint: a change means one was passed
      bool   wrongWay  = false;
      };

    DirectionArrow(Tempest::Device& device, Resources& res);

    // a new race: points ahead, green
    void reset();
    void tick(float dt, const Input& in);
    // in a render pass of its own whose depth buffer is cleared: the arrow is drawn over everything
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, int w, int h) const;

  private:
    struct Vertex {
      float pos[3];
      float uv[2];
      };

    Tempest::VertexBuffer<Vertex> body, tops;
    const Tempest::Texture2d*     tex = nullptr;
    Tempest::RenderPipeline       pso;

    // Settings/arrow.ini
    float                         nearPlane = 0.01f, fov = 45;
    float                         centreX = 0.5f, centreY = 0.87f, size = 0.17f;
    float                         eye[3]  = {0, 1.12f, 1.0f};
    float                         target[3] = {0, 0.777f, 0.629f};

    float                         current = 0, aim = 0;  // degrees
    float                         shade   = 1, shadeAim = 1;
    float                         blink   = 0;     // > 0.25: hidden
    float                         nearest = 1e6f;  // closest the car came to the next checkpoint
    size_t                        next    = size_t(-1);
    bool                          snap    = false; // just after a checkpoint
  };
