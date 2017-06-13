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

  initialise_resource_system(platform, state.resources, 2*1024*1024, 8*1024*1024, 64*1024*1024, 1);

  initialise_render_context(platform, state.rendercontext, 16*1024*1024, 0);

  state.camera.set_projection(state.fov*pi<float>()/180.0f, state.aspect);

  state.scene.initialise_component_storage<NameComponent>();
  state.scene.initialise_component_storage<TransformComponent>();
  state.scene.initialise_component_storage<SpriteComponent>();
  state.scene.initialise_component_storage<MeshComponent>();
  state.scene.initialise_component_storage<PointLightComponent>();
  state.scene.initialise_component_storage<ParticleSystemComponent>();

  auto core = state.assets.load(platform, "core.pack");

  if (!core)
    throw runtime_error("Core Assets Load Failure");

  if (core->magic != CoreAsset::magic || core->version != CoreAsset::version)
    throw runtime_error("Core Assets Version Mismatch");

  state.loader = state.resources.create<Sprite>(state.assets.find(CoreAsset::loader_image));

  state.debugfont = state.resources.create<Font>(state.assets.find(CoreAsset::debug_font));

  state.unitsphere = state.resources.create<Mesh>(state.assets.find(CoreAsset::unit_sphere));
  state.defaultmaterial = state.resources.create<Material>(state.assets.find(CoreAsset::default_material));

  state.sundirection = Vec3(0.3698f,-0.9245f,-0.09245f);
  state.sunintensity = Color3(8.0f, 7.56f, 7.88f);

  state.skybox = state.resources.create<SkyBox>(state.assets.find(CoreAsset::default_skybox));

  auto model = state.assets.load(platform, "sponza.pack");

  if (!model)
    throw runtime_error("Model Assets Load Failure");

  state.model = state.scene.load<Model>(platform, &state.resources, model);

  auto fire = state.assets.load(platform, "fire.pack");

  if (!fire)
    throw runtime_error("Fire Assets Load Failure");

  state.fire = state.resources.create<ParticleSystem>(state.assets.find(fire->id + 1));

  state.lights[0] = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(state.lights[0], Transform::translation(Vec3(4.85f, 1.35f, 1.45f))*Transform::rotation(Vec3(0, 0, 1), pi<float>()/2));
  state.scene.add_component<PointLightComponent>(state.lights[0], Color3(1.0f, 0.5f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));
  state.scene.add_component<ParticleSystemComponent>(state.lights[0], state.fire, ParticleSystemComponent::Visible);

  state.lights[1] = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(state.lights[1], Transform::translation(Vec3(4.85f, 1.35f, -2.20f))*Transform::rotation(Vec3(0, 0, 1), pi<float>()/2));
  state.scene.add_component<PointLightComponent>(state.lights[1], Color3(1.0f, 0.3f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));
  state.scene.add_component<ParticleSystemComponent>(state.lights[1], state.fire, ParticleSystemComponent::Visible);

  state.lights[2] = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(state.lights[2], Transform::translation(Vec3(-6.20f, 1.35f, -2.20f))*Transform::rotation(Vec3(0, 0, 1), pi<float>()/2));
  state.scene.add_component<PointLightComponent>(state.lights[2], Color3(1.0f, 0.5f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));
  state.scene.add_component<ParticleSystemComponent>(state.lights[2], state.fire, ParticleSystemComponent::Visible);

  state.lights[3] = state.scene.create<Entity>();
  state.scene.add_component<TransformComponent>(state.lights[3], Transform::translation(Vec3(-6.20f, 1.35f, 1.45f))*Transform::rotation(Vec3(0, 0, 1), pi<float>()/2));
  state.scene.add_component<PointLightComponent>(state.lights[3], Color3(1.0f, 0.4f, 0.0f), Attenuation(0.4f, 0.0f, 1.0f));
  state.scene.add_component<ParticleSystemComponent>(state.lights[3], state.fire, ParticleSystemComponent::Visible);

  auto envmaps = state.assets.load(platform, "sponza-env.pack");

  if (!envmaps)
    throw runtime_error("Envmap Assets Load Failure");

  state.envmaps[0] = make_tuple(Vec3(-0.625f, 2.45f, -0.35f), Vec3(28.0f, 5.0f, 4.8f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 0)));
  state.envmaps[1] = make_tuple(Vec3(-0.625f, 1.95f, 3.9f), Vec3(28.0f, 4.0f, 3.7f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 1)));
  state.envmaps[2] = make_tuple(Vec3(-0.625f, 1.95f, -4.6f), Vec3(28.0f, 4.0f, 3.7f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 2)));
  state.envmaps[3] = make_tuple(Vec3(0.0f, 9.0f, 0.0f), Vec3(30.0f, 10.0f, 15.0f), state.resources.create<EnvMap>(state.assets.find(envmaps->id + 3)));

