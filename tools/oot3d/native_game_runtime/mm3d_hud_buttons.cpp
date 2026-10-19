#include "mm3d_hud_buttons.h"

#include "oot3d/renderer/pica_texture_decode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace Oot3dNativeGame {
namespace {

struct Segment {
    float X0, Y0, X1, Y1;
};

struct Glyph {
    float Width;
    std::vector<Segment> Strokes;
};

// Stroke font in a box 1 high (y down); widths vary.
Glyph GlyphFor(char c) {
    switch (c) {
        case 'A': return {0.62F, {{0, 1, 0.31F, 0}, {0.31F, 0, 0.62F, 1}, {0.12F, 0.64F, 0.5F, 0.64F}}};
        case 'B':
            return {0.56F, {{0, 0, 0, 1}, {0, 0, 0.36F, 0}, {0.36F, 0, 0.5F, 0.12F}, {0.5F, 0.12F, 0.5F, 0.34F},
                            {0.5F, 0.34F, 0.36F, 0.47F}, {0, 0.47F, 0.4F, 0.47F}, {0.4F, 0.47F, 0.56F, 0.6F},
                            {0.56F, 0.6F, 0.56F, 0.86F}, {0.56F, 0.86F, 0.4F, 1}, {0.4F, 1, 0, 1}}};
        case 'X': return {0.6F, {{0, 0, 0.6F, 1}, {0.6F, 0, 0, 1}}};
        case 'Y': return {0.62F, {{0, 0, 0.31F, 0.5F}, {0.62F, 0, 0.31F, 0.5F}, {0.31F, 0.5F, 0.31F, 1}}};
        case 'L': return {0.5F, {{0, 0, 0, 1}, {0, 1, 0.5F, 1}}};
        case 'Z': return {0.58F, {{0, 0, 0.58F, 0}, {0.58F, 0, 0, 1}, {0, 1, 0.58F, 1}}};
        case 'R':
            return {0.58F, {{0, 0, 0, 1}, {0, 0, 0.38F, 0}, {0.38F, 0, 0.54F, 0.13F}, {0.54F, 0.13F, 0.54F, 0.34F},
                            {0.54F, 0.34F, 0.38F, 0.47F}, {0.38F, 0.47F, 0, 0.47F}, {0.24F, 0.47F, 0.58F, 1}}};
        case '1': return {0.5F, {{0.08F, 0.18F, 0.3F, 0}, {0.3F, 0, 0.3F, 1}, {0.05F, 1, 0.5F, 1}}};
        case '2':
            return {0.56F, {{0, 0.2F, 0.12F, 0.05F}, {0.12F, 0.05F, 0.3F, 0}, {0.3F, 0, 0.46F, 0.06F},
                            {0.46F, 0.06F, 0.54F, 0.22F}, {0.54F, 0.22F, 0.5F, 0.38F}, {0.5F, 0.38F, 0, 1},
                            {0, 1, 0.56F, 1}}};
        case '^': return {0.92F, {{0.46F, 0.04F, 0.92F, 0.96F}, {0.92F, 0.96F, 0, 0.96F}, {0, 0.96F, 0.46F, 0.04F}}};
        case '#': return {0.82F, {{0, 0.09F, 0.82F, 0.09F}, {0.82F, 0.09F, 0.82F, 0.91F},
                                  {0.82F, 0.91F, 0, 0.91F}, {0, 0.91F, 0, 0.09F}}};
        default: return {0.4F, {}};
    }
}

float DistanceToSegment(float px, float py, const Segment& s) {
    const float dx = s.X1 - s.X0, dy = s.Y1 - s.Y0;
    const float length2 = dx * dx + dy * dy;
    float t = length2 > 0.0F ? ((px - s.X0) * dx + (py - s.Y0) * dy) / length2 : 0.0F;
    t = std::clamp(t, 0.0F, 1.0F);
    const float x = s.X0 + t * dx - px, y = s.Y0 + t * dy - py;
    return std::sqrt(x * x + y * y);
}

// Source-over of straight-alpha color c (alpha a in 0..1) onto pixel p.
void Blend(uint8_t* p, float r, float g, float b, float a) {
    if (a <= 0.0F) return;
    const float da = p[3] / 255.0F;
    const float oa = a + da * (1.0F - a);
    if (oa <= 0.0F) return;
    const auto mix = [&](uint8_t d, float s) {
        return static_cast<uint8_t>(std::lround(std::clamp((s * a + d * da * (1.0F - a)) / oa, 0.0F, 255.0F)));
    };
    p[0] = mix(p[0], r);
    p[1] = mix(p[1], g);
    p[2] = mix(p[2], b);
    p[3] = static_cast<uint8_t>(std::lround(oa * 255.0F));
}


using Bytes = std::vector<uint8_t>;

uint32_t U32(const Bytes& b, size_t p) {
    if (p > b.size() || b.size() - p < 4) throw std::runtime_error("truncated");
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
}
uint16_t U16(const Bytes& b, size_t p) {
    if (p > b.size() || b.size() - p < 2) throw std::runtime_error("truncated");
    return static_cast<uint16_t>(b[p] | b[p + 1] << 8);
}
std::string RomFsName(const Bytes& b, size_t p, size_t n) {
    if (n % 2 || p > b.size() || n > b.size() - p) throw std::runtime_error("bad RomFS name");
    std::string result;
    for (size_t i = 0; i < n; i += 2) result += b[p + i + 1] ? '?' : static_cast<char>(b[p + i]);
    return result;
}
std::string CString(const Bytes& b, size_t p) {
    std::string result;
    while (p < b.size() && b[p] != 0U) result += static_cast<char>(b[p++]);
    return result;
}

// Every file of the RomFS level 3 named `name`, read whole.
std::vector<Bytes> ReadRomFsFiles(const std::filesystem::path& path, uint64_t offset, uint64_t size,
                                  std::string_view name) {
    const auto fileSize = std::filesystem::file_size(path);
    if (offset > fileSize) throw std::runtime_error("RomFS offset outside file");
    if (size == 0U) size = fileSize - offset;
    if (size > fileSize - offset) throw std::runtime_error("RomFS size outside file");
    std::ifstream stream(path, std::ios::binary);
    const auto read = [&](uint64_t at, uint64_t count) {
        if (at > size || count > size - at || count > 64U * 1024U * 1024U)
            throw std::runtime_error("RomFS read outside image");
        Bytes result(static_cast<size_t>(count));
        stream.seekg(static_cast<std::streamoff>(offset + at));
        if (!stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
            throw std::runtime_error("cannot read RomFS");
        return result;
    };
    const uint64_t base = U32(read(0, 4), 0) == 0x43465649U ? 0x1000U : 0U;  // "IVFC"
    const auto header = read(base, 40);
    if (U32(header, 0) != 40U) throw std::runtime_error("invalid RomFS level 3 header");
    const auto files = read(base + U32(header, 28), U32(header, 32));
    const uint64_t data = base + U32(header, 36);
    std::vector<Bytes> found;
    for (size_t p = 0; p + 32 <= files.size();) {
        const auto length = U32(files, p + 28);
        if (RomFsName(files, p + 32, length) == name) {
            const uint64_t payload = U32(files, p + 8) | uint64_t(U32(files, p + 12)) << 32;
            const uint64_t bytes = U32(files, p + 16) | uint64_t(U32(files, p + 20)) << 32;
            found.push_back(read(data + payload, bytes));
        }
        p += (32U + length + 3U) & ~size_t(3);
    }
    return found;
}

// A file of a Grezzo GAR (version 2) archive by its full name.
std::optional<Bytes> GarFile(const Bytes& gar, std::string_view name) {
    if (gar.size() < 0x20 || gar[0] != 'G' || gar[1] != 'A' || gar[2] != 'R' || gar[3] != 2) return std::nullopt;
    const uint16_t count = U16(gar, 10);
    const uint32_t entries = U32(gar, 16), offsets = U32(gar, 20);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t fileSize = U32(gar, entries + 12U * i);
        if (CString(gar, U32(gar, entries + 12U * i + 8U)) != name) continue;
        const uint32_t at = U32(gar, offsets + 4U * i);
        if (at > gar.size() || fileSize > gar.size() - at) return std::nullopt;
        return Bytes(gar.begin() + at, gar.begin() + at + fileSize);
    }
    return std::nullopt;
}

// Bicubic (Catmull-Rom) sample of premultiplied RGBA at a pixel position.
std::array<float, 4> SampleBicubic(const std::vector<float>& premultiplied, uint32_t size, float x, float y) {
    const auto weight = [](float t) {
        t = std::fabs(t);
        return t < 1.0F ? 1.5F * t * t * t - 2.5F * t * t + 1.0F
               : t < 2.0F ? -0.5F * t * t * t + 2.5F * t * t - 4.0F * t + 2.0F
                          : 0.0F;
    };
    x -= 0.5F;
    y -= 0.5F;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    std::array<float, 4> sum{};
    for (int j = -1; j <= 2; ++j) {
        const float wy = weight(y - static_cast<float>(y0 + j));
        const int sy = y0 + j;
        if (sy < 0 || sy >= static_cast<int>(size)) continue;
        for (int i = -1; i <= 2; ++i) {
            const int sx = x0 + i;
            if (sx < 0 || sx >= static_cast<int>(size)) continue;
            const float w = wy * weight(x - static_cast<float>(sx));
            const float* p = &premultiplied[(static_cast<size_t>(sy) * size + static_cast<size_t>(sx)) * 4U];
            for (int c = 0; c < 4; ++c) sum[static_cast<size_t>(c)] += p[c] * w;
        }
    }
    return sum;
}

}  // namespace

