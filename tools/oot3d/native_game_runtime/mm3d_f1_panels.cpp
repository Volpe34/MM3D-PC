#include "mm3d_f1_panels.h"

#include "ship/config/ConfigPersistence.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <utility>

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

struct NamedValue {
    const char* Name;
    int Value;
};

// Item ids as N64 Majora's Mask (the 3DS keeps them).
constexpr NamedValue kGiveItems[] = {
    {"Ocarina of Time", 0x00}, {"Hero's Bow", 0x01}, {"Fire Arrow", 0x02}, {"Ice Arrow", 0x03},
    {"Light Arrow", 0x04}, {"Bombs", 0x06}, {"Bombchus", 0x07}, {"Deku Sticks", 0x08}, {"Deku Nuts", 0x09},
    {"Magic Beans", 0x0A}, {"Powder Keg", 0x0C}, {"Pictograph Box", 0x0D}, {"Lens of Truth", 0x0E},
    {"Hookshot", 0x0F}, {"Great Fairy's Sword", 0x10},
    {"Bottle: empty", 0x12}, {"Bottle: Red Potion", 0x13}, {"Bottle: Green Potion", 0x14},
    {"Bottle: Blue Potion", 0x15}, {"Bottle: Fairy", 0x16}, {"Bottle: Deku Princess", 0x17},
    {"Bottle: Milk", 0x18}, {"Bottle: Half Milk", 0x19}, {"Bottle: Fish", 0x1A}, {"Bottle: Bug", 0x1B},
    {"Bottle: Poe", 0x1D}, {"Bottle: Big Poe", 0x1E}, {"Bottle: Spring Water", 0x1F},
    {"Bottle: Hot Spring Water", 0x20}, {"Bottle: Zora Egg", 0x21}, {"Bottle: Gold Dust", 0x22},
    {"Bottle: Magic Mushroom", 0x23}, {"Bottle: Seahorse", 0x24}, {"Bottle: Chateau Romani", 0x25},
    {"Bottle: Hylian Loach", 0x26},
    {"Moon's Tear", 0x28}, {"Land Title Deed", 0x29}, {"Swamp Title Deed", 0x2A}, {"Mountain Title Deed", 0x2B},
    {"Ocean Title Deed", 0x2C}, {"Room Key", 0x2D}, {"Special Delivery to Mama", 0x2E},
    {"Letter to Kafei", 0x2F}, {"Pendant of Memories", 0x30},
    {"Deku Mask", 0x32}, {"Goron Mask", 0x33}, {"Zora Mask", 0x34}, {"Fierce Deity's Mask", 0x35},
    {"Mask of Truth", 0x36}, {"Kafei's Mask", 0x37}, {"All-Night Mask", 0x38}, {"Bunny Hood", 0x39},
    {"Keaton Mask", 0x3A}, {"Garo's Mask", 0x3B}, {"Romani's Mask", 0x3C}, {"Circus Leader's Mask", 0x3D},
    {"Postman's Hat", 0x3E}, {"Couple's Mask", 0x3F}, {"Great Fairy's Mask", 0x40}, {"Gibdo Mask", 0x41},
    {"Don Gero's Mask", 0x42}, {"Kamaro's Mask", 0x43}, {"Captain's Hat", 0x44}, {"Stone Mask", 0x45},
    {"Bremen Mask", 0x46}, {"Blast Mask", 0x47}, {"Mask of Scents", 0x48}, {"Giant's Mask", 0x49},
};

struct SpawnEntry {
    const char* Group;
    const char* Name;
    int Id;  // MM3D actor id (N64's with its empty slots removed)
    int Params;
};

