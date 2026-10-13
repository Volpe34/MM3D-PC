#include "fast/oot3d/azahar_texture_pack_panel.h"

#include "oot3d/renderer/azahar_texture_pack.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

namespace Fast::Oot3d {
namespace {

int ResizeStringInput(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) {
        return 0;
    }
    auto& value = *static_cast<std::string*>(data->UserData);
    value.resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = value.data();
    return 0;
}

bool DrawDirectoryInput(const char* label, std::string& value) {
    if (value.capacity() < 256U) {
        value.reserve(256U);
    }
    return ImGui::InputTextWithHint(label, "Azahar default", value.data(), value.capacity() + 1U,
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                                    ResizeStringInput, &value);
}

void SynchronizeDirectoryInputs(const AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    if (state.DirectoriesInitialized && state.BoundLoadDirectory == settings.LoadDirectory &&
        state.BoundDumpDirectory == settings.DumpDirectory) {
        return;
    }
    state.LoadDirectoryInput = settings.LoadDirectory;
    state.DumpDirectoryInput = settings.DumpDirectory;
    state.BoundLoadDirectory = settings.LoadDirectory;
    state.BoundDumpDirectory = settings.DumpDirectory;
    state.DirectoriesInitialized = true;
}

// Button prompt styles shipped with the game: one custom texture pack per
// folder of resources/button_styles (e.g. Omni UI's Xbox, DualShock).
const std::vector<std::filesystem::path>& ButtonStyles() {
    static const std::vector<std::filesystem::path> styles = [] {
        std::vector<std::filesystem::path> found;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("resources/button_styles", error)) {
            if (entry.is_directory(error)) {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }();
    return styles;
}

bool DrawButtonStyle(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    (void)state;
    const auto& styles = ButtonStyles();
    if (styles.empty()) {
        return false;
    }
    int current = settings.ButtonStyleDirectory.empty() ? 0 : -1;  // -1: another folder
    for (size_t index = 0; index < styles.size(); ++index) {
        if (settings.ButtonStyleDirectory == styles[index].string()) {
            current = static_cast<int>(index) + 1;
        }
    }
    const std::string preview = current == 0 ? "Original (3DS)"
                                : current > 0 ? styles[current - 1].filename().string()
                                              : settings.ButtonStyleDirectory;
    bool changed = false;
    ImGui::SeparatorText("Button prompts");
    if (ImGui::BeginCombo("Button style", preview.c_str())) {
        if (ImGui::Selectable("Original (3DS)", current == 0)) {
            settings.ButtonStyleDirectory.clear();
            changed = true;
        }
        for (size_t index = 0; index < styles.size(); ++index) {
            const std::string name = styles[index].filename().string();
            if (ImGui::Selectable(name.c_str(), current == static_cast<int>(index) + 1)) {
                settings.ButtonStyleDirectory = styles[index].string();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Omni UI by HardDanX (Project Restoration textures), on top of any texture pack.");
    return changed;
}

} // namespace

bool DrawAzaharTexturePackPanel(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    SynchronizeDirectoryInputs(settings, state);
    bool changed = DrawButtonStyle(settings, state);
    ImGui::SeparatorText("Custom textures");
    changed |= ImGui::Checkbox("Load custom textures", &settings.LoadCustomTextures);
    changed |= ImGui::Checkbox("Wait for custom textures (no pop-in)", &settings.WaitForCustomTextures);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A new area appears with its custom textures already loaded, after a short\n"
                          "pause, instead of showing the original textures for a moment.");
    }
    changed |= ImGui::Checkbox("Preload custom textures", &settings.PreloadCustomTextures);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Decodes the whole pack in the background (up to the texture memory below),\n"
                          "so areas appear with their custom textures without any wait.\n"
                          "A 4K pack needs several GB.");
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::SliderInt("Texture memory (MB)", &settings.TextureMemoryMB, 512, 16384, "%d MB",
                         ImGuiSliderFlags_AlwaysClamp)) {
        settings.TextureMemoryMB = settings.TextureMemoryMB / 256 * 256;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("RAM kept for decoded custom textures (areas visited before appear at once).\n"
                          "The least recently used ones are dropped beyond it. Ctrl+click to type a value.");
    }
    changed |= ImGui::Checkbox("Dump native textures", &settings.DumpTextures);

    ImGui::SeparatorText("Folders");
    bool applyDirectories = DrawDirectoryInput("Custom texture folder", state.LoadDirectoryInput);
    applyDirectories |= DrawDirectoryInput("Texture dump folder", state.DumpDirectoryInput);
    if (ImGui::Button("Apply folders")) {
        applyDirectories = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Azahar defaults")) {
        state.LoadDirectoryInput.clear();
        state.DumpDirectoryInput.clear();
        applyDirectories = true;
    }
    if (applyDirectories) {
        settings.LoadDirectory = state.LoadDirectoryInput;
        settings.DumpDirectory = state.DumpDirectoryInput;
        state.BoundLoadDirectory = settings.LoadDirectory;
        state.BoundDumpDirectory = settings.DumpDirectory;
        changed = true;
    }

    auto& texturePacks = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
    if (ImGui::Button("Reload custom textures")) {
        texturePacks.Reload();
        state.Status = "Custom texture index reloaded";
    }

    const auto status = texturePacks.Snapshot();
    ImGui::SeparatorText("Resolved paths");
    ImGui::TextWrapped("Load: %s", status.LoadDirectory.string().c_str());
    ImGui::TextWrapped("Dump: %s", status.DumpDirectory.string().c_str());
    ImGui::Text(
        "Indexed: %zu  Loaded: %zu  Loading: %zu  Failed: %zu",
        status.IndexedTextures, status.LoadedTextures,
        status.PendingLoads, status.FailedLoads);
    ImGui::Text("Memory: %llu MB  Preloading: %zu left",
                static_cast<unsigned long long>(status.CacheBytes >> 20U), status.PreloadsLeft);
    ImGui::Text("Dumped: %zu  Pending dumps: %zu",
                status.DumpedTextures, status.PendingDumps);
    if (status.UnsupportedTextureFiles != 0U) {
        ImGui::TextDisabled("DDS/KTX awaiting compressed GPU upload: %zu", status.UnsupportedTextureFiles);
    }
    if (!status.LastError.empty()) {
        ImGui::TextWrapped("Error: %s", status.LastError.c_str());
    } else if (!state.Status.empty()) {
        ImGui::TextDisabled("%s", state.Status.c_str());
    }
    return changed;
}

} // namespace Fast::Oot3d
