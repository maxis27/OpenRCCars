#pragma once

#include <Tempest/Event>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "canvas.h"
#include "formats/settings.h"
#include "formats/texttable.h"

class Sounds;

// The game's front end as RCCars.exe draws it (its dialog framework, 0x43f250 and the window classes of
// 0x4aa2e0 ..): the main menu, the quick race, the loading screen and the menu over a race (Esc). Laid out in
// units of a 800x600 screen like the original, whose dialogs are fractions of the screen (it always shows its menus
// in 800x600, RCCars.exe 0x4a6840); on wide screens the background is stretched to the whole window and the
// controls keep their shape, by the edge or the part of the background they belong to.
class Menu : public Canvas {
  public:
    enum class Page : uint8_t {
      None,
      Main,       // "Menu główne" (RCCars.exe 0x4c9500)
      QuickRace,  // "Szybki wyścig": dlgRACESUM
      InRace,     // Esc during a race: dlgINRACE
      };
    // what the player chose, for MainWindow
    enum class Command : uint8_t {
      None,
      Race,       // "Wyścig": the track, car, laps and opponents of setup()
      Quit,       // "Wyjdź"
      Resume,     // "Zastosuj" or Esc in the race menu
      BackOnTrack,// "Wróć na tor": the car back to the last checkpoint
      RaceAgain,  // "Jeszcze raz?"
      Abort,      // "Przerwij": the race left for the main menu
      };
    struct Setup {
      size_t  track   = 0;   // 0..9 in the game's order (Language/Game 40050..: Beach_1, Country_3 ...)
      size_t  car     = 0;   // 0..2: Car1..Car3
      int32_t upgrade = 0;   // 0..3
      int     laps    = 3;   // 3, 5 or 7
      int     skill   = 1;   // 0..2: "Niskie", "Norma", "Ekspert"
      };
    // the turning car of the quick race (AnimPreview, 0x4a9e70), in window pixels
    struct Preview {
      float x = 0, y = 0, w = 0, h = 0;
      float yaw   = 0;   // degrees, 60 a second
      float alpha = 1;   // fading in
      };

    static constexpr size_t TrackCount = 10;

    Menu(Tempest::Device& device, Resources& res, Sounds& sounds);

    // the code of the track (e.g. "beach_1") at `index` in the game's order
    static std::string trackName(size_t index);
    // the index of a track code, TrackCount if it is none of them
    static size_t      trackIndex(std::string_view name);

    Page    page() const { return current; }
    bool    active() const { return current!=Page::None; }
    // to `p` with the original's change of screen: the buttons going out, then the new ones coming in
    void    open(Page p);
    void    close();
    Setup&  setup() { return choice; }
    // the next command, Command::None if there is none
    Command take();

    void    tick(float dt);
    // window pixels
    void    mouseMove(int x, int y);
    void    mouseDown(int x, int y);
    void    mouseUp(int x, int y);
    void    keyDown(Tempest::Event::KeyType key);

    std::optional<Preview> carPreview() const;

