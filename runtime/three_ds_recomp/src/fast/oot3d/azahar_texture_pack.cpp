#include "oot3d/renderer/azahar_texture_pack.h"
#include "fast/renderer/content_hash.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Oot3d::Renderer {
namespace {


bool IsPowerOfTwo(uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

uint32_t Morton8(uint32_t x, uint32_t y) noexcept {
    return ((x & 0x1U) << 0U) | ((y & 0x1U) << 1U) | ((x & 0x2U) << 1U) | ((y & 0x2U) << 2U) | ((x & 0x4U) << 2U) |
           ((y & 0x4U) << 3U);
}

uint32_t AlignedTextureDimension(uint32_t value) noexcept {
    return std::max<uint32_t>(8U, (value + 7U) & ~7U);
}

void FlipRows(std::span<uint8_t> pixels, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    if (pixels.size() != rowBytes * height) {
        throw std::runtime_error("texture row flip received an invalid byte count");
    }
    for (uint32_t row = 0; row < height / 2U; ++row) {
        auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
        auto second = pixels.begin() + static_cast<std::ptrdiff_t>((height - row - 1U) * rowBytes);
        std::swap_ranges(first, first + rowBytes, second);
    }
}

std::vector<uint8_t> MakeLegacyHashBytes(const AzaharTextureRequest& request) {
    if (request.Width == 0U || request.Height == 0U || request.NativeFormat > 13U) {
        throw std::runtime_error("legacy Azahar hash received invalid texture metadata");
    }

    if (request.NativeFormat >= 5U) {
        const size_t expected = static_cast<size_t>(request.Width) * request.Height * 4U;
        if (request.NativeRgba8.size() != expected) {
            throw std::runtime_error("legacy Azahar hash requires decoded RGBA8 bytes");
        }
        std::vector<uint8_t> decoded(request.NativeRgba8.begin(), request.NativeRgba8.end());
        // Azahar's legacy path hashes its OpenGL-oriented detiled buffer.
        FlipRows(decoded, request.Width, request.Height, 4U);
        return decoded;
    }

    constexpr std::array<uint8_t, 5> bytesPerPixel{ 4U, 3U, 2U, 2U, 2U };
    const uint32_t pixelBytes = bytesPerPixel[request.NativeFormat];
    const uint32_t alignedWidth = AlignedTextureDimension(request.Width);
    std::vector<uint8_t> output(static_cast<size_t>(request.Width) * request.Height * pixelBytes);
    for (uint32_t y = 0; y < request.Height; ++y) {
        for (uint32_t x = 0; x < request.Width; ++x) {
            const uint32_t tile = (y / 8U) * (alignedWidth / 8U) + (x / 8U);
            const size_t source = static_cast<size_t>(tile * 64U + Morton8(x % 8U, y % 8U)) * pixelBytes;
            const size_t destination = (static_cast<size_t>(request.Height - y - 1U) * request.Width + x) * pixelBytes;
            if (source + pixelBytes > request.NativeBytes.size()) {
                throw std::runtime_error("legacy Azahar hash source is truncated");
            }
            std::copy_n(request.NativeBytes.begin() + static_cast<std::ptrdiff_t>(source), pixelBytes,
                        output.begin() + static_cast<std::ptrdiff_t>(destination));
        }
    }
    return output;
}

std::string Hex16(uint64_t value) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::filesystem::path ResolveDefaultUserDirectory() {
    if (const char* overridePath = std::getenv("OOT3D_AZAHAR_USER_DIRECTORY");
        overridePath != nullptr && *overridePath != '\0') {
        return std::filesystem::path(overridePath);
    }
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "Azahar";
    }
#else
    if (const char* xdgData = std::getenv("XDG_DATA_HOME"); xdgData != nullptr && *xdgData != '\0') {
        return std::filesystem::path(xdgData) / "azahar";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local/share/azahar";
    }
#endif
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return (error ? std::filesystem::path(".") : current) / "Azahar";
}

AzaharTexturePackConfiguration NormalizeConfiguration(AzaharTexturePackConfiguration configuration) {
    if (configuration.UserDirectory.empty()) {
        configuration.UserDirectory = ResolveDefaultUserDirectory();
    }
    configuration.UserDirectory = configuration.UserDirectory.lexically_normal();
    if (const char* loadDirectory = std::getenv("OOT3D_AZAHAR_LOAD_DIRECTORY");
        loadDirectory != nullptr && *loadDirectory != '\0') {
        configuration.LoadDirectory = std::filesystem::path(loadDirectory);
    }
    if (const char* dumpDirectory = std::getenv("OOT3D_AZAHAR_DUMP_DIRECTORY");
        dumpDirectory != nullptr && *dumpDirectory != '\0') {
        configuration.DumpDirectory = std::filesystem::path(dumpDirectory);
    }
    if (!configuration.LoadDirectory.empty()) {
        configuration.LoadDirectory = configuration.LoadDirectory.lexically_normal();
    }
    if (!configuration.DumpDirectory.empty()) {
        configuration.DumpDirectory = configuration.DumpDirectory.lexically_normal();
    }
    if (!configuration.OverlayDirectory.empty()) {
        configuration.OverlayDirectory = configuration.OverlayDirectory.lexically_normal();
    }
    if (const char* titleId = std::getenv("OOT3D_AZAHAR_TITLE_ID"); titleId != nullptr && *titleId != '\0') {
        try {
            size_t parsed = 0;
            const uint64_t value = std::stoull(titleId, &parsed, 16);
            if (parsed == std::strlen(titleId)) {
                configuration.TitleId = value;
            }
        } catch (const std::exception&) {}
    }
    return configuration;
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("unable to open custom texture " + path.string());
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("custom texture has an invalid file size");
    }
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("unable to read custom texture " + path.string());
    }
    return bytes;
}

void AppendBigEndian32(std::vector<uint8_t>& destination, uint32_t value) {
    destination.push_back(static_cast<uint8_t>(value >> 24U));
    destination.push_back(static_cast<uint8_t>(value >> 16U));
    destination.push_back(static_cast<uint8_t>(value >> 8U));
    destination.push_back(static_cast<uint8_t>(value));
}

void AppendPngChunk(std::vector<uint8_t>& png, const std::array<char, 4>& type, std::span<const uint8_t> payload) {
    if (payload.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("PNG chunk is too large");
    }
    AppendBigEndian32(png, static_cast<uint32_t>(payload.size()));
    const size_t crcOffset = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), payload.begin(), payload.end());
    uLong crc =
        crc32(0L, reinterpret_cast<const Bytef*>(png.data() + crcOffset), static_cast<uInt>(4U + payload.size()));
    AppendBigEndian32(png, static_cast<uint32_t>(crc));
}

