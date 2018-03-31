//
// Datum - environment map generator
//

//
// Copyright (c) 2016 Peter Niekamp
//

#include "platform.h"
#include "datum/asset.h"
#include "datum/renderer.h"
#include "datum/scene.h"
#include "assetpacker.h"
#include "ibl.h"
#include <leap.h>
#include <leap/pathstring.h>
#include <fstream>
#include <iostream>

using namespace std;
using namespace lml;
using namespace leap;
using namespace Vulkan;
using namespace DatumPlatform;

//|---------------------- Platform ------------------------------------------
//|--------------------------------------------------------------------------

class Platform : public PlatformInterface
{
  public:

    RenderDevice render_device() override;

    handle_t open_handle(const char *identifier) override;

    size_t read_handle(handle_t handle, uint64_t position, void *buffer, size_t bytes) override;

    void close_handle(handle_t handle) override;

    void submit_work(void (*func)(PlatformInterface &, void*, void*), void *ldata, void *rdata) override;

    void terminate() override;

  protected:

    RenderDevice renderdevice;

    WorkQueue m_workqueue;

    friend void initialise_platform(Platform &platform, size_t gamememorysize);
};

RenderDevice Platform::render_device()
{
  return renderdevice;
}

PlatformInterface::handle_t Platform::open_handle(const char *identifier)
{
  return new FileHandle(pathstring(identifier).c_str());
}

size_t Platform::read_handle(PlatformInterface::handle_t handle, uint64_t position, void *buffer, size_t bytes)
{
  return static_cast<FileHandle*>(handle)->read(position, buffer, bytes);
}

void Platform::close_handle(PlatformInterface::handle_t handle)
{
  delete static_cast<FileHandle*>(handle);
}

void Platform::submit_work(void (*func)(PlatformInterface &, void*, void*), void *ldata, void *rdata)
{
  m_workqueue.push([=]() { func(*this, ldata, rdata); });
}

void Platform::terminate()
{
}

