#pragma once

#include <Tempest/SoundDevice>
#include <Tempest/SoundEffect>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audio/ambient.h"
#include "audio/carsound.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/soundlib.h"
#include "world/objects.h"

class Resources;

namespace rc { class Characters; class Traffic; }

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
    // sound `sound` of a channel of RCCarsDB/Interface.sb, the menus' ("Control sounds": 0 ButtonFocus,
    // 1 ArrowFocus, 2 ArrowPress, 3 ButtonPress, 4 RaceFocus, 5 RacePress ...; "Dialog sounds": 0 ChangeScreen,
    // 1 ChangeScreenLeft), as RCCars.exe 0x4ddb80 plays them
    void playInterface(const std::string& channel, int sound);
    // the first sound of a positioned channel (e.g. "burst", "bullet") at `at`, heard from `ear`
    void playAt(const std::string& channel, const rc::Vec3& at, const rc::Vec3& ear, const rc::Vec3& right);
    // stops every sound effect (not the music)
    void stopAll();
    void setPaused(bool p);

    // the next race track of the playlist (Autoexec.gm), and on to the next one whenever a track ends
    void startMusic();
    void stopMusic();
    // the water of a track (water_splash_N, WATER_FALL_N, the sea along `coast`: LevelMesh::seaCoast), heard from
    // `ear` with `right` to the right of it; `waterAtEar` as rc::AmbientSounds::update
    void setTrack(const rc::SbScene& scene, const std::vector<rc::Vec3>& coast);
    void tickAmbient(const rc::Vec3& ear, const rc::Vec3& right, float dt, std::optional<float> waterAtEar);

    // the voices of the people and animals (the sound channel named like the model in people.sb / AIChars.sb:
    // Man, Woman, RepairMan, Seagull, Dog; rc::Characters::Actor::sound says what each one asks for), heard from
    // `ear`
    void tickCharacters(const rc::Characters& chars, const rc::Vec3& ear, const rc::Vec3& right);

    // the props (RCCars.exe 0x50cb00): a knocked one plays the first sound of the channel in its model in stone.sb
    // (Ball "ball", Cola "pepsi", Klubok "klubok" ...), heard from `ear`
    void tickProps(const std::vector<Objects::Sound>& props, const rc::Vec3& ear, const rc::Vec3& right);
    // the road traffic (RCCars.exe 0x517530): each car's engine (channel Roadcar_slow, all the time) or horn and
    // siren (channel Roadcar, when rc::Traffic asks), heard from `ear`; the BTR's bursts are fx::Gunfire's
    void tickTraffic(const rc::Traffic& traffic, const rc::Vec3& ear, const rc::Vec3& right);

    // the player's antenna scratching along something above it (RCCars.exe 0x4fe5f0): `car` 0..2 (Car1 ..
    // Car3, -1 for none), `under` whether something is within the antenna's reach above its mount, `speed` km/h of
    // the car (its whole velocity). Antenna_scratch, the one sound of the channel AntennaSnd in Car<n>Antenna of
    // Car.sb (the same for all three: loops, not restarted, 350 ms tail), plays while under something faster than 1 km/h, at -15 dB at
    // 1 km/h up to 0 dB at 35 km/h; otherwise it stops
    void tickAntenna(int car, bool under, float speed);

    // call once per frame
    void tick();

    rc::Mixer&        mixer()   { return mix; }
    rc::SoundLibrary& library() { return *lib; }
    bool              enabled() const { return device!=nullptr; }

  private:
    void nextTrack();
    // the sound channels of a .sb file of the game's folder (none if it cannot be read)
    std::unique_ptr<rc::SoundLibrary> loadLibrary(const char* sbFile, const char* what);

    Resources&                                   res;
    rc::Mixer                                    mix;
    std::unique_ptr<rc::SoundLibrary>            lib;
    std::map<std::string,rc::BankChannel>        channels;
    rc::MusicList                                music;
    rc::AmbientSounds                            ambient;
    std::unique_ptr<rc::SoundLibrary>            peopleLib, animalLib, propLib, interfaceLib, carLib;
    std::map<std::string,rc::BankChannel>        interfaceChannels;
    std::map<std::pair<std::string,int>,rc::BankChannel> propChannels; // as actorChannels
    // by model and the actor's number among those of its model; the mixer keeps its channels, so they are reused
    std::map<std::pair<std::string,int>,rc::BankChannel> actorChannels;
    rc::BankChannel                              antenna;
    int                                          antennaCar = -1;
    // per road car: its own sound and its gun
    std::vector<rc::BankChannel> trafficChannels;
    bool                                         musicOn = false;

    std::unique_ptr<Tempest::SoundDevice>        device;
    Tempest::SoundEffect                         output;
  };
