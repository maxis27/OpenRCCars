#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>

#include <chrono>
#include <string>

#include "canvas.h"

class Resources;

// RCCars.exe's frame rate line (0x4a3c3c), shown while the script variable ShowFPS is not 0 (registered as 0 at
// 0x490383; `Set ShowFPS 1` in Config.gm or Autoexec.gm turns it on): "FPS:%5.1f TRS:%4.1f+%4.1f+%4.1f=%4.1fms",
// at the left edge, halfway down the screen (text slot 3 of the debug layer 0x148fae8, 0x480020). The numbers are
// averages over the last second or more (0x4a3e2f): frames counted, divided by the time they took, recounted once
// the time passes 1 s.
// Not from the original: F12 turns it on and off; the font (the original's "albet16" is not among the game's files)
// is Smash20, on a dim band; the three times are ours, in milliseconds per frame: the game's logic, waiting for the frame in flight
// and recording the drawing, and submitting and presenting (the original's T, R and S are not decoded). The original's P/S (polygons per second) and its
// memory lines are left out.
class FpsCounter : public Canvas {
  public:
    FpsCounter(Tempest::Device& device, Resources& res);

    bool visible = false;

    // the frame's parts as they pass, in render() order: start, logic done, drawing recorded, presented
    void frameStart();
    void logicDone();
    void drawDone();
    void presented();

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t frame, int w, int h);

  private:
    using Clock = std::chrono::steady_clock;

    Font              font;
    Clock::time_point start, logic, recorded;
    Clock::time_point windowStart;
    bool              started = false;
    int               frames  = 0;
    double            sum[3]  = {};    // seconds of this window in each part
    std::string       line;
  };
