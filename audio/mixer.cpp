#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace rc {

static uint32_t rd32(const uint8_t* p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]|p[1]<<8); }

Clip Clip::loadWav(const std::vector<uint8_t>& f) {
  if(f.size()<12 || std::memcmp(f.data(),"RIFF",4)!=0 || std::memcmp(f.data()+8,"WAVE",4)!=0)
    throw std::runtime_error("not a WAVE file");
  Clip     c;
  uint16_t format = 0, bits = 0;
  bool     fmt = false;
  size_t   pos = 12;
  while(pos+8<=f.size()) {
    const uint8_t* h    = f.data()+pos;
    const size_t   size = rd32(h+4);
    const size_t   body = pos+8;
    const size_t   end  = std::min(f.size(),body+size);
    if(std::memcmp(h,"fmt ",4)==0 && size>=16 && body+16<=f.size()) {
      format     = rd16(f.data()+body);
      c.channels = rd16(f.data()+body+2);
      c.rate     = rd32(f.data()+body+4);
      bits       = rd16(f.data()+body+14);
      fmt        = true;
      }
    else if(std::memcmp(h,"data",4)==0) {
      if(!fmt || format!=1 || bits!=16 || (c.channels!=1 && c.channels!=2) || c.rate==0)
        throw std::runtime_error("only 16-bit PCM WAVE files are supported");
      c.samples.resize((end-body)/2);
      for(size_t i=0; i<c.samples.size(); ++i)
        c.samples[i] = int16_t(rd16(f.data()+body+i*2));
      c.samples.resize(c.samples.size()/c.channels*c.channels);
      return c;
      }
    pos = body+size+(size&1);
    }
  throw std::runtime_error("WAVE file without data");
  }

// gains change at most once per this many frames, ramped across it
static constexpr size_t Block = 64;

static float dbToGain(float db) {
  return db<=-100.f ? 0.f : std::pow(10.f,db/20.f);
  }

Mixer::Mixer()  = default;
Mixer::~Mixer() = default;

int Mixer::addChannel() {
  std::lock_guard<std::mutex> guard(sync);
  channels.emplace_back();
  return int(channels.size()-1);
  }

void Mixer::start(Voice& v, std::shared_ptr<const Clip> clip, int tag, const Play& p) {
  v       = Voice();
  v.clip  = std::move(clip);
  v.tag   = tag;
  v.prm   = p;
  v.end   = double(v.clip->frames());
  v.pos   = std::min(v.end,double(p.startMs)*v.clip->rate/1000.0);
  v.played = 0;
  }

void Mixer::play(int ch, std::shared_ptr<const Clip> clip, int tag, const Play& p) {
  if(clip==nullptr || clip->frames()==0)
    return;
  std::lock_guard<std::mutex> guard(sync);
  if(ch<0 || size_t(ch)>=channels.size())
    return;
  Channel& c = channels[size_t(ch)];
  if(c.cur.clip==nullptr) {
    start(c.cur,std::move(clip),tag,p);
    c.hasNext = false;
    return;
    }
  if(c.cur.tag==tag && p.noRestart && !c.cur.cut)
    return;
  if(p.queue) {
    // after the current one, which finishes its pass if it loops
    c.cur.prm.loop = false;
    start(c.next,std::move(clip),tag,p);
    c.hasNext = true;
    return;
    }
  if(c.cur.prm.tail>0) {
    if(!c.cur.cut) {
      const double tail = double(c.cur.prm.tail)*c.cur.clip->rate/1000.0;
      c.cur.cut   = true;
      c.cur.cutAt = c.cur.pos;
      if(c.cur.prm.loop)
        c.cur.end = c.cur.pos+tail; // may run past the clip's end: wraps
      else
        c.cur.end = std::min(c.cur.end,c.cur.pos+tail);
      }
    start(c.next,std::move(clip),tag,p);
    c.hasNext = true;
    return;
    }
  start(c.cur,std::move(clip),tag,p);
  c.hasNext = false;
  }

void Mixer::stop(int ch, bool now) {
  std::lock_guard<std::mutex> guard(sync);
  if(ch<0 || size_t(ch)>=channels.size())
    return;
  Channel& c = channels[size_t(ch)];
  c.hasNext = false;
  if(c.cur.clip==nullptr)
    return;
  if(!now && c.cur.prm.tail>0) {
    if(!c.cur.cut) {
      const double tail = double(c.cur.prm.tail)*c.cur.clip->rate/1000.0;
      c.cur.cut   = true;
      c.cur.cutAt = c.cur.pos;
      c.cur.end   = c.cur.prm.loop ? c.cur.pos+tail : std::min(c.cur.end,c.cur.pos+tail);
      }
    return;
    }
  c.cur = Voice();
  }

void Mixer::stopAll() {
  std::lock_guard<std::mutex> guard(sync);
  for(auto& c:channels) {
    c.cur     = Voice();
    c.hasNext = false;
    }
  }

int Mixer::playing(int ch) const {
  std::lock_guard<std::mutex> guard(sync);
  if(ch<0 || size_t(ch)>=channels.size())
    return -1;
  auto& c = channels[size_t(ch)];
  return c.cur.clip ? c.cur.tag : -1;
  }

float Mixer::position(int ch) const {
  std::lock_guard<std::mutex> guard(sync);
  if(ch<0 || size_t(ch)>=channels.size())
    return 0;
  auto& v = channels[size_t(ch)].cur;
  if(v.clip==nullptr)
    return 0;
  return float(std::fmod(v.pos,double(std::max<size_t>(1,v.clip->frames())))*1000.0/v.clip->rate);
  }