void WritePng(const std::filesystem::path& destination, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba8) {
    const uint64_t rowBytes = static_cast<uint64_t>(width) * 4U;
    const uint64_t filteredSize = (rowBytes + 1U) * height;
    if (width == 0U || height == 0U || rowBytes * height != rgba8.size() ||
        filteredSize > std::numeric_limits<uLong>::max() || filteredSize > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("PNG encoder received invalid RGBA8 data");
    }

    std::vector<uint8_t> filtered(static_cast<size_t>(filteredSize));
    for (uint32_t row = 0; row < height; ++row) {
        const size_t destinationRow = static_cast<size_t>(row) * (rowBytes + 1U);
        filtered[destinationRow] = 0U;
        std::copy_n(rgba8.begin() + static_cast<std::ptrdiff_t>(row * rowBytes), static_cast<size_t>(rowBytes),
                    filtered.begin() + static_cast<std::ptrdiff_t>(destinationRow + 1U));
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(filtered.size()));
    std::vector<uint8_t> compressed(compressedSize);
    const int result = compress2(compressed.data(), &compressedSize, filtered.data(),
                                 static_cast<uLong>(filtered.size()), Z_BEST_SPEED);
    if (result != Z_OK) {
        throw std::runtime_error("zlib failed while encoding PNG");
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png{ 0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU };
    std::vector<uint8_t> header;
    header.reserve(13U);
    AppendBigEndian32(header, width);
    AppendBigEndian32(header, height);
    header.insert(header.end(), { 8U, 6U, 0U, 0U, 0U });
    AppendPngChunk(png, { 'I', 'H', 'D', 'R' }, header);
    AppendPngChunk(png, { 'I', 'D', 'A', 'T' }, compressed);
    AppendPngChunk(png, { 'I', 'E', 'N', 'D' }, std::span<const uint8_t>{});

    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        throw std::runtime_error("unable to create texture dump directory: " + error.message());
    }
    const auto temporary = std::filesystem::path(destination.string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()))) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("unable to write texture dump " + destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            std::filesystem::remove(temporary, error);
            return;
        }
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("unable to publish texture dump: " + error.message());
    }
}

struct PackState {
    bool ConfigurationFound = false;
    bool SkipMipmaps = false;
    bool FlipPngFiles = true;
    bool UseNewHash = true;
    std::unordered_map<std::string, std::vector<uint64_t>> MappedHashes;
};

PackState ReadPackState(const std::filesystem::path& packPath, std::string* error) {
    PackState state;
    std::error_code existsError;
    if (!std::filesystem::exists(packPath, existsError) || existsError) {
        // These are Azahar's exact no-pack legacy defaults.
        state.SkipMipmaps = true;
        state.FlipPngFiles = true;
        state.UseNewHash = false;
        return state;
    }
    try {
        std::ifstream input(packPath);
        if (!input) {
            throw std::runtime_error("unable to open pack.json");
        }
        const nlohmann::json document = nlohmann::json::parse(input);
        if (!document.is_object()) {
            throw std::runtime_error("pack.json root is not an object");
        }
        state.ConfigurationFound = true;
        if (const auto options = document.find("options"); options != document.end() && options->is_object()) {
            if (const auto value = options->find("skip_mipmap"); value != options->end() && value->is_boolean()) {
                state.SkipMipmaps = value->get<bool>();
            }
            if (const auto value = options->find("flip_png_files"); value != options->end() && value->is_boolean()) {
                state.FlipPngFiles = value->get<bool>();
            }
            if (const auto value = options->find("use_new_hash"); value != options->end() && value->is_boolean()) {
                state.UseNewHash = value->get<bool>();
            }
        }
        if (const auto textures = document.find("textures"); textures != document.end() && textures->is_object()) {
            for (const auto& item : textures->items()) {
                try {
                    size_t parsed = 0;
                    const uint64_t hash = std::stoull(item.key(), &parsed, 16);
                    if (parsed != item.key().size()) {
                        continue;
                    }
                    const auto addMapping = [&](const std::string& path) {
                        const std::string filename = Lowercase(std::filesystem::path(path).filename().string());
                        if (!filename.empty()) {
                            state.MappedHashes[filename].push_back(hash);
                        }
                    };
                    if (item.value().is_string()) {
                        addMapping(item.value().get<std::string>());
                    } else if (item.value().is_array()) {
                        for (const auto& path : item.value()) {
                            if (path.is_string()) {
                                addMapping(path.get<std::string>());
                            }
                        }
                    }
                } catch (const std::exception&) {}
            }
        }
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = "unable to parse " + packPath.string() + ": " + exception.what();
        }
        state = {};
        state.SkipMipmaps = true;
        state.FlipPngFiles = true;
        state.UseNewHash = false;
    }
    return state;
}

void EnsureDumpPack(const std::filesystem::path& dumpDirectory, bool useNewHash) {
    std::error_code error;
    std::filesystem::create_directories(dumpDirectory, error);
    if (error) {
        throw std::runtime_error("unable to create Azahar dump directory: " + error.message());
    }
    const auto packPath = dumpDirectory / "pack.json";
    if (std::filesystem::exists(packPath, error)) {
        return;
    }
    if (error) {
        throw std::runtime_error("unable to inspect Azahar pack.json: " + error.message());
    }
    const nlohmann::ordered_json document{
        { "author", "citra" },
        { "version", "1.0.0" },
        { "description", "A graphics pack" },
        { "options",
          {
              { "skip_mipmap", false },
              { "flip_png_files", true },
              { "use_new_hash", useNewHash },
          } },
    };
    std::ofstream output(packPath);
    if (!output) {
        throw std::runtime_error("unable to create Azahar pack.json");
    }
    output << document.dump(4);
    if (!output) {
        throw std::runtime_error("unable to write Azahar pack.json");
    }
}

struct IndexedTexture {
    std::filesystem::path Path;
    bool FlipPngFiles = true;
    // The custom texture folder's image for a hash the overlay (button
    // style) also replaces: drawn at its resolution with only the overlay's
    // own changes (its button glyphs) painted over it.
    std::filesystem::path BasePath;
    bool BaseFlipPngFiles = true;
};

std::optional<uint64_t> ParseFilenameHash(const std::filesystem::path& path) {
    const std::string stem = path.stem().string();
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int format = 0;
    unsigned long long hash = 0;
    if (std::sscanf(stem.c_str(), "tex1_%ux%u_%llX_%u", &width, &height, &hash, &format) != 4) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(hash);
}

std::unordered_map<uint64_t, IndexedTexture> IndexTexturePack(const std::filesystem::path& loadDirectory,
                                                              const PackState& pack, size_t& unsupportedFiles,
                                                              std::string* error) {
    std::unordered_map<uint64_t, IndexedTexture> index;
    unsupportedFiles = 0U;
    std::error_code directoryError;
    if (!std::filesystem::exists(loadDirectory, directoryError) || directoryError) {
        if (directoryError && error != nullptr) {
            *error = "unable to inspect Azahar texture pack: " + directoryError.message();
        }
        return index;
    }
    std::vector<std::filesystem::path> files;
    std::error_code scanError;
    for (std::filesystem::recursive_directory_iterator
             iterator(loadDirectory,
                      std::filesystem::directory_options::skip_permission_denied |
                          std::filesystem::directory_options::follow_directory_symlink,
                      scanError),
         end;
         iterator != end && !scanError; iterator.increment(scanError)) {
        if (iterator->is_regular_file()) {
            files.push_back(iterator->path());
        }
    }
    if (scanError && error != nullptr) {
        *error = "unable to scan Azahar texture pack: " + scanError.message();
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        const std::string extension = Lowercase(path.extension().string());
        if (extension != ".png" && extension != ".dds" && extension != ".ktx") {
            continue;
        }
        const std::filesystem::path materialStem = path.stem();
        if (Lowercase(materialStem.extension().string()) == ".norm") {
            continue;
        }

        std::vector<uint64_t> hashes;
        const std::string filename = Lowercase(path.filename().string());
        if (const auto mapped = pack.MappedHashes.find(filename); mapped != pack.MappedHashes.end()) {
            hashes = mapped->second;
        }
        if (const auto parsed = ParseFilenameHash(path);
            parsed.has_value() && std::find(hashes.begin(), hashes.end(), *parsed) == hashes.end()) {
            hashes.push_back(*parsed);
        }
        if (hashes.empty()) {
            continue;
        }
        if (extension != ".png") {
            ++unsupportedFiles;
            continue;
        }
        for (const uint64_t hash : hashes) {
            index.try_emplace(hash, IndexedTexture{ path, pack.FlipPngFiles });
        }
    }
    return index;
}