//  state.skybox = state.resources.create<SkyBox>(state.assets.find(envmaps->id + 0));

  state.camera.lookat(Vec3(0, 1, 0), Vec3(1, 1, 0), Vec3(0, 1, 0));

  prefetch_core_assets(platform, state.assets);

  state.mode = GameState::Startup;
}


///////////////////////// buildgeometrylist /////////////////////////////////
void buildgeometrylist(PlatformInterface &platform, GameState &state, GeometryList &meshes)
{
  GeometryList::BuildState buildstate;

  if (meshes.begin(buildstate, state.rendercontext, state.resources))
  {
    auto frustum = state.camera.frustum();

    auto meshstorage = state.scene.system<MeshComponentStorage>();
    auto transformstorage = state.scene.system<TransformComponentStorage>();

    for(auto branch = meshstorage->tree().begin(), end = meshstorage->tree().end(); branch != end; ++branch)
    {
      if (intersects(frustum, branch.bound()))
      {
        if (contains(frustum, branch.bound()))
        {
          for(auto subtree = branch, end = next(branch); subtree != end; ++subtree)
          {
            for(auto &entity : subtree.items())
            {
              auto instance = meshstorage->get(entity);
              auto transform = transformstorage->get(entity);

              state.resources.request(platform, instance.mesh());
              state.resources.request(platform, instance.material());

              if (instance.mesh()->ready() && instance.material()->ready())
              {
                meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
              }
            }

            subtree.descend();
          }
        }
        else
        {
          for(auto &entity : branch.items())
          {
            auto instance = meshstorage->get(entity);

            if (intersects(frustum, instance.bound()))
            {
              auto transform = transformstorage->get(entity);

              state.resources.request(platform, instance.mesh());
              state.resources.request(platform, instance.material());

              if (instance.mesh()->ready() && instance.material()->ready())
              {
                meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
              }
            }
          }

          branch.descend();
        }
      }
    }

    for(auto &entity : meshstorage->dynamic())
    {
      auto instance = meshstorage->get(entity);

      if (intersects(frustum, instance.bound()))
      {
        auto transform = transformstorage->get(entity);

        state.resources.request(platform, instance.mesh());
        state.resources.request(platform, instance.material());

        if (instance.mesh()->ready() && instance.material()->ready())
        {
          meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
        }
      }
    }

    meshes.finalise(buildstate);
  }
}


///////////////////////// buildobjectlist ///////////////////////////////////
void buildobjectlist(PlatformInterface &platform, GameState &state, ForwardList &objects)
{
  ForwardList::BuildState buildstate;

  if (objects.begin(buildstate, state.rendercontext, state.resources))
  {
    auto frustum = state.camera.frustum();

    auto particlestorage = state.scene.system<ParticleSystemComponentStorage>();

    for(auto &entity : particlestorage->entities())
    {
      auto particles = particlestorage->get(entity);

      if (intersects(frustum, particles.bound()))
      {
        objects.push_particlesystem(buildstate, particles.system(), particles.instance());
      }
    }

    objects.finalise(buildstate);
  }
}