void Mixer::setTail(int ch, float ms) {
  std::lock_guard<std::mutex> guard(sync);
  if(ch>=0 && size_t(ch)<channels.size() && !channels[size_t(ch)].cur.cut)
    channels[size_t(ch)].cur.prm.tail = ms;
  }

void Mixer::setVolume(int ch, float db) {
  std::lock_guard<std::mutex> guard(sync);
  if(ch>=0 && size_t(ch)<channels.size())
    channels[size_t(ch)].cur.prm.volume = db;
  }

void Mixer::setGain(int ch, float db) {
  std::lock_guard<std::mutex> guard(sync);
  if(ch>=0 && size_t(ch)<channels.size())
    channels[size_t(ch)].gain = db;
  }

void Mixer::setPan(int ch, float pan) {
  std::lock_guard<std::mutex> guard(sync);
  if(ch>=0 && size_t(ch)<channels.size())
    channels[size_t(ch)].pan = std::clamp(pan,-1.f,1.f);
  }

void Mixer::setEffectsVolume(float db) {
  std::lock_guard<std::mutex> guard(sync);
  effects = db;
  }

void Mixer::setPaused(bool p) {
  std::lock_guard<std::mutex> guard(sync);
  paused = p;
  }

void Mixer::setMusic(std::unique_ptr<Stream> s) {
  std::unique_ptr<Stream> old;
  {
    std::lock_guard<std::mutex> guard(sync);
    old      = std::move(music);
    music    = std::move(s);
    musicEnd = (music==nullptr);
  }
  // `old` is destroyed outside the lock
  }

void Mixer::setMusicVolume(float db) {
  std::lock_guard<std::mutex> guard(sync);
  musicDb = db;
  }

bool Mixer::musicEnded() const {
  std::lock_guard<std::mutex> guard(sync);
  return musicEnd;
  }

// dB of a voice right now: its volume, the fade-in from -20 dB and the fade-out of its tail down to -20 dB
// (0x494d70 ramps both linearly in dB)
float Mixer::voiceGain(const Voice& v) const {
  float db = v.prm.volume;
  if(v.prm.fadeIn>0) {
    const float ms = float(v.played*1000.0/Rate);
    if(ms<v.prm.fadeIn)
      db += -20.f*(1.f-ms/v.prm.fadeIn);
    }
  if(v.cut && v.end>v.cutAt)
    db += -20.f*float((v.pos-v.cutAt)/(v.end-v.cutAt));
  return db;
  }

void Mixer::mixChannel(Channel& c, float* out, size_t frames) {
  size_t done = 0;
  while(done<frames && c.cur.clip!=nullptr) {
    Voice&      v    = c.cur;
    const Clip& clip = *v.clip;
    const size_t n   = std::min(Block,frames-done);
    const float  db  = voiceGain(v)+c.gain+effects;
    const float  g   = dbToGain(db);
    const float  gl  = g*std::min(1.f,1.f-c.pan);
    const float  gr  = g*std::min(1.f,1.f+c.pan);
    if(c.lastL<0) {
      c.lastL = gl;
      c.lastR = gr;
      }
    const double step   = double(clip.rate)/Rate;
    const double length = double(clip.frames());
    size_t i = 0;
    for(; i<n; ++i) {
      if(v.pos>=v.end) {
        if(v.prm.loop && !v.cut) {
          v.pos -= length;
          v.end  = length;
          }
        else
          break;
        }
      double p = v.pos;
      while(p>=length)    // a looping sound's tail runs past the end
        p -= length;
      const size_t a = size_t(p);
      const size_t b = (a+1<clip.frames()) ? a+1 : (v.prm.loop ? 0 : a);
      const float  t = float(p-double(a));
      const float  k = float(i)/float(n);
      const float  l = (c.lastL+(gl-c.lastL)*k)/32768.f;
      const float  r = (c.lastR+(gr-c.lastR)*k)/32768.f;
      if(clip.channels==1) {
        const float s = float(clip.samples[a])+(float(clip.samples[b])-float(clip.samples[a]))*t;
        out[(done+i)*2+0] += s*l;
        out[(done+i)*2+1] += s*r;
        }
      else {
        const float sl = float(clip.samples[a*2])  +(float(clip.samples[b*2])  -float(clip.samples[a*2]))*t;
        const float sr = float(clip.samples[a*2+1])+(float(clip.samples[b*2+1])-float(clip.samples[a*2+1]))*t;
        out[(done+i)*2+0] += sl*l;
        out[(done+i)*2+1] += sr*r;
        }
      v.pos    += step;
      v.played += 1;
      }
    c.lastL = gl;
    c.lastR = gr;
    done += i;
    if(i<n) {
      // this sound is over: the one waiting starts
      if(c.hasNext) {
        c.cur     = std::move(c.next);
        c.next    = Voice();
        c.hasNext = false;
        }
      else
        c.cur = Voice();
      }
    }
  }

void Mixer::render(float* out, size_t frames) {
  std::fill(out,out+frames*2,0.f);
  std::lock_guard<std::mutex> guard(sync);
  if(!paused)
    for(auto& c:channels)
      mixChannel(c,out,frames);
  if(music!=nullptr && !musicEnd) {
    const size_t n = music->mix(out,frames,dbToGain(musicDb));
    if(n<frames)
      musicEnd = true;
    }
  }

void Mixer::render(int16_t* out, size_t frames) {
  if(buffer.size()<frames*2)
    buffer.resize(frames*2);
  render(buffer.data(),frames);
  for(size_t i=0; i<frames*2; ++i)
    out[i] = int16_t(std::clamp(buffer[i]*32767.f,-32768.f,32767.f));
  }

}
