#include "carshadow.h"

#include <Tempest/Log>

#include "formats/settings.h"
#include "game/resources.h"

using namespace Tempest;

CarShadow::CarShadow(Resources& res) {
  static const char* const Textures[] = {"shadows2", "shadows3", "shadows1"};
  rc::SliderFile ini;
  try {
    auto d = res.pack().read("Settings\\carRender.ini");
    ini = rc::SliderFile::parse(std::string_view(reinterpret_cast<const char*>(d.data()),d.size()));
    }
  catch(const std::exception& e) {
    Log::e("Settings\\carRender.ini: ",e.what());
    }
  static const int32_t Sizes[] = {36, 29, 42}, Shifts[] = {50, 50, 51};
  for(size_t i=0; i<3; ++i) {
    const std::string n = std::to_string(i+1);
    looks[i].size  = float(ini.raw("ShadowSize" +n,Sizes [i]))/100.f;
    looks[i].shift = float(ini.raw("ShadowShift"+n,Shifts[i]))/100.f - 0.5f;
    if(auto t = res.texture(Textures[i]))
      looks[i].texture = &t->tex;
    }
  }

SpotView::Spot CarShadow::spot(size_t car, const rc::Mat4& pose) const {
  const Look&    l = looks[std::min<size_t>(car,2)];
  SpotView::Spot s;
  s.spot.pos    = pose.translation() + pose.vector({0,0,1})*l.shift;
  s.spot.radius = l.size;
  // the textures run the car's length along v (0x5072f0 turns them by 90 degrees less the heading)
  s.spot.along  = pose.vector({1,0,0});
  s.texture     = l.texture;
  return s;
  }