    // the menu over the framebuffer set on `enc`; `preview` draws the car of carPreview() over the background
    void    draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h,
                 const std::function<void()>& preview);
    // the loading screen of a track (0x4dfaa0): its pictures, its name and how far the loading is (0..1)
    void    drawLoading(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, size_t track,
                        float progress);

  private:
    struct Box {
      float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // 800x600 units
      bool  contains(float x, float y) const { return x>=x0 && x<x1 && y>=y0 && y<y1; }
      };
    // a texture of RCCars.pack (Textures.*), drawn by its 0..1 coordinates
    struct Tex {
      const Tempest::Texture2d* tex = nullptr;
      int                       w = 0, h = 0;
      explicit operator bool() const { return tex!=nullptr; }
      };
    struct Uv {
      float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
      };

    // a button (class BTN, 0x4aa2e0): on the right of the screen in one of the 8 slots of 0x570468, or the
    // race (green) or back (orange, slot 8) one at the bottom
    struct Button {
      enum Kind : uint8_t {
        Tab,      // red, an arrow: another screen
        Action,   // orange
        Radio,    // red, a dot: one of the pages of a screen, the one shown with a white dot
        Back,     // orange, bottom right (slot 8)
        Race,     // green, bottom left
        };
      Kind     kind    = Tab;
      int      slot    = 0;
      uint32_t text    = 0;
      Command  command = Command::None;
      Page     to      = Page::None;
      bool     enabled = true;
      bool     checked = false;  // a Radio of the page shown
      // the focus: +0xa0 bits 1 (coming) and 2 (going) with +0x98 the time of it
      bool     hover   = false;
      int      anim    = 0;
      float    animT   = 0;
      float    pressT  = 0;      // the press (+0xa0 bit 4), 0.4 s
      };
    // a button of the race's menu (class MBB, 0x4cbd20): a pill of messagebox.csi, red or orange
    struct Pill {
      Box      box;
      uint32_t text    = 0;
      bool     red     = false;
      Command  command = Command::None;
      bool     hover   = false;
      };
    // the "<  value  >" choice (class ENM, 0x4b4630): a label on the left of `box`, the arrows and the value
    struct Enum {
      Box                      box;
      float                    split = 0;   // the label's share of the width (dlg*.ini <name>SE)
      uint32_t                 label = 0;
      std::vector<std::string> values;
      size_t                   at    = 0;
      bool                     hoverL = false, hoverR = false;
      float                    pushL = 0, pushR = 0;  // s since an arrow was pressed (0x5c8, 0x5c9)
      };

    void   build(Page p);
    void   leave(Page to, Command cmd);
    void   pressButton(Button& b);
    void   changeShot(int step);
    void   changeEnum(Enum& e, int step);
    void   playControl(int sound);   // "Control sounds" of Interface.sb: 0 ButtonFocus, 1 ArrowFocus ..
    void   playScreen(int sound);    // "Dialog sounds": 0 ChangeScreen, 1 ChangeScreenLeft

    std::string_view str(uint32_t id) const;
    Tex    tex(const std::string& name);
    Box    slotBox(int slot) const;
    // where the button is: the right ones stretched to the screen's edge on wide screens
    Box    buttonBox(const Button& b) const;
    // the scale and the width of the window for this frame (after begin())
    void   layout(int w, int h);
    // how many times the background is stretched sideways
    float  stretch() const { return (800.f+wide)/800.f; }
    // and up and down, on a window narrower than 4:3
    float  stretchY() const { return (600.f+tall)/600.f; }
    // where the screen's own controls go on a wide screen: under the middle of the background's circle
    float  contentShift() const { return wide*0.4f; }

    // drawing in 800x600 units
    void   image(const Tex& t, const Box& b, const Uv& uv, const Color& c);
    void   imageOpaque(const Tex& t, const Box& b, const Uv& uv);
    void   fill(const Box& b, const Color& c);
    // RCCars.exe 0x470e30 flags: `ax` 0 left, 1 centred, 2 right; `ay` 0 top, 1 centred, 2 bottom
    // `k`: the font enlarged that many times
    void   label(const Font& f, std::string_view s, const Box& b, int ax, int ay, const Color& c, float k = 1);
    // several lines, one under another from the top of `b`
    void   lines(const Font& f, std::string_view s, const Box& b, int ax, float step, const Color& c);
    // the line under a label (0x4ac6e0)
    void   underline(float x0, float x1, float y, const Color& c);

    void   drawBackground();
    void   drawHeader();
    void   drawButton(const Button& b);
    void   drawPill(const Pill& p);
    void   drawEnum(const Enum& e, bool marker);
    // shot_<track>_<shot> in `b`, its frame included, cut at `clip`'s left and right
    void   drawShot(size_t track, int shot, const Box& b, float alpha, const Box* clip = nullptr);
    void   drawMain();
    void   drawQuickRace();
    void   drawCursor();

    Sounds&                 sounds;
    rc::TextTable           texts;
    Font                    font20, font26;   // Smash20 (0x1499d54), Smash26 (0x1499d58)

    std::array<Tex,4>       podl;        // Podl_LeftTop, Podl_RightTop, Podl_LeftBottom, Podl_RightBottom
    std::array<Tex,9>       plates;      // ButtonPodl_right_1..9
    Tex                     buttons, radio, raceBtn, backBtn, flare, header, arrows, msgBox, logo, cursor,
                            progressor;
    Image                   desktop;     // Language/<language>/Desktop.csi: the loading screen
    rc::CurveSpline         focusIn, focusOut, blick, blickAlpha;

    Page                    current = Page::None;
    Page                    next    = Page::None;  // after the buttons have gone
    Command                 nextCmd = Command::None;
    Command                 command = Command::None;
    uint32_t                headerText = 0;
    // the change of screen: the right buttons coming in (2) or going out (1) over 0.5 s (+0xb8, +0xbc); the
    // header the same (0x4b63a0 +0x40, +0x44)
    int                     slide  = 0;
    float                   slideT = 0;

    std::vector<Button>     right;      // slots 0..7, then the back and race buttons
    std::vector<Pill>       pills;
    std::vector<Enum>       enums;      // QuickRace: laps, skill
    Enum                    trackEnum, carEnum;
    Setup                   choice;
    float                   carYaw   = 0;
    float                   carAlpha = 0;
    // the main menu's pictures (0x4ca000): four of each track, the chosen one in the middle, its track the one
    // raced; `shotScroll` the row still moving to it, in pictures
    size_t                  shotAt     = 0;
    float                   shotScroll = 0;

    float                   mouseX = -1, mouseY = -1;  // 800x600 units
    bool                    mouseIn = false;
    float                   time = 0;
    float                   fade = 1;   // the opacity of what is drawn (the screen's controls changing)
    float                   wide = 0;   // how much wider than 800 units the window is
    float                   tall = 0;   // how much higher than 600 units it is (narrower than 4:3)
    float                   dx   = 0;   // moves what is drawn right, in units
    float                   dy   = 0;   // and down
  };