std::optional<Mm3dButtonAtlas> LoadMm3dButtonAtlas(const std::filesystem::path& romfs, uint64_t offset,
                                                   uint64_t size, std::string* error) {
    try {
        for (const auto& gar : ReadRomFsFiles(romfs, offset, size, "Joker.Main.Play.gar")) {
            const auto ctxb = GarFile(gar, "hud_patrs00_action_text.ctxb");
            if (!ctxb.has_value() || ctxb->size() < 0x48 || CString(*ctxb, 0).substr(0, 4) != "ctxb") continue;
            const uint32_t chunk = U32(*ctxb, 0x10), texels = U32(*ctxb, 0x14);
            const size_t entry = chunk + 12U;
            const uint32_t length = U32(*ctxb, entry);
            const uint16_t width = U16(*ctxb, entry + 8), height = U16(*ctxb, entry + 10);
            const uint16_t format = U16(*ctxb, entry + 12);
            const uint32_t at = texels + U32(*ctxb, entry + 16);
            if (width != 512U || height != 256U || format != 0x675BU || at > ctxb->size() ||
                length > ctxb->size() - at) {
                continue;
            }
            Mm3dButtonAtlas atlas;
            atlas.Width = width;
            atlas.Height = height;
            atlas.Native.assign(ctxb->data() + at, ctxb->data() + at + length);
            if (!::Oot3d::Renderer::DecodePicaTextureRgba8(0x0DU /* ETC1A4 */, width, height,
                                                           std::span<const uint8_t>(atlas.Native), atlas.Rgba,
                                                           error)) {
                continue;
            }
            return atlas;
        }
        if (error != nullptr) *error = "no HUD atlas with the button bubble in the RomFS";
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
    }
    return std::nullopt;
}

