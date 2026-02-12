#pragma once
/*
 * rust_offsets.h - Rust game offsets
 *
 * Patch:  21886569
 * Source: UnknownCheats verified dump (Feb 12 2026)
 */

#include <cstdint>

namespace Offsets {

    /* ── GameAssembly.dll RVAs ─────────────────────────────────── */

    inline constexpr std::uintptr_t BaseNetworkable_TypeInfo = 0xD65A0A8;
    inline constexpr std::uintptr_t MainCamera_TypeInfo      = 0xD676960;
    inline constexpr std::uintptr_t BaseViewModel_TypeInfo   = 0xD6B9878;
    inline constexpr std::uintptr_t TodSky_TypeInfo          = 0xD6C6278;
    inline constexpr std::uintptr_t GameManager_TypeInfo     = 0xD6BA370; // 224963568 dec
    inline constexpr std::uintptr_t il2cpp_get_handle        = 0xD985DD0;

    /* ── Entity traversal chain ───────────────────────────────── */
    /*
     * GameAssembly + BaseNetworkable_TypeInfo -> TypeInfo ptr
     * TypeInfo + 0xB8  -> static_fields
     * static  + 0x08   -> clientEntities wrapper (ENCRYPTED)
     *   decrypt_client_entities(wrapper) -> clientEntities obj
     * clientE + 0x20   -> objectDictionary  (plain read, NOT encrypted!)
     * objDict + 0x10   -> content (entity array / Il2CppArray of ptrs)
     * objDict + 0x18   -> size   (entity count)
     */

    namespace EntityChain {
        inline constexpr uint32_t static_fields      = 0xB8;
        inline constexpr uint32_t client_entities     = 0x08;  /* encrypted wrapper */
        inline constexpr uint32_t object_dictionary   = 0x20;  /* plain ptr in clientEntities */
        inline constexpr uint32_t content             = 0x10;  /* entity array in objDict */
        inline constexpr uint32_t size                = 0x18;  /* count in objDict */
    }

    /* ── Camera chain ─────────────────────────────────────────── */

    namespace CameraChain {
        inline constexpr uint32_t static_fields = 0xB8;
        inline constexpr uint32_t instance      = 0x58;
        inline constexpr uint32_t buffer        = 0x10;
        inline constexpr uint32_t view_matrix   = 0x30C;
        inline constexpr uint32_t position      = 0x454;
    }

    /* ── BaseNetworkable ──────────────────────────────────────── */

    namespace BaseNetworkable {
        inline constexpr uint32_t prefabID = 0x30;
        inline constexpr uint32_t children = 0x40;
        inline constexpr uint32_t parent_entity = 0x70;
    }

    /* ── BasePlayer ───────────────────────────────────────────── */

    namespace BasePlayer {
        inline constexpr uint32_t input         = 0x2F0;   // PlayerInput
        inline constexpr uint32_t currentTeam   = 0x4A0;
        inline constexpr uint32_t clActiveItem  = 0x4D0;   // EncryptedValue<ItemId>
        inline constexpr uint32_t playerFlags   = 0x5E8;
        inline constexpr uint32_t playerModel   = 0x5F0;
        inline constexpr uint32_t playerEyes    = 0x5F8;   // encrypted
        inline constexpr uint32_t displayName   = 0x620;
        inline constexpr uint32_t inventory     = 0x658;   // encrypted
        inline constexpr uint32_t movement      = 0x508;
    }

    /* Player flags bitmask */
    namespace PlayerFlags {
        inline constexpr uint32_t IsAdmin      = (1 << 2);
        inline constexpr uint32_t Wounded      = (1 << 4);
        inline constexpr uint32_t IsSleeping   = (1 << 5);
        inline constexpr uint32_t IsConnected  = (1 << 8);
    }

    /* ── PlayerInput ──────────────────────────────────────────── */

    namespace PlayerInput {
        inline constexpr uint32_t viewAngles = 0x44;
    }

    /* ── PlayerModel ──────────────────────────────────────────── */

