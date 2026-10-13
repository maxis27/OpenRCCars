#include "ambient.h"

#include <cmath>
#include <string>

#include "formats/scene.h"
#include "formats/settings.h"

namespace rc {

AmbientSounds::AmbientSounds(Mixer& mixer, SoundLibrary& lib, const SbScene& scene, const SliderFile& ini) {
  // RCCars.exe reads them in 1/100 (0x527d80 -> 0x532410)
  delay     = ini.real("TimeDelay",    100.f)/100.f;
  delayDisp = ini.real("TimeDelayDisp", 25.f)/100.f;
  radius    = ini.real("Radius",         5.f)/100.f;
  activeRad = ini.real("ActiveRad",   2000.f)/100.f;

  // numbered from 1 until one is missing, at most 16 splashes and 100 waterfalls
  for(int i=1; i<=16; ++i) {
    const SbMarker* m = scene.marker("water_splash_"+std::to_string(i));
    if(m==nullptr)
      break;
    Splash s;
    s.marker  = m->position();
    s.spot    = s.marker;
    s.channel = BankChannel(mixer,lib,"water_splash");
    splashes.push_back(std::move(s));
    }
  for(int i=1; i<=100; ++i) {
    const SbMarker* m = scene.marker("WATER_FALL_"+std::to_string(i));
    if(m==nullptr)
      break;
    falls.push_back({m->position(),BankChannel(mixer,lib,"water_fall")});
    }
  }

void AmbientSounds::update(const Vec3& ear, const Vec3& right, float dt) {
  auto place = [&](BankChannel& ch, const Vec3& at) {
    const Vec3  to = at-ear;
    const float d  = to.length();
    ch.setPlace(d,d>1e-3f ? Vec3::dot(to,right)/d : 0.f);
    };
  std::uniform_real_distribution<float> unit(0.f,1.f);
  for(auto& s:splashes) {
    const float dx = s.marker.x-ear.x, dz = s.marker.z-ear.z;
    if(std::sqrt(dx*dx+dz*dz)>=activeRad) {
      if(s.active)
        s.channel.stop();
      s.active = false;
      continue;
      }
    if(!s.active) {
      s.active = true;
      s.timer  = 0;
      }
    s.timer -= dt;
    if(s.timer<0) {
      // the next splash: somewhere around the marker, after a while
      const float a = unit(rng)*6.2831853f;
      s.spot  = s.marker+Vec3{std::cos(a)*radius,0,std::sin(a)*radius};
      s.timer = delay+(unit(rng)*2.f-1.f)*delayDisp;
      }
    place(s.channel,s.spot);
    s.channel.play(0);
    }
  for(auto& f:falls) {
    place(f.channel,f.at);
    f.channel.play(0);
    }
  }

void AmbientSounds::stop() {
  for(auto& s:splashes) {
    s.channel.stop(true);
    s.active = false;
    }
  for(auto& f:falls)
    f.channel.stop(true);
  }

}