std::optional<Mm3dButtonRing> Mm3dButtonRingFromAtlas(const std::vector<uint8_t>& rgba, uint32_t width,
                                                      uint32_t height) {
    // An HD pack's atlas is the game's 512 x 256 scaled: every place scales.
    const float scale = static_cast<float>(width) / 512.0F;
    if (width == 0U || height == 0U || rgba.size() < static_cast<size_t>(width) * height * 4U ||
        std::abs(static_cast<float>(height) / 256.0F - scale) > 0.01F * scale)
        return std::nullopt;
    // The B button's bubble: 40 x 40 at (46, 0), its letter on top.
    const uint32_t kSize = static_cast<uint32_t>(std::lround(40.0F * scale)),
                   kLeft = static_cast<uint32_t>(std::lround(46.0F * scale));
    if (kSize == 0U || kLeft + kSize > width || kSize > height) return std::nullopt;
    Mm3dButtonRing ring;
    ring.Size = kSize;
    ring.CenterX = 19.0F * scale;
    ring.CenterY = 19.5F * scale;
    ring.Radius = 17.6F * scale;
    ring.Rgba.resize(kSize * kSize * 4U);
    for (uint32_t y = 0; y < kSize; ++y) {
        std::copy_n(&rgba[(static_cast<size_t>(y) * width + kLeft) * 4U], kSize * 4U,
                    &ring.Rgba[static_cast<size_t>(y) * kSize * 4U]);
    }
    const auto pixel = [&](uint32_t x, uint32_t y) { return &ring.Rgba[(y * kSize + x) * 4U]; };
    // (A bubble: the ring solid at its left, the middle see-through.)
    // (An HD pack's ring is a thin line: the most opaque point across it.)
    uint8_t ringAlpha = 0;
    for (uint32_t x = static_cast<uint32_t>(1.0F * scale); x <= static_cast<uint32_t>(7.0F * scale); ++x)
        ringAlpha = std::max(ringAlpha, pixel(x, static_cast<uint32_t>(19.0F * scale))[3]);
    if (ringAlpha < 160U || pixel(static_cast<uint32_t>(19.0F * scale), static_cast<uint32_t>(19.0F * scale))[3] > 200U)
        return std::nullopt;
    // The letter: colored or dark pixels at the top middle, and around them.
    std::vector<uint8_t> letter(kSize * kSize, 0U), mask(kSize * kSize, 0U);
    const uint32_t letterBottom = static_cast<uint32_t>(13.0F * scale), letterTop = static_cast<uint32_t>(2.0F * scale);
    const int grow = std::max(1, static_cast<int>(std::lround(scale)));
    for (uint32_t y = 0; y <= letterBottom; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            if (std::fabs(static_cast<float>(x) + 0.5F - ring.CenterX) > 10.0F * scale) continue;
            const uint8_t* p = pixel(x, y);
            const int chroma = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
            letter[y * kSize + x] = (chroma > 22 && p[3] > 40U) ||
                                    (std::max({p[0], p[1], p[2]}) < 70 && p[3] > 60U) || y <= letterTop;
        }
    }
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            for (int dy = -grow; dy <= grow && !mask[y * kSize + x]; ++dy) {
                for (int dx = -grow; dx <= grow; ++dx) {
                    const int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                    if (nx >= 0 && ny >= 0 && nx < static_cast<int>(kSize) && ny < static_cast<int>(kSize) &&
                        letter[static_cast<size_t>(ny) * kSize + static_cast<size_t>(nx)]) {
                        mask[y * kSize + x] = 1U;
                        break;
                    }
                }
            }
        }
    }
    // Painted from the ring around it: the nearest unmasked points at
    // the same distance from the center, on both sides.
    const auto masked = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < static_cast<int>(kSize) && ny < static_cast<int>(kSize) &&
                    mask[static_cast<size_t>(ny) * kSize + static_cast<size_t>(nx)])
                    return true;
            }
        return false;
    };
    const auto source = ring.Rgba;
    const auto bilinear = [&](float fx, float fy) {
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        std::array<float, 4> sum{};
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int x = std::clamp(x0 + i, 0, static_cast<int>(kSize) - 1);
                const int y = std::clamp(y0 + j, 0, static_cast<int>(kSize) - 1);
                const float w = (i ? tx : 1.0F - tx) * (j ? ty : 1.0F - ty);
                const uint8_t* p = &source[(static_cast<size_t>(y) * kSize + static_cast<size_t>(x)) * 4U];
                const float a = p[3] / 255.0F;
                for (int c = 0; c < 3; ++c) sum[static_cast<size_t>(c)] += p[c] * a * w;
                sum[3] += a * w;
            }
        return sum;
    };
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            if (!mask[y * kSize + x]) continue;
            const float dx = static_cast<float>(x) + 0.5F - ring.CenterX;
            const float dy = static_cast<float>(y) + 0.5F - ring.CenterY;
            std::array<float, 4> total{};
            float weights = 0.0F;
            for (const float sign : {1.0F, -1.0F}) {
                for (int k = 1; k < 60; ++k) {
                    const float angle = sign * 3.0F * static_cast<float>(k) * 3.14159265F / 180.0F;
                    const float c = std::cos(angle), s = std::sin(angle);
                    const float sx = ring.CenterX + dx * c - dy * s - 0.5F;
                    const float sy = ring.CenterY + dx * s + dy * c - 0.5F;
                    if (masked(static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)))) continue;
                    const auto value = bilinear(sx, sy);
                    const float w = 1.0F / static_cast<float>(k);
                    for (size_t i = 0; i < 4; ++i) total[i] += value[i] * w;
                    weights += w;
                    break;
                }
            }
            uint8_t* p = pixel(x, y);
            const float a = weights > 0.0F ? total[3] / weights : 0.0F;
            for (int c = 0; c < 3; ++c)
                p[c] = a > 0.0F ? static_cast<uint8_t>(std::clamp(std::lround(total[static_cast<size_t>(c)] / weights / a), 0L, 255L))
                                : 0U;
            p[3] = static_cast<uint8_t>(std::clamp(std::lround(a * 255.0F), 0L, 255L));
        }
    }
    return ring;
}

