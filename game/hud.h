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
// At the bottom the original's two gauges (cockpit_sp1, cockpit_sp2): turbo on the left, the speed on the right; at the
// top right under the lap the track map (trackmap_<n>, map_cp, map_arrow), at the top left the place (place<n>.csi)
// when the race knows it.
// Not from the original: only the checkpoints' spheres outlined in the 3D view (G, the radius within which a main one
// counts): the next main one green, the one after it yellow, the ones passed in this lap grey, and the
// sub-checkpoint the arrow shows thin blue.
class Hud {
  public:
    // what the race does not know
    struct Info {
      float speed     = 0;     // km/h
      float turbo     = 0;     // 0..1 of the tank
      float turboMax  = 1;     // the tank's size, 0..1 of the gauge (the original: 50..100 units of 100)
      float speedMax  = 0;     // km/h the car can reach (with turbo)
      float direction = 0;     // the next checkpoint relative to the car's heading, radians, positive = left
      int   place     = 0;     // 1.. the place in the race, 0: not shown (no opponents)
      rc::Vec3 carPos, carForward;            // the player's car, for the map
      struct Car { rc::Vec3 pos, forward; };
      std::vector<Car> opponents;             // the other cars on the map
      std::vector<rc::Opponents::Standing> standings;  // with opponents: the order, for the table at the finish
      bool  paused    = false;
      bool  gates     = false; // the checkpoints drawn over the 3D view (debug)
      rc::Mat4 viewProj;       // the camera, for the debug spheres and the checkpoint arrows
      };

    Hud(Tempest::Device& device, Resources& res);

    // the track: picks its map
    void setTrack(const std::string& name);
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
    // a gauge in the bottom corner (mirrored on the right) filled up to `value` (0..1) with the number in it
    // `reach` (value..1): how far the car can get, the gauge is grey beyond it
    void   gauge(bool right, float value, float reach, const std::string& number, std::string_view label);
    void   trackMap(const rc::Race& race, const Info& info);
    void   checkpointArrow(const rc::Race& race, const rc::Mat4& viewProj);
    void   quad(const Tempest::Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
                const Color& c, float angle = 0, float squash = 1, const Color* bottom = nullptr);
    // a sprite standing in the 3D view at the given depth (0..1), hidden where the scene is nearer
    void   quadAt(const Tempest::Texture2d& tex, float x, float y, float w, float h, float depth, const Color& c);
    void   sprite(const Image& img, const Rect& src, float cx, float cy, float size, const Color& c);
    // `bottom`: the colour at the bottom of the letters, fading from `c` at their top
    float  text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align = -1,
                const Color* bottom = nullptr, float gap = 0);
    float  textWidth(const Font& f, std::string_view s, float size, float gap = 0) const;
    // the gauges' numbers: white to grey, outlined
    void   metalText(const Font& f, std::string_view s, float x, float y, float size, int align, float gap = 2);
    // text with a dark shadow under it

    Tempest::Device&                 device;
    Resources&                       resources;
    Tempest::RenderPipeline          pso;
    Tempest::RenderPipeline          psoDepth;  // the same, tested against the scene's depth
    std::string                      lang;
    Image                            msg321, msgGo, msgBest, msgWrong, msgPause, lapLabel;
    const Tempest::Texture2d*        arrow = nullptr; // coc_str_2, 128x128
    // cp_ar_<2 green, 3 red: the lap's end>_f<1..3>, 256x256: arrows pointing down at the next checkpoint
    const Tempest::Texture2d*        cpArrow[2][3] = {};
    Font                             font;
    Image                            place[6];
    const Tempest::Texture2d*        gaugeBase = nullptr; // cockpit_sp1: the dial with its yellow scale
    const Tempest::Texture2d*        gaugeFill = nullptr; // cockpit_sp2: the same tube in red, a grey one opposite
    const Tempest::Texture2d*        gaugePieces = nullptr; // cockpit_sp4: the tube's cap and clamp
    const Tempest::Texture2d*        overtaking  = nullptr; // brackets around the direction arrow
    const Tempest::Texture2d*        panel       = nullptr; // messagebox: the map's frame
    const Tempest::Texture2d*        mapArrow  = nullptr; // 64x32: green (the player), violet (the others)
    const Tempest::Texture2d*        mapCp     = nullptr; // 64x32: an orange dot, a glow
    // the track's map: world (x,z) -> map texels u+iv = k*(x+iz)+offset
    const Tempest::Texture2d*        map       = nullptr;
    std::array<float,2>              mapK      = {}, mapOffset = {};
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
