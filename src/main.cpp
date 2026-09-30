#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cstdlib>

#include "core/log.hpp"
#include "core/math.hpp"
#include "core/memory.hpp"
#include "sdk/input.hpp"
#include "sdk/entity_system.hpp"
#include "sdk/engine.hpp"
#include "sdk/cvar.hpp"
#include "sdk/offsets.hpp"
#include "features/feature.hpp"
#include "hooks/hooks.hpp"

#include "funchook/funchook.h"
#include "libsigscan/libsigscan.h"

#include <vulkan/vulkan.h>

static funchook_t* g_funchook = nullptr;

__attribute__((constructor))
void entry() {
    print("Loaded\n");

    uintptr_t client = 0;
    dl_iterate_phdr([](dl_phdr_info* i, size_t, void* v) -> int {
        if (i->dlpi_name && strstr(i->dlpi_name, "libclient.so")) {
            *(uintptr_t*)v = (uintptr_t)i->dlpi_addr;
            return 1;
        }
        return 0;
    }, &client);

    if (!client) {
        print("libclient.so not mapped\n");
        return;
    }

    mem::client = client;
    view_matrix     = (VMatrix*)(client + off::dwViewMatrix);
    localentity_ptr = (Entity**)(client + off::dwLocalPlayerController);
    print("client: %p\n", (void*)client);
    print("view_matrix: %p\n", view_matrix);
    print("localentity_ptr: %p\n", localentity_ptr);

    unsigned long func_addr1 = (unsigned long)sigscan_module(
        "libclient.so",
        "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 48 89 E5 41 55 41 54 49 89 FC 53 48 89 F3 BE");
    if (func_addr1) {
        unsigned int input_eaddr = *(unsigned int*)(func_addr1 + 0x3);
        input = (Input*)(func_addr1 + 0x7 + input_eaddr);
        print("CInput: %p\n", input);
        if (hook_input_vmt(input))
            print("Input::CreateMove hooked\n");
        else
            print("Input::CreateMove hook failed\n");
    } else {
        print("CInput scan missed — ESP still works, aimbot/bhop-via-cmd won't\n");
        input = nullptr;
    }

    engine = (Engine*)mem::create_interface("libengine2.so", "Source2EngineToClient001");
    cvar_system = (CvarSystem*)mem::create_interface("libtier0.so", "VEngineCvar007");

    void* game_resource_service = mem::create_interface("libengine2.so", "GameResourceServiceClientV001");
    if (!game_resource_service) {
        print("GRS null\n");
        return;
    }
    entity_system = (GameEntitySystem*)*(void**)((uintptr_t)game_resource_service + off::grs_entity_system);
    print("entity_system: %p\n", entity_system);

    g_funchook = funchook_create();
    int rv = 0;

    void* lib_vulkan_handle = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_vulkan_handle)
        lib_vulkan_handle = dlopen("/usr/lib/x86_64-linux-gnu/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_vulkan_handle)
        lib_vulkan_handle = dlopen("/run/host/usr/lib/x86_64-linux-gnu/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_vulkan_handle)
        lib_vulkan_handle = dlopen("/run/host/usr/lib/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_vulkan_handle)
        lib_vulkan_handle = dlopen("/run/host/usr/lib64/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_vulkan_handle) {
        print("Can't find libvulkan! %s\n", dlerror());
        return;
    }
    print("Vulkan loaded at %p\n", lib_vulkan_handle);

    VkInstanceCreateInfo create_info = {};
    constexpr const char* instance_extension = "VK_KHR_surface";
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = &instance_extension;
    vkCreateInstance(&create_info, vk_allocator, &vk_instance);

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, NULL);
    if (gpu_count == 0) {
        print("no vulkan gpus\n");
        return;
    }
    VkPhysicalDevice* gpus = new VkPhysicalDevice[gpu_count];
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus);
    int use_gpu = 0;
    for (int i = 0; i < (int)gpu_count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(gpus[i], &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            use_gpu = i;
            break;
        }
    }
    vk_physical_device = gpus[use_gpu];
    delete[] gpus;

    vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, NULL);
    queue_families = (VkQueueFamilyProperties*)malloc(count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, queue_families);
    for (uint32_t i = 0; i < count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family = i;
            break;
        }
    }
    if (queue_family == (uint32_t)-1)
        print("queue_family fail\n");

    constexpr const char* device_extension = "VK_KHR_swapchain";
    constexpr const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo create_info2 = {};
    create_info2.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info2.queueCreateInfoCount = 1;
    create_info2.pQueueCreateInfos = &queue_info;
    create_info2.enabledExtensionCount = 1;
    create_info2.ppEnabledExtensionNames = &device_extension;

    VkDevice vk_fake_device = VK_NULL_HANDLE;
    vkCreateDevice(vk_physical_device, &create_info2, vk_allocator, &vk_fake_device);
    if (!vk_fake_device) {
        print("Failed to create Vulkan dummy device\n");
        return;
    }

    queue_present_original = (VkResult (*)(VkQueue, const VkPresentInfoKHR*))vkGetDeviceProcAddr(vk_fake_device, "vkQueuePresentKHR");
    acquire_next_image_original = (VkResult (*)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*))vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImageKHR");
    acquire_next_image2_original = (VkResult (*)(VkDevice, const VkAcquireNextImageInfoKHR*, uint32_t*))vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImage2KHR");
    create_swapchain_original = (VkResult (*)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*))vkGetDeviceProcAddr(vk_fake_device, "vkCreateSwapchainKHR");
    vkDestroyDevice(vk_fake_device, vk_allocator);

    rv = funchook_prepare(g_funchook, (void**)&queue_present_original, (void*)queue_present_hook);
    if (rv != 0) { print("Failed to prepare vkQueuePresentKHR hook\n"); return; }
    rv = funchook_prepare(g_funchook, (void**)&acquire_next_image_original, (void*)acquire_next_image_hook);
    if (rv != 0) { print("Failed to prepare vkAcquireNextImageKHR hook\n"); return; }
    rv = funchook_prepare(g_funchook, (void**)&acquire_next_image2_original, (void*)acquire_next_image2_hook);
    if (rv != 0) { print("Failed to prepare vkAcquireNextImage2KHR hook\n"); return; }
    rv = funchook_prepare(g_funchook, (void**)&create_swapchain_original, (void*)create_swapchain_hook);
    if (rv != 0) { print("Failed to prepare vkCreateSwapchainKHR hook\n"); return; }
    dlclose(lib_vulkan_handle);

    void* lib_sdl_handle = dlopen("libSDL3.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_sdl_handle) {
        print("Failed to load SDL3\n");
        return;
    }
    print("SDL3 loaded at %p\n", lib_sdl_handle);

    peep_events_original = (int (*)(SDL_Event*, int, SDL_EventAction, int, int))dlsym(lib_sdl_handle, "SDL_PeepEvents");
    rv = funchook_prepare(g_funchook, (void**)&peep_events_original, (void*)peep_events_hook);
    if (rv != 0) { print("Failed to prepare SDL_PeepEvents hook\n"); return; }

    get_window_size_original = (void (*)(SDL_Window*, int*, int*))dlsym(lib_sdl_handle, "SDL_GetWindowSize");
    rv = funchook_prepare(g_funchook, (void**)&get_window_size_original, (void*)get_window_size_hook);
    if (rv != 0) { print("Failed to prepare SDL_GetWindowSize hook\n"); return; }

    get_keyboard_focus_original = (SDL_Window* (*)(void))dlsym(lib_sdl_handle, "SDL_GetKeyboardFocus");
    rv = funchook_prepare(g_funchook, (void**)&get_keyboard_focus_original, (void*)get_keyboard_focus_hook);
    if (rv != 0) { print("Failed to prepare SDL_GetKeyboardFocus hook\n"); return; }

    get_keyboard_state_original = (const bool* (*)(int*))dlsym(lib_sdl_handle, "SDL_GetKeyboardState");
    rv = funchook_prepare(g_funchook, (void**)&get_keyboard_state_original, (void*)get_keyboard_state_hook);
    if (rv != 0) { print("Failed to prepare SDL_GetKeyboardState hook\n"); return; }

    dlclose(lib_sdl_handle);

    rv = funchook_install(g_funchook, 0);
    if (rv != 0)
        print("Non-VMT related hooks failed\n");

    FeatureRegistry::get().init_all();
    print("features registered: %zu\n", FeatureRegistry::get().all().size());
}

__attribute__((destructor))
void on_unload() {
    print("Uninjecting...\n");
    unhook_input_vmt();
    if (g_funchook)
        funchook_uninstall(g_funchook, 0);
}

#include "features/feature.hpp"
namespace { extern IFeature* force_bhop; }