std::optional<Mm3dButtonRing> LoadMm3dButtonRing(const std::filesystem::path& romfs, uint64_t offset,
                                                 uint64_t size, std::string* error) {
    const auto atlas = LoadMm3dButtonAtlas(romfs, offset, size, error);
    if (!atlas.has_value()) return std::nullopt;
    auto ring = Mm3dButtonRingFromAtlas(atlas->Rgba, atlas->Width, atlas->Height);
    if (!ring.has_value() && error != nullptr) *error = "no button bubble in the HUD atlas";
    return ring;
}

Mm3dButtonStyle Mm3dButtonStyleFromSettings(bool customTextures, std::string_view directory) {
    if (!customTextures || directory.empty()) return Mm3dButtonStyle::Original;
    std::string lower(directory);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.find("dualshock") != std::string::npos || lower.find("playstation") != std::string::npos)
        return Mm3dButtonStyle::DualShock;
    if (lower.find("snes") != std::string::npos) return Mm3dButtonStyle::NintendoSnes;
    if (lower.find("nintendo") != std::string::npos) return Mm3dButtonStyle::Nintendo;
    return Mm3dButtonStyle::Xbox;
}

std::string Mm3dHudButtonLabel(Mm3dButtonStyle style, int slot) {
    static constexpr std::array<std::array<const char*, 4>, 5> kLabels{{
        {"Y", "X", "LB", "RB"},
        {"^", "#", "L1", "R1"},
        {"X", "Y", "L", "R"},
        {"X", "Y", "L", "R"},
        {"X", "Y", "ZL", "ZR"},  // the 3DS's own names (the game's yellow)
    }};
    return kLabels[static_cast<size_t>(style)][static_cast<size_t>(std::clamp(slot, 0, 3))];
}

