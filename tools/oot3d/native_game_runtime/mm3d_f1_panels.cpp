#include "mm3d_f1_panels.h"

#include "ship/config/ConfigPersistence.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace Oot3dNativeGame {
namespace {

// The game's areas by entrance scene (the entrance's top 7 bits) with their
// entrance counts, in the order of 2Ship2Harkinian's map select (names and
// counts from its scene and entrance tables).
struct WarpArea {
    uint8_t Scene;
    const char* Name;
    uint8_t Entrances;
};
constexpr WarpArea kWarpAreas[] = {
    {0x6C, "South Clock Town", 11},
    {0x69, "East Clock Town", 13},
    {0x6A, "West Clock Town", 10},
    {0x6B, "North Clock Town", 8},
    {0x6D, "Laundry Pool", 3},
    {0x00, "The Mayor's Residence", 3},
    {0x26, "Astral Observatory", 3},
    {0x51, "Swordsman's School", 1},
    {0x2B, "Post Office", 1},
    {0x5E, "Stock Pot Inn", 6},
    {0x12, "Milk Bar", 1},
    {0x07, "Curiosity Shop", 4},
    {0x65, "Bomb Shop", 2},
    {0x31, "Trading Post", 2},
    {0x14, "Treasure Chest Shop", 2},
    {0x04, "Honey & Darling's Shop", 1},
    {0x1D, "Town Shooting Gallery", 2},
    {0x36, "Lottery Shop", 1},
    {0x17, "Before Clock Town", 5},
    {0x60, "Clock Tower Interior", 7},
    {0x16, "Clock Tower Rooftop", 3},
    {0x2A, "Termina Field", 15},
    {0x1F, "Milk Road", 7},
    {0x67, "Gorman Track", 6},
    {0x32, "Romani Ranch", 12},
    {0x03, "Mama's House & Barn", 2},
    {0x3F, "Cucco Shack", 2},
    {0x3E, "Doggy Racetrack", 2},
    {0x3D, "Road to Southern Swamp", 3},
    {0x21, "Swamp Shooting Gallery", 1},
    {0x42, "Southern Swamp (poison)", 11},
    {0x06, "Southern Swamp (clear)", 11},
    {0x54, "Tourist Information", 3},
    {0x02, "Magic Hags' Potion Shop", 1},
    {0x24, "Swamp Spider House", 1},
    {0x28, "Deku Palace", 11},
    {0x4F, "Deku Shrine", 3},
    {0x3B, "Deku King's Chamber", 4},
    {0x61, "Woods of Mystery", 1},
    {0x43, "Woodfall", 5},
    {0x18, "Woodfall Temple", 3},
    {0x1C, "Odolwa's Lair", 1},
    {0x19, "Path to Mountain Village", 2},
    {0x4D, "Mountain Village (winter)", 9},
    {0x57, "Mountain Village (spring)", 9},
    {0x29, "Mountain Smithy", 1},
    {0x4B, "Goron Graveyard", 2},
    {0x5A, "Path to Goron Village (winter)", 3},
    {0x5B, "Path to Goron Village (spring)", 3},
    {0x68, "Goron Racetrack", 3},
    {0x4A, "Goron Village (winter)", 5},
    {0x45, "Goron Village (spring)", 5},
    {0x2F, "Goron Shrine", 4},
    {0x3A, "Goron Shop", 1},
    {0x58, "Path to Snowhead", 2},
    {0x59, "Snowhead", 4},
    {0x1E, "Snowhead Temple", 2},
    {0x41, "Goht's Lair", 1},
    {0x34, "Great Bay Coast", 14},
    {0x5F, "Great Bay (cutscene)", 1},
    {0x2C, "Marine Research Lab", 1},
    {0x39, "Fisherman's Hut", 1},
    {0x25, "Oceanside Spider House", 1},
    {0x35, "Zora Cape", 10},
    {0x30, "Zora Hall", 9},
    {0x49, "Zora Hall Rooms", 7},
    {0x22, "Pinnacle Rock", 2},
    {0x47, "Waterfall Rapids", 4},
    {0x38, "Pirates' Fortress Moat", 7},
    {0x11, "Pirates' Fortress", 15},
    {0x20, "Pirates' Fortress Interior", 16},
    {0x46, "Great Bay Temple", 3},
    {0x5C, "Gyorg's Lair", 2},
    {0x50, "Road to Ikana", 3},
    {0x40, "Ikana Graveyard", 6},
    {0x05, "Beneath the Graveyard", 2},
    {0x2D, "Dampe's House", 2},
    {0x10, "Ikana Canyon", 16},
    {0x4C, "Sakon's Hideout", 1},
    {0x52, "Music Box House", 1},
    {0x48, "Beneath the Well", 2},
    {0x4E, "Ghost Hut", 3},
    {0x5D, "Secret Shrine", 1},
    {0x1A, "Ancient Castle of Ikana", 7},
    {0x53, "Igos du Ikana's Lair", 1},
    {0x55, "Stone Tower", 4},
    {0x56, "Inverted Stone Tower", 2},
    {0x13, "Stone Tower Temple", 2},
    {0x15, "Inverted Stone Tower Temple", 3},
    {0x33, "Twinmold's Lair", 5},
    {0x66, "Giants' Chamber", 1},
    {0x64, "The Moon", 1},
    {0x27, "Moon Deku Trial", 1},
    {0x3C, "Moon Goron Trial", 1},
    {0x44, "Moon Zora Trial", 2},
    {0x63, "Moon Link Trial", 1},
    {0x01, "Majora's Lair", 1},
    {0x23, "Fairy's Fountain", 10},
    {0x1B, "Deku Scrub Playground", 2},
    {0x62, "Lost Woods (intro)", 3},
    {0x0A, "Lone Peak Shrine & Grottos", 17},
};

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

class WarpPanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
  public:
    explicit WarpPanel(std::shared_ptr<Mm3dWarpRequest> request) : mRequest(std::move(request)) {}
    const char* Label() const noexcept override { return "Warp"; }
    void Draw() override {
        ImGui::TextWrapped("Go to any area of the game. Entrance picks where you arrive (the area's doors and "
                           "paths, 1 is the usual one).");
        ImGui::SetNextItemWidth(260.0F);
        ImGui::InputTextWithHint("##filter", "Search areas", mFilter, sizeof(mFilter));
        const std::string filter = Lower(mFilter);
        if (ImGui::BeginListBox("##areas", ImVec2(-1.0F, 260.0F))) {
            for (size_t i = 0; i < std::size(kWarpAreas); ++i) {
                if (!filter.empty() && Lower(kWarpAreas[i].Name).find(filter) == std::string::npos) continue;
                const bool selected = mSelected == static_cast<int>(i);
                if (ImGui::Selectable(kWarpAreas[i].Name, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!selected) mEntrance = 1;
                    mSelected = static_cast<int>(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Warp();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
        const auto& area = kWarpAreas[static_cast<size_t>(std::clamp<int>(mSelected, 0, std::size(kWarpAreas) - 1))];
        if (area.Entrances > 1) {
            ImGui::SetNextItemWidth(260.0F);
            ImGui::SliderInt("Entrance", &mEntrance, 1, area.Entrances, "%d", ImGuiSliderFlags_AlwaysClamp);
        } else {
            mEntrance = 1;
        }
        static constexpr const char* kForms[] = {"Keep current", "Hylian (Link)", "Deku", "Goron", "Zora",
                                                 "Fierce Deity"};
        ImGui::SetNextItemWidth(260.0F);
        ImGui::Combo("Form", &mForm, kForms, static_cast<int>(std::size(kForms)));
        const bool inPlay = mRequest->InPlay.load();
        ImGui::BeginDisabled(!inPlay);
        if (ImGui::Button("Warp", ImVec2(160.0F, 0.0F))) Warp();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!inPlay) {
            ImGui::TextDisabled("Load a save first (the warp works in gameplay).");
        } else if (mRequest->Entrance.load() >= 0) {
            ImGui::TextUnformatted("Warping...");
        } else {
            ImGui::TextDisabled("%s, entrance %d", area.Name, mEntrance);
        }
        ImGui::TextWrapped("Some areas need the right time, day or story progress and may look empty or play "
                           "a cutscene; a few places (the Clock Tower rooftop on the final night) do not let you "
                           "leave this way. Quick save (F5) first if you want to come back.");
    }

  private:
    void Warp() {
        if (!mRequest->InPlay.load()) return;
        const auto& area = kWarpAreas[static_cast<size_t>(std::clamp<int>(mSelected, 0, std::size(kWarpAreas) - 1))];
        const int entrance = std::clamp(mEntrance, 1, static_cast<int>(area.Entrances)) - 1;
        static constexpr int kFormValues[] = {-1, 4, 3, 1, 2, 0};
        mRequest->Form = kFormValues[std::clamp(mForm, 0, 5)];
        mRequest->Entrance = (area.Scene << 9) | (entrance << 4);
    }

    std::shared_ptr<Mm3dWarpRequest> mRequest;
    char mFilter[64] = {};
    int mSelected = 0;
    int mEntrance = 1;
    int mForm = 0;
};

class ButtonLabelPanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
  public:
    explicit ButtonLabelPanel(std::shared_ptr<Mm3dButtonLabelEditor> editor)
        : mEditor(std::move(editor)), mMouseEditing(std::getenv("TRIAEVUM_F1_LABEL_EDIT") != nullptr),
          mSavedRevision(mEditor->Revision()) {}
    const char* Label() const noexcept override { return "Buttons"; }
    void Draw() override {
        ImGui::TextWrapped("The letters on the item buttons (top right of the screen, and the row in the "
                           "Items and Masks menus).");
        ImGui::Checkbox("Edit with the mouse", &mMouseEditing);
        if (mMouseEditing) {
            ImGui::TextWrapped("Drag a letter to move it, turn the mouse wheel over it to resize it. The buttons "
                               "show in gameplay with this menu open.");
        }
        static constexpr const char* kSlots[] = {"Top button (3DS X)", "Left button (3DS Y)", "Item slot I",
                                                 "Item slot II"};
        for (int slot = 0; slot < 4; ++slot) {
            auto layout = mEditor->Label(slot);
            ImGui::PushID(slot);
            ImGui::SeparatorText(kSlots[slot]);
            float offset[2] = {layout.OffsetX * 100.0F, layout.OffsetY * 100.0F};
            float size = layout.Scale * 100.0F;
            ImGui::SetNextItemWidth(220.0F);
            if (ImGui::SliderFloat2("Position", offset, -60.0F, 80.0F, "%.0f")) {
                layout.OffsetX = offset[0] / 100.0F;
                layout.OffsetY = offset[1] / 100.0F;
                mEditor->SetLabel(slot, layout);
            }
            ImGui::SetNextItemWidth(220.0F);
            if (ImGui::SliderFloat("Size", &size, 30.0F, 300.0F, "%.0f%%")) {
                layout.Scale = size / 100.0F;
                mEditor->SetLabel(slot, layout);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) mEditor->SetLabel(slot, {});
            ImGui::PopID();
        }
        if (ImGui::Button("Reset all")) {
            for (int slot = 0; slot < 4; ++slot) mEditor->SetLabel(slot, {});
        }
        if (mMouseEditing) DrawMouseHandles();
        // Saved when an edit ends (not on every step of a drag).
        const uint32_t revision = mEditor->Revision();
        if (revision != mSavedRevision && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            mEditor->Save()) {
            mSavedRevision = revision;
        }
        if (!mEditor->Error().empty()) ImGui::TextWrapped("Could not save: %s", mEditor->Error().c_str());
    }

  private:
    // A small borderless window over each visible letter: drag moves it, the
    // wheel resizes it.
    void DrawMouseHandles() {
        const ImGuiIO& io = ImGui::GetIO();
        for (int slot = 0; slot < 4; ++slot) {
            std::array<float, 4> rect{};
            std::string label;
            if (!mEditor->Sprite(slot, rect, label) || label.empty()) continue;
            auto layout = mEditor->Label(slot);
            const auto box = Mm3dHudButtonLabelBox(label, layout);
            const float spriteX = rect[0] * io.DisplaySize.x, spriteY = rect[1] * io.DisplaySize.y;
            const float spriteW = rect[2] * io.DisplaySize.x, spriteH = rect[3] * io.DisplaySize.y;
            const ImVec2 min(spriteX + box[0] * spriteW, spriteY + box[1] * spriteH);
            const ImVec2 max(spriteX + box[2] * spriteW, spriteY + box[3] * spriteH);
            const ImVec2 size(std::max(8.0F, max.x - min.x), std::max(8.0F, max.y - min.y));
            char name[32];
            std::snprintf(name, sizeof(name), "##mm3d_label_%d", slot);
            ImGui::SetNextWindowPos(min);
            ImGui::SetNextWindowSize(size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0F, 1.0F));
            if (ImGui::Begin(name, nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::SetCursorScreenPos(min);
                ImGui::InvisibleButton("handle", size);
                const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
                if (active && (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F) && spriteW > 0.0F &&
                    spriteH > 0.0F) {
                    layout.OffsetX = std::clamp(layout.OffsetX + io.MouseDelta.x / spriteW, -0.6F, 0.8F);
                    layout.OffsetY = std::clamp(layout.OffsetY + io.MouseDelta.y / spriteH, -0.6F, 0.8F);
                    mEditor->SetLabel(slot, layout);
                }
                if (hovered && io.MouseWheel != 0.0F) {
                    layout.Scale = std::clamp(layout.Scale * std::pow(1.1F, io.MouseWheel), 0.3F, 3.0F);
                    mEditor->SetLabel(slot, layout);
                }
                auto* draw = ImGui::GetForegroundDrawList();
                const ImU32 color = active ? IM_COL32(255, 220, 60, 255)
                                    : hovered ? IM_COL32(255, 255, 255, 230)
                                              : IM_COL32(255, 255, 255, 120);
                draw->AddRect(min, ImVec2(min.x + size.x, min.y + size.y), color, 3.0F, 0, active ? 2.5F : 1.5F);
                if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }
    }

    std::shared_ptr<Mm3dButtonLabelEditor> mEditor;
    bool mMouseEditing = false;
    uint32_t mSavedRevision = 0;
};

}  // namespace

Mm3dButtonLabelEditor::Mm3dButtonLabelEditor(std::filesystem::path path) : mPath(std::move(path)) {
    try {
        if (std::filesystem::exists(mPath)) {
            std::ifstream file(mPath);
            const auto doc = nlohmann::json::parse(file);
            if (doc.value("format", "") != "triaevum_mm3d_button_labels_v1")
                throw std::runtime_error("unsupported button label file");
            const auto& labels = doc.at("labels");
            for (size_t slot = 0; slot < mLabels.size() && slot < labels.size(); ++slot) {
                mLabels[slot].OffsetX = std::clamp(labels[slot].value("x", 0.0F), -0.6F, 0.8F);
                mLabels[slot].OffsetY = std::clamp(labels[slot].value("y", 0.0F), -0.6F, 0.8F);
                mLabels[slot].Scale = std::clamp(labels[slot].value("scale", 1.0F), 0.3F, 3.0F);
            }
        }
    } catch (const std::exception& exception) {
        mError = exception.what();
        mLabels = {};
    }
}

Mm3dButtonLabelLayout Mm3dButtonLabelEditor::Label(int slot) const {
    std::scoped_lock lock(mMutex);
    return mLabels[static_cast<size_t>(std::clamp(slot, 0, 3))];
}

void Mm3dButtonLabelEditor::SetLabel(int slot, const Mm3dButtonLabelLayout& layout) {
    std::scoped_lock lock(mMutex);
    auto& label = mLabels[static_cast<size_t>(std::clamp(slot, 0, 3))];
    if (label == layout) return;
    label = layout;
    ++mRevision;
}

bool Mm3dButtonLabelEditor::Save() {
    nlohmann::json labels = nlohmann::json::array();
    {
        std::scoped_lock lock(mMutex);
        for (const auto& label : mLabels)
            labels.push_back({{"x", label.OffsetX}, {"y", label.OffsetY}, {"scale", label.Scale}});
    }
    std::error_code error;
    if (!mPath.parent_path().empty()) std::filesystem::create_directories(mPath.parent_path(), error);
    const auto saved = Ship::WriteConfigFileAtomically(
        mPath, nlohmann::json{{"format", "triaevum_mm3d_button_labels_v1"}, {"labels", labels}}.dump(2));
    mError = saved.Error;
    return saved.Success;
}

void Mm3dButtonLabelEditor::SetSprite(int slot, const std::array<float, 4>& rect, const std::string& label) {
    std::scoped_lock lock(mMutex);
    const auto index = static_cast<size_t>(std::clamp(slot, 0, 3));
    mRects[index] = rect;
    mSpriteLabels[index] = label;
    mVisible[index] = true;
}

bool Mm3dButtonLabelEditor::Sprite(int slot, std::array<float, 4>& rect, std::string& label) const {
    std::scoped_lock lock(mMutex);
    const auto index = static_cast<size_t>(std::clamp(slot, 0, 3));
    rect = mRects[index];
    label = mSpriteLabels[index];
    return mVisible[index];
}

void Mm3dButtonLabelEditor::ClearSprites() {
    std::scoped_lock lock(mMutex);
    mVisible = {};
}

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dWarpPanel(std::shared_ptr<Mm3dWarpRequest> request) {
    return std::make_shared<WarpPanel>(std::move(request));
}

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dButtonLabelPanel(
    std::shared_ptr<Mm3dButtonLabelEditor> editor) {
    return std::make_shared<ButtonLabelPanel>(std::move(editor));
}

}  // namespace Oot3dNativeGame