// Appends the mip levels below the RGBA8 image (2x2 box filter, odd edges
// clamped) down to 1x1; returns the level count.
uint32_t AppendMipChain(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
    uint32_t levels = 1U;
    size_t source = 0U;
    pixels.reserve(pixels.size() + pixels.size() / 3U + 64U);
    while (width > 1U || height > 1U) {
        const uint32_t nextWidth = std::max(1U, width / 2U);
        const uint32_t nextHeight = std::max(1U, height / 2U);
        const size_t destination = pixels.size();
        pixels.resize(destination + static_cast<size_t>(nextWidth) * nextHeight * 4U);
        for (uint32_t y = 0; y < nextHeight; ++y) {
            const uint32_t y0 = std::min(height - 1U, y * 2U), y1 = std::min(height - 1U, y * 2U + 1U);
            for (uint32_t x = 0; x < nextWidth; ++x) {
                const uint32_t x0 = std::min(width - 1U, x * 2U), x1 = std::min(width - 1U, x * 2U + 1U);
                const uint8_t* a = &pixels[source + (static_cast<size_t>(y0) * width + x0) * 4U];
                const uint8_t* b = &pixels[source + (static_cast<size_t>(y0) * width + x1) * 4U];
                const uint8_t* c = &pixels[source + (static_cast<size_t>(y1) * width + x0) * 4U];
                const uint8_t* d = &pixels[source + (static_cast<size_t>(y1) * width + x1) * 4U];
                uint8_t* out = &pixels[destination + (static_cast<size_t>(y) * nextWidth + x) * 4U];
                for (int channel = 0; channel < 4; ++channel) {
                    out[channel] = static_cast<uint8_t>((a[channel] + b[channel] + c[channel] + d[channel] + 2) / 4);
                }
            }
        }
        source = destination;
        width = nextWidth;
        height = nextHeight;
        ++levels;
    }
    return levels;
}

struct DecodedPng {
    std::vector<uint8_t> Rgba8;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

DecodedPng DecodePng(const std::filesystem::path& path, bool flipPngFiles) {
    const auto input = ReadBinaryFile(path);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded =
        stbi_load_from_memory(input.data(), static_cast<int>(input.size()), &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        const std::string reason =
            stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown PNG decode failure";
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        throw std::runtime_error("unable to decode custom texture " + path.string() + ": " + reason);
    }
    DecodedPng result;
    result.Width = static_cast<uint32_t>(width);
    result.Height = static_cast<uint32_t>(height);
    result.Rgba8.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);
    stbi_image_free(decoded);
    if (!flipPngFiles) {
        FlipRows(result.Rgba8, result.Width, result.Height, 4U);
    }
    return result;
}

// The overlay's changes painted over the larger base image. Changed texels:
// those differing from the game's own texture (native, overlay-sized), or,
// without it, clusters differing strongly from the base scaled down (an HD
// redraw differs a little everywhere, a replaced glyph a lot).
void PaintOverlayChanges(DecodedPng& base, const DecodedPng& overlay, std::span<const uint8_t> native) {
    const uint32_t w = overlay.Width, h = overlay.Height;
    const size_t texels = static_cast<size_t>(w) * h;
    const float sx = static_cast<float>(base.Width) / static_cast<float>(w);
    const float sy = static_cast<float>(base.Height) / static_cast<float>(h);
    const bool exact = native.size() == texels * 4U;
    std::vector<uint8_t> reference(texels * 4U);
    if (exact) {
        std::copy(native.begin(), native.end(), reference.begin());
    } else {
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t x0 = static_cast<uint32_t>(x * sx), x1 = std::max(x0 + 1U, static_cast<uint32_t>((x + 1) * sx));
                const uint32_t y0 = static_cast<uint32_t>(y * sy), y1 = std::max(y0 + 1U, static_cast<uint32_t>((y + 1) * sy));
                std::array<uint32_t, 4> sum{};
                for (uint32_t by = y0; by < y1 && by < base.Height; ++by)
                    for (uint32_t bx = x0; bx < x1 && bx < base.Width; ++bx)
                        for (int c = 0; c < 4; ++c) sum[c] += base.Rgba8[(static_cast<size_t>(by) * base.Width + bx) * 4U + c];
                const uint32_t n = (std::min(y1, base.Height) - y0) * (std::min(x1, base.Width) - x0);
                for (int c = 0; c < 4; ++c)
                    reference[(static_cast<size_t>(y) * w + x) * 4U + c] = static_cast<uint8_t>(sum[c] / std::max(1U, n));
            }
        }
    }
    const int threshold = exact ? 10 : 72;
    std::vector<uint8_t> changed(texels, 0U);
    for (size_t i = 0; i < texels; ++i) {
        const uint8_t* a = &overlay.Rgba8[i * 4U];
        const uint8_t* b = &reference[i * 4U];
        if (a[3] < 8U && b[3] < 8U) continue;  // both clear
        int diff = std::abs(int(a[3]) - int(b[3]));
        for (int c = 0; c < 3; ++c) diff = std::max(diff, std::abs(int(a[c]) - int(b[c])) * std::max<int>(a[3], b[3]) / 255);
        changed[i] = diff > threshold ? 1U : 0U;
    }
    if (!exact) {  // drop specks (edges of the redraw)
        std::vector<uint32_t> stack, component;
        for (size_t start = 0; start < texels; ++start) {
            if (changed[start] != 1U) continue;
            component.clear();
            stack.assign(1, static_cast<uint32_t>(start));
            changed[start] = 2U;
            while (!stack.empty()) {
                const uint32_t i = stack.back();
                stack.pop_back();
                component.push_back(i);
                const int x = static_cast<int>(i % w), y = static_cast<int>(i / w);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(w) || ny >= static_cast<int>(h)) continue;
                        const size_t n = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                        if (changed[n] == 1U) { changed[n] = 2U; stack.push_back(static_cast<uint32_t>(n)); }
                    }
            }
            if (component.size() < 12U)
                for (const uint32_t i : component) changed[i] = 0U;
        }
    }
    // Grow by a texel so a glyph comes with its outline.
    std::vector<float> mask(texels, 0.0F);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            bool nearGlyph = false;
            for (int dy = -1; dy <= 1 && !nearGlyph; ++dy)
                for (int dx = -1; dx <= 1 && !nearGlyph; ++dx) {
                    const int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                    nearGlyph = nx >= 0 && ny >= 0 && nx < static_cast<int>(w) && ny < static_cast<int>(h) &&
                           changed[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)] != 0U;
                }
            mask[static_cast<size_t>(y) * w + x] = nearGlyph ? 1.0F : 0.0F;
        }
    const auto bilinear = [&](float fx, float fy, std::array<float, 5>& out) {
        fx -= 0.5F; fy -= 0.5F;
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        out = {};
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int x = std::clamp(x0 + i, 0, static_cast<int>(w) - 1);
                const int y = std::clamp(y0 + j, 0, static_cast<int>(h) - 1);
                const float weight = (i ? tx : 1.0F - tx) * (j ? ty : 1.0F - ty);
                const size_t t = static_cast<size_t>(y) * w + static_cast<size_t>(x);
                const uint8_t* p = &overlay.Rgba8[t * 4U];
                const float a = p[3] / 255.0F;
                for (int c = 0; c < 3; ++c) out[c] += p[c] * a * weight;  // premultiplied
                out[3] += a * weight;
                out[4] += mask[t] * weight;
            }
    };
    std::array<float, 5> sample{};
    for (uint32_t y = 0; y < base.Height; ++y)
        for (uint32_t x = 0; x < base.Width; ++x) {
            const float ox = (static_cast<float>(x) + 0.5F) / sx, oy = (static_cast<float>(y) + 0.5F) / sy;
            const size_t nearest = static_cast<size_t>(std::min(h - 1U, static_cast<uint32_t>(oy))) * w +
                                   std::min(w - 1U, static_cast<uint32_t>(ox));
            if (mask[nearest] == 0.0F) {  // quick reject: nothing masked in the bilinear footprint
                bool any = false;
                for (int dy = -1; dy <= 1 && !any; ++dy)
                    for (int dx = -1; dx <= 1 && !any; ++dx) {
                        const int nx = static_cast<int>(ox) + dx, ny = static_cast<int>(oy) + dy;
                        any = nx >= 0 && ny >= 0 && nx < static_cast<int>(w) && ny < static_cast<int>(h) &&
                              mask[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)] != 0.0F;
                    }
                if (!any) continue;
            }
            bilinear(ox, oy, sample);
            const float m = std::clamp(sample[4], 0.0F, 1.0F);
            if (m <= 0.0F) continue;
            uint8_t* p = &base.Rgba8[(static_cast<size_t>(y) * base.Width + x) * 4U];
            const float a = std::clamp(sample[3], 0.0F, 1.0F);
            const float baseA = p[3] / 255.0F;
            const float outA = a * m + baseA * (1.0F - m);
            for (int c = 0; c < 3; ++c) {
                const float premultiplied = sample[c] * m + p[c] * baseA * (1.0F - m);
                p[c] = outA > 0.0F ? static_cast<uint8_t>(std::clamp(std::lround(premultiplied / outA), 0L, 255L)) : 0U;
            }
            p[3] = static_cast<uint8_t>(std::clamp(std::lround(outA * 255.0F), 0L, 255L));
        }
}

