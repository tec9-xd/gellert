#include "hooks.hpp"
#include "../gui/menu.hpp"
#include "../features/feature.hpp"
#include "../core/log.hpp"
#include "../gui/config.hpp"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <SDL3/SDL.h>

#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl3.h"

VkDevice vk_device = VK_NULL_HANDLE;
VkAllocationCallbacks* vk_allocator = nullptr;
VkQueueFamilyProperties* queue_families = nullptr;
uint32_t queue_family = (uint32_t)-1;
uint32_t count = 0;
VkInstance vk_instance = VK_NULL_HANDLE;
VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;

static VkRenderPass vk_render_pass = VK_NULL_HANDLE;
static VkDescriptorPool vk_descriptor_pool = VK_NULL_HANDLE;
static VkExtent2D vk_image_extent = {};
static VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
static uint32_t min_image_count = 2;
static ImGui_ImplVulkanH_Frame g_Frames[8] = {};
static ImGui_ImplVulkanH_FrameSemaphores g_FrameSemaphores[8] = {};
static VkQueue g_graphics_queue = VK_NULL_HANDLE;
static bool g_logged_present = false;
static bool g_overlay_dead = false;

VkResult (*queue_present_original)(VkQueue, const VkPresentInfoKHR*) = nullptr;
VkResult (*create_swapchain_original)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*) = nullptr;
VkResult (*acquire_next_image_original)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*) = nullptr;
VkResult (*acquire_next_image2_original)(VkDevice, const VkAcquireNextImageInfoKHR*, uint32_t*) = nullptr;

static void fill_extent_from_sdl() {
    int w = 0, h = 0;
    if (sdl_window && get_window_size_original)
        get_window_size_original(sdl_window, &w, &h);
    if (w <= 0) w = 1920;
    if (h <= 0) h = 1080;
    vk_image_extent.width  = (uint32_t)w;
    vk_image_extent.height = (uint32_t)h;
}

