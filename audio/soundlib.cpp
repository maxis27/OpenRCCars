#include "soundlib.h"

#include <cstdio>
#include <exception>

namespace rc {

SoundLibrary::SoundLibrary(Reader readPack, const std::vector<uint8_t>& sb)
  : read(std::move(readPack)) {
  std::vector<uint8_t> volumes;
  try {
    volumes = read("Sound\\snd.dat");
    }
  catch(const std::exception&) {
    }
  sounds = SoundBank::load(sb,volumes);
  }

std::shared_ptr<const Clip> SoundLibrary::clip(const std::string& name) {
  auto it = cache.find(name);
  if(it!=cache.end())
    return it->second;
  std::shared_ptr<const Clip> c;
  try {
    c = std::make_shared<const Clip>(Clip::loadWav(read("Sound\\"+name+".wav")));
    }
  catch(const std::exception& e) {
    std::fprintf(stderr,"sound \"%s\": %s\n",name.c_str(),e.what());
    }
  cache[name] = c;
  return c;
  }

BankChannel::BankChannel(Mixer& mixer, SoundLibrary& lib, const std::string& channel) {
  def = lib.bank().find(channel);
  if(def==nullptr) {
    std::fprintf(stderr,"no sound channel \"%s\" in RCCars.sb\n",channel.c_str());
    return;
    }
  this->mixer = &mixer;
  id          = mixer.addChannel();
  for(auto& s:def->sounds)
    sounds.push_back({s,0.f,lib.clip(s.name),0.f});
  }

void BankChannel::play(int i) {
  if(mixer==nullptr)
    return;
  if(i<0 || size_t(i)>=sounds.size()) {
    mixer->stop(id);
    return;
    }
  auto& s = sounds[size_t(i)];
  // RCCars.exe 0x494400: such a sound never cuts anything, not even itself
  if(s.def.idleOnly() && isPlaying())
    return;
  Mixer::Play p;
  p.startMs   = s.start;
  p.volume    = s.def.volume+s.level;
  p.fadeIn    = s.def.fadeIn;
  p.tail      = s.def.tail;
  p.loop      = s.def.loop();
  p.queue     = s.def.queue();
  p.noRestart = s.def.noRestart();
  mixer->play(id,s.clip,i,p);
  }

void BankChannel::play(const std::string& sound) {
  if(def!=nullptr)
    play(def->find(sound));
  }

void BankChannel::stop(bool now) {
  if(mixer)
    mixer->stop(id,now);
  }

bool BankChannel::isPlaying(int sound) const {
  return mixer!=nullptr && mixer->playing(id)==sound && sound>=0;
  }

bool BankChannel::isPlaying() const {
  return mixer!=nullptr && mixer->playing(id)>=0;
  }

float BankChannel::position() const {
  return mixer ? mixer->position(id) : 0.f;
  }

void BankChannel::setStart(int sound, float ms) {
  if(sound>=0 && size_t(sound)<sounds.size())
    sounds[size_t(sound)].start = ms;
  }

void BankChannel::setTail(int sound, float ms) {
  if(sound<0 || size_t(sound)>=sounds.size())
    return;
  sounds[size_t(sound)].def.tail = ms;
  if(isPlaying(sound))
    mixer->setTail(id,ms);
  }

void BankChannel::setVolume(int sound, float db) {
  if(sound<0 || size_t(sound)>=sounds.size())
    return;
  auto& s = sounds[size_t(sound)];
  s.level = db;
  if(isPlaying(sound))
    mixer->setVolume(id,s.def.volume+db);
  }

void BankChannel::setGain(float db) {
  if(mixer)
    mixer->setGain(id,db);
  }

void BankChannel::setPan(float pan) {
  if(mixer)
    mixer->setPan(id,pan);
  }

void BankChannel::setPlace(float distance, float side) {
  if(mixer==nullptr || def==nullptr || def->far<=0)
    return;
  float db = 0;
  if(distance>=def->far)
    db = -200.f;
  else if(distance>def->near)
    db = -20.f*(distance-def->near)/(def->far-def->near);
  mixer->setGain(id,db);
  mixer->setPan(id,side);
  }

}
