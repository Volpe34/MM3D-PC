#pragma once
// MM3D single screen: the item-slot buttons drawn like TriAevum's OoT3D HUD:
// the game's own button bubble (the B button's, read from the player's RomFS
// at run time, so nothing of the game is shipped) with the controller
// button's name in yellow at its top left.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

enum class Mm3dButtonStyle : uint8_t { Xbox, DualShock, Nintendo, NintendoSnes, Original };

// From F1 > Textures > Button style: the texture pack directory, Original
// (3DS) when custom textures are off.
Mm3dButtonStyle Mm3dButtonStyleFromSettings(bool customTextures, std::string_view directory);

// Button names per style: index 0 top (3DS X), 1 left (3DS Y), 2 item slot I,
// 3 item slot II. A name is letters/digits; "^" is a triangle, "#" a square.
std::string Mm3dHudButtonLabel(Mm3dButtonStyle style, int slot);
// The label's color (RGB): the game's yellow, the SNES pad's colors for
// that style.
std::array<uint8_t, 3> Mm3dHudButtonLabelColor(Mm3dButtonStyle style, int slot);

// The B button's bubble from the HUD atlas (hud_patrs00_action_text in
// layout/<language>/Joker.Main.Play.gar), its "B" painted out from the
// ring around it. RGBA8, Size x Size, screen orientation.
struct Mm3dButtonRing {
    std::vector<uint8_t> Rgba;
    uint32_t Size = 0;
    float CenterX = 0.0F, CenterY = 0.0F;  // pixels
    float Radius = 0.0F;                   // to the ring's outer edge
};
// The HUD atlas itself: its native ETC1A4 bytes (what a texture pack's hash
// is of) and decoded RGBA8.
struct Mm3dButtonAtlas {
    std::vector<uint8_t> Native, Rgba;
    uint32_t Width = 0, Height = 0;
};
std::optional<Mm3dButtonAtlas> LoadMm3dButtonAtlas(const std::filesystem::path& romfs, uint64_t offset,
                                                   uint64_t size, std::string* error = nullptr);
// The bubble cut from the atlas, the game's or an HD pack's (any multiple
// of 512 x 256).
std::optional<Mm3dButtonRing> Mm3dButtonRingFromAtlas(const std::vector<uint8_t>& rgba, uint32_t width,
                                                      uint32_t height);
std::optional<Mm3dButtonRing> LoadMm3dButtonRing(const std::filesystem::path& romfs, uint64_t offset,
                                                 uint64_t size, std::string* error = nullptr);

// Where a slot's letters go (F1 > Buttons): moved by a fraction of the
// sprite, scaled around their top left corner.
struct Mm3dButtonLabelLayout {
    float OffsetX = 0.0F, OffsetY = 0.0F;
    float Scale = 1.0F;
    bool operator==(const Mm3dButtonLabelLayout&) const = default;
};

// The label's box in the sprite (fractions: x0, y0, x1, y1), outline included.
std::array<float, 4> Mm3dHudButtonLabelBox(std::string_view label, const Mm3dButtonLabelLayout& layout = {});

// RGBA8, size x size, screen orientation (row 0 at the top): the circle
// centered at (0.56, 0.56) * size with radius 0.42 * size (the game's
// bubble when given, a drawn ring otherwise), the label in the top left
// corner (or where the layout puts it).
std::vector<uint8_t> MakeMm3dHudButtonSprite(uint32_t size, std::string_view label,
                                             const Mm3dButtonRing* ring = nullptr,
                                             std::array<uint8_t, 3> labelColor = {245, 214, 64},
                                             const Mm3dButtonLabelLayout& layout = {});

// The same pixels laid out as a 3DS framebuffer (rotated: memory row = screen
// column from the right, memory column = screen line from the bottom), for
// display images.
std::vector<uint8_t> Mm3dSpriteToFramebufferOrder(const std::vector<uint8_t>& rgba, uint32_t size);

}  // namespace Oot3dNativeGame
