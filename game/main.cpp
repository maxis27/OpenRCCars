#include <Tempest/Application>
#include <Tempest/Device>
#include <Tempest/Log>
#include <Tempest/VulkanApi>
#if defined(_MSC_VER)
#include <Tempest/DirectX12Api>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include "formats/settings.h"

#include "mainwindow.h"
#include "resources.h"

// usage: OpenRCCars [-g <game_dir>] [-l <track>] [-dev] [-dx12] [-validation]
//                   [-host [players]] [-join <address>] [-port <port>] [-name <name>]
//   game_dir defaults to the current directory (run it from the RC Cars installation folder)
//   -l: straight into a race on that track (beach_1 ...), without the menus
//   -host: a network game on the track of -l (beach_1 without it), started once `players` are in (2 by default);
//   -join: the network game of the host at `address`; -port: its UDP port (2302); -name: the player's name there
//   -dev: the developer mode of RCCars.exe (same switch, 0x4883dd), with our own debug keys on top
// Full screen or a window as the game's RCCars.crs says ([Video] VIDEO_RunInWindow, the original's
// "Uruchom w oknie"): full screen without one, as RCCars.exe defaults to (0x4e2660)
int main(int argc, const char** argv) {
  std::filesystem::path gameDir = std::filesystem::current_path();
  std::string           track;
  bool                  dx12    = false;
  bool                  debug   = false;
  bool                  dev     = false;
  MainWindow::NetOptions net;

  for(int i=1; i<argc; ++i) {
    if(std::strcmp(argv[i],"-g")==0 && i+1<argc)
      gameDir = argv[++i];
    else if(std::strcmp(argv[i],"-l")==0 && i+1<argc)
      track = argv[++i];
    else if(std::strcmp(argv[i],"-dev")==0)
      dev = true;
    else if(std::strcmp(argv[i],"-dx12")==0)
      dx12 = true;
    else if(std::strcmp(argv[i],"-validation")==0)
      debug = true;
    else if(std::strcmp(argv[i],"-host")==0) {
      net.role = MainWindow::NetOptions::Role::Host;
      if(i+1<argc && argv[i+1][0]!='-')
        net.players = std::clamp(std::atoi(argv[++i]),1,rc::net::Session::DefaultMaxPlayers);
      }
    else if(std::strcmp(argv[i],"-join")==0 && i+1<argc) {
      net.role    = MainWindow::NetOptions::Role::Join;
      net.address = argv[++i];
      }
    else if(std::strcmp(argv[i],"-port")==0 && i+1<argc)
      net.port = uint16_t(std::atoi(argv[++i]));
    else if(std::strcmp(argv[i],"-name")==0 && i+1<argc)
      net.name = argv[++i];
    }
  if(net.role==MainWindow::NetOptions::Role::Host && track.empty())
    track = "beach_1";

  try {
    Tempest::Application app;
    const auto flags = debug ? Tempest::ApiFlags::Validation : Tempest::ApiFlags::NoFlags;
    std::unique_ptr<Tempest::AbstractGraphicsApi> api;
#if defined(_MSC_VER)
    if(dx12)
      api.reset(new Tempest::DirectX12Api{flags});
#endif
    (void)dx12;
    if(api==nullptr)
      api.reset(new Tempest::VulkanApi{flags});

    Tempest::Device device{*api};
    Tempest::Log::i("GPU: ",device.properties().name);

    if(!std::filesystem::exists(gameDir/"RCCars.pack")) {
      Tempest::Log::e("RCCars.pack not found in \"",gameDir.string(),"\" - run from the RC Cars folder or pass -g <folder>");
      return 1;
      }
    if(dev)
      Tempest::Log::i("-dev mode on");
    bool    inWindow = false;
    int32_t texQual  = 0;
    if(std::ifstream f{gameDir/"RCCars.crs",std::ios::binary}) {
      const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
      const auto crs = rc::ConfigFile::parse(data);
      inWindow = crs.integer("Video","VIDEO_RunInWindow",0)!=0;
      // the textures' quality, as RCCars.exe from the start (its device's setup, 0x48f595)
      texQual  = crs.integer("Video","VIDEO_TexQual",0);
      }
    Resources  resources(device,gameDir);
    resources.setTextureQuality(texQual);
    MainWindow wnd(device,resources,track,dev,inWindow,net);
    return app.exec();
    }
  catch(const std::exception& e) {
    Tempest::Log::e("fatal: ",e.what());
    return 1;
    }
  }
