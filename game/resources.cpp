#include "resources.h"

#include <Tempest/Log>
#include <Tempest/Pixmap>

#include <cstring>
#include <fstream>
#include <iterator>

using namespace Tempest;

Resources::Resources(Device& device, std::filesystem::path gameDir)
  :device(device), root(std::move(gameDir)), archive(root/"RCCars.pack") {
  Pixmap pm(1,1,TextureFormat::RGBA8);
  std::memset(pm.data(),0xFF,4);
  fallback = device.texture(pm,false);
  Log::i("RCCars.pack: ",archive.entries().size()," files");
  }

std::vector<uint8_t> Resources::readFile(const std::filesystem::path& relative) const {
  std::ifstream f(root/relative,std::ios::binary);
  if(!f)
    throw std::runtime_error("unable to open: "+(root/relative).string());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
  }

Resources::Texture Resources::decode(const std::vector<uint8_t>& csi) {
  auto   img = rc::decodeCsi(csi.data(),csi.size());
  Pixmap pm(img.w,img.h,TextureFormat::RGBA8);
  std::memcpy(pm.data(),img.rgba.data(),img.rgba.size());

  Texture t;
  t.tex   = device.texture(pm,true);
  t.alpha = img.alpha;
  return t;
  }

const Resources::Texture* Resources::texture(const std::string& name) {
  const std::string key = rc::PackFile::normalize(name);
  if(auto it = cache.find(key); it!=cache.end())
    return it->second.get();

  std::unique_ptr<Texture> ret;
  for(const char* dir:{"Textures.1\\","Textures.2\\","Textures.3\\"}) {
    auto e = archive.find(std::string(dir)+name+".csi");
    if(e==nullptr)
      continue;
    try {
      ret = std::make_unique<Texture>(decode(archive.read(*e)));
      }
    catch(const std::exception& ex) {
      Log::e("texture \"",name,"\": ",ex.what());
      }
    break;
    }
  if(ret==nullptr)
    Log::e("texture not found: \"",name,"\"");
  auto* p = ret.get();
  cache[key] = std::move(ret);
  return p;
  }
