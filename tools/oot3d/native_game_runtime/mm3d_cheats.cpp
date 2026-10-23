#include "mm3d_cheats.h"

#include "oot3d_native_a32_process.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {
namespace {

// USA v1.1 save (read at runtime; the CTRPF codes' addresses less 0x1000).
constexpr uint32_t kSaveTime = 0x7751F8U;          // u16, 0x4000 = 6 AM
constexpr uint32_t kHealthMax = 0x775312U;         // u16, 0x10 per heart
constexpr uint32_t kHealth = 0x775314U;            // u16
constexpr uint32_t kMagicLevel = 0x775316U;        // u8, 0 none, 1 normal, 2 double
constexpr uint32_t kMagic = 0x775317U;             // u8, 0x30 per level
constexpr uint32_t kRupees = 0x775318U;            // u16
constexpr uint32_t kRazorSwordHits = 0x77531AU;    // u16, 100 when new
constexpr uint32_t kSwordShield = 0x775352U;       // u8: sword in the low nibble (2 = Razor)
constexpr uint32_t kItems = 0x775354U;             // u8[24] by slot, 0xFF empty
constexpr uint32_t kAmmo = 0x775390U;              // u8[24] by slot
constexpr uint32_t kUpgrades = 0x7753CCU;          // u32: quiver 0-2, bomb bag 3-5, wallet 12-13,
                                                   // sticks 17-19, nuts 20-22
constexpr uint32_t kQuestStatus = 0x7753D0U;       // u32: remains 0-3, songs 6-17, notebook 18,
                                                   // heart pieces 28-31
constexpr uint32_t kMagicAcquired = 0x77531EU;     // u8, then double magic, double defense
constexpr uint32_t kButtonItems = 0x77532AU;       // u8[4][5] per form (B first)
constexpr uint32_t kWeekEvents = 0x776428U;        // u8 weekEventReg[100] (N64 numbering)

// Their lairs and the flags that make them stay away once beaten (as N64:
// WEEKEVENTREG_CLEARED_*_TEMPLE; Odolwa's checked at 0x349088).
constexpr Mm3dBoss kBosses[] = {
    {"Odolwa", "Odolwa's Lair", 0x1C, 0x1F, 20, 0x02, 0},
    {"Goht", "Goht's Lair", 0x41, 0x44, 33, 0x80, 1},
    {"Gyorg", "Gyorg's Lair", 0x5C, 0x5F, 55, 0x80, 2},
    {"Twinmold", "Twinmold's Lair", 0x33, 0x36, 52, 0x20, 3},
    {"Majora", "Majora's Lair (the Moon)", 0x01, 0x0B, -1, 0, -1},
    // Igos counts as beaten by the room's clear flag, for this 3-day cycle.
    {"Igos du Ikana", "Igos du Ikana's Lair", 0x53, 0x56, -1, 0, -1},
};

// Item slots as N64 Majora's Mask (the table at 0x6A0B48): items 0x00-0x10
// in their own slot, trade items in 5, 17, 58, 59; bottles moved to 48-53;
// masks from slot 24 in this order.
constexpr uint8_t kMaskSlots[24] = {0x3E, 0x38, 0x47, 0x45, 0x40, 0x32, 0x3A, 0x46, 0x39, 0x42, 0x48, 0x33,
                                    0x3C, 0x3D, 0x37, 0x3F, 0x36, 0x34, 0x43, 0x41, 0x3B, 0x44, 0x49, 0x35};
constexpr uint32_t kBottleSlot = 48U, kBottleCount = 6U;

// GlobalContext: pad state at +0x28 (buttons +0x30, pressed +0x34, released
// +0x38; main stick x,y in -1..1 at +0x58), cameras at +0x408 + idx * 0x198
// (idx u16 at +0xA7A; look-at +0x50, eye +0x5C), Player at +0x20DC.
// Player: position +0x24, yaw (s16) +0x32, velocity +0x64, xz speed +0x70,
// gravity +0x74, state flags1 +0x11DB0.
constexpr uint32_t kButtonA = 0x1U, kButtonB = 0x2U, kButtonL = 0x200U;

float ReadFloat(oot3d::recomp::a32::MemoryBus& memory, uint32_t address) {
    uint32_t bits = 0;
    memory.Read32(address, &bits);
    return std::bit_cast<float>(bits);
}

void WriteFloat(oot3d::recomp::a32::MemoryBus& memory, uint32_t address, float value) {
    memory.Write32(address, std::bit_cast<uint32_t>(value));
}

uint8_t Read8(oot3d::recomp::a32::MemoryBus& memory, uint32_t address) {
    uint8_t value = 0;
    memory.Read8(address, &value);
    return value;
}

uint16_t Read16(oot3d::recomp::a32::MemoryBus& memory, uint32_t address) {
    uint16_t value = 0;
    memory.Read16(address, &value);
    return value;
}

// The held buttons are taken from the game (it does not see them pressed).
void ConsumeButtons(oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx, uint32_t mask) {
    for (const uint32_t offset : {0x30U, 0x34U, 0x38U}) {
        uint32_t value = 0;
        if (memory.Read32(gctx + offset, &value) && (value & mask) != 0U)
            memory.Write32(gctx + offset, value & ~mask);
    }
}

// Room for an item's ammo when it is given (an upgrade if it has none).
void GiveAmmo(oot3d::recomp::a32::MemoryBus& memory, uint32_t slot) {
    uint32_t upgrades = 0;
    memory.Read32(kUpgrades, &upgrades);
    uint8_t amount = 0;
    if (slot == 1U) {
        if ((upgrades & 7U) == 0U) upgrades |= 1U;
        amount = 30;
    } else if (slot == 6U || slot == 7U) {
        if (((upgrades >> 3U) & 7U) == 0U) upgrades |= 1U << 3U;
        amount = 20;
    } else if (slot == 8U) {
        amount = 10;
    } else if (slot == 9U) {
        amount = 20;
    } else if (slot == 10U || slot == 12U) {
        amount = 1;
    }
    memory.Write32(kUpgrades, upgrades);
    if (amount != 0U && Read8(memory, kAmmo + slot) < amount) memory.Write8(kAmmo + slot, amount);
}

bool GiveItem(oot3d::recomp::a32::MemoryBus& memory, int item) {
    uint32_t slot = 0xFFU;
    if (item >= 0x00 && item <= 0x10 && item != 0x05 && item != 0x0B) {
        slot = static_cast<uint32_t>(item);
    } else if (item >= 0x12 && item <= 0x26) {
        // A bottle: the first empty slot, else the first empty bottle.
        for (uint32_t i = 0; i < kBottleCount && slot == 0xFFU; ++i)
            if (Read8(memory, kItems + kBottleSlot + i) == 0xFFU) slot = kBottleSlot + i;
        for (uint32_t i = 0; i < kBottleCount && slot == 0xFFU; ++i)
            if (Read8(memory, kItems + kBottleSlot + i) == 0x12U) slot = kBottleSlot + i;
        if (slot == 0xFFU) return false;
    } else if (item >= 0x28 && item <= 0x2C) {
        slot = 5;
    } else if (item == 0x2D || item == 0x2E) {
        slot = 58U + static_cast<uint32_t>(item - 0x2D);
    } else if (item == 0x2F || item == 0x30) {
        slot = 17;
    } else if (item >= 0x32 && item <= 0x49) {
        for (uint32_t i = 0; i < 24U; ++i)
            if (kMaskSlots[i] == item) slot = 24U + i;
    }
    if (slot == 0xFFU) return false;
    memory.Write8(kItems + slot, static_cast<uint8_t>(item));
    GiveAmmo(memory, slot);
    return true;
}

// Ammo of the items you have up to what their bag or quiver holds.
void FillAmmo(oot3d::recomp::a32::MemoryBus& memory) {
    uint32_t upgrades = 0;
    memory.Read32(kUpgrades, &upgrades);
    static constexpr uint8_t kQuiver[] = {0, 30, 40, 50, 50, 50, 50, 50};
    static constexpr uint8_t kBombBag[] = {0, 20, 30, 40, 40, 40, 40, 40};
    static constexpr uint8_t kSticks[] = {10, 10, 20, 30, 30, 30, 30, 30};
    static constexpr uint8_t kNuts[] = {20, 20, 30, 40, 40, 40, 40, 40};
    const uint8_t bombs = kBombBag[(upgrades >> 3U) & 7U];
    const std::array<std::pair<uint32_t, uint8_t>, 7> slots{{
        {1, kQuiver[upgrades & 7U]},          // bow
        {6, bombs},                           // bombs
        {7, bombs},                           // bombchus
        {8, kSticks[(upgrades >> 17U) & 7U]}, // deku sticks
        {9, kNuts[(upgrades >> 20U) & 7U]},   // deku nuts
        {10, 20},                             // magic beans
        {12, 1},                              // powder keg
    }};
    for (const auto& [slot, capacity] : slots) {
        if (capacity == 0U || Read8(memory, kItems + slot) == 0xFFU) continue;
        if (Read8(memory, kAmmo + slot) != capacity) memory.Write8(kAmmo + slot, capacity);
    }
}

void CompleteQuest(oot3d::recomp::a32::MemoryBus& memory, int parts) {
    if ((parts & kMm3dQuestItems) != 0) {
        for (int item = 0x00; item <= 0x10; ++item) GiveItem(memory, item);
        for (uint32_t i = 0; i < kBottleCount; ++i)
            if (Read8(memory, kItems + kBottleSlot + i) == 0xFFU) memory.Write8(kItems + kBottleSlot + i, 0x12);
    }
    if ((parts & kMm3dQuestMasks) != 0)
        for (uint32_t i = 0; i < 24U; ++i) memory.Write8(kItems + 24U + i, kMaskSlots[i]);
    if ((parts & kMm3dQuestSongs) != 0) {
        uint32_t quest = 0;
        memory.Read32(kQuestStatus, &quest);
        // Remains, Sonata .. Oath (6-10), Time .. Storms (12-16), notebook.
        quest |= 0xFU | 0x7C0U | 0x1F000U | (1U << 18U);
        memory.Write32(kQuestStatus, quest);
    }
    if ((parts & kMm3dQuestGear) != 0) {
        memory.Write8(kSwordShield, 0x23);  // Gilded Sword, Mirror Shield
        for (uint32_t i = 0; i < 20U; ++i) {
            const uint8_t button = Read8(memory, kButtonItems + i);
            if (button == 0x4DU || button == 0x4EU) memory.Write8(kButtonItems + i, 0x4F);
        }
        memory.Write8(kItems + 16U, 0x10);  // Great Fairy's Sword
        uint32_t upgrades = 0;
        memory.Read32(kUpgrades, &upgrades);
        memory.Write32(kUpgrades, (upgrades & ~0x303FU) | 0x1BU | 0x2000U);  // quiver 50, bombs 40, 500
    }
    if ((parts & kMm3dQuestHearts) != 0) {
        memory.Write16(kHealthMax, 0x140);
        memory.Write16(kHealth, 0x140);
        uint32_t quest = 0;
        memory.Read32(kQuestStatus, &quest);
        memory.Write32(kQuestStatus, quest & 0x0FFFFFFFU);  // no loose heart pieces
        memory.Write8(kMagicLevel, 2);
        memory.Write8(kMagic, 0x60);
        for (uint32_t i = 0; i < 3U; ++i) memory.Write8(kMagicAcquired + i, 1);
    }
    if ((parts & (kMm3dQuestItems | kMm3dQuestGear)) != 0) FillAmmo(memory);
}

void ApplyBossFight(Mm3dCheats& cheats, oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx) {
    const uint16_t scene = Read16(memory, gctx + 0x148U);
    const auto restore = [&] {
        if (!cheats.KeepBossWin.load()) {
            if (cheats.FightFlagAddress != 0U) {
                const uint8_t now = Read8(memory, cheats.FightFlagAddress);
                memory.Write8(cheats.FightFlagAddress,
                              static_cast<uint8_t>((now & ~cheats.FightFlagMask) | cheats.FightFlagWas));
            }
            if (cheats.FightQuestBit != 0U) {
                uint32_t quest = 0;
                memory.Read32(kQuestStatus, &quest);
                memory.Write32(kQuestStatus, (quest & ~cheats.FightQuestBit) | cheats.FightQuestWas);
            }
            if (Read16(memory, kHealthMax) > cheats.FightHeartsWas) {
                memory.Write16(kHealthMax, cheats.FightHeartsWas);
                if (Read16(memory, kHealth) > cheats.FightHeartsWas) memory.Write16(kHealth, cheats.FightHeartsWas);
            }
        }
        std::fprintf(stderr, "TRIAEVUM_BOSS_FIGHT over scene=0x%X keep=%d flag=%02X\n", scene,
                     cheats.KeepBossWin.load() ? 1 : 0,
                     cheats.FightFlagAddress != 0U ? Read8(memory, cheats.FightFlagAddress) : 0U);
        cheats.FightScene = -1;
        cheats.BossFightActive = false;
    };
    if (cheats.FightScene >= 0) {
        ++cheats.FightFrames;
        if (std::getenv("TRIAEVUM_CHEAT_TRACE") != nullptr && cheats.FightFrames % 30 == 0)
            std::fprintf(stderr, "TRIAEVUM_BOSS_FIGHT scene=0x%X arrived=%d\n", scene, cheats.FightArrived ? 1 : 0);
        if (!cheats.FightArrived && scene == cheats.FightScene) cheats.FightArrived = true;
        // Left the lair (won, died, warped away), or the warp never went.
        if ((cheats.FightArrived && scene != cheats.FightScene) || (!cheats.FightArrived && cheats.FightFrames > 900))
            restore();
    }
    const int index = cheats.FightBoss.exchange(-1);
    if (index < 0 || index >= static_cast<int>(std::size(kBosses)) || cheats.FightScene >= 0) return;
    const Mm3dBoss& boss = kBosses[index];
    // The boss comes back: its temple counts as not cleared while you are
    // in the lair (and its remains and heart container are given back to
    // what you had when you leave).
    cheats.FightFlagAddress = boss.WeekEvent >= 0 ? kWeekEvents + static_cast<uint32_t>(boss.WeekEvent) : 0U;
    cheats.FightFlagMask = static_cast<uint8_t>(boss.WeekMask);
    cheats.FightFlagWas = cheats.FightFlagAddress != 0U ? Read8(memory, cheats.FightFlagAddress) & boss.WeekMask : 0U;
    if (cheats.FightFlagAddress != 0U)
        memory.Write8(cheats.FightFlagAddress, Read8(memory, cheats.FightFlagAddress) & ~boss.WeekMask);
    uint32_t quest = 0;
    memory.Read32(kQuestStatus, &quest);
    cheats.FightQuestBit = boss.Remains >= 0 ? 1U << boss.Remains : 0U;
    cheats.FightQuestWas = quest & cheats.FightQuestBit;
    cheats.FightHeartsWas = Read16(memory, kHealthMax);
    cheats.FightScene = boss.Scene;
    cheats.FightFrames = 0;
    cheats.FightArrived = scene == boss.Scene;
    cheats.BossFightActive = true;
    // The F1 warp's request: next entrance (play +0xC52E), transition (+0xC529).
    memory.Write16(gctx + 0xC52EU, static_cast<uint16_t>(boss.Entrance << 9));
    memory.Write8(gctx + 0xC529U, 0x14);
    std::fprintf(stderr, "TRIAEVUM_BOSS_FIGHT %s scene=0x%X flag_was=%u\n", boss.Name, boss.Scene, cheats.FightFlagWas);
}

void ApplySaveCheats(Mm3dCheats& cheats, oot3d::recomp::a32::MemoryBus& memory) {
    if (const int item = cheats.GiveItem.exchange(-1); item >= 0) GiveItem(memory, item);
    if (const int parts = cheats.CompleteQuest.exchange(0); parts != 0) {
        CompleteQuest(memory, parts);
        if (std::getenv("TRIAEVUM_CHEAT_TRACE") != nullptr) {
            std::fprintf(stderr, "TRIAEVUM_QUEST items:");
            for (uint32_t i = 0; i < 0x60U; ++i) std::fprintf(stderr, " %02X", Read8(memory, kItems + i));
            uint32_t upgrades = 0, quest = 0;
            memory.Read32(kUpgrades, &upgrades);
            memory.Read32(kQuestStatus, &quest);
            std::fprintf(stderr, "\nTRIAEVUM_QUEST upgrades=%08X quest=%08X\n", upgrades, quest);
        }
    }
    if (const int hearts = cheats.SetMaxHearts.exchange(-1); hearts >= 3 && hearts <= 20) {
        memory.Write16(kHealthMax, static_cast<uint16_t>(hearts * 0x10));
        memory.Write16(kHealth, static_cast<uint16_t>(hearts * 0x10));
    }
    if (cheats.InfiniteHealth.load()) {
        const uint16_t max = Read16(memory, kHealthMax);
        if (max != 0U && Read16(memory, kHealth) != max) memory.Write16(kHealth, max);
    }
    if (cheats.InfiniteMagic.load()) {
        const uint8_t level = Read8(memory, kMagicLevel);
        if (level >= 1U && level <= 2U && Read8(memory, kMagic) != level * 0x30U)
            memory.Write8(kMagic, static_cast<uint8_t>(level * 0x30U));
    }
    uint32_t upgrades = 0;
    memory.Read32(kUpgrades, &upgrades);
    if (cheats.InfiniteRupees.load()) {
        static constexpr uint16_t kWallet[] = {99, 200, 500, 500};
        const uint16_t capacity = kWallet[(upgrades >> 12U) & 3U];
        if (Read16(memory, kRupees) != capacity) memory.Write16(kRupees, capacity);
    }
    if (cheats.InfiniteAmmo.load()) FillAmmo(memory);
    if (cheats.UnbreakableRazorSword.load() && (Read8(memory, kSwordShield) & 0xFU) == 2U &&
        Read16(memory, kRazorSwordHits) != 100U)
        memory.Write16(kRazorSwordHits, 100);

    if (const int time = cheats.SetTime.exchange(-1); time >= 0) {
        memory.Write16(kSaveTime, static_cast<uint16_t>(time));
        cheats.FrozenTime = static_cast<uint16_t>(time);
    }
    if (cheats.FreezeTime.load()) {
        const uint16_t now = Read16(memory, kSaveTime);
        // The clock runs a few units a frame; a bigger jump is the game's own
        // (Song of Time, a new day, a time skip): the frozen time follows it.
        const uint16_t step = static_cast<uint16_t>(now - cheats.FrozenTime);
        if (!cheats.TimeFrozen || (step > 0x40U && step < 0xFFC0U)) cheats.FrozenTime = now;
        cheats.TimeFrozen = true;
        if (now != cheats.FrozenTime) memory.Write16(kSaveTime, cheats.FrozenTime);
    } else {
        cheats.TimeFrozen = false;
    }
}

void ApplyPlayerCheats(Mm3dCheats& cheats, oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx) {
    uint32_t player = 0, flags1 = 0, buttons = 0;
    if (!memory.Read32(gctx + 0x20DCU, &player) || player < 0x100000U ||
        !memory.Read32(player + 0x11DB0U, &flags1) || !memory.Read32(gctx + 0x30U, &buttons)) {
        cheats.Flying = false;
        return;
    }
    static FILE* trace = [] {
        const char* path = std::getenv("TRIAEVUM_CHEAT_TRACE");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (trace != nullptr) {
        std::fprintf(trace, "pos %.2f %.2f %.2f vel %.2f %.2f %.2f speed %.2f grav %.2f yaw %04X flags1 %08X buttons %08X "
                     "hp %u/%u magic %u rupees %u time %04X\n",
                     ReadFloat(memory, player + 0x24U), ReadFloat(memory, player + 0x28U),
                     ReadFloat(memory, player + 0x2CU), ReadFloat(memory, player + 0x64U),
                     ReadFloat(memory, player + 0x68U), ReadFloat(memory, player + 0x6CU),
                     ReadFloat(memory, player + 0x70U), ReadFloat(memory, player + 0x74U),
                     Read16(memory, player + 0x32U), flags1, buttons, Read16(memory, kHealth),
                     Read16(memory, kHealthMax), Read8(memory, kMagic), Read16(memory, kRupees),
                     Read16(memory, kSaveTime));
        std::fflush(trace);
    }
    // Cutscenes, death, held still by the game: Link is left alone.
    const bool free = (flags1 & 0x20000280U) == 0U;
    const bool fly = cheats.Fly.load() && free;
    if (fly) {
        const float stickX = ReadFloat(memory, gctx + 0x58U), stickY = ReadFloat(memory, gctx + 0x5CU);
        const float vertical = ((buttons & kButtonA) != 0U ? 1.0F : 0.0F) - ((buttons & kButtonB) != 0U ? 1.0F : 0.0F);
        ConsumeButtons(memory, gctx, kButtonA | kButtonB);
        const float speed = std::clamp(cheats.FlySpeed.load(), 4.0F, 60.0F);
        uint16_t index = 0;
        memory.Read16(gctx + 0xA7AU, &index);
        const uint32_t camera = gctx + 0x408U + std::min<uint16_t>(index, 3) * 0x198U;
        const float cameraYaw = std::atan2(ReadFloat(memory, camera + 0x50U) - ReadFloat(memory, camera + 0x5CU),
                                           ReadFloat(memory, camera + 0x58U) - ReadFloat(memory, camera + 0x64U));
        // Stick up flies away from the camera, right to the screen's right.
        const float dx = std::sin(cameraYaw) * stickY - std::cos(cameraYaw) * stickX;
        const float dz = std::cos(cameraYaw) * stickY + std::sin(cameraYaw) * stickX;
        const float length = std::sqrt(dx * dx + dz * dz);
        const float scale = length > 1.0F ? speed / length : speed;
        // Across: Link is moved (the game's own air control is too slow;
        // the collision still pushes him out of thin walls and onto floors).
        if (length > 0.1F) {
            WriteFloat(memory, player + 0x24U, ReadFloat(memory, player + 0x24U) + dx * scale);
            WriteFloat(memory, player + 0x2CU, ReadFloat(memory, player + 0x2CU) + dz * scale);
        }
        WriteFloat(memory, player + 0x70U, 0.0F);
        // Up and down by the game's own vertical speed (floors stop him),
        // less what gravity takes off it each frame (measured on the last
        // frame: two thirds of the actor's gravity at 30 fps), so he hovers
        // with nothing held.
        const float velocityY = ReadFloat(memory, player + 0x68U);
        if (!cheats.Flying || !std::isfinite(cheats.FlyGravityStep))
            cheats.FlyGravityStep = ReadFloat(memory, player + 0x74U) * (2.0F / 3.0F);
        else if (const float step = velocityY - cheats.FlyWrittenVelocityY; step > -4.0F && step < 0.5F)
            cheats.FlyGravityStep = step;
        cheats.FlyWrittenVelocityY = vertical * speed * 0.6F - cheats.FlyGravityStep;
        WriteFloat(memory, player + 0x68U, cheats.FlyWrittenVelocityY);
        cheats.Flying = true;
        return;
    }
    cheats.Flying = false;
    if (cheats.MoonJump.load() && free && (buttons & (kButtonL | kButtonA)) == (kButtonL | kButtonA)) {
        ConsumeButtons(memory, gctx, kButtonA);
        WriteFloat(memory, player + 0x68U, 8.0F);
    }
}

}  // namespace

void ApplyMm3dCheats(Mm3dCheats& cheats, oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx) {
    static const bool fromEnvironment = [&cheats] {
        // Tests: TRIAEVUM_CHEATS=fly,moonjump,health,magic,rupees,ammo,razor,time,quest,boss=<index>
        const char* list = std::getenv("TRIAEVUM_CHEATS");
        if (list == nullptr) return false;
        const auto has = [list](const char* name) { return std::strstr(list, name) != nullptr; };
        cheats.Fly = has("fly");
        cheats.MoonJump = has("moonjump");
        cheats.InfiniteHealth = has("health");
        cheats.InfiniteMagic = has("magic");
        cheats.InfiniteRupees = has("rupees");
        cheats.InfiniteAmmo = has("ammo");
        cheats.UnbreakableRazorSword = has("razor");
        cheats.FreezeTime = has("time");
        if (has("quest")) cheats.CompleteQuest = 31;

        return true;
    }();
    (void)fromEnvironment;
    // Tests: TRIAEVUM_CHEATS=...,boss=<index> asks for that boss fight after
    // 2 s of play (after TRIAEVUM_POKE).
    static int bossTestFrames = 0;
    if (++bossTestFrames == 60)
        if (const char* list = std::getenv("TRIAEVUM_CHEATS"); list != nullptr && std::strstr(list, "boss=") != nullptr)
            cheats.FightBoss = std::atoi(std::strstr(list, "boss=") + 5);
    ApplySaveCheats(cheats, memory);
    ApplyBossFight(cheats, memory, gctx);
    ApplyPlayerCheats(cheats, memory, gctx);
}

}  // namespace Oot3dNativeGame

