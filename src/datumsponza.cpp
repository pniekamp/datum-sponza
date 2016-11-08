//
// datumsponza.cpp
//

#include "datumsponza.h"
#include "fallback.h"
#include "datum/debug.h"

using namespace std;
using namespace lml;
using namespace DatumPlatform;


///////////////////////// GameState::Constructor ////////////////////////////
GameState::GameState(StackAllocator<> const &allocator)
  : assets(allocator),
    resources(&assets, allocator),
    scene(allocator)
{
  readframe = &renderframes[0];
  writeframe = &renderframes[1];
  readyframe = &renderframes[2];
}


///////////////////////// game_init /////////////////////////////////////////
void datumsponza_init(PlatformInterface &platform)
{
  cout << "Init" << endl;

  GameState &state = *new(allocate<GameState>(platform.gamememory)) GameState(platform.gamememory);

  assert(&state == platform.gamememory.data);

  initialise_asset_system(platform, state.assets, 64*1024, 128*1024*1024);

  initialise_resource_system(platform, state.resources, 2*1024*1024, 8*1024*1024, 64*1024*1024);

  initialise_resource_pool(platform, state.rendercontext.resourcepool, 16*1024*1024);

  state.camera.set_projection(state.fov*pi<float>()/180.0f, state.aspect);

  state.scene.initialise_component_storage<NameComponent>();
  state.scene.initialise_component_storage<TransformComponent>();
  state.scene.initialise_component_storage<SpriteComponent>();
  state.scene.initialise_component_storage<MeshComponent>();
  state.scene.initialise_component_storage<LightComponent>();

  state.assets.load(platform, "core.pack");

  state.loader = state.resources.create<Sprite>(state.assets.find(CoreAsset::loader_image));

  state.debugfont = state.resources.create<Font>(state.assets.find(CoreAsset::debug_font));

  state.unitsphere = state.resources.create<Mesh>(state.assets.find(CoreAsset::unit_sphere));
  state.defaultmaterial = state.resources.create<Material>(state.assets.find(CoreAsset::default_material));

  state.skybox = state.resources.create<SkyBox>(state.assets.find(CoreAsset::default_skybox));

  state.scene.load<Model>(platform, &state.resources, state.assets.load(platform, "sponza.pack"));

  auto light1 = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(light1, Transform::translation(Vec3(4.85f, 1.45f, 1.45f)));
  state.scene.add_component<PointLightComponent>(light1, Color3(1.0f, 0.5f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));

  auto light2 = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(light2, Transform::translation(Vec3(4.85f, 1.45f, -2.20f)));
  state.scene.add_component<PointLightComponent>(light2, Color3(1.0f, 0.3f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));

  auto light3 = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(light3, Transform::translation(Vec3(-6.20f, 1.45f, -2.20f)));
  state.scene.add_component<PointLightComponent>(light3, Color3(1.0f, 0.5f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));

  auto light4 = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(light4, Transform::translation(Vec3(-6.20f, 1.45f, 1.45f)));
  state.scene.add_component<PointLightComponent>(light4, Color3(1.0f, 0.4f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));

  auto envmaps = state.assets.load(platform, "sponza-env.pack");
  state.envmaps[0] = make_tuple(Vec3(-0.625f, 2.45f, -0.4f), Vec3(28.0f, 5.0f, 5.2f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 0)));
  state.envmaps[1] = make_tuple(Vec3(-0.625f, 1.95f, 4.1f), Vec3(28.0f, 4.0f, 4.1f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 1)));
  state.envmaps[2] = make_tuple(Vec3(-0.625f, 1.95f, -4.65f), Vec3(28.0f, 4, 3.6f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 2)));
  state.envmaps[3] = make_tuple(Vec3(0.0f, 9.0f, 0.0f), Vec3(30.0f, 10.0f, 15.0f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 3)));

//  state.skybox = state.resources.create<SkyBox>(state.assets.find(envmaps->id + 0));

  state.camera.lookat(Vec3(0, 1, 0), Vec3(1, 1, 0), Vec3(0, 1, 0));
}


