#include "feature.hpp"

// explicit refs so the linker cannot drop feature TUs
extern "C" {
    void* bhop_keep();
    void* aim_keep();
    void* esp_keep();
    void* vis_keep();
    void* tp_keep();
    void* misc_keep();
}

void features_force_link() {
    (void)bhop_keep();
    (void)aim_keep();
    (void)esp_keep();
    (void)vis_keep();
    (void)tp_keep();
    (void)misc_keep();
}