///////////////////////// buildcasterlist ///////////////////////////////////
void buildcasterlist(PlatformInterface &platform, GameState &state, CasterList &casters)
{
  CasterList::BuildState buildstate;

  if (casters.begin(buildstate, state.rendercontext, state.resources))
  {
    const float znear = 0.1f;
    const float zfar = state.rendercontext.shadows.shadowsplitfar;
    const float extrusion = 1000.0f;

    auto camerafrustum = state.camera.frustum(znear, zfar + 1.0f);

    auto lightpos = camerafrustum.centre() - extrusion * state.sundirection;

    auto lightview = Transform::lookat(lightpos, lightpos + state.sundirection, Vec3(0, 1, 0));

    auto invlightview = inverse(lightview);

    Vec3 mincorner(std::numeric_limits<float>::max());
    Vec3 maxcorner(std::numeric_limits<float>::lowest());

    for(size_t i = 1; i < 8; ++i)
    {
      auto corner = invlightview * camerafrustum.corners[i];

      mincorner = lml::min(mincorner, corner);
      maxcorner = lml::max(maxcorner, corner);
    }

    auto frustum = lightview * Frustum::orthographic(mincorner.x, mincorner.y, maxcorner.x, maxcorner.y, 0.1f, extrusion + maxcorner.z - mincorner.z);

    auto meshstorage = state.scene.system<MeshComponentStorage>();
    auto transformstorage = state.scene.system<TransformComponentStorage>();

    for(auto branch = meshstorage->tree().begin(), end = meshstorage->tree().end(); branch != end; ++branch)
    {
      if (intersects(frustum, branch.bound()))
      {
        if (contains(frustum, branch.bound()))
        {
          for(auto subtree = branch, end = next(branch); subtree != end; ++subtree)
          {
            for(auto &entity : subtree.items())
            {
              auto instance = meshstorage->get(entity);
              auto transform = transformstorage->get(entity);

              state.resources.request(platform, instance.mesh());
              state.resources.request(platform, instance.material());

              if (instance.mesh()->ready() && instance.material()->ready())
              {
                casters.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
              }
            }

            subtree.descend();
          }
        }
        else
        {
          for(auto &entity : branch.items())
          {
            auto instance = meshstorage->get(entity);

            if (intersects(frustum, instance.bound()))
            {
              auto transform = transformstorage->get(entity);

              state.resources.request(platform, instance.mesh());
              state.resources.request(platform, instance.material());

              if (instance.mesh()->ready() && instance.material()->ready())
              {
                casters.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
              }
            }
          }

          branch.descend();
        }
      }
    }

    for(auto &entity : meshstorage->dynamic())
    {
      auto instance = meshstorage->get(entity);

      if (intersects(frustum, instance.bound()))
      {
        auto transform = transformstorage->get(entity);

        state.resources.request(platform, instance.mesh());
        state.resources.request(platform, instance.material());

        if (instance.mesh()->ready() && instance.material()->ready())
        {
          casters.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
        }
      }
    }

    casters.finalise(buildstate);
  }
}