std::shared_ptr<const AzaharTextureReplacement> DecodeReplacement(const IndexedTexture& indexed, uint64_t hash,
                                                                  std::span<const uint8_t> native = {}) {
    if (!indexed.BasePath.empty()) {
        static const bool overlayOnly = std::getenv("TRIAEVUM_NO_HD_UNDER_BUTTON_STYLE") != nullptr;
        auto overlay = DecodePng(indexed.Path, indexed.FlipPngFiles);
        DecodedPng image;
        std::filesystem::path source = indexed.Path;
        if (!overlayOnly) {
            auto base = DecodePng(indexed.BasePath, indexed.BaseFlipPngFiles);
            if (base.Width > overlay.Width && base.Height > overlay.Height) {
                std::vector<uint8_t> flippedNative;
                if (!indexed.FlipPngFiles && !native.empty()) {  // the native image as the overlay is laid out
                    flippedNative.assign(native.begin(), native.end());
                    FlipRows(flippedNative, overlay.Width, overlay.Height, 4U);
                    native = flippedNative;
                }
                PaintOverlayChanges(base, overlay, native);
                image = std::move(base);
                source = indexed.BasePath;
            }
        }
        if (image.Rgba8.empty()) image = std::move(overlay);
        static const bool trace = std::getenv("TRIAEVUM_TEXTURE_TRACE") != nullptr;
        if (trace) {
            std::fprintf(stderr, "TEXTRACE merged %016llX native=%d -> %ux%u from %s\n",
                         static_cast<unsigned long long>(hash), native.empty() ? 0 : 1, image.Width, image.Height,
                         source.filename().string().c_str());
        }
        auto pixels = std::make_shared<std::vector<uint8_t>>(std::move(image.Rgba8));
        auto replacement = std::make_shared<AzaharTextureReplacement>();
        replacement->Width = image.Width;
        replacement->Height = image.Height;
        replacement->MipLevels = AppendMipChain(*pixels, image.Width, image.Height);
        replacement->NativeHash = hash;
        replacement->Rgba8 = std::move(pixels);
        replacement->SourcePath = source;
        return replacement;
    }
    const auto input = ReadBinaryFile(indexed.Path);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded =
        stbi_load_from_memory(input.data(), static_cast<int>(input.size()), &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        const std::string reason =
            stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown PNG decode failure";
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        throw std::runtime_error("unable to decode custom texture " + indexed.Path.string() + ": " + reason);
    }
    const uint64_t byteCount = static_cast<uint64_t>(width) * height * 4U;
    if (byteCount > std::numeric_limits<size_t>::max()) {
        stbi_image_free(decoded);
        throw std::runtime_error("custom texture dimensions are too large");
    }
    auto pixels = std::make_shared<std::vector<uint8_t>>(decoded, decoded + static_cast<size_t>(byteCount));
    stbi_image_free(decoded);

    // Azahar flips PNGs into its bottom-left OpenGL staging layout. The
    // native NRI/Vulkan decoder already matches the on-disk dump layout,
    // so only packs that explicitly disable Azahar's flip need conversion.
    if (!indexed.FlipPngFiles) {
        FlipRows(*pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4U);
    }

    const uint32_t mipLevels = AppendMipChain(*pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    auto replacement = std::make_shared<AzaharTextureReplacement>();
    replacement->Width = static_cast<uint32_t>(width);
    replacement->Height = static_cast<uint32_t>(height);
    replacement->MipLevels = mipLevels;
    replacement->NativeHash = hash;
    replacement->Rgba8 = std::move(pixels);
    replacement->SourcePath = indexed.Path;
    return replacement;
}

struct DumpJob {
    uint64_t Hash = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::filesystem::path Destination;
    std::vector<uint8_t> Rgba8;
};

struct LoadJob {
    uint64_t Hash = 0;
    uint64_t Generation = 0;
    IndexedTexture Texture;
    bool Preload = false;  // decoded ahead of use (background, lower priority)
    std::shared_ptr<const std::vector<uint8_t>> Native;  // the game's own image (merged layers)
};

// The decoded size of a PNG from its header (no decode): width x height x 4.
uint64_t PngDecodedBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 24> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) {
        return 0U;
    }
    const auto be32 = [&](size_t at) {
        return (uint32_t{header[at]} << 24U) | (uint32_t{header[at + 1]} << 16U) |
               (uint32_t{header[at + 2]} << 8U) | uint32_t{header[at + 3]};
    };
    return uint64_t{be32(16)} * be32(20) * 4U;
}

} // namespace

uint64_t AzaharCityHash64(std::span<const uint8_t> bytes) noexcept {
    return ::Fast::Renderer::ContentHash64(bytes);
}

uint64_t ComputeAzaharTextureHash(const AzaharTextureRequest& request, bool useNewHash, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    try {
        if (request.NativeBytes.empty()) {
            throw std::runtime_error("Azahar texture hash requires native PICA bytes");
        }
        if (useNewHash) {
            return AzaharCityHash64(request.NativeBytes);
        }
        const auto legacy = MakeLegacyHashBytes(request);
        return AzaharCityHash64(legacy);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return 0U;
    }
}

