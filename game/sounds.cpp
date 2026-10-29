#include "sounds.h"

#include <Tempest/Log>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>

#include "resources.h"
#include "formats/settings.h"
#include "physics/characters.h"
#include "physics/traffic.h"

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
  applyVolumes();
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
  // every sound read now, before the menus: read when first played, it held up that frame of the race
  peopleLib    = loadLibrary("RCCarsDB/people.sb","character sounds");
  animalLib    = loadLibrary("RCCarsDB/AIChars.sb","character sounds");
  propLib      = loadLibrary("RCCarsDB/stone.sb","prop sounds");
  interfaceLib = loadLibrary("RCCarsDB/Interface.sb","menu sounds");
  carLib       = loadLibrary("RCCarsDB/Car.sb","antenna sound");
  for(auto* l:{lib.get(),peopleLib.get(),animalLib.get(),propLib.get(),interfaceLib.get(),carLib.get()})
    l->preload();

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

std::unique_ptr<rc::SoundLibrary> Sounds::loadLibrary(const char* sbFile, const char* what) {
  std::vector<uint8_t> sb;
  try {
    sb = res.readFile(sbFile);
    }
  catch(const std::exception& e) {
    Log::e(what,": ",e.what());
    }
  return std::make_unique<rc::SoundLibrary>([this](const std::string& f){ return res.pack().read(f); },sb);
  }

void Sounds::play(const std::string& channel, const std::string& sound) {
  auto it = channels.find(channel);
  if(it==channels.end())
    it = channels.emplace(channel,rc::BankChannel(mix,*lib,channel)).first;
  it->second.play(sound);
  }

void Sounds::playInterface(const std::string& channel, int sound) {
  auto it = interfaceChannels.find(channel);
  if(it==interfaceChannels.end()) {
    it = interfaceChannels.emplace(channel,rc::BankChannel(mix,*interfaceLib,channel)).first;
    it->second.setPausable(false);   // heard over a paused race
    }
  if(sound>=0 && size_t(sound)<it->second.size())
    it->second.play(sound);
  }

