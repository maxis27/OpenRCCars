#pragma once

#include <Tempest/Device>
#include <Tempest/Texture2d>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/csi.h"
#include "formats/pack.h"

// Access to the original game installation: RCCars.pack (encrypted archive) + loose files (RCCarsDB/*.sb).
class Resources {
  public:
    struct Texture {
      Tempest::Texture2d tex;
      rc::AlphaMode      alpha = rc::AlphaMode::Opaque;
      };

    Resources(Tempest::Device& device, std::filesystem::path gameDir);

    const std::filesystem::path& gameDir() const { return root; }
    rc::PackFile&                pack()          { return archive; }

    std::vector<uint8_t>         readFile(const std::filesystem::path& relative) const;

    // texture by name, searched in Textures.1 (best quality) .. Textures.3; nullptr if not found
    const Texture*               texture(const std::string& name);
    // whether there is such a texture, without reading it
    bool                         hasTexture(const std::string& name) const;
    // reads the textures there are of `names` now, quietly skipping the missing ones: RCCars.exe reads all it may
    // need while the game stands still (at the start, on the loading screen), never while it runs
    void                         preload(const std::vector<std::string>& names);
    // texture from raw .csi data (e.g. lightmaps embedded in a scene); not cached
    Texture                      decode(const std::vector<uint8_t>& csi);

    const Tempest::Texture2d&    white() const { return fallback; }

  private:
    Tempest::Device&                                          device;
    std::filesystem::path                                     root;
    rc::PackFile                                              archive;
    Tempest::Texture2d                                        fallback;
    std::unordered_map<std::string,std::unique_ptr<Texture>> cache;
  };