std::string MakeAzaharTextureFilename(uint16_t width, uint16_t height, uint64_t hash, uint8_t nativeFormat,
                                      uint32_t mipLevel) {
    return "tex1_" + std::to_string(width) + "x" + std::to_string(height) + "_" + Hex16(hash) + "_" +
           std::to_string(nativeFormat) + "_mip" + std::to_string(mipLevel) + ".png";
}

class AzaharTexturePackRuntime::Impl {
  public:
    Impl() {
        // Constructor-body startup guarantees every queue and stop flag exists
        // before WorkerMain can inspect them. Several workers: an area's
        // custom textures are PNGs of up to 4K, decoded in parallel.
        const unsigned cores = std::max(1U, std::thread::hardware_concurrency());
        const unsigned workers = std::clamp(cores > 2U ? cores - 2U : 1U, 1U, 8U);
        for (unsigned index = 0; index < workers; ++index) {
            mWorkers.emplace_back([this] { WorkerMain(false); });
        }
        // Preloading has threads of its own at the lowest priority: decoding
        // a 4K pack on half the workers at normal priority stuttered the
        // game for its first seconds.
        const unsigned preloaders = std::clamp(cores / 6U, 1U, 4U);
        for (unsigned index = 0; index < preloaders; ++index) {
            mWorkers.emplace_back([this] { WorkerMain(true); });
        }
    }

    ~Impl() {
        {
            std::scoped_lock lock(mMutex);
            mStopping = true;
        }
        mWorkAvailable.notify_all();
        mPreloadAvailable.notify_all();
        for (auto& worker : mWorkers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void Configure(AzaharTexturePackConfiguration configuration) {
        configuration = NormalizeConfiguration(std::move(configuration));
        std::scoped_lock lock(mMutex);
        if (mConfigured && configuration == mConfiguration) {
            return;
        }
        // The memory limit and preloading apply without reindexing (and
        // without the renderer dropping its textures).
        auto same = configuration;
        same.CacheBudgetBytes = mConfiguration.CacheBudgetBytes;
        same.Preload = mConfiguration.Preload;
        if (mConfigured && same == mConfiguration) {
            mConfiguration = std::move(configuration);
            EvictLocked();
            for (const auto& job : mPreloadJobs) {
                mPendingLoadGenerations.erase(job.Hash);
            }
            mPreloadJobs.clear();
            StartPreloadLocked();
            return;
        }
        mConfiguration = std::move(configuration);
        if (mConfigured) {
            ++mGeneration;
        } else {
            mGeneration = 1U;
            mConfigured = true;
        }
        try {
            RebuildLocked();
        } catch (const std::exception& exception) {
            mIndex.clear();
            ClearCacheLocked();
            mLastError = exception.what();
        }
    }

    void Reload() {
        std::scoped_lock lock(mMutex);
        if (!mConfigured) {
            return;
        }
        ++mGeneration;
        try {
            RebuildLocked();
        } catch (const std::exception& exception) {
            mIndex.clear();
            ClearCacheLocked();
            mLastError = exception.what();
        }
    }

    std::shared_ptr<const AzaharTextureReplacement> ResolveAndMaybeDump(const AzaharTextureRequest& request) {
        bool dumpEnabled = false;
        bool loadEnabled = false;
        bool dumpUsesNewHash = true;
        bool loadUsesNewHash = true;
        uint64_t generation = 0;
        std::filesystem::path dumpDirectory;
        {
            std::scoped_lock lock(mMutex);
            if (!mConfigured) {
                return nullptr;
            }
            dumpEnabled = mConfiguration.DumpTextures;
            loadEnabled = LoadActiveLocked();
            dumpUsesNewHash = mDumpUsesNewHash;
            loadUsesNewHash = mPack.UseNewHash;
            generation = mGeneration;
            dumpDirectory = mDumpDirectory;
        }
        if (!dumpEnabled && !loadEnabled) {
            return nullptr;
        }

        std::string hashError;
        std::optional<uint64_t> modernHash;
        std::optional<uint64_t> legacyHash;
        const auto resolveHash = [&](bool useNewHash) {
            std::optional<uint64_t>& cached = useNewHash ? modernHash : legacyHash;
            if (!cached.has_value()) {
                const uint64_t computed = ComputeAzaharTextureHash(request, useNewHash, &hashError);
                if (!hashError.empty()) {
                    return std::optional<uint64_t>{};
                }
                cached = computed;
            }
            return cached;
        };

        if (dumpEnabled) {
            const auto dumpHash = resolveHash(dumpUsesNewHash);
            if (dumpHash.has_value()) {
                try {
                    QueueDump(request, *dumpHash, generation, dumpDirectory);
                } catch (const std::exception& exception) { SetError(exception.what()); }
            }
        }
        if (!loadEnabled) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return nullptr;
        }

        // Packs without pack.json may use either hash (Azahar assumes the
        // legacy one; packs dumped by newer emulators use the new one): when
        // the preferred hash has no texture, try the other.
        auto loadHash = resolveHash(loadUsesNewHash);
        if (loadHash.has_value()) {
            bool indexed = false;
            {
                std::scoped_lock lock(mMutex);
                indexed = mIndex.contains(*loadHash);
            }
            if (!indexed) {
                std::string otherError;
                std::swap(otherError, hashError);
                const auto other = resolveHash(!loadUsesNewHash);
                std::swap(otherError, hashError);
                if (other.has_value()) {
                    std::scoped_lock lock(mMutex);
                    if (mIndex.contains(*other)) {
                        loadHash = other;
                    }
                }
            }
        }
        if (!loadHash.has_value()) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return nullptr;
        }

        IndexedTexture indexed;
        {
            std::scoped_lock lock(mMutex);
            if (generation != mGeneration) {
                return nullptr;
            }
            if (const auto cached = mReplacementCache.find(*loadHash); cached != mReplacementCache.end()) {
                TouchLocked(*loadHash);
                return cached->second;
            }
            const auto found = mIndex.find(*loadHash);
            if (found == mIndex.end()) {
                return nullptr;
            }
            indexed = found->second;
        }

        try {
            const auto replacement = DecodeReplacement(indexed, *loadHash, request.NativeRgba8);
            std::scoped_lock lock(mMutex);
            if (generation != mGeneration || !LoadActiveLocked()) {
                return nullptr;
            }
            if (const auto cached = mReplacementCache.find(*loadHash); cached != mReplacementCache.end()) {
                TouchLocked(*loadHash);
                return cached->second;
            }
            InsertCacheLocked(*loadHash, replacement);
            TouchLocked(*loadHash);
            return replacement;
        } catch (const std::exception& exception) {
            SetError(exception.what());
            return nullptr;
        }
    }

