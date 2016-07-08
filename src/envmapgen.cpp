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

    void read_handle(handle_t handle, uint64_t position, void *buffer, size_t bytes) override;

    void close_handle(handle_t handle) override;

    void submit_work(void (*func)(PlatformInterface &, void*, void*), void *ldata, void *rdata) override;

    void terminate() override;

  public:

    Viewport viewport;

    void *viewportmemory;

    void render(RenderContext &context, Camera const &camera, PushBuffer const &renderables, RenderParams const &params);

  protected:

    VulkanDevice vulkan;

    Vulkan::Image backbuffer;
    Vulkan::Semaphore acquirecomplete;
    Vulkan::Semaphore rendercomplete;
    Vulkan::TransferBuffer transferbuffer;

    WorkQueue m_workqueue;

    friend void initialise_platform(Platform &platform, int width, int height, size_t gamememorysize);
};

RenderDevice Platform::render_device()
{
  return { vulkan.physicaldevice, vulkan.device };
}

PlatformInterface::handle_t Platform::open_handle(const char *identifier)
{
  return new FileHandle(pathstring(identifier).c_str());
}

void Platform::read_handle(PlatformInterface::handle_t handle, uint64_t position, void *buffer, size_t bytes)
{
  static_cast<FileHandle*>(handle)->read(position, buffer, bytes);
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

void Platform::render(RenderContext &context, Camera const &camera, PushBuffer const &renderables, RenderParams const &params)
{
  VkSubmitInfo acquireinfo = {};
  acquireinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  acquireinfo.signalSemaphoreCount = 1;
  acquireinfo.pSignalSemaphores = &viewport.acquirecomplete;

  vkQueueSubmit(vulkan.queue, 1, &acquireinfo, VK_NULL_HANDLE);

  ::render(context, viewport, camera, renderables, params);

  CommandPool commandpool = create_commandpool(vulkan, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

  CommandBuffer commandbuffer = allocate_commandbuffer(vulkan, commandpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  begin(vulkan, commandbuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  setimagelayout(commandbuffer, viewport.image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

  blit(commandbuffer, viewport.image, 0, 0, viewport.width, viewport.height, transferbuffer, 0, viewport.width, viewport.height);

  setimagelayout(commandbuffer, viewport.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

  end(vulkan, commandbuffer);

  VkPipelineStageFlags waitdststagemask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo presentinfo = {};
  presentinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  presentinfo.pWaitDstStageMask = &waitdststagemask;
  presentinfo.commandBufferCount = 1;
  presentinfo.pCommandBuffers = commandbuffer.data();
  presentinfo.waitSemaphoreCount = 1;
  presentinfo.pWaitSemaphores = &viewport.rendercomplete;

  vkQueueSubmit(vulkan.queue, 1, &presentinfo, VK_NULL_HANDLE);

  vkQueueWaitIdle(vulkan.queue);
}

void initialise_platform(Platform &platform, int width, int height, size_t gamememorysize)
{
  gamememory_initialise(platform.gamememory, new char[gamememorysize], gamememorysize);

  //
  // Vulkan
  //

  VkApplicationInfo appinfo = {};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "Datum Sponza";
  appinfo.pEngineName = "Datum";
  appinfo.apiVersion = VK_MAKE_VERSION(1, 0, 8);

  const char *validationlayers[] = { "VK_LAYER_LUNARG_standard_validation" };
  const char *instanceextensions[] = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

  VkInstanceCreateInfo instanceinfo = {};
  instanceinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceinfo.pApplicationInfo = &appinfo;
  instanceinfo.enabledExtensionCount = std::extent<decltype(instanceextensions)>::value;
  instanceinfo.ppEnabledExtensionNames = instanceextensions;
  instanceinfo.enabledLayerCount = std::extent<decltype(validationlayers)>::value;
  instanceinfo.ppEnabledLayerNames = validationlayers;

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

  array<float, 1> queuepriorities = { 0.0f };

  VkDeviceQueueCreateInfo queueinfo = {};
  queueinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueinfo.queueFamilyIndex = queueindex;
  queueinfo.queueCount = 2;
  queueinfo.pQueuePriorities = queuepriorities.data();

  VkPhysicalDeviceFeatures devicefeatures = {};
  devicefeatures.shaderClipDistance = true;
  devicefeatures.shaderCullDistance = true;
  devicefeatures.geometryShader = true;
  devicefeatures.shaderTessellationAndGeometryPointSize = true;
  devicefeatures.shaderStorageImageWriteWithoutFormat = true;

  VkDeviceCreateInfo deviceinfo = {};
  deviceinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceinfo.queueCreateInfoCount = 1;
  deviceinfo.pQueueCreateInfos = &queueinfo;
  deviceinfo.pEnabledFeatures = &devicefeatures;
  deviceinfo.enabledLayerCount = std::extent<decltype(validationlayers)>::value;
  deviceinfo.ppEnabledLayerNames = validationlayers;

  VkDevice device;
  if (vkCreateDevice(physicaldevice, &deviceinfo, nullptr, &device) != VK_SUCCESS)
    throw runtime_error("Vulkan vkCreateDevice failed");

  initialise_vulkan_device(&platform.vulkan, physicaldevices[0], device, 0);

  // Debug

  static auto debugmessagecallback = [](VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objtype, uint64_t srcobject, size_t location, int32_t msgcode, const char *layerprefix, const char *msg, void *userdata) -> VkBool32 {
    cout << msg << endl;
    return false;
  };

  auto VkCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");

  VkDebugReportCallbackCreateInfoEXT debugreportinfo = {};
  debugreportinfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
  debugreportinfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugmessagecallback;
  debugreportinfo.pUserData = nullptr;
  debugreportinfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;// | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;

  VkDebugReportCallbackEXT debugreportcallback;
  VkCreateDebugReportCallback(instance, &debugreportinfo, nullptr, &debugreportcallback);

  // Viewport Image

  VkImageCreateInfo imageinfo = {};
  imageinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageinfo.imageType = VK_IMAGE_TYPE_2D;
  imageinfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  imageinfo.extent.width = width;
  imageinfo.extent.height = height;
  imageinfo.extent.depth = 1;
  imageinfo.mipLevels = 1;
  imageinfo.arrayLayers = 1;
  imageinfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageinfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageinfo.flags = 0;

  platform.backbuffer = create_image(platform.vulkan, imageinfo);

  setimagelayout(platform.vulkan, platform.backbuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

  platform.acquirecomplete = create_semaphore(platform.vulkan);
  platform.rendercomplete = create_semaphore(platform.vulkan);

  platform.transferbuffer = create_transferbuffer(platform.vulkan, width * height * 4*sizeof(float));

  platform.viewportmemory = map_memory<void*>(platform.vulkan, platform.transferbuffer, 0, platform.transferbuffer.size);

  // Viewport

  platform.viewport.x = 0;
  platform.viewport.y = 0;
  platform.viewport.width = width;
  platform.viewport.height = height;
  platform.viewport.image = platform.backbuffer;
  platform.viewport.acquirecomplete = platform.acquirecomplete;
  platform.viewport.rendercomplete = platform.rendercomplete;
}


//|---------------------- main ----------------------------------------------
//|--------------------------------------------------------------------------

void image_render_envmap(Platform &platform, RenderContext &context, Vec3 position, PushBuffer const &renderables, RenderParams const &params, int width, int height, void *bits)
{
  Camera camera;
  camera.set_exposure(1);
  camera.set_projection(pi<float>()/2, 1);
  camera.set_position(position);

  Color4 *src = (Color4*)(platform.viewportmemory);
  uint32_t *dst = (uint32_t*)bits;

  // Right

  camera.lookat(camera.position() + Vec3(1, 0, 0), Vec3(0, 1, 0));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }

  // Left

  camera.lookat(camera.position() + Vec3(-1, 0, 0), Vec3(0, 1, 0));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }

  // Down

  camera.lookat(camera.position() + Vec3(0, -1, 0), Vec3(0, 0, -1));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }

  // Up

  camera.lookat(camera.position() + Vec3(0, 1, 0), Vec3(0, 0, 1));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }

  // Forward

  camera.lookat(camera.position() + Vec3(0, 0, -1), Vec3(0, 1, 0));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }

  // Backwards

  camera.lookat(camera.position() + Vec3(0, 0, 1), Vec3(0, 1, 0));
  platform.render(context, camera, renderables, params);

  for(int y = height-1; y >= 0; --y)
  {
    for(int x = 0; x < width; ++x)
    {
      *dst++ = rgbe(src[y * width + x]);
    }
  }
}


int main(int argc, char **argv)
{
  cout << "EnvMap Generator" << endl;

  try
  {
    Platform platform;

    initialise_platform(platform, 128, 128, 256*1024*1024);

    AssetManager assets(platform.gamememory);

    initialise_asset_system(platform, assets, 64*1024, 128*1024*1024);

    ResourceManager resources(&assets, platform.gamememory);

    initialise_resource_system(platform, resources, 2*1024*1024, 8*1024*1024, 64*1024*1024);

    RenderContext rendercontext;

    initialise_resource_pool(platform, rendercontext.resourcepool, 16*1024*1024);

    assets.load(platform, "core.pack");

    RenderParams renderparams;
    renderparams.width = platform.viewport.width;
    renderparams.height = platform.viewport.height;
    renderparams.aspect = 1.0;
    renderparams.sunintensity = Color3(0, 0, 0);
    renderparams.bloomstrength = 0;

    while (!prepare_render_context(platform, rendercontext, &assets))
      ;

    prepare_render_pipeline(rendercontext, renderparams);

    Scene scene(platform.gamememory);

    scene.initialise_component_storage<TransformComponent>();
    scene.initialise_component_storage<MeshComponent>();
    scene.initialise_component_storage<LightComponent>();

    auto model = scene.load<Model>(platform, &resources, assets.load(platform, "sponza.pack"));

    auto light1 = scene.create<Entity>();
    scene.add_component<TransformComponent>(light1, Transform::translation(Vec3(4.85,1.45,1.45)));
    scene.add_component<PointLightComponent>(light1, Color3(1, 0.5, 0), Attenuation(0.4f, 0.0f, 1.0f));

    auto light2 = scene.create<Entity>();
    scene.add_component<TransformComponent>(light2, Transform::translation(Vec3(4.85,1.45,-2.20)));
    scene.add_component<PointLightComponent>(light2, Color3(1, 0.3, 0), Attenuation(0.4f, 0.0f, 1.0f));

    auto light3 = scene.create<Entity>();
    scene.add_component<TransformComponent>(light3, Transform::translation(Vec3(-6.20,1.45,-2.20)));
    scene.add_component<PointLightComponent>(light3, Color3(1, 0.5, 0), Attenuation(0.4f, 0.0f, 1.0f));

    auto light4 = scene.create<Entity>();
    scene.add_component<TransformComponent>(light4, Transform::translation(Vec3(-6.20,1.45,1.45)));
    scene.add_component<PointLightComponent>(light4, Color3(1, 0.4, 0), Attenuation(0.4f, 0.0f, 1.0f));

    for(auto &mesh : scene.get<Model>(model)->meshes)
    {
      while (mesh && !mesh->ready())
        resources.request(platform, mesh);
    }

    for(auto &texture : scene.get<Model>(model)->textures)
    {
      while (texture && !texture->ready())
        resources.request(platform, texture);
    }

    for(auto &material : scene.get<Model>(model)->materials)
    {
      while (material && !material->ready())
        resources.request(platform, material);
    }

    RenderList renderlist(platform.gamememory, 1*1024*1024);

    {
      MeshList meshes;
      MeshList::BuildState buildstate;

      if (meshes.begin(buildstate, platform, rendercontext, &resources))
      {       
        for(auto &entity : scene.entities<MeshComponent>())
        {
          auto instance = scene.get_component<MeshComponent>(entity);
          auto transform = scene.get_component<TransformComponent>(entity);

          meshes.push_mesh(buildstate, transform.world(), instance.mesh(), instance.material());
        }

        meshes.finalise(buildstate);
      }

      renderlist.push_meshes(meshes);
    }

    {
      LightList lights;
      LightList::BuildState buildstate;

      if (lights.begin(buildstate, platform, rendercontext, &resources))
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

    ofstream fout("sponza-env.pack", ios::binary | ios::trunc);

    write_header(fout);

    int width = platform.viewport.width;
    int height = platform.viewport.height;
    int layers = 6;
    int levels = 8;

    vector<char> payload(image_datasize(width, height, layers, levels));

    image_render_envmap(platform, rendercontext, Vec3(-0.625f, 2.5f, -0.4f), renderlist, renderparams, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 0, width, height, layers, levels, PackImageHeader::rgbe, payload.data(), 0.0f, 0.0f);

    image_render_envmap(platform, rendercontext, Vec3(-0.625f, 2.0f, 4.1f), renderlist, renderparams, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 1, width, height, layers, levels, PackImageHeader::rgbe, payload.data(), 0.0f, 0.0f);

    image_render_envmap(platform, rendercontext, Vec3(-0.625f, 2.0f, -4.65f), renderlist, renderparams, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 2, width, height, layers, levels, PackImageHeader::rgbe, payload.data(), 0.0f, 0.0f);

    image_render_envmap(platform, rendercontext, Vec3(0.0f, 9.0f, 0.0f), renderlist, renderparams, width, height, payload.data());

    image_buildmips_cube_ibl(width, height, levels, payload.data());

    write_imag_asset(fout, 3, width, height, layers, levels, PackImageHeader::rgbe, payload.data(), 0.0f, 0.0f);

    write_chunk(fout, "HEND", 0, nullptr);

    fout.close();
  }
  catch(std::exception &e)
  {
    cerr << "Critical Error: " << e.what() << endl;
  }
}
