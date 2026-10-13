#pragma once

#include <Tempest/Sound>
#include <Tempest/SoundDevice>
#include <Tempest/SoundEffect>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rc { class PackFile; }

// Sound\*.wav from RCCars.pack, played once at full volume (no 3D position yet). Without a sound device the game
// goes on silent.
class Sounds {
  public:
    explicit Sounds(rc::PackFile& pack);
    ~Sounds();

    // e.g. "prestart" for Sound\prestart.wav
    void play(const std::string& name);
    // stops everything still playing
    void stopAll();
    // drops the effects that finished
    void tick();

  private:
    const Tempest::Sound* sound(const std::string& name);

    rc::PackFile&                                  pack;
    std::unique_ptr<Tempest::SoundDevice>          device;
    std::unordered_map<std::string,Tempest::Sound> cache;
    std::vector<Tempest::SoundEffect>              playing;
  };
