#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "physics/opponents.h"
#include "physics/race.h"

class Resources;

// The race cockpit over the 3D view, drawn with the original's own pictures from Language/<language>/: the
// countdown ("3", "2", "1", "Jazda!"), "okrążenie" with the lap number, "zły kierunek", "najlepsze okrążenie",
// "meta", "pauza", and the times in the game's Smash font. Positions follow Settings/cockpit.ini and
// cockpit_time.ini (a 800x600 screen; elements on the right keep their distance from the right edge on wider
// screens). The direction arrow is the cockpit's chevron (coc_str_2) turned towards the next checkpoint. Over the
// next main checkpoint hangs the animated arrow cp_ar_* (Settings/anim_cp.ini).
// Not from the original: the count of main checkpoints passed in this lap under the lap number, a green
// "Punkt n/m" each time one is passed, and the gates outlined in the 3D view (G): the next main one green, the
// one after it yellow, the ones passed in this lap grey, and the sub-checkpoint the arrow shows thin blue.
class Hud {
  public:
    // what the race does not know
    struct Info {
      float speed     = 0;     // km/h
      float turbo     = 0;     // 0..1
      float direction = 0;     // the next checkpoint relative to the car's heading, radians, positive = left
      bool  paused    = false;
      bool  gates     = false; // the checkpoint gates drawn over the 3D view (debug)
      int   place     = 0;     // against the opponents, 1 ..; 0 without any
      std::vector<rc::Opponents::Standing> standings;  // with opponents: the order, for the table at the finish
      rc::Mat4 viewProj;       // the camera, for the gates and the checkpoint arrows
      };

    Hud(Tempest::Device& device, Resources& res);

    // a race event: starts its message
    void event(rc::Race::Event e);
    // `dt` seconds of real time (messages and blinking)
    void tick(float dt) { clock += dt; }
    // `frame` is the frame in flight (its vertex buffer is rewritten)
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Race& race,
              const Info& info);

    // "3:07.25"
    static std::string formatTime(float seconds);

  private:
    struct Vertex {
      float pos[2];
      float uv[2];
      float color[4];
      float depth = 0;  // used by the depth-tested pipeline only
      };
    struct Image {
      Tempest::Texture2d tex;
      int                w = 0, h = 0;
      bool               loaded = false;
      };
    struct Rect {
      float x0, y0, x1, y1;   // texture pixels
      };
    struct Glyph {
      float u0 = 0, u1 = 0;   // texture pixels
      float v0 = 0, v1 = 0;
      };
    struct Font {
      Image                  img;
      std::array<Glyph,256>  glyphs = {};
      std::array<bool,256>   has    = {};
      float                  cell   = 0;
      };
    struct Draw {
      const Tempest::Texture2d* tex = nullptr;
      size_t                    first = 0, count = 0;
      bool                      depth = false;  // hidden behind the scene (sprites in the 3D view)
      };
    enum class Message { None, Go, BestLap, Finish };
    using Color = std::array<float,4>;

    Image  loadImage(Resources& res, std::string_view file, std::vector<uint8_t>* rgba = nullptr);
    void   loadFont(Resources& res, std::string_view name, Font& f);

    // layout: 800x600 units -> pixels; `right` keeps the distance from the right edge
    float  px(float x, bool right = false) const { return right ? float(scrW)-(800.f-x)*scale : offsetX+x*scale; }
    float  py(float y) const { return y*scale; }

    // a quadrilateral given by its four corners on the screen, in order
    void   poly(const Tempest::Texture2d& tex, const std::array<std::array<float,2>,4>& p, const Color& c);
    // a world-space line of the given thickness in pixels, cut at the camera's near plane
    void   line3d(const rc::Mat4& viewProj, const rc::Vec3& a, const rc::Vec3& b, float width, const Color& c);
    void   gates(const rc::Race& race, const rc::Mat4& viewProj);
    void   checkpointArrow(const rc::Race& race, const rc::Mat4& viewProj);
    void   quad(const Tempest::Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
                const Color& c, float angle = 0, float squash = 1);
    // a sprite standing in the 3D view at the given depth (0..1), hidden where the scene is nearer
    void   quadAt(const Tempest::Texture2d& tex, float x, float y, float w, float h, float depth, const Color& c);
    void   sprite(const Image& img, const Rect& src, float cx, float cy, float size, const Color& c);
    float  text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align = -1);
    float  textWidth(const Font& f, std::string_view s, float size) const;

    Tempest::Device&                 device;
    Tempest::RenderPipeline          pso;
    Tempest::RenderPipeline          psoDepth;  // the same, tested against the scene's depth
    std::string                      lang;
    Image                            msg321, msgGo, msgBest, msgWrong, msgPause, lapLabel;
    Image                            places[6];  // place1..6.csi: "1szy" .. "6ty"
    const Tempest::Texture2d*        arrow = nullptr; // coc_str_2, 128x128
    // cp_ar_<2 green, 3 red: the lap's end>_f<1..3>, 256x256: arrows pointing down at the next checkpoint
    const Tempest::Texture2d*        cpArrow[2][3] = {};
    Font                             font;
    const Tempest::Texture2d*        white = nullptr;

    std::vector<Vertex>              verts;
    std::vector<Draw>                draws;
    Tempest::VertexBuffer<Vertex>    vbo[2];

    float                            clock      = 0;
    Message                          message    = Message::None;
    float                            messageAt  = 0;
    float                            lapAt      = -100; // when the last lap was finished: its time blinks
    float                            flashAt    = -100; // when the last main checkpoint was passed
    int                              scrW = 0, scrH = 0;
    float                            scale = 1, offsetX = 0;
  };