void initialise_platform(Platform &platform, size_t gamememorysize)
{
  gamememory_initialise(platform.gamememory, new char[gamememorysize], gamememorysize);

  //
  // Vulkan
  //

  VkApplicationInfo appinfo = {};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "Datum Sponza";
  appinfo.pEngineName = "Datum";
  appinfo.apiVersion = VK_MAKE_VERSION(1, 0, 65);

  VkInstanceCreateInfo instanceinfo = {};
  instanceinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceinfo.pApplicationInfo = &appinfo;

  VkInstance instance;
  if (vkCreateInstance(&instanceinfo, nullptr, &instance) != VK_SUCCESS)
    throw runtime_error("Vulkan CreateInstance failed");

  uint32_t physicaldevicecount = 0;
  vkEnumeratePhysicalDevices(instance, &physicaldevicecount, nullptr);

  if (physicaldevicecount == 0)
    throw runtime_error("Vulkan EnumeratePhysicalDevices failed");

  vector<VkPhysicalDevice> physicaldevices(physicaldevicecount);
  vkEnumeratePhysicalDevices(instance, &physicaldevicecount, physicaldevices.data());

  for(uint32_t i = 0; i < physicaldevicecount; ++i)
  {
    VkPhysicalDeviceProperties physicaldevicesproperties;
    vkGetPhysicalDeviceProperties(physicaldevices[i], &physicaldevicesproperties);

    cout << "Vulkan Physical Device " << i << ": " << physicaldevicesproperties.deviceName << endl;
  }

  VkPhysicalDevice physicaldevice;
  physicaldevice = physicaldevices[0];

  uint32_t queuecount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicaldevice, &queuecount, nullptr);

  if (queuecount == 0)
    throw runtime_error("Vulkan vkGetPhysicalDeviceQueueFamilyProperties failed");

  vector<VkQueueFamilyProperties> queueproperties(queuecount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicaldevice, &queuecount, queueproperties.data());

  uint32_t queueindex = 0;
  while (queueindex < queuecount && !(queueproperties[queueindex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
    ++queueindex;

  float queuepriorities[] = { 0.0f, 0.0f };

  VkDeviceQueueCreateInfo queueinfo = {};
  queueinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueinfo.queueFamilyIndex = queueindex;
  queueinfo.queueCount = extentof(queuepriorities);
  queueinfo.pQueuePriorities = queuepriorities;

  VkPhysicalDeviceFeatures devicefeatures = {};
  devicefeatures.shaderClipDistance = true;
  devicefeatures.shaderCullDistance = true;
  devicefeatures.geometryShader = true;
  devicefeatures.shaderTessellationAndGeometryPointSize = true;
  devicefeatures.shaderStorageImageWriteWithoutFormat = true;
  devicefeatures.samplerAnisotropy = true;

  VkDeviceCreateInfo deviceinfo = {};
  deviceinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceinfo.queueCreateInfoCount = 1;
  deviceinfo.pQueueCreateInfos = &queueinfo;
  deviceinfo.pEnabledFeatures = &devicefeatures;

  VkDevice device;
  if (vkCreateDevice(physicaldevice, &deviceinfo, nullptr, &device) != VK_SUCCESS)
    throw runtime_error("Vulkan vkCreateDevice failed");

  VkQueue renderqueue;
  vkGetDeviceQueue(device, queueindex, 0, &renderqueue);

  platform.renderdevice.device = device;
  platform.renderdevice.physicaldevice = physicaldevices[0];
  platform.renderdevice.queues[0] = { renderqueue, queueindex };
}


//|---------------------- Renderer ------------------------------------------
//|--------------------------------------------------------------------------

class Renderer
{
  public:
    Renderer(StackAllocator<> const &allocator);

    AssetManager assets;
    ResourceManager resources;

    RenderParams renderparams;
    RenderContext rendercontext;

    void prepare();

    void render(Camera const &camera, PushBuffer const &renderables, void *bits);

  protected:

    Vulkan::CommandPool commandpool;
    Vulkan::CommandBuffer commandbuffer;
    Vulkan::Semaphore acquirecomplete;
    Vulkan::Semaphore rendercomplete;

    Vulkan::StorageBuffer transferbuffer;
    Vulkan::MemoryView<uint64_t> transfermemory;

    friend void initialise_renderer(Platform &platform, Renderer &renderer, int width, int height, size_t slotcount, size_t slabsize, size_t storagesize);
};

Renderer::Renderer(StackAllocator<> const &allocator)
  : assets(allocator), resources(assets, allocator)
{
}

void Renderer::prepare()
{
  prepare_render_pipeline(rendercontext, renderparams);
}

void Renderer::render(Camera const &camera, PushBuffer const &renderables, void *bits)
{
  using ::render;

  Viewport viewport;
  viewport.image = VK_NULL_HANDLE;
  viewport.acquirecomplete = acquirecomplete;
  viewport.rendercomplete = rendercomplete;

  signal_semaphore(rendercontext.vulkan, acquirecomplete);

  render(rendercontext, viewport, camera, renderables, renderparams);

  wait_semaphore(rendercontext.vulkan, rendercomplete);

  blit(rendercontext, rendercontext.colorbuffer, transferbuffer, 0);

  uint64_t *src = transfermemory;
  uint32_t *dst = (uint32_t*)bits;

  const __m128i zero = _mm_setzero_si128();
  const __m128i mask_s4 = _mm_set1_epi32(0x00008000);
  const __m128i mask_m4 = _mm_set1_epi32(0x000003FF);
  const __m128i mask_e4 = _mm_set1_epi32(0x00007C00);
  const __m128i bias_e4 = _mm_set1_epi32(0x0001C000);

  for(int y = renderparams.height-1; y >= 0; --y)
  {
    for(int x = 0; x < renderparams.width; ++x)
    {
      Color4 pixel;

      __m128i h4 = _mm_unpacklo_epi16(_mm_loadl_epi64((__m128i*)(src + y * renderparams.width + x)), zero);
      __m128i s4 = _mm_and_si128(h4, mask_s4);
      __m128i m4 = _mm_and_si128(h4, mask_m4);
      __m128i e4 = _mm_add_epi32(_mm_and_si128(h4, mask_e4), bias_e4);
      __m128i f4 = _mm_or_si128(_mm_slli_epi32(s4, 16), _mm_or_si128(_mm_slli_epi32(m4, 13), _mm_slli_epi32(e4, 13)));
      _mm_store_si128((__m128i*)&pixel, f4);

      *dst++ = rgbe(pixel);
    }
  }
}

void initialise_renderer(Platform &platform, Renderer &renderer, int width, int height, size_t slotcount, size_t slabsize, size_t storagesize)
{
  // Resources

  initialise_asset_system(platform, renderer.assets, slotcount, slabsize);

  initialise_resource_system(platform, renderer.resources, storagesize/8, storagesize/4, storagesize/2, 0);

  initialise_render_context(platform, renderer.rendercontext, storagesize, 0);

  auto core = renderer.assets.load(platform, "core.pack");

  if (!core)
    throw runtime_error("Core Assets Load Failure");

  if (core->magic != CoreAsset::magic || core->version != CoreAsset::version)
    throw runtime_error("Core Assets Version Mismatch");

  // Renderer

  renderer.renderparams.width = width;
  renderer.renderparams.height = height;
  renderer.renderparams.aspect = (float)width / (float)height;

  while (!prepare_render_context(platform, renderer.rendercontext, renderer.assets))
    ;

  renderer.commandpool = create_commandpool(renderer.rendercontext.vulkan, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  renderer.commandbuffer = allocate_commandbuffer(renderer.rendercontext.vulkan, renderer.commandpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  renderer.acquirecomplete = create_semaphore(renderer.rendercontext.vulkan);
  renderer.rendercomplete = create_semaphore(renderer.rendercontext.vulkan);

  renderer.transferbuffer = create_transferbuffer(renderer.rendercontext.vulkan, width * height * sizeof(uint64_t));
  renderer.transfermemory = map_memory<uint64_t>(renderer.rendercontext.vulkan, renderer.transferbuffer, 0, renderer.transferbuffer.size);
}


//|---------------------- main ----------------------------------------------
//|--------------------------------------------------------------------------

void image_render_envmap(Renderer &renderer, Vec3 position, float exposure, PushBuffer const &renderables, int width, int height, void *bits)
{
  Camera camera;
  camera.set_exposure(exposure);
  camera.set_projection(pi<float>()/2, 1);
  camera.set_position(position);

  uint32_t *dst = (uint32_t*)bits;

  // Right

  camera.lookat(camera.position() + Vec3(1, 0, 0), Vec3(0, 1, 0));
  renderer.render(camera, renderables, dst);
  dst += width * height;

  // Left

  camera.lookat(camera.position() + Vec3(-1, 0, 0), Vec3(0, 1, 0));
  renderer.render(camera, renderables, dst);
  dst += width * height;

  // Down

  camera.lookat(camera.position() + Vec3(0, -1, 0), Vec3(0, 0, -1));
  renderer.render(camera, renderables, dst);
  dst += width * height;

  // Up

  camera.lookat(camera.position() + Vec3(0, 1, 0), Vec3(0, 0, 1));
  renderer.render(camera, renderables, dst);
  dst += width * height;

  // Forward

  camera.lookat(camera.position() + Vec3(0, 0, -1), Vec3(0, 1, 0));
  renderer.render(camera, renderables, dst);
  dst += width * height;

  // Backwards

  camera.lookat(camera.position() + Vec3(0, 0, 1), Vec3(0, 1, 0));
  renderer.render( camera, renderables, dst);
  dst += width * height;
}


int main(int argc, char **argv)
{
  cout << "EnvMap Generator" << endl;

  try
  {
    Platform platform;

    initialise_platform(platform, 256*1024*1024);

    Renderer renderer(platform.gamememory);

    initialise_renderer(platform, renderer, 128, 128, 64*1024, 128*1024*1024, 16*1024*1024);

    renderer.renderparams.sunintensity = Color3(0, 0, 0);
    renderer.renderparams.ssrstrength = 0;
    renderer.renderparams.bloomstrength = 0;

    Scene scene(platform.gamememory);

    scene.initialise_component_storage<TransformComponent>();
    scene.initialise_component_storage<MeshComponent>();
    scene.initialise_component_storage<PointLightComponent>();

    auto model = scene.load<Model>(platform, &renderer.resources, renderer.assets.load(platform, "sponza.pack"));

    for(auto &mesh : scene.get<Model>(model)->meshes)
    {
      asset_guard lock(renderer.assets);

      while (mesh && !mesh->ready())
        renderer.resources.request(platform, mesh);
    }

    for(auto &texture : scene.get<Model>(model)->textures)
    {
      asset_guard lock(renderer.assets);

      while (texture && !texture->ready())
        renderer.resources.request(platform, texture);
    }

    for(auto &material : scene.get<Model>(model)->materials)
    {
      asset_guard lock(renderer.assets);

      while (material && !material->ready())
        renderer.resources.request(platform, material);
    }

    renderer.prepare();

    RenderList renderlist(platform.gamememory, 1*1024*1024);

    {
      GeometryList geometry;
      GeometryList::BuildState buildstate;

      if (geometry.begin(buildstate, renderer.rendercontext, renderer.resources))
      {       
        for(auto &entity : scene.entities<MeshComponent>())
        {
          auto instance = scene.get_component<MeshComponent>(entity);
          auto transform = scene.get_component<TransformComponent>(entity);

          geometry.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
        }

        geometry.finalise(buildstate);
      }

      renderlist.push_geometry(geometry);
    }

    {
      LightList lights;
      LightList::BuildState buildstate;

      if (lights.begin(buildstate, renderer.rendercontext, renderer.resources))
      {
        for(auto &entity : scene.entities<PointLightComponent>())
        {
          auto light = scene.get_component<PointLightComponent>(entity);
          auto transform = scene.get_component<TransformComponent>(entity);

          lights.push_pointlight(buildstate, transform.world().translation(), light.range(), light.intensity(), light.attenuation());
        }

        lights.finalise(buildstate);
      }

      renderlist.push_lights(lights);
    }

    cout << "Generating..." << endl;

    ofstream fout("sponza-env.pack", ios::binary | ios::trunc);

    write_header(fout);

    int width = renderer.renderparams.width;
    int height = renderer.renderparams.height;
    int layers = 6;
    int levels = 8;

    vector<char> payload(image_datasize(width, height, layers, levels));

    image_render_envmap(renderer, Vec3(-0.625f, 2.45f, -0.35f), 0.4f, renderlist, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 0, width, height, layers, levels, PackImageHeader::rgbe, payload.data());

    image_render_envmap(renderer, Vec3(-0.625f, 1.95f, 3.95f), 0.2f, renderlist, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 1, width, height, layers, levels, PackImageHeader::rgbe, payload.data());

    image_render_envmap(renderer, Vec3(-0.625f, 1.95f, -4.65f), 0.2f, renderlist, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 2, width, height, layers, levels, PackImageHeader::rgbe, payload.data());

    image_render_envmap(renderer, Vec3(0.0f, 9.0f, 0.0f), 0.5f, renderlist, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 3, width, height, layers, levels, PackImageHeader::rgbe, payload.data());

    write_chunk(fout, "HEND", 0, nullptr);

    fout.close();
  }
  catch(exception &e)
  {
    cerr << "Critical Error:" << e.what() << endl;
  }
}
