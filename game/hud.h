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

#include "canvas.h"
#include "formats/texttable.h"
#include "physics/opponents.h"
#include "physics/race.h"

class Resources;

// The race cockpit over the 3D view, drawn with the original's own pictures from Language/<language>/: the
// countdown ("3", "2", "1", "Jazda!"), "okrążenie" with the lap number, "zły kierunek", "najlepsze okrążenie",
// "meta", "pauza", "Łup!", and the times in the game's Smash font. Positions follow Settings/cockpit.ini and
// cockpit_time.ini (a 800x600 screen; elements on the right keep their distance from the right edge on wider
// screens). The direction arrow between the brackets is a 3D model drawn by DirectionArrow (game/arrow.h). Over the
// next main checkpoint hangs the animated arrow cp_ar_* (Settings/anim_cp.ini).
// At the bottom the original's two gauges (cockpit_sp1, cockpit_sp2): turbo on the left, the speed on the right; at the
// top right under the lap the track map (trackmap_<n>, map_cp, map_arrow), at the top left the place (place<n>.csi)
// when the race knows it.
// Not from the original: only the checkpoints' spheres outlined in the 3D view (G, the radius within which a main one
// counts): the next main one green, the one after it yellow, the ones passed in this lap grey, and the
// sub-checkpoint the arrow shows thin blue.
class Hud : public Canvas {
  public:
    // what the race does not know
    struct Info {
      float speed     = 0;     // km/h
      float turbo     = 0;     // 0..1 of the tank
      float turboMax  = 1;     // the tank's size, 0..1 of the gauge (the original: 50..100 units of 100)
      float speedMax  = 0;     // km/h the car can reach (with turbo)
      int   place     = 0;     // 1.. the place in the race, 0: not shown (no opponents)
      rc::Vec3 carPos, carForward;            // the player's car, for the map
      struct Car { rc::Vec3 pos, forward; };
      std::vector<Car> opponents;             // the other cars on the map
      bool  paused    = false;
      bool  lowSignal = false; // in a reset volume: "Słaby sygnał" (msg_low_signal.csi)
      bool  gates     = false; // the checkpoints drawn over the 3D view (debug)
      // the ghost race (RCCars.exe 0x4afdc0 with flags 0xc0): the lap without the laps of the race, which has no
      // end, and the time of the lap being driven instead of the race's
      bool  ghostRace = false;
      rc::Mat4 viewProj;       // the camera, for the debug spheres and the checkpoint arrows
      };

    Hud(Tempest::Device& device, Resources& res);

    // the track: picks its map
    void setTrack(const std::string& name);
    // a race event: starts its message
    void event(rc::Race::Event e);
    // the player's car hit something (RCCars.exe): "Łup!", or "Wielkie Łup!" when `great`, for a second
    void hit(bool great) { hitAt = clock; hitGreat = great; }
    // a line for the race's chat console (Windows-1250, as Language/Game/*.tbl has it), see chatConsole()
    void say(std::string line);
    // the game's text `id` (Language/Game/<language>.tbl) with its "%s" filled with `who` and then `what`, on the
    // chat console (RCCars.exe 0x4ad140): the race's news of 20102 .. 20109
    void tell(uint32_t id, std::string_view who, std::string_view what = {});
    // the game's text `id`, empty without it
    std::string_view gameText(uint32_t id) const { return texts.get(id); }
    // `dt` seconds of real time (messages and blinking)
    void tick(float dt) { clock += dt; }
    // `frame` is the frame in flight (its vertex buffer is rewritten)
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h, const rc::Race& race,
              const Info& info);

  private:
    enum class Message { None, Go, BestLap, Finish };

    void   chatConsole();

    // a world-space line of the given thickness in pixels, cut at the camera's near plane
    void   line3d(const rc::Mat4& viewProj, const rc::Vec3& a, const rc::Vec3& b, float width, const Color& c);
    void   gates(const rc::Race& race, const rc::Mat4& viewProj);
    // a gauge in the bottom corner (mirrored on the right) filled up to `value` (0..1) with the number in it
    // `reach` (value..1): how far the car can get, the gauge is grey beyond it
    void   gauge(bool right, float value, float reach, const std::string& number, std::string_view label);
    void   trackMap(const rc::Race& race, const Info& info);
    void   checkpointArrow(const rc::Race& race, const rc::Mat4& viewProj);
    // the gauges' numbers: white to grey, outlined
    // the cockpit's numbers (time, lap, gauges) in cock_num, 1:1; `y` is the top of its 64 high cells
    void   numberText(std::string_view s, float x, float y, int align);
    // text with a dark shadow under it

    Image                            msg321, msgGo, msgBest, msgWrong, msgPause, msgHits, msgLow, lapLabel;
    // cp_ar_<2 green, 3 red: the lap's end>_f<1..3>, 256x256: arrows pointing down at the next checkpoint
    const Tempest::Texture2d*        cpArrow[2][3] = {};
    Font                             font;
    Font                             font20;   // Smash20: the chat console
    rc::TextTable                    texts;    // Language/Game/<language>.tbl
    Font                             numbers;  // cock_num from RCCars.sb
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

    float                            clock      = 0;
    struct ChatLine {
      std::string text;
      float       at = 0;   // clock when said
      };
    std::vector<ChatLine>            chat;     // the newest last
    Message                          message    = Message::None;
    float                            messageAt  = 0;
    float                            hitAt      = -100;
    bool                             hitGreat   = false;
    float                            lapAt      = -100; // when the last lap was finished: its time blinks
    float                            flashAt    = -100; // when the last main checkpoint was passed
  };
