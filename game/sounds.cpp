#include "sounds.h"

#include <Tempest/Log>
#include <Tempest/MemReader>

#include <algorithm>

#include "formats/pack.h"

using namespace Tempest;

Sounds::Sounds(rc::PackFile& pack) : pack(pack) {
  try {
    device = std::make_unique<SoundDevice>();
    }
  catch(const std::exception& e) {
    Log::e("no sound device (",e.what(),"), playing without sound");
    }
  }

Sounds::~Sounds() {
  playing.clear(); // the effects go before the device
  cache.clear();
  }

const Sound* Sounds::sound(const std::string& name) {
  auto it = cache.find(name);
  if(it==cache.end()) {
    try {
      // the Sound folder holds plain PCM WAV
      auto      data = pack.read("Sound\\"+name+".wav");
      MemReader rd(data);
      it = cache.emplace(name,Sound(rd)).first;
      }
    catch(const std::exception& e) {
      Log::e("unable to load sound \"",name,"\": ",e.what());
      it = cache.emplace(name,Sound()).first; // not tried again
      }
    }
  return it->second.isEmpty() ? nullptr : &it->second;
  }

void Sounds::play(const std::string& name) {
  if(device==nullptr)
    return;
  if(auto s = sound(name)) {
    auto fx = device->load(*s);
    fx.play();
    playing.push_back(std::move(fx));
    }
  }

void Sounds::stopAll() {
  playing.clear();
  }

void Sounds::tick() {
  playing.erase(std::remove_if(playing.begin(),playing.end(),[](const SoundEffect& e){ return e.isFinished(); }),
                playing.end());
  }