// Each one spawned in South Clock Town, stayed and behaved (2026-10-22). Left
// out: the bosses and Gomess (their code places them and Link at their
// arena's coordinates: F1 > Cheats > Fight a boss takes you there instead),
// Wizzrobe (the game hangs), Garo and Epona (crash), Wart and Igos (need
// their room, invisible), the Gerudo Pirate (a stuck guard message) and the
// ones that vanish at once.
constexpr SpawnEntry kSpawnList[] = {
    {"Enemy", "Chuchu", 0xE6, 0}, {"Enemy", "Keese", 0x0C, 0}, {"Enemy", "Deku Baba", 0x29, 0},
    {"Enemy", "Mad Scrub", 0x30, 0}, {"Enemy", "Skulltula", 0x21, 0}, {"Enemy", "Wolfos", 0x9F, 0},
    {"Enemy", "Tektite", 0x12, 0}, {"Enemy", "Leever", 0x1A5, 0}, {"Enemy", "Like Like", 0x51, 0},
    {"Enemy", "ReDead", 0x3C, 0}, {"Enemy", "Stalchild", 0xA0, 0}, {"Enemy", "Bubble", 0x33, 0},
    {"Enemy", "Red Bubble", 0x31, 0}, {"Enemy", "Armos", 0x28, 0}, {"Enemy", "Beamos", 0x38, 0},
    {"Enemy", "Dodongo", 0x0B, 0}, {"Enemy", "Peahat", 0x13, 0}, {"Enemy", "Dinolfos", 0x18, 0},
    {"Enemy", "Poe", 0x183, 0}, {"Enemy", "Guay", 0xA3, 0}, {"Enemy", "Eeno", 0x176, 0},
    {"Enemy", "Freezard", 0x64, 0}, {"Enemy", "Hiploop", 0x179, 0}, {"Enemy", "Takkuri", 0x220, 0},
    {"Enemy", "Nejiron", 0xF0, 0}, {"Enemy", "Snapper", 0x14E, 0}, {"Enemy", "Wallmaster", 0x0A, 0},
    {"Enemy", "Floormaster", 0x3B, 0}, {"Enemy", "Death Armos", 0x26, 0}, {"Enemy", "Bad Bat", 0xF6, 0},
    {"Enemy", "Boe", 0xFF, 0}, {"Enemy", "Dragonfly", 0xB1, 0}, {"Enemy", "Shell Blade", 0x4A, 0},
    {"Enemy", "Skullfish", 0xE7, 0}, {"Enemy", "Bio Deku Baba", 0x4B, 0},
    {"Mini-boss", "Iron Knuckle", 0x5D, 0}, {"Mini-boss", "Big Poe", 0x197, 0},
    {"NPC", "Cucco", 0x11, 0}, {"NPC", "Cow", 0xA4, 0}, {"NPC", "Dog", 0x98, 0}, {"NPC", "Fairy", 0x10, 0},
    {"NPC", "Butterfly", 0x14, 0}, {"NPC", "Fish", 0x16, 0}, {"NPC", "Tingle", 0x10E, 0},
    {"NPC", "Happy Mask Salesman", 0x149, 0}, {"NPC", "Bombers kid", 0xC1, 0}, {"NPC", "Zora", 0x1B7, 0},
    {"NPC", "Deku Scrub", 0x5F, 0}, {"NPC", "Great Fairy", 0xEE, 0},
    {"Object", "Bomb", 0x09, 0}, {"Object", "Treasure chest", 0x06, 0},
};

class CheatsPanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
  public:
    explicit CheatsPanel(std::shared_ptr<Mm3dCheats> cheats) : mCheats(std::move(cheats)) {}
    const char* Label() const noexcept override { return "Cheats"; }
    void Draw() override {
        const bool inPlay = mCheats->InPlay.load();
        ImGui::TextWrapped("Cheats change your save while they are on: quick save (F5) first if you want to "
                           "go back. They are off again at the next launch.");
        if (!inPlay) ImGui::TextDisabled("Load a save first (cheats work in gameplay).");
        if (ImGui::CollapsingHeader("Link", ImGuiTreeNodeFlags_DefaultOpen)) DrawLink();
        if (ImGui::CollapsingHeader("Items", ImGuiTreeNodeFlags_DefaultOpen)) DrawItems(inPlay);
        if (ImGui::CollapsingHeader("Complete quest")) DrawQuest(inPlay);
        if (ImGui::CollapsingHeader("Time")) DrawTime(inPlay);
        if (ImGui::CollapsingHeader("Fight a boss")) DrawBosses(inPlay);
        if (ImGui::CollapsingHeader("Spawn enemies and NPCs")) DrawSpawn(inPlay);
    }

  private:
    void DrawLink() {
        Toggle("Fly", mCheats->Fly,
               "Link floats: the stick flies him where the camera looks, A (hold) goes up, B (hold) goes "
               "down. A and B do nothing else while flying. Walls still stop him.");
        if (mCheats->Fly.load()) {
            float speed = mCheats->FlySpeed.load();
            ImGui::SetNextItemWidth(260.0F);
            if (ImGui::SliderFloat("Fly speed", &speed, 4.0F, 60.0F, "%.0f", ImGuiSliderFlags_AlwaysClamp))
                mCheats->FlySpeed = speed;
        }
        Toggle("Moon jump", mCheats->MoonJump, "Hold L and A: Link rises for as long as you hold them.");
        Toggle("Infinite health", mCheats->InfiniteHealth, "Hearts refill at once.");
        ImGui::SetNextItemWidth(260.0F);
        ImGui::SliderInt("##hearts", &mHearts, 3, 20, "%d hearts", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        if (ImGui::Button("Set heart containers")) mCheats->SetMaxHearts = mHearts;
    }

    void DrawItems(bool inPlay) {
        Toggle("Infinite magic", mCheats->InfiniteMagic, "Needs the magic meter (from the Great Fairy).");
        Toggle("Infinite rupees", mCheats->InfiniteRupees, "The wallet stays full.");
        Toggle("Infinite ammo", mCheats->InfiniteAmmo,
               "Arrows, bombs, bombchus, Deku sticks and nuts, magic beans and powder kegs stay full (for the "
               "ones you have).");
        Toggle("Unbreakable Razor Sword", mCheats->UnbreakableRazorSword, "The Razor Sword never wears out.");
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::BeginCombo("##give", kGiveItems[mGive].Name)) {
            for (int i = 0; i < static_cast<int>(std::size(kGiveItems)); ++i)
                if (ImGui::Selectable(kGiveItems[i].Name, i == mGive)) mGive = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!inPlay);
        if (ImGui::Button("Give item")) mCheats->GiveItem = kGiveItems[mGive].Value;
        ImGui::EndDisabled();
        ImGui::TextDisabled("Bottles go into an empty bottle slot; a trade item replaces the one in its slot.");
    }

    void DrawQuest(bool inPlay) {
        ImGui::TextWrapped("Gives what finishing the game would. Owl statues, heart pieces, stray fairies, "
                           "people's quests and the Bombers' Notebook entries stay as they are.");
        static constexpr std::pair<const char*, int> kParts[] = {
            {"All items and 6 bottles", kMm3dQuestItems},
            {"All 24 masks", kMm3dQuestMasks},
            {"The four remains, all songs and the Bombers' Notebook", kMm3dQuestSongs},
            {"Gilded Sword, Mirror Shield, Great Fairy's Sword, biggest quiver, bomb bag and wallet",
             kMm3dQuestGear},
            {"20 hearts, double magic and double defense", kMm3dQuestHearts},
        };
        for (const auto& [label, bit] : kParts) {
            bool on = (mQuestParts & bit) != 0;
            if (ImGui::Checkbox(label, &on)) mQuestParts = on ? (mQuestParts | bit) : (mQuestParts & ~bit);
        }
        ImGui::BeginDisabled(!inPlay || mQuestParts == 0);
        if (ImGui::Button("Complete quest", ImVec2(200.0F, 0.0F))) mCheats->CompleteQuest = mQuestParts;
        ImGui::EndDisabled();
    }

    void DrawTime(bool inPlay) {
        Toggle("Freeze time", mCheats->FreezeTime,
               "The clock stops (the Song of Time and new days still work). Inverted Song of Time is the "
               "game's own way to slow it.");
        static constexpr std::pair<const char*, int> kTimes[] = {
            {"6 AM", 0x4000}, {"Noon", 0x8000}, {"6 PM", 0xC000}, {"Midnight", 0x0000}};
        ImGui::BeginDisabled(!inPlay);
        for (size_t i = 0; i < std::size(kTimes); ++i) {
            if (i != 0U) ImGui::SameLine();
            if (ImGui::Button(kTimes[i].first, ImVec2(90.0F, 0.0F))) mCheats->SetTime = kTimes[i].second;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Sets the time of the current day.");
    }

    void DrawBosses(bool inPlay) {
        ImGui::TextWrapped("Takes you to the boss's arena for the real fight, even if you have beaten it. "
                           "When you leave the arena your save is put back as it was: the temple stays as it "
                           "was, and remains or heart containers won from this fight are taken back.");
        Toggle("Keep what I win", mCheats->KeepBossWin,
               "Keep the temple cleared, the remains and the heart container from this fight.");
        int count = 0;
        const Mm3dBoss* bosses = Mm3dBosses(&count);
        const bool fighting = mCheats->BossFightActive.load();
        ImGui::BeginDisabled(!inPlay || fighting);
        for (int i = 0; i < count; ++i) {
            if (i % 3 != 0) ImGui::SameLine();
            if (ImGui::Button(bosses[i].Name, ImVec2(150.0F, 0.0F))) mCheats->FightBoss = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", bosses[i].Where);
        }
        ImGui::EndDisabled();
        if (fighting) ImGui::TextDisabled("Boss fight under way: leave the arena to end it.");
        ImGui::TextDisabled("Majora ends the game when beaten. Igos returns in each 3-day cycle.");
    }

    void DrawSpawn(bool inPlay) {
        ImGui::TextWrapped("Spawns in front of Link, facing him. Quick save (F5) first: spawned things can "
                           "break scripted scenes. An area can hold only a few new kinds at once: leave it "
                           "or defeat them to make room.");
        ImGui::SetNextItemWidth(260.0F);
        ImGui::InputTextWithHint("##spawnfilter", "Search", mSpawnFilter, sizeof(mSpawnFilter));
        const std::string filter = Lower(mSpawnFilter);
        if (ImGui::BeginListBox("##spawnlist", ImVec2(-1.0F, 200.0F))) {
            for (int i = 0; i < static_cast<int>(std::size(kSpawnList)); ++i) {
                const auto& entry = kSpawnList[i];
                const std::string label = std::string(entry.Name) + "  (" + entry.Group + ")";
                if (!filter.empty() && Lower(label).find(filter) == std::string::npos) continue;
                if (ImGui::Selectable(label.c_str(), i == mSpawnSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    mSpawnSelected = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && inPlay) Spawn(entry.Id, entry.Params);
                }
            }
            ImGui::EndListBox();
        }
        ImGui::BeginDisabled(!inPlay);
        const auto& selected = kSpawnList[std::clamp<int>(mSpawnSelected, 0, std::size(kSpawnList) - 1)];
        if (ImGui::Button("Spawn", ImVec2(160.0F, 0.0F))) Spawn(selected.Id, selected.Params);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(mCheats->SpawnStatus().c_str());
        if (ImGui::TreeNode("Any actor (advanced)")) {
            ImGui::TextWrapped("Actor id and params as the 3DS numbers them (N64 Majora's Mask ids without its "
                               "empty slots). Anything not in the list above may crash the game.");
            ImGui::SetNextItemWidth(120.0F);
            ImGui::InputInt("Id", &mCustomId, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SetNextItemWidth(120.0F);
            ImGui::InputInt("Params", &mCustomParams, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
            mCustomId = std::clamp(mCustomId, 0, 0x22B);
            mCustomParams = std::clamp(mCustomParams, 0, 0xFFFF);
            ImGui::BeginDisabled(!inPlay);
            if (ImGui::Button("Spawn actor")) Spawn(mCustomId, mCustomParams);
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    void Spawn(int id, int params) {
        mCheats->SetSpawnStatus("...");
        mCheats->SpawnParams = params;
        mCheats->SpawnId = id;
    }

    static void Toggle(const char* label, std::atomic<bool>& value, const char* help) {
        bool on = value.load();
        if (ImGui::Checkbox(label, &on)) value = on;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
    }

    std::shared_ptr<Mm3dCheats> mCheats;
    int mHearts = 20;
    int mGive = 0;
    int mQuestParts = kMm3dQuestItems | kMm3dQuestMasks | kMm3dQuestSongs | kMm3dQuestGear | kMm3dQuestHearts;
    char mSpawnFilter[64] = {};
    int mSpawnSelected = 0;
    int mCustomId = 0;
    int mCustomParams = 0;
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

std::filesystem::path SaveStateSlots::Path(int slot) const {
    if (slot <= 1) return mFirstSlot;
    return mFirstSlot.parent_path() /
           (mFirstSlot.stem().string() + "_" + std::to_string(slot) + mFirstSlot.extension().string());
}

void SaveStateSlots::Select(int slot) {
    mSelected = std::clamp(slot, 1, kSlots);
    ++mRevision;
}

void SaveStateSlots::Report(std::string message) {
    std::scoped_lock lock(mMutex);
    mMessage = std::move(message);
    mMessageTime = std::chrono::steady_clock::now();
    ++mRevision;
}

std::string SaveStateSlots::Message(double seconds) const {
    std::scoped_lock lock(mMutex);
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - mMessageTime).count() > seconds) return {};
    return mMessage;
}

std::string SaveStateSlots::LastMessage() const {
    std::scoped_lock lock(mMutex);
    return mMessage;
}

static std::string SaveStateTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error) return {};
    // file_clock -> system_clock without C++20 clock_cast (not in every standard library).
    const auto now = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(
                         written - std::filesystem::file_time_type::clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    char text[64] = {};
    if (const std::tm* local = std::localtime(&time)) std::strftime(text, sizeof(text), "%d %b %Y  %H:%M:%S", local);
    return text;
}

class SaveStatePanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
  public:
    explicit SaveStatePanel(std::shared_ptr<SaveStateSlots> slots) : mSlots(std::move(slots)) {}
    const char* Label() const noexcept override { return "Save states"; }
    void Draw() override {
        ImGui::TextWrapped("Save the whole game at any moment and come back to that moment later. F5 saves to "
                           "the selected slot, F8 loads it, F6 / F7 select the previous / next slot.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Each state takes about 450 MB of disk. States belong to this version of the port: "
                           "after an update, older states may not load. Your in-game saves always carry over.");
        ImGui::PopStyleColor();
        // The slots' files are looked at again after a save/load and every 2 seconds.
        const auto now = std::chrono::steady_clock::now();
        if (mSeenRevision != mSlots->Revision() || now - mRefreshed > std::chrono::seconds(2)) {
            for (int slot = 1; slot <= SaveStateSlots::kSlots; ++slot)
                mTimes[static_cast<size_t>(slot - 1)] = SaveStateTime(mSlots->Path(slot));
            mSeenRevision = mSlots->Revision();
            mRefreshed = now;
        }
        if (ImGui::BeginTable("##states", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (int slot = 1; slot <= SaveStateSlots::kSlots; ++slot) {
                const std::string& saved = mTimes[static_cast<size_t>(slot - 1)];
                ImGui::PushID(slot);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::RadioButton(("Slot " + std::to_string(slot)).c_str(), mSlots->Selected() == slot))
                    mSlots->Select(slot);
                ImGui::TableNextColumn();
                if (saved.empty()) {
                    ImGui::TextDisabled("empty");
                } else {
                    ImGui::TextUnformatted(saved.c_str());
                }
                ImGui::TableNextColumn();
                if (ImGui::Button("Save")) {
                    mSlots->Select(slot);
                    mSlots->RequestSave(slot);
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(saved.empty());
                if (ImGui::Button("Load")) {
                    mSlots->Select(slot);
                    mSlots->RequestLoad(slot);
                }
                ImGui::TableNextColumn();
                // Delete asks once more on the same button.
                if (ImGui::Button(mConfirmDelete == slot ? "Sure?" : "Delete")) {
                    if (mConfirmDelete == slot) {
                        std::error_code error;
                        std::filesystem::remove(mSlots->Path(slot), error);
                        mSlots->Report(error ? "Slot " + std::to_string(slot) + " could not be deleted: " +
                                                   error.message()
                                             : "Slot " + std::to_string(slot) + " deleted");
                        mConfirmDelete = 0;
                    } else {
                        mConfirmDelete = slot;
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (mSlots->Pending()) {
            ImGui::TextUnformatted("Working...");
        } else {
            ImGui::TextUnformatted(mSlots->LastMessage().c_str());
        }
    }

  private:
    std::shared_ptr<SaveStateSlots> mSlots;
    std::array<std::string, SaveStateSlots::kSlots> mTimes{};
    uint32_t mSeenRevision = 0;
    std::chrono::steady_clock::time_point mRefreshed{};
    int mConfirmDelete = 0;
};

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateSaveStatePanel(std::shared_ptr<SaveStateSlots> slots) {
    return std::make_shared<SaveStatePanel>(std::move(slots));
}

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dWarpPanel(std::shared_ptr<Mm3dWarpRequest> request) {
    return std::make_shared<WarpPanel>(std::move(request));
}

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dCheatsPanel(std::shared_ptr<Mm3dCheats> cheats) {
    return std::make_shared<CheatsPanel>(std::move(cheats));
}

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateMm3dButtonLabelPanel(
    std::shared_ptr<Mm3dButtonLabelEditor> editor) {
    return std::make_shared<ButtonLabelPanel>(std::move(editor));
}

}  // namespace Oot3dNativeGame
