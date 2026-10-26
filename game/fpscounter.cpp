#include "fpscounter.h"

#include <cstdio>

using namespace Tempest;

// RCCars.exe 0x554390: the window the averages are taken over, in seconds
constexpr double AverageSpan = 1.0;
// Smash20's cell, drawn 1:1 at 800x600 as the chat console is (Hud::ChatSize)
constexpr float  TextSize    = 28.f;

FpsCounter::FpsCounter(Device& device, Resources& res)
  : Canvas(device,res) {
  loadFont(res,"Smash20",font);
  }

void FpsCounter::frameStart() {
  start = Clock::now();
  if(!started) {
    windowStart = start;
    started     = true;
    }
  }

void FpsCounter::logicDone() {
  logic = Clock::now();
  }

void FpsCounter::drawDone() {
  recorded = Clock::now();
  }

void FpsCounter::presented() {
  const auto end = Clock::now();
  auto sec = [](Clock::duration d) { return std::chrono::duration<double>(d).count(); };
  sum[0] += sec(logic-start);
  sum[1] += sec(recorded-logic);
  sum[2] += sec(end-recorded);
  ++frames;
  const double span = sec(end-windowStart);
  if(span<=AverageSpan)
    return;
  const double t = sum[0]/frames*1000, r = sum[1]/frames*1000, s = sum[2]/frames*1000;
  char buf[96];
  std::snprintf(buf,sizeof(buf),"FPS:%5.1f TRS:%4.1f+%4.1f+%4.1f=%4.1fms",frames/span,t,r,s,t+r+s);
  line        = buf;
  frames      = 0;
  sum[0]      = sum[1] = sum[2] = 0;
  windowStart = end;
  }

void FpsCounter::draw(Encoder<CommandBuffer>& enc, uint8_t frame, int w, int h) {
  if(!visible)
    return;
  begin(w,h);
  // the first second has no numbers yet
  const std::string s = line.empty() ? "FPS:" : line;
  // left edge, centred on the screen's height (draw flags 0x200000 of slot 3, the rectangle the whole screen)
  const float y = (float(h)-TextSize*scale)*0.5f;
  // ours: a dim band behind it, to read it over the bright menus
  if(white!=nullptr)
    quad(*white,1,1,{0,0,1,1},0,y,textWidth(font,s,TextSize)+4*scale,TextSize*scale,{0,0,0,0.5f});
  text(font,s,2*scale,y,TextSize,{1,1,1,1},-1);
  flush(enc,frame);
  }
