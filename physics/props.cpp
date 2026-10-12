#include "props.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>

#include "btconvert.h"
#include "cargeometry.h"
#include "formats/settings.h"

using namespace rc;
using namespace rc::bt;

PropParams PropParams::parse(std::string_view script) {
  const auto keys = KeySections::parse(script);
  PropParams p;
  auto get = [&](std::string_view section, std::string_view key, float def) {
    auto& v = keys.get(section,key);
    return v.empty() ? def : v[0];
    };
  p.shape    = int32_t(get("Geom","type",float(p.shape)));
  p.comY     = get("Geom","shiftCenterMassOY",p.comY);
  p.mass     = std::max(get("Params","mass",p.mass),0.01f);
  p.bounce   = get("Params","coeffHookNorm",p.bounce);
  p.friction = get("Params","coeffFrict",p.friction);
  p.wind     = get("ExtForces","fWindBlow",0)!=0;
  return p;
  }

PropGeometry rc::propGeometry(const SbScene& sb, int32_t model, const Vec3& origin) {
  PropGeometry g;
  g.min = { 1e9f, 1e9f, 1e9f};
  g.max = {-1e9f,-1e9f,-1e9f};
  for(size_t mi=0; mi<sb.meshes.size(); ++mi) {
    auto& m = sb.meshes[mi];
    if(m.model!=model || m.positions.empty() || !carPart(sb,mi))
      continue;
    std::vector<bool> used(m.positions.size(),false);
    for(auto& f:m.faces) {
      if(f.pos.size()<3 || f.layer(LayerTag::Base)==nullptr)
        continue;
      for(auto pi:f.pos)
        if(pi<used.size())
          used[pi] = true;
      }
    for(size_t i=0; i<used.size(); ++i) {
      if(!used[i])
        continue;
      const Vec3 p = m.world.point(m.positions[i])-origin;
      g.points.push_back(p);
      g.min = {std::min(g.min.x,p.x),std::min(g.min.y,p.y),std::min(g.min.z,p.z)};
      g.max = {std::max(g.max.x,p.x),std::max(g.max.y,p.y),std::max(g.max.z,p.z)};
      }
    }
  return g;
  }

Prop::Prop(PhysicsWorld& world, const PropGeometry& g, const PropParams& prm, const Mat4& placed)
  : owner(world), start(placed) {
  // the placement may carry a scale (crabs are placed at 2x); bodies only take rotation + translation
  for(int c=0; c<3; ++c) {
    const Vec3 axis = Vec3{start.m[0][c],start.m[1][c],start.m[2][c]}.normalized();
    start.m[0][c] = axis.x; start.m[1][c] = axis.y; start.m[2][c] = axis.z;
    }

  const Vec3 center = (g.min+g.max)*0.5f;
  const Vec3 half   = (g.max-g.min)*0.5f;
  const int  type   = prm.shape;

  // centre of mass: in the middle of the box, `shiftCenterMassOY` above the model's origin
  com   = center;
  com.y = std::clamp(prm.comY,g.min.y,g.max.y);

  btTransform local;
  local.setIdentity();
  if(type==PropParams::Sphere) {
    child = std::make_unique<btSphereShape>(std::max({half.x,half.y,half.z})*S);
    local.setOrigin(toBt(center-com)*S);
    }
  else if(type==PropParams::Cylinder) {
    child = std::make_unique<btCylinderShape>(toBt(half)*S);
    local.setOrigin(toBt(center-com)*S);
    }
  else {
    // convex hull of the model: only the vertices on the hull
    auto hull = std::make_unique<btConvexHullShape>();
    for(auto& p:g.points)
      hull->addPoint(toBt(p-com)*S,false);
    hull->optimizeConvexHull();
    hull->recalcLocalAabb();
    child = std::move(hull);
    }
  shape = std::make_unique<btCompoundShape>();
  shape->addChildShape(local,child.get());

  btVector3 inertia(0,0,0);
  shape->calculateLocalInertia(prm.mass,inertia);
  motion = std::make_unique<btDefaultMotionState>();
  btRigidBody::btRigidBodyConstructionInfo ci(prm.mass,motion.get(),shape.get(),inertia);
  ci.m_friction        = 0.6f*prm.friction;
  ci.m_restitution     = prm.bounce;
  ci.m_rollingFriction = type==PropParams::Sphere ? 0.002f : 0.02f;
  ci.m_linearDamping   = 0.05f;
  ci.m_angularDamping  = 0.2f;
  rigid = std::make_unique<btRigidBody>(ci);
  rigid->setUserIndex(PhysicsWorld::PropTag);
  const float r = std::min({half.x,half.y,half.z});
  rigid->setCcdMotionThreshold(r*S*0.5f);
  rigid->setCcdSweptSphereRadius(r*S*0.8f);
  reset();
  owner.bullet().addRigidBody(rigid.get());
  }

Prop::~Prop() {
  owner.bullet().removeRigidBody(rigid.get());
  }

void Prop::reset() {
  const btTransform t = toBt(start*Mat4::translate(com));
  rigid->setWorldTransform(t);
  rigid->setInterpolationWorldTransform(t);
  motion->setWorldTransform(t);
  rigid->setLinearVelocity(btVector3(0,0,0));
  rigid->setAngularVelocity(btVector3(0,0,0));
  rigid->clearForces();
  rigid->activate(true);
  }

Mat4 Prop::pose() const {
  btTransform t;
  motion->getWorldTransform(t);
  return fromBt(t)*Mat4::translate(com*-1.f);
  }
