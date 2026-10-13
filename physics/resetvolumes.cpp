#include "resetvolumes.h"

#include "formats/settings.h"

using namespace rc;

ResetVolumes::ResetVolumes(const SbScene& track) {
  for(auto& v:track.volumes) {
    if(v.name.rfind("car_reset",0)!=0 || list.size()>=MaxVolumes)
      continue;
    Volume rv;
    rv.volume = &v;
    const auto  keys = KeySections::parse(v.script);
    const auto& ms   = keys.get("Params","FilterTime");
    if(!ms.empty())
      rv.filter = ms[0]*0.001f;
    list.push_back(rv);
    }
  }

bool ResetVolumes::tick(const Vec3& at, float dt) {
  int now = -1;
  for(size_t i=0; i<list.size() && now<0; ++i)
    if(list[i].volume->contains(at))
      now = int(i);
  // entering one (or moving into another) starts the timer
  if(now!=inside) {
    inside = now;
    timer  = 0;
    return false;
    }
  if(now<0)
    return false;
  timer += dt;
  if(timer<=list[size_t(now)].filter)
    return false;
  reset();
  return true;
  }
