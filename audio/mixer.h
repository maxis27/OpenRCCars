#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace rc {

// 16-bit PCM, mono or stereo, any rate (Sound\*.wav are 44.1 kHz mono, the menu ones stereo)
struct Clip {
  std::vector<int16_t> samples;
  uint32_t             rate     = 44100;
  uint16_t             channels = 1;

  size_t frames()   const { return channels ? samples.size()/channels : 0; }
  float  lengthMs() const { return rate ? float(frames())*1000.f/float(rate) : 0.f; }

  // RIFF WAVE with 16-bit PCM; throws std::runtime_error otherwise
  static Clip loadWav(const std::vector<uint8_t>& file);
  };

// Something that plays for a long time and is decoded on the fly: the music. Called on the mixing thread.
class Stream {
  public:
    virtual ~Stream() = default;
    // adds up to `frames` stereo frames at Mixer::Rate (interleaved) to `out`; returns how many, fewer at the end
    virtual size_t mix(float* out, size_t frames, float gain) = 0;
  };

// Software mixer of the game's sounds, modelled on the sound channels of RCCars.exe (0x494400): a channel plays one
// sound at a time, and asking it for another one
//   - restarts or keeps the current one if it is the same (Play::noRestart),
//   - waits for the current one to end (Play::queue),
//   - lets the current one play its tail (fading out) first, if it has one,
//   - or cuts the current one.
// Volumes are in dB as in the original. All methods are thread safe; render() runs on the audio thread.
class Mixer {
  public:
    static constexpr uint32_t Rate = 44100;

    struct Play {
      float startMs   = 0;     // where in the clip to start
      float volume    = 0;     // dB
      float fadeIn    = 0;     // ms, from -20 dB
      float tail      = 0;     // ms the sound goes on (fading out) when the next one is asked for
      bool  loop      = false;
      bool  queue     = false;
      bool  noRestart = false;
      };

    Mixer();
    ~Mixer();

    // a new channel, empty
    int   addChannel();
    // `tag` names the sound for playing() (e.g. its index in a SoundBank channel)
    void  play(int ch, std::shared_ptr<const Clip> clip, int tag, const Play& p);
    // ends the channel: after the current sound's tail, or at once
    void  stop(int ch, bool now = false);
    void  stopAll();
    // tag of the sound playing (not the one waiting), -1 if none
    int   playing(int ch) const;
    // ms into the current sound
    float position(int ch) const;
    // changes the current sound's tail or volume (dB)
    void  setTail(int ch, float ms);
    void  setVolume(int ch, float db);
    // per channel: extra gain (dB, e.g. distance) and pan (-1 left .. 1 right)
    void  setGain(int ch, float db);
    void  setPan(int ch, float pan);

    // all sound effects, dB
    void  setEffectsVolume(float db);
    // pauses the effects (the music goes on)
    void  setPaused(bool p);

    // the music: one stream at a time, dB
    void  setMusic(std::unique_ptr<Stream> s);
    void  setMusicVolume(float db);
    // the stream has ended (or there is none)
    bool  musicEnded() const;

    // `frames` stereo frames at Rate, interleaved
    void  render(int16_t* out, size_t frames);
    void  render(float* out, size_t frames);

  private:
    struct Voice {
      std::shared_ptr<const Clip> clip;
      int    tag     = -1;
      Play   prm;
      double pos     = 0;   // source frames
      double end     = 0;   // source frames, where it stops (loops when it does not stop)
      double played  = 0;   // output frames since it began (for the fade-in)
      bool   cut     = false;
      double cutAt   = 0;   // source frames: the tail began here
      };
    struct Channel {
      Voice  cur;
      Voice  next;
      bool   hasNext  = false;
      float  gain     = 0;
      float  pan      = 0;
      float  lastL    = -1, lastR = -1;  // gains of the previous block, to ramp
      };

    static void start(Voice& v, std::shared_ptr<const Clip> clip, int tag, const Play& p);
    void        mixChannel(Channel& c, float* out, size_t frames);
    float       voiceGain(const Voice& v) const;

    mutable std::mutex      sync;
    std::vector<Channel>    channels;
    float                   effects  = 0;
    bool                    paused   = false;
    std::unique_ptr<Stream> music;
    float                   musicDb  = 0;
    bool                    musicEnd = true;
    std::vector<float>      buffer;
  };

}
