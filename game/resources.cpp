#include "resources.h"

#include <Tempest/Log>
#include <Tempest/Pixmap>

#include <algorithm>
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
  std::ifstream f(root/relative,std::ios::binary|std::ios::ate);
  if(!f)
    throw std::runtime_error("unable to open: "+(root/relative).string());
  // in one read: byte by byte through istreambuf_iterator a music track took a good part of a second
  const auto size = f.tellg();
  if(size<0)
    throw std::runtime_error("unable to read: "+(root/relative).string());
  std::vector<uint8_t> ret(static_cast<size_t>(size));
  f.seekg(0);
  if(!f.read(reinterpret_cast<char*>(ret.data()),std::streamsize(ret.size())))
    throw std::runtime_error("unable to read: "+(root/relative).string());
  return ret;
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

void Resources::setTextureQuality(int32_t q) {
  // 0x48f595: Config.gm's TexturePath ".\Textures.<n>", <n> searched from VIDEO_TexQual+1 down to 1 (Textures.1
  // has every texture, Textures.2 and .3 only the smaller copies of some)
  q = std::clamp(q,0,2);
  if(q==texQual)
    return;
  texQual = q;
  // 0x4c854b: RCCars.exe reads its textures again as soon as the video options are left
  if(cache.empty())
    return;
  device.waitIdle();
  for(auto& [name,tex]:cache) {
    if(tex==nullptr)
      continue;
    if(auto t = load(name))
      *tex = std::move(*t);
    }
  }

std::vector<std::string> Resources::textureDirs() const {
  std::vector<std::string> ret;
  for(int32_t n=texQual+1; n>=1; --n)
    ret.push_back("Textures."+std::to_string(n)+"\\");
  return ret;
  }

bool Resources::hasTexture(const std::string& name) const {
  for(auto& dir:textureDirs())
    if(archive.find(dir+name+".csi")!=nullptr)
      return true;
  return false;
  }

void Resources::preload(const std::vector<std::string>& names) {
  for(auto& n:names)
    if(hasTexture(n))
      texture(n);
  }

const Resources::Texture* Resources::texture(const std::string& name) {
  const std::string key = rc::PackFile::normalize(name);
  if(auto it = cache.find(key); it!=cache.end())
    return it->second.get();

  auto ret = load(name);
  if(ret==nullptr)
    Log::e("texture not found: \"",name,"\"");
  auto* p = ret.get();
  cache[key] = std::move(ret);
  return p;
  }

std::unique_ptr<Resources::Texture> Resources::load(const std::string& name) {
  for(auto& dir:textureDirs()) {
    auto e = archive.find(dir+name+".csi");
    if(e==nullptr)
      continue;
    try {
      return std::make_unique<Texture>(decode(archive.read(*e)));
      }
    catch(const std::exception& ex) {
      Log::e("texture \"",name,"\": ",ex.what());
      }
    break;
    }
  return nullptr;
  }