    AzaharTextureResolveResult ResolveOrQueue(
        const AzaharTextureRequest& request) {
        bool dumpEnabled = false;
        bool loadEnabled = false;
        bool dumpUsesNewHash = true;
        bool loadUsesNewHash = true;
        uint64_t generation = 0;
        std::filesystem::path dumpDirectory;
        {
            std::scoped_lock lock(mMutex);
            if (!mConfigured) {
                return {};
            }
            dumpEnabled = mConfiguration.DumpTextures;
            loadEnabled = LoadActiveLocked();
            dumpUsesNewHash = mDumpUsesNewHash;
            loadUsesNewHash = mPack.UseNewHash;
            generation = mGeneration;
            dumpDirectory = mDumpDirectory;
        }
        if (!dumpEnabled && !loadEnabled) {
            return {};
        }

        std::string hashError;
        std::optional<uint64_t> modernHash;
        std::optional<uint64_t> legacyHash;
        const auto resolveHash = [&](bool useNewHash) {
            std::optional<uint64_t>& cached =
                useNewHash ? modernHash : legacyHash;
            if (!cached.has_value()) {
                const uint64_t computed =
                    ComputeAzaharTextureHash(
                        request, useNewHash, &hashError);
                if (!hashError.empty()) {
                    return std::optional<uint64_t>{};
                }
                cached = computed;
            }
            return cached;
        };

        if (dumpEnabled) {
            const auto dumpHash = resolveHash(dumpUsesNewHash);
            if (dumpHash.has_value()) {
                try {
                    QueueDump(
                        request, *dumpHash, generation,
                        dumpDirectory);
                } catch (const std::exception& exception) {
                    SetError(exception.what());
                }
            }
        }
        if (!loadEnabled) {
            if (!hashError.empty()) {
                SetError(hashError);
                return {
                    AzaharTextureResolveState::Failed, 0U, nullptr};
            }
            return {};
        }

        // Packs without pack.json may use either hash (Azahar assumes the
        // legacy one; packs dumped by newer emulators use the new one): when
        // the preferred hash has no texture, try the other.
        auto loadHash = resolveHash(loadUsesNewHash);
        if (loadHash.has_value()) {
            bool indexed = false;
            {
                std::scoped_lock lock(mMutex);
                indexed = mIndex.contains(*loadHash);
            }
            if (!indexed) {
                std::string otherError;
                std::swap(otherError, hashError);
                const auto other = resolveHash(!loadUsesNewHash);
                std::swap(otherError, hashError);
                if (other.has_value()) {
                    std::scoped_lock lock(mMutex);
                    if (mIndex.contains(*other)) {
                        loadHash = other;
                    }
                }
            }
        }
        if (!loadHash.has_value()) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return {
                AzaharTextureResolveState::Failed, 0U, nullptr};
        }

        std::scoped_lock lock(mMutex);
        if (generation != mGeneration ||
            !LoadActiveLocked()) {
            return {
                AzaharTextureResolveState::Disabled,
                *loadHash, nullptr};
        }
        if (const auto cached = mReplacementCache.find(*loadHash);
            cached != mReplacementCache.end()) {
            TouchLocked(*loadHash);
            return {
                AzaharTextureResolveState::Ready,
                *loadHash, cached->second};
        }
        const auto indexed = mIndex.find(*loadHash);
        if (indexed == mIndex.end()) {
            return {
                AzaharTextureResolveState::Missing,
                *loadHash, nullptr};
        }
        if (mFailedLoadHashes.contains(*loadHash)) {
            return {
                AzaharTextureResolveState::Failed,
                *loadHash, nullptr};
        }
        if (const auto pending =
                mPendingLoadGenerations.find(*loadHash);
            pending != mPendingLoadGenerations.end() &&
            pending->second == generation) {
            if (const auto preload = std::find_if(mPreloadJobs.begin(), mPreloadJobs.end(),
                                                  [&](const LoadJob& job) { return job.Hash == *loadHash; });
                preload != mPreloadJobs.end()) {
                auto job = std::move(*preload);
                mPreloadJobs.erase(preload);
                job.Preload = false;
                mLoadJobs.push_front(std::move(job));
                mWorkAvailable.notify_one();
            }
            DemandLocked(*loadHash);  // (also one being preloaded right now)
            return {
                AzaharTextureResolveState::Pending,
                *loadHash, nullptr};
        }

