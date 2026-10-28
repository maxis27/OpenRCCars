#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "mixer.h"

namespace rc {

// An MP3 file decoded while it plays (dr_mp3), resampled to Mixer::Rate if needed
class Mp3Stream : public Stream {
  public:
    explicit Mp3Stream(std::vector<uint8_t> file);  // throws std::runtime_error if it is not an MP3
    ~Mp3Stream() override;

    size_t mix(float* out, size_t frames, float gain) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
  };

// The race music as RCCars.exe picks it (0x486350, 0x486c00): Autoexec.gm lists the MP3s of the Tracks folder, each
// with a style ("TrackMP map "Dead_Track01.mp3" cycle 1": the 'Deaduski' tracks are style 1, the 'Master' ones
// style 0). With the default Snd_Style the styles take turns, the first one at random, and within a style the tracks
// come in a random order, none twice until all have played.
class MusicList {
  public:
    struct Track {
      std::string file;    // in the Tracks folder
      int         style = 0;
      };

    static MusicList parseAutoexec(std::string_view text);

    const std::vector<Track>& tracks() const { return list; }
    bool                      empty()  const { return list.empty(); }
    // the next track to play, nullptr if there are none
    const Track*              next();
    // the style played (RCCars.crs Snd_Style): only the tracks of that "cycle", -1 any
    void                      setStyle(int s) { only = s; }

  private:
    std::vector<Track> list;
    std::vector<bool>  played;
    int                lastStyle = -2;   // -2: none yet
    int                only      = -1;
    std::mt19937       rnd{std::random_device{}()};
  };

}