namespace Oot3dNativeGame {
namespace {

// USA v1.1 code (found by their use of the actor table and object slots):
// the actor table (0x20-byte entries, ActorInit* at +0x14; the init's object
// id at +8, category at +2), Actor_Spawn(actorCtx, play, id, rotX, x, y, z
// in s0-s2; rotY, rotZ, params on the stack), the object slots at play +
// 0x9438 (find, request a load, loaded?) and the actor context at +0x20B0
// (category lists from +0x10, 0xC each: count, first; next actor +0x134).
constexpr uint32_t kActorTable = 0x657768U;
constexpr uint32_t kActorCount = 0x22CU;
constexpr uint32_t kActorSpawn = 0x22053CU;
constexpr uint32_t kObjectFind = 0x1F55CCU;
constexpr uint32_t kObjectRequest = 0x1F3F5CU;
constexpr uint32_t kObjectLoaded = 0x1DF798U;
constexpr uint32_t kObjectFree = 0x1F4FB0U;  // (objects, slot)
constexpr uint32_t kCallReturn = 0x60000000U;

bool CallGuest(NativeA32Process& process, uint32_t entry, std::initializer_list<uint32_t> registers,
               std::initializer_list<float> floats, std::initializer_list<uint32_t> stack, uint32_t* result) {
    auto state = process.PrimaryThreadState();
    auto& memory = process.Memory();
    // Well below the waiting main thread's stack pointer.
    const uint32_t sp = (state.r[13] - 0x1000U) & ~7U;
    if (!memory.IsWritable(sp - 0x4000U, 0x4000U + stack.size() * 4U)) return false;
    uint32_t index = 0;
    for (const uint32_t value : stack) memory.Write32(sp + 4U * index++, value);
    index = 0;
    for (const uint32_t value : registers) state.r[index++] = value;
    index = 0;
    for (const float value : floats) state.vfp[index++] = std::bit_cast<uint32_t>(value);
    state.r[13] = sp;
    std::string error;
    if (!process.InvokeFunctionWithState(entry, state, kCallReturn, &error)) {
        std::fprintf(stderr, "oot3d_native_game: cheat call %08X failed: %s\n", entry, error.c_str());
        return false;
    }
    if (result != nullptr) *result = state.r[0];
    return true;
}

// Whether an actor still uses this object slot (its slot at actor +0x1E).
bool ObjectSlotUsed(oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx, uint32_t slot) {
    for (uint32_t category = 0; category < 12U; ++category) {
        uint32_t node = 0;
        memory.Read32(gctx + 0x20B0U + 0x10U + category * 0xCU + 4U, &node);
        for (int guard = 0; guard < 400 && node >= 0x100000U; ++guard) {
            if (Read8(memory, node + 0x1EU) == slot) return true;
            if (!memory.Read32(node + 0x134U, &node)) break;
        }
    }
    return false;
}

// In any category list (some actors move to another one in their init).
bool ActorAlive(oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx, uint32_t actor) {
    for (uint32_t category = 0; category < 12U; ++category) {
        uint32_t node = 0;
        memory.Read32(gctx + 0x20B0U + 0x10U + category * 0xCU + 4U, &node);
        for (int guard = 0; guard < 400 && node >= 0x100000U; ++guard) {
            if (node == actor) return true;
            if (!memory.Read32(node + 0x134U, &node)) break;
        }
    }
    return false;
}

}  // namespace

void ProcessMm3dSpawn(Mm3dCheats& cheats, NativeA32Process& process, uint32_t gctx) {
    auto& memory = process.Memory();
    // Tests: TRIAEVUM_SPAWN_TEST=id[:params[/object...]],... one every 90 game frames,
    // each checked 45 frames later.
    struct SpawnTest {
        int Id;
        int Params;
        uint64_t Extra;
    };
    static std::vector<SpawnTest> tests = [] {
        std::vector<SpawnTest> list;
        if (const char* text = std::getenv("TRIAEVUM_SPAWN_TEST")) {
            for (const char* p = text; *p != '\0';) {
                char* end = nullptr;
                const int id = static_cast<int>(std::strtol(p, &end, 0));
                int params = 0;
                uint64_t extra = 0;
                if (*end == ':') params = static_cast<int>(std::strtol(end + 1, &end, 0));
                for (int shift = 0; *end == '/' && shift < 64; shift += 16)
                    extra |= static_cast<uint64_t>(std::strtoul(end + 1, &end, 0) & 0xFFFFU) << shift;
                list.push_back({id, params, extra});
                p = *end == ',' ? end + 1 : end;
                if (end == p && *p != '\0') break;
            }
        }
        return list;
    }();
    static size_t nextTest = 0;
    static int testFrames = 0;
    static uint32_t testActor = 0;
    static int testId = -1;
    if (!tests.empty()) {
        ++testFrames;
        if (testFrames == 45 && testId >= 0)
            std::fprintf(stderr, "TRIAEVUM_SPAWN_CHECK id=0x%X alive=%d\n", testId,
                         testActor != 0U && ActorAlive(memory, gctx, testActor) ? 1 : 0);
        if (testFrames >= 90 && nextTest < tests.size()) {
            testFrames = 0;
            cheats.SpawnParams = tests[nextTest].Params;
            cheats.SpawnExtraObjects = tests[nextTest].Extra;
            cheats.SpawnId = tests[nextTest].Id;
            ++nextTest;
        }
    }

    if (const int id = cheats.SpawnId.exchange(-1); id >= 0) {
        cheats.PendingSpawnId = id;
        cheats.PendingSpawnParams = cheats.SpawnParams.load();
        cheats.PendingSpawnFrames = 0;
        cheats.PendingSpawnObjects.clear();
        cheats.RequestedObjects.clear();
        uint32_t init = 0;
        if (static_cast<uint32_t>(id) < kActorCount && memory.Read32(kActorTable + id * 0x20U + 0x14U, &init) &&
            init >= 0x100000U)
            cheats.PendingSpawnObjects.push_back(static_cast<int16_t>(Read16(memory, init + 8U)));
        for (uint64_t extra = cheats.SpawnExtraObjects.load(); extra != 0U; extra >>= 16U)
            if ((extra & 0xFFFFU) != 0U) cheats.PendingSpawnObjects.push_back(static_cast<int16_t>(extra & 0xFFFFU));
    }
    const int id = cheats.PendingSpawnId;
    if (id < 0) return;
    const auto fail = [&](const char* why) {
        cheats.SetSpawnStatus(why);
        std::fprintf(stderr, "TRIAEVUM_SPAWN id=0x%X failed: %s\n", id, why);
        cheats.PendingSpawnId = -1;
        testId = -1;
    };
    uint32_t init = 0;
    if (static_cast<uint32_t>(id) >= kActorCount || !memory.Read32(kActorTable + id * 0x20U + 0x14U, &init) ||
        init < 0x100000U)
        return fail("No such actor.");
    const uint32_t objects = gctx + 0x9438U;
    // Its objects (models, animations; a boss can need more than its own) are
    // loaded first if this area does not have them.
    bool waiting = false;
    for (const int16_t objectId : cheats.PendingSpawnObjects) {
        uint32_t slot = 0;
        if (!CallGuest(process, kObjectFind, {objects, static_cast<uint32_t>(objectId)}, {}, {}, &slot))
            return fail("The game refused the call.");
        if (static_cast<int32_t>(slot) >= 0) {
            uint32_t loaded = 0;
            CallGuest(process, kObjectLoaded, {objects, slot}, {}, {}, &loaded);
            waiting |= loaded == 0U;
            continue;
        }
        if (std::find(cheats.RequestedObjects.begin(), cheats.RequestedObjects.end(), objectId) !=
            cheats.RequestedObjects.end())
            return fail("No room for its object here: leave the area or defeat what you spawned.");
        // The area has 36 object slots: objects loaded for earlier spawns
        // that no actor uses any more make room (a slot that holds another
        // object now, after an area change, is not ours any more).
        for (auto it = cheats.LoadedObjects.begin(); it != cheats.LoadedObjects.end();) {
            const auto [used, usedObject] = *it;
            const auto holds = static_cast<int16_t>(Read16(memory, objects + used * 0xACU + 4U));
            if (holds != usedObject && holds != -usedObject) {
                it = cheats.LoadedObjects.erase(it);
            } else if (!ObjectSlotUsed(memory, gctx, used) &&
                       std::find(cheats.PendingSpawnObjects.begin(), cheats.PendingSpawnObjects.end(),
                                 usedObject) == cheats.PendingSpawnObjects.end()) {
                CallGuest(process, kObjectFree, {objects, used}, {}, {}, nullptr);
                it = cheats.LoadedObjects.erase(it);
            } else {
                ++it;
            }
        }
        uint32_t requested = 0;
        CallGuest(process, kObjectRequest, {objects, static_cast<uint32_t>(objectId)}, {}, {}, &requested);
        if (static_cast<int32_t>(requested) >= 0) cheats.LoadedObjects.emplace_back(requested, objectId);
        cheats.RequestedObjects.push_back(objectId);
        waiting = true;
    }
    if (waiting) {
        if (++cheats.PendingSpawnFrames > 300) return fail("Its object did not load.");
        cheats.SetSpawnStatus("Loading...");
        return;
    }
    // In front of Link, facing him.
    uint32_t player = 0;
    if (!memory.Read32(gctx + 0x20DCU, &player) || player < 0x100000U) return fail("Link is not here.");
    const uint16_t yaw = Read16(memory, player + 0x32U);
    const float angle = static_cast<float>(yaw) * (3.14159265F / 32768.0F);
    const float x = ReadFloat(memory, player + 0x24U) + std::sin(angle) * 120.0F;
    const float y = ReadFloat(memory, player + 0x28U);
    const float z = ReadFloat(memory, player + 0x2CU) + std::cos(angle) * 120.0F;
    uint32_t actor = 0;
    if (!CallGuest(process, kActorSpawn,
                   {gctx + 0x20B0U, gctx, static_cast<uint32_t>(id), 0U}, {x, y, z},
                   {static_cast<uint16_t>(yaw + 0x8000U), 0U,
                    static_cast<uint32_t>(cheats.PendingSpawnParams) & 0xFFFFU},
                   &actor) ||
        actor == 0U)
        return fail("The game did not spawn it here.");
    std::fprintf(stderr, "TRIAEVUM_SPAWN id=0x%X params=0x%X objects=%zu actor=%08X\n", id,
                 cheats.PendingSpawnParams, cheats.PendingSpawnObjects.size(), actor);
    cheats.SetSpawnStatus("Spawned.");
    cheats.PendingSpawnId = -1;
    testId = id;
    testActor = actor;
}

}  // namespace Oot3dNativeGame

namespace Oot3dNativeGame {

const Mm3dBoss* Mm3dBosses(int* count) {
    *count = static_cast<int>(std::size(kBosses));
    return kBosses;
}

}  // namespace Oot3dNativeGame
