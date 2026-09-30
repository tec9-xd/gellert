#include "hooks.hpp"
#include "../features/feature.hpp"
#include "../core/memory.hpp"
#include "../core/log.hpp"
#include "../gui/config.hpp"

bool (*input_create_move_original)(void*, int, bool) = nullptr;
void (*allow_camera_angle_change_original)(void*, int) = nullptr;

static void** input_vtable = nullptr;

bool input_create_move_hook(void* me, int slot, bool active) {
    g_cm_ticks++;
    g_csgo_input = me;
    FeatureRegistry::get().create_move_pre();
    bool ret = input_create_move_original ? input_create_move_original(me, slot, active) : false;
    FeatureRegistry::get().create_move();
    return ret;
}

void allow_camera_angle_change_hook(void* me, int n) {
    if (allow_camera_angle_change_original)
        allow_camera_angle_change_original(me, n);
}

bool hook_input_vmt(void* input_ptr) {
    if (!valid_ptr(input_ptr)) return false;
    input_vtable = *(void***)input_ptr;
    if (!valid_ptr(input_vtable)) return false;
    if (!mem::vmt_swap(input_vtable, 6, (void*)input_create_move_hook, (void**)&input_create_move_original))
        return false;
    mem::vmt_swap(input_vtable, 8, (void*)allow_camera_angle_change_hook, (void**)&allow_camera_angle_change_original);
    return true;
}

void unhook_input_vmt() {
    if (!input_vtable) return;
    if (input_create_move_original)
        mem::vmt_swap(input_vtable, 6, (void*)input_create_move_original, nullptr);
    if (allow_camera_angle_change_original)
        mem::vmt_swap(input_vtable, 8, (void*)allow_camera_angle_change_original, nullptr);
}
