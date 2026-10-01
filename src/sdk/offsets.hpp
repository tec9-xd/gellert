#pragma once
#include <cstddef>
#include <cstdint>

// schema: offset-files/libclient_dump.cs + offset-files/a2x/libclient.so.hpp  (2026-10-01)
// globals: offset-files/a2x/offsets.hpp  (linux libclient.so, 2026-10-01)
namespace off {
    constexpr std::ptrdiff_t dwViewMatrix            = 0x493FD80;
    constexpr std::ptrdiff_t dwLocalPlayerController = 0x4900698;
    constexpr std::ptrdiff_t dwLocalPlayerPawn       = 0x4938758;
    constexpr std::ptrdiff_t dwGameEntitySystem      = 0x4B8D7D0;
    constexpr std::ptrdiff_t dwEntityList            = 0x46BB280;

    constexpr std::ptrdiff_t m_aimPunchAngle = 0x16CC; // stale; punch is CCSPlayer_AimPunchServices now

    constexpr std::ptrdiff_t m_pMovementServices       = 0x12B8;
    constexpr std::ptrdiff_t m_nButtons                = 0x50;
    constexpr std::ptrdiff_t m_nQueuedButtonDownMask   = 0x70;
    constexpr std::ptrdiff_t m_nQueuedButtonChangeMask = 0x78;
    constexpr std::ptrdiff_t m_LegacyJump              = 0x6B0;
    constexpr std::ptrdiff_t m_bOldJumpPressed         = 0x10;
    constexpr std::ptrdiff_t m_pButtonStates           = 0x8;

    constexpr std::ptrdiff_t entity_identity_size    = 0x70;
    constexpr std::ptrdiff_t ges_identity_chunks     = 0x10;
    constexpr std::ptrdiff_t grs_entity_system       = 0x50;

    constexpr std::ptrdiff_t m_hPlayerPawn              = 0xAAC;
    constexpr std::ptrdiff_t m_iszPlayerName            = 0x87C;
    constexpr std::ptrdiff_t m_bIsLocalPlayerController = 0x910; // libclient_dump.cs
    constexpr std::ptrdiff_t m_iDesiredFOV              = 0x914;

    constexpr std::ptrdiff_t m_pGameSceneNode        = 0x4A0;
    constexpr std::ptrdiff_t m_pCameraServices       = 0x12B0;
    constexpr std::ptrdiff_t m_iFOV                  = 0x2A0;
    constexpr std::ptrdiff_t m_vecAbsOrigin          = 0xC8;
    constexpr std::ptrdiff_t m_bDormant              = 0x103;
    constexpr std::ptrdiff_t m_modelState            = 0x140;
    constexpr std::ptrdiff_t bone_array              = 0x80;
    constexpr std::ptrdiff_t bone_stride             = 32;
    constexpr std::ptrdiff_t m_vecViewOffset         = 0xEE0;
    constexpr std::ptrdiff_t m_vOldOrigin            = 0x142C; // a2x schema; not in libclient_dump.cs
    constexpr std::ptrdiff_t m_lifeState             = 0x4C4;
    constexpr std::ptrdiff_t m_iTeamNum              = 0x557;
    constexpr std::ptrdiff_t m_iHealth               = 0x4BC;
    constexpr std::ptrdiff_t m_fFlags                = 0x564;
    constexpr std::ptrdiff_t m_hGroundEntity         = 0x6A8;
    constexpr std::ptrdiff_t m_bGunGameImmunity      = 0x43A8;

    constexpr std::ptrdiff_t input_thirdperson       = 0x261;
    constexpr std::ptrdiff_t input_shoot             = 0x288;
    constexpr std::ptrdiff_t jump                    = 0;
}

constexpr uint64_t IN_JUMP     = 1ull << 1;
constexpr uint32_t FL_ONGROUND = 1u << 0;
