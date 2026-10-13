#pragma once

#include <Tempest/SoundDevice>
#include <Tempest/SoundEffect>

#include <map>
#include <memory>
#include <string>

#include "audio/carsound.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/soundlib.h"

class Resources;

// The game's sound: the channels of RCCars.sb (Sound\*.wav from RCCars.pack) and the music (Tracks\*.mp3), mixed by
// rc::Mixer and played through one Tempest sound effect. Without a sound device the game goes on silent.
//
// Volumes as RCCars.exe sets them from its defaults (Config: SoundVolume 70, SoundBkVolume 100): the effects at
// (70-100)*0.2 = -6 dB (0x4837ae), the music at (100-100)*0.4 dB, 20 units less during a race: -8 dB (0x4863c0).
class Sounds {
  public:
    explicit Sounds(Resources& res);
    ~Sounds();

    // a sound of a channel of RCCars.sb, e.g. ("Checkpoint","cp_start"); each channel plays one sound at a time
    void play(const std::string& channel, const std::string& sound);
    // stops every sound effect (not the music)
    void stopAll();
    void setPaused(bool p);

    // the next race track of the playlist (Autoexec.gm), and on to the next one whenever a track ends
    void startMusic();
    void stopMusic();
    // call once per frame
    void tick();

    rc::Mixer&        mixer()   { return mix; }
    rc::SoundLibrary& library() { return *lib; }
    bool              enabled() const { return device!=nullptr; }

  private:
    void nextTrack();

    Resources&                                   res;
    rc::Mixer                                    mix;
    std::unique_ptr<rc::SoundLibrary>            lib;
    std::map<std::string,rc::BankChannel>        channels;
    rc::MusicList                                music;
    bool                                         musicOn = false;

    std::unique_ptr<Tempest::SoundDevice>        device;
    Tempest::SoundEffect                         output;
  };
