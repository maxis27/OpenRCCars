#pragma once

#include <array>
#include <memory>

#include "modelmesh.h"

namespace rc { class PackFile; }

// A car as RCCars.exe draws it: its full model near the camera and the Car<n>LOD1, Car<n>LOD2 models further
// away (0x501c10 picks, 0x4fcd8d loads them), each fitted out the same way (0x50bb60).
class CarModel {
  public:
    // how far from the camera the LOD models take over (Settings\LOD.ini, RCCars.exe 0x4f98e0)
    struct Distances {
      float lod1 = 7.96f;
      float lod2 = 8.87f;
      static Distances load(rc::PackFile& pack);
      };

    // `car` 0..2: Car1 (Overkill) .. Car3 (Hummer)
    CarModel(Tempest::Device& device, Resources& res, const rc::SbScene& sb, size_t car,
             const ModelMesh::CarLook& look);

    // the full model: its parts and size are the car's
    ModelMesh&       full() { return *lods[0]; }
    const ModelMesh& full() const { return *lods[0]; }
    // the one to draw `distance` from the camera
    ModelMesh&       at(float distance, const Distances& d);

  private:
    std::array<std::unique_ptr<ModelMesh>,3> lods;
  };
