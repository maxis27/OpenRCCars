#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class Resources;

// Pictures and text drawn over the screen, laid out as the original lays them out: in units of a 800x600 screen,
// scaled to the window's height and centred on wide screens. The pictures and the Smash fonts are the game's own,
// from RCCars.pack and Language/<language>/. Shared by the race cockpit (Hud) and the menus (Menu).
class Canvas {
  public:
    using Color = std::array<float,4>;

    Canvas(Tempest::Device& device, Resources& res);
    virtual ~Canvas() = default;

    // "Polish" if installed, as the game itself ships it, "English" otherwise; empty without either
    const std::string& language() const { return lang; }

    // "3:07.25"
    static std::string formatTime(float seconds);

  protected:
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
      uint8_t page = 0;       // 0: Font::img, 1: Font::extra
      };
    struct Font {
      Image                  img;
      Image                  extra;   // Smash<size>b.csi: the Polish letters, on the same grid

      std::array<Glyph,256>  glyphs = {};
      std::array<bool,256>   has    = {};
      float                  cell   = 0;
      bool                   hard   = true;  // all-or-nothing opacity (Smash): edges smoothed when enlarged
      };
    struct Draw {
      const Tempest::Texture2d* tex = nullptr;
      size_t                    first = 0, count = 0;
      bool                      depth = false;  // hidden behind the scene (sprites in the 3D view)
      float                     sharpen = 0;    // >0: the opacity's edge narrowed this many times (text), <0: opaque
      };

    // Language/<language>/<file>
    Image  loadImage(Resources& res, std::string_view file, std::vector<uint8_t>* rgba = nullptr);
    // `extra`: also the file of the font's Polish letters (Smash<size>b), if the language has one
    void   loadFont(Resources& res, std::string_view name, Font& f, bool extra = false);
    void   loadGlyphs(Resources& res, std::string_view name, Font& f, uint8_t page);
    // "cock_num" of RCCars.sb, the cockpit's numbers (RCCars.exe 0x4a16f0 makes it the font of 0x1499d50)
    void   loadNumbers(Resources& res, Font& f);

    // a new frame of `w` x `h` pixels: nothing drawn yet
    void   begin(int w, int h);
    // what was drawn since begin(), into the framebuffer set on `enc`; `frame` is the frame in flight (its vertex
    // buffer is rewritten). `between` is called where mark() was, between what was drawn before and after it
    void   flush(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame,
                 const std::function<void()>& between = {}, size_t at = size_t(-1));
    // the place in what is drawn for flush()'s `between`
    size_t mark();

    // layout: 800x600 units -> pixels, centred on wide screens; `right` keeps the distance from the right edge
    float  px(float x, bool right = false) const { return right ? float(scrW)-(800.f-x)*scale : offsetX+x*scale; }
    // keeps the distance from the left edge, for what the original puts in the left corners
    float  pl(float x) const { return x*scale; }
    float  py(float y) const { return y*scale; }

    // a quadrilateral given by its four corners on the screen, in order
    void   poly(const Tempest::Texture2d& tex, const std::array<std::array<float,2>,4>& p, const Color& c);
    void   quad(const Tempest::Texture2d& tex, int tw, int th, const Rect& src, float x, float y, float w, float h,
                const Color& c, float angle = 0, float squash = 1, const Color* bottom = nullptr);
    // a sprite standing in the 3D view at the given depth (0..1), hidden where the scene is nearer
    void   quadAt(const Tempest::Texture2d& tex, float x, float y, float w, float h, float depth, const Color& c);
    // the same with its four corners (screen x, y, depth) and their texture coordinates given, in order
    void   quadAt(const Tempest::Texture2d& tex, const std::array<std::array<float,3>,4>& p,
                  const std::array<std::array<float,2>,4>& uv, const Color& c);
    void   sprite(const Image& img, const Rect& src, float cx, float cy, float size, const Color& c);
    // `bottom`: the colour at the bottom of the letters, fading from `c` at their top
    float  text(const Font& f, std::string_view s, float x, float y, float size, const Color& c, int align = -1,
                const Color* bottom = nullptr, float gap = 0);
    float  textWidth(const Font& f, std::string_view s, float size, float gap = 0) const;

    Tempest::Device&                 device;
    Resources&                       resources;
    Tempest::RenderPipeline          pso;
    Tempest::RenderPipeline          psoDepth;  // the same, tested against the scene's depth
    std::string                      lang;
    const Tempest::Texture2d*        white = nullptr;

    std::vector<Vertex>              verts;
    float                            sharpen = 0;   // for the quads being added: see Draw::sharpen (-1: opaque)
    std::vector<Draw>                draws;
    Tempest::VertexBuffer<Vertex>    vbo[4];   // by the frame in flight and the view (two in the split screen)

    int                              scrW = 0, scrH = 0;
    float                            scale = 1, offsetX = 0;
  };
