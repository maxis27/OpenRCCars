#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mixer.h"

namespace rc {

// An MPEG-1 system stream (Tracks\Intro.dat: MPEG-1 video 640x480 at 25 frames/s, MP2 sound 44.1 kHz stereo)
// decoded with pl_mpeg as it plays. RCCars.exe plays it with DirectShow (0x4a1eb0).
class Film {
  public:
    explicit Film(std::vector<uint8_t> file);  // throws std::runtime_error if it is not MPEG-1 video
    ~Film();

    int    width()    const;
    int    height()   const;
    double duration() const;    // s
    double time()     const;    // s, where the decoding is
    bool   ended()    const;

    // decoding goes on from `t` seconds (the sound decoded and not yet played is dropped)
    void   seek(double t);
    // decodes the next `dt` seconds; true if the picture changed
    bool   advance(double dt);
    // the last picture decoded, width() x height() RGBA
    const std::vector<uint8_t>& picture() const { return rgba; }
    // the film's sound for the mixer's music slot: what advance() decodes, at Mixer::Rate (silence while there is
    // none, so that the mixer keeps it); nullptr if the film has no sound or another rate
    std::unique_ptr<Stream> sound();

  private:
    struct Impl;
    struct Sound;
    std::unique_ptr<Impl>  impl;
    std::shared_ptr<Sound> queue;
    std::vector<uint8_t>   rgba;
    bool                   fresh = false;
  };

}
