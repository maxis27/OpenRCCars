#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/soundbank.h"
#include "mixer.h"

namespace rc {

// Sound\*.wav loaded on first use, and the channels of RCCars.sb
class SoundLibrary {
  public:
    using Reader = std::function<std::vector<uint8_t>(const std::string&)>;

    // `readPack`: a file of RCCars.pack; `sb`: RCCars.sb
    SoundLibrary(Reader readPack, const std::vector<uint8_t>& sb);

    const SoundBank&            bank() const { return sounds; }
    // e.g. "cp_start" for Sound\cp_start.wav; nullptr if it cannot be loaded (logged once)
    std::shared_ptr<const Clip> clip(const std::string& name);

  private:
    Reader                                             read;
    SoundBank                                          sounds;
    std::map<std::string,std::shared_ptr<const Clip>>  cache;
  };

// One channel of RCCars.sb playing in the mixer. Like the original's channel instances it has its own copy of the
// sounds, so the game can change where a sound starts and its tail (as the engine does).
class BankChannel {
  public:
    BankChannel() = default;
    // `channel` of the library's bank; an unknown one stays silent
    BankChannel(Mixer& mixer, SoundLibrary& lib, const std::string& channel);

    bool   valid() const { return mixer!=nullptr; }
    size_t size()  const { return sounds.size(); }
    const SoundBank::Channel* definition() const { return def; }

    void   play(int sound);
    void   play(const std::string& sound);
    void   stop(bool now = false);
    // whether `sound` is the one playing
    bool   isPlaying(int sound) const;
    bool   isPlaying() const;
    // ms into the sound playing
    float  position() const;

    // where `sound` starts next time, ms
    void   setStart(int sound, float ms);
    // the tail of `sound`, also for the one playing now if it is that sound
    void   setTail(int sound, float ms);
    // the volume of `sound` in dB on top of its snd.dat one (RCCars.exe's SOND +0x1c, set by the car's sound code),
    // also for the one playing now
    void   setVolume(int sound, float db);
    // index of a sound by name, -1 if the channel has none of that name
    int    find(const std::string& sound) const { return def ? def->find(sound) : -1; }
    // extra gain (dB) and pan of the whole channel
    void   setGain(float db);
    void   setPan(float pan);
    // whether it stops while the mixer's effects are paused (Mixer::setPausable)
    void   setPausable(bool p);
    // a positioned channel (definition()->far > 0): gain and pan for a sound at `distance` m, `side` -1 left .. 1 right;
    // as RCCars.exe (0x494c00): full volume up to `near`, down to -20 dB at `far`, silent beyond
    void   setPlace(float distance, float side);

  private:
    struct Sound {
      SoundBank::Sound            def;
      float                       start = 0;
      std::shared_ptr<const Clip> clip;
      float                       level = 0;
      };
    Mixer*                      mixer = nullptr;
    const SoundBank::Channel*   def   = nullptr;
    int                         id    = -1;
    std::vector<Sound>          sounds;
  };

}
