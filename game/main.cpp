#include <Tempest/Application>
#include <Tempest/Device>
#include <Tempest/Log>
#include <Tempest/VulkanApi>
#if defined(_MSC_VER)
#include <Tempest/DirectX12Api>
#endif

#include <cstring>
#include <filesystem>
#include <memory>

#include "mainwindow.h"
#include "resources.h"

// usage: OpenRCCars [-g <game_dir>] [-l <track>] [-dev] [-dx12] [-validation]
//   game_dir defaults to the current directory (run it from the RC Cars installation folder)
//   -l: straight into a race on that track (beach_1 ...), without the menus
//   -dev: the developer mode of RCCars.exe (same switch, 0x4883dd), with our own debug keys on top
int main(int argc, const char** argv) {
  std::filesystem::path gameDir = std::filesystem::current_path();
  std::string           track;
  bool                  dx12    = false;
  bool                  debug   = false;
  bool                  dev     = false;

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
    }

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
    Resources  resources(device,gameDir);
    MainWindow wnd(device,resources,track,dev);
    return app.exec();
    }
  catch(const std::exception& e) {
    Tempest::Log::e("fatal: ",e.what());
    return 1;
    }
  }