///////////////////////// buildlightlist ////////////////////////////////////
void buildlightlist(PlatformInterface &platform, GameState &state, LightList &lights)
{
  LightList::BuildState buildstate;

  if (lights.begin(buildstate, state.rendercontext, state.resources))
  {
    auto frustum = state.camera.frustum();

    auto lightstorage = state.scene.system<PointLightComponentStorage>();
    auto transformstorage = state.scene.system<TransformComponentStorage>();

    for(auto &entity : lightstorage->entities())
    {
      auto light = lightstorage->get(entity);
      auto transform = transformstorage->get(entity);

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

  state.writeframe->mode = state.mode;
  state.writeframe->time = state.time;

  if (state.mode == GameState::Startup)
  {
    asset_guard lock(state.assets);

    state.resources.request(platform, state.loader);
    state.resources.request(platform, state.debugfont);

    if (state.rendercontext.ready && state.loader->ready() && state.debugfont->ready())
    {
      state.mode = GameState::Load;
    }
  }

  if (state.mode == GameState::Load)
  {
    asset_guard lock(state.assets);

    int ready = 0, total = 0;

    request(platform, state.resources, state.fire, &ready, &total);
    request(platform, state.resources, state.skybox, &ready, &total);

    for(auto &envmap : state.envmaps)
    {
      request(platform, state.resources, get<2>(envmap), &ready, &total);
    }

    for(auto &entity : state.scene.entities<MeshComponent>())
    {
      auto instance = state.scene.get_component<MeshComponent>(entity);

      if (intersects(state.camera.frustum(), instance.bound()))
      {
        request(platform, state.resources, instance.mesh(), &ready, &total);
        request(platform, state.resources, instance.material(), &ready, &total);
      }
    }

    if (ready == total)
    {
      state.mode = GameState::Play;
    }
  }

  if (state.mode == GameState::Play)
  {
    bool inputaccepted = false;

    update_debug_overlay(input, &inputaccepted);

    if (!inputaccepted)
    {
      if (input.mousebuttons[GameInput::Left].state == true)
      {
        state.camera.yaw(1.5f * (state.lastmousex - input.mousex), Vec3(0, 1, 0));
        state.camera.pitch(1.5f * (state.lastmousey - input.mousey));
      }

      float speed = 0.02f;

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

    Color3 lampintensity = Color3(8.0f, 8.0f, 8.0f);
    DEBUG_MENU_VALUE("Scene/Lamp Intensity", &lampintensity, Color3(0.0f, 0.0f, 0.0f), Color3(16.0f, 16.0f, 16.0f));

    for(auto &light : state.lights)
    {
      auto lightcomponent = state.scene.get_component<PointLightComponent>(light);

      lightcomponent.set_intensity(lampintensity);
    }

    float floorroughness = 0.05f;
    DEBUG_MENU_VALUE("Scene/Floor Roughness", &floorroughness, 0.0f, 1.0f);

    if (auto model = state.scene.get<Model>(state.model))
    {
      state.resources.update(model->materials[8], Color4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f, floorroughness, 1.0f, 0.0f);
    }

    DEBUG_MENU_ENTRY("Lighting/Sun Direction", state.sundirection = normalise(debug_menu_value("Lighting/Sun Direction", state.sundirection, Vec3(-1), Vec3(1))))

    update_meshes(state.scene);
    update_particlesystems(state.scene, state.camera, dt);

    state.writeframe->camera = state.camera;

    asset_guard lock(state.assets);

    buildgeometrylist(platform, state, state.writeframe->geometry);
    buildobjectlist(platform, state, state.writeframe->objects);
    buildcasterlist(platform, state, state.writeframe->casters);
    buildlightlist(platform, state, state.writeframe->lights);
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

  while (state.readyframe.load()->time <= state.readframe->time)
    ;

  state.readframe = state.readyframe.exchange(state.readframe);

  BEGIN_TIMED_BLOCK(Render, Color3(0.0f, 0.2f, 1.0f))

  if (state.readframe->mode == GameState::Startup)
  {
    prepare_render_context(platform, state.rendercontext, state.assets);

    render_fallback(state.rendercontext, viewport, embeded::logo.data, embeded::logo.width, embeded::logo.height);
  }

  if (state.readframe->mode == GameState::Load)
  {
    RenderList renderlist(platform.renderscratchmemory, 8*1024*1024);

    SpriteList sprites;
    SpriteList::BuildState buildstate;

    if (sprites.begin(buildstate, state.rendercontext, state.resources))
    {
      sprites.viewport(buildstate, viewport);

      sprites.push_text(buildstate, Vec2(viewport.width/2 - state.debugfont->width("Loading...")/2, viewport.height/2 + state.debugfont->height()/2), state.debugfont->height(), state.debugfont, "Loading...");

      sprites.finalise(buildstate);
    }

    renderlist.push_sprites(sprites);

    RenderParams renderparams;
    renderparams.width = viewport.width;
    renderparams.height = viewport.height;
    renderparams.aspect = state.aspect;
    renderparams.ssrstrength = 1.0f;
    renderparams.ssaoscale = 0.0f;

    prepare_render_pipeline(state.rendercontext, renderparams);

    render(state.rendercontext, viewport, Camera(), renderlist, renderparams);
  }

  if (state.readframe->mode == GameState::Play)
  {
    auto &camera = state.readframe->camera;

    RenderParams renderparams;
    renderparams.width = viewport.width;
    renderparams.height = viewport.height;
    renderparams.aspect = state.aspect;
    renderparams.skybox = state.skybox;
    renderparams.sundirection = state.sundirection;
    renderparams.sunintensity = state.sunintensity;
    renderparams.skyboxorientation = Transform::rotation(Vec3(0, 1, 0), -0.1f*state.readframe->time);
    renderparams.ssrstrength = 1.0f;
    renderparams.ssaoscale = 0.0f;

    DEBUG_MENU_VALUE("Lighting/SSR Strength", &renderparams.ssrstrength, 0.0f, 80.0f);
    DEBUG_MENU_VALUE("Lighting/Bloom Strength", &renderparams.bloomstrength, 0.0f, 8.0f);

    prepare_render_pipeline(state.rendercontext, renderparams);

    RenderList renderlist(platform.renderscratchmemory, 8*1024*1024);

    renderlist.push_casters(state.readframe->casters);
    renderlist.push_geometry(state.readframe->geometry);
    renderlist.push_objects(state.readframe->objects);
    renderlist.push_lights(state.readframe->lights);

    for(auto &envmap : state.envmaps)
    {
      renderlist.push_environment(Transform::translation(get<0>(envmap)), get<1>(envmap), get<2>(envmap));
    }

    render_debug_overlay(state.rendercontext, state.resources, renderlist, viewport, state.debugfont);

    render(state.rendercontext, viewport, camera, renderlist, renderparams);
  }

  state.resources.release(state.readframe->resourcetoken);

  END_TIMED_BLOCK(Render)
}