        mPendingLoadGenerations.insert_or_assign(
            *loadHash, generation);
        mLoadJobs.push_back({
            *loadHash, generation, indexed->second});
        if (!indexed->second.BasePath.empty() && !request.NativeRgba8.empty()) {
            mLoadJobs.back().Native = std::make_shared<const std::vector<uint8_t>>(
                request.NativeRgba8.begin(), request.NativeRgba8.end());
        }
        DemandLocked(*loadHash);
        mWorkAvailable.notify_one();
        return {
            AzaharTextureResolveState::Pending,
            *loadHash, nullptr};
    }

    AzaharTextureResolveResult PollQueued(
        uint64_t nativeHash, std::span<const uint8_t> nativeRgba8) {
        std::scoped_lock lock(mMutex);
        if (!mConfigured ||
            !LoadActiveLocked()) {
            return {
                AzaharTextureResolveState::Disabled,
                nativeHash, nullptr};
        }
        if (const auto cached =
                mReplacementCache.find(nativeHash);
            cached != mReplacementCache.end()) {
            TouchLocked(nativeHash);
            return {
                AzaharTextureResolveState::Ready,
                nativeHash, cached->second};
        }
        if (mFailedLoadHashes.contains(nativeHash)) {
            return {
                AzaharTextureResolveState::Failed,
                nativeHash, nullptr};
        }
        if (const auto pending =
                mPendingLoadGenerations.find(nativeHash);
            pending != mPendingLoadGenerations.end() &&
            pending->second == mGeneration) {
            return {
                AzaharTextureResolveState::Pending,
                nativeHash, nullptr};
        }
        // Indexed but neither decoded nor queued: dropped from the cache
        // (memory limit) or a texture restored from a savestate: decode it
        // again.
        if (const auto indexed = mIndex.find(nativeHash); indexed != mIndex.end()) {
            mPendingLoadGenerations.insert_or_assign(nativeHash, mGeneration);
            mLoadJobs.push_back({nativeHash, mGeneration, indexed->second});
            if (!indexed->second.BasePath.empty() && !nativeRgba8.empty()) {
                mLoadJobs.back().Native =
                    std::make_shared<const std::vector<uint8_t>>(nativeRgba8.begin(), nativeRgba8.end());
            }
            DemandLocked(nativeHash);
            mWorkAvailable.notify_one();
            return {
                AzaharTextureResolveState::Pending,
                nativeHash, nullptr};
        }
        return {
            AzaharTextureResolveState::Missing,
            nativeHash, nullptr};
    }

    AzaharTexturePackStatus Snapshot() const {
        std::scoped_lock lock(mMutex);
        AzaharTexturePackStatus status;
        status.DumpTextures = mConfiguration.DumpTextures;
        status.LoadCustomTextures = mConfiguration.LoadCustomTextures;
        status.PackConfigurationFound = mPack.ConfigurationFound;
        status.PackUsesNewHash = mPack.UseNewHash;
        status.Generation = mGeneration;
        status.IndexedTextures = mIndex.size();
        status.LoadedTextures = mReplacementCache.size();
        status.DumpedTextures = mDumpedCount;
        status.PendingDumps = mPendingDumps;
        status.PendingLoads = mPendingLoads;
        status.PreloadsLeft = mPreloadJobs.size() + mActivePreloads;
        status.CacheBytes = mCacheBytes;
        status.FailedLoads = mFailedLoadHashes.size();
        status.UnsupportedTextureFiles = mUnsupportedFiles;
        status.LoadDirectory = mLoadDirectory;
        status.DumpDirectory = mDumpDirectory;
        status.LastError = mLastError;
        return status;
    }

    uint64_t Generation() const {
        std::scoped_lock lock(mMutex);
        return mGeneration;
    }

    void WaitForPendingDumps() {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [&] { return mPendingDumps == 0U; });
    }

    void WaitForPendingLoads() {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [&] { return mPendingLoads == 0U; });
    }

    bool WaitForPendingLoadsFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mMutex);
        return mIdle.wait_for(lock, timeout, [&] { return mPendingLoads == 0U; });
    }

  private:
    // Decoded textures stay in memory for the renderer to upload again (an
    // area visited before): at most the configured limit (a 4K pack decodes
    // to several GB), least recently used first; preloaded ones not used yet
    // count as the oldest. One just decoded for a frame is the newest.

    static uint64_t ReplacementBytes(const std::shared_ptr<const AzaharTextureReplacement>& replacement) {
        return replacement != nullptr && replacement->Rgba8 != nullptr ? replacement->Rgba8->size() : 0U;
    }

    void TouchLocked(uint64_t hash) const {
        mCacheUse[hash] = ++mUseClock;
    }

    void ClearCacheLocked() {
        mReplacementCache.clear();
        mCacheUse.clear();
        mCacheBytes = 0U;
    }

    void InsertCacheLocked(uint64_t hash, std::shared_ptr<const AzaharTextureReplacement> replacement,
                           bool preloaded = false) {
        if (const auto old = mReplacementCache.find(hash); old != mReplacementCache.end()) {
            mCacheBytes -= std::min(mCacheBytes, ReplacementBytes(old->second));
        }
        mCacheBytes += ReplacementBytes(replacement);
        mReplacementCache.insert_or_assign(hash, std::move(replacement));
        mCacheUse[hash] = preloaded ? 0U : ++mUseClock;
        EvictLocked();
    }

    void EvictLocked() {
        const uint64_t budget = mConfiguration.CacheBudgetBytes;
        if (mCacheBytes <= budget) {
            return;
        }
        std::vector<std::pair<uint64_t, uint64_t>> taken;  // last use, hash
        for (const auto& [cachedHash, use] : mCacheUse) {
            if (mReplacementCache.contains(cachedHash)) {
                taken.emplace_back(use, cachedHash);
            }
        }
        std::sort(taken.begin(), taken.end());
        for (const auto& [use, cachedHash] : taken) {
            if (mCacheBytes <= budget * 7U / 8U) {
                break;
            }
            const auto found = mReplacementCache.find(cachedHash);
            mCacheBytes -= std::min(mCacheBytes, ReplacementBytes(found->second));
            mReplacementCache.erase(found);
            mCacheUse.erase(cachedHash);
        }
    }

    bool LoadActiveLocked() const {
        return mConfiguration.LoadCustomTextures || !mConfiguration.OverlayDirectory.empty();
    }

    void RebuildLocked() {
        mIndex.clear();
        ClearCacheLocked();
        mFailedLoadHashes.clear();
        mPendingLoadGenerations.clear();
        mPreloadJobs.clear();
        mDumpedHashes.clear();
        mLastError.clear();
        mUnsupportedFiles = 0U;
        const std::string title = Hex16(mConfiguration.TitleId);
        mLoadDirectory = mConfiguration.LoadDirectory.empty()
                             ? mConfiguration.UserDirectory / "load" / "textures" / title
                             : mConfiguration.LoadDirectory;
        mDumpDirectory = mConfiguration.DumpDirectory.empty()
                             ? mConfiguration.UserDirectory / "dump" / "textures" / title
                             : mConfiguration.DumpDirectory;
        mPack = ReadPackState(mLoadDirectory / "pack.json", &mLastError);
        std::string dumpPackError;
        const auto dumpPack = ReadPackState(mDumpDirectory / "pack.json", &dumpPackError);
        mDumpUsesNewHash = dumpPack.ConfigurationFound ? dumpPack.UseNewHash : true;
        if (mLastError.empty() && !dumpPackError.empty()) {
            mLastError = std::move(dumpPackError);
        }

        if (!mConfiguration.OverlayDirectory.empty()) {
            const auto overlayPack = ReadPackState(mConfiguration.OverlayDirectory / "pack.json", &mLastError);
            size_t overlayUnsupported = 0U;
            mIndex = IndexTexturePack(mConfiguration.OverlayDirectory, overlayPack, overlayUnsupported, &mLastError);
        }
        if (mConfiguration.LoadCustomTextures) {
            auto base = IndexTexturePack(mLoadDirectory, mPack, mUnsupportedFiles, &mLastError);
            for (auto& [hash, texture] : base) {
                // The overlay wins; a larger custom image stays under its changes.
                const auto [entry, inserted] = mIndex.try_emplace(hash, texture);
                if (!inserted && entry->second.BasePath.empty()) {
                    entry->second.BasePath = texture.Path;
                    entry->second.BaseFlipPngFiles = texture.FlipPngFiles;
                }
            }
        }
        if (mConfiguration.DumpTextures) {
            try {
                EnsureDumpPack(mDumpDirectory, mDumpUsesNewHash);
            } catch (const std::exception& exception) { mLastError = exception.what(); }
        }
        StartPreloadLocked();
    }

    // Preload: the pack's textures are decoded in the background (up to the
    // memory limit), so areas appear with them without waiting.
    void StartPreloadLocked() {
        if (!mConfiguration.Preload || !LoadActiveLocked()) {
            return;
        }
        uint64_t planned = mCacheBytes;
        std::unordered_set<std::string> paths;
        for (const auto& [hash, texture] : mIndex) {
            if (!texture.BasePath.empty()) {
                continue;  // merged on first use, with the game's own image
            }
            if (mReplacementCache.contains(hash) || mPendingLoadGenerations.contains(hash) ||
                mFailedLoadHashes.contains(hash) || !paths.insert(texture.Path.string()).second) {
                continue;
            }
            const uint64_t bytes = PngDecodedBytes(texture.Path);
            if (bytes == 0U || planned + bytes > mConfiguration.CacheBudgetBytes * 9U / 10U) {
                continue;
            }
            planned += bytes;
            mPendingLoadGenerations.insert_or_assign(hash, mGeneration);
            mPreloadJobs.push_back({hash, mGeneration, texture, true});
        }
        mPreloadAvailable.notify_all();
    }

    // A texture needed now (a frame waits for it): queued, or a preload of
    // it moved to the front.
    void DemandLocked(uint64_t hash) {
        if (mDemanded.insert(hash).second) {
            ++mPendingLoads;
        }
    }

    void QueueDump(const AzaharTextureRequest& request, uint64_t hash, uint64_t generation,
                   const std::filesystem::path& dumpDirectory) {
        if (!IsPowerOfTwo(request.Width) || !IsPowerOfTwo(request.Height)) {
            return;
        }
        const size_t expected = static_cast<size_t>(request.Width) * request.Height * 4U;
        if (request.NativeRgba8.size() != expected) {
            SetError("Azahar texture dump requires decoded RGBA8 bytes");
            return;
        }
        const auto destination = dumpDirectory / MakeAzaharTextureFilename(request.Width, request.Height, hash,
                                                                           request.NativeFormat, request.MipLevel);

        std::scoped_lock lock(mMutex);
        if (generation != mGeneration || !mConfiguration.DumpTextures || mDumpedHashes.contains(hash)) {
            return;
        }
        mDumpedHashes.insert(hash);
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            return;
        }
        if (existsError) {
            mDumpedHashes.erase(hash);
            mLastError = "unable to inspect texture dump path: " + existsError.message();
            return;
        }
        DumpJob job;
        job.Hash = hash;
        job.Width = request.Width;
        job.Height = request.Height;
        job.Destination = destination;
        job.Rgba8.assign(request.NativeRgba8.begin(), request.NativeRgba8.end());
        mDumpJobs.push_back(std::move(job));
        ++mPendingDumps;
        mWorkAvailable.notify_one();
    }

    void SetError(std::string error) {
        std::scoped_lock lock(mMutex);
        mLastError = std::move(error);
    }

    static void LowerThreadPriority() {
#if defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#elif defined(__linux__)
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);
#endif
    }

    void WorkerMain(bool preloader) {
        if (preloader) {
            LowerThreadPriority();
        }
        for (;;) {
            LoadJob loadJob;
            DumpJob job;
            bool load = false;
            {
                std::unique_lock lock(mMutex);
                if (preloader) {
                    mPreloadAvailable.wait(lock, [&] { return mStopping || !mPreloadJobs.empty(); });
                    if (mStopping) {
                        return;
                    }
                } else {
                    mWorkAvailable.wait(lock, [&] {
                        return mStopping || !mLoadJobs.empty() || !mDumpJobs.empty();
                    });
                    if (mLoadJobs.empty() && mDumpJobs.empty()) {
                        return;  // stopping
                    }
                }
                if (preloader) {
                    load = true;
                    loadJob = std::move(mPreloadJobs.front());
                    mPreloadJobs.pop_front();
                    ++mActivePreloads;
                } else if (!mLoadJobs.empty()) {
                    load = true;
                    loadJob = std::move(mLoadJobs.front());
                    mLoadJobs.pop_front();
                } else {
                    job = std::move(mDumpJobs.front());
                    mDumpJobs.pop_front();
                }
            }

            if (load) {
                std::shared_ptr<const AzaharTextureReplacement>
                    replacement;
                std::string failure;
                try {
                    replacement = DecodeReplacement(
                        loadJob.Texture, loadJob.Hash,
                        loadJob.Native != nullptr ? std::span<const uint8_t>(*loadJob.Native)
                                                  : std::span<const uint8_t>{});
                } catch (const std::exception& exception) {
                    failure = exception.what();
                }
                {
                    std::scoped_lock lock(mMutex);
                    const auto pending =
                        mPendingLoadGenerations.find(loadJob.Hash);
                    const bool currentJob =
                        pending != mPendingLoadGenerations.end() &&
                        pending->second == loadJob.Generation;
                    if (currentJob) {
                        if (replacement != nullptr &&
                            loadJob.Generation == mGeneration &&
                            LoadActiveLocked()) {
                            InsertCacheLocked(loadJob.Hash, std::move(replacement),
                                              loadJob.Preload && !mDemanded.contains(loadJob.Hash));
                            mFailedLoadHashes.erase(loadJob.Hash);
                        } else if (!failure.empty() &&
                                   loadJob.Generation ==
                                       mGeneration) {
                            mFailedLoadHashes.insert(loadJob.Hash);
                            mLastError = std::move(failure);
                        }
                        mPendingLoadGenerations.erase(pending);
                    }
                    if (loadJob.Preload) {
                        --mActivePreloads;
                    }
                    if (mDemanded.erase(loadJob.Hash) != 0U && mPendingLoads > 0U) {
                        --mPendingLoads;
                    }
                    if (mPendingLoads == 0U) {
                        mIdle.notify_all();
                    }
                }
                continue;
            }

            bool success = false;
            std::string failure;
            try {
                WritePng(job.Destination, job.Width, job.Height, job.Rgba8);
                success = true;
            } catch (const std::exception& exception) { failure = exception.what(); }

            {
                std::scoped_lock lock(mMutex);
                if (success) {
                    ++mDumpedCount;
                } else {
                    mDumpedHashes.erase(job.Hash);
                    mLastError = std::move(failure);
                }
                if (mPendingDumps > 0U) {
                    --mPendingDumps;
                }
                if (mPendingDumps == 0U) {
                    mIdle.notify_all();
                }
            }
        }
    }

    mutable std::mutex mMutex;
    std::condition_variable mWorkAvailable;
    std::condition_variable mPreloadAvailable;
    std::condition_variable mIdle;
    std::vector<std::thread> mWorkers;
    bool mStopping = false;
    bool mConfigured = false;
    AzaharTexturePackConfiguration mConfiguration;
    uint64_t mGeneration = 1U;
    std::filesystem::path mLoadDirectory;
    std::filesystem::path mDumpDirectory;
    PackState mPack;
    bool mDumpUsesNewHash = true;
    std::unordered_map<uint64_t, IndexedTexture> mIndex;
    std::unordered_map<uint64_t, std::shared_ptr<const AzaharTextureReplacement>> mReplacementCache;
    mutable std::unordered_map<uint64_t, uint64_t> mCacheUse;  // hash -> last use (taken by the renderer)
    mutable uint64_t mUseClock = 0U;
    uint64_t mCacheBytes = 0U;
    std::unordered_map<uint64_t, uint64_t> mPendingLoadGenerations;
    std::unordered_set<uint64_t> mFailedLoadHashes;
    std::unordered_set<uint64_t> mDumpedHashes;
    std::deque<LoadJob> mLoadJobs;
    std::deque<LoadJob> mPreloadJobs;
    std::unordered_set<uint64_t> mDemanded;  // queued for a frame (mPendingLoads)
    size_t mActivePreloads = 0U;
    std::deque<DumpJob> mDumpJobs;
    size_t mPendingLoads = 0U;
    size_t mPendingDumps = 0U;
    size_t mDumpedCount = 0U;
    size_t mUnsupportedFiles = 0U;
    std::string mLastError;
};