void Sounds::playAt(const std::string& channel, const rc::Vec3& at, const rc::Vec3& ear, const rc::Vec3& right) {
  auto it = channels.find(channel);
  if(it==channels.end())
    it = channels.emplace(channel,rc::BankChannel(mix,*lib,channel)).first;
  auto&          ch = it->second;
  const rc::Vec3 to = at-ear;
  const float    d  = to.length();
  ch.setPlace(d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
  if(ch.size()>0)
    ch.play(0);
  }

void Sounds::stopAll() {
  mix.stopAll();
  }

void Sounds::setPaused(bool p) {
  mix.setPaused(p);
  }

void Sounds::setTrack(const rc::SbScene& scene, const std::vector<rc::Vec3>& coast) {
  ambient.stop();
  rc::SliderFile ini;
  try {
    const auto d = res.pack().read("Settings\\water_splash_mng.ini");
    ini = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(const std::exception& e) {
    Log::e("water_splash_mng.ini: ",e.what());
    }
  ambient = rc::AmbientSounds(mix,*lib,scene,ini,coast);
  }

void Sounds::tickAmbient(const rc::Vec3& ear, const rc::Vec3& right, float dt, std::optional<float> waterAtEar) {
  ambient.update(ear,right,dt,waterAtEar);
  }

void Sounds::tickCharacters(const rc::Characters& chars, const rc::Vec3& ear, const rc::Vec3& right) {
  std::map<std::string,int> seen;
  for(auto* a:chars.actors()) {
    const auto key = std::make_pair(a->model->name,seen[a->model->name]++);
    auto it = actorChannels.find(key);
    if(it==actorChannels.end()) {
      if(a->sound.empty())
        continue;   // nothing to stop either
      const bool people = a->kind==rc::Characters::Kind::Person || a->kind==rc::Characters::Kind::Guard;
      auto&      lib    = people ? peopleLib : animalLib;
      it = actorChannels.emplace(key,rc::BankChannel(mix,*lib,a->model->name)).first;
      }
    auto&          ch = it->second;
    const rc::Vec3 to = a->world.translation()-ear;
    const float    d  = to.length();
    ch.setPlace(d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
    if(!a->sound.empty())
      ch.play(a->sound);
    else if(a->silence)
      ch.play(-1);   // stops it, after its tail (RCCars.exe 0x494400 with -1)
    }
  }

void Sounds::tickProps(const std::vector<Objects::Sound>& props, const rc::Vec3& ear, const rc::Vec3& right) {
  std::map<std::string,int> seen;
  for(auto& p:props) {
    if(p.model==nullptr)
      continue;
    const auto key = std::make_pair(*p.model,seen[*p.model]++);
    auto it = propChannels.find(key);
    if(it==propChannels.end()) {
      if(!p.hit)
        continue;
      auto def = propLib->bank().ofModel(*p.model);
      if(def==nullptr)
        continue;
      it = propChannels.emplace(key,rc::BankChannel(mix,*propLib,def->name)).first;
      }
    auto&          ch = it->second;
    const rc::Vec3 to = p.at-ear;
    const float    d  = to.length();
    ch.setPlace(d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
    if(p.hit)
      ch.play(0);
    }
  }

void Sounds::tickTraffic(const rc::Traffic& traffic, const rc::Vec3& ear, const rc::Vec3& right) {
  if(lib==nullptr)
    return;
  const auto& cars = traffic.cars();
  while(trafficChannels.size()<cars.size())
    trafficChannels.emplace_back(mix,*lib,traffic.slow() ? "Roadcar_slow" : "Roadcar");
  auto place = [&](rc::BankChannel& ch, const rc::Vec3& at) {
    const rc::Vec3 to = at-ear;
    const float    d  = to.length();
    ch.setPlace(d,d>1e-3f ? rc::Vec3::dot(to,right)/d : 0.f);
    };
  for(size_t i=0; i<cars.size(); ++i) {
    auto& own = trafficChannels[i];
    const rc::Traffic::Car& c = *cars[i];
    if(!c.onRoad) {
      if(own.isPlaying())
        own.stop();
      continue;
      }
    const rc::Vec3 at = c.pose.translation();
    place(own,at);
    if(c.sound>=0 && !own.isPlaying() && size_t(c.sound)<own.size())
      own.play(c.sound);
    }
  }

void Sounds::tickAntenna(int car, bool under, float speed) {
  if(car!=antennaCar) {
    if(antenna.valid())
      antenna.stop();
    antenna    = rc::BankChannel();
    antennaCar = car;
    if(car>=0)
      if(auto def = carLib->bank().ofModel("Car"+std::to_string(car+1)+"Antenna"))
        antenna = rc::BankChannel(mix,*carLib,def->name);
    }
  if(!antenna.valid() || antenna.size()==0)
    return;
  if(!under || speed<=1.f) {
    if(antenna.isPlaying())
      antenna.play(-1);   // stops it, after its tail
    return;
    }
  antenna.setVolume(0,speed>35.f ? 0.f : (speed-1.f)*(15.f/34.f)-15.f);
  antenna.play(0);
  }

void Sounds::startMusic() {
  musicWanted = true;
  if(!useMusic)
    return;
  musicOn      = true;
  loadFailures = 0;
  nextTrack();
  }

void Sounds::setOptions(const rc::ConfigFile& f) {
  useSound    = f.integer("Sound","Use_Sound",1)!=0;
  useMusic    = f.integer("Sound","Use_BkSound",1)!=0;
  volume      = std::clamp(f.integer("Sound","SoundVolume",70),0,100);
  musicVolume = std::clamp(f.integer("Sound","SoundBkVolume",100),0,100);
  // 0x485d0e, 0x486c00: 0 only the tracks of "cycle 0", 1 only those of "cycle 1", -1 both
  music.setStyle(f.integer("Sound","Snd_Style",-1));
  if(!useMusic && musicOn) {
    musicOn = false;
    mix.setMusic(nullptr);
    }
  else if(useMusic && !musicOn && musicWanted)
    startMusic();
  applyVolumes();
  }

void Sounds::setMasterVolume(int32_t v) {
  master = std::clamp(v,0,100);
  applyVolumes();
  }

void Sounds::applyVolumes() {
  // the effects at (volume - 100) * 0.2 dB, the music at (volume - 120) * 0.4 dB: -6 and -8 dB as the game ships
  // (70, 100); the system's volume over both
  const float sys = master>0 ? 20.f*std::log10(float(master)/100.f) : -100.f;
  mix.setEffectsVolume(useSound ? (float(volume)-100.f)*0.2f+sys : -100.f);
  mix.setMusicVolume((float(musicVolume)-100.f-20.f)*0.4f+sys);
  }

void Sounds::stopMusic() {
  musicWanted = false;
  musicOn = false;
  mix.setMusic(nullptr);
  }

void Sounds::startFilm(std::unique_ptr<rc::Stream> s) {
  // IBasicAudio::put_Volume((SoundBkVolume - 100) * 40), -10000 (silence) from -3999 down
  const float sys = master>0 ? 20.f*std::log10(float(master)/100.f) : -100.f;
  mix.setMusicVolume(musicVolume>0 ? (float(musicVolume)-100.f)*0.4f+sys : -100.f);
  mix.setMusic(std::move(s));
  }

void Sounds::stopFilm() {
  mix.setMusic(nullptr);
  applyVolumes();
  }

void Sounds::nextTrack() {
  if(loading.valid())
    return;  // one is on its way already
  const auto* t = music.next();
  if(t==nullptr) {
    musicOn = false;
    return;
    }
  // reading a few MB of MP3 from the disk would stop the game for a moment, so a worker does it. RCCars.exe does
  // not stop for it either: its music thread (0x486d70) waits for the DirectShow graph's EC_COMPLETE and builds the
  // next track's graph itself (0x486350, 0x486690), and DirectShow streams and decodes the file on its own threads
  loadingFile = t->file;
  loading = std::async(std::launch::async,[this,path = std::filesystem::path("Tracks")/t->file]() {
    return std::make_unique<rc::Mp3Stream>(res.readFile(path));
    });
  }

void Sounds::tick() {
  if(loading.valid()) {
    if(loading.wait_for(std::chrono::seconds(0))!=std::future_status::ready)
      return;
    try {
      auto stream = loading.get();
      loadFailures = 0;
      if(musicOn) {
        mix.setMusic(std::move(stream));
        Log::i("music: ",loadingFile);
        }
      }
    catch(const std::exception& e) {
      // a track that cannot be read is skipped, but not forever
      Log::e("music \"",loadingFile,"\": ",e.what());
      if(++loadFailures>=music.tracks().size())
        musicOn = false;
      else if(musicOn)
        nextTrack();
      }
    return;
    }
  if(musicOn && mix.musicEnded())
    nextTrack();
  }
