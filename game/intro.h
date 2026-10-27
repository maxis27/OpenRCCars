#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/Texture2d>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "audio/film.h"
#include "canvas.h"

class Resources;
class Sounds;

// What RCCars.exe shows before the main menu (the start's steps 0x0f .. 0x13 of 0x4a2a60):
//  - the intro (0x4a3030), unless the script variable AutoRunIntro is 0 (Config.gm sets it to 1) or the game runs in
//    the developer mode: Scripts\intro.ini names the film (introName, in the Tracks folder: Tracks\Intro.dat, an
//    MPEG-1 system stream) and up to 4 parts of it (introNum, part_<n> = from, to seconds; to < 0: the end). The
//    parts play one after another, each from its start to its end (DirectShow's SetPositions, 0x4a1d80), and Esc
//    ends the part playing (0x4a2f10 -> 0x4a23b0). The film fills the window, 800x600 then (0x4a20d0).
//  - the start screen while the game loads (0x4a2ae4 step 0x13): Desktop over the whole screen, the logos LogoRC,
//    Logo1C and LogoCR (the table 0x56b8a0: x0, y0, x1, y1 as parts of the screen) and "Wczytywanie..." (41920) in
//    the middle of the bottom 90 of 480 lines (flags 0x110001: centred both ways).
class Intro : public Canvas {
  public:
    struct Part {
      float from = 0, to = -1;   // s; to < 0: the film's end
      };
    // Scripts\intro.ini
    struct Info {
      std::string       film;
      std::vector<Part> parts;
      };
    static Info loadInfo(Resources& res);

    Intro(Tempest::Device& device, Resources& res);
    ~Intro() override;

    // the film of Scripts\intro.ini from its first part, its sound through `sounds`; false if there is none
    bool start(Sounds& sounds);
    bool playing() const { return film!=nullptr; }
    // Esc: the part playing ends
    void skip();
    void tick(float dt);
    // the film's picture, kept 4:3 in the middle of the window
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h);
    // the start screen
    void drawStart(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h);

  private:
    void stop();
    void play(size_t part);

    struct Picture {
      Tempest::Texture2d tex;
      uint64_t           version = 0;
      };

    Image                       desktop;
    Image                       logos[3];
    Font                        font;
    std::string                 loading;

    Info                        info;
    std::unique_ptr<rc::Film>   film;
    Sounds*                     sounds  = nullptr;
    size_t                      part    = 0;
    std::array<Picture,2>       pictures;     // one per frame in flight
    uint64_t                    version = 0;  // of the film's picture
  };