std::array<uint8_t, 3> Mm3dHudButtonLabelColor(Mm3dButtonStyle style, int slot) {
    if (style == Mm3dButtonStyle::NintendoSnes) {
        // SNES pad: X blue, Y green, the shoulder buttons gray.
        static constexpr std::array<std::array<uint8_t, 3>, 4> kSnes{{
            {{96, 112, 230}}, {{70, 180, 90}}, {{200, 200, 208}}, {{200, 200, 208}},
        }};
        return kSnes[static_cast<size_t>(std::clamp(slot, 0, 3))];
    }
    return {245, 214, 64};
}

namespace {
// The label's geometry in sprite fractions (size 1).
struct LabelMetrics {
    float Left, Top, Height, Width, Stroke, Outline;  // Width: in glyph-box heights
};
LabelMetrics MeasureLabel(std::string_view label, const Mm3dButtonLabelLayout& layout, float s) {
    float width = 0.0F;
    size_t count = 0;
    for (const char c : label) {
        width += GlyphFor(c).Width + (count++ > 0 ? 0.22F : 0.0F);
    }
    const float scale = std::clamp(layout.Scale, 0.2F, 4.0F);
    const float height = 0.24F * s * scale;  // glyph box height in pixels
    const float stroke = std::max(1.5F, 0.06F * height * 2.2F);
    const float outline = stroke * 0.55F;
    return {0.04F * s + outline + layout.OffsetX * s, 0.03F * s + outline + layout.OffsetY * s, height, width,
            stroke, outline};
}
}  // namespace

std::array<float, 4> Mm3dHudButtonLabelBox(std::string_view label, const Mm3dButtonLabelLayout& layout) {
    constexpr float s = 192.0F;  // the sprite's size (strokes have a pixel minimum)
    const auto m = MeasureLabel(label, layout, s);
    const float pad = m.Stroke * 0.5F + m.Outline;
    return {(m.Left - pad) / s, (m.Top - pad) / s, (m.Left + m.Width * m.Height + pad) / s,
            (m.Top + m.Height + pad) / s};
}

