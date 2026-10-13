#include "sounds.h"

#include <Tempest/Log>

#include <string_view>

#include "resources.h"
#include "formats/settings.h"

using namespace Tempest;

namespace {

class MixerOutput : public SoundProducer {
  public:
    explicit MixerOutput(rc::Mixer& mixer) : SoundProducer(rc::Mixer::Rate,2), mixer(mixer) {}
    void renderSound(int16_t* out, size_t n) override { mixer.render(out,n); }

  private:
    rc::Mixer& mixer;
  };

}

Sounds::Sounds(Resources& res) : res(res) {
  mix.setEffectsVolume((70.f-100.f)*0.2f);
  mix.setMusicVolume((100.f-100.f-20.f)*0.4f);
  try {
    lib = std::make_unique<rc::SoundLibrary>([&res](const std::string& f){ return res.pack().read(f); },
                                             res.readFile("RCCars.sb"));
    Log::i("sound channels: ",lib->bank().channels.size());
    }
  catch(const std::exception& e) {
    Log::e("RCCars.sb sound channels: ",e.what());
    lib = std::make_unique<rc::SoundLibrary>([&res](const std::string& f){ return res.pack().read(f); },
                                             std::vector<uint8_t>{});
    }
  try {
    const auto text = res.readFile("Autoexec.gm");
    music = rc::MusicList::parseAutoexec(std::string_view(reinterpret_cast<const char*>(text.data()),text.size()));
    }
  catch(const std::exception& e) {
    Log::e("music list: ",e.what());
    }

  try {
    device = std::make_unique<SoundDevice>();
    output = device->load(std::make_unique<MixerOutput>(mix));
    output.play();
    }
  catch(const std::exception& e) {
    Log::e("no sound device (",e.what(),"), playing without sound");
    device.reset();
    }
  }

Sounds::~Sounds() {
  output = SoundEffect(); // stops the mixing thread's use of `mix` before it goes
  device.reset();
  }

void Sounds::play(const std::string& channel, const std::string& sound) {
  auto it = channels.find(channel);
  if(it==channels.end())
    it = channels.emplace(channel,rc::BankChannel(mix,*lib,channel)).first;
  it->second.play(sound);
  }

void Sounds::stopAll() {
  mix.stopAll();
  }

void Sounds::setPaused(bool p) {
  mix.setPaused(p);
  }

void Sounds::setTrack(const rc::SbScene& scene) {
  ambient.stop();
  rc::SliderFile ini;
  try {
    const auto d = res.pack().read("Settings\\water_splash_mng.ini");
    ini = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(const std::exception& e) {
    Log::e("water_splash_mng.ini: ",e.what());
    }
  ambient = rc::AmbientSounds(mix,*lib,scene,ini);
  }

void Sounds::tickAmbient(const rc::Vec3& ear, const rc::Vec3& right, float dt) {
  ambient.update(ear,right,dt);
  }

void Sounds::startMusic() {
  musicOn = true;
  nextTrack();
  }

void Sounds::stopMusic() {
  musicOn = false;
  mix.setMusic(nullptr);
  }

void Sounds::nextTrack() {
  // a track that cannot be read is skipped, but not forever
  for(size_t tries=0; tries<music.tracks().size(); ++tries) {
    const auto* t = music.next();
    if(t==nullptr)
      break;
    try {
      mix.setMusic(std::make_unique<rc::Mp3Stream>(res.readFile(std::filesystem::path("Tracks")/t->file)));
      Log::i("music: ",t->file);
      return;
      }
    catch(const std::exception& e) {
      Log::e("music \"",t->file,"\": ",e.what());
      }
    }
  musicOn = false;
  }

void Sounds::tick() {
  if(musicOn && mix.musicEnded())
    nextTrack();
  }
