#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <vector>

#include "fx/lensflare.h"

class Resources;

// Draws rc::fx::LensFlare: the sun's disc alpha blended and tested against the scene's depth, the flare added on
// top of everything (shader/sprite.*, without fog: the sun stands in the sky).
class SunView {
  public:
    SunView(Tempest::Device& device, Resources& res);

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h,
              const rc::fx::LensFlare& flare);

  private:
    struct Vertex {   // as Effects' sprites
      float pos[2];
      float uv[2];
      float color[4];
      float depth = 0;
      float fog   = 1;
      };

    Tempest::Device&              device;
    const Tempest::Texture2d*     tex[5] = {};  // sun_disc, lens_flare_1, _2, _3, shine
    Tempest::RenderPipeline       blend, add;
    Tempest::VertexBuffer<Vertex> vbo[2];
    std::vector<Vertex>           verts;
  };
