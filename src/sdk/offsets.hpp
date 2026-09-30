#pragma once
#include <cstddef>

// Linux libclient.so — from your working binary + a2x dump in offsets/
namespace off {
    constexpr std::ptrdiff_t dwViewMatrix            = 0x4941D80;
    constexpr std::ptrdiff_t dwLocalPlayerController = 0x4902698;
    constexpr std::ptrdiff_t dwLocalPlayerPawn       = 0x493A758;
    constexpr std::ptrdiff_t dwGameEntitySystem      = 0x4B8EF90;
    constexpr std::ptrdiff_t m_aimPunchAngle = 0x16CC; // C_CSPlayerPawn

    constexpr std::ptrdiff_t m_pMovementServices       = 0x12B8; // C_BasePlayerPawn
    constexpr std::ptrdiff_t m_nButtons                = 0x50;   // CPlayer_MovementServices
    constexpr std::ptrdiff_t m_nQueuedButtonDownMask   = 0x70;
    constexpr std::ptrdiff_t m_nQueuedButtonChangeMask = 0x78;
    constexpr std::ptrdiff_t m_LegacyJump              = 0x6B0;  // CCSPlayer_MovementServices
    constexpr std::ptrdiff_t m_bOldJumpPressed         = 0x10;   // CCSPlayerLegacyJump
    constexpr std::ptrdiff_t m_pButtonStates           = 0x8;    // inside CInButtonState; 0x50+0x20 == m_nQueuedButtonDownMask

    constexpr std::ptrdiff_t entity_identity_size    = 0x70;
    constexpr std::ptrdiff_t grs_entity_system       = 0x50; // GameResourceServiceClient + 0x50

    constexpr std::ptrdiff_t m_hPlayerPawn           = 0xAAC;
    constexpr std::ptrdiff_t m_iszPlayerName         = 0x87C;
    constexpr std::ptrdiff_t m_iDesiredFOV           = 0x914;

    constexpr std::ptrdiff_t m_pGameSceneNode        = 0x4A0;
    constexpr std::ptrdiff_t m_pCameraServices       = 0x12B0;
    constexpr std::ptrdiff_t m_iFOV                  = 0x2A0;
    constexpr std::ptrdiff_t m_vecAbsOrigin          = 0xC8;
    constexpr std::ptrdiff_t m_bDormant              = 0x103;
    constexpr std::ptrdiff_t m_modelState            = 0x140;
    constexpr std::ptrdiff_t bone_array              = 0x80; // CModelState
    constexpr std::ptrdiff_t bone_stride             = 32;
    constexpr std::ptrdiff_t m_vecViewOffset         = 0xEE0;
    constexpr std::ptrdiff_t m_lifeState             = 0x4C4;
    constexpr std::ptrdiff_t m_iTeamNum              = 0x557;
    constexpr std::ptrdiff_t m_iHealth               = 0x4BC;
    constexpr std::ptrdiff_t m_fFlags                = 0x564;
    constexpr std::ptrdiff_t m_hGroundEntity         = 0x6A8;
    constexpr std::ptrdiff_t m_bGunGameImmunity      = 0x43A8;

    constexpr std::ptrdiff_t input_thirdperson       = 0x261;
    constexpr std::ptrdiff_t input_shoot             = 0x288;

    // Fill from a2x Linux buttons.hpp `jump` if you dump it. 0 = SDL fallback.
    constexpr std::ptrdiff_t jump                    = 0;
}

constexpr uint64_t IN_JUMP     = 1ull << 1;
constexpr uint32_t FL_ONGROUND = 1u << 0;
