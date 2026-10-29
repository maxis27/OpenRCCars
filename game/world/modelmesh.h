#pragma once

#include <Tempest/CommandBuffer>
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/Texture2d>
#include <Tempest/VertexBuffer>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/scene.h"
#include "levelmesh.h"

class Resources;

// One MODL of a scene on the GPU: a car from RCCarsDB/Car.sb ("Car1" Overkill, "Car2" Buggy, "Car3" Hummer;
// +Z forward, +Y up, about 0.4 m long) or an object placed on a track (stone.sb, people.sb ...).
// The body, every WHEEL_* node and the suspension's nodes are separate parts (rc::carNode), so wheels can spin
// and steer and the axles, arms and springs can move (rc::CarRig).
class ModelMesh {
  public:
    struct Part {
      std::string name;   // empty for the body, otherwise the node, e.g. WHEEL_FRONT_LEFT or FRONT_AXLE
      rc::Vec3    pivot;  // model space centre of the part (a wheel's hub); part vertices are relative to it
      };

    // other models than the cars (the road traffic of Roadcar.sb)
    struct Options {
      // nodes that are parts of their own (with everything below them), in this order (parts() 1, 2 ..); their
      // pivot is the node's origin. Empty: the cars' WHEEL_* nodes, pivot at the wheel's centre
      std::vector<std::string> parts;
      // textures worn instead of the model's own: {model's texture, the one to use}
      std::vector<std::pair<std::string,std::string>> textures;
      // nodes not drawn, with everything below them (the road cars' collision boxes)
      std::vector<std::string> hidden;
      // nodes of head lights whose texture holds them lit and unlit side by side (RCCars.exe 0x517460): drawn
      // with the texture coordinate `lightAxis` (0 u, 1 v) moved by 0.5 when they are switched the other way
      // than modelled (`modelledLit`)
      std::vector<std::string> lights;
      int                      lightAxis   = 0;
      bool                     modelledLit = false;
      };

    // how a car is fitted out (RCCars.exe 0x50bb60), -1 everywhere = as modelled
    struct CarLook {
      // another of the car's liveries: Textures*/car_askin<car><1..4>.csi (and car_bskin..), the model using 1;
      // skin 1 is the second one and so on (0x49fc80: car_askin<car+1><skin+1>)
      int32_t skin      = 0;
      // upgrade levels 0..3 (Scripts/upgrades.ini BOOSTERS, RESONATORS, TIRES): the booster_<boost+1> and
      // compressor_<resonator+1> add-ons (rc::CarAddOns, not on the LOD models), the tires' texture
      // tire<2|3>_<tires+1>, and while the boost is upgraded the wheels widened by 0.95, 1.07, 1.25, 1.35 by tires
      int32_t boost     = -1;
      int32_t resonator = -1;
      int32_t tires     = -1;
      bool    lod       = false; // a Car<n>LOD<n> model
      // the modelled ANTENNA node drawn; off where AntennaView draws the moving one
      bool    antenna   = true;
      };

    // RCCars.exe's light of a car (setLight)
    struct Light {
      bool     on = false;
      float    ambient = 1, direct = 0;
      rc::Vec3 toLight = {0,1,0};
      };

    // `origin`: model space point that becomes the origin (ObjectLibrary::Model::origin)
    ModelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
              const rc::Vec3& origin = {});
    ModelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
              const CarLook& look);
    ModelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
              const rc::Vec3& origin, const Options& options);

    // `world` places the model (rotation, translation and a uniform scale); `partPoses`, if given, places each part in model
    // space (index as in parts(); rotation, translation, a spring's stretch), default: translate(pivot) - the modelled
    // position. `lightsOn`: how the Options::lights are switched. `shade` 0..1 (rc::fx::FeatureShade): the textures
    // times it and no other light, the Options::lights as they are; negative: the light set by setLight
    void                     draw(Tempest::Encoder<Tempest::CommandBuffer>& enc, const rc::Mat4& viewProj,
                                  const LevelMesh::Push& frame, const rc::Mat4& world,
                                  const std::vector<rc::Mat4>* partPoses = nullptr, bool lightsOn = false,
                                  float shade = -1) const;

    // how dirty the car is, 0 clean .. 2 (Effects::dirt): its paint (car_askin<car>*, car_bskin<car>*) gets the
    // dirt_car<car>_1/_2 textures (with a/b for the two paints of cars 2 and 3) laid over it
    void                     setDirt(float level) { dirt = level; }
    // RCCars.exe's light of a car (rc::fx::CarLight): `ambient` + `direct` x the cosine towards `toLight` (world),
    // held at 1; ambient 1 and direct 0 show the textures as they are (the far LODs). Not set: the light all
    // models have
    void                     setLight(float ambient, float direct = 0, const rc::Vec3& toLight = {0,1,0}) {
      light = {true,ambient,direct,toLight};
      }
    const Light&             lighting() const { return light; }

    const std::string&       name()    const { return modelName; }
    const std::vector<Part>& parts()   const { return partList; }
    rc::Vec3                 bboxMin() const { return bbox[0]; }
    rc::Vec3                 bboxMax() const { return bbox[1]; }

  private:
    ModelMesh(Tempest::Device& device, Resources& res, const rc::SbScene& sb, std::string_view model,
              const rc::Vec3& origin, const Options& options, const CarLook& look);

    struct Batch {
      size_t                    part  = 0;
      size_t                    first = 0;
      size_t                    count = 0;
      const Tempest::Texture2d* tex   = nullptr;
      rc::AlphaMode             mode  = rc::AlphaMode::Opaque;
      const Tempest::Texture2d* dirt[2] = {};
      float                     envMap = 0; // LevelMesh::Push::envMap
      int8_t                    lit    = -1; // Options::lights: drawn only with the lights on (1) / off (0)
      };

    std::string                           modelName;
    std::vector<Part>                     partList;
    std::vector<Batch>                    batches;
    Tempest::VertexBuffer<LevelMesh::Vertex> vbo;
    ScenePipelines                        pso;
    const Tempest::Texture2d*             envTex = nullptr;
    rc::Vec3                              bbox[2];
    float                                 dirt = 0;
    Light                                 light;
  };