    namespace PlayerModel {
        inline constexpr uint32_t position      = 0x1F8;
        inline constexpr uint32_t newVelocity   = 0x21C;
        inline constexpr uint32_t boxCollider   = 0x20;
        inline constexpr uint32_t skinnedMultiMesh = 0x378;
    }

    namespace BoxCollider {
        inline constexpr uint32_t internal_     = 0x10;
        inline constexpr uint32_t center        = 0x80;
        inline constexpr uint32_t size          = 0x8C;
    }

    namespace SkinnedMultiMesh {
        inline constexpr uint32_t rendererList  = 0x58;
    }

    /* ── PlayerEyes ───────────────────────────────────────────── */

    namespace PlayerEyes {
        inline constexpr uint32_t bodyRotation = 0x50;
    }

    /* ── BaseCombatEntity ─────────────────────────────────────── */

    namespace BaseCombatEntity {
        inline constexpr uint32_t lifestate = 0x258;
    }

    /* ── BaseEntity ───────────────────────────────────────────── */

    namespace BaseEntity {
        inline constexpr uint32_t model     = 0xE8;
        inline constexpr uint32_t isVisible = 0x18;  // placeholder — may need ReClass
    }

    namespace Model {
        inline constexpr uint32_t boneTransforms = 0x50;
    }

    /* ── Item / Inventory ─────────────────────────────────────── */

    namespace PlayerInventory {
        inline constexpr uint32_t containerBelt = 0x78;
    }

    namespace ItemContainer {
        inline constexpr uint32_t itemList = 0x58;
    }

    namespace WorldItem {
        inline constexpr uint32_t item = 0x1C8;
    }

    namespace Item {
        inline constexpr uint32_t uid            = 0x60;
        inline constexpr uint32_t itemDefinition = 0xD0;
    }

    namespace ItemDefinition {
        inline constexpr uint32_t shortname = 0x28;
        inline constexpr uint32_t category  = 0x58;
    }

    /* ── BaseViewModel ────────────────────────────────────────── */

    namespace BaseViewModel {
        inline constexpr uint32_t instance        = 0xD8;
        inline constexpr uint32_t animationEvents = 0xD0;
    }

    namespace AnimationEvents {
        inline constexpr uint32_t targetEntity = 0x28;
    }

    /* ── TOD_Sky ──────────────────────────────────────────────── */

    namespace TodSky {
        inline constexpr uint32_t instance = 0x90;
        inline constexpr uint32_t night    = 0x60;
        inline constexpr uint32_t cycle    = 0x40;
    }

    namespace TOD_NightParameters {
        inline constexpr uint32_t lightIntensity       = 0x50;
        inline constexpr uint32_t reflectionMultiplier = 0x64;
    }

    namespace TOD_CycleParameters {
        inline constexpr uint32_t hour = 0x10;
    }

    /* ── GameManager ──────────────────────────────────────────── */

    namespace GameManager {
        inline constexpr uint32_t instance              = 0x40;
        inline constexpr uint32_t prefabPoolCollection  = 0x20;
    }

    /* ── Decryption function RVAs (GameAssembly.dll) ──────────── */

    namespace Decrypt {
        inline constexpr std::uintptr_t client_entities_fn = 0xCD3A30;
        inline constexpr std::uintptr_t entity_list_fn     = 0xBB4D00;
        inline constexpr std::uintptr_t player_eyes_fn     = 0xBD9740;
        inline constexpr std::uintptr_t player_inv_fn      = 0xBDAB80;
        inline constexpr std::uintptr_t cl_active_item_fn  = 0xDF220;
    }

    /* ── IL2CPP internals ─────────────────────────────────────── */

    namespace Il2CppString {
        inline constexpr uint32_t length = 0x10;
        inline constexpr uint32_t chars  = 0x14;
    }

    namespace Il2CppList {
        inline constexpr uint32_t items = 0x10;
        inline constexpr uint32_t size  = 0x18;
    }

    namespace Il2CppArray {
        inline constexpr uint32_t length  = 0x18;
        inline constexpr uint32_t first   = 0x20;
    }

} // namespace Offsets