///////////////////////// buildmeshlist /////////////////////////////////////
void buildmeshlist(PlatformInterface &platform, GameState &state, MeshList &meshes)
{
  MeshList::BuildState buildstate;

  if (meshes.begin(buildstate, platform, state.rendercontext, &state.resources))
  {
    auto frustum = state.camera.frustum();

    auto meshstorage = state.scene.system<MeshComponentStorage>();

    for(auto branch = meshstorage->tree().begin(), end = meshstorage->tree().end(); branch != end; ++branch)
    {
      if (intersects(frustum, branch.bound()))
      {
        if (contains(frustum, branch.bound()))
        {
          for(auto subtree = branch, end = next(branch); subtree != end; ++subtree)
          {
            for(auto &entity : *subtree)
            {
              auto instance = state.scene.get_component<MeshComponent>(entity);
              auto transform = state.scene.get_component<TransformComponent>(entity);

              meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
            }

            subtree.descend();
          }
        }
        else
        {
          for(auto &entity : *branch)
          {
            if (intersects(frustum, meshstorage->bound(entity)))
            {
              auto instance = state.scene.get_component<MeshComponent>(entity);
              auto transform = state.scene.get_component<TransformComponent>(entity);

              meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
            }
          }

          branch.descend();
        }
      }
    }

    for(auto &entity : meshstorage->dynamic())
    {
      if (intersects(frustum, meshstorage->bound(entity)))
      {
        auto instance = state.scene.get_component<MeshComponent>(entity);
        auto transform = state.scene.get_component<TransformComponent>(entity);

        meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
      }
    }

    meshes.finalise(buildstate);
  }
}


///////////////////////// buildlightlist ////////////////////////////////////
void buildlightlist(PlatformInterface &platform, GameState &state, LightList &lights)
{
  LightList::BuildState buildstate;

  if (lights.begin(buildstate, platform, state.rendercontext, &state.resources))
  {
    auto frustum = state.camera.frustum();

    for(auto &entity : state.scene.entities<PointLightComponent>())
    {
      auto light = state.scene.get_component<PointLightComponent>(entity);
      auto transform = state.scene.get_component<TransformComponent>(entity);

      if (intersects(frustum, Sphere(transform.world().translation(), light.range())))
      {
        lights.push_pointlight(buildstate, transform.world().translation(), light.range(), light.intensity(), light.attenuation());
      }
    }

    lights.finalise(buildstate);
  }
}


///////////////////////// game_update ///////////////////////////////////////
void datumsponza_update(PlatformInterface &platform, GameInput const &input, float dt)
{
  BEGIN_TIMED_BLOCK(Update, Color3(1.0f, 1.0f, 0.4f))

  GameState &state = *static_cast<GameState*>(platform.gamememory.data);

  state.time += dt;

  bool inputaccepted = false;

  update_debug_overlay(input, &inputaccepted);

  if (!inputaccepted)
  {
    if (input.mousebuttons[GameInput::Left].state == true)
    {
      state.camera.yaw(1.5f * (state.lastmousex - input.mousex), Vec3(0, 1, 0));
      state.camera.pitch(1.5f * (state.lastmousey - input.mousey));
    }

    float speed = 0.02;

    if (input.modifiers & GameInput::Shift)
      speed *= 10;

    if (input.controllers[0].move_up.state == true && !(input.modifiers & GameInput::Control))
      state.camera.offset(speed*Vec3(0, 0, -1));

    if (input.controllers[0].move_down.state == true && !(input.modifiers & GameInput::Control))
      state.camera.offset(speed*Vec3(0, 0, 1));

    if (input.controllers[0].move_up.state == true && (input.modifiers & GameInput::Control))
      state.camera.offset(speed*Vec3(0, 1, 0));

    if (input.controllers[0].move_down.state == true && (input.modifiers & GameInput::Control))
      state.camera.offset(speed*Vec3(0, -1, 0));

    if (input.controllers[0].move_left.state == true)
      state.camera.offset(speed*Vec3(-1, 0, 0));

    if (input.controllers[0].move_right.state == true)
      state.camera.offset(speed*Vec3(1, 0, 0));
  }

  state.lastmousex = input.mousex;
  state.lastmousey = input.mousey;
  state.lastmousez = input.mousez;

  state.camera = adapt(state.camera, state.rendercontext.luminance, 0.1f, 0.5f*dt);

  state.camera = normalise(state.camera);

  state.writeframe->time = state.time;
  state.writeframe->camera = state.camera;

  buildmeshlist(platform, state, state.writeframe->meshes);
  buildlightlist(platform, state, state.writeframe->lights);

  {
    CasterList::BuildState buildstate;

    if (state.writeframe->casters.begin(buildstate, platform, state.rendercontext, &state.resources))
    {
      for(auto &entity : state.scene.entities<MeshComponent>())
      {
        auto instance = state.scene.get_component<MeshComponent>(entity);
        auto transform = state.scene.get_component<TransformComponent>(entity);

        state.writeframe->casters.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
      }

      state.writeframe->casters.finalise(buildstate);
    }
  }

  state.writeframe->resourcetoken = state.resources.token();

  state.writeframe = state.readyframe.exchange(state.writeframe);

  END_TIMED_BLOCK(Update)
}