VkResult queue_present_hook(VkQueue queue, const VkPresentInfoKHR* present_info) {
    if (g_overlay_dead || !vk_device || !sdl_window || !present_info || !present_info->pSwapchains)
        return queue_present_original(queue, present_info);

    if (!g_logged_present) {
        print("first present  device=%p window=%p extent=%ux%u\n",
              vk_device, sdl_window, vk_image_extent.width, vk_image_extent.height);
        g_logged_present = true;
    }

    if (vk_image_extent.width == 0 || vk_image_extent.height == 0) {
        fill_extent_from_sdl();
        print("extent from SDL: %ux%u\n", vk_image_extent.width, vk_image_extent.height);
    }

    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
        ImGui_ImplSDL3_InitForVulkan(sdl_window);
        ImGui::GetIO().Fonts->AddFontDefault();
    }

    if (g_graphics_queue == VK_NULL_HANDLE) {
        if (queue_families && count) {
            for (uint32_t i = 0; i < count; ++i) {
                if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    vkGetDeviceQueue(vk_device, i, 0, &g_graphics_queue);
                    queue_family = i;
                    break;
                }
            }
        }
        if (g_graphics_queue == VK_NULL_HANDLE)
            g_graphics_queue = queue;
    }
    VkQueue graphicQueue = g_graphics_queue;

    for (uint32_t i = 0; i < present_info->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = present_info->pSwapchains[i];
        if (g_Frames[0].Framebuffer == VK_NULL_HANDLE) {
            uint32_t uImageCount = 0;
            vkGetSwapchainImagesKHR(vk_device, swapchain, &uImageCount, NULL);
            if (uImageCount == 0 || uImageCount > 8) {
                print("bad swapchain image count %u\n", uImageCount);
                g_overlay_dead = true;
                return queue_present_original(queue, present_info);
            }

            VkImage backbuffers[8] = {};
            vkGetSwapchainImagesKHR(vk_device, swapchain, &uImageCount, backbuffers);

            for (uint32_t fi = 0; fi < uImageCount; ++fi) {
                g_Frames[fi].Backbuffer = backbuffers[fi];
                ImGui_ImplVulkanH_Frame* fd = &g_Frames[fi];
                ImGui_ImplVulkanH_FrameSemaphores* fsd = &g_FrameSemaphores[fi];
                {
                    VkCommandPoolCreateInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                    info.queueFamilyIndex = queue_family;
                    vkCreateCommandPool(vk_device, &info, vk_allocator, &fd->CommandPool);
                }
                {
                    VkCommandBufferAllocateInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                    info.commandPool = fd->CommandPool;
                    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                    info.commandBufferCount = 1;
                    vkAllocateCommandBuffers(vk_device, &info, &fd->CommandBuffer);
                }
                {
                    VkFenceCreateInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                    vkCreateFence(vk_device, &info, vk_allocator, &fd->Fence);
                }
                {
                    VkSemaphoreCreateInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                    vkCreateSemaphore(vk_device, &info, vk_allocator, &fsd->ImageAcquiredSemaphore);
                    vkCreateSemaphore(vk_device, &info, vk_allocator, &fsd->RenderCompleteSemaphore);
                }
            }

            {
                VkAttachmentDescription attachment = {};
                attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                VkAttachmentReference color_attachment = {};
                color_attachment.attachment = 0;
                color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkSubpassDescription subpass = {};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &color_attachment;

                VkRenderPassCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                info.attachmentCount = 1;
                info.pAttachments = &attachment;
                info.subpassCount = 1;
                info.pSubpasses = &subpass;

                if (vkCreateRenderPass(vk_device, &info, vk_allocator, &vk_render_pass) != VK_SUCCESS) {
                    print("renderpass err\n");
                    g_overlay_dead = true;
                    return queue_present_original(queue, present_info);
                }
            }

            {
                VkImageViewCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                info.format = VK_FORMAT_B8G8R8A8_UNORM;
                info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                info.subresourceRange.baseMipLevel = 0;
                info.subresourceRange.levelCount = 1;
                info.subresourceRange.baseArrayLayer = 0;
                info.subresourceRange.layerCount = 1;
                for (uint32_t fi = 0; fi < uImageCount; ++fi) {
                    ImGui_ImplVulkanH_Frame* fd = &g_Frames[fi];
                    info.image = fd->Backbuffer;
                    vkCreateImageView(vk_device, &info, vk_allocator, &fd->BackbufferView);
                }
            }

            {
                VkImageView attachment[1];
                VkFramebufferCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                info.renderPass = vk_render_pass;
                info.attachmentCount = 1;
                info.pAttachments = attachment;
                info.layers = 1;
                info.width  = vk_image_extent.width;
                info.height = vk_image_extent.height;
                for (uint32_t fi = 0; fi < uImageCount; ++fi) {
                    ImGui_ImplVulkanH_Frame* fd = &g_Frames[fi];
                    attachment[0] = fd->BackbufferView;
                    if (vkCreateFramebuffer(vk_device, &info, vk_allocator, &fd->Framebuffer) != VK_SUCCESS) {
                        print("framebuffer err\n");
                        g_overlay_dead = true;
                        return queue_present_original(queue, present_info);
                    }
                }
            }

            if (!vk_descriptor_pool) {
                constexpr VkDescriptorPoolSize pool_sizes[] = {
                    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
                };
                VkDescriptorPoolCreateInfo pool_info = {};
                pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
                pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
                pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
                pool_info.pPoolSizes = pool_sizes;
                vkCreateDescriptorPool(vk_device, &pool_info, vk_allocator, &vk_descriptor_pool);
            }
        }

        uint32_t image_index = present_info->pImageIndices[i];
        if (image_index >= 8) {
            print("bad image index %u\n", image_index);
            continue;
        }

        ImGui_ImplVulkanH_Frame* fd = &g_Frames[image_index];
        if (!fd->Fence || !fd->CommandBuffer || !fd->Framebuffer) {
            print("frame resources missing\n");
            g_overlay_dead = true;
            return queue_present_original(queue, present_info);
        }

        {
            VkResult wr = vkWaitForFences(vk_device, 1, &fd->Fence, VK_TRUE, 50000000ull);
            if (wr != VK_SUCCESS)
                return queue_present_original(queue, present_info);
            vkResetFences(vk_device, 1, &fd->Fence);
        }

        {
            vkResetCommandBuffer(fd->CommandBuffer, 0);
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(fd->CommandBuffer, &info);
        }

        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = vk_render_pass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent = vk_image_extent;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        if (!ImGui::GetIO().BackendRendererUserData) {
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = vk_instance;
            init_info.PhysicalDevice = vk_physical_device;
            init_info.Device = vk_device;
            init_info.QueueFamily = queue_family;
            init_info.Queue = graphicQueue;
            init_info.PipelineCache = vk_pipeline_cache;
            init_info.DescriptorPool = vk_descriptor_pool;
            init_info.Subpass = 0;
            init_info.MinImageCount = min_image_count;
            init_info.ImageCount = min_image_count;
            init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.Allocator = vk_allocator;
            init_info.RenderPass = vk_render_pass;

            ImGui::GetIO().DisplaySize.x = (float)vk_image_extent.width;
            ImGui::GetIO().DisplaySize.y = (float)vk_image_extent.height;
            ImGui_ImplVulkan_Init(&init_info);

            ImGuiStyle* style = &ImGui::GetStyle();
            style->Colors[ImGuiCol_WindowBg]         = ImVec4(0.1f, 0.1f, 0.1f, 1);
            style->Colors[ImGuiCol_TitleBgActive]    = ImVec4(0.05f, 0.05f, 0.05f, 1);
            style->Colors[ImGuiCol_TitleBg]          = ImVec4(0.05f, 0.05f, 0.05f, 1);
            style->Colors[ImGuiCol_CheckMark]        = ImVec4(0.869346734f, 0.450980392f, 0.211764706f, 1);
            style->Colors[ImGuiCol_FrameBg]          = ImVec4(0.15f, 0.15f, 0.15f, 1);
            style->Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.869346734f, 0.450980392f, 0.211764706f, 0.5f);
            style->Colors[ImGuiCol_FrameBgActive]    = ImVec4(0.919346734f, 0.500980392f, 0.261764706f, 0.6f);
            style->Colors[ImGuiCol_ButtonHovered]    = ImVec4(0.869346734f, 0.450980392f, 0.211764706f, 0.5f);
            style->Colors[ImGuiCol_ButtonActive]     = ImVec4(0.919346734f, 0.500980392f, 0.261764706f, 0.6f);
            style->Colors[ImGuiCol_SliderGrab]       = ImVec4(0.869346734f, 0.450980392f, 0.211764706f, 1);
            style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.899346734f, 0.480980392f, 0.241764706f, 1);
            style->GrabMinSize = 2;
            style->Colors[ImGuiCol_Header]           = ImVec4(0.18f, 0.18f, 0.18f, 1);
            style->Colors[ImGuiCol_HeaderHovered]    = ImVec4(0.869346734f, 0.450980392f, 0.211764706f, 0.5f);
            style->Colors[ImGuiCol_HeaderActive]     = ImVec4(0.919346734f, 0.500980392f, 0.261764706f, 0.6f);
        }

        ImGui::GetIO().DisplaySize.x = (float)vk_image_extent.width;
        ImGui::GetIO().DisplaySize.y = (float)vk_image_extent.height;

        if (ImGui::IsKeyPressed(ImGuiKey_Insert, false) || ImGui::IsKeyPressed(ImGuiKey_F11, false))
            menu_focused = !menu_focused;

        sync_menu_mouse();

        ImGui::GetIO().MouseDrawCursor = menu_focused;
        ImGui::GetIO().WantCaptureMouse = menu_focused;
        ImGui::GetIO().WantCaptureKeyboard = menu_focused;
        if (menu_focused && !config.misc.input_passthrough)
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)vk_image_extent.width, (float)vk_image_extent.height));
        ImGui::Begin("Overlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);

        FeatureRegistry::get().draw();

        ImGui::End();

        if (menu_focused)
            draw_menu();
        draw_watermark();

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

        vkCmdEndRenderPass(fd->CommandBuffer);
        vkEndCommandBuffer(fd->CommandBuffer);

        constexpr VkPipelineStageFlags stages_wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        {
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.pWaitDstStageMask = &stages_wait;
            vkQueueSubmit(graphicQueue, 1, &info, fd->Fence);
        }
    }

    return queue_present_original(queue, present_info);
}

