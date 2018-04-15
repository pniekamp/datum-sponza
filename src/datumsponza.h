//
// datumsponza.h
//

#pragma once

#include "datum.h"
#include "datum/asset.h"
#include "datum/math.h"
#include "datum/scene.h"
#include "datum/renderer.h"

//|---------------------- GameState -----------------------------------------
//|--------------------------------------------------------------------------

struct GameState
{
  using Vec3 = lml::Vec3;
  using Color3 = lml::Color3;
  using Transform = lml::Transform;
  using Attenuation = lml::Attenuation;

  GameState(StackAllocator<> const &allocator);

  const float fov = 60.0f;
  const float aspect = 1920.0f/1080.0f;

  enum { Startup, Load, Play } mode;

  float time = 0;

  Camera camera;

  Sprite const *loader;
  Font const *debugfont;
  Mesh const *unitsphere;
  Material const *defaultmaterial;
  SkyBox const *skybox;

  ParticleSystem const *fire;

  std::tuple<Vec3, Vec3, EnvMap const *> envmaps[4];

  AssetManager assets;

  ResourceManager resources;

  RenderContext rendercontext;

  lml::Vec3 sundirection;
  lml::Color3 sunintensity;

  Scene scene;

  Scene::EntityId model;
  Scene::EntityId lights[4];

  size_t resourcetoken = 0;
};


void datumsponza_init(DatumPlatform::PlatformInterface &platform);
void datumsponza_resize(DatumPlatform::PlatformInterface &platform, DatumPlatform::Viewport const &viewport);
void datumsponza_update(DatumPlatform::PlatformInterface &platform, DatumPlatform::GameInput const &input, float dt);
void datumsponza_render(DatumPlatform::PlatformInterface &platform, DatumPlatform::Viewport const &viewport);
