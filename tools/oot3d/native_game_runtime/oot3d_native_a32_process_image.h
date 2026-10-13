#pragma once

#include "oot3d_native_a32_process.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct NativeA32ProcessImageSegment {
    std::string Name;
    uint32_t Address = 0;
    size_t MappedSize = 0;
    size_t DeclaredCodeSize = 0;
    size_t FileOffset = 0;
    size_t FileSize = 0;
    bool Writable = false;
    bool Executable = false;
};

struct NativeA32ProcessSystemRegion {
    std::string Name;
    uint32_t Address = 0;
    size_t MappedSize = 0;
    bool Writable = false;
    bool Executable = false;
    std::vector<uint8_t> InitialBytes;
};

// Optional per-title identity for titles other than the OoT3D baseline. When
// present the runtime takes the console region/language from it instead of
// title-specific RomFS probing.
struct NativeA32TitleProfile {
    bool Present = false;
    std::string Id;
    uint64_t ProgramId = 0;
    uint8_t Region = 2;
    uint8_t Language = 1;
    // A modified build of the title (e.g. "restoration": MM3D with Project
    // Restoration), whose code addresses differ.
    std::string Variant;
};

struct NativeA32ProcessImageManifest {
    std::filesystem::path ManifestPath;
    std::filesystem::path CodeBinPath;
    std::string CodeBinSha256;
    size_t CodeBinSize = 0;
    std::filesystem::path RomFsImagePath;
    uint64_t RomFsImageFileSize = 0;
    uint64_t RomFsImageOffset = 0;
    uint64_t RomFsImageSize = 0;
    std::string ProcessName;
    uint32_t EntryAddress = 0;
    uint32_t PageSize = 0;
    std::vector<NativeA32ProcessImageSegment> Segments;
    std::vector<NativeA32ProcessSystemRegion> SystemRegions;
    std::array<uint64_t, 10> ResourceLimitValues{};
    std::array<uint64_t, 10> ResourceCurrentValues{};
    uint32_t LinearHeapBaseAddress = 0;
    size_t LinearHeapSize = 0;
    uint32_t HeapBaseAddress = 0;
    size_t HeapSize = 0;
    NativeA32PrimaryThreadConfig PrimaryThread;
    NativeA32TitleProfile Title;
};

std::optional<NativeA32ProcessImageManifest>
LoadNativeA32ProcessImageManifest(const std::filesystem::path& path,
                                  std::string* error = nullptr);

std::optional<NativeA32ProcessImageManifest>
LoadNativeA32ProcessImageManifest(std::span<const uint8_t> encodedManifest,
                                  std::string* error = nullptr);

bool MountNativeA32ProcessImage(
    NativeA32Process& process, const NativeA32ProcessImageManifest& manifest,
    const std::filesystem::path& codeBinOverride = {},
    std::string* error = nullptr);

bool MountNativeA32ProcessImage(NativeA32Process& process,
                                const NativeA32ProcessImageManifest& manifest,
                                std::span<const uint8_t> codeBin,
                                std::string* error = nullptr);

} // namespace Oot3dNativeGame