std::vector<uint8_t> MakeMm3dHudButtonSprite(uint32_t size, std::string_view label,
                                             const Mm3dButtonRing* bubble,
                                             std::array<uint8_t, 3> labelColor,
                                             const Mm3dButtonLabelLayout& layout) {
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4U, 0U);
    const float s = static_cast<float>(size);
    const float cx = 0.56F * s, cy = 0.56F * s, radius = 0.42F * s, ring = std::max(1.5F, 0.035F * s);
    const bool useBubble =
        bubble != nullptr && bubble->Size > 0U && bubble->Rgba.size() == bubble->Size * bubble->Size * 4U;
    if (useBubble) {
        // The game's bubble, scaled so its ring's outer edge is the circle.
        std::vector<float> premultiplied(bubble->Rgba.size());
        for (size_t i = 0; i < bubble->Rgba.size(); i += 4U) {
            const float a = bubble->Rgba[i + 3U] / 255.0F;
            for (size_t c = 0; c < 3U; ++c) premultiplied[i + c] = bubble->Rgba[i + c] * a;
            premultiplied[i + 3U] = a;
        }
        const float scale = bubble->Radius / radius;
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const float sx = bubble->CenterX + (static_cast<float>(x) + 0.5F - cx) * scale;
                const float sy = bubble->CenterY + (static_cast<float>(y) + 0.5F - cy) * scale;
                const auto value = SampleBicubic(premultiplied, bubble->Size, sx, sy);
                const float a = std::clamp(value[3], 0.0F, 1.0F);
                if (a <= 0.0F) continue;
                uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4U];
                for (size_t c = 0; c < 3U; ++c)
                    p[c] = static_cast<uint8_t>(std::clamp(std::lround(value[c] / value[3]), 0L, 255L));
                p[3] = static_cast<uint8_t>(std::lround(a * 255.0F));
            }
        }
    }
    // Circle: translucent dark fill and a light ring (4x4 supersampled).
    for (uint32_t y = 0; y < size && !useBubble; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float fill = 0.0F, edge = 0.0F;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const float px = x + (sx + 0.5F) / 4.0F, py = y + (sy + 0.5F) / 4.0F;
                    const float d = std::hypot(px - cx, py - cy);
                    if (d <= radius - ring) fill += 1.0F / 16.0F;
                    else if (d <= radius) edge += 1.0F / 16.0F;
                }
            }
            uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4U];
            Blend(p, 18.0F, 20.0F, 26.0F, 0.42F * fill);
            Blend(p, 225.0F, 228.0F, 232.0F, 0.82F * edge);
        }
    }
    // Label: yellow strokes with a dark outline, in the top left corner.
    std::vector<Glyph> glyphs;
    for (const char c : label) glyphs.push_back(GlyphFor(c));
    if (glyphs.empty()) return rgba;
    const auto metrics = MeasureLabel(label, layout, s);
    const float width = metrics.Width, height = metrics.Height;
    const float stroke = metrics.Stroke, outline = metrics.Outline;
    const float left = metrics.Left, top = metrics.Top;
    std::vector<Segment> segments;
    float pen = 0.0F;
    for (const auto& glyph : glyphs) {
        for (const auto& st : glyph.Strokes) {
            segments.push_back({left + (pen + st.X0) * height, top + st.Y0 * height,
                                left + (pen + st.X1) * height, top + st.Y1 * height});
        }
        pen += glyph.Width + 0.22F;
    }
    const float reach = stroke * 0.5F + outline + 2.0F;
    const auto bound = [&](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0F, s)); };
    const uint32_t minX = bound(left - reach), maxX = bound(left + width * height + reach);
    const uint32_t minY = bound(top - reach), maxY = bound(top + height + reach);
    for (uint32_t y = minY; y < maxY; ++y) {
        for (uint32_t x = minX; x < maxX; ++x) {
            float d = 1.0e9F;
            for (const auto& segment : segments) d = std::min(d, DistanceToSegment(x + 0.5F, y + 0.5F, segment));
            const float inner = std::clamp(stroke * 0.5F + 0.5F - d, 0.0F, 1.0F);
            const float outer = std::clamp(stroke * 0.5F + outline + 0.5F - d, 0.0F, 1.0F);
            uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4U];
            Blend(p, 10.0F, 10.0F, 10.0F, 0.85F * outer);
            Blend(p, labelColor[0], labelColor[1], labelColor[2], inner);
        }
    }
    return rgba;
}

std::vector<uint8_t> Mm3dSpriteToFramebufferOrder(const std::vector<uint8_t>& rgba, uint32_t size) {
    std::vector<uint8_t> out(rgba.size());
    for (uint32_t row = 0; row < size; ++row) {        // memory row = screen x
        for (uint32_t column = 0; column < size; ++column) {  // memory column = screen y from the bottom
            const uint32_t x = size - 1U - row, y = size - 1U - column;
            std::copy_n(&rgba[(static_cast<size_t>(y) * size + x) * 4U], 4,
                        &out[(static_cast<size_t>(row) * size + column) * 4U]);
        }
    }
    return out;
}

}  // namespace Oot3dNativeGame