VkResult create_swapchain_hook(VkDevice device, const VkSwapchainCreateInfoKHR* create_info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {
    for (uint32_t i = 0; i < 8; ++i) {
        if (g_Frames[i].Fence) {
            vkDestroyFence(vk_device, g_Frames[i].Fence, vk_allocator);
            g_Frames[i].Fence = VK_NULL_HANDLE;
        }
        if (g_Frames[i].CommandBuffer) {
            vkFreeCommandBuffers(vk_device, g_Frames[i].CommandPool, 1, &g_Frames[i].CommandBuffer);
            g_Frames[i].CommandBuffer = VK_NULL_HANDLE;
        }
        if (g_Frames[i].CommandPool) {
            vkDestroyCommandPool(vk_device, g_Frames[i].CommandPool, vk_allocator);
            g_Frames[i].CommandPool = VK_NULL_HANDLE;
        }
        if (g_Frames[i].BackbufferView) {
            vkDestroyImageView(vk_device, g_Frames[i].BackbufferView, vk_allocator);
            g_Frames[i].BackbufferView = VK_NULL_HANDLE;
        }
        if (g_Frames[i].Framebuffer) {
            vkDestroyFramebuffer(vk_device, g_Frames[i].Framebuffer, vk_allocator);
            g_Frames[i].Framebuffer = VK_NULL_HANDLE;
        }
    }
    for (uint32_t i = 0; i < 8; ++i) {
        if (g_FrameSemaphores[i].ImageAcquiredSemaphore) {
            vkDestroySemaphore(vk_device, g_FrameSemaphores[i].ImageAcquiredSemaphore, vk_allocator);
            g_FrameSemaphores[i].ImageAcquiredSemaphore = VK_NULL_HANDLE;
        }
        if (g_FrameSemaphores[i].RenderCompleteSemaphore) {
            vkDestroySemaphore(vk_device, g_FrameSemaphores[i].RenderCompleteSemaphore, vk_allocator);
            g_FrameSemaphores[i].RenderCompleteSemaphore = VK_NULL_HANDLE;
        }
    }
    vk_image_extent = create_info->imageExtent;
    return create_swapchain_original(device, create_info, allocator, swapchain);
}

VkResult acquire_next_image_hook(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* image_index) {
    vk_device = device;
    return acquire_next_image_original(device, swapchain, timeout, semaphore, fence, image_index);
}

VkResult acquire_next_image2_hook(VkDevice device, const VkAcquireNextImageInfoKHR* acquire_info, uint32_t* image_index) {
    vk_device = device;
    return acquire_next_image2_original(device, acquire_info, image_index);
}
