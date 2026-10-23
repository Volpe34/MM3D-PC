#pragma once
// MM3D's F1 > Cheats: switches the play state's update applies to the save
// and to Link once per game frame (before the actors' update).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
}  // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

class NativeA32Process;

// F1 > Cheats > Complete quest parts.
enum Mm3dQuestPart : int {
    kMm3dQuestItems = 1,      // every item (no trade items), 6 bottles
    kMm3dQuestMasks = 2,      // all 24 masks
    kMm3dQuestSongs = 4,      // remains, songs, Bombers' Notebook
    kMm3dQuestGear = 8,       // Gilded Sword, Mirror Shield, biggest quiver/bomb bag/wallet
    kMm3dQuestHearts = 16,    // 20 hearts, double magic, double defense
};

struct Mm3dCheats {
    std::atomic<bool> InfiniteHealth{false};
    std::atomic<bool> InfiniteMagic{false};
    std::atomic<bool> InfiniteRupees{false};
    std::atomic<bool> InfiniteAmmo{false};
    std::atomic<bool> UnbreakableRazorSword{false};
    std::atomic<bool> FreezeTime{false};
    std::atomic<bool> MoonJump{false};
    std::atomic<bool> Fly{false};
    std::atomic<float> FlySpeed{18.0F};
    // One-shot requests from the panel (-1 none).
    std::atomic<int> SetTime{-1};       // save.time (0x4000 = 6 AM)
    std::atomic<int> SetMaxHearts{-1};  // heart containers
    std::atomic<int> GiveItem{-1};      // item id (N64 numbering)
    // Fight a boss in its lair (index into Mm3dBosses()); restore afterwards
    // unless KeepBossWin.
    std::atomic<int> FightBoss{-1};
    std::atomic<bool> KeepBossWin{false};
    std::atomic<bool> BossFightActive{false};
    std::atomic<int> CompleteQuest{0};  // Mm3dQuestPart bits
    // Spawn: actor id (MM3D numbering) and params; the status for the panel.
    std::atomic<int> SpawnId{-1};
    std::atomic<int> SpawnParams{0};
    std::atomic<uint64_t> SpawnExtraObjects{0};  // up to 4 more object ids it needs (16 bits each)
    std::atomic<bool> InPlay{false};
    std::string SpawnStatus() const {
        std::scoped_lock lock(StatusMutex);
        return Status;
    }
    void SetSpawnStatus(std::string text) {
        std::scoped_lock lock(StatusMutex);
        Status = std::move(text);
    }

    // Game thread only.
    bool TimeFrozen = false;
    uint16_t FrozenTime = 0;
    bool Flying = false;
    float FlyWrittenVelocityY = 0.0F;
    float FlyGravityStep = 0.0F;
    // Boss fight under way (game thread).
    int FightScene = -1;
    int FightFrames = 0;
    bool FightArrived = false;
    uint32_t FightFlagAddress = 0;
    uint8_t FightFlagMask = 0, FightFlagWas = 0;
    uint32_t FightQuestBit = 0, FightQuestWas = 0;
    uint16_t FightHeartsWas = 0;
    int PendingSpawnId = -1;
    int PendingSpawnParams = 0;
    int PendingSpawnFrames = 0;
    std::vector<int16_t> PendingSpawnObjects;    // its object first, then the extra ones
    std::vector<int16_t> RequestedObjects;       // loads asked for this spawn
    std::vector<std::pair<uint32_t, int16_t>> LoadedObjects;  // object slots loaded for spawns

  private:
    mutable std::mutex StatusMutex;
    std::string Status;
};

struct Mm3dBoss {
    const char* Name;
    const char* Where;
    int Entrance;   // entrance scene (the entrance id's top 7 bits)
    int Scene;      // scene id (play +0x148), numbered apart from entrances
    int WeekEvent;  // its "temple cleared" flag (byte, mask), -1 none
    int WeekMask;
    int Remains;    // quest status bit, -1 none
};
// Odolwa, Goht, Gyorg, Twinmold, Majora, Igos du Ikana.
const Mm3dBoss* Mm3dBosses(int* count);

// From the play state's calc function (gctx = the GlobalContext, in play).
void ApplyMm3dCheats(Mm3dCheats& cheats, oot3d::recomp::a32::MemoryBus& memory, uint32_t gctx);

// Spawns the requested actor (its object loaded first). Calls game code: only
// from the main loop while the guest waits, once per game frame in play.
void ProcessMm3dSpawn(Mm3dCheats& cheats, NativeA32Process& process, uint32_t gctx);

}  // namespace Oot3dNativeGame
