#pragma once
// MM3D's own F1 tabs: Warp (any area of the game, by its entrances) and
// Buttons (the slot buttons' letters moved and resized with the mouse).

#include "fast/oot3d/graphics_settings_window.h"
#include "mm3d_hud_buttons.h"

#include <array>
#include <atomic>
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
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dButtonLabelPanel(
    std::shared_ptr<Mm3dButtonLabelEditor> editor);

}  // namespace Oot3dNativeGame
