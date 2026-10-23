#pragma once
// MM3D's own F1 tabs: Warp (any area of the game, by its entrances), Cheats,
// Buttons (the slot buttons' letters moved and resized with the mouse) and
// Save states.

#include "fast/oot3d/graphics_settings_window.h"
#include "mm3d_cheats.h"
#include "mm3d_hud_buttons.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace Oot3dNativeGame {

// A warp asked for in F1 > Warp; the main loop does it in the play state.
struct Mm3dWarpRequest {
    std::atomic<int> Entrance{-1};  // scene << 9 | spawn << 4
    std::atomic<int> Form{-1};      // -1 keep; else save.player_form (0 Fierce Deity .. 4 human)
    std::atomic<bool> InPlay{false};
};

// Save state slots (F1 > Save states; F5 saves the selected slot, F8 loads
// it, F6/F7 select the previous/next one). Slot 1 is quick.oot3dsav, slot N
// quick_N.oot3dsav next to it. The main loop saves and loads at the end of a
// frame and reports back with Report (shown on screen for a few seconds).
class SaveStateSlots {
  public:
    static constexpr int kSlots = 10;
    explicit SaveStateSlots(std::filesystem::path firstSlot) : mFirstSlot(std::move(firstSlot)) {}
    std::filesystem::path Path(int slot) const;
    int Selected() const { return mSelected.load(); }
    void Select(int slot);
    // Requests from the panel or the keys; Take* returns the slot once (0: none).
    void RequestSave(int slot) { mSaveSlot = slot; }
    void RequestLoad(int slot) { mLoadSlot = slot; }
    int TakeSave() { return mSaveSlot.exchange(0); }
    int TakeLoad() { return mLoadSlot.exchange(0); }
    bool Pending() const { return mSaveSlot.load() != 0 || mLoadSlot.load() != 0; }
    void Report(std::string message);
    // The last report while it is fresh (empty after a few seconds).
    std::string Message(double seconds = 3.0) const;
    std::string LastMessage() const;
    uint32_t Revision() const { return mRevision.load(); }

  private:
    std::filesystem::path mFirstSlot;
    std::atomic<int> mSelected{1};
    std::atomic<int> mSaveSlot{0};
    std::atomic<int> mLoadSlot{0};
    std::atomic<uint32_t> mRevision{1};
    mutable std::mutex mMutex;
    std::string mMessage;
    std::chrono::steady_clock::time_point mMessageTime{};
};

// The slot buttons' letters as the player placed them (F1 > Buttons),
// saved next to the settings.
class Mm3dButtonLabelEditor {
  public:
    explicit Mm3dButtonLabelEditor(std::filesystem::path path);
    Mm3dButtonLabelLayout Label(int slot) const;
    void SetLabel(int slot, const Mm3dButtonLabelLayout& layout);
    uint32_t Revision() const { return mRevision.load(); }
    bool Save();
    const std::string& Error() const { return mError; }

    // Where the main loop drew each slot's sprite last (window fractions:
    // x, y, width, height) and its label.
    void SetSprite(int slot, const std::array<float, 4>& rect, const std::string& label);
    bool Sprite(int slot, std::array<float, 4>& rect, std::string& label) const;
    void ClearSprites();

  private:
    mutable std::mutex mMutex;
    std::filesystem::path mPath;
    std::array<Mm3dButtonLabelLayout, 4> mLabels{};
    std::array<std::array<float, 4>, 4> mRects{};
    std::array<std::string, 4> mSpriteLabels{};
    std::array<bool, 4> mVisible{};
    std::atomic<uint32_t> mRevision{1};
    std::string mError;
};

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dWarpPanel(std::shared_ptr<Mm3dWarpRequest> request);
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dCheatsPanel(std::shared_ptr<Mm3dCheats> cheats);
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dButtonLabelPanel(
    std::shared_ptr<Mm3dButtonLabelEditor> editor);
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateSaveStatePanel(std::shared_ptr<SaveStateSlots> slots);

}  // namespace Oot3dNativeGame