///////////////////////// game_render ///////////////////////////////////////
void datumsponza_render(PlatformInterface &platform, Viewport const &viewport)
{
  BEGIN_FRAME()

  GameState &state = *static_cast<GameState*>(platform.gamememory.data);

  if (!prepare_render_context(platform, state.rendercontext, &state.assets))
  {
    render_fallback(state.rendercontext, viewport, embeded::logo.data, embeded::logo.width, embeded::logo.height);
    return;
  }

  if (!state.skybox->ready())
  {
    asset_guard lock(&state.assets);

    state.resources.request(platform, state.skybox);
  }

  while (state.readyframe.load()->time <= state.readframe->time)
    ;

  state.readframe = state.readyframe.exchange(state.readframe);

  BEGIN_TIMED_BLOCK(Render, Color3(0.0f, 0.2f, 1.0f))

  auto &camera = state.readframe->camera;

  RenderList renderlist(platform.renderscratchmemory, 8*1024*1024);

  renderlist.push_meshes(state.readframe->meshes);
  renderlist.push_lights(state.readframe->lights);
  renderlist.push_casters(state.readframe->casters);

  for(auto &envmap : state.envmaps)
  {
    state.resources.request(platform, get<2>(envmap));

    renderlist.push_environment(Transform::translation(get<0>(envmap)), get<1>(envmap), get<2>(envmap));
  }

  RenderParams renderparams;
  renderparams.width = viewport.width;
  renderparams.height = viewport.height;
  renderparams.aspect = state.aspect;
  renderparams.skybox = state.skybox;
  renderparams.sundirection = normalise(Vec3(0.4f, -1.0f, -0.1f));
  renderparams.sunintensity = Color3(8.0f, 7.56f, 7.88f);
  renderparams.skyboxorientation = Transform::rotation(Vec3(0, 1, 0), -0.1*state.readframe->time);
  renderparams.ssaoscale = 0;

  DEBUG_MENU_ENTRY("Lighting/Sun Direction", renderparams.sundirection = normalise(debug_menu_value("Lighting/Sun Direction", renderparams.sundirection, Vec3(-1), Vec3(1))))
  DEBUG_MENU_VALUE("Lighting/SSR Strength", &renderparams.ssrstrength, 0.0f, 80.0f);
  DEBUG_MENU_VALUE("Lighting/Bloom Strength", &renderparams.bloomstrength, 0.0f, 8.0f);

  render_debug_overlay(platform, state.rendercontext, &state.resources, renderlist, viewport, state.debugfont);

  render(state.rendercontext, viewport, camera, renderlist, renderparams);

  state.resources.release(state.readframe->resourcetoken);

  END_TIMED_BLOCK(Render)
}