AzaharTexturePackRuntime::AzaharTexturePackRuntime() : mImpl(std::make_unique<Impl>()) {
}

AzaharTexturePackRuntime::~AzaharTexturePackRuntime() = default;

AzaharTexturePackRuntime& AzaharTexturePackRuntime::Instance() {
    static AzaharTexturePackRuntime runtime;
    return runtime;
}

void AzaharTexturePackRuntime::Configure(AzaharTexturePackConfiguration configuration) {
    mImpl->Configure(std::move(configuration));
}

void AzaharTexturePackRuntime::Reload() {
    mImpl->Reload();
}

std::shared_ptr<const AzaharTextureReplacement>
AzaharTexturePackRuntime::ResolveAndMaybeDump(const AzaharTextureRequest& request) {
    return mImpl->ResolveAndMaybeDump(request);
}

AzaharTextureResolveResult
AzaharTexturePackRuntime::ResolveOrQueue(
    const AzaharTextureRequest& request) {
    return mImpl->ResolveOrQueue(request);
}

AzaharTextureResolveResult
AzaharTexturePackRuntime::PollQueued(
    uint64_t nativeHash, std::span<const uint8_t> nativeRgba8) const {
    return mImpl->PollQueued(nativeHash, nativeRgba8);
}

AzaharTexturePackStatus AzaharTexturePackRuntime::Snapshot() const {
    return mImpl->Snapshot();
}

uint64_t AzaharTexturePackRuntime::Generation() const {
    return mImpl->Generation();
}

void AzaharTexturePackRuntime::WaitForPendingDumps() {
    mImpl->WaitForPendingDumps();
}

void AzaharTexturePackRuntime::WaitForPendingLoads() {
    mImpl->WaitForPendingLoads();
}

bool AzaharTexturePackRuntime::WaitForPendingLoadsFor(std::chrono::milliseconds timeout) {
    return mImpl->WaitForPendingLoadsFor(timeout);
}

} // namespace Oot3d::Renderer
