#pragma once
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

extern VkDevice vk_device;
extern VkAllocationCallbacks* vk_allocator;
extern VkQueueFamilyProperties* queue_families;
extern uint32_t queue_family;
extern uint32_t count;
extern VkInstance vk_instance;
extern VkPhysicalDevice vk_physical_device;

extern VkResult (*queue_present_original)(VkQueue, const VkPresentInfoKHR*);
extern VkResult (*create_swapchain_original)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
extern VkResult (*acquire_next_image_original)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
extern VkResult (*acquire_next_image2_original)(VkDevice, const VkAcquireNextImageInfoKHR*, uint32_t*);

VkResult queue_present_hook(VkQueue queue, const VkPresentInfoKHR* present_info);
VkResult create_swapchain_hook(VkDevice device, const VkSwapchainCreateInfoKHR* create_info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain);
VkResult acquire_next_image_hook(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* image_index);
VkResult acquire_next_image2_hook(VkDevice device, const VkAcquireNextImageInfoKHR* acquire_info, uint32_t* image_index);

extern int  (*peep_events_original)(SDL_Event*, int, SDL_EventAction, int, int);
extern void (*get_window_size_original)(SDL_Window*, int*, int*);
extern SDL_Window* (*get_keyboard_focus_original)(void);
extern const bool* (*get_keyboard_state_original)(int*);
extern Uint32 (*get_mouse_state_original)(float*, float*);
extern Uint32 (*get_relative_mouse_state_original)(float*, float*);
extern bool (*set_window_relative_mouse_mode_original)(SDL_Window*, bool);
extern bool (*get_window_relative_mouse_mode_original)(SDL_Window*);
extern void (*warp_mouse_in_window_original)(SDL_Window*, float, float);
extern bool (*warp_mouse_global_original)(float, float);
extern bool (*show_cursor_original)(void);
extern bool (*hide_cursor_original)(void);
extern SDL_Cursor* (*set_cursor_original)(SDL_Cursor*);
extern bool (*set_window_mouse_grab_original)(SDL_Window*, bool);
extern bool (*set_window_mouse_rect_original)(SDL_Window*, const SDL_Rect*);

int peep_events_hook(SDL_Event* events, int numevents, SDL_EventAction action, int min, int max);
void get_window_size_hook(SDL_Window* window, int* w, int* h);
SDL_Window* get_keyboard_focus_hook(void);
void bhop_inject_space(bool down);
const bool* get_keyboard_state_hook(int* nkeys);
Uint32 get_mouse_state_hook(float* x, float* y);
Uint32 get_relative_mouse_state_hook(float* x, float* y);
bool set_window_relative_mouse_mode_hook(SDL_Window* window, bool enabled);
bool get_window_relative_mouse_mode_hook(SDL_Window* window);
void warp_mouse_in_window_hook(SDL_Window* window, float x, float y);
bool warp_mouse_global_hook(float x, float y);
bool show_cursor_hook(void);
bool hide_cursor_hook(void);
SDL_Cursor* set_cursor_hook(SDL_Cursor* cursor);
bool set_window_mouse_grab_hook(SDL_Window* window, bool grabbed);
bool set_window_mouse_rect_hook(SDL_Window* window, const SDL_Rect* rect);
void sync_menu_mouse();

extern bool (*input_create_move_original)(void*, int, bool);
bool input_create_move_hook(void* me, int slot, bool active);

extern void (*allow_camera_angle_change_original)(void*, int);
void allow_camera_angle_change_hook(void* me, int n);

bool hook_input_vmt(void* input_ptr);
void unhook_input_vmt();

void aim_inject_lmb(bool down);
