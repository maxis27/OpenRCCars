#include "skeleton.h"

#include <algorithm>
#include <cctype>

using namespace rc;

static bool sameName(std::string_view a, std::string_view b) {
  if(a.size()!=b.size())
    return false;
  for(size_t i=0; i<a.size(); ++i)
    if(std::tolower(uint8_t(a[i]))!=std::tolower(uint8_t(b[i])))
      return false;
  return true;
  }

// T * Rz * Ry * Rx * S from position, rotation (degrees) and scale
static Mat4 compose(const float pos[3], const float rot[3], const float scl[3]) {
  return Mat4::translate({pos[0],pos[1],pos[2]}) *
         Mat4::rotate(2,rot[2]) * Mat4::rotate(1,rot[1]) * Mat4::rotate(0,rot[0]) *
         Mat4::scale({scl[0],scl[1],scl[2]});
  }

Skeleton::Skeleton(const SbScene& scene, int32_t model)
  :sb(scene), modelId(model) {
  byMesh.assign(scene.meshes.size(),-1);
  for(size_t i=0; i<scene.meshes.size(); ++i) {
    auto& m = scene.meshes[i];
    if(m.model!=model)
      continue;
    Bone b;
    b.name   = m.name;
    b.mesh   = i;
    b.node   = m.node;
    b.parent = m.parent>=0 && size_t(m.parent)<byMesh.size() ? byMesh[size_t(m.parent)] : -1;
    // skins are bound in the 0x38 pose (arms out), not in the file's current pose (0x0b)
    const Mat4 local = m.hasBind ? m.bindLocal : m.local;
    b.bind   = b.parent>=0 ? boneList[size_t(b.parent)].bind*local : local;
    b.bindInverse = b.bind.inverse();
    byMesh[i] = int32_t(boneList.size());
    boneList.push_back(std::move(b));
    }
  for(auto& s:scene.sequences) {
    if(s.model!=model)
      continue;
    seqList.push_back({s.name,s.index,s.duration()});
    }
  }

int32_t Skeleton::sequence(std::string_view name) const {
  for(size_t i=0; i<seqList.size(); ++i)
    if(sameName(seqList[i].name,name))
      return int32_t(i);
  return -1;
  }

int32_t Skeleton::bone(std::string_view name) const {
  for(size_t i=0; i<boneList.size(); ++i)
    if(sameName(boneList[i].name,name))
      return int32_t(i);
  return -1;
  }

int32_t Skeleton::boneOfNode(int32_t node) const {
  for(size_t i=0; i<boneList.size(); ++i)
    if(boneList[i].node==node)
      return int32_t(i);
  return -1;
  }

int32_t Skeleton::boneOfMesh(size_t mesh) const {
  return mesh<byMesh.size() ? byMesh[mesh] : -1;
  }

void Skeleton::pose(int32_t seq, float time, std::vector<Mat4>& out) const {
  out.resize(boneList.size());
  const int32_t index = seq>=0 && size_t(seq)<seqList.size() ? seqList[size_t(seq)].index : -1;
  for(size_t i=0; i<boneList.size(); ++i) {
    const Bone&   b = boneList[i];
    const SbMesh& m = sb.meshes[b.mesh];
    Mat4 local = m.local;
    if(index>=0 && size_t(index)<m.anims.size() && m.anims[size_t(index)].valid) {
      const SbAnim& a = m.anims[size_t(index)];
      float v[9];   // pos, rotation, scale
      std::copy(a.rest,a.rest+9,v);
      for(auto& c:a.channels)
        v[c.target] = c.value(time);
      local = compose(v,v+3,v+6);
      }
    out[i] = b.parent>=0 ? out[size_t(b.parent)]*local : local;
    }
  }
