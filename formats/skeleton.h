#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "scene.h"

namespace rc {

// The bones of an animated model (people.sb, AIChars.sb): every MESH node of the model, in scene order (parents
// first), and its animation sequences (ASEQ). See SbMesh / SbAnim / SbSequence for the data.
//
// A bone's local transform is T * Rz * Ry * Rx * S of either its ANIM for the sequence (rest pose overridden by
// the channels that have keys) or, without one, its 0x0b transform. The file's own pose (0x0b of every node) is
// the pose the skins are bound in; it is also the first frame of "Stand".
//   skinned vertex (model space) = sum of weight * pose[bone] * inverse(bind[bone]) * bind[mesh] * vertex
//   rigid mesh vertex            = pose[mesh] * vertex
class Skeleton {
  public:
    struct Bone {
      std::string name;
      size_t      mesh   = 0;    // SbScene::meshes
      int32_t     parent = -1;   // bones() index
      int32_t     node   = -1;   // SbMesh::node
      Mat4        bind;          // model space transform in the pose the skins were bound in (0x38)
      Mat4        bindInverse;
      };
    struct Sequence {
      std::string name;
      int32_t     index    = -1; // SbSequence::index: the ANIM of each bone
      float       duration = 0;  // s
      };

    Skeleton(const SbScene& scene, int32_t model);

    const SbScene&               scene() const { return sb; }
    int32_t                      model() const { return modelId; }
    const std::vector<Bone>&     bones() const { return boneList; }
    const std::vector<Sequence>& sequences() const { return seqList; }

    // sequences() index of a sequence by name (any case), -1 if the model has none of that name
    int32_t                      sequence(std::string_view name) const;
    // bones() index by name (any case) or by SbMesh::node, -1 if none
    int32_t                      bone(std::string_view name) const;
    int32_t                      boneOfNode(int32_t node) const;
    // bones() index of a scene mesh, -1 if it is not a bone of this model
    int32_t                      boneOfMesh(size_t mesh) const;

    // model space transform of every bone in sequences()[seq] at `time` s (clamped to the sequence), or in the
    // file's pose for seq < 0
    void                         pose(int32_t seq, float time, std::vector<Mat4>& out) const;

  private:
    const SbScene&        sb;
    int32_t               modelId = -1;
    std::vector<Bone>     boneList;
    std::vector<Sequence> seqList;
    std::vector<int32_t>  byMesh;   // SbScene::meshes index -> bone, -1
  };

}
