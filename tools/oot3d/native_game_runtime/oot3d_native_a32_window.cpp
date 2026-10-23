#include "oot3d_native_a32_window.h"
#include "three_ds_sdl_controller.h"
#include "oot3d_prebackend_budget.h"
#include "oot3d_cpu_phase_probe.h"
#include "fast/renderer/frame_time_distribution.h"
#include "fast/renderer/slow_frame_samples.h"
#include "oot3d/renderer/azahar_texture_pack.h"
#include "fast/renderer3ds/pica_program_preparation.h"
#include "oot3d_game_language_panel.h"
#include "mm3d_f1_panels.h"
#ifdef OOT3D_WHOLE_AOT_PRODUCT_MODE
#include "oot3d_native_crash_diagnostics.h"
#endif

#include "oot3d_a32_provenance.h"
#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_screenshot.h"
#include "oot3d_demo_host_window_timing.h"
#include "oot3d_legacy_ui_adapters.h"
#include "oot3d_n64_ui_renderer.h"
#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_a32_dsp_memory_adapter.h"
#include "oot3d_native_a32_process_image.h"
#include "oot3d_native_a32_registry.h"
#include "oot3d_native_a32_savestate.h"
#include "oot3d_native_a32_scene_view.h"
#include "oot3d_native_actor_interactions.h"
#include "fast/oot3d/render_resolution_policy.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "oot3d_native_a32_timing_probe.h"
#include "oot3d_native_compiled_functions.h"
#include "oot3d_native_controls_settings_panel.h"
#include "oot3d_native_frame_rate.h"
#include "oot3d_native_game_bootstrap.h"
#include "oot3d_native_mass_aot.h"
#include "oot3d_native_pcm_diagnostics.h"
#include "oot3d_native_presentation_pacer.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_composition.h"
#include "oot3d_native_pica_presentation_scheduler.h"
#include "triaevum_screen_layout.h"
#include "triaevum_crash_report.h"
#include "oot3d_native_pica_semantic_trace.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_visual_savestate.h"
#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "oot3d_native_player_temporal_bridge.h"
#include "oot3d_native_scenario_bootstrap.h"
#include "oot3d_source_actor_init_context_runtime.h"
#include "oot3d_source_actor_update_all_runtime.h"
#include "oot3d_source_camera_update_runtime.h"
#include "oot3d_source_csab_curve_runtime.h"
#include "oot3d_source_player_update_runtime.h"
#include "oot3d_source_player_update_common_runtime.h"
#include "oot3d_source_cutscene_process_commands_runtime.h"
#include "oot3d_source_cutscene_update_frame_runtime.h"
#include "oot3d_source_gameplay_profile.h"
#include "oot3d_native_temporal_event_ledger.h"
#include "oot3d_native_true_aot_blocks.h"
#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_native_ui_texture_provider.h"
#include "oot3d_top_screen_ocarina_text_runtime.h"
#include "oot3d_native_whole_aot_runtime.h"
#ifdef OOT3D_NATIVE_DIRECT_AOT_PLUGIN
#include "oot3d_native_direct_aot.h"
#endif
#include "oot3d_top_screen_camera_guest.h"
#include "oot3d_top_screen_controls.h"
#include "oot3d_top_screen_frontend_composition.h"
#include "oot3d_top_screen_gameplay_actions.h"
#include "oot3d_top_screen_gameplay_action_consumer.h"
#include "oot3d_top_screen_item_guest.h"
#include "oot3d_top_screen_item_dispatch.h"
#include "oot3d_top_screen_input_cadence.h"
#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_settings_panel.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_native_hd_font_runtime.h"
#include "oot3d_top_screen_texture_runtime.h"
#include "oot3d_typed_gameplay_bridge.h"

#include "fast/Fast3dWindow.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "mm3d_hud_buttons.h"
#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/perspective_fov_policy.h"
#include "fast/oot3d/presentation_pacing_policy.h"
#include "fast/oot3d/title_render_backend.h"
#include "ship/Context.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/controller/physicaldevice/GlobalSDLDeviceSettings.h"
#include "ship/window/gui/Gui.h"

#include <SDL2/SDL.h>
#include <imgui.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <sstream>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {
namespace CpuProbe = Oot3dNativeGame::CpuPhaseProbe;

#if defined(__SWITCH__)
size_t gSwitchBlockHookCount = 0U;
bool gSwitchBlockHooksSorted = false;
bool gSwitchBlockHooksContainProcessEntry = false;
uint64_t gSwitchRuntimeHostFrames = 0U;
uint32_t gSwitchRuntimeGuestFrames = 0U;
std::string gSwitchUiProfile = "unknown";
std::string gSwitchGameplayTiming = "unknown";
std::string gSwitchControlProfile = "unknown";
uint32_t gSwitchPresentationRateHz = 0U;
bool gSwitchVisualInterpolation = false;
bool gSwitchTopScreen211Assets = false;
uint32_t gSwitchWholeAotBlockBudget = 0U;
uint64_t gSwitchWholeAotBlockLimitExits = 0U;
bool gSwitchPicaGeometryCacheEnabled = false;
uint64_t gSwitchPicaGeometryPersistentDraws = 0U;
uint64_t gSwitchPicaGeometryPersistentUploads = 0U;
uint64_t gSwitchPicaGeometryRegistryEntries = 0U;
bool gSwitchPicaGeometryCacheObserved = false;

void WriteSwitchBootStage(const char* stage) {
  std::ofstream stream("sdmc:/switch/oot3dre/boot-status.txt",
                       std::ios::trunc);
  if (!stream) {
    return;
  }
  stream << "status=launching\n"
         << "stage=" << stage << '\n'
         << "whole_aot_functions=12419\n"
         << "host_boundaries=3\n"
         << "residual_a32=0\n"
         << "block_hook_count=" << gSwitchBlockHookCount << '\n'
         << "block_hooks_sorted=" << (gSwitchBlockHooksSorted ? 1 : 0)
         << '\n'
         << "block_hooks_contain_process_entry="
         << (gSwitchBlockHooksContainProcessEntry ? 1 : 0) << '\n'
         << "host_frames=" << gSwitchRuntimeHostFrames << '\n'
         << "guest_frames=" << gSwitchRuntimeGuestFrames << '\n'
         << "ui_profile=" << gSwitchUiProfile << '\n'
         << "gameplay_timing=" << gSwitchGameplayTiming << '\n'
         << "presentation_rate_hz=" << gSwitchPresentationRateHz << '\n'
         << "visual_interpolation=" << (gSwitchVisualInterpolation ? 1 : 0)
         << '\n'
         << "control_profile=" << gSwitchControlProfile << '\n'
         << "topscreen_211_assets=" << (gSwitchTopScreen211Assets ? 1 : 0)
         << '\n'
         << "whole_aot_block_budget=" << gSwitchWholeAotBlockBudget << '\n'
         << "whole_aot_block_limit_exits="
         << gSwitchWholeAotBlockLimitExits << '\n'
         << "pica_geometry_cache_enabled="
         << (gSwitchPicaGeometryCacheEnabled ? 1 : 0) << '\n'
         << "pica_geometry_persistent_draws="
         << gSwitchPicaGeometryPersistentDraws << '\n'
         << "pica_geometry_persistent_uploads="
         << gSwitchPicaGeometryPersistentUploads << '\n'
         << "pica_geometry_registry_entries="
         << gSwitchPicaGeometryRegistryEntries << '\n';
}

#endif

constexpr uint64_t kCtrArm11TicksPerSecond = 268111856ULL;
constexpr uint64_t kGuestDisplayRefreshRate =
    Oot3dNativeGame::kOot3dNativeTimeUnitsPerSecond;
constexpr uint32_t kMinimumHostAudioPrebufferMilliseconds = 100U;
constexpr uint64_t kA32RuntimeSampleDenominator = 64ULL;
constexpr uint64_t kVisualInterpolationRenderTargetNamespace = 1ULL;
constexpr size_t kMaximumPicaDrainPassesPerRefresh = 64U;
constexpr uint32_t kMtx4x4BuildFrustum = 0x002FDE9CU;
constexpr uint32_t kMtx4x4BuildOrthographicProjectionRotated = 0x00300AA8U;
constexpr uint32_t kEnMagInit = 0x0018CBB8U;
constexpr uint32_t kEnMagDraw = 0x001DA4F4U;
constexpr uint32_t kEnMagUpdate = 0x001DA9F8U;
constexpr uint32_t kTopScreenTitleLogoFadeHoldPrefix = 0x001DADCCU;
constexpr uint32_t kPauseUiUpdate = 0x0041E968U;
constexpr uint32_t kPauseUiDraw = 0x0041EC50U;
constexpr uint32_t kPauseUiDrawReturn = 0x00419830U;
constexpr uint32_t kPauseAlternateViewportDrawCall = 0x00419850U;
constexpr uint32_t kPauseAlternateViewportNativeDraw = 0x0041D1A8U;
constexpr uint32_t kPauseConditionalDrawCall = 0x00419874U;
constexpr uint32_t kPauseConditionalNativeDraw = 0x0041C500U;
constexpr uint32_t kPauseRoutedCommandCallA = 0x0041952CU;
constexpr uint32_t kPauseRoutedCommandCallB = 0x004198B0U;
constexpr uint32_t kPauseRoutedCommandNative = 0x00300240U;
constexpr uint32_t kPauseOverlayViewportDrawCall = 0x00419838U;
constexpr uint32_t kPauseOverlayNativeDraw = 0x0041F308U;
constexpr uint32_t kPauseAlternateRendererUpdateCall = 0x00300538U;
constexpr uint32_t kPauseAlternateRendererNativeUpdate = 0x0042A278U;
constexpr uint32_t kPauseSceneSevenDrawCall = 0x00300520U;
constexpr uint32_t kPauseSceneSevenNativeDraw = 0x004228E4U;
constexpr uint32_t kPauseSceneViewportDrawCall = 0x00300550U;
constexpr uint32_t kPauseSceneViewportNativeDraw = 0x0042CBCCU;
constexpr uint32_t kPauseTouchCoordinateUpdateCall = 0x0041E984U;
constexpr uint32_t kPauseTouchCoordinateNativeUpdate = 0x004289DCU;
constexpr uint32_t kPauseRendererVisibilityCall = 0x00300544U;
constexpr uint32_t kPauseRendererVisibilityNativeDraw = 0x00427A3CU;
constexpr uint32_t kPauseControllerDrawCall = 0x0041988CU;
constexpr uint32_t kPauseControllerNativeDraw = 0x0041AFACU;
constexpr uint32_t kPauseControllerRendererDraw = 0x002F1280U;
constexpr uint32_t kPauseInputSetTouchEnabled = 0x002FD84CU;
constexpr uint32_t kPauseSystemMenuOpen = 0x0043ACA8U;
constexpr uint32_t kTopScreenPauseState = 0x0050AF68U;
constexpr uint32_t kTopScreenPauseSceneControl = 0x005043E8U;
constexpr uint32_t kTopScreenDirectCallReturn = 0x60000000U;
constexpr uint32_t kAudioPlaySoundGeneral = 0x0037547CU;
constexpr uint32_t kTopScreenAimCycleSound = 0x0100048EU;
constexpr uint32_t kTopScreenAimCycleSoundPosition = 0x0054AC20U;
constexpr uint32_t kTopScreenAimCycleSoundScale = 0x0054AC24U;
constexpr uint32_t kTopScreenOcarinaEligibility = 0x002EFFF8U;
constexpr uint32_t kGlViewportEntry = 0x002FEABCU;
constexpr uint32_t kPauseIconBuild = 0x002E6CD4U;
constexpr uint32_t kPauseTouchButtonsDrawCall = 0x0042F5CCU;
constexpr uint32_t kPauseTouchButtonsTouchBranch = 0x0042EC2CU;
constexpr uint32_t kPauseTouchButtonsNoTouchExit = 0x0042F294U;
constexpr uint32_t kPauseLowerScreenBlock = 0x0041DDC8U;
constexpr uint32_t kPauseLowerScreenBlockEnd = 0x0041DEC8U;
constexpr uint32_t kPauseDungeonMapCursorInput = 0x00442F38U;
constexpr uint32_t kPauseUiNativeUpdateCall = 0x0041EAE0U;
constexpr uint32_t kPauseProjectionPrepareCall = 0x0041EB28U;
constexpr uint32_t kPauseProjectionPrepare = 0x002FBC50U;
constexpr uint32_t kPauseUiNativeUpdate = 0x0042DDA8U;
constexpr uint32_t kTopScreenCameraNormal1Scalar = 0x0023A930U;
constexpr uint32_t kTopScreenCameraNormal1DefaultPath = 0x0023A94CU;
constexpr uint32_t kTopScreenCameraNormal1AlternatePath = 0x0023A974U;
constexpr uint32_t kTopScreenCameraFovScalar = 0x00478EB8U;
constexpr uint32_t kTopScreenCameraUpdateEntry = 0x002D84C4U;
constexpr uint32_t kTopScreenCameraUpdatePatchSite = 0x002D84C8U;
constexpr uint32_t kTopScreenGameplayCompositionPrefix = 0x002E278CU;
constexpr uint32_t kTopScreenGameplayCompositionBranch = 0x002E27C0U;
constexpr uint32_t kFileSelectUpdate = 0x0042F9A8U;
constexpr uint32_t kFileSelectInit = 0x0046B114U;
constexpr uint32_t kGameStateUpdate =
    Oot3dNativeGame::kOot3dGameStateUpdateEntry;
constexpr uint32_t kFileChooseInit = 0x00450B60U;
constexpr uint32_t kPlayTransitionUpdate = 0x002E2E60U;
constexpr uint32_t kFileSelectActivate = 0x002E7AE0U;
constexpr uint32_t kFileSelectState = 0x00504FA0U;
constexpr uint32_t kNativeGameMode = 0x00588E3CU;
constexpr uint32_t kBlockingQueuePop = 0x002CE040U;
constexpr uint32_t kBlockingQueuePush = 0x002D2EF4U;
constexpr uint32_t kRendererCommandQueueProcessPending = 0x002DBCBCU;
constexpr uint32_t kBlockingQueueTryPush = 0x00490D60U;
constexpr uint32_t kTaskQueueTakeFirstFromPriority = 0x002D33CCU;
constexpr uint32_t kTaskQueueEnqueuePriorityNode = 0x0030AB9CU;
constexpr uint32_t kTaskQueueWorkerDrainPriorityNodes = 0x004667B8U;
constexpr uint32_t kNngxP3dCallback = 0x00415AE4U;
constexpr uint32_t kNngxCommonInterruptHandler = 0x003025C0U;
constexpr uint32_t kNngxGlobalState = 0x005A6F30U;
constexpr uint32_t kNngxCmdlistCompletionDispatch = 0x003FD420U;
constexpr uint32_t kNngxCmdlistOwnerState = 0x0054CC34U;
constexpr uint32_t kRendererGlobalCallbackAfterLookup = 0x0016CB78U;
constexpr uint32_t kRendererCommandQueue = 0x005B93A8U;
constexpr uint32_t kInputGlobalState = 0x0056559CU;
constexpr uint32_t kInputHeldOffset = 0x04U;
constexpr uint32_t kInputPressedOffset = 0x08U;
constexpr uint32_t kInputReleasedOffset = 0x0CU;
constexpr uint32_t kInputRawCurrentOffset = 0xC4U;
constexpr uint32_t kInputRawPreviousOffset = 0xC8U;
constexpr size_t kMaximumA32BlockTraceRecords = 65'536U;

uint64_t HashDiagnosticBytes(std::span<const uint8_t> bytes) noexcept {
  uint64_t hash = 14695981039346656037ULL;
  for (const uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool IsNativeFrontendOpaqueTargetInitializationDraw(
    const Oot3dNativeGame::Oot3dPicaDrawSubmission &draw) {
  const auto &state = draw.State;
  const auto &blend = state.OutputMerger.Blend;
  const auto &depth = state.OutputMerger.Depth;
  const bool hasTexture =
      std::any_of(state.Textures.begin(), state.Textures.end(),
                  [](const auto &texture) { return texture.Enabled; });
  // Native UI targets include rotated/padded storage outside the logical
  // viewport, so framebuffer dimensions cannot establish whether this copy
  // quad initializes the frontend canvas. The replacement blend/depth state
  // identifies that operation; its association with CommonBackground00's
  // target AND viewport is checked by the caller, preserving the scene clear.
  return !hasTexture && state.VertexInput.Indexed &&
         state.VertexInput.VertexCount == 4U &&
         state.OutputMerger.ColorWriteMask == 0x0FU &&
         state.OutputMerger.LogicOperation ==
             Oot3dNativeGame::Oot3dPicaLogicOperation::Copy &&
         blend.Enabled &&
         blend.SourceColor == Oot3dNativeGame::Oot3dPicaBlendFactor::One &&
         blend.DestinationColor ==
             Oot3dNativeGame::Oot3dPicaBlendFactor::Zero &&
         blend.SourceAlpha == Oot3dNativeGame::Oot3dPicaBlendFactor::One &&
         blend.DestinationAlpha ==
             Oot3dNativeGame::Oot3dPicaBlendFactor::Zero &&
         depth.TestEnabled && depth.WriteEnabled &&
         depth.Compare == Oot3dNativeGame::Oot3dPicaCompareFunction::Always;
}

struct NativeSceneProjectionState {
  struct CallsiteStats {
    uint64_t Calls = 0;
    float FirstLeft = 0.0F;
    float FirstRight = 0.0F;
  };

  Fast::Oot3d::ScenePresentationPolicy Presentation{};
  float FovMultiplier = 1.0F;
  uint64_t FrustumCalls = 0;
  uint64_t OrthographicCalls = 0;
  std::map<std::pair<uint32_t, uint32_t>, CallsiteStats> Callsites;
};

struct NativeWidescreenProjectionState {
  struct A32BlockTraceRecord {
    uint32_t HostFrame = 0;
    uint32_t Pc = 0;
    uint64_t MemoryWriteGeneration = 0;
    uint64_t MemoryWriteFingerprint = 0;
    uint64_t FastReadMismatchCount = 0;
    uint32_t LastFastReadMismatchAddress = 0;
    uint64_t LastFastReadMismatchValue = 0;
    uint64_t LastCheckedReadMismatchValue = 0;
    std::array<uint32_t, 16> Registers{};
    uint32_t Cpsr = 0;
    uint32_t Fpscr = 0;
    uint32_t ThreadPointer = 0;
    std::array<uint32_t, 32> Vfp{};
    uint32_t ExclusiveAddress = 0;
    uint64_t ExclusiveToken = 0;
    uint8_t ExclusiveSize = 0;
    bool ExclusiveValid = false;
  };

  NativeSceneProjectionState Projection;
  // The title's projection builders (OoT3D addresses unless a manifest title
  // supplies its own). FrustumAspect, when set, limits widening to frustums
  // of that shape (the upper screen's).
  uint32_t FrustumPc = 0x002FDE9CU;
  uint32_t OrthographicPc = 0x00300AA8U;
  float FrustumAspect = 0.0F;
  // MM3D: the texture setup function, whose 512x256 RGB565 textures are the
  // pause background the game captures from the upper framebuffer (physical
  // addresses collected here).
  uint32_t PauseTextureSetupPc = 0;
  std::vector<uint32_t> PauseTextures;
  // The game's capture of the upper framebuffer for that background (its
  // HUD has faded out by then): the presented frame is kept at that point.
  uint32_t PauseCapturePc = 0;
  bool PauseCaptured = false;
  // The capture's driver (each frame; r0 = the capture object): it sets
  // +0x1D to 1 one frame before converting the RGB8 top framebuffers at
  // *(+0xC)/*(+0x10) (Pictograph Box photo, pause background). Those are
  // display transfer outputs the GPU path never writes to guest memory.
  uint32_t CaptureDriverPc = 0;
  uint32_t CaptureObject = 0;
  // MM3D: the play state's calc function (after the pad is read, before the
  // camera's update) and the F1 camera speed applied to the right stick the
  // camera reads.
  uint32_t PlayCalcPc = 0;
  float CameraStickSpeed = 1.0F;
  // MM3D: F1 > Cheats, applied there too (once per game frame).
  Oot3dNativeGame::Mm3dCheats *Cheats = nullptr;
  // MM3D: the layout draw manager's per-pass function (manager in r0, after
  // mm3d-dekui): the names of the layouts queued for the lower screen.
  uint32_t LayoutPassPc = 0;
  std::vector<std::string> LowerLayoutNames;
  bool CollectExtendedDiagnostics = false;
  uint32_t CurrentHostFrame = 0;
  uint32_t EnMagInitCalls = 0;
  uint32_t EnMagUpdateCalls = 0;
  uint32_t EnMagDrawCalls = 0;
  nlohmann::json EnMagEvents = nlohmann::json::array();
  std::map<std::string, uint64_t> UiLifecycleCallCounts;
  nlohmann::json UiLifecycleEvents = nlohmann::json::array();
  nlohmann::json GameStateEvents = nlohmann::json::array();
  uint64_t GameStateUpdateCalls = 0;
  uint32_t LastGameStateAddress = 0;
  uint32_t LastGameStateMain = 0;
  uint32_t LastNextGameStateInit = 0;
  uint32_t LastNativeGameMode = 0;
  uint8_t LastGameStateRunning = 0;
  uint8_t LastTransitionTrigger = 0;
  uint8_t LastTransitionType = 0;
  uint8_t LastTransitionState = 0;
  bool HasGameStateSnapshot = false;
  nlohmann::json RendererQueueEvents = nlohmann::json::array();
  std::map<std::string, uint64_t> RendererQueueCallCounts;
  bool ProfileA32Blocks = false;
  uint64_t BlockEntries = 0;
  uint32_t BlockSampleState = 0x6D2B79F5U;
  std::unordered_map<uint32_t, uint64_t> BlockSamples;
  bool TraceA32Blocks = false;
  bool BlockTraceTruncated = false;
  uint64_t BlockTraceRecordsObserved = 0;
  Oot3dNativeGame::NativeA32Memory *TraceMemory = nullptr;
  Oot3dNativeGame::NativeHdFontRuntime *HdFonts = nullptr;
  std::deque<A32BlockTraceRecord> BlockTrace;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::TopScreenOcarinaTextRuntime *OcarinaText = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker
      *PicaCompositionTracker = nullptr;
  Oot3dNativeGame::TopScreenTextureOverrideRuntime *TopScreenTextureOverrides =
      nullptr;
  bool TopScreenUiProfile = false;
  Oot3dNativeGame::TopScreenUiConfig TopScreenConfig;
  Oot3dNativeGame::TopScreenExtendedInputFrame *TopScreenInput = nullptr;
  Oot3dNativeGame::TopScreenInputCadence *TopScreenInputClock = nullptr;
  const Oot3dNativeGame::TopScreenItemDispatchRuntime *TopScreenItems = nullptr;
  std::span<const uint32_t> WholeAotObservableExitPcs;
  std::optional<Oot3dNativeGame::TopScreenPauseTargetPlan>
      PendingTopScreenPauseTarget;
  uint32_t PendingTopScreenPauseRenderer = 0U;
  uint64_t TopScreenPauseTargetObservations = 0U;
  uint64_t TopScreenPauseTargetCommands = 0U;
  uint64_t TopScreenPauseTargetRewrites = 0U;
  uint64_t TopScreenPauseChildSuppressions = 0U;
  uint64_t TopScreenPauseChildNativeDraws = 0U;
  bool HasTopScreenPauseDrawInputs = false;
  bool LastTopScreenPauseRouteActive = false;
  bool LastTopScreenPauseChildrenSuppressed = false;
  Oot3dNativeGame::TopScreenPauseDrawInputs LastTopScreenPauseDrawInputs{};
  Oot3dNativeGame::TopScreenPausePageRedrawState TopScreenPausePageRedraw;
  Oot3dNativeGame::TopScreenTouchCoordinateRouteState
      *TopScreenTouchCoordinateRoute = nullptr;
  bool TopScreenPausePageRedrawActive = false;
  uint64_t TopScreenPausePageCompositionCalls = 0U;
  uint64_t TopScreenPausePageRedraws = 0U;
  uint64_t TopScreenPausePageRedrawSkips = 0U;
  uint64_t TopScreenPauseProjectionCalls = 0U;
  uint64_t TopScreenPauseProjectionFailures = 0U;
  nlohmann::json TopScreenPauseProjectionEvents = nlohmann::json::array();
  Oot3dNativeGame::TopScreenPauseRouteState *TopScreenPauseTargetRoute =
      nullptr;
  Oot3dNativeGame::TopScreenPauseDrawRoutingState TopScreenPauseDrawRouting;
  std::optional<Oot3dNativeGame::TopScreenPauseChildState>
      PendingTopScreenPauseChildRestore;
  Oot3dNativeGame::NativeFrameRatePolicy *FrameRatePolicy = nullptr;
  Oot3dNativeGame::NativeA32SceneViewProbe *SceneViewProbe = nullptr;
  // MM3D: read the Play camera in the frustum hook (ReadMm3dPlayCamera).
  bool Mm3dPlayCamera = false;
  Oot3dNativeGame::NativeA32PlayerTimingProbe *PlayerTimingProbe = nullptr;
  Oot3dNativeGame::NativeTemporalEventLedger *TemporalEventLedger = nullptr;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge *PlayerTemporalBridge =
      nullptr;
  nlohmann::json PlayerTimingEvents = nlohmann::json::array();
  bool PlayerTimingEventsTruncated = false;
};

void WriteA32BlockTrace(const std::filesystem::path &path,
                        const NativeWidescreenProjectionState &runtime) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot open A32 block trace: " + path.string());
  }
  stream << nlohmann::json({
                               {"format", "oot3d_a32_block_trace_v2"},
                               {"records", runtime.BlockTrace.size()},
                               {"records_observed",
                                runtime.BlockTraceRecordsObserved},
                               {"truncated", runtime.BlockTraceTruncated},
                           })
                .dump()
         << '\n';
  for (const auto &record : runtime.BlockTrace) {
    stream
        << nlohmann::json(
               {
                   {"host_frame", record.HostFrame},
                   {"pc", record.Pc},
                   {"memory_write_generation", record.MemoryWriteGeneration},
                   {"memory_write_fingerprint", record.MemoryWriteFingerprint},
                   {"fast_read_mismatch_count", record.FastReadMismatchCount},
                   {"last_fast_read_mismatch_address",
                    record.LastFastReadMismatchAddress},
                   {"last_fast_read_mismatch_value",
                    record.LastFastReadMismatchValue},
                   {"last_checked_read_mismatch_value",
                    record.LastCheckedReadMismatchValue},
                   {"r", record.Registers},
                   {"cpsr", record.Cpsr},
                   {"fpscr", record.Fpscr},
                   {"thread_pointer", record.ThreadPointer},
                   {"vfp", record.Vfp},
                   {"exclusive_address", record.ExclusiveAddress},
                   {"exclusive_token", record.ExclusiveToken},
                   {"exclusive_size", record.ExclusiveSize},
                   {"exclusive_valid", record.ExclusiveValid},
               })
               .dump()
        << '\n';
  }
  if (!stream) {
    throw std::runtime_error("cannot write A32 block trace: " + path.string());
  }
}

class ScopedA32BlockTraceWriter final {
 public:
  ScopedA32BlockTraceWriter(
      const std::filesystem::path &path,
      const NativeWidescreenProjectionState &runtime)
      : Path(path), Runtime(runtime) {}

  ~ScopedA32BlockTraceWriter() {
    if (Written || Path.empty()) {
      return;
    }
    try {
      Write();
    } catch (const std::exception &exception) {
      std::cerr << "oot3d_native_game: failed to preserve A32 fault trace: "
                << exception.what() << '\n';
    }
  }

  void Write() {
    if (Written || Path.empty()) {
      return;
    }
    WriteA32BlockTrace(Path, Runtime);
    Written = true;
  }

 private:
  std::filesystem::path Path;
  const NativeWidescreenProjectionState &Runtime;
  bool Written = false;
};

double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

struct NativeFramePhaseTiming {
  double GuestSeconds = 0.0;
  double GuestInsidePicaSubmitSeconds = 0.0;
  double GuestClockSeconds = 0.0;
  double FrameStartSeconds = 0.0;
  double HostFrameStartSeconds = 0.0;
  double InputPollSeconds = 0.0;
  double RendererFrameStartSeconds = 0.0;
  double DspMixSeconds = 0.0;
  double AudioOutputSeconds = 0.0;
  double PicaSubmitSeconds = 0.0;
  double PicaPlanSeconds = 0.0;
  double PicaBackendSeconds = 0.0;
  double PicaDiagnosticsSeconds = 0.0;
  double VisualPresentationSeconds = 0.0;
  double VisualReplaySeconds = 0.0;
  double PresentSeconds = 0.0;
  double PacingWaitSeconds = 0.0;
  std::array<double, 15> Values() const {
    return {GuestSeconds, FrameStartSeconds, HostFrameStartSeconds, InputPollSeconds,
      RendererFrameStartSeconds, DspMixSeconds, AudioOutputSeconds, PicaSubmitSeconds,
      PicaPlanSeconds, PicaBackendSeconds, PicaDiagnosticsSeconds,
      VisualPresentationSeconds, VisualReplaySeconds, PresentSeconds, PacingWaitSeconds};
  }
};

struct NativeAudioOutputDiagnostics {
  void Observe(uint32_t hostFrame, int32_t before, int32_t after,
               uint64_t requestedFrames, int32_t desiredBuffered) {
    const auto now = std::chrono::steady_clock::now();
    if (HasSubmitTime) {
      const double gapSeconds =
          std::chrono::duration<double>(now - LastSubmitTime).count();
      MaximumSubmitGapSeconds = std::max(MaximumSubmitGapSeconds, gapSeconds);
      if (gapSeconds >= 0.05 && SubmitGapEvents.size() < 64U) {
        SubmitGapEvents.push_back({
            {"host_frame", hostFrame},
            {"gap_seconds", gapSeconds},
            {"buffered_before", before},
        });
      }
    }
    LastSubmitTime = now;
    HasSubmitTime = true;
    ++SubmitCalls;
    RequestedFrames += requestedFrames;
    if (!HasBufferObservation) {
      MinimumBufferedBefore = before;
      MaximumBufferedBefore = before;
      MinimumBufferedAfter = after;
      MaximumBufferedAfter = after;
      HasBufferObservation = true;
    } else {
      MinimumBufferedBefore = std::min(MinimumBufferedBefore, before);
      MaximumBufferedBefore = std::max(MaximumBufferedBefore, before);
      MinimumBufferedAfter = std::min(MinimumBufferedAfter, after);
      MaximumBufferedAfter = std::max(MaximumBufferedAfter, after);
    }
    const bool wasPrimed = Primed;
    if (after >= desiredBuffered) {
      Primed = true;
    }
    if (wasPrimed && before == 0) {
      ++StarvationObservations;
      StarvationEvents.push_back({
          {"host_frame", hostFrame},
          {"buffered_before", before},
          {"buffered_after", after},
          {"requested_frames", requestedFrames},
      });
    }
    const uint64_t growth =
        after > before ? static_cast<uint64_t>(after - before) : 0U;
    if (growth < requestedFrames) {
      EstimatedShortfallFrames += requestedFrames - growth;
      ++ShortGrowthSubmissions;
    }
  }

  bool HasBufferObservation = false;
  bool HasSubmitTime = false;
  bool Primed = false;
  std::chrono::steady_clock::time_point LastSubmitTime{};
  double MaximumSubmitGapSeconds = 0.0;
  uint64_t SubmitCalls = 0;
  uint64_t RequestedFrames = 0;
  uint64_t ShortGrowthSubmissions = 0;
  uint64_t EstimatedShortfallFrames = 0;
  uint64_t StarvationObservations = 0;
  int32_t MinimumBufferedBefore = 0;
  int32_t MaximumBufferedBefore = 0;
  int32_t MinimumBufferedAfter = 0;
  int32_t MaximumBufferedAfter = 0;
  nlohmann::json StarvationEvents = nlohmann::json::array();
  nlohmann::json SubmitGapEvents = nlohmann::json::array();
};

struct NativeInputConsumerDiagnostics {
  uint64_t Samples = 0;
  uint64_t ReadFailures = 0;
  uint64_t HeldFrames = 0;
  uint64_t PressedFrames = 0;
  uint64_t ReleasedFrames = 0;
  uint32_t LastHeld = 0;
  uint32_t LastPressed = 0;
  uint32_t LastReleased = 0;
  uint32_t LastRawCurrent = 0;
  uint32_t LastRawPrevious = 0;
  bool HasPrevious = false;
  nlohmann::json Transitions = nlohmann::json::array();

  void Observe(Oot3dNativeGame::NativeA32Memory &memory, uint32_t hostFrame) {
    uint32_t held = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
    uint32_t rawCurrent = 0;
    uint32_t rawPrevious = 0;
    if (!memory.Read32(kInputGlobalState + kInputHeldOffset, &held) ||
        !memory.Read32(kInputGlobalState + kInputPressedOffset, &pressed) ||
        !memory.Read32(kInputGlobalState + kInputReleasedOffset, &released) ||
        !memory.Read32(kInputGlobalState + kInputRawCurrentOffset,
                       &rawCurrent) ||
        !memory.Read32(kInputGlobalState + kInputRawPreviousOffset,
                       &rawPrevious)) {
      ++ReadFailures;
      return;
    }
    ++Samples;
    HeldFrames += held != 0U ? 1U : 0U;
    PressedFrames += pressed != 0U ? 1U : 0U;
    ReleasedFrames += released != 0U ? 1U : 0U;
    const bool changed = !HasPrevious || held != LastHeld ||
                         pressed != LastPressed || released != LastReleased ||
                         rawCurrent != LastRawCurrent ||
                         rawPrevious != LastRawPrevious;
    if (changed && Transitions.size() < 64U) {
      Transitions.push_back({
          {"host_frame", hostFrame},
          {"held", held},
          {"pressed", pressed},
          {"released", released},
          {"raw_current", rawCurrent},
          {"raw_previous", rawPrevious},
      });
    }
    LastHeld = held;
    LastPressed = pressed;
    LastReleased = released;
    LastRawCurrent = rawCurrent;
    LastRawPrevious = rawPrevious;
    HasPrevious = true;
  }
};

struct NativeCandidateDispatchState {
  oot3d::recomp::a32::BlockEntryCallback BlockEntry = nullptr;
  void *BlockEntryUser = nullptr;
  std::span<const uint32_t> BlockEntryPcs;
  Oot3dNativeGame::Oot3dMassAotPcFilter MassAotBlockEntryFilter;
  Oot3dNativeGame::Oot3dMassAotPcFilter MassAotObservableExitFilter;
  Oot3dNativeGame::NativeA32Memory *Memory = nullptr;
  Oot3dNativeGame::Oot3dTypedGameplayContext TypedGameplayContext;
  bool TypedGameplay = false;
  bool MassAot = false;
  bool CompiledFunctions = false;
  bool WholeAot = false;
  bool ManualCompiledFunctions = true;
  bool TrueAotBlocks = false;
  bool ProfileRuntime = false;
  bool TopScreenUiProfile = false;
  bool TopScreenPauseAotBarrier = false;
  bool TopScreenExtendedCameraActive = false;
  Oot3dNativeGame::TopScreenFreeCameraGuestRuntime TopScreenCamera;
  Oot3dNativeGame::TopScreenCameraFovOwnership TopScreenCameraFov;
  Oot3dNativeGame::NativeFreeCameraInputAccumulator TopScreenCameraInput;
  Oot3dNativeGame::TopScreenExtendedInputFrame TopScreenInput;
  Oot3dNativeGame::TopScreenInputCadence TopScreenInputClock;
  uint32_t PreviousTopScreenButtons = 0U;
  Oot3dNativeGame::NativeA32PolledButtonLatch StartButtonLatch;
  Oot3dNativeGame::TopScreenStartRoutingState TopScreenStartRouting;
  Oot3dNativeGame::TopScreenPauseSystemOpenState TopScreenPauseSystemOpen;
  uint64_t TopScreenPauseStartCloseCalls = 0U;
  uint64_t TopScreenPauseSystemOpenCalls = 0U;
  uint64_t TopScreenAimProjectileCycleAttempts = 0U;
  uint64_t TopScreenAimProjectileCycleUpdates = 0U;
  uint64_t TopScreenAimProjectileCycleSoundCalls = 0U;
  uint64_t TopScreenGameplayDpadAttempts = 0U;
  uint64_t TopScreenNaviViewActivations = 0U;
  uint64_t TopScreenOcarinaQueries = 0U;
  uint64_t TopScreenOcarinaActivations = 0U;
  Oot3dNativeGame::TopScreenGameplayActionConsumerStats
      TopScreenGameplayActions;
  Oot3dNativeGame::TopScreenGameplayActionConsumerRuntime
      TopScreenGameplayActionRuntime;
  Oot3dNativeGame::TopScreenPauseClosePage TopScreenPauseLastClosedPage =
      Oot3dNativeGame::TopScreenPauseClosePage::None;
  Oot3dNativeGame::TopScreenPauseClosePage TopScreenPauseSystemSourcePage =
      Oot3dNativeGame::TopScreenPauseClosePage::None;
  uint8_t TopScreenViewportDrawPhase = 0U;
  uint32_t TopScreenViewportDrawArgument = 0U;
  uint8_t TopScreenSceneViewportDrawPhase = 0U;
  uint32_t TopScreenSceneViewportDrawArgument = 0U;
  uint8_t TopScreenOverlayViewportDrawPhase = 0U;
  uint8_t TopScreenTouchCoordinateUpdatePhase = 0U;
  std::optional<Oot3dNativeGame::TopScreenAlternateRendererState>
      TopScreenAlternateRendererRestore;
  Oot3dNativeGame::TopScreenPauseRouteState TopScreenPauseRoute;
  Oot3dNativeGame::TopScreenPauseRouteState TopScreenAotPauseRoute;
  Oot3dNativeGame::TopScreenTouchCoordinateRouteState
      TopScreenTouchCoordinateRoute;
  Oot3dNativeGame::TopScreenRendererVisibilityRouteState
      TopScreenRendererVisibilityRoute;
  uint32_t TopScreenGameOverDrawPhase = 0U;
  std::array<uint32_t, 5> TopScreenGameOverDrawRegisters{};
  Oot3dNativeGame::TopScreenPauseChildState TopScreenGameOverChildRestore{};
  Oot3dNativeGame::TopScreenPauseControllerState TopScreenPauseController;
  uint8_t TopScreenPauseControllerDrawPhase = 0U;
  std::array<uint32_t, 2> TopScreenPauseControllerVisibleRenderers{};
  Oot3dNativeGame::TopScreenItemDispatchRuntime TopScreenItems;
  uint32_t SampleState = 0xA32C0DE1U;
  uint64_t Calls = 0;
  uint64_t Samples = 0;
  uint64_t SourceActorInitContextSamples = 0;
  uint64_t SourceActorInitContextSampleNanoseconds = 0;
  uint64_t SourceActorUpdateAllSamples = 0;
  uint64_t SourceActorUpdateAllSampleNanoseconds = 0;
  uint64_t SourceCutsceneUpdateFrameSamples = 0;
  uint64_t SourceCutsceneUpdateFrameSampleNanoseconds = 0;
  uint64_t SourceCutsceneProcessCommandsSamples = 0;
  uint64_t SourceCutsceneProcessCommandsSampleNanoseconds = 0;
  uint64_t SourceCameraUpdateSamples = 0;
  uint64_t SourceCameraUpdateSampleNanoseconds = 0;
  uint64_t SourcePlayerUpdateSamples = 0;
  uint64_t SourcePlayerUpdateSampleNanoseconds = 0;
  uint64_t SourcePlayerUpdateCommonSamples = 0;
  uint64_t SourcePlayerUpdateCommonSampleNanoseconds = 0;
  uint64_t SourceCsabCurveSamples = 0;
  uint64_t SourceCsabCurveSampleNanoseconds = 0;
  uint64_t TypedGameplaySamples = 0;
  uint64_t TypedGameplaySampleNanoseconds = 0;
  uint64_t CompiledSamples = 0;
  uint64_t CompiledSampleNanoseconds = 0;
  uint64_t TrueAotSamples = 0;
  uint64_t TrueAotSampleNanoseconds = 0;
  uint64_t UnhandledSamples = 0;
  uint64_t UnhandledSampleNanoseconds = 0;
  uint64_t TopScreenSourcePortCalls = 0;
  uint64_t TopScreenTitleLogoFadeHoldCalls = 0;
  uint64_t TopScreenLowerCompositionSkips = 0;
  uint64_t TopScreenCameraNormal1Calls = 0;
  uint64_t TopScreenCameraUpdateCalls = 0;
  uint64_t TopScreenCameraActiveCalls = 0;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::TopScreenOcarinaTextRuntime *OcarinaText = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::TopScreenPauseProjectionState *TopScreenPauseProjection =
      nullptr;
  const Oot3dNativeGame::TopScreenUiConfig *TopScreenConfig = nullptr;
  Oot3dNativeGame::SourceActorInitContextRuntime *SourceActorInitContext =
      nullptr;
  Oot3dNativeGame::SourceActorUpdateAllRuntime *SourceActorUpdateAll =
      nullptr;
  Oot3dNativeGame::SourceCutsceneUpdateFrameRuntime
      *SourceCutsceneUpdateFrame = nullptr;
  Oot3dNativeGame::SourceCutsceneProcessCommandsRuntime
      *SourceCutsceneProcessCommands = nullptr;
  Oot3dNativeGame::SourceCameraUpdateRuntime *SourceCameraUpdate = nullptr;
  Oot3dNativeGame::SourcePlayerUpdateRuntime *SourcePlayerUpdate = nullptr;
  Oot3dNativeGame::SourcePlayerUpdateCommonRuntime
      *SourcePlayerUpdateCommon = nullptr;
  Oot3dNativeGame::SourceCsabCurveRuntime *SourceCsabCurve = nullptr;
};

// MM3D layout draw queue (16 priority buckets of up to 32 layouts: entries
// at +4 + bucket*0x80, counts at +0x804): the queued layouts' resource names
// (layout +4 -> resource, +0x18 -> name), as mm3d-dekui reads them.
std::vector<std::string> ReadMm3dLayoutQueueNames(Oot3dNativeGame::NativeA32Memory &memory,
                                                  uint32_t manager, uint32_t queueOffset) {
  std::vector<std::string> names;
  uint32_t queue = 0;
  if (manager < 0x100000U || !memory.Read32(manager + queueOffset, &queue) || queue < 0x100000U) {
    return names;
  }
  for (uint32_t bucket = 0; bucket < 16U; ++bucket) {
    uint32_t count = 0;
    if (!memory.Read32(queue + 0x804U + bucket * 4U, &count) || count > 32U) return {};
    for (uint32_t index = 0; index < count; ++index) {
      uint32_t layout = 0, resource = 0, name = 0;
      if (!memory.Read32(queue + 4U + bucket * 0x80U + index * 4U, &layout) ||
          !memory.Read32(layout + 4U, &resource) || !memory.Read32(resource + 0x18U, &name)) {
        continue;
      }
      std::string text;
      for (uint32_t offset = 0; offset < 48U; ++offset) {
        uint8_t byte = 0;
        if (!memory.Read8(name + offset, &byte) || byte == 0U) break;
        text.push_back(static_cast<char>(byte));
      }
      if (!text.empty() && std::find(names.begin(), names.end(), text) == names.end()) {
        names.push_back(std::move(text));
      }
    }
  }
  return names;
}

bool ExecuteTopScreenUiSourcePortBlock(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeCandidateDispatchState &dispatch,
    oot3d::recomp::a32::ExecutionResult *result, uint32_t *blocksConsumed) {
  auto &memory = *dispatch.Memory;
  if (pc == Oot3dNativeGame::kTopScreenInputUpdateBoundary && dispatch.OcarinaText != nullptr) {
    return dispatch.OcarinaText->Execute(state, result, blocksConsumed);
  }
  uint32_t nextPc = 0U;
  auto branchFromCallsite = [&](uint32_t target, uint32_t returnAddress) {
    state.r[14] = returnAddress;
    state.r[15] = target;
    *result = {oot3d::recomp::a32::ExitKind::Branch, target,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  };
  if (pc == kTopScreenTitleLogoFadeHoldPrefix) {
    Oot3dNativeGame::TopScreenTitleLogoFadeHoldResult hold;
    if (!Oot3dNativeGame::ApplyTopScreenTitleLogoFadeHold(
            memory, state.r[4], state.r[5], state.vfp[16], &hold)) {
      return false;
    }
    ++dispatch.TopScreenTitleLogoFadeHoldCalls;
    state.r[0] = hold.AnimationOwner;
    state.r[1] = hold.Argument1;
    state.r[15] = hold.NextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, hold.NextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseConditionalDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
            inputs, &dispatch.TopScreenPauseRoute)) {
      return branchFromCallsite(kPauseConditionalNativeDraw, pc + 4U);
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseTouchCoordinateUpdateCall) {
    if (dispatch.TopScreenTouchCoordinateUpdatePhase == 0U) {
      dispatch.TopScreenTouchCoordinateUpdatePhase = 1U;
      return branchFromCallsite(kPauseTouchCoordinateNativeUpdate, pc);
    }
    dispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    std::uint32_t pauseCameraControl = 0U;
    std::uint32_t secondaryLayer = 0U;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs) ||
        !memory.Read32(0x00506CE8U, &pauseCameraControl) ||
        !memory.Read32(0x004FDABCU, &secondaryLayer)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    if (Oot3dNativeGame::ResolveTopScreenTouchCoordinateSuppression(
            inputs, routeActive, pauseCameraControl != 0U, secondaryLayer == 1U,
            &dispatch.TopScreenTouchCoordinateRoute) &&
        !Oot3dNativeGame::ApplyTopScreenTouchCoordinateSuppression(memory)) {
      return false;
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseRendererVisibilityCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs globalInputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &globalInputs)) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityInputs inputs;
    inputs.ControllerAddress = state.r[0];
    inputs.HasCallScene = state.r[1] != 0U;
    inputs.GlobalSceneRuntimeRoute =
        globalInputs.HasScene && globalInputs.SceneMode == 3U &&
        (globalInputs.RuntimeMode == 1U || globalInputs.RuntimeMode == 2U);
    inputs.RuntimeSceneLatch =
        dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch;
    inputs.NativeRuntimeActive = globalInputs.NativeTransitionActive;
    std::uint16_t nativeFadeGate = 0U;
    if (!memory.Read16(0x0058799CU, &nativeFadeGate))
      return false;
    inputs.NativeFadeGate = static_cast<std::int16_t>(nativeFadeGate);
    if (inputs.HasCallScene &&
        (!memory.Read8(state.r[1] + 0x100U, &inputs.CallSceneMode) ||
         !memory.Read32(state.r[1] + 0x104U, &inputs.CallSceneSubmode) ||
         !memory.Read16(state.r[1] + 0x318CU, &inputs.CallSceneSequence))) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityAction action;
    if (!Oot3dNativeGame::ResolveTopScreenRendererVisibilityRoute(
            memory, inputs, &dispatch.TopScreenRendererVisibilityRoute,
            &action)) {
      return false;
    }
    return branchFromCallsite(
        action == Oot3dNativeGame::TopScreenRendererVisibilityAction::NativeDraw
            ? kPauseRendererVisibilityNativeDraw
            : pc + 4U,
        pc + 4U);
  }
  if (pc == kPauseControllerDrawCall) {
    if (dispatch.TopScreenPauseControllerDrawPhase == 1U) {
      if (!Oot3dNativeGame::PrepareTopScreenPauseAlternateRenderers(
              memory, &dispatch.TopScreenPauseController,
              &dispatch.TopScreenPauseControllerVisibleRenderers)) {
        return false;
      }
      dispatch.TopScreenPauseControllerDrawPhase = 2U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 2U) {
      dispatch.TopScreenPauseControllerDrawPhase = 3U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[0];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 3U) {
      dispatch.TopScreenPauseControllerDrawPhase = 4U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[1];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 4U) {
      dispatch.TopScreenPauseControllerDrawPhase = 5U;
      state.r[0] = 0U;
      state.r[1] = 0U;
      state.r[2] = 480U;
      state.r[3] = 400U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 5U) {
      dispatch.TopScreenPauseControllerDrawPhase = 0U;
      dispatch.TopScreenPauseControllerVisibleRenderers = {};
      Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory,
                                                               &inputs)) {
        return false;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
            Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                inputs.Pause.HasScene, inputs.Pause.SceneMode,
                inputs.Pause.RuntimeMode, 0U, 0, 1.0F, 1.0F));
      }
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs.Pause, &dispatch.TopScreenPauseRoute);
    Oot3dNativeGame::TopScreenPauseControllerAction action;
    if (!Oot3dNativeGame::ResolveTopScreenPauseController(
            memory, inputs, routeActive, &dispatch.TopScreenPauseController,
            &action)) {
      return false;
    }
    if (action ==
        Oot3dNativeGame::TopScreenPauseControllerAction::SuppressNativeDraw) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    if (action == Oot3dNativeGame::TopScreenPauseControllerAction::
                      NativeDrawThenAlternateOverlay) {
      dispatch.TopScreenPauseControllerDrawPhase = 1U;
      return branchFromCallsite(kPauseControllerNativeDraw, pc);
    }
    return branchFromCallsite(kPauseControllerNativeDraw, pc + 4U);
  }
  if (pc == kPauseRoutedCommandCallA || pc == kPauseRoutedCommandCallB) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    state.r[1] = Oot3dNativeGame::ResolveTopScreenPauseRoutedCommand(
        state.r[1], routeActive);
    return branchFromCallsite(kPauseRoutedCommandNative, pc + 4U);
  }
  if (pc == kPauseOverlayViewportDrawCall) {
    if (dispatch.TopScreenOverlayViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, &dispatch.TopScreenPauseRoute)) {
        if (dispatch.UiLifecycleBridge != nullptr) {
          dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
              std::nullopt);
        }
        return branchFromCallsite(kPauseOverlayNativeDraw, pc + 4U);
      }
      dispatch.TopScreenOverlayViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenOverlayViewportDrawPhase == 1U) {
      dispatch.TopScreenOverlayViewportDrawPhase = 2U;
      return branchFromCallsite(kPauseOverlayNativeDraw, pc);
    }
    dispatch.TopScreenOverlayViewportDrawPhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (!Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs)) {
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
            std::nullopt);
      }
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    if (dispatch.UiLifecycleBridge != nullptr) {
      dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
          Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
              inputs.HasScene, inputs.SceneMode, inputs.RuntimeMode, 0U, 0,
              1.0F, 1.0F));
    }
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branchFromCallsite(kGlViewportEntry, pc + 4U);
  }
  if (pc == kPauseSceneSevenDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (inputs.HasScene && inputs.SceneMode == 7U) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    return branchFromCallsite(kPauseSceneSevenNativeDraw, pc + 4U);
  }
  if (pc == kPauseSceneViewportDrawCall) {
    auto branch = [&](uint32_t target, uint32_t returnAddress) {
      state.r[14] = returnAddress;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    if (dispatch.TopScreenSceneViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs)) {
        return branch(kPauseSceneViewportNativeDraw, pc + 4U);
      }
      dispatch.TopScreenSceneViewportDrawArgument = state.r[0];
      dispatch.TopScreenSceneViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = static_cast<uint32_t>(-80);
      state.r[2] = 240U;
      state.r[3] = 480U;
      return branch(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenSceneViewportDrawPhase == 1U) {
      dispatch.TopScreenSceneViewportDrawPhase = 2U;
      state.r[0] = dispatch.TopScreenSceneViewportDrawArgument;
      return branch(kPauseSceneViewportNativeDraw, pc);
    }
    dispatch.TopScreenSceneViewportDrawPhase = 0U;
    dispatch.TopScreenSceneViewportDrawArgument = 0U;
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 240U;
    state.r[3] = 320U;
    return branch(kGlViewportEntry, pc + 4U);
  }
  if (pc == kPauseAlternateRendererUpdateCall) {
    if (!dispatch.TopScreenAlternateRendererRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        Oot3dNativeGame::TopScreenAlternateRendererState saved;
        if (!Oot3dNativeGame::BeginTopScreenAlternateRendererSuppression(
                memory, &saved)) {
          return false;
        }
        dispatch.TopScreenAlternateRendererRestore = saved;
        state.r[14] = pc;
      } else {
        state.r[14] = pc + 4U;
      }
      state.r[15] = kPauseAlternateRendererNativeUpdate;
      *result = {oot3d::recomp::a32::ExitKind::Branch,
                 kPauseAlternateRendererNativeUpdate,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    }
    if (!Oot3dNativeGame::EndTopScreenAlternateRendererSuppression(
            memory, *dispatch.TopScreenAlternateRendererRestore)) {
      return false;
    }
    dispatch.TopScreenAlternateRendererRestore.reset();
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseAlternateViewportDrawCall) {
    auto branch = [&](uint32_t target, uint32_t returnAddress) {
      state.r[14] = returnAddress;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    if (dispatch.TopScreenViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        return branch(kPauseAlternateViewportNativeDraw, pc + 4U);
      }
      dispatch.TopScreenViewportDrawArgument = state.r[0];
      dispatch.TopScreenViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branch(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenViewportDrawPhase == 1U) {
      dispatch.TopScreenViewportDrawPhase = 2U;
      state.r[0] = dispatch.TopScreenViewportDrawArgument;
      return branch(kPauseAlternateViewportNativeDraw, pc);
    }
    dispatch.TopScreenViewportDrawPhase = 0U;
    dispatch.TopScreenViewportDrawArgument = 0U;
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branch(kGlViewportEntry, pc + 4U);
  }
  if (pc == kTopScreenCameraUpdateEntry ||
      pc == kTopScreenCameraUpdatePatchSite) {
    const bool enteredAtFunctionEntry = pc == kTopScreenCameraUpdateEntry;
    if (enteredAtFunctionEntry &&
        dispatch.TopScreenCamera.Phase ==
            Oot3dNativeGame::TopScreenCameraGuestPhase::Idle) {
      std::uint16_t cameraSetting = 0U;
      std::uint16_t cameraIndex = 0xFFFFU;
      if (!memory.Read16(state.r[1] + 0x18AU, &cameraSetting) ||
          !memory.Read16(state.r[1] + 0x1ACU, &cameraIndex)) {
        return false;
      }
      dispatch.TopScreenCameraFov =
          Oot3dNativeGame::ResolveTopScreenCameraFovOwnership(
              {state.r[0], static_cast<std::int16_t>(cameraSetting),
               static_cast<std::int16_t>(cameraIndex)});
    }
    if (!dispatch.TopScreenCamera.Camera.Enabled) {
      dispatch.TopScreenExtendedCameraActive = false;
      return false;
    }
    const uint32_t cameraStackAddress =
        enteredAtFunctionEntry ? state.r[13] - 36U : state.r[13];
    auto branchToNative = [&](uint32_t target, uint32_t argument0,
                              uint32_t argument1 = 0U,
                              uint32_t argument2 = 0U) {
      state.r[0] = argument0;
      state.r[1] = argument1;
      state.r[2] = argument2;
      // Return through the real function entry: 0x002D84C8 is the IPS patch
      // site but not a materialized AOT block boundary. The staged phase
      // distinguishes this callback from the initial function invocation.
      state.r[14] = kTopScreenCameraUpdateEntry;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    auto finishUpdate = [&]() {
      const uint32_t stack = dispatch.TopScreenCamera.OriginalStackAddress;
      for (uint32_t index = 0U; index < 8U; ++index) {
        if (!memory.Read32(stack + index * 4U, &state.r[4U + index])) {
          return false;
        }
      }
      if (!memory.Read32(stack + 32U, &nextPc))
        return false;
      state.r[13] = stack + 36U;
      state.r[15] = nextPc;
      Oot3dNativeGame::ResetTopScreenFreeCameraGuestUpdate(
          &dispatch.TopScreenCamera);
      dispatch.TopScreenExtendedCameraActive =
          dispatch.TopScreenCamera.Camera.Active;
      *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };

    switch (dispatch.TopScreenCamera.Phase) {
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Idle: {
      ++dispatch.TopScreenCameraUpdateCalls;
      bool active = false;
      const auto cameraInput = dispatch.TopScreenCameraInput.Consume();
      if (!Oot3dNativeGame::BeginTopScreenFreeCameraGuestUpdate(
              memory, state.r[0], state.r[1], cameraStackAddress,
              cameraInput.X, cameraInput.Y,
              cameraInput.Kind ==
                  Oot3dNativeGame::NativeFreeCameraInputKind::Relative,
              &dispatch.TopScreenCamera, &active)) {
        return false;
      }
      dispatch.TopScreenExtendedCameraActive = active;
      dispatch.TopScreenCameraActiveCalls += active ? 1U : 0U;
      if (!active) {
        // The payload owns Camera_Update only while free-camera
        // ownership is active. Let the compiled native function run
        // from its original entry so it retains its optimized region
        // and executes the original `mov r6, r0` itself.
        return false;
      }
      if (enteredAtFunctionEntry) {
        // Native 0x002D84C4 is `push {r4-r11, lr}`. The IPS record replaces
        // the following `mov r6, r0`, but AOT callbacks can only observe the
        // function's real basic-block entry. Materialize the prologue only
        // after the typed path has accepted ownership; otherwise returning
        // false executes the complete original function unchanged.
        for (uint32_t index = 0U; index < 8U; ++index) {
          if (!memory.Write32(cameraStackAddress + index * 4U,
                              state.r[4U + index])) {
            return false;
          }
        }
        if (!memory.Write32(cameraStackAddress + 32U, state.r[14])) {
          return false;
        }
        state.r[13] = cameraStackAddress;
      }
      state.r[13] = dispatch.TopScreenCamera.ScratchAddress;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraCheckWater,
                            dispatch.TopScreenCamera.CameraAddress);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Water:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraEnvironment(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraUpdateInterface,
                            0U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Interface:
      if (!Oot3dNativeGame::PrepareTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      if (dispatch.TopScreenCamera.PlayerAddress == 0U) {
        return finishUpdate();
      }
      return branchToNative(Oot3dNativeGame::kTopScreenCameraBgCheckInfo,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x20U,
                            dispatch.TopScreenCamera.ScratchAddress + 0x30U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Collision:
      if (!Oot3dNativeGame::CommitTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraQuakeUpdate,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x60U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Quake: {
      const bool quakeActive = state.r[0] != 0U;
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraQuake(
              memory, &dispatch.TopScreenCamera, quakeActive)) {
        return false;
      }
      std::uint32_t floorPoly = 0U;
      std::uint8_t bgId = 0U;
      if (!memory.Read32(dispatch.TopScreenCamera.PlayerAddress + 0x7CU,
                         &floorPoly) ||
          !memory.Read8(dispatch.TopScreenCamera.PlayerAddress + 0x81U, &bgId))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraGetDataId,
                            dispatch.TopScreenCamera.GlobalContextAddress +
                                0xA98U,
                            floorPoly, bgId);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::CameraData:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraData(
              memory, &dispatch.TopScreenCamera,
              static_cast<int16_t>(state.r[0])))
        return false;
      return finishUpdate();
    }
  }
  if (pc == kTopScreenCameraFovScalar) {
    if (!dispatch.TopScreenCameraFov.Active ||
        dispatch.TopScreenCameraFov.ValueAddress != state.r[4] + 0x10U ||
        dispatch.TopScreenCamera.Camera.FovPercent == 100U) {
      return false;
    }
    std::uint32_t nativeFovBits = 0U;
    if (!memory.Read32(state.r[4] + 0x10U, &nativeFovBits)) {
      return false;
    }
    const float resolvedFov = Oot3dNativeGame::ResolveTopScreenCameraFovDegrees(
        std::bit_cast<float>(nativeFovBits),
        dispatch.TopScreenCamera.Camera.FovPercent);
    state.vfp[16] = std::bit_cast<std::uint32_t>(resolvedFov);
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenCameraNormal1Scalar) {
    if (!dispatch.TopScreenCamera.Camera.N64StyleZoom) {
      return false;
    }
    ++dispatch.TopScreenCameraNormal1Calls;
    const auto scalar = Oot3dNativeGame::ResolveTopScreenCameraNormal1Scalar(
        state.vfp[0], dispatch.TopScreenCamera.Camera.ZoomPercent);
    if (!memory.Write32(state.r[4] + 0x124U, scalar.NativeValueBits) ||
        !memory.Write32(state.r[13] + 0x60U, scalar.ContinuedValueBits)) {
      return false;
    }
    state.vfp[0] = scalar.ContinuedValueBits;
    // The payload returns at 0x0023A938, inside the original basic block.
    // Preserve that continuation through its next real AOT boundary.
    uint16_t modeBits = 0U;
    if (!memory.Read16(state.r[5] + 0x2AU, &modeBits))
      return false;
    const auto mode = static_cast<int32_t>(static_cast<int16_t>(modeBits));
    state.r[0] = static_cast<uint32_t>(mode);
    auto compareZero = [&](uint32_t value) {
      using namespace oot3d::recomp::a32;
      state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                   kFlagC | (value & kFlagN) | (value == 0U ? kFlagZ : 0U);
    };
    compareZero(state.r[0]);
    if (mode <= 0) {
      nextPc = kTopScreenCameraNormal1DefaultPath;
    } else {
      compareZero(state.r[9]);
      nextPc = state.r[9] == 0U ? kTopScreenCameraNormal1AlternatePath
                                : kTopScreenCameraNormal1DefaultPath;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (Oot3dNativeGame::ExecuteTopScreenItemDispatch(
          pc, memory, state, dispatch.TopScreenInput, dispatch.TopScreenItems)) {
    *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[15],
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenGameplayCompositionPrefix) {
    uint32_t thresholdBits = 0U;
    uint32_t compositionBits = 0U;
    if (!memory.Read32(0x002E2A6CU, &thresholdBits))
      return false;
    state.vfp[3] = state.vfp[16];
    state.r[8] = state.r[4] + 0x7C00U;
    state.r[1] = thresholdBits;
    if (!memory.Read32(state.r[8] + 0x338U, &compositionBits)) {
      return false;
    }
    state.vfp[0] = compositionBits;
    state.vfp[2] = state.vfp[3];
    state.vfp[1] = state.vfp[3];
    if (!memory.Write32(state.r[13] + 0x1DCU, state.vfp[2]) ||
        !memory.Write32(state.r[13] + 0x1E0U, state.vfp[1]) ||
        !memory.Write32(state.r[13] + 0x1E4U, state.vfp[3]) ||
        !memory.Write32(state.r[13] + 0x1E8U, state.vfp[0])) {
      return false;
    }
    state.r[0] = compositionBits;
    const uint32_t subtraction = state.r[0] - state.r[1];
    using namespace oot3d::recomp::a32;
    state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                 (subtraction & kFlagN) | (subtraction == 0U ? kFlagZ : 0U) |
                 (state.r[0] >= state.r[1] ? kFlagC : 0U) |
                 ((((state.r[0] ^ state.r[1]) & (state.r[0] ^ subtraction)) &
                   kFlagN) != 0U
                      ? kFlagV
                      : 0U);
    if (!Oot3dNativeGame::ResolveTopScreenControlFlow(
            kTopScreenGameplayCompositionBranch, &nextPc)) {
      return false;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (Oot3dNativeGame::ResolveTopScreenControlFlow(pc, &nextPc)) {
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  uint8_t vfpLane = 0U;
  uint32_t valueBits = 0U;
  if (Oot3dNativeGame::ResolveTopScreenFloatLoad(pc, &vfpLane, &valueBits)) {
    state.vfp[vfpLane] = valueBits;
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  switch (pc) {
  case kPauseUiNativeUpdateCall: {
    uint32_t pauseState = 0U;
    if (!memory.Read32(0x0050AF68U, &pauseState))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonDraw(pauseState)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = kPauseUiNativeUpdate;
    }
    break;
  }
  case kPauseProjectionPrepareCall:
    if (dispatch.TopScreenPauseProjection == nullptr ||
        !Oot3dNativeGame::ApplyTopScreenPauseProjection(
            memory, *dispatch.TopScreenPauseProjection, nullptr,
            dispatch.TopScreenConfig)) {
      return false;
    }
    state.r[14] = pc + 4U;
    nextPc = kPauseProjectionPrepare;
    break;
  case kPauseTouchButtonsDrawCall: {
    if (!Oot3dNativeGame::ApplyTopScreenNativeTouchQuad28Layout(memory)) {
      return false;
    }
    uint32_t pauseState = 0U;
    if (!memory.Read32(0x0050AF68U, &pauseState))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonDraw(pauseState)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = state.r[1];
    }
    break;
  }
  case kPauseTouchButtonsTouchBranch:
    // IPS record 27 changes `tst r0, #8` to `tst r0, #0`; the following
    // native BEQ therefore always takes the no-touch continuation.
    state.cpsr = (state.cpsr & ~((1U << 31U) | (1U << 30U))) | (1U << 30U);
    nextPc = kPauseTouchButtonsNoTouchExit;
    break;
  case kPauseLowerScreenBlock:
    nextPc = kPauseLowerScreenBlockEnd;
    break;
  case kPauseDungeonMapCursorInput:
    nextPc = state.r[14];
    break;
  default:
    return false;
  }
  state.r[15] = nextPc;
  *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
             oot3d::recomp::a32::FallbackReason::None, pc};
  if (blocksConsumed != nullptr)
    *blocksConsumed = 1U;
  return true;
}

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user);

bool ExecuteProductTopScreenHook(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                                   oot3d::recomp::a32::MemoryBus &,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   uint32_t blockBudget, uint32_t *blocksConsumed, void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  // Product opt-in UI mod, not the experimental source/gameplay dispatcher.
  if (!dispatch.TopScreenUiProfile || dispatch.Memory == nullptr ||
      (pc != kTopScreenCameraUpdateEntry && pc != kTopScreenCameraUpdatePatchSite &&
       pc != kTopScreenCameraNormal1Scalar &&
       pc != Oot3dNativeGame::kTopScreenInputUpdateBoundary &&
       pc != kPauseRendererVisibilityNativeDraw &&
       !Oot3dNativeGame::IsTopScreenItemDispatchEntry(pc))) return false;
  if (pc == kPauseRendererVisibilityNativeDraw) {
    auto &phase = dispatch.TopScreenGameOverDrawPhase;
    auto &saved = dispatch.TopScreenGameOverDrawRegisters;
    auto branch = [&](uint32_t target) {
      state.r[14] = pc;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed) *blocksConsumed = 1U;
      return true;
    };
    std::string error;
    if (phase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*dispatch.Memory, &inputs, &error) ||
          !Oot3dNativeGame::PrepareTopScreenGameOverDraw(
              *dispatch.Memory, state.r[0], inputs,
              dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch,
              &dispatch.TopScreenRendererVisibilityRoute, &error)) {
        throw std::runtime_error("TopScreen GameOver prepare: " + error);
      }
      if (dispatch.TopScreenRendererVisibilityRoute.FadeStep == 24) {
        saved = {state.r[0], state.r[1], state.r[2], state.r[3], state.r[14]};
        // 2.1.1 compositor: native auxiliary redraw establishes the 2D
        // material state before drawing the reconciled native choices.
        if (!Oot3dNativeGame::BeginTopScreenPauseChildSuppression(
                *dispatch.Memory, Oot3dNativeGame::TopScreenPauseChildSuppressionSet::AuxiliaryRedraw,
                &dispatch.TopScreenGameOverChildRestore, &error)) {
          throw std::runtime_error("TopScreen GameOver auxiliary draw: " + error);
        }
        phase = 1U;
        state.r[0] = 0U; state.r[1] = 40U;
        state.r[2] = 480U; state.r[3] = 320U;
        return branch(kGlViewportEntry);
      }
    } else if (phase == 1U) {
      phase = 2U;
      return branch(kPauseUiDraw);
    } else if (phase == 2U) {
      if (!Oot3dNativeGame::EndTopScreenPauseChildSuppression(
              *dispatch.Memory, dispatch.TopScreenGameOverChildRestore, &error)) {
        throw std::runtime_error("TopScreen GameOver restore: " + error);
      }
      phase = 3U;
      std::copy_n(saved.begin(), 4U, state.r.begin());
      state.r[14] = pc;
      // Fall through to native draw, bypassing this entry hook once.
    } else if (phase == 3U) {
      phase = 4U;
      state.r[0] = 0U; state.r[1] = 0U;
      state.r[2] = 480U; state.r[3] = 400U;
      return branch(kGlViewportEntry);
    } else {
      phase = 0U;
      std::copy_n(saved.begin(), 4U, state.r.begin());
      const auto handled = branch(saved[4]);
      state.r[14] = saved[4];
      return handled;
    }
    return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
        pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
        dispatch.BlockEntry, dispatch.BlockEntryUser,
        dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
  }
  const bool handled = ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result, blocksConsumed);
  if (handled) return true;
  // This entry was already observed before returning to the dispatcher. If
  // native ownership wins, resume it once instead of trapping the same entry.
  return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
      pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
      dispatch.BlockEntry, dispatch.BlockEntryUser,
      dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
}

bool ExecuteNativeCandidate(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                            oot3d::recomp::a32::MemoryBus &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            uint32_t blockBudget, uint32_t *blocksConsumed,
                            void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  bool sample = false;
  if (dispatch.ProfileRuntime) {
    ++dispatch.Calls;
    dispatch.SampleState = dispatch.SampleState * 1664525U + 1013904223U;
    sample = (dispatch.SampleState & (kA32RuntimeSampleDenominator - 1U)) == 0U;
  }
  const auto sampleStart = sample ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
  const bool topScreenUi =
      dispatch.TopScreenUiProfile && dispatch.Memory != nullptr &&
      ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result,
                                        blocksConsumed);
  if (topScreenUi) {
    ++dispatch.TopScreenSourcePortCalls;
    if (pc == kTopScreenGameplayCompositionPrefix) {
      ++dispatch.TopScreenLowerCompositionSkips;
      if (dispatch.PicaCompositionDomain != nullptr) {
        *dispatch.PicaCompositionDomain =
            Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->ObserveGameplayComposition();
      }
    }
  }
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  constexpr bool sourceActorInitContext = false;
  constexpr bool sourceActorUpdateAll = false;
  constexpr bool sourceCutsceneUpdateFrame = false;
  constexpr bool sourceCutsceneProcessCommands = false;
  constexpr bool sourceCameraUpdate = false;
  constexpr bool sourcePlayerUpdate = false;
  constexpr bool sourcePlayerUpdateCommon = false;
  constexpr bool sourceOwner = false;
  constexpr bool sourceCsabCurve = false;
#else
  const bool sourceActorInitContext =
      !topScreenUi && dispatch.SourceActorInitContext != nullptr &&
      dispatch.SourceActorInitContext->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceActorUpdateAll =
      !topScreenUi && !sourceActorInitContext &&
      dispatch.SourceActorUpdateAll != nullptr &&
      dispatch.SourceActorUpdateAll->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCutsceneUpdateFrame =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      dispatch.SourceCutsceneUpdateFrame != nullptr &&
      dispatch.SourceCutsceneUpdateFrame->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCutsceneProcessCommands =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame &&
      dispatch.SourceCutsceneProcessCommands != nullptr &&
      dispatch.SourceCutsceneProcessCommands->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCameraUpdate =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      dispatch.SourceCameraUpdate != nullptr &&
      dispatch.SourceCameraUpdate->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourceCameraUpdate && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr << "oot3d_native_game: source Camera_Update failed: "
              << dispatch.SourceCameraUpdate->LastError() << '\n';
  }
  const bool sourcePlayerUpdate =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      !sourceCameraUpdate && dispatch.SourcePlayerUpdate != nullptr &&
      dispatch.SourcePlayerUpdate->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourcePlayerUpdate && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr << "oot3d_native_game: source Player_Update failed: "
              << dispatch.SourcePlayerUpdate->LastError() << '\n';
  }
  const bool sourcePlayerUpdateCommon =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      !sourceCameraUpdate && !sourcePlayerUpdate &&
      dispatch.SourcePlayerUpdateCommon != nullptr &&
      dispatch.SourcePlayerUpdateCommon->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourcePlayerUpdateCommon && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr
        << "oot3d_native_game: source Player_UpdateCommon failed: "
        << dispatch.SourcePlayerUpdateCommon->LastError() << '\n';
  }
  const bool sourceOwner =
      sourceActorInitContext || sourceActorUpdateAll ||
      sourceCutsceneUpdateFrame || sourceCutsceneProcessCommands ||
      sourceCameraUpdate || sourcePlayerUpdate ||
      sourcePlayerUpdateCommon;
  const bool sourceCsabCurve =
      !topScreenUi && !sourceOwner &&
      dispatch.SourceCsabCurve != nullptr &&
      dispatch.SourceCsabCurve->Execute(
          pc, state, memory, result, blocksConsumed);
#endif
  const bool typedGameplay = !topScreenUi && !sourceOwner &&
                             !sourceCsabCurve &&
                             dispatch.TypedGameplay &&
                             dispatch.Memory != nullptr &&
                             Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                                 pc, state, *dispatch.Memory, result,
                                 dispatch.TypedGameplayContext, blocksConsumed);
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool wholeAotAllowed = dispatch.WholeAot;
#else
  const bool wholeAotAllowed =
      dispatch.WholeAot && !dispatch.TopScreenPauseAotBarrier;
#endif
  const bool compiled =
      !topScreenUi && !sourceOwner && !sourceCsabCurve &&
      !typedGameplay &&
      dispatch.CompiledFunctions &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dCompiledFunction(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true,
          dispatch.ManualCompiledFunctions, wholeAotAllowed);
  const bool massAot =
      !topScreenUi && !sourceOwner && !sourceCsabCurve &&
      !typedGameplay &&
      !compiled && dispatch.MassAot &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dMassAot(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.MassAotBlockEntryFilter,
          dispatch.MassAotObservableExitFilter, true);
  const bool trueAot = !topScreenUi && !sourceOwner &&
                       !sourceCsabCurve &&
                       !typedGameplay && !compiled && !massAot &&
                       dispatch.TrueAotBlocks &&
                       Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
                           pc, state, memory, result, nullptr);
  if (sample) {
    const uint64_t nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - sampleStart)
            .count());
    ++dispatch.Samples;
    if (sourceActorInitContext) {
      ++dispatch.SourceActorInitContextSamples;
      dispatch.SourceActorInitContextSampleNanoseconds += nanoseconds;
    } else if (sourceActorUpdateAll) {
      ++dispatch.SourceActorUpdateAllSamples;
      dispatch.SourceActorUpdateAllSampleNanoseconds += nanoseconds;
    } else if (sourceCutsceneUpdateFrame) {
      ++dispatch.SourceCutsceneUpdateFrameSamples;
      dispatch.SourceCutsceneUpdateFrameSampleNanoseconds += nanoseconds;
    } else if (sourceCutsceneProcessCommands) {
      ++dispatch.SourceCutsceneProcessCommandsSamples;
      dispatch.SourceCutsceneProcessCommandsSampleNanoseconds += nanoseconds;
    } else if (sourceCameraUpdate) {
      ++dispatch.SourceCameraUpdateSamples;
      dispatch.SourceCameraUpdateSampleNanoseconds += nanoseconds;
    } else if (sourcePlayerUpdate) {
      ++dispatch.SourcePlayerUpdateSamples;
      dispatch.SourcePlayerUpdateSampleNanoseconds += nanoseconds;
    } else if (sourcePlayerUpdateCommon) {
      ++dispatch.SourcePlayerUpdateCommonSamples;
      dispatch.SourcePlayerUpdateCommonSampleNanoseconds +=
          nanoseconds;
    } else if (sourceCsabCurve) {
      ++dispatch.SourceCsabCurveSamples;
      dispatch.SourceCsabCurveSampleNanoseconds += nanoseconds;
    } else if (typedGameplay) {
      ++dispatch.TypedGameplaySamples;
      dispatch.TypedGameplaySampleNanoseconds += nanoseconds;
    } else if (massAot || compiled) {
      ++dispatch.CompiledSamples;
      dispatch.CompiledSampleNanoseconds += nanoseconds;
    } else if (trueAot) {
      ++dispatch.TrueAotSamples;
      dispatch.TrueAotSampleNanoseconds += nanoseconds;
    } else {
      ++dispatch.UnhandledSamples;
      dispatch.UnhandledSampleNanoseconds += nanoseconds;
    }
  }
  const bool handled =
      topScreenUi || sourceOwner || sourceCsabCurve || typedGameplay ||
      massAot || compiled || trueAot;
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!handled) {
    if (result != nullptr) {
      *result = {
          oot3d::recomp::a32::ExitKind::Unsupported,
          pc,
          oot3d::recomp::a32::FallbackReason::Unsupported,
          0x57414F54U,
      };
    }
    if (blocksConsumed != nullptr) {
      *blocksConsumed = 1U;
    }
    return true;
  }
#endif
  return handled;
}

// MM3D's active Play camera (layout from Project Restoration / dekui):
// GlobalContext = *(*0x72F5C0 + 0x16C), Play when +0x13C's byte 1 is 4,
// camera index at +0xA7A, cameras at +0x408 (0x198 each) with the look-at
// point at +0x50 and the eye at +0x5C.
bool ReadMm3dPlayCamera(oot3d::recomp::a32::MemoryBus &memory,
                        std::array<float, 3> &eye, std::array<float, 3> &at) {
  uint32_t context = 0, game = 0, type = 0, indices = 0;
  if (!memory.Read32(0x0072F5C0U, &context) || context < 0x100000U ||
      !memory.Read32(context + 0x16CU, &game) || game < 0x100000U || (game & 3U) != 0U ||
      !memory.Read32(game + 0x13CU, &type) || ((type >> 8U) & 0xFFU) != 4U ||
      !memory.Read32(game + 0xA78U, &indices) || (indices >> 16U) > 3U) {
    return false;
  }
  const uint32_t camera = game + 0x408U + (indices >> 16U) * 0x198U;
  std::array<uint32_t, 6> words{};
  for (uint32_t index = 0; index < 6U; ++index) {
    if (!memory.Read32(camera + 0x50U + index * 4U, &words[index])) {
      return false;
    }
  }
  for (uint32_t index = 0; index < 3U; ++index) {
    at[index] = std::bit_cast<float>(words[index]);
    eye[index] = std::bit_cast<float>(words[3U + index]);
  }
  const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
  return std::isfinite(dx + dy + dz) && dx * dx + dy * dy + dz * dz > 1.0e-6F;
}

// MM3D: Tatl's "wants to talk" alert sits in the upper screen's top left,
// under the hearts and magic placed there: it is moved down below the
// minimap (layout units, y down; TRIAEVUM_TATL_ALERT_Y, 0 leaves it). v1.1's
// CommonLayouts (0x6B7B8C) has the play HUD at +0x10, its "chat_alart" pane
// widget at +0x170; a widget's name is at +0x10, its position at +0x3C
// (translate x, y, z; flags at +0x48 of it).
void MoveMm3dTatlAlert(oot3d::recomp::a32::MemoryBus &memory) {
  static const float offset = [] {
    const char *value = std::getenv("TRIAEVUM_TATL_ALERT_Y");
    return value != nullptr ? std::strtof(value, nullptr) : 125.0F;
  }();
  static uint32_t checkedWidget = 0;
  static bool isAlert = false;
  static uint32_t originalBits = 0, writtenBits = 0;
  uint32_t hud = 0, widget = 0, bits = 0, flags = 0, active = 0;
  if (offset == 0.0F || !memory.Read32(0x006B7B8CU + 0x10U, &hud) || hud == 0U ||
      !memory.Read32(hud + 0x170U, &widget) || widget == 0U) {
    return;
  }
  if (widget != checkedWidget) {
    checkedWidget = widget;
    uint32_t name = 0;
    char text[11] = {};
    isAlert = memory.Read32(widget + 0x10U, &name);
    for (uint32_t index = 0; isAlert && index < 10U; ++index) {
      uint8_t c = 0;
      isAlert = memory.Read8(name + index, &c);
      text[index] = static_cast<char>(c);
    }
    isAlert = isAlert && std::string_view(text, 10) == "chat_alart";
    if (!memory.Read32(widget + 0x40U, &originalBits)) isAlert = false;
    writtenBits = originalBits;
  }
  if (!isAlert || !memory.Read32(widget + 0x40U, &bits)) {
    return;
  }
  // TRIAEVUM_FORCE_TATL_STATE=<n> (tests): the HUD's Tatl state byte (+0x19D,
  // 4 hidden).
  if (const char *forced = std::getenv("TRIAEVUM_FORCE_TATL_STATE")) {
    memory.Write8(hud + 0x19DU, static_cast<uint8_t>(std::strtoul(forced, nullptr, 0)));
  }
  static const bool debug = std::getenv("TRIAEVUM_TATL_DEBUG") != nullptr;
  static uint32_t debugCalls = 0;
  if (debug && ++debugCalls % 60U == 0U) {
    uint32_t x = 0, f = 0, state = 0;
    memory.Read32(widget + 0x3CU, &x);
    memory.Read32(widget + 0x84U, &f);
    memory.Read32(hud + 0x19CU, &state);
    std::fprintf(stderr, "TATL widget=%08x pos=%.1f,%.1f flags=%08x state=%08x\n", widget,
                 std::bit_cast<float>(x), std::bit_cast<float>(bits), f, state);
  }
  // The game set its own position (its animation): offset from that one.
  if (bits != writtenBits) originalBits = bits;
  const float y = std::bit_cast<float>(originalBits) + offset;
  writtenBits = std::bit_cast<uint32_t>(y);
  if (writtenBits == bits || !memory.Read32(widget + 0x84U, &flags) || !memory.Read32(widget + 0x88U, &active)) {
    return;
  }
  memory.Write32(widget + 0x40U, writtenBits);
  memory.Write32(widget + 0x84U, flags | 0x200U);  // translate Y set
  memory.Write32(widget + 0x88U, y == 0.0F ? active | 0x200U : active & ~0x200U);
}

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user) {
  auto &runtime = *static_cast<NativeWidescreenProjectionState *>(user);
  if (pc == 0x001F55CCU) {
    // TRIAEVUM_OBJECT_TRACE: Object find (objects r0, id r1) with no slot
    // holding that object, once per id and caller.
    static std::set<uint64_t> reported;
    uint8_t count = 0;
    bool found = false;
    memory.Read8(state.r[0], &count);
    for (uint32_t i = 0; i < count && i < 0x24U && !found; ++i) {
      uint16_t id = 0;
      memory.Read16(state.r[0] + i * 0xACU + 4U, &id);
      found = std::abs(static_cast<int16_t>(id)) == static_cast<int>(state.r[1]);
    }
    if (!found && reported.insert((static_cast<uint64_t>(state.r[14]) << 32) | state.r[1]).second)
      std::fprintf(stderr, "TRIAEVUM_OBJECT_MISS id=%u lr=%08X\n", state.r[1], state.r[14]);
  }
  if (pc != 0U && pc == runtime.PlayCalcPc) {
    // GlobalContext = *(*0x72F5C0 + 0x16C), the play state (+0x13D == 4);
    // its pad state is at +0x28, the right stick's at +0x70.
    uint32_t root = 0, gctx = 0;
    uint8_t type = 0;
    if (memory.Read32(0x0072F5C0U, &root) && memory.Read32(root + 0x16CU, &gctx) && gctx != 0U &&
        memory.Read8(gctx + 0x13DU, &type) && type == 4U) {
      // The camera turns by the stick's last raw values (+0x80, +0x84: x and
      // y, -60..60); the others are left to the rest of the game.
      if (runtime.CameraStickSpeed != 1.0F) {
        for (const uint32_t offset : {0x80U, 0x84U}) {
          uint32_t bits = 0;
          if (memory.Read32(gctx + offset, &bits))
            memory.Write32(gctx + offset, std::bit_cast<uint32_t>(std::bit_cast<float>(bits) * runtime.CameraStickSpeed));
        }
      }
      if (runtime.Cheats != nullptr) Oot3dNativeGame::ApplyMm3dCheats(*runtime.Cheats, memory, gctx);
      static FILE *cameraSpeedTrace = [] {
        const char *path = std::getenv("TRIAEVUM_CAMERA_YAW_TRACE");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
      }();
      uint16_t index = 0;
      std::array<uint32_t, 6> v{};
      if (cameraSpeedTrace != nullptr && memory.Read16(gctx + 0xA7AU, &index) && index < 4U) {
        const uint32_t camera = gctx + 0x408U + index * 0x198U;
        for (uint32_t i = 0; i < 6U; ++i) memory.Read32(camera + 0x50U + i * 4U, &v[i]);
        const float dx = std::bit_cast<float>(v[3]) - std::bit_cast<float>(v[0]);
        const float dz = std::bit_cast<float>(v[5]) - std::bit_cast<float>(v[2]);
        uint32_t stick = 0;
        memory.Read32(gctx + 0x78U, &stick);
        std::fprintf(cameraSpeedTrace, "%.3f %.2f\n", std::atan2(dx, dz) * 57.29578F, std::bit_cast<float>(stick));
      }
    }
  }
  if (pc == Oot3dNativeGame::kNativeQbfA4Blit && runtime.HdFonts && runtime.TraceMemory)
    runtime.HdFonts->ObserveBlit(state, *runtime.TraceMemory);
  if (runtime.TopScreenUiProfile &&
      pc == Oot3dNativeGame::kTopScreenInputUpdateBoundary &&
      (runtime.OcarinaText == nullptr || !runtime.OcarinaText->Pending()) &&
      runtime.TopScreenInputClock != nullptr && runtime.TopScreenInput != nullptr) {
    *runtime.TopScreenInput = runtime.TopScreenInputClock->Advance();
    if (runtime.UiLifecycleBridge != nullptr)
      runtime.UiLifecycleBridge->SetTopScreenInputFrame(*runtime.TopScreenInput);
    if (runtime.UiLifecycleBridge != nullptr && runtime.OcarinaText != nullptr) {
      Oot3dNativeGame::TopScreenOcarinaGeometry geometry;
      std::string textError;
      if (!runtime.UiLifecycleBridge->ReadOcarinaGeometry(geometry, &textError) ||
          !runtime.OcarinaText->Prepare(geometry, &textError))
        throw std::runtime_error("TopScreen song text preparation: " + textError);
    }
  }
  const bool enabledTopScreenCameraExit =
      (pc != kTopScreenCameraUpdateEntry ||
       runtime.TopScreenConfig.FreeCameraEnabled) &&
      (pc != kTopScreenCameraNormal1Scalar ||
       runtime.TopScreenConfig.CameraZoomPercent != 100U);
  const bool enabledTopScreenItemExit =
      !Oot3dNativeGame::IsTopScreenItemDispatchEntry(pc) ||
      (runtime.TopScreenInput != nullptr && runtime.TopScreenItems != nullptr &&
       runtime.TraceMemory != nullptr &&
       Oot3dNativeGame::ShouldObserveTopScreenItemDispatch(
           pc, *runtime.TraceMemory, state, *runtime.TopScreenInput,
           *runtime.TopScreenItems));
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotPluginExecutionActive();
#else
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotExecutionActive();
#endif
  if (wholeAotExecutionActive &&
      enabledTopScreenCameraExit && enabledTopScreenItemExit &&
      (pc != Oot3dNativeGame::kTopScreenInputUpdateBoundary ||
       (runtime.OcarinaText != nullptr && runtime.OcarinaText->NeedsExecution())) &&
      std::binary_search(runtime.WholeAotObservableExitPcs.begin(),
                         runtime.WholeAotObservableExitPcs.end(), pc)) {
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
    Oot3dNativeGame::Oot3dWholeAotPluginObservableExit(pc, state);
#else
    Oot3dNativeGame::Oot3dWholeAotExitAt(pc, state);
#endif
  }
  if (runtime.PlayerTemporalBridge != nullptr &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.PlayerTemporalBridge->ObserveBlockEntry(
        pc, state, memory,
        runtime.FrameRatePolicy->GameplayClock().Context());
  }
  if (runtime.PicaCompositionTracker != nullptr) {
    runtime.PicaCompositionTracker->ObserveBlockEntry(pc, state, memory);
  }
  if (runtime.TraceA32Blocks) {
    ++runtime.BlockTraceRecordsObserved;
    if (runtime.BlockTrace.size() == kMaximumA32BlockTraceRecords) {
      runtime.BlockTrace.pop_front();
      runtime.BlockTraceTruncated = true;
    }
    NativeWidescreenProjectionState::A32BlockTraceRecord record;
    record.HostFrame = runtime.CurrentHostFrame;
    record.Pc = pc;
    record.MemoryWriteGeneration =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteGeneration()
            : 0U;
    record.MemoryWriteFingerprint =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteTraceFingerprint()
            : 0U;
    if (runtime.TraceMemory != nullptr) {
      record.FastReadMismatchCount =
          runtime.TraceMemory->FastReadMismatchCount();
      record.LastFastReadMismatchAddress =
          runtime.TraceMemory->LastFastReadMismatchAddress();
      record.LastFastReadMismatchValue =
          runtime.TraceMemory->LastFastReadMismatchValue();
      record.LastCheckedReadMismatchValue =
          runtime.TraceMemory->LastCheckedReadMismatchValue();
    }
    record.Registers = state.r;
    record.Cpsr = state.cpsr;
    record.Fpscr = state.fpscr;
    record.ThreadPointer = state.thread_pointer;
    record.Vfp = state.vfp;
    record.ExclusiveAddress = state.exclusive_address;
    record.ExclusiveToken = state.exclusive_token;
    record.ExclusiveSize = state.exclusive_size;
    record.ExclusiveValid = state.exclusive_valid;
    runtime.BlockTrace.push_back(std::move(record));
  }
  if (runtime.UiLifecycleBridge != nullptr) {
    const auto observation =
        runtime.UiLifecycleBridge->ObserveGuestEntry(pc);
    if (runtime.PicaCompositionDomain != nullptr &&
        Oot3dNativeGame::BeginsNativeGameplayUiPresentation(observation)) {
      *runtime.PicaCompositionDomain =
          Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
    }
    if (runtime.TopScreenUiProfile) {
      runtime.UiLifecycleBridge->ApplyTopScreenGuestHook(pc, state.r[0]);
    }
  }
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr &&
      runtime.UiLifecycleBridge != nullptr && pc == kPauseProjectionPrepare &&
      state.r[14] == kPauseProjectionPrepareCall + 4U) {
    const auto captureProjection = [&]() {
      nlohmann::json snapshot = nlohmann::json::object();
      uint32_t owner = 0U;
      uint32_t extents = 0U;
      uint32_t materialized = 0U;
      uint32_t offset = 0U;
      uint32_t extentA = 0U;
      uint32_t extentB = 0U;
      uint32_t materializedA = 0U;
      uint32_t materializedB = 0U;
      uint32_t offsetX = 0U;
      uint32_t offsetY = 0U;
      const bool rootAvailable =
          runtime.TraceMemory->Read32(0x004FDA84U, &owner);
      const bool ownerAvailable =
          rootAvailable && owner != 0U &&
          runtime.TraceMemory->Read32(owner + 0x0CU, &extents) &&
          runtime.TraceMemory->Read32(owner + 0x10U, &materialized) &&
          runtime.TraceMemory->Read32(owner + 0x1CU, &offset);
      const bool extentsAvailable =
          ownerAvailable && extents != 0U &&
          runtime.TraceMemory->Read32(extents, &extentA) &&
          runtime.TraceMemory->Read32(extents + 0x24U, &extentB);
      const bool materializedAvailable =
          ownerAvailable && materialized != 0U &&
          runtime.TraceMemory->Read32(materialized, &materializedA) &&
          runtime.TraceMemory->Read32(materialized + 0x24U, &materializedB);
      const bool offsetAvailable =
          ownerAvailable && offset != 0U &&
          runtime.TraceMemory->Read32(offset, &offsetX) &&
          runtime.TraceMemory->Read32(offset + 4U, &offsetY);
      snapshot = {
          {"owner", owner},
          {"extents", extents},
          {"materialized", materialized},
          {"offset", offset},
          {"extent_a_bits", extentsAvailable ? extentA : 0U},
          {"extent_b_bits", extentsAvailable ? extentB : 0U},
          {"materialized_a_bits", materializedAvailable ? materializedA : 0U},
          {"materialized_b_bits", materializedAvailable ? materializedB : 0U},
          {"offset_x_bits", offsetAvailable ? offsetX : 0U},
          {"offset_y_bits", offsetAvailable ? offsetY : 0U},
      };
      return snapshot;
    };
    nlohmann::json before = nullptr;
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      before = captureProjection();
    }
    ++runtime.TopScreenPauseProjectionCalls;
    std::string projectionError;
    const bool applied = Oot3dNativeGame::ApplyTopScreenPauseProjection(
        *runtime.TraceMemory,
        runtime.UiLifecycleBridge->TopScreenPauseProjection(),
        &projectionError, &runtime.TopScreenConfig);
    if (!applied) {
      ++runtime.TopScreenPauseProjectionFailures;
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      runtime.TopScreenPauseProjectionEvents.push_back(
          {{"host_frame", runtime.CurrentHostFrame},
           {"return_address", state.r[14]},
           {"applied", applied},
           {"error", projectionError},
           {"before", std::move(before)},
           {"after", captureProjection()}});
    }
  }
  constexpr uint32_t kPauseRendererSetTarget = 0x00300588U;
  constexpr uint32_t kGlViewport = 0x002FEABCU;
  constexpr uint32_t kGlBindFramebuffer = 0x00311364U;
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr) {
    if (pc == kPauseIconBuild) {
      Oot3dNativeGame::TopScreenPauseIconBuild build{state.r[2], state.r[3],
                                                     0U, 0U};
      if (runtime.TraceMemory->IsWritable(state.r[13], 8U) &&
          runtime.TraceMemory->Read32(state.r[13], &build.StackArgument0) &&
          runtime.TraceMemory->Read32(state.r[13] + 4U,
                                      &build.StackArgument1) &&
          Oot3dNativeGame::ResolveTopScreenPauseIconBuild(
              &build, runtime.TopScreenConfig)) {
        state.r[2] = build.Argument2;
        state.r[3] = build.Argument3;
        runtime.TraceMemory->Write32(state.r[13], build.StackArgument0);
        runtime.TraceMemory->Write32(state.r[13] + 4U, build.StackArgument1);
      }
    } else if (pc == kPauseRendererSetTarget) {
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      uint8_t enabled = 0U;
      uint8_t halfHeight = 0U;
      if (state.r[0] != 0U &&
          runtime.TraceMemory->Read8(state.r[0] + 4U, &enabled) &&
          runtime.TraceMemory->Read8(state.r[0] + 0x75U, &halfHeight)) {
        Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
        bool routeActive = false;
        if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                          &inputs) &&
            runtime.TopScreenPauseTargetRoute != nullptr) {
          ++runtime.TopScreenPauseTargetObservations;
          routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, runtime.TopScreenPauseTargetRoute);
          routeActive = routeActive || runtime.TopScreenPausePageRedrawActive;
          runtime.HasTopScreenPauseDrawInputs = true;
          runtime.LastTopScreenPauseDrawInputs = inputs;
          runtime.LastTopScreenPauseRouteActive = routeActive;
          Oot3dNativeGame::ObserveTopScreenPauseTargetCommand(
              inputs, state.r[1], &runtime.TopScreenPauseDrawRouting);
        }
        const Oot3dNativeGame::TopScreenPauseTargetCommand command{
            enabled != 0U, state.r[1], halfHeight != 0U};
        auto plan =
            runtime.TopScreenPausePageRedrawActive
                ? Oot3dNativeGame::ResolveTopScreenPausePageRedrawTargetCommand(
                      command)
                : Oot3dNativeGame::ResolveTopScreenPauseTargetCommand(
                      command, routeActive);
        const auto frontendPlan =
            Oot3dNativeGame::ResolveTopScreenFrontendTargetCommand(
                command,
                runtime.UiLifecycleBridge != nullptr &&
                    runtime.UiLifecycleBridge->NativeFrontendPresentationActive());
        if (frontendPlan.Handled) {
          plan = frontendPlan;
        }
        if (plan.Handled && plan.BindTopTarget) {
          runtime.PendingTopScreenPauseTarget = plan;
          runtime.PendingTopScreenPauseRenderer = state.r[0];
          ++runtime.TopScreenPauseTargetCommands;
        }
      }
    } else if (pc == kGlBindFramebuffer &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      uint32_t framebuffer = 0U;
      const auto &plan = *runtime.PendingTopScreenPauseTarget;
      if (runtime.TraceMemory->Read32(runtime.PendingTopScreenPauseRenderer +
                                          plan.FramebufferBindingOffset,
                                      &framebuffer)) {
        state.r[1] = framebuffer;
      }
    } else if (pc == kGlViewport &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      const auto plan = *runtime.PendingTopScreenPauseTarget;
      state.r[0] = plan.ViewportX;
      state.r[1] = plan.ViewportY;
      state.r[2] = plan.ViewportWidth;
      state.r[3] = plan.ViewportHeight;
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      ++runtime.TopScreenPauseTargetRewrites;
    } else if (pc == kPauseUiDraw && state.r[14] == kPauseUiDrawReturn &&
               !runtime.PendingTopScreenPauseChildRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                        &inputs)) {
        runtime.HasTopScreenPauseDrawInputs = true;
        runtime.LastTopScreenPauseDrawInputs = inputs;
        const auto action = Oot3dNativeGame::ResolveTopScreenPauseDrawAction(
            inputs, &runtime.TopScreenPauseDrawRouting);
        runtime.LastTopScreenPauseChildrenSuppressed =
            action ==
            Oot3dNativeGame::TopScreenPauseDrawAction::SuppressNativeChildren;
        if (!runtime.LastTopScreenPauseChildrenSuppressed) {
          runtime.TopScreenPausePageRedrawActive = false;
          if (runtime.UiLifecycleBridge != nullptr) {
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(std::nullopt);
          }
          ++runtime.TopScreenPauseChildNativeDraws;
        }
        if (runtime.LastTopScreenPauseChildrenSuppressed) {
          ++runtime.TopScreenPausePageCompositionCalls;
          Oot3dNativeGame::TopScreenPausePageRedrawInputs pageInputs;
          const bool runtimeSceneLatch =
              runtime.TopScreenTouchCoordinateRoute != nullptr &&
              runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch;
          const bool pageRedraw =
              Oot3dNativeGame::ReadTopScreenPausePageRedrawInputs(
                  *runtime.TraceMemory, runtimeSceneLatch, &pageInputs) &&
              Oot3dNativeGame::ResolveTopScreenPausePageRedraw(
                  pageInputs, &runtime.TopScreenPausePageRedraw);
          runtime.TopScreenPausePageRedrawActive = pageRedraw;
          if (runtime.UiLifecycleBridge != nullptr) {
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(
                    pageRedraw
                        ? std::optional(
                              Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                                  pageInputs.Pause.HasScene,
                                  pageInputs.Pause.SceneMode,
                                  pageInputs.Pause.RuntimeMode, 0U, 0, 1.0F,
                                  1.0F))
                        : std::nullopt);
          }
          const auto suppressionSet =
              pageRedraw
                  ? Oot3dNativeGame::TopScreenPauseChildSuppressionSet::
                        PageRedraw
                  : Oot3dNativeGame::TopScreenPauseChildSuppressionSet::Tail;
          Oot3dNativeGame::TopScreenPauseChildState saved;
          if (Oot3dNativeGame::BeginTopScreenPauseChildSuppression(
                  *runtime.TraceMemory, suppressionSet, &saved)) {
            runtime.PendingTopScreenPauseChildRestore = saved;
            ++runtime.TopScreenPauseChildSuppressions;
            if (pageRedraw) {
              ++runtime.TopScreenPausePageRedraws;
            } else {
              ++runtime.TopScreenPausePageRedrawSkips;
            }
          }
        }
      } else if (runtime.UiLifecycleBridge != nullptr) {
        runtime.UiLifecycleBridge->SetTopScreenPausePageRedrawEdgePresentation(
            std::nullopt);
      }
    } else if (pc == kPauseUiDrawReturn &&
               runtime.PendingTopScreenPauseChildRestore.has_value()) {
      Oot3dNativeGame::EndTopScreenPauseChildSuppression(
          *runtime.TraceMemory, *runtime.PendingTopScreenPauseChildRestore);
      runtime.PendingTopScreenPauseChildRestore.reset();
    }
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingBeginEntry &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.FrameRatePolicy->BeginFramePacing(state.r[0]);
  }
  if (pc == Oot3dNativeGame::kOot3dPlayerUpdateEntry) {
    if (runtime.PlayerTimingProbe != nullptr) {
      runtime.PlayerTimingProbe->ObserveUpdateEntry(state);
    }
    if (runtime.SceneViewProbe != nullptr) {
      runtime.SceneViewProbe->ObservePlayState(state.r[1]);
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.PlayerTimingProbe != nullptr) {
      constexpr size_t kMaximumPlayerTimingEvents = 4096U;
      const auto snapshot = runtime.PlayerTimingProbe->Capture(memory);
      if (snapshot.has_value() &&
          runtime.TemporalEventLedger != nullptr &&
          runtime.FrameRatePolicy != nullptr) {
        runtime.TemporalEventLedger->ObservePlayer(
            runtime.FrameRatePolicy->GameplayClock().Context(), *snapshot);
      }
      if (snapshot.has_value() &&
          runtime.PlayerTimingEvents.size() < kMaximumPlayerTimingEvents) {
        runtime.PlayerTimingEvents.push_back({
            {"host_frame", runtime.CurrentHostFrame},
            {"world_position",
             {snapshot->WorldX, snapshot->WorldY, snapshot->WorldZ}},
            {"velocity",
             {snapshot->VelocityX, snapshot->VelocityY, snapshot->VelocityZ}},
            {"actor_speed", snapshot->ActorSpeed},
            {"player_speed", snapshot->PlayerSpeed},
            {"floor_height", snapshot->FloorHeight},
            {"underwater_timer", snapshot->UnderwaterTimer},
            {"melee_weapon_action_timer",
             snapshot->MeleeWeaponActionTimer},
            {"melee_weapon_combo_state",
             snapshot->MeleeWeaponComboState},
            {"item_action_state_or_burn_timer",
             snapshot->ItemActionStateOrBurnTimer},
            {"ledge_climb_type", snapshot->LedgeClimbType},
            {"ledge_climb_delay_timer",
             snapshot->LedgeClimbDelayTimer},
            {"textbox_button_cooldown_timer",
             snapshot->TextboxButtonCooldownTimer},
            {"damage_flicker_animation_counter",
             snapshot->DamageFlickerAnimationCounter},
            {"damage_run_timer", snapshot->DamageRunTimer},
            {"item_action_cooldown_timer",
             snapshot->ItemActionCooldownTimer},
            {"collision_sfx_cooldown_timer",
             snapshot->CollisionSfxCooldownTimer},
            {"invincibility_timer", snapshot->InvincibilityTimer},
            {"floor_type_timer", snapshot->FloorTypeTimer},
            {"previous_floor_type", snapshot->PreviousFloorType},
            {"respawn_damage_state", snapshot->RespawnDamageState},
            {"random_turn_state", snapshot->RandomTurnState},
            {"random_turn_timer", snapshot->RandomTurnTimer},
            {"attention_persistence_counter",
             snapshot->AttentionPersistenceCounter},
            {"fairy_revive_grace_timer",
             snapshot->FairyReviveGraceTimer},
            {"background_check_flags", snapshot->BackgroundCheckFlags},
            {"action_function", snapshot->ActionFunction},
            {"animation_resource", snapshot->AnimationResource},
            {"animation_frame", snapshot->AnimationFrame},
            {"animation_play_speed", snapshot->AnimationPlaySpeed},
            {"animation_mode", snapshot->AnimationMode},
            {"time_state_address", snapshot->TimeStateAddress},
            {"time_state_update_rate", snapshot->TimeStateUpdateRate},
        });
      } else if (snapshot.has_value()) {
        runtime.PlayerTimingEventsTruncated = true;
      }
    }
  }
  if (pc == kGameStateUpdate && runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyNativeTimeScaleAtGameStateUpdate(
          state.r[0], memory)) {
    throw std::runtime_error("native frame-rate policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingCommitEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingInterval(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingDecisionEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingDeadline(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing deadline policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (runtime.ProfileA32Blocks) {
    ++runtime.BlockEntries;
    runtime.BlockSampleState =
        runtime.BlockSampleState * 1664525U + 1013904223U;
    if ((runtime.BlockSampleState & 63U) == 0U) {
      ++runtime.BlockSamples[pc];
    }
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kGameStateUpdate || pc == kFileChooseInit ||
       pc == kPlayTransitionUpdate || pc == kFileSelectActivate)) {
    const auto read32 = [&](uint32_t address) {
      uint32_t value = 0;
      memory.Read32(address, &value);
      return value;
    };
    if (pc == kFileSelectActivate) {
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", "FileSelect_Activate"},
          {"argument", state.r[0]},
          {"controller_state_before", read32(kFileSelectState + 0x10U)},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
      return;
    }
    const uint32_t gameState = state.r[0];
    const auto read8 = [&](uint32_t address) {
      uint8_t value = 0;
      memory.Read8(address, &value);
      return value;
    };
    const uint32_t main = read32(gameState + 0x04U);
    const uint32_t destroy = read32(gameState + 0x08U);
    const uint32_t nextInit = read32(gameState + 0x0CU);
    const uint32_t nextSize = read32(gameState + 0x10U);
    const uint8_t running = read8(gameState + 0x101U);
    const uint32_t nativeGameMode = read32(kNativeGameMode);
    const uint8_t transitionTrigger = read8(gameState + 0x5C2DU);
    const uint8_t transitionType = read8(gameState + 0x5C76U);
    const uint8_t transitionState = read8(gameState + 0x7F12U);
    const bool changed = !runtime.HasGameStateSnapshot ||
                         gameState != runtime.LastGameStateAddress ||
                         main != runtime.LastGameStateMain ||
                         nextInit != runtime.LastNextGameStateInit ||
                         running != runtime.LastGameStateRunning ||
                         nativeGameMode != runtime.LastNativeGameMode ||
                         transitionTrigger != runtime.LastTransitionTrigger ||
                         transitionType != runtime.LastTransitionType ||
                         transitionState != runtime.LastTransitionState;
    if (pc == kFileChooseInit || changed) {
      const char *function = pc == kGameStateUpdate  ? "GameState_Update"
                             : pc == kFileChooseInit ? "FileChoose_Init"
                                                     : "PlayTransition_Update";
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", function},
          {"state", gameState},
          {"main", main},
          {"destroy", destroy},
          {"next_init", nextInit},
          {"next_size", nextSize},
          {"running", running},
          {"native_game_mode", nativeGameMode},
          {"transition_trigger", transitionTrigger},
          {"transition_type", transitionType},
          {"transition_state", transitionState},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
    }
    runtime.GameStateUpdateCalls += pc == kGameStateUpdate ? 1U : 0U;
    runtime.LastGameStateAddress = gameState;
    runtime.LastGameStateMain = main;
    runtime.LastNextGameStateInit = nextInit;
    runtime.LastGameStateRunning = running;
    runtime.LastNativeGameMode = nativeGameMode;
    runtime.LastTransitionTrigger = transitionTrigger;
    runtime.LastTransitionType = transitionType;
    runtime.LastTransitionState = transitionState;
    runtime.HasGameStateSnapshot = true;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kPauseUiUpdate || pc == kPauseUiDraw || pc == kFileSelectUpdate ||
       pc == kFileSelectInit)) {
    const char *function = pc == kPauseUiUpdate      ? "PauseUi_Update"
                           : pc == kPauseUiDraw      ? "PauseUi_Draw"
                           : pc == kFileSelectUpdate ? "FileSelect_Update"
                                                     : "FileSelect_Init";
    ++runtime.UiLifecycleCallCounts[function];
    runtime.UiLifecycleEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"argument_0", state.r[0]},
        {"argument_1", state.r[1]},
        {"return_address", state.r[14]},
        {"thread_pointer", state.thread_pointer},
    });
    if (runtime.UiLifecycleEvents.size() > 256U) {
      runtime.UiLifecycleEvents.erase(runtime.UiLifecycleEvents.begin());
    }
    return;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kBlockingQueuePop || pc == kBlockingQueuePush ||
       pc == kRendererCommandQueueProcessPending ||
       pc == kBlockingQueueTryPush || pc == kTaskQueueTakeFirstFromPriority ||
       pc == kTaskQueueEnqueuePriorityNode ||
       pc == kTaskQueueWorkerDrainPriorityNodes || pc == kNngxP3dCallback ||
       pc == kNngxCommonInterruptHandler ||
       pc == kNngxCmdlistCompletionDispatch ||
       pc == kRendererGlobalCallbackAfterLookup)) {
    const char *function =
        pc == kBlockingQueuePop    ? "BlockingQueue_Pop"
        : pc == kBlockingQueuePush ? "BlockingQueue_Push"
        : pc == kRendererCommandQueueProcessPending
            ? "RendererCommandQueue_ProcessPending"
        : pc == kBlockingQueueTryPush ? "BlockingQueue_TryPush"
        : pc == kTaskQueueTakeFirstFromPriority
            ? "TaskQueue_TakeFirstFromPriority"
        : pc == kTaskQueueEnqueuePriorityNode ? "TaskQueue_EnqueuePriorityNode"
        : pc == kTaskQueueWorkerDrainPriorityNodes
            ? "TaskQueueWorker_DrainPriorityNodes"
        : pc == kNngxP3dCallback            ? "nngx_P3DCallback"
        : pc == kNngxCommonInterruptHandler ? "nngx_CommonInterruptHandler"
        : pc == kNngxCmdlistCompletionDispatch
            ? "nngx_CmdlistCompletionDispatch"
            : "RendererGlobalCallback_AfterLookup";
    nlohmann::json argumentWords = nlohmann::json::array();
    if (pc == kTaskQueueEnqueuePriorityNode) {
      for (uint32_t offset = 0; offset < 0x20U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(state.r[1] + offset, &value)) {
          break;
        }
        argumentWords.push_back(value);
      }
    }
    uint32_t queue = state.r[0];
    uint32_t argument = state.r[1];
    if (pc == kNngxP3dCallback || pc == kNngxCommonInterruptHandler) {
      uint32_t commandList = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxGlobalState + 0xA0U, &commandList);
      if (commandList != 0U) {
        memory.Read32(commandList + 0x30U, &callback);
        for (uint32_t offset = 0; offset < 0x38U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(commandList + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = commandList;
      argument = callback;
    } else if (pc == kNngxCmdlistCompletionDispatch) {
      uint32_t owner = 0;
      uint32_t vtable = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxCmdlistOwnerState + 0x14U, &owner);
      if (owner != 0U && memory.Read32(owner, &vtable)) {
        memory.Read32(vtable + 0x1CU, &callback);
        for (uint32_t offset = 0; offset < 0x30U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(owner + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = owner;
      argument = callback;
    } else if (pc == kRendererGlobalCallbackAfterLookup) {
      uint32_t callback = 0;
      uint32_t callbackArgument = 0;
      if (state.r[0] != 0U) {
        memory.Read32(state.r[0] + 0x10U, &callback);
        memory.Read32(state.r[0] + 0x14U, &callbackArgument);
      }
      queue = state.r[0];
      argument = callback;
      argumentWords.push_back(callbackArgument);
    }
    runtime.RendererQueueEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"thread_pointer", state.thread_pointer},
        {"queue", queue},
        {"argument", argument},
        {"return_address", state.r[14]},
        {"argument_words", std::move(argumentWords)},
    });
    ++runtime.RendererQueueCallCounts[function];
    if (runtime.RendererQueueEvents.size() > 512U) {
      runtime.RendererQueueEvents.erase(runtime.RendererQueueEvents.begin());
    }
    return;
  }
  if (pc == kEnMagInit || pc == kEnMagUpdate || pc == kEnMagDraw) {
    uint32_t &callCount = pc == kEnMagInit     ? runtime.EnMagInitCalls
                          : pc == kEnMagUpdate ? runtime.EnMagUpdateCalls
                                               : runtime.EnMagDrawCalls;
    ++callCount;

    const uint32_t actor = state.r[0];
    const auto read16 = [&](uint32_t offset) {
      uint16_t value = 0;
      return memory.Read16(actor + offset, &value) ? value : 0xFFFFU;
    };
    const auto read32 = [&](uint32_t offset) {
      uint32_t value = 0;
      return memory.Read32(actor + offset, &value) ? value : 0xFFFFFFFFU;
    };
    const auto readFloat = [&](uint32_t offset) {
      return std::bit_cast<float>(read32(offset));
    };
    nlohmann::json event{
        {"host_frame", runtime.CurrentHostFrame},
        {"function", pc == kEnMagInit     ? "EnMag_Init"
                     : pc == kEnMagUpdate ? "EnMag_Update"
                                          : "EnMag_Draw"},
        {"call", callCount},
        {"actor", actor},
        {"handle_main", read32(0x1A4U)},
        {"handle_title", read32(0x1A8U)},
        {"handle_copyright", read32(0x1ACU)},
        {"delay_timer", read16(0x1C0U)},
        {"substate", read16(0x1C4U)},
        {"timer", read16(0x1C6U)},
        {"state", read16(0x1C8U)},
        {"title_alpha", readFloat(0x1D0U)},
        {"main_alpha", readFloat(0x1D4U)},
        {"copyright_alpha", readFloat(0x1D8U)},
        {"effect_alpha", readFloat(0x1DCU)},
    };
    if (pc == kEnMagDraw) {
      nlohmann::json queueWords = nlohmann::json::array();
      for (uint32_t offset = 0; offset < 0x80U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(kRendererCommandQueue + offset, &value)) {
          break;
        }
        queueWords.push_back(value);
      }
      event["renderer_command_queue_address"] = kRendererCommandQueue;
      event["renderer_command_queue_words"] = std::move(queueWords);
    }
    if (runtime.EnMagEvents.size() == 256U) {
      runtime.EnMagEvents.erase(runtime.EnMagEvents.begin());
    }
    runtime.EnMagEvents.push_back(std::move(event));
    return;
  }
  if (pc != 0U && pc == runtime.PauseCapturePc) {
    runtime.PauseCaptured = true;
    return;
  }
  if (pc != 0U && pc == runtime.CaptureDriverPc) {
    runtime.CaptureObject = state.r[0];
    return;
  }
  if (pc != 0U && pc == runtime.LayoutPassPc && runtime.TraceMemory != nullptr) {
    runtime.LowerLayoutNames = ReadMm3dLayoutQueueNames(*runtime.TraceMemory, state.r[0], 28U);
    MoveMm3dTatlAlert(*runtime.TraceMemory);
    return;
  }
  if (pc != 0U && pc == runtime.PauseTextureSetupPc && runtime.TraceMemory != nullptr) {
    // (texture, ?, data address, descriptor): a GL-style descriptor with the
    // size at +10 (512x256), format +8 (GL_RGB) and type +C (5_6_5).
    const uint32_t descriptor = state.r[3];
    uint32_t size = 0, format = 0, type = 0;
    if (runtime.TraceMemory->Read32(descriptor + 0x10U, &size) && size == 0x01000200U &&
        runtime.TraceMemory->Read32(descriptor + 0x08U, &format) && format == 0x1907U &&
        runtime.TraceMemory->Read32(descriptor + 0x0CU, &type) && type == 0x8363U) {
      const uint32_t address = state.r[2];
      uint32_t physical = 0;
      if (address >= 0x14000000U && address < 0x1C000000U) {
        physical = address - 0x14000000U + 0x20000000U;
      } else if (address >= 0x30000000U && address < 0x40000000U) {
        physical = address - 0x30000000U + 0x20000000U;
      } else if (address >= 0x1F000000U && address < 0x1F600000U) {
        physical = address - 0x1F000000U + 0x18000000U;
      }
      auto &textures = runtime.PauseTextures;
      if (physical != 0U &&
          std::find(textures.begin(), textures.end(), physical) == textures.end()) {
        textures.push_back(physical);
        if (textures.size() > 4U) {
          textures.erase(textures.begin());
        }
      }
    }
    return;
  }
  if (pc != runtime.FrustumPc && pc != runtime.OrthographicPc) {
    return;
  }
  auto &projection = runtime.Projection;
  const float left = std::bit_cast<float>(state.vfp[0]);
  const float right = std::bit_cast<float>(state.vfp[1]);
  if (!std::isfinite(left) || !std::isfinite(right) || left == right) {
    return;
  }
  auto &callsite = projection.Callsites[{pc, state.r[14]}];
  if (callsite.Calls == 0) {
    callsite.FirstLeft = left;
    callsite.FirstRight = right;
  }
  ++callsite.Calls;
  if (pc == runtime.OrthographicPc) {
    ++projection.OrthographicCalls;
    return;
  }
  const float bottom = std::bit_cast<float>(state.vfp[2]);
  const float top = std::bit_cast<float>(state.vfp[3]);
  const float nearPlane = std::bit_cast<float>(state.vfp[4]);
  const float farPlane = std::bit_cast<float>(state.vfp[5]);
  if (runtime.FrustumAspect > 0.0F && top != bottom &&
      std::abs(std::abs((right - left) / (top - bottom)) - runtime.FrustumAspect) >
          0.02F * runtime.FrustumAspect) {
    return;  // another screen's or a special camera's frustum
  }
  const auto resolved = Fast::Oot3d::ResolvePerspectiveFov({
      {left, right, bottom, top, nearPlane, farPlane},
      projection.FovMultiplier,
      projection.Presentation,
      true,
  });
  const float resolvedLeft = resolved.Frustum.Left;
  const float resolvedRight = resolved.Frustum.Right;
  if (resolvedLeft != left || resolvedRight != right ||
      resolved.Frustum.Bottom != bottom || resolved.Frustum.Top != top) {
    state.vfp[0] = std::bit_cast<uint32_t>(resolvedLeft);
    state.vfp[1] = std::bit_cast<uint32_t>(resolvedRight);
    state.vfp[2] = std::bit_cast<uint32_t>(resolved.Frustum.Bottom);
    state.vfp[3] = std::bit_cast<uint32_t>(resolved.Frustum.Top);
    ++projection.FrustumCalls;
  }
  if (runtime.SceneViewProbe != nullptr) {
    std::array<float, 3> eye{}, at{};
    // Read the camera now, while the guest builds this frame's view: read at
    // publish time it can already be the next frame's, and anchored effects
    // (grass) then drift while the camera moves.
    if (runtime.Mm3dPlayCamera && runtime.TraceMemory != nullptr &&
        ReadMm3dPlayCamera(*runtime.TraceMemory, eye, at)) {
      runtime.SceneViewProbe->ObservePerspectiveCamera(
          pc, state.r[14], resolvedLeft, resolvedRight,
          resolved.Frustum.Bottom, resolved.Frustum.Top,
          resolved.Frustum.NearPlane, resolved.Frustum.FarPlane, eye, at);
    } else {
      runtime.SceneViewProbe->ObservePerspective(
          pc, state.r[14], resolvedLeft, resolvedRight,
          resolved.Frustum.Bottom, resolved.Frustum.Top,
          resolved.Frustum.NearPlane, resolved.Frustum.FarPlane);
    }
  }
}

Oot3dNativeGame::NativeA32ProcessRunResult
RunUntilGuestWait(Oot3dNativeGame::NativeA32Process &process,
                  uint32_t wholeAotBlockBudget) {
  static const bool diagnoseWait = std::getenv("TRIAEVUM_DIAGNOSE_GUEST_WAIT") != nullptr;
  const auto waitStart = diagnoseWait ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
  // A block-limit exit unwinds the native whole-AOT call chain. The caller
  // resumes it immediately: this is a stack-safety boundary, not an SDL event
  // pump or guest scheduling boundary.
  auto result = process.Run(wholeAotBlockBudget);
  while (result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Yielded) {
    // Low-priority workers may poll indefinitely. Once the foreground is
    // waiting, let the host deliver its timers, HID and GPU completions.
    // The worker stays Ready and resumes after those events, not next boot.
    if (process.OnlyBackgroundThreadsReady()) return result;
    if (diagnoseWait && std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(20)) {
      for (uint32_t id = 0; id < process.ThreadCount(); ++id) {
        const auto* thread = process.ThreadState(id);
        if (thread != nullptr)
          std::fprintf(stderr, "GUEST_WAIT_TIMEOUT thread=%u pc=%08x lr=%08x sp=%08x r0=%08x r1=%08x r2=%08x\n",
                       id, thread->r[15], thread->r[14], thread->r[13],
                       thread->r[0], thread->r[1], thread->r[2]);
      }
      throw std::runtime_error("guest did not reach a wait boundary within diagnostic deadline");
    }
    result = process.Run(wholeAotBlockBudget);
  }
  return result;
}

void RequireRunnableGuest(
    const Oot3dNativeGame::NativeA32ProcessRunResult &result) {
  if (result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Faulted) {
    throw std::runtime_error("native A32 process faulted: " + result.Error);
  }
}

Oot3dNativeGame::NativeGuestClockDeadlineResolution AdvanceGuestClockTo(
    Oot3dNativeGame::NativeA32Process &process,
    Oot3dNativeGame::NativeA32CtrHostServices &hostServices,
    Oot3dNativeGame::NativeA32ProcessRunResult &processResult,
    uint64_t deadline, uint32_t wholeAotBlockBudget) {
  auto resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
      hostServices.SystemTicks(), deadline);
  if (resolution.OvershootTicks != 0U) {
    return resolution;
  }
  while (processResult.Kind !=
         Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    const auto wakeTick = hostServices.NextSleepWakeTick();
    if (!wakeTick.has_value() || *wakeTick > deadline) {
      break;
    }
    const uint64_t currentTick = hostServices.SystemTicks();
    hostServices.AdvanceSystemTicks(
        *wakeTick > currentTick ? *wakeTick - currentTick : 0U);
    processResult = RunUntilGuestWait(process, wholeAotBlockBudget);
    RequireRunnableGuest(processResult);
    resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
        hostServices.SystemTicks(), deadline);
    if (resolution.OvershootTicks != 0U) {
      return resolution;
    }
  }
  resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
      hostServices.SystemTicks(), deadline);
  if (resolution.AdvanceTicks != 0U) {
    hostServices.AdvanceSystemTicks(resolution.AdvanceTicks);
  }
  return resolution;
}

std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion>
BuildPicaMemoryRegions(
    const Oot3dNativeGame::NativeA32ProcessImageManifest &manifest) {
  std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion> regions{
      {0x20000000U, manifest.LinearHeapBaseAddress, manifest.LinearHeapSize}};
  for (const auto &region : manifest.SystemRegions) {
    if (region.Name == "ctr_vram") {
      regions.push_back({0x18000000U, region.Address, region.MappedSize});
      break;
    }
  }
  return regions;
}

struct NativeControlPollingState {
  int32_t ControllerInstance = -1;
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PhysicalActions{};
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PendingPressedActions{};
  int64_t PendingMouseDeltaX = 0;
  int64_t PendingMouseDeltaY = 0;
  double PendingInputSeconds = 0.0;
  bool GameplayMouseOwned = false;
  uint64_t MouseEligiblePolls = 0;
  uint64_t MouseReleasedPolls = 0;
  uint64_t MouseHostUiPolls = 0;
  uint64_t MouseNativeUiPolls = 0;
  uint64_t MouseCaptureTransitions = 0;
  uint64_t MouseMovementPolls = 0;
  Oot3dNativeGame::NativeRightStickProfileState RightStickProfile;
  ThreeDsRecomp::Input::VirtualMotionState VirtualMotion;
  uint32_t PreviousControllerButtons = 0;
  uint32_t PendingControllerButtons = 0;
};

Oot3dNativeGame::NativeA32InputFrame
PollNativeA32Input(Fast::Fast3dWindow &window,
                   bool nativeFrontendTouchEnabled,
                   bool hostGuiVisible,
                   bool topScreenUiProfile,
                   bool topScreenFreeCameraEnabled,
                   Oot3dNativeGame::NativeControlConfigRuntime &controls,
                   const Oot3dNativeGame::NativeAimProfileTransform
                       &aimTransform,
                   double samplePeriodSeconds,
                   bool guestRefreshWillConsume,
                   NativeControlPollingState &pollingState) {
  using namespace Oot3dNativeGame;
  auto config = controls.Snapshot().Config;
  NativeControlHostInputState host;
  if (hostGuiVisible) pollingState.PendingInputSeconds = 0.0;
  host.SamplePeriodSeconds = ThreeDsRecomp::Input::ConsumeInputPeriod(
      samplePeriodSeconds, guestRefreshWillConsume || hostGuiVisible, pollingState.PendingInputSeconds);
  const auto &io = ImGui::GetIO();
  const bool keyboardCaptured = hostGuiVisible || io.WantCaptureKeyboard;
  const bool mouseCaptured = hostGuiVisible;

  ThreeDsRecomp::Input::SdlControllerSelection selectedController;
  auto *context = Ship::Context::GetRawInstance();
  auto controlDeck = context != nullptr ? context->GetControlDeck() : nullptr;
  if (controlDeck != nullptr) {
    auto devices = controlDeck->GetConnectedPhysicalDeviceManager();
    if (devices != nullptr) {
      selectedController = ThreeDsRecomp::Input::ResolveSdlController(
          devices->GetConnectedSDLGamepadsForPort(0), config.PreferredControllerGuid,
          config.PreferredControllerSerial, pollingState.ControllerInstance);
    }
  }
  if (pollingState.ControllerInstance != selectedController.InstanceId) {
    pollingState.RightStickProfile = {};
    pollingState.VirtualMotion.ResetController();
    pollingState.PreviousControllerButtons = pollingState.PendingControllerButtons = 0;
  }
  pollingState.ControllerInstance = selectedController.InstanceId;
  const auto selectedDevice = std::find_if(selectedController.Devices.begin(), selectedController.Devices.end(),
      [](const auto& device) { return device.Selected; });
  config = ControlsForDevice(config, selectedDevice != selectedController.Devices.end() ? &*selectedDevice : nullptr);
  controls.ObserveDevices(std::move(selectedController.Devices));

  const int16_t triggerThreshold = static_cast<int16_t>(
      32767 * std::clamp(config.TriggerDeadZonePercent, 0, 95) / 100);
  class LusHostButtonSource final
      : public ThreeDsRecomp::Input::HostButtonSource {
   public:
    LusHostButtonSource(Fast::Fast3dWindow &window,
                        bool keyboardCaptured,
                        bool mouseCaptured,
                        SDL_GameController *controller,
                        int16_t triggerThreshold)
        : mWindow(window), mKeyboardCaptured(keyboardCaptured),
          mMouseCaptured(mouseCaptured), mController(controller),
          mTriggerThreshold(triggerThreshold) {}

    bool IsKeyboardKeyHeld(
        NativeKeyboardKey key) const noexcept override {
      return !mKeyboardCaptured && key != NativeKeyboardKey::None && key != NativeKeyboardKey::Escape &&
             mWindow.IsKeyDown(static_cast<int32_t>(key));
    }

    bool IsMouseButtonHeld(
        NativeMouseButton button) const noexcept override {
      return !mMouseCaptured && button != NativeMouseButton::None &&
             mWindow.GetMouseState(static_cast<Ship::MouseBtn>(button));
    }

    bool IsGamepadButtonHeld(
        NativeGamepadButton binding) const noexcept override {
      return ThreeDsRecomp::Input::IsSdlControllerButtonHeld(mController, binding, mTriggerThreshold);
    }

   private:
    Fast::Fast3dWindow &mWindow;
    bool mKeyboardCaptured = false;
    bool mMouseCaptured = false;
    SDL_GameController *mController = nullptr;
    int16_t mTriggerThreshold = 0;
  };
  // Remapping observes the same physical source, before gameplay's device and
  // UI filters. It works even for a currently disabled device.
  LusHostButtonSource rawButtonSource(window, false, false,
      selectedController.Controller,
      triggerThreshold);
  if (hostGuiVisible) {
    controls.ObserveBindingCapture(rawButtonSource, window.IsKeyDown(Ship::LUS_KB_ESCAPE));
  } else {
    const auto phase = controls.BindingCaptureStatus().Phase;
    if (phase != ThreeDsRecomp::Input::BindingCapturePhase::Idle &&
        phase != ThreeDsRecomp::Input::BindingCapturePhase::Cancelled)
      controls.CancelBindingCapture();
  }
  LusHostButtonSource buttonSource(
      window, keyboardCaptured, mouseCaptured,
      !hostGuiVisible ? selectedController.Controller : nullptr,
      triggerThreshold);
  const ThreeDsRecomp::Input::HostDeviceEnablement enabledDevices{
      config.KeyboardEnabled,
      config.MouseEnabled,
      config.ControllerEnabled,
  };

  for (size_t index = 0; index < kNativeControlActionCount; ++index) {
    if (hostGuiVisible) pollingState.PendingPressedActions[index] = false;
    const auto &binding = config.Bindings[index];
    const bool physicalHeld = ThreeDsRecomp::Input::IsHostBindingHeld(
        binding, enabledDevices, buttonSource);
    if (physicalHeld && !pollingState.PhysicalActions[index] &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      pollingState.PendingPressedActions[index] = true;
    }
    pollingState.PhysicalActions[index] = physicalHeld;
    host.Actions[index] = physicalHeld;
    if (guestRefreshWillConsume &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      host.Actions[index] =
          physicalHeld || pollingState.PendingPressedActions[index];
      pollingState.PendingPressedActions[index] = false;
    }
  }

  ThreeDsRecomp::Input::SampleSdlController(
      config.ControllerEnabled ? selectedController.Controller : nullptr, host, !hostGuiVisible,
      config.ControllerTouchpadEnabled ? config.ControllerTouchpadIndex : -1, triggerThreshold);
  controls.ObserveMotion(host.ControllerMotion);
  pollingState.PendingControllerButtons |= host.ControllerButtons & ~pollingState.PreviousControllerButtons;
  pollingState.PreviousControllerButtons = host.ControllerButtons;
  if (guestRefreshWillConsume) {
    host.ControllerPressed = pollingState.PendingControllerButtons;
    pollingState.PendingControllerButtons = 0;
  }
  if (hostGuiVisible) {
    pollingState.PendingControllerButtons = 0;
    host.ControllerMotion = {};
    pollingState.RightStickProfile = {};
  }

  const bool sourceUsesMouse =
      config.NativeAimSource == NativeMotionSource::Mouse ||
      config.NativeAimSource == NativeMotionSource::Automatic ||
      (topScreenUiProfile && topScreenFreeCameraEnabled &&
       (config.FreeCameraSource == NativeMotionSource::Mouse ||
        config.FreeCameraSource == NativeMotionSource::Automatic));
  const bool gameplayMouseEligible = ResolveNativeGameplayMouseOwnership(
      nativeFrontendTouchEnabled, hostGuiVisible, config.MouseEnabled,
      sourceUsesMouse);
  const bool wasCaptured = window.IsMouseCaptured();
  window.SetMouseCapture(gameplayMouseEligible && config.CaptureMouseInGameplay);
  window.SetCursorVisibility(!window.IsMouseCaptured());
  const bool gameplayMouseOwned = gameplayMouseEligible && !window.IsMouseCaptureReleased();
  bool mouseCaptureChanged =
      gameplayMouseOwned != pollingState.GameplayMouseOwned || wasCaptured != window.IsMouseCaptured();
  pollingState.MouseEligiblePolls += gameplayMouseEligible ? 1U : 0U;
  pollingState.MouseReleasedPolls += window.IsMouseCaptureReleased() ? 1U : 0U;
  pollingState.MouseHostUiPolls += hostGuiVisible ? 1U : 0U;
  pollingState.MouseNativeUiPolls += nativeFrontendTouchEnabled ? 1U : 0U;
  pollingState.MouseCaptureTransitions += mouseCaptureChanged ? 1U : 0U;
  pollingState.GameplayMouseOwned = gameplayMouseOwned;
  // SDL relative motion is an accumulator. Drain it even while ImGui owns the
  // pointer so menu movement and capture warps cannot leak into gameplay.
  const auto mouseDelta = window.GetMouseDelta();
  pollingState.MouseMovementPolls += mouseDelta.x != 0 || mouseDelta.y != 0 ? 1U : 0U;
  if (gameplayMouseOwned && !mouseCaptureChanged) {
    pollingState.PendingMouseDeltaX += mouseDelta.x;
    pollingState.PendingMouseDeltaY += mouseDelta.y;
    if (guestRefreshWillConsume) {
      host.MouseDeltaX = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaX, INT32_MIN, INT32_MAX));
      host.MouseDeltaY = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaY, INT32_MIN, INT32_MAX));
      pollingState.PendingMouseDeltaX = 0;
      pollingState.PendingMouseDeltaY = 0;
    }
  } else {
    pollingState.PendingMouseDeltaX = 0;
    pollingState.PendingMouseDeltaY = 0;
  }

  auto frame = MapNativeControlInput(config, host, aimTransform,
                                     &pollingState.RightStickProfile,
                                     guestRefreshWillConsume,
                                     &pollingState.VirtualMotion);
  // Title touch actions, then the menu pages (bits 8-10).
  for (size_t index = static_cast<size_t>(kFirstNativeTitleAction);
       index < kNativeControlActionCount; ++index) {
    if (host.Actions[index]) {
      frame.TitleActions |= 1U << (index - static_cast<size_t>(kFirstNativeTitleAction));
    }
  }
  if (host.Actions[static_cast<size_t>(NativeControlAction::Gear)]) frame.TitleActions |= 1U << 8U;
  if (host.Actions[static_cast<size_t>(NativeControlAction::Map)]) frame.TitleActions |= 1U << 9U;
  if (host.Actions[static_cast<size_t>(NativeControlAction::Items)]) frame.TitleActions |= 1U << 10U;
  if (nativeFrontendTouchEnabled && !hostGuiVisible) {
    if (window.IsMouseCaptured()) {
      window.SetMouseCapture(false);
    }
    window.SetCursorVisibility(true);
    const auto pointer = window.GetMousePos();
    auto touch = Oot3dNativeGame::MapHostPointerToNativeA32Touch(
        pointer.x, pointer.y, window.GetWidth(), window.GetHeight(),
        window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT),
        topScreenUiProfile
            ? Oot3dNativeGame::NativeA32TouchPresentation::TopScreen400x240
            : Oot3dNativeGame::NativeA32TouchPresentation::
                  NativeLowerScreen320x240);
    if (const auto lower = TriAevumLowerScreenTouchRect(); lower.has_value()) {
      // The lower screen is laid out somewhere in the window: touch only
      // inside it.
      const float u = (static_cast<float>(pointer.x) /
                           static_cast<float>(std::max(window.GetWidth(), 1U)) -
                       lower->X) / lower->Width;
      const float v = (static_cast<float>(pointer.y) /
                           static_cast<float>(std::max(window.GetHeight(), 1U)) -
                       lower->Y) / lower->Height;
      touch.Inside = u >= 0.0F && u < 1.0F && v >= 0.0F && v < 1.0F;
      touch.X = static_cast<uint16_t>(std::clamp(
          u * static_cast<float>(NativeA32TouchWidth), 0.0F,
          static_cast<float>(NativeA32TouchWidth - 1U)));
      touch.Y = static_cast<uint16_t>(std::clamp(
          v * static_cast<float>(NativeA32TouchHeight), 0.0F,
          static_cast<float>(NativeA32TouchHeight - 1U)));
      touch.Pressed = touch.Inside &&
                      window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT);
    } else if (TriAevumLowerScreenTouchRectActive()) {
      // Parts of the lower screen shown elsewhere are touched through;
      // otherwise the lower screen is hidden.
      touch = {};
      if (const auto point = TriAevumLowerScreenPointThroughCrops(
              static_cast<float>(pointer.x) / static_cast<float>(std::max(window.GetWidth(), 1U)),
              static_cast<float>(pointer.y) / static_cast<float>(std::max(window.GetHeight(), 1U)))) {
        touch.Inside = true;
        touch.X = static_cast<uint16_t>(std::clamp(
            point->first * static_cast<float>(NativeA32TouchWidth), 0.0F,
            static_cast<float>(NativeA32TouchWidth - 1U)));
        touch.Y = static_cast<uint16_t>(std::clamp(
            point->second * static_cast<float>(NativeA32TouchHeight), 0.0F,
            static_cast<float>(NativeA32TouchHeight - 1U)));
        touch.Pressed = window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT);
      }
    }
    frame.Hid.TouchX = touch.X;
    frame.Hid.TouchY = touch.Y;
    frame.Hid.TouchPressed = touch.Pressed;
    {
      static const bool touchDebug = std::getenv("TRIAEVUM_TOUCH_DEBUG") != nullptr;
      if (touchDebug && window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT)) {
        const auto rect = TriAevumLowerScreenTouchRect();
        std::cerr << "[touch] pointer " << pointer.x << "," << pointer.y
                  << " window " << window.GetWidth() << "x" << window.GetHeight()
                  << " rect " << (rect ? rect->X : -1.0F) << "," << (rect ? rect->Y : -1.0F)
                  << " " << (rect ? rect->Width : -1.0F) << "x" << (rect ? rect->Height : -1.0F)
                  << " -> " << touch.X << "," << touch.Y << " pressed " << touch.Pressed
                  << " inside " << touch.Inside << '\n';
      }
    }
    if (!frame.Hid.TouchPressed) {
      const auto pad = ThreeDsRecomp::Input::MapNormalizedTouch(host.ControllerTouch);
      if (pad.Pressed) {
        frame.Hid.TouchX = pad.X;
        frame.Hid.TouchY = pad.Y;
        frame.Hid.TouchPressed = true;
      }
    }
  }
  if (!TriAevumLowerScreenTouchRectActive()) {
    // Titles with their own lower-screen layout get shortcut taps in
    // ApplyMm3dMenuButtons, after an input timeline.
    ApplyNativeControlShortcutTouch(host, frame);
  }
  return frame;
}

// MM3D menus without the touch screen, as in Project Restoration (and dekui):
// Select opens Items, ZR+Select the Map, ZR+Start Gear (Start alone is the
// Bombers' Notebook natively), ZR+X / ZR+Y use item slots I / II, ZR+A
// plays the ocarina, ZL+Select shows/hides the minimap (returns true). They tap
// the lower screen's buttons; the guest does not see these presses. In the
// Items/Masks/Gear/Map screens Select closes the menu (as B). In the
// Notebook (its buttons are touch only on the 3DS) the buttons of Project
// Restoration, which the Omni UI prompts show: Y switches between events and
// schedule, Select shows the event's place on the map, X sets the alarm.
bool ApplyMm3dMenuButtons(Oot3dNativeGame::NativeA32InputFrame &frame,
                          int screen, uint32_t *previousButtons,
                          uint32_t titleActions, uint32_t *previousTitleActions,
                          uint32_t menuCursor = 0, bool notebookSchedule = false) {
  bool toggleMinimap = false;
  // The bindable touch-screen buttons (F1 > Controls): View, Ocarina, item
  // slots I/II, Masks, Notebook, minimap, then the Gear/Map/Items pages.
  enum : uint32_t {
    kView = 1U << 0U, kOcarina = 1U << 1U, kSlotI = 1U << 2U, kSlotII = 1U << 3U,
    kMasksPage = 1U << 4U, kNotebookAction = 1U << 5U, kMinimap = 1U << 6U,
    kGearPage = 1U << 8U, kMapPage = 1U << 9U, kItemsPage = 1U << 10U,
  };
  const uint32_t titlePressed = titleActions & ~*previousTitleActions;
  *previousTitleActions = titleActions;
  if ((titlePressed & kMinimap) != 0U) {
    toggleMinimap = true;
  }
  if ((titleActions & kNotebookAction) != 0U) {
    frame.Hid.Buttons |= ThreeDsRecomp::Input::ButtonMask(ThreeDsRecomp::Input::Button::Start);
  }
  using ThreeDsRecomp::Input::Button;
  using ThreeDsRecomp::Input::ButtonMask;
  enum : int { kMain = 1, kNotebook = 2, kGear = 3, kItems = 4, kMap = 5, kMasks = 6, kOcarinaScreen = 7 };
  uint32_t &buttons = frame.Hid.Buttons;
  const uint32_t pressed = buttons & ~*previousButtons;
  *previousButtons = buttons;
  const uint32_t select = ButtonMask(Button::Select);
  const uint32_t start = ButtonMask(Button::Start);
  const uint32_t x = ButtonMask(Button::X);
  const uint32_t y = ButtonMask(Button::Y);
  const uint32_t a = ButtonMask(Button::A);
  const bool zr = (buttons & ButtonMask(Button::Zr)) != 0U;
  const bool zl = (buttons & ButtonMask(Button::Zl)) != 0U;
  if (zl && (buttons & select) != 0U) {
    toggleMinimap = (pressed & select) != 0U;
    buttons &= ~select;
  } else if (screen == kMain) {
    constexpr std::array<std::pair<uint32_t, std::pair<uint16_t, uint16_t>>, 8> kTaps{{
        {kView, {27U, 30U}},      {kOcarina, {27U, 208U}},  {kSlotI, {292U, 27U}},
        {kSlotII, {292U, 212U}},  {kMasksPage, {100U, 222U}}, {kGearPage, {27U, 120U}},
        {kMapPage, {162U, 220U}}, {kItemsPage, {220U, 222U}},
    }};
    for (const auto &[action, point] : kTaps) {
      if ((titlePressed & action) != 0U) {
        Oot3dNativeGame::RequestTriAevumShortcutTouch(point.first, point.second);
      }
    }
    if ((titleActions & 0x7FFU & ~(kNotebookAction | kMinimap)) != 0U) {
      // (The controller's D-pad is also the 3DS D-pad, for menus.)
      buttons &= ~(ButtonMask(Button::DpadUp) | ButtonMask(Button::DpadDown) |
                   ButtonMask(Button::DpadLeft) | ButtonMask(Button::DpadRight));
    }
    if ((pressed & select) != 0U) {
      zr ? Oot3dNativeGame::RequestTriAevumShortcutTouch(162U, 220U)    // Map
         : Oot3dNativeGame::RequestTriAevumShortcutTouch(220U, 222U);   // Items
    } else if (zr && (pressed & start) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(27U, 120U);         // Gear
    } else if (zr && (pressed & x) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(292U, 27U);         // slot I
    } else if (zr && (pressed & y) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(292U, 212U);        // slot II
    } else if (zr && (pressed & a) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(27U, 208U);         // ocarina
    }
    buttons &= ~(zr ? (select | start | x | y | a) : select);
  } else if (screen == kOcarinaScreen) {
    // Start / Select: the song list (its button, lower right, is touch only).
    if ((pressed & (start | select)) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(292U, 222U);
    }
    buttons &= ~(start | select);
  } else if (screen == kNotebook) {
    if ((pressed & y) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(292U, 226U);        // Schedule / Events
    } else if (notebookSchedule && (pressed & x) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(223U, 222U);        // Alarm
    } else if (!notebookSchedule && (pressed & select) != 0U) {
      Oot3dNativeGame::RequestTriAevumShortcutTouch(161U, 222U);        // the event's map
    }
    buttons &= ~(y | x | select);
  } else if (screen == kGear || screen == kItems || screen == kMap ||
             screen == kMasks) {
    // Item slots I/II: on the 3DS the highlighted item or mask is dragged onto
    // the slot (cursor = column | row << 8; cells measured on the lower screen).
    if ((screen == kItems || screen == kMasks) && (titlePressed & (kSlotI | kSlotII)) != 0U) {
      const uint32_t column = menuCursor & 0xFFU, row = (menuCursor >> 8U) & 0xFFU;
      if (column < (screen == kItems ? 5U : 6U) && row < 4U) {
        const uint16_t cellX = static_cast<uint16_t>(screen == kItems ? 42U + 46U * column : 32U + 40U * column);
        const uint16_t cellY = static_cast<uint16_t>(screen == kItems ? 32U + 45U * row : 29U + 46U * row);
        Oot3dNativeGame::RequestTriAevumShortcutDrag(
            cellX, cellY, 292U, (titlePressed & kSlotI) != 0U ? 27U : 212U);
      }
    }
    if ((buttons & select) != 0U) {
      buttons = (buttons & ~select) | ButtonMask(Button::B);
    }
  }
  if (const auto shortcut = Oot3dNativeGame::TakeTriAevumShortcutTouch()) {
    frame.Hid.TouchX = shortcut->first;
    frame.Hid.TouchY = shortcut->second;
    frame.Hid.TouchPressed = true;
  }
  return toggleMinimap;
}

Oot3dNativeGame::NativeA32InputFrame ResolveNativeA32GuestInput(
    const Oot3dNativeGame::NativeA32InputFrame &physical,
    const Oot3dNativeGame::NativeA32InputTimeline &timeline,
    uint32_t guestFrameIndex, uint32_t runFrameIndex,
    bool nativeFrontendTouchEnabled,
    bool topScreenStartRoutingEligible, bool topScreenUiProfile,
    Oot3dNativeGame::NativeA32PolledButtonLatch &startButtonLatch,
    Oot3dNativeGame::TopScreenStartRoutingState &startRoutingState) {
  auto resolved = physical;
  if (timeline.Enabled()) {
    resolved = timeline.Sample(guestFrameIndex, runFrameIndex);
    resolved.Exit = physical.Exit;
  } else {
    const uint32_t start = Oot3dNativeGame::NativeA32HidButtonMask(
        Oot3dNativeGame::NativeA32HidButton::Start);
    const bool physicalStartHeld = (resolved.Hid.Buttons & start) != 0U;
    if (Oot3dNativeGame::ResolveNativeA32PolledButton(
            physicalStartHeld, startButtonLatch)) {
      resolved.Hid.Buttons |= start;
    } else {
      resolved.Hid.Buttons &= ~start;
    }
  }
  Oot3dNativeGame::ApplyTopScreenStartRouting(
      resolved, topScreenUiProfile, topScreenStartRoutingEligible,
      nativeFrontendTouchEnabled, true, startRoutingState);
  return resolved;
}

bool ResolveNativeA32TouchInputEnabled(
    Oot3dNativeGame::NativeA32Memory &memory,
    const Oot3dNativeGame::Oot3dNativeUiLifecycleBridge &uiLifecycleBridge,
    bool topScreenUiProfile) {
  bool nativePausePageActive = false;
  if (topScreenUiProfile) {
    Oot3dNativeGame::TopScreenPauseDrawInputs pauseInputs;
    nativePausePageActive =
        Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &pauseInputs) &&
        pauseInputs.AnyPageGateActive;
  }
  return Oot3dNativeGame::ResolveNativeA32TouchInputEnabled(
      topScreenUiProfile,
      uiLifecycleBridge.NativeTouchPresentationActive(),
      uiLifecycleBridge.NativeFrontendPresentationActive(),
      nativePausePageActive);
}

bool ApplyTopScreenPauseClose(
    Oot3dNativeGame::NativeA32Process &process,
    const Oot3dNativeGame::TopScreenPauseClosePlan &plan, std::string *error) {
  if (plan.Page == Oot3dNativeGame::TopScreenPauseClosePage::None ||
      plan.ResetFunction == 0U) {
    return true;
  }

  uint32_t pauseState = 0U;
  uint32_t sceneControl = 0U;
  auto &memory = process.Memory();
  if (!memory.Read32(kTopScreenPauseState, &pauseState) ||
      !memory.Read32(kTopScreenPauseSceneControl, &sceneControl) ||
      !memory.IsWritable(kTopScreenPauseState, sizeof(uint32_t)) ||
      !memory.IsWritable(kTopScreenPauseSceneControl, sizeof(uint32_t))) {
    if (error != nullptr)
      *error = "cannot preflight native pause state";
    return false;
  }
  static_cast<void>(pauseState);
  static_cast<void>(sceneControl);

  constexpr std::array<uint32_t, 1> kResetArguments{0U};
  if (!process.InvokeFunction(plan.ResetFunction, kResetArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "page reset failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseState, 2U) ||
      !memory.Write32(kTopScreenPauseSceneControl, 0U)) {
    if (error != nullptr)
      *error = "native pause close-state write failed";
    return false;
  }
  constexpr std::array<uint32_t, 2> kTouchArguments{0U, 1U};
  if (!process.InvokeFunction(kPauseInputSetTouchEnabled, kTouchArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "pause input restore failed: " + *error;
    return false;
  }
  return true;
}

bool ApplyTopScreenPauseSystemOpen(
    Oot3dNativeGame::NativeA32Process &process,
    const Oot3dNativeGame::TopScreenPauseSystemOpenPlan &plan,
    std::string *error) {
  if (plan.Page == Oot3dNativeGame::TopScreenPauseClosePage::None ||
      plan.ResetFunction == 0U) {
    return true;
  }

  auto &memory = process.Memory();
  if (!memory.IsWritable(kTopScreenPauseState, sizeof(uint32_t)) ||
      !memory.IsWritable(kTopScreenPauseSceneControl, sizeof(uint32_t))) {
    if (error != nullptr)
      *error = "cannot preflight native system-menu transition state";
    return false;
  }

  constexpr std::array<uint32_t, 1> kResetArguments{0U};
  if (!process.InvokeFunction(plan.ResetFunction, kResetArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "page reset before system menu failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseSceneControl, 1U)) {
    if (error != nullptr)
      *error = "native system-menu scene-control write failed";
    return false;
  }
  constexpr std::array<uint32_t, 2> kTouchArguments{1U, 0U};
  if (!process.InvokeFunction(kPauseInputSetTouchEnabled, kTouchArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "system-menu touch ownership failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseState, 0U)) {
    if (error != nullptr)
      *error = "native system-menu pause-state write failed";
    return false;
  }
  constexpr std::array<uint32_t, 0> kNoArguments{};
  if (!process.InvokeFunction(kPauseSystemMenuOpen, kNoArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "native system-menu open failed: " + *error;
    return false;
  }
  return true;
}

} // namespace

void RunOot3dNativeA32Window(const Oot3dNativeGameLaunch &launch) {
  if (launch.Host.Renderer != "opengl" &&
      launch.Host.Renderer != "nri" &&
      launch.Host.Renderer != "vulkan") {
    throw std::runtime_error(
        "the native process requires the OpenGL or NRI/Vulkan backend");
  }

  auto topScreenConfigRuntime =
      std::make_shared<Oot3dNativeGame::TopScreenUiConfigRuntime>(
          launch.TopScreenConfigPath, launch.TopScreenConfig);
  auto controlConfigRuntime =
      std::make_shared<Oot3dNativeGame::NativeControlConfigRuntime>(
          launch.ControlConfigPath, launch.ControlConfig);
  Oot3dNativeGame::TopScreenUiConfig activeTopScreenConfig =
      topScreenConfigRuntime->Snapshot().Config;
#if defined(__SWITCH__)
  gSwitchUiProfile = Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile);
  gSwitchGameplayTiming =
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming);
  gSwitchControlProfile = Oot3dNativeGame::NativeControlProfileName(
      controlConfigRuntime->Snapshot().Config.Profile);
  gSwitchPresentationRateHz = launch.PresentationRateHz;
  gSwitchWholeAotBlockBudget = launch.WholeAotBlockBudget;
  gSwitchPicaGeometryCacheEnabled =
      !launch.DisableOpenGlPicaGeometryCache;
#endif

  std::string error;
  const auto manifest = Oot3dNativeGame::LoadNativeA32ProcessImageManifest(
      launch.A32ProcessManifestPath, &error);
  if (!manifest.has_value()) {
    throw std::runtime_error(error);
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("manifest_loaded");
#endif

  std::optional<Oot3dNativeGame::NativeScenarioCatalog> scenarioCatalog;
  std::optional<Oot3dNativeGame::NativeScenarioBootstrap> scenarioBootstrap;
  if (!launch.ScenarioId.empty()) {
    scenarioCatalog =
        Oot3dNativeGame::LoadNativeScenarioCatalog(launch.ScenarioCatalogPath);
    const auto *recipe = Oot3dNativeGame::FindNativeScenarioRecipe(
        *scenarioCatalog, launch.ScenarioId);
    if (recipe == nullptr) {
      throw std::runtime_error("unknown structural scenario: " +
                               launch.ScenarioId);
    }
    scenarioBootstrap.emplace(scenarioCatalog->Runtime, *recipe);
    std::cout << "oot3d_native_game: structural scenario " << recipe->Id
              << " selects " << recipe->ScenePath << " entrance "
              << recipe->EntranceIndex << '\n';
  }

  Oot3dNativeGame::TopScreenTextureOverridePack topScreenTextureOverridePack;
  Oot3dNativeGame::NativeHdFontRuntime hdFonts;
  bool hdFontsLoaded = false;
  if (!launch.TopScreenTextureOverridePackPath.empty()) {
    const auto fontPath = launch.TopScreenTextureOverridePackPath.parent_path() / "font_coverage.zip";
    if (std::filesystem::is_regular_file(fontPath)) {
      hdFontsLoaded = hdFonts.Load(fontPath, &error);
      if (!hdFontsLoaded) std::cerr << "HD font coverage unavailable; re-run Forge: " << error << '\n';
    }
  }
  const Oot3dNativeGame::TopScreenTextureOverridePack
      *topScreenTextureOverridePackPointer = nullptr;
  if (!launch.TopScreenTextureOverridePackPath.empty()) {
    if (!topScreenTextureOverridePack.Load(
            launch.TopScreenTextureOverridePackPath, &error)) {
      throw std::runtime_error("cannot load TopScreen texture overrides: " +
                               error);
    }
    topScreenTextureOverridePackPointer = &topScreenTextureOverridePack;
#if defined(__SWITCH__)
    gSwitchTopScreen211Assets =
        topScreenTextureOverridePack.FindProfileTexture(
            Oot3dNativeGame::kTopScreen211MenuAtlasSemantic) != nullptr &&
        topScreenTextureOverridePack.FindProfileTexture(
            "oot3d/topscreen/2.1.1/font_atlas") != nullptr;
#endif
  }

  Oot3dNativeGame::Oot3dNativePicaFrontend picaFrontend;
  picaFrontend.SetDiagnosticHistoryEnabled(false);
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker
      picaCompositionTracker;
  Oot3dNativeGame::Oot3dPicaCompositionDomain picaCompositionDomain =
      Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
  Oot3dNativeGame::NativeA32CtrHostConfig hostConfig;
  // OoT3D picks its language from the RomFS message archives; other titles
  // declare region and language in their process manifest.
  std::shared_ptr<Oot3dNativeGame::GameLanguageSettings> languageSettings;
  // MM3D's F1 > Warp and F1 > Buttons.
  std::shared_ptr<Oot3dNativeGame::Mm3dWarpRequest> mm3dWarpRequest;
  std::shared_ptr<Oot3dNativeGame::Mm3dButtonLabelEditor> mm3dButtonLabels;
  std::shared_ptr<Oot3dNativeGame::Mm3dCheats> mm3dCheats;
  // A title other than OoT3D runs without the OoT3D ports, hooks and UI
  // semantics, which are all keyed by OoT3D code and data addresses.
  const bool foreignTitle = manifest->Title.Present;
  Oot3dNativeGame::SetOot3dGameSemanticsEnabled(!foreignTitle);
  if (foreignTitle && manifest->Title.Id == "mm3d") {
    Fast::Oot3d::gPaddedTopScreenTargetMode =
        Oot3dNativeGame::TriAevumWidescreenEnabled() ? 2 : 1;
  }
  if (manifest->Title.Present) {
    hostConfig.SystemLanguage = manifest->Title.Language;
    hostConfig.SystemRegion = manifest->Title.Region;
    if (manifest->Title.Id == "mm3d") {
      // MM3D (USA) carries English, French and Spanish (message/us/use, usf,
      // uss.gmsg and layout/US_*) and picks one by the system language.
      languageSettings = std::make_shared<Oot3dNativeGame::GameLanguageSettings>(
          std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "game_language.json",
          std::vector<Oot3dNativeGame::GameLanguage>{
              {"en", "English", 1}, {"fr", "Francais", 2}, {"es", "Espanol", 5}});
      hostConfig.SystemLanguage = languageSettings->SystemId();
      mm3dWarpRequest = std::make_shared<Oot3dNativeGame::Mm3dWarpRequest>();
      mm3dCheats = std::make_shared<Oot3dNativeGame::Mm3dCheats>();
      mm3dButtonLabels = std::make_shared<Oot3dNativeGame::Mm3dButtonLabelEditor>(
          std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "button_labels.json");
    }
  } else {
    languageSettings = std::make_shared<Oot3dNativeGame::GameLanguageSettings>(
        std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "game_language.json",
        Oot3dNativeGame::DetectGameLanguages(manifest->RomFsImagePath,
            manifest->RomFsImageOffset, manifest->RomFsImageSize));
    hostConfig.SystemLanguage = languageSettings->SystemId();
  }
  hostConfig.ResourceLimitValues = manifest->ResourceLimitValues;
  hostConfig.ResourceCurrentValues = manifest->ResourceCurrentValues;
  hostConfig.LinearHeapBaseAddress = manifest->LinearHeapBaseAddress;
  hostConfig.LinearHeapSize = manifest->LinearHeapSize;
  hostConfig.HeapBaseAddress = manifest->HeapBaseAddress;
  hostConfig.HeapSize = manifest->HeapSize;
  hostConfig.RomFsImagePath = manifest->RomFsImagePath;
  hostConfig.RomFsImageOffset = manifest->RomFsImageOffset;
  hostConfig.RomFsImageSize = manifest->RomFsImageSize;
  hostConfig.PicaFrontend = &picaFrontend;
  hostConfig.PicaCompositionDomain = &picaCompositionDomain;
  hostConfig.PicaCompositionProvider = &picaCompositionTracker;
  hostConfig.SaveDataDirectory =
      launch.SaveDataDirectory.empty()
          ? launch.A32ProcessManifestPath.parent_path() /
                "oot3d_native_savedata"
          : launch.SaveDataDirectory;
  const std::filesystem::path saveDataDirectory = hostConfig.SaveDataDirectory;
  const std::filesystem::path quickStatePath =
      launch.QuickStatePath.empty() ? saveDataDirectory / "quick.oot3dsav"
                                    : launch.QuickStatePath;
  // F1 > Save states: slot 1 is quickStatePath, slots 2..10 sit next to it.
  const auto saveStateSlots = std::make_shared<Oot3dNativeGame::SaveStateSlots>(quickStatePath);
  const Oot3dNativeGame::NativeA32SavestateCompatibility savestateCompatibility{
      manifest->CodeBinSha256,
      manifest->ProcessName,
      std::string(oot3d::recomp::kA32SourceSnapshotId),
  };
  hostConfig.DetailedSvcDiagnostics = launch.ExtendedDiagnostics;
  hostConfig.ProfileRuntime = launch.ProfileA32Runtime;
  Oot3dNativeGame::NativeA32CtrHostServices hostServices(std::move(hostConfig));
  const auto frameRateContract =
      Oot3dNativeGame::ResolveNativeFrameRateContract(
          launch.GameplayTiming, launch.PresentationRateHz);
#if defined(__SWITCH__)
  gSwitchVisualInterpolation = frameRateContract.VisualInterpolationAllowed;
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (launch.EnableSourceGameplayProfile ||
      launch.EnableSourceActorInitContext ||
      launch.EnableSourceActorUpdateAll ||
      launch.EnableSourceCutsceneUpdateFrame ||
      launch.EnableSourceCutsceneProcessCommands ||
      launch.EnableSourceCameraUpdate ||
      launch.EnableSourcePlayerUpdate ||
      launch.EnableSourcePlayerUpdateCommon ||
      launch.EnableSourceCsabCurves) {
    throw std::runtime_error(
        "source-native options are documentation-only in a whole-AOT "
        "product");
  }
  const Oot3dNativeGame::SourceGameplayOwnerSelection
      sourceGameplayOwners{};
#else
  if (launch.EnableSourceGameplayProfile &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-gameplay-profile is Native30-only; "
        "Enhanced60 retains its typed timing owners");
  }
  if (launch.EnableSourceGameplayProfile &&
      (launch.DisableCompiledFunctions ||
       launch.DisableTypedGameplay)) {
    throw std::runtime_error(
        "--enable-source-gameplay-profile requires typed gameplay "
        "for GameState_Update");
  }
  const auto sourceGameplayOwners =
      Oot3dNativeGame::ResolveSourceGameplayOwnerSelection({
          launch.EnableSourceGameplayProfile,
          !launch.DisableCompiledFunctions &&
              !launch.DisableTypedGameplay,
          launch.EnableSourceActorInitContext,
          launch.EnableSourceActorUpdateAll,
          launch.EnableSourceCutsceneUpdateFrame,
          launch.EnableSourceCutsceneProcessCommands,
          launch.EnableSourceCameraUpdate,
          launch.EnableSourcePlayerUpdate,
          launch.EnableSourcePlayerUpdateCommon,
      });
#endif
  const bool enableSourceActorInitContext =
      sourceGameplayOwners.ActorInitContext;
  const bool enableSourceActorUpdateAll =
      sourceGameplayOwners.ActorUpdateAll;
  const bool enableSourceCutsceneUpdateFrame =
      sourceGameplayOwners.CutsceneUpdateFrame;
  const bool enableSourceCutsceneProcessCommands =
      sourceGameplayOwners.CutsceneProcessCommands;
  const bool enableSourceCameraUpdate =
      sourceGameplayOwners.CameraUpdate;
  const bool enableSourcePlayerUpdate =
      sourceGameplayOwners.PlayerUpdate;
  const bool enableSourcePlayerUpdateCommon =
      sourceGameplayOwners.PlayerUpdateCommon;
  const bool enableSourceCsabCurves = launch.EnableSourceCsabCurves;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (enableSourceCutsceneUpdateFrame &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-cutscene-update-frame is Native30-only; "
        "Enhanced60 retains its typed frame-crossing owner");
  }
  if (enableSourceCutsceneProcessCommands &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-cutscene-process-commands is Native30-only; "
        "Enhanced60 retains its typed cutscene path");
  }
  if (enableSourceCameraUpdate &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-camera-update is Native30-only; "
        "Enhanced60 retains its typed camera timing owner");
  }
  if (enableSourcePlayerUpdate &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-player-update is Native30-only; "
        "Enhanced60 retains its typed player timing owner");
  }
  if (enableSourcePlayerUpdateCommon &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-player-update-common is Native30-only; "
        "Enhanced60 retains its typed player timing owner");
  }
#endif
  Oot3dNativeGame::NativeFrameRatePolicy frameRatePolicy(frameRateContract);
  Oot3dNativeGame::NativeA32SceneViewProbe sceneViewProbe;
  Oot3dNativeGame::NativeA32PlayerTimingProbe playerTimingProbe;
  Oot3dNativeGame::NativeTemporalEventLedger temporalEventLedger;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge playerTemporalBridge;
  NativeWidescreenProjectionState widescreenProjection;
  widescreenProjection.HdFonts = hdFontsLoaded ? &hdFonts : nullptr;
  widescreenProjection.FrameRatePolicy = &frameRatePolicy;
  widescreenProjection.SceneViewProbe = &sceneViewProbe;
  widescreenProjection.PlayerTimingProbe = &playerTimingProbe;
  widescreenProjection.TemporalEventLedger = &temporalEventLedger;
  if (frameRateContract.Mode ==
      Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    widescreenProjection.PlayerTemporalBridge = &playerTemporalBridge;
  }
  widescreenProjection.CollectExtendedDiagnostics = launch.ExtendedDiagnostics;
  widescreenProjection.ProfileA32Blocks = launch.ProfileA32Blocks;
  widescreenProjection.TraceA32Blocks = !launch.A32BlockTracePath.empty();
  ScopedA32BlockTraceWriter blockTraceWriter(
      launch.A32BlockTracePath, widescreenProjection);
  std::vector<uint32_t> blockEntryHooks{
      kMtx4x4BuildFrustum,
      kMtx4x4BuildOrthographicProjectionRotated,
      kGameStateUpdate,
      Oot3dNativeGame::kOot3dPlayerUpdateEntry,
      Oot3dNativeGame::kOot3dFramePacingBeginEntry,
      Oot3dNativeGame::kOot3dFramePacingDecisionEntry,
      Oot3dNativeGame::kOot3dFramePacingCommitEntry,
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry,
  };
  const auto picaCompositionHooks =
      Oot3dNativeGame::Oot3dNativePicaCompositionHookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(),
                         picaCompositionHooks.begin(),
                         picaCompositionHooks.end());
  if (widescreenProjection.PlayerTemporalBridge != nullptr) {
    const auto temporalHooks =
        Oot3dNativeGame::NativeA32PlayerTemporalHookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), temporalHooks.begin(),
                           temporalHooks.end());
  }
  const auto typedGameplayObservableExitHooks =
      Oot3dNativeGame::Oot3dTypedGameplayObservableExitPoints();
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  blockEntryHooks.insert(blockEntryHooks.end(),
                         typedGameplayObservableExitHooks.begin(),
                         typedGameplayObservableExitHooks.end());
#endif
  const auto &uiLifecycleEntries =
      Oot3dNativeGame::Oot3dNativeUiLifecycleBridge::GuestEntryPoints();
  blockEntryHooks.insert(blockEntryHooks.end(), uiLifecycleEntries.begin(),
                         uiLifecycleEntries.end());
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    const std::array topScreenHooks{
        Oot3dNativeGame::kTopScreenQuestMaterializedHook,
        Oot3dNativeGame::kTopScreenQuestSubmitModelsHook,
        Oot3dNativeGame::kTopScreenQuestDrawHook,
        0x00300588U,
        0x002FEABCU,
        0x00311364U,
        kPauseProjectionPrepare,
        kPauseUiDraw,
        kPauseUiDrawReturn,
        kPauseRendererVisibilityNativeDraw,
        kPauseIconBuild};
    // Texture payloads are patched when the native pause owner has loaded
    // them, before its update can submit HUD or pause draws.
    blockEntryHooks.push_back(kPauseUiUpdate);
    blockEntryHooks.insert(blockEntryHooks.end(), topScreenHooks.begin(),
                           topScreenHooks.end());
    const std::array topScreenSourcePortHooks{
        kPauseTouchButtonsDrawCall,
        kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,
        kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,
        kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,
        kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,
        kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,
        kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,
        kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,
        kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraFovScalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenCameraUpdatePatchSite,
        kTopScreenGameplayCompositionPrefix,
    };
    blockEntryHooks.insert(blockEntryHooks.end(),
                           topScreenSourcePortHooks.begin(),
                           topScreenSourcePortHooks.end());
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    const auto itemHooks = Oot3dNativeGame::TopScreenItemDispatchEntries();
    blockEntryHooks.insert(blockEntryHooks.end(), itemHooks.begin(), itemHooks.end());
    blockEntryHooks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
  }
  if (launch.ExtendedDiagnostics) {
    const std::array diagnosticHooks{
        kEnMagInit,
        kEnMagUpdate,
        kEnMagDraw,
        kPauseUiUpdate,
        kPauseUiDraw,
        kFileSelectUpdate,
        kFileSelectInit,
        kFileChooseInit,
        kPlayTransitionUpdate,
        kFileSelectActivate,
        kBlockingQueuePop,
        kBlockingQueuePush,
        kRendererCommandQueueProcessPending,
        kBlockingQueueTryPush,
        kTaskQueueTakeFirstFromPriority,
        kTaskQueueEnqueuePriorityNode,
        kTaskQueueWorkerDrainPriorityNodes,
        kNngxP3dCallback,
        kNngxCommonInterruptHandler,
        kNngxCmdlistCompletionDispatch,
        kRendererGlobalCallbackAfterLookup,
    };
    blockEntryHooks.insert(blockEntryHooks.end(), diagnosticHooks.begin(),
                           diagnosticHooks.end());
  }
  if (foreignTitle) {
    blockEntryHooks.clear();
    if (manifest->Title.Id == "mm3d") {
      // Majora's Mask 3D (v1.1): perspective frustum builder (left, right,
      // bottom, top, near, far in s0-s5; the screen rotation is a separate
      // call) and the rotated orthographic builder.
      // Project Restoration's code.bin is a relinked build: its functions
      // sit at other addresses (data and .bss are unchanged).
      const bool restoration = manifest->Title.Variant == "restoration";
      widescreenProjection.FrustumPc = restoration ? 0x00123010U : 0x00122FE8U;
      widescreenProjection.OrthographicPc = restoration ? 0x00131838U : 0x00131810U;
      widescreenProjection.FrustumAspect = 400.0F / 240.0F;
      widescreenProjection.Mm3dPlayCamera = std::getenv("TRIAEVUM_MM3D_LATE_CAMERA") == nullptr;
      if (Oot3dNativeGame::TriAevumWidescreenEnabled()) {
        blockEntryHooks.push_back(widescreenProjection.FrustumPc);
      }
      if (std::getenv("TRIAEVUM_NO_HD_PAUSE") == nullptr) {
        widescreenProjection.PauseTextureSetupPc = restoration ? 0x00141D88U : 0x00141D44U;
        blockEntryHooks.push_back(widescreenProjection.PauseTextureSetupPc);
        widescreenProjection.PauseCapturePc = restoration ? 0x00120608U : 0x001205E0U;
        blockEntryHooks.push_back(widescreenProjection.PauseCapturePc);
      }
      if (std::getenv("TRIAEVUM_NO_CAPTURE_READBACK") == nullptr) {
        widescreenProjection.CaptureDriverPc = restoration ? 0x00112358U : 0x0011232CU;
        blockEntryHooks.push_back(widescreenProjection.CaptureDriverPc);
      }
      if (!restoration) {
        widescreenProjection.LayoutPassPc = 0x001267E8U;
        blockEntryHooks.push_back(widescreenProjection.LayoutPassPc);
        widescreenProjection.PlayCalcPc = 0x0014F550U;  // GameState+0x104 of the play state
        widescreenProjection.Cheats = mm3dCheats.get();
        blockEntryHooks.push_back(widescreenProjection.PlayCalcPc);
        // TRIAEVUM_OBJECT_TRACE: object lookups that find nothing (tests).
        if (std::getenv("TRIAEVUM_OBJECT_TRACE") != nullptr) blockEntryHooks.push_back(0x001F55CCU);
      }
      // Each draw carries the model instance that submitted it, so frame
      // interpolation pairs a model with itself whatever the draw order.
      if (!restoration && std::getenv("TRIAEVUM_NO_MODEL_IDENTITY") == nullptr) {
        picaCompositionTracker.EnableMm3dModelInstances();
        picaFrontend.SetCompositionAddressResolver(&picaCompositionTracker);
        const auto modelHooks = Oot3dNativeGame::Mm3dNativePicaCompositionHookPcs();
        blockEntryHooks.insert(blockEntryHooks.end(), modelHooks.begin(), modelHooks.end());
      }
    }
  }
  std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
  if (hdFontsLoaded && !foreignTitle) blockEntryHooks.push_back(Oot3dNativeGame::kNativeQbfA4Blit);
  std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
  blockEntryHooks.erase(
      std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
      blockEntryHooks.end());
#if defined(__SWITCH__)
  gSwitchBlockHookCount = blockEntryHooks.size();
  gSwitchBlockHooksSorted =
      std::is_sorted(blockEntryHooks.begin(), blockEntryHooks.end());
  gSwitchBlockHooksContainProcessEntry = std::binary_search(
      blockEntryHooks.begin(), blockEntryHooks.end(), 0x00100000U);
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableCompiledFunctions = true;
#else
  const bool enableCompiledFunctions = !launch.DisableCompiledFunctions;
#endif
  const bool wholeAotAvailable =
      Oot3dNativeGame::Oot3dWholeAotAvailable();
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!wholeAotAvailable) {
    throw std::runtime_error(
        "whole-AOT product mode requires the audited generated backend");
  }
  const bool enableWholeAot = true;
#else
  const bool enableWholeAot = enableCompiledFunctions &&
                              !launch.DisableWholeAot &&
                              wholeAotAvailable;
#endif
  if (enableCompiledFunctions && !launch.DisableWholeAot &&
      !wholeAotAvailable) {
    std::cerr
        << "Warning: whole-AOT was requested but is not linked. "
           "Guest execution will use substantially slower fallback paths.\n";
  }
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableMassAot = false;
#else
  const bool enableMassAot = enableCompiledFunctions &&
                             !launch.DisableMassAot &&
                             Oot3dNativeGame::Oot3dMassAotAvailable();
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableTrueAot = false;
#else
  const bool enableTrueAot =
      enableCompiledFunctions && !launch.DisableTrueAotBlocks;
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableManualCompiledFunctions = true;
#else
  const bool enableManualCompiledFunctions =
      !launch.DisableManualCompiledFunctions;
#endif
  const auto trueAotEntries = Oot3dNativeGame::Oot3dTrueAotBlockEntryPoints();
  const auto compiledFunctionEntries =
      Oot3dNativeGame::Oot3dCompiledFunctionEntryPoints(
          enableWholeAot,
          enableCompiledFunctions && enableManualCompiledFunctions);
  const auto typedGameplayEntries =
      Oot3dNativeGame::Oot3dTypedGameplayEntryPoints();
  std::vector<uint32_t> nativeCandidateEntries;
  if (enableMassAot) {
    const auto massAotEntries = Oot3dNativeGame::Oot3dMassAotBlockEntryPoints();
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  massAotEntries.begin(), massAotEntries.end());
    Oot3dNativeGame::ResetOot3dMassAotStats();
  }
  if (enableCompiledFunctions) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  typedGameplayEntries.begin(),
                                  typedGameplayEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  compiledFunctionEntries.begin(),
                                  compiledFunctionEntries.end());
    Oot3dNativeGame::ResetOot3dTypedGameplayStats();
    Oot3dNativeGame::ResetOot3dCompiledFunctionStats();
    Oot3dNativeGame::SetOot3dCompiledFunctionProfilingEnabled(
        launch.ProfileA32Runtime);
  }
  if (enableTrueAot) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  trueAotEntries.begin(), trueAotEntries.end());
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
  }
  if (enableSourceActorUpdateAll) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceActorUpdateAllEntry);
  }
  if (enableSourceActorInitContext) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceActorInitContextEntry);
  }
  if (enableSourceCutsceneUpdateFrame) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCutsceneUpdateFrameEntry);
  }
  if (enableSourceCutsceneProcessCommands) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCutsceneProcessCommandsEntry);
  }
  if (enableSourceCameraUpdate) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCameraUpdateEntry);
  }
  if (enableSourcePlayerUpdate) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateEntry);
  }
  if (enableSourcePlayerUpdateCommon) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateCommonEntry);
  }
  if (enableSourceCsabCurves) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCsabCurveS16Entry);
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCsabCurveF32Entry);
  }
  std::vector<uint32_t> topScreenSourcePortBlocks;
  std::vector<uint32_t> wholeAotObservableExitBlocks;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  wholeAotObservableExitBlocks.push_back(
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry);
  wholeAotObservableExitBlocks.insert(
      wholeAotObservableExitBlocks.end(), picaCompositionHooks.begin(),
      picaCompositionHooks.end());
#endif
  if (enableSourceActorUpdateAll) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceActorUpdateAllEntry);
  }
  if (enableSourceActorInitContext) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceActorInitContextEntry);
  }
  if (enableSourceCutsceneUpdateFrame) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCutsceneUpdateFrameEntry);
  }
  if (enableSourceCutsceneProcessCommands) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCutsceneProcessCommandsEntry);
  }
  if (enableSourceCameraUpdate) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCameraUpdateEntry);
  }
  if (enableSourcePlayerUpdate) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateEntry);
  }
  if (enableSourcePlayerUpdateCommon) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateCommonEntry);
  }
  if (enableSourceCsabCurves) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCsabCurveS16Entry);
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCsabCurveF32Entry);
  }
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (enableCompiledFunctions && !launch.DisableTypedGameplay) {
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        typedGameplayObservableExitHooks.begin(),
        typedGameplayObservableExitHooks.end());
  }
#endif
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    topScreenSourcePortBlocks = {
        kPauseTouchButtonsDrawCall,       kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,           kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,         kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,  kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,         kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,    kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,         kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,  kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,         kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,      kTopScreenCameraUpdatePatchSite,
        kTopScreenTitleLogoFadeHoldPrefix};
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    const auto itemEntries = Oot3dNativeGame::TopScreenItemDispatchEntries();
    topScreenSourcePortBlocks.insert(topScreenSourcePortBlocks.end(),
                                    itemEntries.begin(), itemEntries.end());
    topScreenSourcePortBlocks.push_back(kTopScreenGameplayCompositionPrefix);
    topScreenSourcePortBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    // These source ports are reached from inside large AOT regions. Make
    // their native entries observable so execution returns to the dispatcher
    // before applying the typed replacement. Keep this list sorted because
    // the whole-AOT block-entry callback uses binary_search.
    const std::array topScreenObservableExitBlocks{
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenGameplayCompositionPrefix,
        kTopScreenTitleLogoFadeHoldPrefix,
    };
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        topScreenObservableExitBlocks.begin(),
        topScreenObservableExitBlocks.end());
#else
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraUpdateEntry);
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraNormal1Scalar);
#endif
    wholeAotObservableExitBlocks.push_back(Oot3dNativeGame::kTopScreenInputUpdateBoundary);
    wholeAotObservableExitBlocks.push_back(kPauseRendererVisibilityNativeDraw);
    wholeAotObservableExitBlocks.insert(wholeAotObservableExitBlocks.end(),
                                       itemEntries.begin(), itemEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  topScreenSourcePortBlocks.begin(),
                                  topScreenSourcePortBlocks.end());
  }
  std::sort(wholeAotObservableExitBlocks.begin(),
            wholeAotObservableExitBlocks.end());
  wholeAotObservableExitBlocks.erase(
      std::unique(wholeAotObservableExitBlocks.begin(),
                  wholeAotObservableExitBlocks.end()),
      wholeAotObservableExitBlocks.end());
  std::sort(nativeCandidateEntries.begin(), nativeCandidateEntries.end());
  nativeCandidateEntries.erase(
      std::unique(nativeCandidateEntries.begin(), nativeCandidateEntries.end()),
      nativeCandidateEntries.end());
  Oot3dNativeGame::ConfigureOot3dNativeA32Candidates(
      nativeCandidateEntries);
  Oot3dNativeGame::NativeA32Process process(
      Oot3dNativeGame::Oot3dNativeA32Registry(), hostServices);
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  Oot3dNativeGame::SourceActorInitContextRuntime sourceActorInitContext(
      process);
  Oot3dNativeGame::SourceActorUpdateAllRuntime sourceActorUpdateAll(process);
  Oot3dNativeGame::SourceCutsceneUpdateFrameRuntime
      sourceCutsceneUpdateFrame(process);
  Oot3dNativeGame::SourceCutsceneProcessCommandsRuntime
      sourceCutsceneProcessCommands(process);
  Oot3dNativeGame::SourceCameraUpdateRuntime sourceCameraUpdate(process);
  Oot3dNativeGame::SourcePlayerUpdateRuntime sourcePlayerUpdate(process);
  Oot3dNativeGame::SourcePlayerUpdateCommonRuntime
      sourcePlayerUpdateCommon(process);
  Oot3dNativeGame::SourceCsabCurveRuntime sourceCsabCurve(
      process.Memory());
#endif
  widescreenProjection.TraceMemory = &process.Memory();
  process.Memory().EnableWriteTraceFingerprint(
      widescreenProjection.TraceA32Blocks);
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge uiLifecycleBridge(
      process.Memory());
  uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
  widescreenProjection.UiLifecycleBridge = &uiLifecycleBridge;
  widescreenProjection.PicaCompositionDomain = &picaCompositionDomain;
  widescreenProjection.PicaCompositionTracker = &picaCompositionTracker;
  widescreenProjection.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  const auto nativeBlockEntryPcs =
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::span<const uint32_t>{}
          : std::span<const uint32_t>{blockEntryHooks};
  NativeCandidateDispatchState nativeCandidateDispatch{
      &ApplyNativeWidescreenProjectionPolicy,
      &widescreenProjection,
      nativeBlockEntryPcs,
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(nativeBlockEntryPcs,
                                                 nativeBlockEntryPcs.empty()),
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(
          wholeAotObservableExitBlocks, false),
      &process.Memory(),
      {static_cast<float>(frameRateContract.NativeUpdateRate),
       &frameRatePolicy.GameplayClock().Context()},
      enableCompiledFunctions && !launch.DisableTypedGameplay,
      enableMassAot,
      enableCompiledFunctions,
      enableWholeAot,
      enableManualCompiledFunctions,
      enableTrueAot,
      launch.ProfileA32Runtime,
  };
  nativeCandidateDispatch.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  nativeCandidateDispatch.SourceActorInitContext =
      enableSourceActorInitContext ? &sourceActorInitContext : nullptr;
  nativeCandidateDispatch.SourceActorUpdateAll =
      enableSourceActorUpdateAll ? &sourceActorUpdateAll : nullptr;
  nativeCandidateDispatch.SourceCutsceneUpdateFrame =
      enableSourceCutsceneUpdateFrame ? &sourceCutsceneUpdateFrame : nullptr;
  nativeCandidateDispatch.SourceCutsceneProcessCommands =
      enableSourceCutsceneProcessCommands
          ? &sourceCutsceneProcessCommands
          : nullptr;
  nativeCandidateDispatch.SourceCameraUpdate =
      enableSourceCameraUpdate ? &sourceCameraUpdate : nullptr;
  nativeCandidateDispatch.SourcePlayerUpdate =
      enableSourcePlayerUpdate ? &sourcePlayerUpdate : nullptr;
  nativeCandidateDispatch.SourcePlayerUpdateCommon =
      enableSourcePlayerUpdateCommon
          ? &sourcePlayerUpdateCommon
          : nullptr;
  nativeCandidateDispatch.SourceCsabCurve =
      enableSourceCsabCurves ? &sourceCsabCurve : nullptr;
#endif
  widescreenProjection.TopScreenInput =
      &nativeCandidateDispatch.TopScreenInput;
  widescreenProjection.TopScreenInputClock =
      &nativeCandidateDispatch.TopScreenInputClock;
  widescreenProjection.TopScreenItems = &nativeCandidateDispatch.TopScreenItems;
  nativeCandidateDispatch.TopScreenItems.TraceEnabled = launch.ExtendedDiagnostics;
  nativeCandidateDispatch.UiLifecycleBridge = &uiLifecycleBridge;
  nativeCandidateDispatch.PicaCompositionDomain = &picaCompositionDomain;
  nativeCandidateDispatch.TopScreenPauseProjection =
      &uiLifecycleBridge.TopScreenPauseProjection();
  nativeCandidateDispatch.TopScreenConfig = &activeTopScreenConfig;
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera, activeTopScreenConfig);
  std::uint64_t appliedTopScreenConfigRevision =
      topScreenConfigRuntime->Snapshot().Revision;
  const auto applyTopScreenConfigSnapshot = [&]() {
    const auto snapshot = topScreenConfigRuntime->Snapshot();
    if (snapshot.Revision == appliedTopScreenConfigRevision) {
      return;
    }
    activeTopScreenConfig = snapshot.Config;
    appliedTopScreenConfigRevision = snapshot.Revision;
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenConfig = activeTopScreenConfig;
    Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
        nativeCandidateDispatch.TopScreenCamera.Camera,
        activeTopScreenConfig);
    if (!activeTopScreenConfig.FreeCameraEnabled) {
      nativeCandidateDispatch.TopScreenCameraInput.Reset();
    }
  };
  const auto setTopScreenSessionMinimapVisible = [&](bool visible) {
    activeTopScreenConfig.MinimapVisible = visible;
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  };
  widescreenProjection.TopScreenPauseTargetRoute =
      &nativeCandidateDispatch.TopScreenPauseRoute;
  widescreenProjection.TopScreenTouchCoordinateRoute =
      &nativeCandidateDispatch.TopScreenTouchCoordinateRoute;
  widescreenProjection.WholeAotObservableExitPcs =
      wholeAotObservableExitBlocks;
  process.SetTimingEnabled(launch.ProfileA32Runtime);
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  // Native candidates are experimental reconstruction aids.  The product
  // executes the audited whole-AOT graph and never substitutes source-native
  // or typed gameplay implementations for guest functions.
  if (!nativeCandidateEntries.empty()) {
    process.SetNativeBlockCallback(&ExecuteNativeCandidate,
                                   &nativeCandidateDispatch);
  }
#else
  if (nativeCandidateDispatch.TopScreenUiProfile) {
    process.SetNativeBlockCallback(&ExecuteProductTopScreenHook,
                                   &nativeCandidateDispatch);
  }
#endif
  if (!Oot3dNativeGame::MountNativeA32ProcessImage(
          process, *manifest, std::filesystem::path{}, &error)) {
    throw std::runtime_error(error);
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("process_image_mounted");
#endif
  const auto applySelectedUiProfile = [&]() {
    if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
      return;
    }
    Oot3dNativeGame::TopScreenLayoutApplyStats layoutStats;
    if (!Oot3dNativeGame::ApplyTopScreenVerifiedLayout(process.Memory(),
                                                       &layoutStats, &error)) {
      throw std::runtime_error(
          "TopScreen source-port layout rejected the process image: " + error);
    }
    Oot3dNativeGame::TopScreenRuntimeGeometryApplyStats geometryStats;
    if (layoutStats.WordsChanged != 0U &&
        !Oot3dNativeGame::RebindTopScreenVerifiedRuntimeGeometry(
            process.Memory(), &geometryStats, &error)) {
      throw std::runtime_error(
          "TopScreen source-port runtime geometry rejected the process "
          "image: " +
          error);
    }
    std::cout << "TopScreen source-port layout: contracts="
              << layoutStats.ContractsChecked
              << " words=" << layoutStats.WordsWritten
              << " changed_words=" << layoutStats.WordsChanged
              << " rebound_streams=" << geometryStats.StreamsRebound
              << " rebound_quads=" << geometryStats.QuadsRebound << '\n';
  };

  const auto picaMemoryRegions = BuildPicaMemoryRegions(*manifest);
  const Oot3dNativeGame::Oot3dPicaPhysicalMemoryView picaMemoryView(
      process.Memory(), picaMemoryRegions);
  std::optional<Oot3dNativeGame::TopScreenTextureOverrideRuntime>
      topScreenTextureOverrideRuntime;
  if (topScreenTextureOverridePackPointer != nullptr) {
    topScreenTextureOverrideRuntime.emplace(topScreenTextureOverridePack,
                                            picaMemoryView);
    widescreenProjection.TopScreenTextureOverrides =
        &*topScreenTextureOverrideRuntime;
  }
  Oot3dNativeGame::Oot3dNativePicaSubmissionQueue submissionQueue(
      picaMemoryView, true);
  if (hdFontsLoaded) submissionQueue.SetTextureSnapshotTransform(
      [&](const Oot3dNativeGame::Oot3dPicaTextureState& texture,
          Oot3dNativeGame::Oot3dPicaResourceSnapshot& resource) {
        hdFonts.Resolve(picaMemoryView, texture, resource);
      });
  if (topScreenTextureOverrideRuntime.has_value()) {
    submissionQueue.SetTexturePayloadTransform(
        [&](const Oot3dNativeGame::Oot3dPicaTextureState &texture,
            std::span<std::uint8_t> payload) {
          topScreenTextureOverrideRuntime->Transform(texture, payload);
        });
  }
  submissionQueue.SetRuntimeProfilingEnabled(launch.ProfileA32Runtime);
  picaFrontend.SetPacketSink(&submissionQueue);

  Args hostArgs = launch.Host;
  hostArgs.AudioSampleRate = Oot3dNativeGame::NativeA32DspHle::NativeSampleRate;
  hostArgs.AudioSampleLength =
      Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame;
  hostArgs.AudioDesiredBuffered = std::max<int32_t>(
      hostArgs.AudioDesiredBuffered,
      static_cast<int32_t>((static_cast<uint64_t>(hostArgs.AudioSampleRate) *
                                kMinimumHostAudioPrebufferMilliseconds +
                            999U) /
                           1000U));
  std::vector<std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>>
      applicationSettingsTabs;
  if (languageSettings) applicationSettingsTabs.push_back(Oot3dNativeGame::CreateGameLanguagePanel(languageSettings));
  if (mm3dWarpRequest) applicationSettingsTabs.push_back(Oot3dNativeGame::CreateMm3dWarpPanel(mm3dWarpRequest));
  if (mm3dCheats) applicationSettingsTabs.push_back(Oot3dNativeGame::CreateMm3dCheatsPanel(mm3dCheats));
  if (mm3dButtonLabels)
    applicationSettingsTabs.push_back(Oot3dNativeGame::CreateMm3dButtonLabelPanel(mm3dButtonLabels));
  applicationSettingsTabs.push_back(Oot3dNativeGame::CreateSaveStatePanel(saveStateSlots));
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateNativeControlsSettingsPanel(
          controlConfigRuntime,
          launch.UiProfile ==
                  Oot3dNativeGame::Oot3dUiProfile::TopScreen
              ? topScreenConfigRuntime
              : nullptr));
  if (launch.UiProfile ==
      Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    applicationSettingsTabs.push_back(
        Oot3dNativeGame::CreateTopScreenSettingsPanel(
            topScreenConfigRuntime));
  }
  Fast::Oot3d::InstallGraphicsSettingsPanelTabs(
      std::move(applicationSettingsTabs));
  InitContextForDemo(hostArgs);
#if defined(__SWITCH__)
  WriteSwitchBootStage("host_context_initialized");
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE) && defined(_WIN32)
  std::string crashDiagnosticsError;
  if (!Oot3dNativeGame::InstallWholeAotCrashDiagnostics(
          saveDataDirectory / "crashes", &crashDiagnosticsError)) {
    throw std::runtime_error("whole-AOT crash diagnostics failed: " +
                             crashDiagnosticsError);
  }
#endif
  auto &window = GetActiveFast3dWindowForDemo();
  if (hostArgs.ThroughputBenchmark) {
    window.SetTargetFps(0);
  }
  auto &api = GetActiveRenderingApiForDemo(window);
  if (launch.PicaParametricTev) {
    if (auto* preparation = dynamic_cast<Fast::Renderer3ds::PicaProgramPreparationBackend*>(&api))
      preparation->PrepareNativePicaPrograms(Oot3dNativeGame::NativePicaVertexProgramFamily());
  }
  api.SetNativePicaGeometryCacheEnabled(
      !launch.DisableOpenGlPicaGeometryCache);
  auto *const titleRenderBackend =
      dynamic_cast<Fast::Oot3d::TitleRenderBackend *>(&api);
  const auto gui =
      Ship::Context::GetRawInstance()->GetWindow()->GetGui();
  if (gui == nullptr) {
    throw std::runtime_error("native PICA host has no renderer GUI");
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("opengl_renderer_initialized");
#endif
  Oot3dNativeGame::Oot3dNativeA32UiTextureProvider nativeUiTextureProvider(
      picaMemoryView, topScreenTextureOverridePackPointer);
  if (hdFontsLoaded) nativeUiTextureProvider.SetGeneratedTextureTransform(
      [&](const Oot3dNativeGame::Oot3dPicaTextureState& texture,
          Oot3dNativeGame::Oot3dPicaResourceSnapshot& resource) {
        hdFonts.Resolve(picaMemoryView, texture, resource);
      });
  Oot3dNativeGame::TopScreenOcarinaTextRuntime ocarinaText(process.Memory(), nativeUiTextureProvider);
  nativeCandidateDispatch.OcarinaText = &ocarinaText;
  widescreenProjection.OcarinaText = &ocarinaText;
  oot3d::ui::Fast3dUiRenderBackend uiRenderBackend(api);
  oot3d::ui::N64UiFast3dRenderer n64UiRenderer(uiRenderBackend,
                                               &ocarinaText);
  std::array<std::vector<oot3d::ui::UiPrimitive>,
             oot3d::ui::kUiSubsystemCount>
      lastTopScreenPrimitivesBySubsystem;
  window.SetTextureFilter(Fast::FILTER_LINEAR);
  const auto inputTimeline = Oot3dNativeGame::NativeA32InputTimeline::LoadFile(
      launch.Host.InputTimelinePath);
#if defined(__SWITCH__)
  WriteSwitchBootStage("ui_runtime_initialized");
#endif

  const auto updateScenePresentation = [&]() {
    widescreenProjection.Projection.Presentation =
        Fast::Oot3d::ResolveScenePresentationPolicy(
            std::max<uint32_t>(1, window.GetWidth()),
            std::max<uint32_t>(1, window.GetHeight()));
    widescreenProjection.Projection.FovMultiplier =
        Fast::Oot3d::GraphicsSettingsRuntime::Instance()
            .Snapshot()
            .FovMultiplier;
  };
  updateScenePresentation();
  process.SetBlockEntryCallback(
      &ApplyNativeWidescreenProjectionPolicy, &widescreenProjection,
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::vector<uint32_t>{}
          : blockEntryHooks);
#if defined(__SWITCH__)
  WriteSwitchBootStage("guest_callbacks_initialized");
#endif

  NativeFramePhaseTiming phaseTiming;
  Oot3dNativeGame::NativeA32InputDiagnostics inputDiagnostics;
  NativeInputConsumerDiagnostics inputConsumerDiagnostics;
  NativeControlPollingState nativeControlPollingState;
  Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache shaderSourceCache;
  shaderSourceCache.TevMode = launch.PicaParametricTev
      ? Oot3dNativeGame::Oot3dPicaTevMode::Parametric
      : Oot3dNativeGame::Oot3dPicaTevMode::Specialized;
  Oot3dNativeGame::Oot3dPicaSemanticTraceWriter picaSemanticTrace(
      launch.PicaSemanticTracePath);
  picaSemanticTrace.BeginSession(
      launch.Host.Renderer,
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming),
      Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile));
  Oot3dNativeGame::NativeA32DspHle dspHle(
      Oot3dNativeGame::BuildNativeA32DspPhysicalRegions(*manifest));
  auto *context = Ship::Context::GetRawInstance();
  auto audio = context != nullptr ? context->GetAudio() : nullptr;
  auto audioPlayer = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
#if defined(__SWITCH__)
  WriteSwitchBootStage("audio_runtime_initialized");
#endif
  auto phaseStart = std::chrono::steady_clock::now();
  Oot3dNativeGame::NativeA32ProcessRunResult processResult;
  uint32_t frameCount = 0;
  uint32_t runFrameCount = 0;
  uint64_t presentationFrameCount = 0;
  uint64_t refreshTickRemainder = 0;
  uint64_t nextVblankTick = 0;
  uint64_t lateVblankDeadlineCount = 0;
  uint64_t maximumVblankDeadlineOvershootTicks = 0;
  uint64_t savestateSaveCount = 0;
  uint64_t savestateLoadCount = 0;
  uint64_t savestateSaveBytes = 0;
  uint64_t savestateLoadBytes = 0;
  double savestateSaveSeconds = 0.0;
  double savestateLoadSeconds = 0.0;
  double savestateCaptureSeconds = 0.0;
  double savestateEncodeSeconds = 0.0;
  double savestateWriteSeconds = 0.0;
  double savestateReadSeconds = 0.0;
  double savestateDecodeSeconds = 0.0;
  double savestateRestoreSeconds = 0.0;
  uint64_t legacySavestateClockRestoreCount = 0;
  uint64_t legacySavestatePresentationSchedulerRestoreCount = 0;
  uint64_t legacySavestateFrameRatePolicyRestoreCount = 0;
  uint64_t legacySavestateTopScreenTemporalRestoreCount = 0;
  uint64_t legacySavestatePicaVisualReplayRestoreCount = 0;
  uint64_t legacySavestatePicaTextureCacheRestoreCount = 0;
  uint64_t legacySavestatePicaColorTargetRestoreCount = 0;
  uint64_t legacySavestatePicaPresentationRestoreCount = 0;
  uint64_t savestatePicaTexturesCaptured = 0;
  uint64_t savestatePicaTextureBytesCaptured = 0;
  uint64_t savestatePicaTexturesRestored = 0;
  uint64_t savestatePicaTextureBytesRestored = 0;
  uint64_t savestatePicaColorTargetsCaptured = 0;
  uint64_t savestatePicaColorTargetBytesCaptured = 0;
  uint64_t savestatePicaColorTargetsRestored = 0;
  uint64_t savestatePicaColorTargetBytesRestored = 0;
  uint64_t savestatePicaDisplayImagesCaptured = 0;
  uint64_t savestatePicaDisplayImageBytesCaptured = 0;
  uint64_t savestatePicaDisplayImagesRestored = 0;
  uint64_t savestatePicaDisplayImageBytesRestored = 0;
  uint64_t savestatePicaVisualReplayBytesCaptured = 0;
  uint64_t savestatePicaVisualReplayBytesRestored = 0;
  uint64_t lastSavestateSemanticFingerprint = 0;
  Oot3dNativeGame::NativePresentationScheduler presentationScheduler(
      frameRateContract.SimulationRateHz,
      frameRateContract.Mode ==
              Oot3dNativeGame::GameplayTimingMode::Enhanced60
          ? Oot3dNativeGame::NativeSimulationCatchUpPolicy::
                PreserveBoundedDebt
          : Oot3dNativeGame::NativeSimulationCatchUpPolicy::DropExcess);
  Oot3dNativeGame::NativeVisualSampleCadence visualSampleCadence;
  Oot3dNativeGame::Oot3dPicaPresentationScheduler
      picaPresentationScheduler;
  Oot3dNativeGame::Oot3dPicaVisualContinuityTracker
      visualContinuityTracker;
  Oot3dNativeGame::Oot3dPicaPreparedVisualTransition
      preparedVisualTransition;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame>
      latestVisualFrame;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame>
      previousVisualFrame;
  bool latestVisualTransitionContinuous = false;
  // One game frame further back (N-2 -> N-1): the presentation clock may
  // still show a moment before the previous frame when a new one arrives.
  Oot3dNativeGame::Oot3dPicaPreparedVisualTransition olderVisualTransition;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame> olderVisualFrame;
  bool olderVisualTransitionContinuous = false;
  uint64_t visualContinuityEpoch = 1U;
  std::map<uint32_t, Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission>
      displayTransfersByOutput;
  uint64_t lastSubmittedDrawId = 0;
  std::unordered_set<uint64_t> pendingTextureCopyCompletions;
  uint64_t lastSelectedTopTransferCompletionId = 0;
  const auto restoreSavestateTiming =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.GameplayClockAvailable) {
          frameRatePolicy.ResetGameplayClock();
          ++legacySavestateClockRestoreCount;
          std::cerr
              << "oot3d_native_game: legacy savestate has no gameplay clock; "
                 "starting at a quiescent logical-frame boundary\n";
        } else {
          if (runtime.GameplayTiming != frameRateContract.Mode) {
            throw std::runtime_error(
                "savestate gameplay timing mode is " +
                std::string(Oot3dNativeGame::GameplayTimingModeName(
                    runtime.GameplayTiming)) +
                ", requested runtime mode is " +
                Oot3dNativeGame::GameplayTimingModeName(
                    frameRateContract.Mode));
          }
          if (!frameRatePolicy.RestoreGameplayClock(runtime.GameplayClock)) {
            throw std::runtime_error(
                "savestate gameplay clock restore failed: " +
                frameRatePolicy.LastError());
          }
        }
        if (!runtime.PresentationSchedulerAvailable) {
          presentationScheduler.Reset();
          ++legacySavestatePresentationSchedulerRestoreCount;
        } else if (!presentationScheduler.Restore(
                       runtime.PresentationScheduler, &error)) {
          throw std::runtime_error(
              "savestate presentation scheduler restore failed: " + error);
        }
        if (!runtime.FrameRatePolicyTemporalStateAvailable) {
          frameRatePolicy.ResetTemporalState();
          ++legacySavestateFrameRatePolicyRestoreCount;
        } else if (!frameRatePolicy.RestoreTemporalState(
                       runtime.FrameRatePolicyTemporalState, &error)) {
          throw std::runtime_error(
              "savestate frame-rate policy restore failed: " + error);
        }
      };
  const auto restoreSavestateTopScreenTemporalState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (launch.UiProfile !=
            Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
          return;
        }
        if (!runtime.TopScreenTemporalStateAvailable ||
            !runtime.TopScreenTemporalState.ProfileActive) {
          ++legacySavestateTopScreenTemporalRestoreCount;
          return;
        }
        const auto &state = runtime.TopScreenTemporalState;
        if (state.ProjectionAvailable) {
          uiLifecycleBridge.TopScreenPauseProjection() = state.Projection;
          uiLifecycleBridge.TopScreenPauseProjection().AlternatePage =
              activeTopScreenConfig.MinimapVisible;
        }
        widescreenProjection.TopScreenPausePageRedrawActive =
            state.PausePageRedrawActive;
        widescreenProjection.TopScreenPausePageRedraw.DelayCalls =
            state.PausePageRedrawDelayCalls;
        widescreenProjection.TopScreenPauseDrawRouting
            .NativeTransitionLatched =
            state.PauseDrawNativeTransitionLatched;
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayArmed =
            state.PauseDrawSuppressionDelayArmed;
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayCommands =
            state.PauseDrawSuppressionDelayCommands;
        nativeCandidateDispatch.TopScreenPauseRoute.PreviousRuntimeMode =
            state.PauseRoutePreviousRuntimeMode;
        nativeCandidateDispatch.TopScreenPauseRoute.TransitionPhase =
            state.PauseRouteTransitionPhase;
        nativeCandidateDispatch.TopScreenPauseRoute.RemainingCalls =
            state.PauseRouteRemainingCalls;
        nativeCandidateDispatch.TopScreenAotPauseRoute.PreviousRuntimeMode =
            state.AotPauseRoutePreviousRuntimeMode;
        nativeCandidateDispatch.TopScreenAotPauseRoute.TransitionPhase =
            state.AotPauseRouteTransitionPhase;
        nativeCandidateDispatch.TopScreenAotPauseRoute.RemainingCalls =
            state.AotPauseRouteRemainingCalls;
        nativeCandidateDispatch.TopScreenTouchCoordinateRoute
            .RuntimeSceneLatch = state.TouchCoordinateRuntimeSceneLatch;
      };
  const auto restoreSavestatePicaTextureCache =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaTextureCacheAvailable) {
          ++legacySavestatePicaTextureCacheRestoreCount;
          return;
        }
        if (!api.RestorePicaTextureCache(
                runtime.PicaTextureCache, &error)) {
          throw std::runtime_error(
              "savestate native PICA texture restore failed: " + error);
        }
        for (const auto &texture : runtime.PicaTextureCache) {
          savestatePicaTextureBytesRestored +=
              texture.PixelBytes.size();
        }
        savestatePicaTexturesRestored +=
            runtime.PicaTextureCache.size();
      };
  const auto restoreSavestatePicaColorTargets =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaColorTargetsAvailable) {
          ++legacySavestatePicaColorTargetRestoreCount;
          return;
        }
        if (!api.RestorePicaColorTargets(
                runtime.PicaColorTargets, &error)) {
          throw std::runtime_error(
              "savestate native PICA color restore failed: " + error);
        }
        std::vector<uint32_t> restoredAddresses;
        restoredAddresses.reserve(runtime.PicaColorTargets.size());
        for (const auto &target : runtime.PicaColorTargets) {
          restoredAddresses.push_back(target.ColorPhysicalAddress);
          savestatePicaColorTargetBytesRestored +=
              target.ColorRgba8.size();
        }
        submissionQueue.RestoreGpuColorRenderTargetOwnership(
            restoredAddresses);
        savestatePicaColorTargetsRestored += restoredAddresses.size();
      };
  const auto restoreSavestatePicaPresentationState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaPresentationStateAvailable) {
          ++legacySavestatePicaPresentationRestoreCount;
          return;
        }
        if (!api.RestorePicaPresentationState(
                runtime.PicaPresentationState, &error)) {
          throw std::runtime_error(
              "savestate native PICA presentation restore failed: " +
              error);
        }
        for (const auto &image :
             runtime.PicaPresentationState.DisplayImages) {
          savestatePicaDisplayImageBytesRestored +=
              image.ColorRgba8.size();
        }
        savestatePicaDisplayImagesRestored +=
            runtime.PicaPresentationState.DisplayImages.size();
      };
  const auto restoreSavestatePicaVisualReplayState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        pendingTextureCopyCompletions.clear();
        if (!runtime.PicaVisualReplayStateAvailable) {
          picaPresentationScheduler.Reset();
          pendingTextureCopyCompletions.clear();
          visualContinuityTracker = {};
          preparedVisualTransition.Reset();
          olderVisualTransition.Reset();
          olderVisualFrame.reset();
          olderVisualTransitionContinuous = false;
          latestVisualFrame.reset();
          previousVisualFrame.reset();
          latestVisualTransitionContinuous = false;
          visualSampleCadence.Reset();
          displayTransfersByOutput.clear();
          lastSubmittedDrawId = 0U;
          lastSelectedTopTransferCompletionId = 0U;
          ++visualContinuityEpoch;
          ++legacySavestatePicaVisualReplayRestoreCount;
          return;
        }
        Oot3dNativeGame::Oot3dPicaVisualReplayState state;
        if (!Oot3dNativeGame::DecodeOot3dPicaVisualReplayState(
                runtime.PicaVisualReplayState, state, &error) ||
            !picaPresentationScheduler.RestoreState(
                std::move(state.Scheduler)) ||
            !visualContinuityTracker.RestoreState(
                std::move(state.Continuity))) {
          throw std::runtime_error(
              "savestate native PICA visual replay restore failed: " +
              error);
        }
        previousVisualFrame = std::move(state.PreviousFrame);
        latestVisualFrame = std::move(state.LatestFrame);
        latestVisualTransitionContinuous =
            state.LatestTransitionContinuous;
        preparedVisualTransition.Reset();
        olderVisualTransition.Reset();
        olderVisualFrame.reset();
        olderVisualTransitionContinuous = false;
        visualSampleCadence.Reset();
        if (latestVisualTransitionContinuous &&
            previousVisualFrame.has_value() &&
            latestVisualFrame.has_value()) {
          latestVisualTransitionContinuous =
              preparedVisualTransition.Prepare(*previousVisualFrame,
                                               *latestVisualFrame);
        }
        ++visualContinuityEpoch;
        displayTransfersByOutput =
            std::move(state.DisplayTransfersByOutput);
        lastSelectedTopTransferCompletionId =
            state.LastSelectedTopTransferCompletionId;
        lastSubmittedDrawId = state.LastSubmittedDrawId;
        savestatePicaVisualReplayBytesRestored +=
            runtime.PicaVisualReplayState.size();
      };
  if (!launch.LoadStatePath.empty()) {
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_savestate_loading");
#endif
    Oot3dNativeGame::NativeA32SavestateRuntimeState restoredRuntime;
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::LoadNativeA32State(
            launch.LoadStatePath, savestateCompatibility, restoredRuntime,
            process, hostServices, picaFrontend, submissionQueue, dspHle,
            &ioResult, &error)) {
      throw std::runtime_error("native savestate load failed: " + error);
    }
    Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
    hdFonts.Reset();
    const auto restoredGravity = hostServices.HidRuntimeProfile().LastAccelerometer;
    nativeControlPollingState.VirtualMotion.RestoreGravity(
        {static_cast<float>(restoredGravity[0]), static_cast<float>(restoredGravity[1]),
         static_cast<float>(restoredGravity[2])});
    nativeControlPollingState.PendingMouseDeltaX = 0;
    nativeControlPollingState.PendingMouseDeltaY = 0;
    nativeControlPollingState.PendingInputSeconds = 0.0;
    picaCompositionTracker.Reset();
    frameCount = restoredRuntime.FrameCount;
    refreshTickRemainder = restoredRuntime.RefreshTickRemainder;
    nextVblankTick = restoredRuntime.NextVblankTick;
    processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
        restoredRuntime.ProcessRunKind);
    restoreSavestateTiming(restoredRuntime);
    applySelectedUiProfile();
    if (!api.ResetPicaState(&error)) {
      throw std::runtime_error(
          "native PICA reset before initial savestate restore failed: " +
          error);
    }
    if (titleRenderBackend != nullptr &&
        !titleRenderBackend->ResetTitleState(&error)) {
      throw std::runtime_error(
          "OOT3D renderer reset before initial savestate restore failed: " +
          error);
    }
    restoreSavestatePicaTextureCache(restoredRuntime);
    restoreSavestatePicaColorTargets(restoredRuntime);
    restoreSavestatePicaPresentationState(restoredRuntime);
    restoreSavestatePicaVisualReplayState(restoredRuntime);
    restoreSavestateTopScreenTemporalState(restoredRuntime);
    ++savestateLoadCount;
    savestateLoadBytes += ioResult.FileBytes;
    savestateLoadSeconds += SecondsSince(phaseStart);
    savestateReadSeconds += ioResult.IoSeconds;
    savestateDecodeSeconds += ioResult.DecodeSeconds;
    savestateRestoreSeconds += ioResult.RestoreSeconds;
    std::cout << "oot3d_native_game: loaded initial state at frame "
              << frameCount << " from " << launch.LoadStatePath.string()
              << '\n';
  } else {
    applySelectedUiProfile();
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_dispatch");
#endif
    processResult =
        RunUntilGuestWait(process, launch.WholeAotBlockBudget);
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_wait");
#endif
    phaseTiming.GuestSeconds += SecondsSince(phaseStart);
    RequireRunnableGuest(processResult);
  }
  uint64_t drawCount = 0;
  uint64_t refreshesWithNativeDraws = 0;
  uint64_t displayTransferCount = 0;
  uint64_t recordedCompletionCount = 0;
  uint64_t completedP3dCount = 0;
  uint64_t completedPpfCount = 0;
  uint64_t picaDrainPassCount = 0;
  uint64_t maximumPicaDrainPassesPerRefresh = 0;
  uint64_t selectedTopTransferCount = 0;
  uint64_t nativeBottomFrontendPresentationCount = 0;
  uint64_t nativeMenuBackdropDrawOmissionCount = 0;
  uint64_t nativeMenuClearDrawOmissionCount = 0;
  uint64_t nativeFrontendPresentationActiveCount = 0;
  uint64_t nativeFrontendBottomTransferHitCount = 0;
  uint64_t visualSnapshotCount = 0;
  uint64_t visualTransitionCount = 0;
  uint64_t visualMatchedDrawCount = 0;
  uint64_t visualStrictUniqueMatchedDrawCount = 0;
  uint64_t visualStrictOrdinalMatchedDrawCount = 0;
  uint64_t visualStructuralOrdinalMatchedDrawCount = 0;
  uint64_t visualPipelineOrdinalMatchedDrawCount = 0;
  uint64_t visualChangedContinuousDrawCount = 0;
  uint64_t visualChangedPerInstanceVertexDrawCount = 0;
  uint64_t visualChangedPerVertexDrawCount = 0;
  Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming visualInterpolationTiming;
  uint64_t visualAmbiguousPreviousDrawCount = 0;
  uint64_t visualAmbiguousCurrentDrawCount = 0;
  uint64_t visualUnmatchedPreviousDrawCount = 0;
  uint64_t visualUnmatchedCurrentDrawCount = 0;
  uint64_t visualSamplesPresented = 0;
  uint64_t visualDirectFramesPresented = 0;
  uint64_t visualReusedSnapshotPresentations = 0;
  uint64_t visualDiscontinuityCount = 0;
  uint64_t visualNamespaceResyncCount = 0;
  uint64_t visualSampleDrawCount = 0;
  uint64_t visualInterpolatedDrawCount = 0;
  uint64_t visualInterpolatedPerInstanceVertexDrawCount = 0;
  uint64_t visualInterpolatedPerVertexDrawCount = 0;
  uint64_t vblankCount = 0;
  uint64_t dspFramesMixed = 0;
  uint64_t dspFramesSuppressed = 0;
  uint64_t dspSamplesMixed = 0;
  uint64_t dspNonzeroSamples = 0;
  uint64_t dspPcmFnv1a64 = 14695981039346656037ULL;
  int32_t dspPeakMagnitude = 0;
  nlohmann::json displayTransferEvents = nlohmann::json::array();
  nlohmann::json drawEvents = nlohmann::json::array();
  nlohmann::json visualTransitionEvents = nlohmann::json::array();
  WindowDemoTimingState timing;
  auto presentationPacingPolicy =
      Fast::Oot3d::ResolvePresentationPacingPolicy(
          Fast::Oot3d::GraphicsSettingsRuntime::Instance()
              .Snapshot()
              .FrameRate);
  const auto resolveFrameCompositionPolicy =
      [&](const Fast::Oot3d::PresentationPacingPolicy& pacing) {
        auto policy = pacing.Composition;
        policy.NativeRateHz = frameRateContract.SimulationRateHz;
        if (!frameRateContract.VisualInterpolationAllowed) {
          policy.Interpolation =
              Fast::Oot3d::NativeVisualInterpolationMode::Disabled;
          policy.FixedSampleMultiplier = 1U;
        }
        return policy;
      };
  auto frameCompositionPolicy =
      resolveFrameCompositionPolicy(presentationPacingPolicy);
  visualSampleCadence.Configure(
      frameCompositionPolicy.FixedMultiplier()
          ? frameCompositionPolicy.FixedSampleMultiplier
          : 0U);
  Oot3dNativeGame::NativeRealtimeRefreshPacer realtimePacer(
      hostArgs.FrameLimit == 0U && !hostArgs.ThroughputBenchmark,
      presentationPacingPolicy.TargetRateHz);
  realtimePacer.Configure(presentationPacingPolicy.Enabled &&
                              !hostArgs.ThroughputBenchmark,
                          presentationPacingPolicy.TargetRateHz);
  bool visualInterpolationEnabled =
      Oot3dNativeGame::ShouldUseNativeVisualInterpolation(
          frameRateContract,
          frameCompositionPolicy.InterpolationEnabled());
  auto lastPresentationTime = std::chrono::steady_clock::now();
  NativeAudioOutputDiagnostics audioOutputDiagnostics;
  Oot3dNativeGame::NativePcmContinuityDiagnostics pcmContinuityDiagnostics;
  std::vector<int16_t> pcmDumpSamples;
  FramebufferScreenshotState screenshotState;
  uint64_t lastPublishedSceneViewSerial = 0;
  uint64_t publishedSceneViewCount = 0;
  const auto publishNativeSceneView = [&]() {
    const auto snapshot = sceneViewProbe.Capture(process.Memory());
    if (!snapshot.has_value() ||
        snapshot->Serial == lastPublishedSceneViewSerial) {
      return;
    }
    auto cameraSnapshot = *snapshot;
    if (manifest->Title.Id == "mm3d" && std::getenv("TRIAEVUM_CLOCK_TRACE") != nullptr) {
      // In-game clock (save.time, CommonData 0x7751D8 + 8 + 0x18) against real time.
      static auto clockStart = std::chrono::steady_clock::now();
      static auto lastPrint = clockStart;
      static uint16_t firstTime = 0;
      static bool haveFirst = false;
      static uint64_t unwrapped = 0;
      static uint16_t lastTime = 0;
      uint16_t timeOfDay = 0;
      if (process.Memory().Read16(0x7751D8U + 0x20U, &timeOfDay)) {
        if (!haveFirst) { haveFirst = true; firstTime = lastTime = timeOfDay; }
        unwrapped += static_cast<uint16_t>(timeOfDay - lastTime);
        lastTime = timeOfDay;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPrint >= std::chrono::seconds(1)) {
          lastPrint = now;
          const double seconds = std::chrono::duration<double>(now - clockStart).count();
          std::fprintf(stderr, "TRIAEVUM_CLOCK real=%.2f time=0x%04X advanced=%llu per_second=%.2f\n",
                       seconds, timeOfDay, static_cast<unsigned long long>(unwrapped), unwrapped / seconds);
        }
      }
    }
    if (manifest->Title.Id == "mm3d") {
      // Tests: TRIAEVUM_POKE=address=value[/bytes],... written once the play
      // state runs (e.g. 0x7751FC=200/2: 200 rupees into the wallet).
      // TRIAEVUM_PEEK=address:length prints guest bytes once in play.
      if (const char *peek = std::getenv("TRIAEVUM_PEEK")) {
        static bool peeked = false;
        auto &memory = process.Memory();
        uint32_t context = 0, game = 0, type = 0;
        if (!peeked && memory.Read32(0x0072F5C0U, &context) && context >= 0x100000U &&
            memory.Read32(context + 0x16CU, &game) && game >= 0x100000U &&
            memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U) {
          peeked = true;
          const uint32_t address = static_cast<uint32_t>(std::strtoul(peek, nullptr, 0));
          const char *colon = std::strchr(peek, ':');
          const uint32_t length = colon != nullptr ? static_cast<uint32_t>(std::strtoul(colon + 1, nullptr, 0)) : 16U;
          std::fprintf(stderr, "TRIAEVUM_PEEK %08X:", address);
          for (uint32_t i = 0; i < length; ++i) {
            uint8_t byte = 0;
            memory.Read8(address + i, &byte);
            std::fprintf(stderr, " %02X", byte);
          }
          std::fprintf(stderr, "\n");
        }
      }
      if (const char *pokes = std::getenv("TRIAEVUM_POKE")) {
        static bool poked = false;
        auto &memory = process.Memory();
        uint32_t context = 0, game = 0, type = 0;
        if (!poked && memory.Read32(0x0072F5C0U, &context) && context >= 0x100000U &&
            memory.Read32(context + 0x16CU, &game) && game >= 0x100000U && (game & 3U) == 0U &&
            memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U) {
          poked = true;
          std::string text(pokes);
          for (size_t start = 0; start < text.size();) {
            const size_t end = std::min(text.find(',', start), text.size());
            const std::string item = text.substr(start, end - start);
            const size_t equals = item.find('='), slash = item.find('/');
            if (equals != std::string::npos) {
              const uint32_t address = static_cast<uint32_t>(std::strtoul(item.substr(0, equals).c_str(), nullptr, 0));
              const uint32_t value = static_cast<uint32_t>(std::strtoul(item.substr(equals + 1).c_str(), nullptr, 0));
              const unsigned bytes = slash != std::string::npos ? std::atoi(item.substr(slash + 1).c_str()) : 1;
              if (bytes == 4) memory.Write32(address, value);
              else if (bytes == 2) memory.Write16(address, static_cast<uint16_t>(value));
              else memory.Write8(address, static_cast<uint8_t>(value));
              std::fprintf(stderr, "TRIAEVUM_POKE %08X=%u/%u\n", address, value, bytes);
            }
            start = end + 1;
          }
        }
      }
      // Test warps (shader tours, scene checks): TRIAEVUM_WARP_ENTRANCES=0x5400,0x...
      // with TRIAEVUM_WARP_SECONDS between them. Same request as the
      // randomizer's: next_entrance (+0xC52E) and the transition trigger (+0xC529).
      static const std::vector<uint16_t> warpEntrances = [] {
        std::vector<uint16_t> list;
        if (const char *value = std::getenv("TRIAEVUM_WARP_ENTRANCES")) {
          std::string text(value);
          for (size_t start = 0; start < text.size();) {
            const size_t end = std::min(text.find(',', start), text.size());
            list.push_back(static_cast<uint16_t>(std::strtoul(text.substr(start, end - start).c_str(), nullptr, 0)));
            start = end + 1;
          }
        }
        return list;
      }();
      if (!warpEntrances.empty()) {
        static const double warpSeconds = [] {
          const char *value = std::getenv("TRIAEVUM_WARP_SECONDS");
          return value != nullptr ? std::atof(value) : 8.0;
        }();
        static size_t nextWarp = 0;
        static auto lastWarp = std::chrono::steady_clock::now();
        auto &memory = process.Memory();
        uint32_t context = 0, game = 0, type = 0;
        if (std::getenv("TRIAEVUM_PLAYER_DEBUG")) {
          static int playerDebug = 0;
          uint32_t c = 0, g = 0, player = 0, flags = 0;
          if ((playerDebug++ % 30) == 0 && memory.Read32(0x0072F5C0U, &c) && c &&
              memory.Read32(c + 0x16CU, &g) && g && memory.Read32(g + 0x20DCU, &player) && player &&
              memory.Read32(player + 0x11DB0U, &flags))
            std::fprintf(stderr, "TRIAEVUM_PLAYER player=%08X flags1=%08X\n", player, flags);
          if ((playerDebug % 30) == 1) {
            uint32_t map0 = 0, map1 = 0, n0 = 0, n1 = 0, radar = 0;
            memory.Read32(0x6890ECU, &map0); memory.Read32(0x6890F0U, &map1);
            memory.Read32(0x727970U + 0x828U, &radar);
            if (map0 >= 0x100000U) memory.Read32(map0 + 0x2C8U, &n0);
            if (map1 >= 0x100000U) memory.Read32(map1 + 0x2C8U, &n1);
            std::fprintf(stderr, "TRIAEVUM_MAPSTATE map0=%08X n0=%u map1=%08X n1=%u radar=%08X\n", map0, n0, map1, n1, radar);
          }
        }
        static int warpDebug = 0;
        if (std::getenv("TRIAEVUM_WARP_DEBUG") && (warpDebug++ % 200) == 0) {
          uint32_t c = 0, g = 0, t = 0;
          memory.Read32(0x0072F5C0U, &c); if (c) memory.Read32(c + 0x16CU, &g); if (g) memory.Read32(g + 0x13CU, &t);
          std::fprintf(stderr, "TRIAEVUM_WARP_DEBUG context=%08X game=%08X type=%08X\n", c, g, t);
        }
        if (nextWarp < warpEntrances.size() &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - lastWarp).count() >= warpSeconds &&
            memory.Read32(0x0072F5C0U, &context) && context >= 0x100000U &&
            memory.Read32(context + 0x16CU, &game) && game >= 0x100000U && (game & 3U) == 0U &&
            memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U) {
          // TRIAEVUM_PLAYER_FORM: 0 Fierce Deity, 1 Goron, 2 Zora, 3 Deku, 4 human
          // (save.player_form; CommonData at 0x7751D8, layout from the randomizer).
          if (const char *form = std::getenv("TRIAEVUM_PLAYER_FORM")) {
            uint16_t timeOfDay = 0;
            uint8_t currentForm = 0;
            memory.Read16(0x7751D8U + 0x20U, &timeOfDay);
            memory.Read8(0x7751D8U + 0x26U, &currentForm);
            std::fprintf(stderr, "TRIAEVUM_PLAYER_FORM time=0x%04X form=%u -> %s\n", timeOfDay, currentForm, form);
            memory.Write8(0x7751D8U + 0x26U, static_cast<uint8_t>(std::atoi(form)));
          }
          memory.Write16(game + 0xC52EU, warpEntrances[nextWarp]);
          memory.Write8(game + 0xC529U, 0x14U);
          std::fprintf(stderr, "TRIAEVUM_WARP entrance=0x%04X index=%zu ms=%lld\n", warpEntrances[nextWarp], nextWarp,
                       static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count()));
          ++nextWarp;
          lastWarp = std::chrono::steady_clock::now();
        }
      }
    }
    if (mm3dWarpRequest) {
      // F1 > Warp: the test warps' request (next_entrance +0xC52E, transition
      // trigger +0xC529), only in the play state.
      auto &memory = process.Memory();
      uint32_t context = 0, game = 0, type = 0;
      const bool inPlay = memory.Read32(0x0072F5C0U, &context) && context >= 0x100000U &&
                          memory.Read32(context + 0x16CU, &game) && game >= 0x100000U && (game & 3U) == 0U &&
                          memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U;
      mm3dWarpRequest->InPlay = inPlay;
      if (mm3dCheats) {
        mm3dCheats->InPlay = inPlay;
        if (inPlay) Oot3dNativeGame::ProcessMm3dSpawn(*mm3dCheats, process, game);
      }
      // TRIAEVUM_F1_WARP_TEST=<entrance>: the F1 warp asked for once in play (tests).
      static bool warpTestQueued = false;
      static const auto warpTestStart = std::chrono::steady_clock::now();
      if (const char *test = std::getenv("TRIAEVUM_F1_WARP_TEST");
          test != nullptr && inPlay && !warpTestQueued &&
          std::chrono::steady_clock::now() - warpTestStart > std::chrono::seconds(4)) {
        warpTestQueued = true;
        mm3dWarpRequest->Entrance = static_cast<int>(std::strtol(test, nullptr, 0));
      }
      const int entrance = mm3dWarpRequest->Entrance.load();
      if (entrance >= 0 && inPlay) {
        const int form = mm3dWarpRequest->Form.exchange(-1);
        if (form >= 0 && form <= 4) memory.Write8(0x7751D8U + 0x26U, static_cast<uint8_t>(form));
        memory.Write16(game + 0xC52EU, static_cast<uint16_t>(entrance));
        memory.Write8(game + 0xC529U, 0x14U);
        mm3dWarpRequest->Entrance = -1;
        std::fprintf(stderr, "oot3d_native_game: warp to entrance 0x%04X (F1)\n", entrance);
      }
    }
    if (manifest->Title.Id == "mm3d" && !cameraSnapshot.CameraAvailable) {
      // Fallback when the frustum hook did not supply the camera (hook off,
      // or TRIAEVUM_MM3D_LATE_CAMERA): read it now, possibly a frame late.
      cameraSnapshot.CameraAvailable =
          ReadMm3dPlayCamera(process.Memory(), cameraSnapshot.Eye, cameraSnapshot.At);
    }
    if (manifest->Title.Id == "mm3d" && cameraSnapshot.CameraAvailable) {
      static FILE *cameraTrace = [] {
        const char *path = std::getenv("TRIAEVUM_CAMERA_TRACE");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
      }();
      if (cameraTrace != nullptr) {
        std::fprintf(cameraTrace, "%llu %.3f %.3f %.3f\n",
                     static_cast<unsigned long long>(snapshot->Serial),
                     cameraSnapshot.At[0], cameraSnapshot.At[1], cameraSnapshot.At[2]);
        std::fflush(cameraTrace);
      }
    }
    if (std::getenv("TRIAEVUM_VIEW_DEBUG")) {
      static int viewDebug = 0;
      if ((viewDebug++ % 120) == 0)
        std::fprintf(stderr, "TRIAEVUM_VIEW fn=%08X l=%.3f r=%.3f b=%.3f t=%.3f n=%.3f f=%.1f eye=%.1f,%.1f,%.1f at=%.1f,%.1f,%.1f cam=%d\n",
                     snapshot->GuestFunction, snapshot->Left, snapshot->Right, snapshot->Bottom, snapshot->Top,
                     snapshot->NearPlane, snapshot->FarPlane, cameraSnapshot.Eye[0], cameraSnapshot.Eye[1],
                     cameraSnapshot.Eye[2], cameraSnapshot.At[0], cameraSnapshot.At[1], cameraSnapshot.At[2],
                     cameraSnapshot.CameraAvailable ? 1 : 0);
    }
    const Fast::Oot3d::TitleSceneViewSubmission view{
        snapshot->GuestFunction,
        snapshot->GuestReturnAddress,
        snapshot->Left,
        snapshot->Right,
        snapshot->Bottom,
        snapshot->Top,
        snapshot->NearPlane,
        snapshot->FarPlane,
        cameraSnapshot.Eye,
        cameraSnapshot.At,
        cameraSnapshot.CameraAvailable,
        picaPresentationScheduler.NextVisualFrameSequence(),
    };
    if (titleRenderBackend != nullptr &&
        titleRenderBackend->PublishSceneView(view)) {
      lastPublishedSceneViewSerial = snapshot->Serial;
      ++publishedSceneViewCount;
    }
  };
  picaPresentationScheduler.SetTimingEnabled(launch.ExtendedDiagnostics);
  api.SetPicaPhysicalMemoryAccess(
      [&](uint32_t address, std::span<uint8_t> bytes) { return picaMemoryView.Read(address, bytes); },
      [&](uint64_t completion, std::span<const Fast::Renderer3ds::PicaPhysicalMemoryWrite> writes) {
        // (A copy already signalled -- into VRAM, which the GPU path could
        // not take -- still lands in guest memory.)
        const bool pending = pendingTextureCopyCompletions.contains(completion);
        for (const auto& write : writes) {
          const auto address = picaMemoryView.Translate(write.Address, write.Bytes.size());
          if (!address || !process.Memory().IsWritable(*address, write.Bytes.size())) return false;
        }
        for (const auto& write : writes) {
          const auto address = picaMemoryView.Translate(write.Address, write.Bytes.size());
          if (!process.Memory().WriteBytes(*address, write.Bytes)) return false;
        }
        if (!pending) return true;
        if (!hostServices.SignalPicaInterrupt(process.Memory(), Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)) return false;
        pendingTextureCopyCompletions.erase(completion);
        ++completedPpfCount;
        ++recordedCompletionCount;
        return true;
      });
  struct PhysicalMemoryAccessLifetime {
    Fast::Renderer3ds::PicaRenderBackend& Backend;
    ~PhysicalMemoryAccessLifetime() { Backend.SetPicaPhysicalMemoryAccess({}, {}); }
  } physicalMemoryAccessLifetime{api};
  std::optional<Fast::Oot3d::NativeFrameTemporalSample> capturedTemporalSample;
  const auto executeVisualSample =
      [&](const Oot3dNativeGame::Oot3dPicaVisualFrameViewSample &sample,
          Oot3dNativeGame::Oot3dPicaPresentationExecutionKind kind,
          bool present,
          const Fast::Oot3d::NativeFrameTemporalSample& temporalSample) {
        const auto started = std::chrono::steady_clock::now();
        auto publishedSample = temporalSample;
        publishedSample.ViewAdjustment = sample.ViewAdjustment;
        publishedSample.ViewAdjustmentValid = sample.ViewAdjustmentValid;
        (void)api.PublishPicaFrameTemporalSample(publishedSample);
        Fast::Oot3d::GrassInteractionBridge::Instance().SelectSample(temporalSample);
        if (!picaPresentationScheduler.Execute(
                api, sample, kVisualInterpolationRenderTargetNamespace, kind,
                present, &error)) {
          throw std::runtime_error(
              "native PICA scheduled presentation failed: " + error);
        }
        const double elapsed = SecondsSince(started);
        if (present) {
          capturedTemporalSample = temporalSample;
        }
        phaseTiming.PicaBackendSeconds += elapsed;
        phaseTiming.VisualReplaySeconds += elapsed;
      };
  Oot3dNativeGame::Oot3dPicaVisualFrameViewSample visualFrameSampleScratch;
  std::vector<Oot3dNativeGame::Oot3dPicaDrawSubmission> drainedDraws;
  bool quickSaveKeyWasDown = false;
  // Screen layout for titles that use the lower screen (F3 cycles it).
  auto screenLayout =
      Oot3dNativeGame::TriAevumScreenLayout::SingleScreen;
  bool lowerOverlay = std::getenv("TRIAEVUM_LOWER_OVERLAY") != nullptr;
  // MM3D: the open UI screen (index into its screen list, see below) and the
  // buttons of the previous guest frame for the menu button combinations.
  // The window library installs its crash handlers by now: report first.
  Oot3dNativeGame::ReinstallTriAevumCrashSignals();
  int mm3dScreen = -1;
  // The lower screen's layouts as of the presented game frame.
  std::vector<std::string> shownLowerLayouts;
  // HUD parts last applied: 0 HUD, 1 HUD with minimap, 2 none (cutscene),
  // 3 the title's start prompt, 4 rupees only (talking).
  int mm3dAppliedHud = -1;
  // The give-an-item menu (MenuItemPresen, touch only on the 3DS): a host
  // cursor over its cells (5 x 4 items and the trade-item column) moved with
  // the D-pad / circle pad; A taps the cell.
  bool mm3dGiveMenu = false;
  int mm3dGiveColumn = 0, mm3dGiveRow = 0;
  uint32_t mm3dGivePreviousButtons = 0;
  bool mm3dGiveStickHeld = false;
  const auto mm3dGiveCell = [](int column, int row) {
    return std::make_pair(static_cast<uint16_t>(column < 5 ? 32 + 48 * column : 290),
                          static_cast<uint16_t>(27 + 48 * row));
  };
  uint32_t mm3dPreviousButtons = 0;
  uint32_t mm3dPreviousTitleActions = 0;
  // MM3D menus composed on one screen (after mm3d-dekui), by UI screen:
  // parts of both screens (lower-screen pixels unless Upper) and whether the
  // frozen gameplay frame covers the upper screen first.
  constexpr int kMm3dFileSelectScreen = 100;  // not in the play state's list
  // The Notebook's schedule view (its lower screen MenuSchedule).
  constexpr int kMm3dScheduleScreen = 102;
  struct Mm3dMenuLayout {
    std::vector<Oot3dNativeGame::TriAevumHudPart> Parts;
    bool FrozenBackground = false;
  };
  using Part = Oot3dNativeGame::TriAevumHudPart;
  // The item slots as TriAevum-style buttons (TRIAEVUM_NATIVE_SLOT_FRAMES=1:
  // the lower screen's own slots instead): the game's button bubble
  // (MakeMm3dHudButtonSprite) with the item icon cut from the lower screen's
  // slot on top, in the HUD's top right and in a row in Items/Masks.
  static const bool mm3dSlotButtonsEnabled = std::getenv("TRIAEVUM_NATIVE_SLOT_FRAMES") == nullptr;
  // Items and Masks: the header across the top, the item slots in a row,
  // the grid and tabs on the left, the item model and description right.
  const Mm3dMenuLayout itemsLayout = [] {
    Mm3dMenuLayout layout{{
        Part{0.0F, 0.0F, 400.0F, 30.0F, 0.0F, 0.0F, 1.0F, true, true},
        Part{0.0F, 0.0F, 258.0F, 202.0F, 0.012F, 0.30F, 0.64F},
        Part{0.0F, 200.0F, 258.0F, 240.0F, 0.012F, -0.01F, 0.64F},
        Part{110.0F, 36.0F, 290.0F, 150.0F, 0.52F, 0.17F, 0.70F, true},
        Part{40.0F, 150.0F, 360.0F, 236.0F, -0.02F, -0.05F, 0.95F, true},
    }, true};
    if (!mm3dSlotButtonsEnabled) {
      layout.Parts.insert(layout.Parts.begin() + 1, {
          Part{265.0F, 0.0F, 320.0F, 56.0F, 0.012F, 0.145F, 0.12F},
          Part{262.0F, 66.0F, 318.0F, 122.0F, 0.087F, 0.145F, 0.12F},
          Part{254.0F, 118.0F, 310.0F, 174.0F, 0.162F, 0.145F, 0.12F},
          Part{265.0F, 184.0F, 320.0F, 240.0F, 0.237F, 0.145F, 0.12F},
      });
    }
    return layout;
  }();
  const std::map<int, Mm3dMenuLayout> mm3dMenuLayouts{
      // Notebook (as in mm3d-dekui): the header across the top, the event
      // list large on the left over the notebook's own background, the
      // character icons, the selected event's card and the buttons on the
      // right.
      {2, {{
          // (one column of the list's dark panel, between the list and its
          // scrollbar: plain in both of its views)
          Part{386.0F, 40.0F, 387.0F, 110.0F, 0.0F, 0.0F, 1.0F, true, false, true},
          Part{0.0F, 0.0F, 400.0F, 30.0F, 0.0F, 0.0F, 1.0F, true, true},
          Part{0.0F, 32.0F, 392.0F, 238.0F, 0.0F, 0.135F, 1.08F, true},
          Part{88.0F, 0.0F, 232.0F, 64.0F, 0.69F, 0.15F, 0.40F},
          Part{0.0F, 70.0F, 320.0F, 200.0F, -0.012F, 0.34F, 0.64F},
          Part{0.0F, 200.0F, 320.0F, 240.0F, -0.012F, -0.01F, 0.64F},
      }, false}},
      // The Notebook's schedule: the three days on the left as the event
      // list; on the right the selected day's hours (where Tatl sets the
      // alarm), the character's or event's card under them and the buttons.
      {kMm3dScheduleScreen, {{
          Part{386.0F, 40.0F, 387.0F, 110.0F, 0.0F, 0.0F, 1.0F, true, false, true},
          Part{0.0F, 0.0F, 400.0F, 30.0F, 0.0F, 0.0F, 1.0F, true, true},
          Part{0.0F, 32.0F, 392.0F, 238.0F, 0.0F, 0.135F, 1.08F, true},
          Part{0.0F, 0.0F, 320.0F, 200.0F, -0.012F, 0.15F, 0.66F},
          Part{0.0F, 200.0F, 320.0F, 240.0F, -0.012F, -0.01F, 0.66F},
      }, false}},
      // Gear: the panel, its column and tabs over the (live) world.
      {3, {{
          Part{0.0F, 0.0F, 258.0F, 200.0F, 0.02F, 0.16F, 0.95F},
          Part{260.0F, 0.0F, 320.0F, 200.0F, 0.564F, 0.16F, 0.22F},
          Part{0.0F, 200.0F, 240.0F, 240.0F, 0.02F, -0.01F, 0.60F},
      }, false}},
      {4, itemsLayout},
      // Map: Termina stays; the area map and its buttons on the right.
      {5, {{
          Part{0.0F, 0.0F, 320.0F, 200.0F, -0.02F, 0.15F, 0.62F},
          Part{0.0F, 200.0F, 320.0F, 240.0F, 0.30F, -0.01F, 0.62F},
      }, false}},
      {6, itemsLayout},
      // File select: the file's card on the left over the purple backdrop,
      // the file list / start prompt on the right.
      {kMm3dFileSelectScreen, {{
          Part{2.0F, 60.0F, 38.0F, 200.0F, 0.0F, 0.0F, 1.0F, true, false, true},
          Part{0.0F, 0.0F, 400.0F, 30.0F, 0.0F, 0.0F, 1.0F, true, true},
          Part{44.0F, 38.0F, 358.0F, 236.0F, 0.02F, 0.16F, 0.95F, true},
          Part{10.0F, 0.0F, 312.0F, 200.0F, -0.02F, 0.17F, 0.78F},
          Part{0.0F, 200.0F, 58.0F, 240.0F, -0.02F, -0.01F, 0.16F},
      }, false}},
  };
  int mergedMenuScreen = -1;
  // Single screen: how far the menu (side by side) / ocarina (inset) layouts
  // have blended in.
  Oot3dNativeGame::TriAevumSingleScreenState singleScreen;
  auto singleScreenBlendTime = std::chrono::steady_clock::now();
  // MM3D lower-screen parts on the upper screen: hearts/magic, rupees, the
  // I/X/Y/II item slots and the area map as a minimap (lower-screen pixels
  // -> upper-screen place). N / ZL+Select show or hide the minimap.
  bool minimapShown = std::getenv("TRIAEVUM_NO_MINIMAP") == nullptr;
  const bool mm3dRestoration = manifest->Title.Variant == "restoration";
  // The slot buttons: the icon box on the lower screen (cut from a HUD layer
  // that leaves out the HUD atlas and the slot frames: the item icons
  // alone), the center in the HUD (upper-screen heights from the right edge
  // / top) and in the Items/Masks row (window heights from the left / top).
  struct Mm3dSlotButton {
    float SourceX0, SourceY0, SourceX1, SourceY1;
    float CenterFromRight, CenterY;
    float MenuCenterX, MenuCenterY;
  };
  static constexpr float kMm3dSlotDiameter = 0.105F;      // upper-screen heights
  static constexpr float kMm3dMenuSlotDiameter = 0.095F;  // window heights
  static constexpr std::array<Mm3dSlotButton, 4> kMm3dSlotButtons{{
      {276.0F, 77.0F, 316.0F, 117.0F, 0.18F, 0.09F, 0.205F, 0.215F},    // top: 3DS X
      {262.0F, 124.0F, 304.0F, 166.0F, 0.29F, 0.185F, 0.33F, 0.215F},   // left: 3DS Y
      {268.0F, 8.0F, 312.0F, 52.0F, 0.07F, 0.185F, 0.08F, 0.215F},      // right: item slot I
      {268.0F, 189.0F, 312.0F, 233.0F, 0.18F, 0.28F, 0.455F, 0.215F},   // bottom: item slot II
  }};
  const auto mm3dHudParts = [mm3dRestoration](bool minimap) {
    std::vector<Oot3dNativeGame::TriAevumHudPart> parts;
    if (mm3dRestoration) {
      // Project Restoration draws the HUD on the upper screen itself.
      if (minimap) {
        parts.push_back({66.0F, 40.0F, 256.0F, 204.0F, -0.015F, -0.16F, 0.36F});
      }
      return parts;
    }
    // Hearts/magic and rupees in the corners, the item slots as a diamond
    // in the top right (X top, Y left, I right, II bottom).
    parts = {
        {64.0F, 0.0F, 256.0F, 38.0F, 0.015F, 0.025F, 0.62F},
        {0.0F, 62.0F, 56.0F, 96.0F, 0.015F, -0.03F, 0.20F},
    };
    if (mm3dSlotButtonsEnabled) {
      // The slots are presented with their circles (icon layer), not as parts.
    } else {
      parts.insert(parts.end(), {
          {262.0F, 66.0F, 318.0F, 122.0F, -0.075F, 0.02F, 0.15F},
          {254.0F, 118.0F, 310.0F, 174.0F, -0.14F, 0.13F, 0.15F},
          {265.0F, 0.0F, 320.0F, 56.0F, -0.012F, 0.13F, 0.15F},
          {265.0F, 184.0F, 320.0F, 240.0F, -0.075F, 0.24F, 0.15F},
      });
    }
    if (minimap) {
      parts.push_back({66.0F, 40.0F, 256.0F, 204.0F, 0.015F, 0.16F, 0.36F});
    }
    return parts;
  };
  if (foreignTitle && manifest->Title.Id == "mm3d") {
    Oot3dNativeGame::SetTriAevumHudParts(mm3dHudParts(minimapShown));
    // The interface is shown as each game frame has it (TRIAEVUM_UI_INTERPOLATION=1: blended).
    Oot3dNativeGame::SetOot3dPicaVisualInterfaceSnaps(std::getenv("TRIAEVUM_UI_INTERPOLATION") == nullptr);
  }
  if (const char *layoutName = std::getenv("TRIAEVUM_SCREEN_LAYOUT")) {
    if (const auto parsed =
            Oot3dNativeGame::ParseTriAevumScreenLayout(layoutName)) {
      screenLayout = *parsed;
    }
  }
  bool screenLayoutKeyWasDown = false;
  Oot3dNativeGame::TriAevumScreenPlacements screenPlacements;
  std::optional<Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission>
      lastLowerScreenTransfer;
  // MM3D: the upper screen's last transfer, and a menu just opened from
  // gameplay (its background is frozen from that frame).
  std::optional<Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission>
      lastTopScreenTransfer;
  bool pauseFreezePending = false;
  bool pauseFrozen = false;
  std::vector<uint32_t> substitutedPauseTextures;
  int previousMm3dScreen = -1;
  // Per lower framebuffer: the guest wrote it on the CPU (a clear copied by
  // the CPU path) more recently than the GPU produced it.
  struct LowerScreenCpuState {
    uint64_t UploadedSerial = 0;
    uint64_t GpuCompletion = 0;
    bool CpuNewest = false;
    bool Uploaded = false;
  };
  std::map<uint32_t, LowerScreenCpuState> lowerScreenCpuStates;
  std::vector<uint8_t> lowerScreenPixels;
  Oot3dNativeGame::SetTriAevumScreenLayoutActive(foreignTitle);
  // MM3D: the lower-screen HUD drawn again into a transparent layer
  // (without panel backgrounds and the map) for the single-screen layout.
  constexpr uint32_t kHudLayerColorAddress = 0x18800000U;
  // The upper screen's menu UI moved into its own layer (merged menus).
  constexpr uint32_t kUpperUiLayerColorAddress = 0x18C00000U;
  bool upperUiLayerEnabled = false;
  // Layer slot 2 holds the lower screen's item icons alone (the HUD atlas
  // with the button labels and the slot frames left out) for the slot buttons.
  constexpr uint32_t kSlotIconLayerColorAddress = 0x18F00000U;
  bool slotIconLayerApplied = false;
  // The lower-screen HUD rule (gameplay exclusions; menus keep item icons).
  Fast::Renderer3ds::PicaHudLayerRule lowerHudRule;
  const bool hudLayerEnabled = foreignTitle && manifest->Title.Id == "mm3d" &&
                               std::getenv("TRIAEVUM_NO_HUD_LAYER") == nullptr;
  if (hudLayerEnabled) {
    Fast::Renderer3ds::PicaHudLayerRule rule;
    rule.Enabled = true;
    rule.SourceColorPhysicalAddress = 0x18237FC0U;
    rule.ViewportX = 0.0F;
    rule.ViewportY = 0.0F;
    rule.ViewportWidth = 240.0F;
    rule.ViewportHeight = 320.0F;
    rule.OverlayColorPhysicalAddress = kHudLayerColorAddress;
    rule.OverlayDepthPhysicalAddress = 0x18900000U;
    // The panel background and the HUD's frames/boxes (hearts, rupees and
    // the item slots float over the game, as in mm3d-dekui).
    std::string excluded = "146825612bb556ca,535a25b48e42e6bf";
    if (const char *value = std::getenv("TRIAEVUM_HUD_EXCLUDE")) {
      excluded = value;
    }
    for (size_t start = 0; start < excluded.size();) {
      const size_t end = std::min(excluded.find(',', start), excluded.size());
      if (end > start) {
        rule.ExcludedTextureHashes.push_back(
            std::stoull(excluded.substr(start, end - start), nullptr, 16));
      }
      start = end + 1;
    }
    api.SetPicaHudLayerRule(rule);
    lowerHudRule = rule;
    if (mm3dSlotButtonsEnabled) {
      rule.Slot = 2U;
      rule.OverlayColorPhysicalAddress = kSlotIconLayerColorAddress;
      rule.OverlayDepthPhysicalAddress = 0x18F80000U;
      rule.ExcludedTextureHashes = {0x146825612BB556CAULL, 0x535A25B48E42E6BFULL,
                                    0x88E403FAEA6381BBULL};  // HUD atlas: labels, hearts, digits
      if (const char *extra = std::getenv("TRIAEVUM_SLOT_ICON_EXCLUDE")) {
        rule.ExcludedTextureHashes.push_back(std::strtoull(extra, nullptr, 16));
      }
      api.SetPicaHudLayerRule(rule);
      slotIconLayerApplied = true;
    }
  }
  // Presented frames since the HUD layer last received a draw: none for a
  // while (cutscenes, the title) hides the HUD parts.
  uint64_t hudLayerLastDrawCount = 0;
  uint32_t hudLayerIdleFrames = 1000U;
  bool quickLoadKeyWasDown = false;
  bool previousSlotKeyWasDown = false;
  bool nextSlotKeyWasDown = false;
  // The slot of a save that could not be taken yet (retried every frame).
  int saveSlotPending = 0;
  bool automatedStateSaved = false;
  std::optional<uint32_t> automatedStateSavedAtRunFrame;
  std::optional<uint32_t> automatedStateSavedAtGuestFrame;

  const auto saveState = [&](const std::filesystem::path &path) {
    // Host-owned continuation registers must never escape into a standalone savestate.
    if (ocarinaText.Pending()) return false;
    const auto started = std::chrono::steady_clock::now();
    Oot3dNativeGame::NativeA32SavestateRuntimeState runtime;
    runtime.FrameCount = frameCount;
    runtime.RefreshTickRemainder = refreshTickRemainder;
    runtime.NextVblankTick = nextVblankTick;
    runtime.ProcessRunKind = static_cast<uint32_t>(processResult.Kind);
    runtime.GameplayClockAvailable = true;
    runtime.GameplayTiming = frameRateContract.Mode;
    runtime.GameplayClock =
        frameRatePolicy.GameplayClock().CaptureState();
    runtime.PresentationSchedulerAvailable = true;
    runtime.PresentationScheduler = presentationScheduler.CaptureState();
    runtime.FrameRatePolicyTemporalStateAvailable = true;
    runtime.FrameRatePolicyTemporalState =
        frameRatePolicy.CaptureTemporalState();
    runtime.TopScreenTemporalStateAvailable = true;
    auto &topScreenState = runtime.TopScreenTemporalState;
    topScreenState.ProjectionAvailable = true;
    topScreenState.Projection = uiLifecycleBridge.TopScreenPauseProjection();
    topScreenState.ProfileActive =
        launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
    topScreenState.PausePageRedrawActive =
        widescreenProjection.TopScreenPausePageRedrawActive;
    topScreenState.PausePageRedrawDelayCalls =
        widescreenProjection.TopScreenPausePageRedraw.DelayCalls;
    topScreenState.PauseDrawNativeTransitionLatched =
        widescreenProjection.TopScreenPauseDrawRouting
            .NativeTransitionLatched;
    topScreenState.PauseDrawSuppressionDelayArmed =
        widescreenProjection.TopScreenPauseDrawRouting.SuppressionDelayArmed;
    topScreenState.PauseDrawSuppressionDelayCommands =
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayCommands;
    topScreenState.PauseRoutePreviousRuntimeMode =
        nativeCandidateDispatch.TopScreenPauseRoute.PreviousRuntimeMode;
    topScreenState.PauseRouteTransitionPhase =
        nativeCandidateDispatch.TopScreenPauseRoute.TransitionPhase;
    topScreenState.PauseRouteRemainingCalls =
        nativeCandidateDispatch.TopScreenPauseRoute.RemainingCalls;
    topScreenState.AotPauseRoutePreviousRuntimeMode =
        nativeCandidateDispatch.TopScreenAotPauseRoute.PreviousRuntimeMode;
    topScreenState.AotPauseRouteTransitionPhase =
        nativeCandidateDispatch.TopScreenAotPauseRoute.TransitionPhase;
    topScreenState.AotPauseRouteRemainingCalls =
        nativeCandidateDispatch.TopScreenAotPauseRoute.RemainingCalls;
    topScreenState.TouchCoordinateRuntimeSceneLatch =
        nativeCandidateDispatch.TopScreenTouchCoordinateRoute
            .RuntimeSceneLatch;
    Oot3dNativeGame::Oot3dPicaVisualReplayState visualReplayState;
    visualReplayState.Scheduler =
        picaPresentationScheduler.CaptureState();
    visualReplayState.Continuity =
        visualContinuityTracker.CaptureState();
    visualReplayState.PreviousFrame = previousVisualFrame;
    visualReplayState.LatestFrame = latestVisualFrame;
    visualReplayState.LatestTransitionContinuous =
        latestVisualTransitionContinuous;
    visualReplayState.DisplayTransfersByOutput =
        displayTransfersByOutput;
    visualReplayState.LastSelectedTopTransferCompletionId =
        lastSelectedTopTransferCompletionId;
    visualReplayState.LastSubmittedDrawId = lastSubmittedDrawId;
    if (!Oot3dNativeGame::EncodeOot3dPicaVisualReplayState(
            visualReplayState, runtime.PicaVisualReplayState, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA visual replay capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaVisualReplayStateAvailable = true;
    savestatePicaVisualReplayBytesCaptured +=
        runtime.PicaVisualReplayState.size();
    if (!api.CapturePicaTextureCache(
            runtime.PicaTextureCache, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA texture capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaTextureCacheAvailable = true;
    for (const auto &texture : runtime.PicaTextureCache) {
      savestatePicaTextureBytesCaptured += texture.PixelBytes.size();
    }
    savestatePicaTexturesCaptured += runtime.PicaTextureCache.size();
    if (!api.CapturePicaColorTargets(
            runtime.PicaColorTargets, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA color capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaColorTargetsAvailable = true;
    for (const auto &target : runtime.PicaColorTargets) {
      savestatePicaColorTargetBytesCaptured +=
          target.ColorRgba8.size();
    }
    savestatePicaColorTargetsCaptured += runtime.PicaColorTargets.size();
    if (!api.CapturePicaPresentationState(
            runtime.PicaPresentationState, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA presentation capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaPresentationStateAvailable = true;
    for (const auto &image :
         runtime.PicaPresentationState.DisplayImages) {
      savestatePicaDisplayImageBytesCaptured +=
          image.ColorRgba8.size();
    }
    savestatePicaDisplayImagesCaptured +=
        runtime.PicaPresentationState.DisplayImages.size();
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::SaveNativeA32State(
            path, savestateCompatibility, runtime, process, hostServices,
            picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
      std::cerr << "oot3d_native_game: savestate save failed: " << error
                << '\n';
      return false;
    }
    ++savestateSaveCount;
    savestateSaveBytes += ioResult.FileBytes;
    lastSavestateSemanticFingerprint = ioResult.SemanticFingerprint;
    savestateSaveSeconds += SecondsSince(started);
    savestateCaptureSeconds += ioResult.CaptureSeconds;
    savestateEncodeSeconds += ioResult.EncodeSeconds;
    savestateWriteSeconds += ioResult.IoSeconds;
    std::cout << "oot3d_native_game: saved state at frame " << frameCount
              << " to " << path.string() << '\n';
    return true;
  };
  const auto loadState = [&](const std::filesystem::path &path) {
    const auto started = std::chrono::steady_clock::now();
    Oot3dNativeGame::NativeA32SavestateRuntimeState runtime;
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::LoadNativeA32State(
            path, savestateCompatibility, runtime, process, hostServices,
            picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
      std::cerr << "oot3d_native_game: savestate load failed: " << error
                << '\n';
      return false;
    }
    Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
    picaCompositionTracker.Reset();
    const auto restoredGravity = hostServices.HidRuntimeProfile().LastAccelerometer;
    nativeControlPollingState.VirtualMotion.RestoreGravity(
        {static_cast<float>(restoredGravity[0]), static_cast<float>(restoredGravity[1]),
         static_cast<float>(restoredGravity[2])});
    nativeControlPollingState.PendingMouseDeltaX = 0;
    nativeControlPollingState.PendingMouseDeltaY = 0;
    nativeControlPollingState.PendingInputSeconds = 0.0;
    applySelectedUiProfile();
    if (!api.ResetPicaState(&error)) {
      throw std::runtime_error(
          "native PICA reset after savestate load failed: " + error);
    }
    if (titleRenderBackend != nullptr &&
        !titleRenderBackend->ResetTitleState(&error)) {
      throw std::runtime_error(
          "OOT3D renderer reset after savestate load failed: " + error);
    }
    restoreSavestatePicaTextureCache(runtime);
    restoreSavestatePicaColorTargets(runtime);
    restoreSavestatePicaPresentationState(runtime);
    if (audioPlayer != nullptr && audioPlayer->IsInitialized()) {
      audioPlayer->Flush();
    }
    frameCount = runtime.FrameCount;
    refreshTickRemainder = runtime.RefreshTickRemainder;
    nextVblankTick = runtime.NextVblankTick;
    processResult = {};
    processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
        runtime.ProcessRunKind);
    restoreSavestateTiming(runtime);
    picaFrontend.SetPacketSink(&submissionQueue);
    uiLifecycleBridge.ResetAfterStateLoad(frameCount);
    hdFonts.Reset();
    ocarinaText.Reset();
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenPauseDrawRouting = {};
    widescreenProjection.TopScreenPausePageRedraw = {};
    widescreenProjection.TopScreenPausePageRedrawActive = false;
    widescreenProjection.TopScreenPausePageCompositionCalls = 0U;
    widescreenProjection.TopScreenPausePageRedraws = 0U;
    widescreenProjection.TopScreenPausePageRedrawSkips = 0U;
    widescreenProjection.PendingTopScreenPauseChildRestore.reset();
    nativeCandidateDispatch.TopScreenInput = {};
    nativeCandidateDispatch.TopScreenInputClock.Reset();
    nativeCandidateDispatch.TopScreenGameplayActionRuntime = {};
    nativeCandidateDispatch.PreviousTopScreenButtons = 0U;
    nativeCandidateDispatch.StartButtonLatch = {};
    nativeCandidateDispatch.TopScreenStartRouting = {};
    nativeCandidateDispatch.TopScreenPauseSystemOpen = {};
    nativeCandidateDispatch.TopScreenViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenViewportDrawArgument = 0U;
    nativeCandidateDispatch.TopScreenSceneViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenSceneViewportDrawArgument = 0U;
    nativeCandidateDispatch.TopScreenOverlayViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
    nativeCandidateDispatch.TopScreenAlternateRendererRestore.reset();
    nativeCandidateDispatch.TopScreenPauseRoute = {};
    nativeCandidateDispatch.TopScreenTouchCoordinateRoute = {};
    nativeCandidateDispatch.TopScreenRendererVisibilityRoute = {};
    nativeCandidateDispatch.TopScreenGameOverDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenPauseController = {};
    nativeCandidateDispatch.TopScreenPauseControllerDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenPauseControllerVisibleRenderers = {};
    nativeCandidateDispatch.TopScreenItems.PendingSelection = 0U;
    nativeCandidateDispatch.TopScreenItems.PreviousSelectionButtons = 0U;
    nativeCandidateDispatch.TopScreenItems.PreviousSelectionPressed = 0U;
    nativeCandidateDispatch.TopScreenCamera = {};
    nativeCandidateDispatch.TopScreenCameraInput.Reset();
    Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
        nativeCandidateDispatch.TopScreenCamera.Camera,
        activeTopScreenConfig);
    nativeCandidateDispatch.TopScreenExtendedCameraActive = false;
    restoreSavestateTopScreenTemporalState(runtime);
    sceneViewProbe.Reset();
    Fast::Oot3d::GrassInteractionBridge::Instance().Reset();
    temporalEventLedger.Reset();
    lastPublishedSceneViewSerial = 0;
    n64UiRenderer.Release();
    picaPresentationScheduler.Reset();
    pendingTextureCopyCompletions.clear();
    visualContinuityTracker = {};
    preparedVisualTransition.Reset();
    olderVisualTransition.Reset();
    olderVisualFrame.reset();
    olderVisualTransitionContinuous = false;
    latestVisualFrame.reset();
    previousVisualFrame.reset();
    latestVisualTransitionContinuous = false;
    visualFrameSampleScratch = {};
    displayTransfersByOutput.clear();
    drainedDraws.clear();
    lastSubmittedDrawId = 0;
    lastSelectedTopTransferCompletionId = 0;
    ++visualContinuityEpoch;
    restoreSavestatePicaVisualReplayState(runtime);
    pcmContinuityDiagnostics = {};
    realtimePacer.ResetDeadline();
    lastPresentationTime = std::chrono::steady_clock::now();
    ++savestateLoadCount;
    savestateLoadBytes += ioResult.FileBytes;
    savestateLoadSeconds += SecondsSince(started);
    savestateReadSeconds += ioResult.IoSeconds;
    savestateDecodeSeconds += ioResult.DecodeSeconds;
    savestateRestoreSeconds += ioResult.RestoreSeconds;
    std::cout << "oot3d_native_game: loaded state at frame " << frameCount
              << " from " << path.string() << '\n';
    return true;
  };

  auto benchmarkMeasurementStart = std::chrono::steady_clock::now();
  auto benchmarkMeasurementEnd = benchmarkMeasurementStart;
  uint64_t benchmarkMeasuredFrames = 0U;
  std::array<double, 15> benchmarkPhaseSeconds{};
  double benchmarkNestedGuestSeconds = 0.0;
  double benchmarkGuestClockSeconds = 0.0;
  double benchmarkUnattributedSeconds = 0.0;
  Fast::Renderer::FrameTimeDistribution preBackendTimes;
  Fast::Renderer::FrameTimeDistribution preBackendEnvelopeTimes;
  uint64_t invalidPreBackendIntervals = 0;
  Fast::Renderer::SlowFrameSamples<15> slowFrames;
  const char* pacingTrace = std::getenv("TRIAEVUM_PACING_TRACE");
  const bool measurePacing = pacingTrace && std::string_view(pacingTrace) == "1";
  auto benchmarkFrameTimes = (hostArgs.ThroughputBenchmark || measurePacing)
      ? std::make_unique<Fast::Renderer::FrameTimeDistribution>() : nullptr;
  bool benchmarkMeasurementStarted =
      hostArgs.BenchmarkWarmupFrames == 0U;
  const char* cpuProbeEnvironment = std::getenv("TRIAEVUM_PROFILE_CPU_LEDGER");
  const bool cpuProbeRequested = CpuProbe::BuildEnabled && cpuProbeEnvironment &&
      std::string_view(cpuProbeEnvironment) == "1";
  CpuProbe::Totals benchmarkCpuPhases{};
  if (cpuProbeRequested && benchmarkMeasurementStarted)
    CpuProbe::Thread.StartAt(CpuProbe::Read());

#if defined(__SWITCH__)
  WriteSwitchBootStage("runtime_loop");
#endif
  while (window.IsRunning() &&
         processResult.Kind !=
             Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    const auto phaseBeforeFrame = phaseTiming.Values();
    const double nestedGuestBeforeFrame = phaseTiming.GuestInsidePicaSubmitSeconds;
    const double guestClockBeforeFrame = phaseTiming.GuestClockSeconds;
    widescreenProjection.CurrentHostFrame =
        static_cast<uint32_t>(presentationFrameCount);
    WindowDemoFrameTiming frameTiming;
    if (!PrepareNextWindowDemoFrame(hostArgs, window, timing, frameTiming)) {
      continue;
    }
    const auto requestedPresentationPolicy =
        Fast::Oot3d::ResolvePresentationPacingPolicy(
            Fast::Oot3d::GraphicsSettingsRuntime::Instance()
                .Snapshot()
                .FrameRate);
    if (Fast::Oot3d::PresentationPacingPolicyChanged(
            presentationPacingPolicy, requestedPresentationPolicy)) {
      const auto requestedCompositionPolicy =
          resolveFrameCompositionPolicy(requestedPresentationPolicy);
      const bool previousInterpolationEnabled = visualInterpolationEnabled;
      const bool requestedInterpolationEnabled =
          Oot3dNativeGame::ShouldUseNativeVisualInterpolation(
              frameRateContract,
              requestedCompositionPolicy.InterpolationEnabled());
      const bool compositionModeChanged =
          frameCompositionPolicy.Interpolation !=
              requestedCompositionPolicy.Interpolation ||
          frameCompositionPolicy.FixedSampleMultiplier !=
              requestedCompositionPolicy.FixedSampleMultiplier;
      presentationPacingPolicy = requestedPresentationPolicy;
      frameCompositionPolicy = requestedCompositionPolicy;
      realtimePacer.Configure(presentationPacingPolicy.Enabled &&
                                  !hostArgs.ThroughputBenchmark,
                              presentationPacingPolicy.TargetRateHz);
      visualSampleCadence.Configure(
          frameCompositionPolicy.FixedMultiplier()
              ? frameCompositionPolicy.FixedSampleMultiplier
              : 0U);
      visualInterpolationEnabled = requestedInterpolationEnabled;
      if (compositionModeChanged) {
        ++visualContinuityEpoch;
      }
      if (previousInterpolationEnabled != visualInterpolationEnabled) {
        preparedVisualTransition.Reset();
        olderVisualTransition.Reset();
        olderVisualFrame.reset();
        olderVisualTransitionContinuous = false;
        visualContinuityTracker = {};
        previousVisualFrame.reset();
        latestVisualFrame.reset();
        latestVisualTransitionContinuous = false;
        visualSampleCadence.Reset();
      }
    }
    const auto presentationTime = std::chrono::steady_clock::now();
    realtimePacer.ObservePresentation(presentationTime);
    const double presentationElapsedSeconds =
        hostArgs.FixedDeltaSeconds > 0.0
            ? hostArgs.FixedDeltaSeconds
            : std::chrono::duration<double>(presentationTime -
                                            lastPresentationTime)
                  .count();
    lastPresentationTime = presentationTime;
    // TRIAEVUM_FRAME_TRACE=<file>: per interpolated sample, the wall clock and
    // the game moment shown (to measure smoothness).
    static FILE *frameTrace = [] {
      const char *path = std::getenv("TRIAEVUM_FRAME_TRACE");
      return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    const auto traceSample = [&](uint64_t sequence, float alpha) {
      if (frameTrace != nullptr) {
        std::fprintf(frameTrace, "%lld %llu %.6f\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count()),
                     static_cast<unsigned long long>(sequence), alpha);
      }
    };
    const auto presentationStateBeforeFrame = presentationScheduler.CaptureState();
    const auto presentationStep =
        presentationScheduler.Advance(presentationElapsedSeconds);
    const uint32_t guestRefreshesDue = presentationStep.GuestRefreshesDue;
    const float visualInterpolationAlpha = static_cast<float>(
        std::clamp(presentationStep.InterpolationAlpha, 0.0, 1.0));
    // Uncapped: the sample shows the moment it is drawn, measured from when
    // the newest game frame actually arrived. (The scheduler's own visual
    // clock is not aligned with the guest's frames -- MM3D renders every
    // second refresh -- and reported 1.0 one sample after each new frame,
    // then fell back: a jump forward and back 30 times a second.) Arrivals
    // jitter by the guest's CPU time, so their clock is smoothed.
    static const bool drawTimeAlphaDisabled = std::getenv("TRIAEVUM_NO_DRAWTIME_ALPHA") != nullptr;
    static std::optional<std::chrono::steady_clock::time_point> visualFrameClock;
    // A fixed host step (tests, captures) runs the same clock on simulated
    // time, so captured in-between frames show what a real run shows.
    static std::chrono::steady_clock::duration simulatedHostTime{};
    if (hostArgs.FixedDeltaSeconds > 0.0) {
      simulatedHostTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(hostArgs.FixedDeltaSeconds));
    }
    const auto hostNow = [&] {
      return hostArgs.FixedDeltaSeconds > 0.0 ? std::chrono::steady_clock::time_point(simulatedHostTime)
                                              : std::chrono::steady_clock::now();
    };
    const bool arrivalClockAlpha =
        !drawTimeAlphaDisabled && frameCompositionPolicy.InterpolationEnabled() &&
        !frameCompositionPolicy.FixedMultiplier() && frameRateContract.StepSeconds > 0.0;
    // A sample shows the moment it reaches the screen, not the moment it is
    // computed. A game frame runs inside one host frame (that one takes ~14
    // ms, the next ones catch up in 3-7 ms), yet with vsync the images still
    // show one per refresh: timed by when they were computed, the motion
    // jerked 30 times a second (most visible turning the camera). The
    // presentation clock advances by the mean sample interval each sample,
    // pulled gently toward real time, and starts again after a real stall.
    static const bool presentationClockDisabled =
        std::getenv("TRIAEVUM_NO_PRESENTATION_CLOCK") != nullptr;
    static std::optional<std::chrono::steady_clock::time_point> presentationClock;
    static std::optional<std::chrono::steady_clock::time_point> lastPresentationSample;
    static double presentationInterval = 0.0;
    static uint64_t presentationClockFrame = ~0ULL;
    static std::chrono::steady_clock::time_point presentationClockSample{};
    const auto presentationNow = [&] {
      if (presentationClockFrame == presentationFrameCount) return presentationClockSample;
      presentationClockFrame = presentationFrameCount;
      presentationClockSample = [&] {
      const auto now = hostNow();
      if (presentationClockDisabled || hostArgs.FixedDeltaSeconds > 0.0) {
        return now;
      }
      if (lastPresentationSample.has_value()) {
        const double gap = std::chrono::duration<double>(now - *lastPresentationSample).count();
        if (gap > 0.0 && gap < 0.05) {
          presentationInterval = presentationInterval > 0.0
                                     ? presentationInterval * 0.97 + gap * 0.03
                                     : gap;
        }
      }
      lastPresentationSample = now;
      if (!presentationClock.has_value() || presentationInterval <= 0.0) {
        presentationClock = now;
        return now;
      }
      const auto predicted =
          *presentationClock + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                   std::chrono::duration<double>(presentationInterval));
      const double error = std::chrono::duration<double>(now - predicted).count();
      presentationClock =
          std::abs(error) > 0.025
              ? now
              : predicted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(error * 0.05));
      return *presentationClock;
      }();
      return presentationClockSample;
    };
    const auto observeVisualFrameArrival = [&] {
      const auto now = presentationNow();
      const auto step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(frameRateContract.StepSeconds));
      if (!visualFrameClock.has_value()) {
        visualFrameClock = now;
        return;
      }
      const auto predicted = *visualFrameClock + step;
      const auto error = now - predicted;
      if (error > 2 * step || error < -2 * step) {
        visualFrameClock = now;  // a stall or a skip: start again
      } else {
        visualFrameClock = predicted + error * 15 / 100;
      }
    };
    const auto drawTimeInterpolationAlpha = [&] {
      if (!arrivalClockAlpha || !visualFrameClock.has_value()) {
        return visualInterpolationAlpha;
      }
      // One sample interval later than the newest frame's arrival: a frame
      // is noticed at the first sample after it is ready, and the older
      // pair covers the moments before the previous frame (alpha below 0).
      const double margin =
          presentationClockDisabled || hostArgs.FixedDeltaSeconds > 0.0 ? 0.0 : presentationInterval;
      const double since = std::chrono::duration<double>(
          presentationNow() - *visualFrameClock).count() - margin;
      return static_cast<float>(std::clamp(since / frameRateContract.StepSeconds, -1.0, 1.0));
    };
    applyTopScreenConfigSnapshot();
    uiLifecycleBridge.BeginHostFrame(
        static_cast<uint32_t>(presentationFrameCount));
    if (topScreenTextureOverrideRuntime.has_value()) {
      topScreenTextureOverrideRuntime->Observe(uiLifecycleBridge);
    }

    const uint32_t width = std::max<uint32_t>(1, window.GetWidth());
    const uint32_t height = std::max<uint32_t>(1, window.GetHeight());
    hdFonts.SetOutputHeight(height);
    updateScenePresentation();
    phaseStart = std::chrono::steady_clock::now();
    window.GetMouseStateManager()->StartFrame();
    gui->StartDraw();
    window.StartFrame();
    phaseTiming.HostFrameStartSeconds += SecondsSince(phaseStart);
    const auto inputPhaseStart = std::chrono::steady_clock::now();
    const bool quickSaveKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F5);
    const bool quickLoadKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F8);
    // F5 / F8 save / load the selected slot, F6 / F7 select the previous / next one.
    const bool previousSlotKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F6);
    const bool nextSlotKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F7);
    if (quickSaveKeyDown && !quickSaveKeyWasDown) saveStateSlots->RequestSave(saveStateSlots->Selected());
    if (quickLoadKeyDown && !quickLoadKeyWasDown) saveStateSlots->RequestLoad(saveStateSlots->Selected());
    if ((previousSlotKeyDown && !previousSlotKeyWasDown) || (nextSlotKeyDown && !nextSlotKeyWasDown)) {
      constexpr int slots = Oot3dNativeGame::SaveStateSlots::kSlots;
      const int slot = (saveStateSlots->Selected() - 1 + (nextSlotKeyDown ? 1 : slots - 1)) % slots + 1;
      saveStateSlots->Select(slot);
      std::error_code ignored;
      saveStateSlots->Report("Slot " + std::to_string(slot) +
                             (std::filesystem::exists(saveStateSlots->Path(slot), ignored) ? "" : " (empty)"));
    }
    // TRIAEVUM_SAVESTATE_TEST=s3@600,l3@900: save / load a slot at those host frames.
    static const std::vector<std::tuple<uint64_t, char, int>> savestateTest = [] {
      std::vector<std::tuple<uint64_t, char, int>> steps;
      if (const char *list = std::getenv("TRIAEVUM_SAVESTATE_TEST")) {
        std::stringstream stream(list);
        std::string item;
        while (std::getline(stream, item, ',')) {
          const auto at = item.find('@');
          if (item.size() < 4 || at == std::string::npos) continue;
          steps.emplace_back(std::strtoull(item.c_str() + at + 1, nullptr, 10), item[0],
                             std::atoi(item.substr(1, at - 1).c_str()));
        }
      }
      return steps;
    }();
    for (const auto &[frame, kind, slot] : savestateTest) {
      if (frame != presentationFrameCount) continue;
      if (kind == 's') saveStateSlots->RequestSave(slot);
      if (kind == 'l') saveStateSlots->RequestLoad(slot);
    }
    quickSaveKeyWasDown = quickSaveKeyDown;
    quickLoadKeyWasDown = quickLoadKeyDown;
    previousSlotKeyWasDown = previousSlotKeyDown;
    nextSlotKeyWasDown = nextSlotKeyDown;
    if (foreignTitle) {
      const bool screenLayoutKeyDown =
          window.IsKeyDown(Ship::KbScancode::LUS_KB_F3);
      if (screenLayoutKeyDown && !screenLayoutKeyWasDown) {
        screenLayout = Oot3dNativeGame::NextTriAevumScreenLayout(screenLayout);
      }
      screenLayoutKeyWasDown = screenLayoutKeyDown;
      // Single screen: F4 shows/hides the whole lower screen. (The
      // touch-screen buttons are bindable controls, see ApplyMm3dMenuButtons.)
      static bool overlayKeyWasDown = false;
      {
        // F9 drops a marker into the fling trace (TRIAEVUM_FLING_TRACE).
        static bool markKeyWasDown = false;
        const bool markKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F9);
        if (markKeyDown && !markKeyWasDown) Oot3dNativeGame::FlingTraceMark();
        markKeyWasDown = markKeyDown;
      }
      const bool overlayKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F4);
      if (overlayKeyDown && !overlayKeyWasDown && !gui->GetMenuOrMenubarVisible()) {
        lowerOverlay = !lowerOverlay;
      }
      overlayKeyWasDown = overlayKeyDown;
      // MM3D's open UI screen (Main, Notebook, Gear, Items, Map, Masks,
      // Ocarina): its ScreenContext at 0x72F5BC (+8 the active screen, +C one
      // being opened) and the screen objects listed at 0x656B00 (v1.1, from
      // Project Restoration's game/ui.cpp).
      mm3dScreen = -1;
      // A menu (Notebook..ocarina) being opened, not yet active.
      bool menuOpening = false;
      shownLowerLayouts = widescreenProjection.LowerLayoutNames;
      bool menuLayout = false;
      bool insetLayout = false;
      bool fileSelectShown = false;
      bool titleShown = false;
      if (manifest->Title.Id == "mm3d") {
        uint32_t activeScreen = 0;
        uint32_t newScreen = 0;
        if (process.Memory().Read32(0x0072F5C4U, &activeScreen) &&
            process.Memory().Read32(0x0072F5C8U, &newScreen)) {
          // While a menu closes the gameplay screen is already the new one:
          // the menu stays laid out until it has closed.
          uint32_t mainScreen = 0;
          const bool closingMenu =
              newScreen != 0U && process.Memory().Read32(0x00656B04U, &mainScreen) &&
              newScreen == mainScreen && [&] {
                for (uint32_t index = 2; index < 8U; ++index) {
                  uint32_t screen = 0;
                  if (process.Memory().Read32(0x00656B00U + index * 4U, &screen) &&
                      screen == activeScreen) {
                    return true;
                  }
                }
                return false;
              }();
          // A screen being opened is laid out once it is active: until then
          // the guest still draws the previous one (Items: 14 game frames,
          // the Notebook 10), and the menu's parts cut from gameplay drew a
          // stretched, distorted frame (TRIAEVUM_LAYOUT_ON_OPEN=1: as soon
          // as it is being opened).
          static const bool layoutOnOpen = std::getenv("TRIAEVUM_LAYOUT_ON_OPEN") != nullptr;
          for (uint32_t index = 2; index < 8U && newScreen != 0U && !closingMenu; ++index) {
            uint32_t screen = 0;
            menuOpening = menuOpening ||
                          (process.Memory().Read32(0x00656B00U + index * 4U, &screen) && screen == newScreen &&
                           newScreen != activeScreen);
          }
          const uint32_t shown =
              newScreen != 0U && !closingMenu && layoutOnOpen ? newScreen : activeScreen;
          // Title (intro, attract and logo): of the lower screen only its
          // "PRESS START", under the logo where the gameplay HUD parts would
          // have cut it into the minimap's corner.
          constexpr uint32_t kTitleScreen = 0x006F7FBCU;
          titleShown = shown == kTitleScreen;
          if (titleShown && mm3dAppliedHud != 3 &&
              std::getenv("TRIAEVUM_KEEP_HUD") == nullptr) {
            mm3dAppliedHud = 3;
            Oot3dNativeGame::SetTriAevumHudParts(
                {{76.0F, 114.0F, 244.0F, 142.0F, 0.40F, 0.732F, 0.36F}});
          }
          // File select (outside the play state, not in the list).
          constexpr uint32_t kFileSelectScreen = 0x006F79DCU;
          menuLayout = shown == kFileSelectScreen;
          fileSelectShown = menuLayout;
          for (uint32_t index = 0; index < 8U && shown != 0U; ++index) {
            uint32_t screen = 0;
            if (process.Memory().Read32(0x00656B00U + index * 4U, &screen) &&
                screen == shown) {
              mm3dScreen = static_cast<int>(index);
              break;
            }
          }
        }
        // The picture shows the last finished game frame (blending toward
        // it) while the guest already runs the next: what is read from guest
        // memory now belongs to the frame being produced. The screen state
        // (menus, title, file select, lower-screen layouts) is applied once
        // that frame is presented; read live it laid a menu out over the last
        // gameplay frame (its parts cut from the world: a distorted frame).
        {
          struct ScreenObservation {
            uint64_t Sequence;
            int Screen;
            bool Opening, Title, FileSelect;
            std::vector<std::string> Lower;
          };
          static std::deque<ScreenObservation> observations;
          static const bool live = std::getenv("TRIAEVUM_LIVE_SCREEN_STATE") != nullptr;
          const uint64_t presented = latestVisualFrame.has_value() ? latestVisualFrame->Sequence : 0U;
          if (!observations.empty() && observations.back().Sequence > presented + 1U) {
            observations.clear();  // the frame numbering restarted (state load)
          }
          if (!observations.empty() && observations.back().Sequence == presented + 1U) {
            observations.pop_back();  // a newer reading of the same frame
          }
          observations.push_back({presented + 1U, mm3dScreen, menuOpening, titleShown, fileSelectShown,
                                  widescreenProjection.LowerLayoutNames});
          while (observations.size() > 1U && observations[1].Sequence <= presented) observations.pop_front();
          while (observations.size() > 8U) observations.pop_front();
          if (!live && manifest->Title.Id == "mm3d") {
            const auto &shown = observations.front();
            mm3dScreen = shown.Screen;
            menuOpening = shown.Opening;
            titleShown = shown.Title;
            fileSelectShown = shown.FileSelect;
            shownLowerLayouts = shown.Lower;
          }
        }
        // Gameplay HUD: hidden while Link is held in a cutscene (frozen, dead,
        // time stopped: MM's Player_InBlockingCsMode flags), and the minimap
        // only where the lower screen shows an area map (a 512x1024 map sheet;
        // without the area's map it shows the region's emblem instead).
        // The Song of Time's return to the first day: the lower screen shows
        // the items kept (ReturnToStart) through the question and the fall,
        // and the HUD parts cut that into the corners (an item grid).
        const bool returnToStart =
            std::find(shownLowerLayouts.begin(), shownLowerLayouts.end(), "ReturnToStart") != shownLowerLayouts.end();
        if (returnToStart && !mm3dRestoration && mm3dAppliedHud != 2 &&
            std::getenv("TRIAEVUM_KEEP_HUD") == nullptr) {
          mm3dAppliedHud = 2;
          Oot3dNativeGame::SetTriAevumHudParts({});
        }
        if (mm3dScreen == 1 && !mm3dRestoration && !returnToStart) {
          auto &memory = process.Memory();
          uint32_t context = 0, game = 0, type = 0, player = 0, flags1 = 0;
          bool cutscene = false;
          bool talking = false;
          if (memory.Read32(0x0072F5C0U, &context) && context >= 0x100000U &&
              memory.Read32(context + 0x16CU, &game) && game >= 0x100000U &&
              memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U &&
              memory.Read32(game + 0x20DCU, &player) && player >= 0x100000U &&
              memory.Read32(player + 0x11DB0U, &flags1)) {
            cutscene = (flags1 & 0x20000280U) != 0U;
            // Talking (Player state 0x40): the 3DS keeps the lower screen's
            // rupee counter in view (the bank, shops, bets).
            talking = (flags1 & 0x40U) != 0U;
          }
          // Real time, not frames: uncapped presentation redraws the lower
          // screen only some frames, which blinked the minimap off and on.
          const auto now = std::chrono::steady_clock::now();
          static auto lastMapSeen = now - std::chrono::seconds(10);
          static auto lastBannerSeen = now - std::chrono::seconds(10);
          if (lastLowerScreenTransfer.has_value() &&
              Fast::Renderer3ds::PicaTargetSampledTextureSize(
                  lastLowerScreenTransfer->InputPhysicalAddress, 512U, 1024U))
            lastMapSeen = now;
          // The area-name banner (its 256x256 backdrop) sits where the minimap is.
          if (lastTopScreenTransfer.has_value() &&
              Fast::Renderer3ds::PicaTargetSampledTextureHash(
                  lastTopScreenTransfer->InputPhysicalAddress, 0x2b9a2781daaeb134ULL))
            lastBannerSeen = now;
          const bool mapAvailable = now - lastMapSeen < std::chrono::milliseconds(500) &&
                                    now - lastBannerSeen > std::chrono::milliseconds(300);
          const int hud = cutscene ? (talking ? 4 : 2) : (minimapShown && mapAvailable ? 1 : 0);
          if (hud != mm3dAppliedHud && std::getenv("TRIAEVUM_KEEP_HUD") == nullptr) {
            mm3dAppliedHud = hud;
            Oot3dNativeGame::FlingTraceNote(hud == 2 ? "HUD hidden (cutscene)" : hud == 1 ? "HUD with minimap" : "HUD without minimap");
            if (std::getenv("TRIAEVUM_SCREEN_DEBUG"))
              std::fprintf(stderr, "[hud] %d flags1 %08x\n", hud, flags1);
            Oot3dNativeGame::SetTriAevumHudParts(
                hud == 2   ? std::vector<Oot3dNativeGame::TriAevumHudPart>{}
                : hud == 4 ? std::vector<Oot3dNativeGame::TriAevumHudPart>{mm3dHudParts(false)[1]}
                           : mm3dHudParts(hud == 1));
          }
        }
        if ((mm3dScreen == 4 || mm3dScreen == 6) && std::getenv("TRIAEVUM_MENU_DEBUG")) {
          uint32_t object = 0, state = 0;
          static uint32_t lastState = ~0U;
          if (process.Memory().Read32(0x00656B00U + mm3dScreen * 4U, &object) && object &&
              process.Memory().Read32(object + 0x18U, &state) && state != lastState) {
            lastState = state;
            std::fprintf(stderr, "TRIAEVUM_MENU screen=%d object=%08X state=%08X\n", mm3dScreen, object, state);
          }
        }
        // The lower screen's layouts, logged when they change (names of UIs
        // still to lay out on one screen show up in logs/last_run.log).
        static std::vector<std::string> loggedLowerLayouts;
        if (widescreenProjection.LowerLayoutNames != loggedLowerLayouts) {
          loggedLowerLayouts = widescreenProjection.LowerLayoutNames;
          std::string joined;
          for (const auto &name : loggedLowerLayouts) joined += (joined.empty() ? "" : ",") + name;
          std::fprintf(stderr, "[layouts] frame %u screen %d lower: %s\n", frameCount, mm3dScreen,
                       joined.c_str());
        }
        static uint64_t loggedScreen = ~0ULL;
        const uint64_t screenKey = (static_cast<uint64_t>(activeScreen) << 32U) | newScreen;
        if (screenKey != loggedScreen && std::getenv("TRIAEVUM_SCREEN_DEBUG")) {
          std::fprintf(stderr, "[screen] frame %llu screen %d active %08x new %08x\n",
                       static_cast<unsigned long long>(frameCount), mm3dScreen,
                       activeScreen, newScreen);
          loggedScreen = screenKey;
        }
        // Notebook, Gear, Items, Map, Masks: both screens side by side;
        // ocarina: its song list as an inset. Only while the lower screen
        // draws something (not on the black "Dawn of" screen).
        if (widescreenProjection.PauseCaptured) {
          widescreenProjection.PauseCaptured = false;
          pauseFreezePending = true;
          if (std::getenv("TRIAEVUM_SCREEN_DEBUG"))
            std::fprintf(stderr, "[pause] captured frame %llu textures %zu\n",
                         static_cast<unsigned long long>(frameCount), widescreenProjection.PauseTextures.size());
        }
        previousMm3dScreen = mm3dScreen;
        static uint32_t contextFrame = 0;
        if (++contextFrame % 30U == 0U) {
          static constexpr std::array<const char *, 8> kScreenNames{
              "black/loading", "gameplay", "Bombers' Notebook", "Gear", "Items", "Map",
              "Masks", "ocarina"};
          std::array<char, 256> context{};
          std::snprintf(context.data(), context.size(),
                        "guest frame %u, presented frame %llu, screen %s, layout %s%s",
                        frameCount, static_cast<unsigned long long>(presentationFrameCount),
                        mm3dScreen >= 0 && mm3dScreen < 8 ? kScreenNames[mm3dScreen]
                        : fileSelectShown                 ? "file select"
                                                          : "title/other",
                        std::string(Oot3dNativeGame::TriAevumScreenLayoutName(screenLayout)).c_str(),
                        singleScreen.MergedMenu ? " (menu on one screen)" : "");
          Oot3dNativeGame::SetTriAevumCrashContext(context.data());
        }
        const bool lowerDrawing = hudLayerIdleFrames < 30U;
        menuLayout = lowerDrawing &&
                     (menuLayout || (mm3dScreen >= 2 && mm3dScreen <= 6));
        // In gameplay the lower screen queues only its touch panel; any other
        // layout there is a lower-screen dialog (give an item/mask/photo,
        // Song of Double Time (no UI screen of the list), the bank's amount,
        // ...): shown as an inset like the ocarina's, to be read and clicked.
        const auto &lowerLayouts = shownLowerLayouts;
        // (Not while a menu opens: the lower screen already shows the menu
        // before it is active.)
        const bool lowerDialog =
            (mm3dScreen == 1 || mm3dScreen == -1) && !titleShown && !fileSelectShown && !menuOpening &&
            !lowerLayouts.empty() &&
            std::any_of(lowerLayouts.begin(), lowerLayouts.end(),
                        [](const std::string &name) {
                          // (The Song of Time's "return to the first day" shows
                          // the items kept on the lower screen: only
                          // information, and over the question's box.)
                          return name != "TouchPanel" && name != "ReturnToStart" && !name.empty();
                        }) &&
            std::getenv("TRIAEVUM_NO_LOWER_DIALOG_INSET") == nullptr;
        insetLayout = lowerDrawing && (mm3dScreen == 7 || lowerDialog);
        // Items and Masks are composed on one screen.
        static const bool mergedMenus = std::getenv("TRIAEVUM_SIDE_BY_SIDE_MENUS") == nullptr;
        const bool scheduleShown =
            mm3dScreen == 2 && std::find(lowerLayouts.begin(), lowerLayouts.end(), "MenuSchedule") != lowerLayouts.end();
        const auto menuLayoutFound = mm3dMenuLayouts.find(
            fileSelectShown ? kMm3dFileSelectScreen : scheduleShown ? kMm3dScheduleScreen : mm3dScreen);
        singleScreen.MergedMenu = mergedMenus && lowerDrawing &&
                                  menuLayoutFound != mm3dMenuLayouts.end() &&
                                  screenLayout == Oot3dNativeGame::TriAevumScreenLayout::SingleScreen;
        if (singleScreen.MergedMenu && mergedMenuScreen != menuLayoutFound->first) {
          auto parts = menuLayoutFound->second.Parts;
          // (Debugging: TRIAEVUM_SKIP_MENU_PARTS=bit mask of parts left out.)
          if (const char *skip = std::getenv("TRIAEVUM_SKIP_MENU_PARTS")) {
            const unsigned long mask = std::strtoul(skip, nullptr, 0);
            std::vector<Oot3dNativeGame::TriAevumHudPart> kept;
            for (size_t index = 0; index < parts.size(); ++index) {
              if (index >= 32U || (mask & (1UL << index)) == 0U) kept.push_back(parts[index]);
            }
            parts = std::move(kept);
          }
          Oot3dNativeGame::SetTriAevumMenuParts(std::move(parts));
        }
        if (!singleScreen.MergedMenu && mergedMenuScreen >= 0) {
          // A menu closed: the next one shows its own frozen frame, not this
          // one's (the guest captures it as it opens, before the menu is
          // laid out).
          pauseFrozen = false;
          substitutedPauseTextures.clear();
          api.ClearPicaTextureSubstitutes();
        }
        mergedMenuScreen = singleScreen.MergedMenu ? menuLayoutFound->first : -1;
        if (singleScreen.MergedMenu) {
          menuLayout = false;
        }
        // (Again once the pause background's textures are known: they are
        // set up as the menu opens.)
        // The pause textures the upper UI layer leaves out (re-applied when
        // they change: the list keeps the last four addresses, its size
        // stops changing while the addresses still do).
        static std::vector<uint32_t> ruleTextures;
        if ((singleScreen.MergedMenu != upperUiLayerEnabled ||
             (upperUiLayerEnabled &&
              ruleTextures != widescreenProjection.PauseTextures)) &&
            hudLayerEnabled) {
          upperUiLayerEnabled = singleScreen.MergedMenu;
          ruleTextures = widescreenProjection.PauseTextures;
          Fast::Renderer3ds::PicaHudLayerRule rule;
          rule.Slot = 1U;
          rule.Enabled = upperUiLayerEnabled;
          // Copied, not moved (the upper screen keeps its draws; the frozen
          // background covers them).
          rule.MoveDraws = false;
          rule.IncludeUntextured = true;
          rule.CoverageAlpha = true;  // models stay solid (Deku Mask eyes)
          rule.SourceColorPhysicalAddress = 0x18237FC0U;
          rule.ViewportWidth = 480.0F;
          rule.ViewportHeight = 400.0F;
          rule.OverlayColorPhysicalAddress = kUpperUiLayerColorAddress;
          rule.OverlayDepthPhysicalAddress = 0x18D00000U;
          // The pause background stays on the screen.
          rule.ExcludedTextureAddresses = widescreenProjection.PauseTextures;
          if (std::getenv("TRIAEVUM_SCREEN_DEBUG") != nullptr) {
            std::fprintf(stderr, "[merged] %d pause textures %zu\n", upperUiLayerEnabled ? 1 : 0,
                         widescreenProjection.PauseTextures.size());
          }
          api.SetPicaHudLayerRule(rule);
          // In menus the lower layer keeps the item icons (gameplay hides
          // that atlas) and leaves out only the screen's background.
          auto lowerRule = lowerHudRule;
          if (upperUiLayerEnabled) {
            // ... and the item slots' frames (535a...): in the merged menu they
            // were stray squares and circles over the world.
            lowerRule.ExcludedTextureHashes = {0x146825612BB556CAULL};
            if (std::getenv("TRIAEVUM_MENU_SLOT_FRAMES") == nullptr) {
              lowerRule.ExcludedTextureHashes.push_back(0x535A25B48E42E6BFULL);
            }
          }
          api.SetPicaHudLayerRule(lowerRule);
        }
      }
      {
        const auto now = std::chrono::steady_clock::now();
        const float step =
            std::chrono::duration<float>(now - singleScreenBlendTime).count() / 0.22F;
        singleScreenBlendTime = now;
        const auto approach = [step](float value, bool target) {
          return target ? std::min(1.0F, value + step) : std::max(0.0F, value - step);
        };
        singleScreen.MenuBlend = approach(singleScreen.MenuBlend, menuLayout);
        singleScreen.InsetBlend = approach(singleScreen.InsetBlend, insetLayout);
        if (menuLayout) {
          lowerOverlay = false;  // the menu shows the lower screen itself
        }
        singleScreen.LowerOverlay = lowerOverlay;
      }
      screenPlacements = Oot3dNativeGame::ResolveTriAevumScreenPlacements(
          screenLayout, width, height, 0.32F,
          Oot3dNativeGame::TriAevumWidescreenEnabled()
              ? static_cast<float>(width) / static_cast<float>(std::max<uint32_t>(height, 1U))
              : 0.0F,
          singleScreen);
      // The Notebook's backdrop is one column of its list panel stretched:
      // while its page still slides in that column is the page (a yellow
      // screen for a few frames). Only once it is interactive (its cursor):
      // from then on it stays, also while the cursor is rebuilt for two
      // frames on each move in the list (the backdrop flickered).
      static bool notebookInteractive = false;
      if (mergedMenuScreen != 2 && mergedMenuScreen != kMm3dScheduleScreen) {
        notebookInteractive = false;
      } else if (std::any_of(shownLowerLayouts.begin(), shownLowerLayouts.end(),
                             [](const std::string &name) {
                               return name.rfind("Cursor", 0) == 0 || name == "MenuSchedule";
                             })) {
        notebookInteractive = true;
      }
      if (mergedMenuScreen == 2 && !notebookInteractive) {
        screenPlacements.UpperFills.clear();
      }
      if (screenPlacements.Upper.has_value()) {
        const auto &upper = *screenPlacements.Upper;
        picaPresentationScheduler.SetTopScreenPlacement(
            Oot3dNativeGame::Oot3dPicaScreenPlacement{
                Oot3d::Renderer::PicaPresentationMode::Replace, upper.X,
                upper.Y, upper.Width, upper.Height, 0U, true});
      } else {
        picaPresentationScheduler.SetTopScreenPlacement(std::nullopt);
      }
      Oot3dNativeGame::SetTriAevumLowerScreenTouchRect(screenPlacements.Lower);
      Oot3dNativeGame::SetTriAevumLowerScreenTouchCrops(screenPlacements.LowerCrops);
    }
    bool nativeFrontendTouchEnabled = foreignTitle ||
        ResolveNativeA32TouchInputEnabled(
            process.Memory(), uiLifecycleBridge,
            nativeCandidateDispatch.TopScreenUiProfile);
    uint32_t topScreenNativeGameMode = UINT32_MAX;
    const bool topScreenGameplayInputMode =
        !foreignTitle &&
        process.Memory().Read32(kNativeGameMode, &topScreenNativeGameMode) &&
        Oot3dNativeGame::IsTopScreenGameplayInputMode(topScreenNativeGameMode);
    picaCompositionDomain =
        topScreenGameplayInputMode
            ? Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene
            : Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
    if (foreignTitle && manifest->Title.Id == "mm3d" && mm3dScreen == 1 &&
        std::getenv("TRIAEVUM_NO_SCENE_EFFECTS") == nullptr) {
      // MM3D gameplay: the upper screen is the world (its HUD is mostly on
      // the lower screen, laid over it afterwards), so the F1 anti-aliasing
      // and effects apply to it. Menus stay untouched.
      picaCompositionDomain = Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
    }
    bool topScreenStartRoutingEligible =
        uiLifecycleBridge.NativeGameplayPresentationActive() &&
        topScreenGameplayInputMode;
    Oot3dNativeGame::NativeAimProfileTransform aimProfileTransform;
    if (nativeCandidateDispatch.TopScreenUiProfile) {
      const auto topScreenAim =
          Oot3dNativeGame::ResolveTopScreenCStickAimPolicy(
              activeTopScreenConfig);
      aimProfileTransform.RightStickScale = topScreenAim.SpeedMultiplier;
      aimProfileTransform.RightStickSmoothingCoefficient =
          Oot3dNativeGame::ResolveTopScreenCStickSmoothingCoefficient(
              activeTopScreenConfig.FreeCameraSmoothing);
      aimProfileTransform.RightStickInvertX = topScreenAim.InvertX;
      aimProfileTransform.RightStickInvertY = topScreenAim.InvertY;
    }
    {
      const auto &controls = controlConfigRuntime->Snapshot().Config;
      static const float cameraSpeedOverride = [] {
        const char *value = std::getenv("TRIAEVUM_CAMERA_STICK_SPEED");
        return value != nullptr ? std::strtof(value, nullptr) : 0.0F;
      }();
      widescreenProjection.CameraStickSpeed =
          cameraSpeedOverride > 0.0F ? cameraSpeedOverride : controls.CameraStickSpeed;
      // F1 menu size: the setting, or 1 per 1080 window lines in steps of
      // a half (1x up to 1440p, 2x at 4K).
      static float appliedMenuScale = 1.0F;
      static ImGuiStyle baseStyle = ImGui::GetStyle();
      const float menuScale =
          controls.MenuScale > 0.0F
              ? controls.MenuScale
              : std::max(1.0F, std::floor(static_cast<float>(window.GetHeight()) / 1080.0F * 2.0F) / 2.0F);
      if (std::abs(menuScale - appliedMenuScale) > 0.01F) {
        ImGui::GetStyle() = baseStyle;
        ImGui::GetStyle().ScaleAllSizes(menuScale);
        ImGui::GetIO().FontGlobalScale = menuScale;
        appliedMenuScale = menuScale;
        // The settings window grows with it (its saved size is the old one).
        ImGui::SetWindowSize(
            "OOT3D Graphics",
            ImVec2(std::min(560.0F * menuScale, static_cast<float>(window.GetWidth()) * 0.9F),
                   std::min(760.0F * menuScale, static_cast<float>(window.GetHeight()) * 0.9F)));
      }
    }
    const bool hostGuiVisible =
        gui->GetMenuOrMenubarVisible() || gui->GetAnyGuiWindowVisible();
    auto physicalInputFrame =
        PollNativeA32Input(window, nativeFrontendTouchEnabled,
                           hostGuiVisible,
                           nativeCandidateDispatch.TopScreenUiProfile,
                           activeTopScreenConfig.FreeCameraEnabled,
                           *controlConfigRuntime, aimProfileTransform,
                           presentationElapsedSeconds,
                           guestRefreshesDue != 0U,
                           nativeControlPollingState);
    if (physicalInputFrame.Exit) {
      window.Close();
      break;
    }
    // Start+A (A is the PlayStation pad's cross): the F1 settings, driven by
    // the controller (ImGui gamepad navigation; the game gets no pad input
    // while they are open). Start+B (circle) closes them. Start is held
    // back from the game for a moment so the combination does not also open
    // the Notebook; a short Start press still reaches the game.
    if (!inputTimeline.Enabled()) {
      using Clock = std::chrono::steady_clock;
      static bool padSettings = false;
      static bool comboWasDown = false;
      static bool closeComboWasDown = false;
      static bool startWasDown = false;
      static bool startHeldBack = false;
      static bool waitStartRelease = false;
      static Clock::time_point startDownAt;
      static Clock::time_point startPulseUntil;
      const uint32_t startMask = Oot3dNativeGame::NativeA32HidButtonMask(Oot3dNativeGame::NativeA32HidButton::Start);
      const uint32_t aMask = Oot3dNativeGame::NativeA32HidButtonMask(Oot3dNativeGame::NativeA32HidButton::A);
      auto &buttons = physicalInputFrame.Hid.Buttons;
      const bool startDown = (buttons & startMask) != 0U;
      const uint32_t bMask = Oot3dNativeGame::NativeA32HidButtonMask(Oot3dNativeGame::NativeA32HidButton::B);
      const bool comboDown = startDown && (buttons & aMask) != 0U;
      const bool closeComboDown = startDown && (buttons & bMask) != 0U;
      const auto now = Clock::now();
      const auto settingsWindow = gui->GetGuiWindow("OOT3D Graphics");
      if (padSettings && (settingsWindow == nullptr || !settingsWindow->IsVisible())) {
        padSettings = false;  // closed with F1 or its close button
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
      }
      const bool startPressed = startDown && !startWasDown;
      if (settingsWindow != nullptr &&
          ((!padSettings && comboDown && !comboWasDown) ||
           (padSettings && closeComboDown && !closeComboWasDown))) {
        padSettings = !padSettings;
        if (padSettings) {
          settingsWindow->Show();
          ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
          ImGui::SetWindowFocus("OOT3D Graphics");
        } else {
          settingsWindow->Hide();
          ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }
        startHeldBack = false;
        waitStartRelease = true;  // this Start press belongs to the combination
      }
      if (!startDown) waitStartRelease = false;
      if (startPressed && !padSettings && !waitStartRelease) {
        startHeldBack = true;
        startDownAt = now;
      }
      if (startHeldBack) {
        if (!startDown) {
          // Released before the hold-back ended: still a Start press.
          startHeldBack = false;
          startPulseUntil = now + std::chrono::milliseconds(100);
        } else if (now - startDownAt > std::chrono::milliseconds(120)) {
          startHeldBack = false;
        } else {
          buttons &= ~startMask;
        }
      }
      if (now < startPulseUntil) buttons |= startMask;
      if (waitStartRelease) buttons &= ~startMask;
      startWasDown = startDown;
      comboWasDown = comboDown;
      closeComboWasDown = closeComboDown;
      if (padSettings) {
        physicalInputFrame.Hid.Buttons = 0U;
        physicalInputFrame.Hid.CirclePadX = physicalInputFrame.Hid.CirclePadY = 0;
        physicalInputFrame.Hid.TouchPressed = false;
        physicalInputFrame.CStick = {};
        physicalInputFrame.TitleActions = 0U;
      }
    }
    if (!inputTimeline.Enabled()) {
      const uint32_t start = Oot3dNativeGame::NativeA32HidButtonMask(
          Oot3dNativeGame::NativeA32HidButton::Start);
      const bool physicalStartHeld =
          (physicalInputFrame.Hid.Buttons & start) != 0U;
      Oot3dNativeGame::ObserveNativeA32PolledButton(
          physicalStartHeld, nativeCandidateDispatch.StartButtonLatch);
      Oot3dNativeGame::ReleaseTopScreenStartRoutingLatch(
          physicalStartHeld, nativeCandidateDispatch.TopScreenStartRouting);
    }
    Oot3dNativeGame::NativeA32InputFrame inputFrame;
    phaseTiming.InputPollSeconds += SecondsSince(inputPhaseStart);
    // Between frames (the renderer is idle; the guest runs after StartFrame).
    if (manifest->Title.Id == "mm3d" && widescreenProjection.CaptureObject != 0U) {
      // A capture converts next frame (+0x1D == 1): its RGB8 sources get the
      // presented top screen read back, scaled to the native 240x400.
      auto &memory = process.Memory();
      const uint32_t object = widescreenProjection.CaptureObject;
      uint8_t stage = 0, mode = 0;
      static bool filled = false;
      if (memory.Read8(object + 0x1DU, &stage) && stage == 1U) {
        if (!filled) {
          filled = true;
          memory.Read8(object + 0x1CU, &mode);
          Oot3d::Renderer::PicaPresentationStateSnapshot snapshot;
          std::string error;
          size_t written = 0;
          if (!api.CapturePicaPresentationState(snapshot, &error)) {
            std::fprintf(stderr, "[capture] readback failed: %s\n", error.c_str());
          }
          for (uint32_t slot = 0; slot < 2U; ++slot) {
            uint32_t buffer = 0;
            if (!memory.Read32(object + 0xCU + 4U * slot, &buffer) || buffer == 0U) continue;
            const auto physical = picaMemoryView.TranslateGuest(buffer, 240U * 400U * 3U);
            if (!physical) continue;
            for (const auto &image : snapshot.DisplayImages) {
              if (image.OutputPhysicalAddress != *physical || !image.Initialized || image.ImageWidth == 0U ||
                  image.ImageHeight == 0U || image.ColorRgba8.size() < size_t{image.ImageWidth} * image.ImageHeight * 4U)
                continue;
              // 240 pixels per line, 400 lines, bytes B,G,R; each pixel the
              // average of the image's pixels over it.
              std::vector<uint8_t> native(240U * 400U * 3U);
              for (uint32_t line = 0; line < 400U; ++line) {
                const uint32_t y0 = line * image.ImageHeight / 400U;
                const uint32_t y1 = std::max(y0 + 1U, (line + 1U) * image.ImageHeight / 400U);
                for (uint32_t column = 0; column < 240U; ++column) {
                  const uint32_t x0 = column * image.ImageWidth / 240U;
                  const uint32_t x1 = std::max(x0 + 1U, (column + 1U) * image.ImageWidth / 240U);
                  uint32_t sum[3] = {}, count = 0;
                  for (uint32_t y = y0; y < y1; ++y)
                    for (uint32_t x = x0; x < x1; ++x, ++count)
                      for (uint32_t c = 0; c < 3U; ++c)
                        sum[c] += image.ColorRgba8[(size_t{y} * image.ImageWidth + x) * 4U + c];
                  uint8_t *pixel = &native[(line * 240U + column) * 3U];
                  pixel[0] = static_cast<uint8_t>(sum[2] / count);
                  pixel[1] = static_cast<uint8_t>(sum[1] / count);
                  pixel[2] = static_cast<uint8_t>(sum[0] / count);
                }
              }
              for (size_t offset = 0; offset < native.size(); offset += 4U) {
                uint32_t word = 0;
                std::memcpy(&word, &native[offset], sizeof(word));
                memory.Write32(buffer + static_cast<uint32_t>(offset), word);
              }
              ++written;
              std::fprintf(stderr, "[capture] mode %u buffer %08X image %ux%u\n", mode, buffer,
                           image.ImageWidth, image.ImageHeight);
              break;
            }
          }
          if (written == 0U) std::fprintf(stderr, "[capture] mode %u: no presented image for its buffers\n", mode);
        }
      } else {
        filled = false;
      }
    }
    const auto rendererFrameStart = std::chrono::steady_clock::now();
    CpuProbe::Scope cpuRendererStart(CpuProbe::Phase::RendererStart);
    api.UpdateFramebufferParameters(0, width, height, 1, false, true, true,
                                    true);
    api.StartFrame();
    if (!api.HasActiveFrame()) {
      // A minimized or changing surface is not a guest/PICA failure. Keep events
      // and UI balanced, but do not consume guest time without an acquired frame.
      if (!presentationScheduler.Restore(presentationStateBeforeFrame, &error)) {
        throw std::runtime_error("cannot restore suspended presentation clock: " + error);
      }
      gui->EndDraw();
      window.EndFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      realtimePacer.ResetDeadline();
      lastPresentationTime = std::chrono::steady_clock::now();
      continue;
    }
    picaPresentationScheduler.BeginPresentation(presentationFrameCount);
    api.StartDrawToFramebuffer(0, 1.0F);
    api.SetClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    api.ClearFramebuffer(true, true);
    api.SetViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    api.SetScissor(0, 0, static_cast<int>(width), static_cast<int>(height));
    phaseTiming.RendererFrameStartSeconds += SecondsSince(rendererFrameStart);
    cpuRendererStart.Stop();
    phaseTiming.FrameStartSeconds += SecondsSince(phaseStart);
    const uint32_t guestRefreshIterations =
        std::max<uint32_t>(1U, guestRefreshesDue);
    for (uint32_t guestRefreshIndex = 0;
         guestRefreshIndex < guestRefreshIterations; ++guestRefreshIndex) {
      const bool advanceGuest = guestRefreshIndex < guestRefreshesDue;
      const bool presentHostFrame =
          guestRefreshIndex + 1U == guestRefreshIterations;
      if (advanceGuest) {
        if (guestRefreshIndex != 0U) {
          nativeFrontendTouchEnabled = foreignTitle ||
              ResolveNativeA32TouchInputEnabled(
                  process.Memory(), uiLifecycleBridge,
                  nativeCandidateDispatch.TopScreenUiProfile);
          topScreenNativeGameMode = UINT32_MAX;
          const bool refreshedTopScreenGameplayInputMode =
              !foreignTitle &&
              process.Memory().Read32(kNativeGameMode,
                                      &topScreenNativeGameMode) &&
              Oot3dNativeGame::IsTopScreenGameplayInputMode(
                  topScreenNativeGameMode);
          topScreenStartRoutingEligible =
              uiLifecycleBridge.NativeGameplayPresentationActive() &&
              refreshedTopScreenGameplayInputMode;
          picaCompositionDomain =
              refreshedTopScreenGameplayInputMode
                  ? Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene
                  : Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
          if (foreignTitle && manifest->Title.Id == "mm3d" && mm3dScreen == 1 &&
              std::getenv("TRIAEVUM_NO_SCENE_EFFECTS") == nullptr) {
            picaCompositionDomain = Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
          }
        }
        inputFrame = ResolveNativeA32GuestInput(
            physicalInputFrame, inputTimeline, frameCount, runFrameCount,
            nativeFrontendTouchEnabled, topScreenStartRoutingEligible,
            nativeCandidateDispatch.TopScreenUiProfile,
            nativeCandidateDispatch.StartButtonLatch,
            nativeCandidateDispatch.TopScreenStartRouting);
        // Tests: TRIAEVUM_AUTOWALK=<actor id hex>[:stop distance] steers Link
        // (circle pad, camera-relative) to the nearest actor with that id until
        // it is that close; TRIAEVUM_ACTOR_DUMP=1 lists the scene's actors.
        if (foreignTitle && manifest->Title.Id == "mm3d") {
          static const char *autowalk = std::getenv("TRIAEVUM_AUTOWALK");
          static const bool actorDump = std::getenv("TRIAEVUM_ACTOR_DUMP") != nullptr;
          auto &memory = process.Memory();
          uint32_t context = 0, game = 0, type = 0, player = 0;
          if ((autowalk != nullptr || actorDump) && memory.Read32(0x0072F5C0U, &context) &&
              context >= 0x100000U && memory.Read32(context + 0x16CU, &game) && game >= 0x100000U &&
              memory.Read32(game + 0x13CU, &type) && ((type >> 8U) & 0xFFU) == 4U &&
              memory.Read32(game + 0x20DCU, &player) && player >= 0x100000U) {
            const auto readFloat = [&](uint32_t address) {
              uint32_t bits = 0;
              memory.Read32(address, &bits);
              return std::bit_cast<float>(bits);
            };
            const float px = readFloat(player + 0x24U), pz = readFloat(player + 0x2CU);
            static uint32_t dumpedScene = ~0U;
            uint32_t sceneKey = 0;
            memory.Read32(game + 0x20C0U, &sceneKey);
            const auto forEachActor = [&](auto &&visit) {
              for (uint32_t list = 0; list < 12U; ++list) {
                uint32_t actor = 0;
                memory.Read32(game + 0x20C0U + list * 0xCU + 4U, &actor);
                for (int guard = 0; actor >= 0x100000U && guard < 256; ++guard) {
                  uint16_t id = 0;
                  memory.Read16(actor, &id);
                  visit(list, actor, id);
                  memory.Read32(actor + 0x134U, &actor);
                }
              }
            };
            static uint64_t dumpFrame = 0;
            if (actorDump && (dumpFrame++ % 600U) == 0U) {
              std::fprintf(stderr, "[actors] link %.0f,%.0f\n", px, pz);
              forEachActor([&](uint32_t list, uint32_t actor, uint16_t id) {
                const float x = readFloat(actor + 0x24U), z = readFloat(actor + 0x2CU);
                std::fprintf(stderr, "[actors]  type %u id %04x at %.0f,%.0f dist %.0f\n", list, id, x, z,
                             std::hypot(x - px, z - pz));
              });
            }
            if (autowalk != nullptr) {
              const uint16_t wanted = static_cast<uint16_t>(std::strtoul(autowalk, nullptr, 16));
              const char *colon = std::strchr(autowalk, ':');
              const float stop = colon != nullptr ? static_cast<float>(std::atof(colon + 1)) : 40.0F;
              float best = 1.0e9F, tx = 0.0F, tz = 0.0F;
              forEachActor([&](uint32_t, uint32_t actor, uint16_t id) {
                if (id != wanted) return;
                const float x = readFloat(actor + 0x24U), z = readFloat(actor + 0x2CU);
                const float d = std::hypot(x - px, z - pz);
                if (d < best) { best = d; tx = x; tz = z; }
              });
              uint32_t cameraIndexWord = 0;
              memory.Read32(game + 0xA78U, &cameraIndexWord);
              const uint32_t camera = game + 0x408U + ((cameraIndexWord >> 16U) & 0xFFFFU) * 0x198U;
              const float fx = readFloat(camera + 0x50U) - readFloat(camera + 0x5CU);
              const float fz = readFloat(camera + 0x58U) - readFloat(camera + 0x64U);
              const float fl = std::hypot(fx, fz);
              static uint64_t walkLog = 0;
              if (best < 1.0e9F && best > stop && fl > 1.0e-3F) {
                const float dx = (tx - px) / best, dz = (tz - pz) / best;
                const float forward = (dx * fx + dz * fz) / fl, right = (dx * -fz + dz * fx) / fl;
                inputFrame.Hid.CirclePadY = static_cast<int16_t>(std::lround(forward * 154.0F));
                inputFrame.Hid.CirclePadX = static_cast<int16_t>(std::lround(right * 154.0F));
              }
              if ((walkLog++ % 120U) == 0U)
                std::fprintf(stderr, "[autowalk] link %.0f,%.0f target %04x %.0f,%.0f dist %.0f\n", px, pz,
                             wanted, tx, tz, best);
            }
          }
        }
        inputDiagnostics.Observe(inputFrame);
        if (inputFrame.Exit) {
          window.Close();
          break;
        }
        uint32_t mm3dMenuCursor = 0;
        if (mm3dScreen == 4 || mm3dScreen == 6) {
          uint32_t menuObject = 0;
          if (process.Memory().Read32(0x00656B00U + static_cast<uint32_t>(mm3dScreen) * 4U, &menuObject) &&
              menuObject >= 0x100000U) {
            process.Memory().Read32(menuObject + 0x18U, &mm3dMenuCursor);
          }
        }
        if (foreignTitle && manifest->Title.Id == "mm3d" &&
            ApplyMm3dMenuButtons(inputFrame, mm3dRestoration ? -1 : mm3dScreen,
                                 &mm3dPreviousButtons, physicalInputFrame.TitleActions,
                                 &mm3dPreviousTitleActions, mm3dMenuCursor,
                                 std::find(shownLowerLayouts.begin(), shownLowerLayouts.end(),
                                           "MenuSchedule") != shownLowerLayouts.end())) {
          minimapShown = !minimapShown;
          Oot3dNativeGame::SetTriAevumHudParts(mm3dHudParts(minimapShown));
        }
        if (foreignTitle && manifest->Title.Id == "mm3d") {
          const auto &layouts = widescreenProjection.LowerLayoutNames;
          // Interactive once its cursor exists (before that A dismisses the
          // "select an item on the lower screen" message).
          const bool giveMenu = std::find(layouts.begin(), layouts.end(), "MenuItemPresen") != layouts.end() &&
                                std::find(layouts.begin(), layouts.end(), "CursorItemCollect") != layouts.end();
          using ThreeDsRecomp::Input::Button;
          using ThreeDsRecomp::Input::ButtonMask;
          uint32_t &buttons = inputFrame.Hid.Buttons;
          const uint32_t pressed = buttons & ~mm3dGivePreviousButtons;
          mm3dGivePreviousButtons = buttons;
          if (giveMenu && !mm3dGiveMenu) {
            mm3dGiveColumn = 0;
            mm3dGiveRow = 0;
          }
          mm3dGiveMenu = giveMenu;
          if (giveMenu) {
            int dx = 0, dy = 0;
            if (pressed & ButtonMask(Button::DpadLeft)) dx = -1;
            if (pressed & ButtonMask(Button::DpadRight)) dx = 1;
            if (pressed & ButtonMask(Button::DpadUp)) dy = -1;
            if (pressed & ButtonMask(Button::DpadDown)) dy = 1;
            const int stickX = inputFrame.Hid.CirclePadX, stickY = inputFrame.Hid.CirclePadY;
            const bool stickOut = std::abs(stickX) > 100 || std::abs(stickY) > 100;
            if (stickOut && !mm3dGiveStickHeld) {
              if (std::abs(stickX) >= std::abs(stickY)) dx = stickX > 0 ? 1 : -1;
              else dy = stickY > 0 ? -1 : 1;  // circle pad up is positive
            }
            mm3dGiveStickHeld = std::abs(stickX) > 60 || std::abs(stickY) > 60;
            mm3dGiveColumn = std::clamp(mm3dGiveColumn + dx, 0, 5);
            mm3dGiveRow = std::clamp(mm3dGiveRow + dy, 0, 3);
            if (pressed & ButtonMask(Button::A)) {
              const auto cell = mm3dGiveCell(mm3dGiveColumn, mm3dGiveRow);
              Oot3dNativeGame::RequestTriAevumShortcutTouch(cell.first, cell.second);
            }
            buttons &= ~(ButtonMask(Button::DpadLeft) | ButtonMask(Button::DpadRight) |
                         ButtonMask(Button::DpadUp) | ButtonMask(Button::DpadDown) | ButtonMask(Button::A));
            inputFrame.Hid.CirclePadX = inputFrame.Hid.CirclePadY = 0;
          }
        }
      }
      if (advanceGuest) {
        const uint32_t topScreenButtons = inputFrame.Hid.Buttons;
        const uint32_t topScreenPressed =
            topScreenButtons &
            ~nativeCandidateDispatch.PreviousTopScreenButtons;
        const auto topScreenButton =
            [](Oot3dNativeGame::NativeA32HidButton value) {
              return Oot3dNativeGame::NativeA32HidButtonMask(value);
            };
        Oot3dNativeGame::TopScreenDpadActionState topScreenDpadActions;
        Oot3dNativeGame::TopScreenOcarinaState ocarinaOwner;
        const bool ocarinaOwnsDpad = nativeCandidateDispatch.TopScreenUiProfile &&
            Oot3dNativeGame::ReadTopScreenOcarinaState(process.Memory(), &ocarinaOwner) &&
            ocarinaOwner.Active;
        if (nativeCandidateDispatch.TopScreenUiProfile &&
            topScreenStartRoutingEligible && !ocarinaOwnsDpad) {
          bool childLink = false;
          std::string childLinkError;
          if (!Oot3dNativeGame::ReadTopScreenChildLink(
                  process.Memory(), &childLink, &childLinkError)) {
            throw std::runtime_error("TopScreen Link age decode failed: " +
                                     childLinkError);
          }
          Oot3dNativeGame::TopScreenDpadPhysicalState dpadPhysical;
          dpadPhysical.ChildLink = childLink;
          constexpr std::array<Oot3dNativeGame::NativeA32HidButton, 4>
              kDpadButtons{Oot3dNativeGame::NativeA32HidButton::DpadUp,
                           Oot3dNativeGame::NativeA32HidButton::DpadDown,
                           Oot3dNativeGame::NativeA32HidButton::DpadLeft,
                           Oot3dNativeGame::NativeA32HidButton::DpadRight};
          for (std::size_t direction = 0U;
               direction < kDpadButtons.size(); ++direction) {
            const auto mask = topScreenButton(kDpadButtons[direction]);
            dpadPhysical.Pressed[direction] =
                (topScreenPressed & mask) != 0U;
            dpadPhysical.Held[direction] =
                (topScreenButtons & mask) != 0U;
          }
          topScreenDpadActions = Oot3dNativeGame::ResolveTopScreenDpadActions(
              activeTopScreenConfig, dpadPhysical);
        }
        const bool zrHeld = ThreeDsRecomp::Input::IsButtonHeld(
            inputFrame, ThreeDsRecomp::Input::Button::Zr);
        const bool zlHeld = ThreeDsRecomp::Input::IsButtonHeld(
            inputFrame, ThreeDsRecomp::Input::Button::Zl);
        Oot3dNativeGame::TopScreenExtendedInputFrame sampledTopScreenInput = {
            .ZrHeld =
                zrHeld ||
                topScreenDpadActions.IsHeld(
                    Oot3dNativeGame::TopScreenDpadAction::ItemZr),
            .ZlHeld =
                zlHeld ||
                topScreenDpadActions.IsHeld(
                    Oot3dNativeGame::TopScreenDpadAction::ItemZl),
            .DpadLeftHeld =
                (inputFrame.Hid.Buttons &
                 Oot3dNativeGame::NativeA32HidButtonMask(
                     Oot3dNativeGame::NativeA32HidButton::DpadLeft)) != 0U,
            .DpadRightHeld =
                (inputFrame.Hid.Buttons &
                 Oot3dNativeGame::NativeA32HidButtonMask(
                     Oot3dNativeGame::NativeA32HidButton::DpadRight)) != 0U};
        sampledTopScreenInput.DpadUpHeld =
            (inputFrame.Hid.Buttons & topScreenButton(
                Oot3dNativeGame::NativeA32HidButton::DpadUp)) != 0U;
        if (ocarinaOwnsDpad) {
          inputFrame.Hid.Buttons &= ~(topScreenButton(Oot3dNativeGame::NativeA32HidButton::DpadLeft) |
                                     (ocarinaOwner.Page == 12 ? topScreenButton(
                                         Oot3dNativeGame::NativeA32HidButton::DpadUp) : 0U) |
                                     topScreenButton(Oot3dNativeGame::NativeA32HidButton::DpadRight));
        }
        sampledTopScreenInput.XHeld =
            (topScreenButtons &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::X)) != 0U;
        sampledTopScreenInput.YHeld =
            (topScreenButtons &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::Y)) != 0U;
        sampledTopScreenInput.GameplayDpadActionsOwned = true;
        sampledTopScreenInput.RestorationLayout =
            activeTopScreenConfig.HudLayout ==
            Oot3dNativeGame::TopScreenHudLayout::Restoration;
        nativeCandidateDispatch.TopScreenInputClock.ObserveGuest(sampledTopScreenInput);
        if (nativeCandidateDispatch.TopScreenUiProfile &&
            activeTopScreenConfig.FreeCameraEnabled &&
            nativeControlPollingState.GameplayMouseOwned) {
          nativeCandidateDispatch.TopScreenCameraInput.Observe(
              inputFrame.CStick);
        } else if (inputFrame.CStick.Kind ==
                   Oot3dNativeGame::NativeFreeCameraInputKind::Absolute) {
          nativeCandidateDispatch.TopScreenCameraInput.Observe(
              inputFrame.CStick);
        } else {
          nativeCandidateDispatch.TopScreenCameraInput.Reset();
        }
        if (nativeCandidateDispatch.TopScreenUiProfile) {
          const uint32_t buttons = inputFrame.Hid.Buttons;
          const uint32_t pressed =
              buttons & ~nativeCandidateDispatch.PreviousTopScreenButtons;
          const auto button = [](Oot3dNativeGame::NativeA32HidButton value) {
            return Oot3dNativeGame::NativeA32HidButtonMask(value);
          };
          const bool gameplayViewPressed = topScreenDpadActions.WasPressed(
              Oot3dNativeGame::TopScreenDpadAction::View);
          const bool gameplayOcarinaPressed =
              topScreenDpadActions.WasPressed(
                  Oot3dNativeGame::TopScreenDpadAction::Ocarina);
          const bool leftShoulderHeld =
              (buttons &
               button(Oot3dNativeGame::NativeA32HidButton::L)) != 0U;
          const bool rightShoulderHeld =
              (buttons &
               button(Oot3dNativeGame::NativeA32HidButton::R)) != 0U;
          const bool selectPressed =
              (pressed &
               button(Oot3dNativeGame::NativeA32HidButton::Select)) != 0U;
          if (topScreenDpadActions.WasPressed(
                  Oot3dNativeGame::TopScreenDpadAction::MinimapToggle) ||
              (selectPressed &&
               activeTopScreenConfig.SelectAction ==
                   Oot3dNativeGame::TopScreenSelectAction::MinimapToggle)) {
            setTopScreenSessionMinimapVisible(
                !activeTopScreenConfig.MinimapVisible);
          }
          if (selectPressed &&
              activeTopScreenConfig.SelectAction ==
                  Oot3dNativeGame::TopScreenSelectAction::MinimapToggle) {
            inputFrame.Hid.Buttons &=
                ~button(Oot3dNativeGame::NativeA32HidButton::Select);
          }

          if (gameplayViewPressed || gameplayOcarinaPressed) {
            ++nativeCandidateDispatch.TopScreenGameplayDpadAttempts;
            Oot3dNativeGame::TopScreenGameplayDpadResult dpadResult;
            std::string dpadError;
            if (!Oot3dNativeGame::PrepareTopScreenGameplayDpadGuest(
                    process.Memory(),
                    {.ViewPressed = gameplayViewPressed,
                     .OcarinaPressed = gameplayOcarinaPressed,
                     .LeftShoulderHeld = leftShoulderHeld,
                     .RightShoulderHeld = rightShoulderHeld},
                    &dpadResult, &dpadError)) {
              throw std::runtime_error(
                  "TopScreen gameplay D-pad prepare failed: " + dpadError);
            }
            nativeCandidateDispatch.TopScreenNaviViewActivations +=
                dpadResult.NaviViewActivated ? 1U : 0U;
            if (dpadResult.OcarinaQueryRequired) {
              ++nativeCandidateDispatch.TopScreenOcarinaQueries;
              uint32_t nativeEligibility = 0U;
              constexpr std::array<std::uint32_t, 0> kNoArguments{};
              if (!process.InvokeFunctionWithResult(
                      kTopScreenOcarinaEligibility, kNoArguments,
                      kTopScreenDirectCallReturn, &nativeEligibility,
                      &dpadError) ||
                  !Oot3dNativeGame::CommitTopScreenGameplayOcarinaGuest(
                      process.Memory(), nativeEligibility, &dpadResult,
                      &dpadError)) {
                throw std::runtime_error(
                    "TopScreen ocarina D-pad route failed: " + dpadError);
              }
              nativeCandidateDispatch.TopScreenOcarinaActivations +=
                  dpadResult.OcarinaActivated ? 1U : 0U;
            }
          }
          std::string gameplayActionError;
          if (const auto sound = uiLifecycleBridge.TakeOcarinaGuideSound(); sound != 0U) {
            if (!Oot3dNativeGame::PlayTopScreenUiSound(
                    process, sound, kTopScreenDirectCallReturn, &gameplayActionError)) {
              throw std::runtime_error("TopScreen ocarina guide sound failed: " + gameplayActionError);
            }
          }
          if (!Oot3dNativeGame::ConsumeTopScreenGameplayActions(
                  process, topScreenDpadActions,
                  nativeCandidateDispatch.TopScreenInput.ZrPressed ||
                      nativeCandidateDispatch.TopScreenInput.ZlPressed,
                  kTopScreenDirectCallReturn,
                  &nativeCandidateDispatch.TopScreenGameplayActionRuntime,
                  &nativeCandidateDispatch.TopScreenGameplayActions,
                  &gameplayActionError)) {
            throw std::runtime_error(
                "TopScreen gameplay action failed: " +
                gameplayActionError);
          }
          nativeCandidateDispatch.PreviousTopScreenButtons = buttons;
          nativeCandidateDispatch.TopScreenItems.DirectItemId =
              nativeCandidateDispatch.TopScreenGameplayActionRuntime.DirectItem.ActiveItemId;
          Oot3dNativeGame::TopScreenPauseDrawInputs pauseInputs;
          if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(process.Memory(),
                                                             &pauseInputs)) {
            nativeCandidateDispatch.TopScreenPauseAotBarrier = true;
          } else {
            const auto closePlan =
                Oot3dNativeGame::ResolveTopScreenPauseStartClose(
                    pauseInputs,
                    (pressed &
                     button(Oot3dNativeGame::NativeA32HidButton::Start)) != 0U);
            if (closePlan.Page !=
                Oot3dNativeGame::TopScreenPauseClosePage::None) {
              std::string closeError;
              if (!ApplyTopScreenPauseClose(process, closePlan, &closeError)) {
                throw std::runtime_error(
                    "TopScreen START pause-close transaction failed: " +
                    closeError);
              }
              Oot3dNativeGame::MarkTopScreenStartCloseGestureConsumed(
                  nativeCandidateDispatch.TopScreenStartRouting);
              inputFrame.Hid.Buttons &=
                  ~button(Oot3dNativeGame::NativeA32HidButton::Start);
              ++nativeCandidateDispatch.TopScreenPauseStartCloseCalls;
              nativeCandidateDispatch.TopScreenPauseLastClosedPage =
                  closePlan.Page;
              if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(
                      process.Memory(), &pauseInputs)) {
                throw std::runtime_error("TopScreen pause state could not be "
                                         "read after START close");
              }
            }
            if (activeTopScreenConfig.ExitItemsToSaveScreen) {
              const auto systemOpenPlan =
                  Oot3dNativeGame::ResolveTopScreenPauseBSystemOpen(
                      pauseInputs,
                      (pressed &
                       button(Oot3dNativeGame::NativeA32HidButton::B)) != 0U,
                      nativeCandidateDispatch.TopScreenPauseSystemOpen);
              if (systemOpenPlan.Page !=
                  Oot3dNativeGame::TopScreenPauseClosePage::None) {
                std::string systemOpenError;
                if (!ApplyTopScreenPauseSystemOpen(
                        process, systemOpenPlan, &systemOpenError)) {
                  throw std::runtime_error(
                      "TopScreen B system-menu transaction failed: " +
                      systemOpenError);
                }
                ++nativeCandidateDispatch.TopScreenPauseSystemOpenCalls;
                nativeCandidateDispatch.TopScreenPauseSystemSourcePage =
                    systemOpenPlan.Page;
                if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(
                        process.Memory(), &pauseInputs)) {
                  throw std::runtime_error("TopScreen pause state could not be "
                                           "read after B system-menu open");
                }
              }
            }
            const bool sceneRuntimeRoute = pauseInputs.HasScene &&
                                           pauseInputs.SceneMode == 3U &&
                                           (pauseInputs.RuntimeMode == 1U ||
                                            pauseInputs.RuntimeMode == 2U);
            nativeCandidateDispatch.TopScreenPauseAotBarrier =
                sceneRuntimeRoute ||
                Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
                    pauseInputs,
                    &nativeCandidateDispatch.TopScreenAotPauseRoute);
          }
        }
        if (scenarioBootstrap.has_value() && !scenarioBootstrap->Finished()) {
          std::string scenarioError;
          if (!scenarioBootstrap->Advance(
                  process, sceneViewProbe.Stats().LastPlayStateAddress,
                  frameCount, &scenarioError)) {
            std::cerr << "oot3d_native_game: structural scenario failed: "
                      << scenarioError << '\n';
            if (launch.ScenarioStrict) {
              window.Close();
              break;
            }
          }
          if (launch.ScenarioAutoExit && scenarioBootstrap->Complete()) {
            window.Close();
            break;
          }
        }
        refreshTickRemainder += kCtrArm11TicksPerSecond;
        nextVblankTick += refreshTickRemainder / kGuestDisplayRefreshRate;
        refreshTickRemainder %= kGuestDisplayRefreshRate;
        const auto clockStart = std::chrono::steady_clock::now();
        const auto clockResolution = AdvanceGuestClockTo(
            process, hostServices, processResult, nextVblankTick,
            launch.WholeAotBlockBudget);
        const double clockSeconds = SecondsSince(clockStart);
        phaseTiming.GuestClockSeconds += clockSeconds;
        phaseTiming.GuestSeconds += clockSeconds;
        if (clockResolution.OvershootTicks != 0U) {
          ++lateVblankDeadlineCount;
          maximumVblankDeadlineOvershootTicks =
              std::max(maximumVblankDeadlineOvershootTicks,
                       clockResolution.OvershootTicks);
        }
        hostServices.SetCirclePadProState(inputFrame.Hid.Buttons,
                                          inputFrame.CStick.X,
                                          inputFrame.CStick.Y);
        const auto hidUpdate = hostServices.AdvanceHidToCurrentTick(
            process.Memory(), inputFrame.Hid);
        if (hidUpdate.Status ==
            Oot3dNativeGame::NativeA32CtrHidUpdateStatus::Failed) {
          throw std::runtime_error(
              "native HID sample could not be written to shared memory");
        }
        if (hidUpdate.EventsSignaled &&
            processResult.Kind !=
                Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          phaseTiming.GuestSeconds += SecondsSince(phaseStart);
          RequireRunnableGuest(processResult);
        }
        const uint32_t dspAudioFrames =
            hostServices.TakePendingDspAudioFrames();
        std::vector<int16_t> audioSamples;
        audioSamples.reserve(static_cast<size_t>(dspAudioFrames) * 320U);
        for (uint32_t audioFrame = 0; audioFrame < dspAudioFrames;
             ++audioFrame) {
          const size_t frameSampleOffset = audioSamples.size();
          const auto dspMixStart = std::chrono::steady_clock::now();
          CpuProbe::Scope cpuDsp(CpuProbe::Phase::Dsp);
          if (!dspHle.ProcessFrame(process.Memory(), audioSamples, &error)) {
            throw std::runtime_error("native DSP HLE mix failed: " + error);
          }
          phaseTiming.DspMixSeconds += SecondsSince(dspMixStart);
          cpuDsp.Stop();
          pcmContinuityDiagnostics.ObserveFrame(
              dspFramesMixed, std::span<const int16_t>(audioSamples)
                                  .subspan(frameSampleOffset));
          ++dspFramesMixed;
          dspFramesSuppressed += launch.DisableAudio ? 1U : 0U;
          if (!hostServices.SignalDspAudioFrame()) {
            break;
          }
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          phaseTiming.GuestSeconds += SecondsSince(phaseStart);
          RequireRunnableGuest(processResult);
        }
        for (const int16_t sample : audioSamples) {
          const int32_t magnitude = std::abs(static_cast<int32_t>(sample));
          dspPeakMagnitude = std::max(dspPeakMagnitude, magnitude);
          dspNonzeroSamples += sample != 0 ? 1U : 0U;
          const uint16_t bits = static_cast<uint16_t>(sample);
          dspPcmFnv1a64 ^= static_cast<uint8_t>(bits);
          dspPcmFnv1a64 *= 1099511628211ULL;
          dspPcmFnv1a64 ^= static_cast<uint8_t>(bits >> 8U);
          dspPcmFnv1a64 *= 1099511628211ULL;
        }
        dspSamplesMixed += audioSamples.size() / 2U;
        if (!launch.AudioPcmDumpPath.empty()) {
          pcmDumpSamples.insert(pcmDumpSamples.end(), audioSamples.begin(),
                                audioSamples.end());
        }
        if (!launch.DisableAudio && audioPlayer != nullptr &&
            audioPlayer->IsInitialized() && !audioSamples.empty()) {
          const auto audioOutputStart = std::chrono::steady_clock::now();
          CpuProbe::Scope cpuAudio(CpuProbe::Phase::AudioOutput);
          const int32_t bufferedBefore = audioPlayer->Buffered();
          audioPlayer->Play(
              reinterpret_cast<const uint8_t *>(audioSamples.data()),
              audioSamples.size() * sizeof(int16_t));
          audioOutputDiagnostics.Observe(
              frameCount, bufferedBefore, audioPlayer->Buffered(),
              audioSamples.size() / 2U, audioPlayer->GetDesiredBuffered());
          phaseTiming.AudioOutputSeconds += SecondsSince(audioOutputStart);
        }
        if (!hostServices.SignalVBlank(process.Memory())) {
          throw std::runtime_error(
              "native VBlank could not enter the GSP relay queue");
        }
        ++vblankCount;
      }
      if (presentHostFrame) {
        CpuProbe::Scope cpuVisual(CpuProbe::Phase::VisualPresentation);
        const auto visualPresentationStart = std::chrono::steady_clock::now();
        bool flushedPicaDependency = false;
        if (!pendingTextureCopyCompletions.empty()) {
          if (auto work = picaPresentationScheduler.TakeDependencyWork()) {
            Oot3dNativeGame::Oot3dPicaVisualFrameViewSample sample;
            (void)Oot3dNativeGame::ViewOot3dPicaVisualFrame(*work, sample);
            if (!picaPresentationScheduler.Execute(api, sample, kVisualInterpolationRenderTargetNamespace,
                    Oot3dNativeGame::Oot3dPicaPresentationExecutionKind::DependencyFlush, false, &error)) {
              throw std::runtime_error("native PICA dependency flush failed: " + error);
            }
            flushedPicaDependency = true;
          }
        }
        const bool lcdForceBlack = hostServices.LcdForceBlack();
        const auto topFramebuffer = hostServices.TopFramebuffer();
        const Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission
            *traceSelectedTopTransfer = nullptr;
        if (topFramebuffer.has_value()) {
          auto traceSelected =
              displayTransfersByOutput.find(topFramebuffer->AddressLeft);
          if (traceSelected == displayTransfersByOutput.end() &&
              topFramebuffer->AddressRight != topFramebuffer->AddressLeft) {
            traceSelected =
                displayTransfersByOutput.find(topFramebuffer->AddressRight);
          }
          if (traceSelected != displayTransfersByOutput.end()) {
            traceSelectedTopTransfer = &traceSelected->second;
          }
        }
        picaSemanticTrace.RecordPresentationSelection(
            presentationFrameCount, frameCount, lcdForceBlack,
            topFramebuffer.has_value()
                ? std::optional<uint32_t>(topFramebuffer->AddressLeft)
                : std::nullopt,
            topFramebuffer.has_value()
                ? std::optional<uint32_t>(topFramebuffer->AddressRight)
                : std::nullopt,
            traceSelectedTopTransfer);
        if (!lcdForceBlack) {
          if (flushedPicaDependency) {
            if (traceSelectedTopTransfer && picaPresentationScheduler.HasSnapshot(
                    *traceSelectedTopTransfer, kVisualInterpolationRenderTargetNamespace) &&
                !picaPresentationScheduler.PresentExisting(api, *traceSelectedTopTransfer,
                    kVisualInterpolationRenderTargetNamespace, &error))
              throw std::runtime_error("native PICA dependency scanout failed: " + error);
          } else if (topFramebuffer.has_value()) {
            auto selected =
                displayTransfersByOutput.find(topFramebuffer->AddressLeft);
            if (selected == displayTransfersByOutput.end() &&
                topFramebuffer->AddressRight != topFramebuffer->AddressLeft) {
              selected =
                  displayTransfersByOutput.find(topFramebuffer->AddressRight);
            }
            if (selected != displayTransfersByOutput.end()) {
              lastTopScreenTransfer = selected->second;
              bool presentedVisualSample = false;
              bool selectedNewVisualFrame = false;
              if (selected->second.CompletionId !=
                  lastSelectedTopTransferCompletionId) {
                lastSelectedTopTransferCompletionId =
                    selected->second.CompletionId;
                ++selectedTopTransferCount;
                auto currentVisualFrame =
                    picaPresentationScheduler.FinishFrame(selected->second);
                if (currentVisualFrame.has_value()) {
                  Oot3dNativeGame::PublishNativeActorInteractions(
                      process.Memory(), sceneViewProbe.Stats().LastPlayStateAddress, currentVisualFrame->Sequence);
                  Oot3dNativeGame::ComposeTopScreenFrontendFrame(
                      *currentVisualFrame,
                      Oot3dNativeGame::ShouldSuppressTopScreenFrontendBackdrop(
                          launch.UiProfile,
                          uiLifecycleBridge.NativeFrontendPresentationActive()));
                  selectedNewVisualFrame = true;
                  ++visualSnapshotCount;
                  auto &sample = visualFrameSampleScratch;
                  if (!visualInterpolationEnabled) {
                    if (Oot3dNativeGame::ViewOot3dPicaVisualFrame(
                            *currentVisualFrame, sample)) {
                      const auto temporalSample =
                          Fast::Oot3d::BuildNativeFrameTemporalSample(
                              frameCompositionPolicy,
                              Fast::Oot3d::NativeFrameTemporalSampleKind::
                                  Authoritative,
                              currentVisualFrame->Sequence,
                              currentVisualFrame->Sequence,
                              visualContinuityEpoch, 1.0F,
                              static_cast<float>(
                                  presentationElapsedSeconds));
                      executeVisualSample(
                          sample,
                          Oot3dNativeGame::
                              Oot3dPicaPresentationExecutionKind::CurrentFrame,
                          true, temporalSample);
                      ++visualDirectFramesPresented;
                      ++visualSamplesPresented;
                      visualSampleDrawCount += sample.Draws.size();
                      presentedVisualSample = true;
                    }
                    // Direct gameplay modes retain no cross-frame matching
                    // history. The scheduler keeps only the executed scanout
                    // snapshot needed if presentation temporarily outruns
                    // simulation.
                    preparedVisualTransition.Reset();
                    olderVisualTransition.Reset();
                    olderVisualFrame.reset();
                    olderVisualTransitionContinuous = false;
                    previousVisualFrame.reset();
                    latestVisualFrame.reset();
                    latestVisualTransitionContinuous = false;
                  } else {
                    bool resynchronizeVisualNamespace =
                        !latestVisualFrame.has_value();
                    if (latestVisualFrame.has_value()) {
                      std::swap(olderVisualTransition, preparedVisualTransition);
                      olderVisualTransitionContinuous =
                          latestVisualTransitionContinuous && previousVisualFrame.has_value() &&
                          olderVisualTransition.Ready();
                      const bool transitionPrepared =
                          preparedVisualTransition.Prepare(
                              *latestVisualFrame, *currentVisualFrame,
                              &visualInterpolationTiming);
                      const auto &transition =
                          preparedVisualTransition.Stats();
                      float sampleAlpha =
                          visualSampleCadence.Resolve(
                              true, visualInterpolationAlpha);
                      if (arrivalClockAlpha) {
                        observeVisualFrameArrival();
                        sampleAlpha = drawTimeInterpolationAlpha();
                      }
                      // Still before the previous frame: the older pair.
                      const bool olderSample = sampleAlpha < 0.0F && olderVisualTransitionContinuous;
                      sampleAlpha = olderSample ? sampleAlpha + 1.0F : std::max(sampleAlpha, 0.0F);
                      traceSample(currentVisualFrame->Sequence - (olderSample ? 1U : 0U), sampleAlpha);
                      const bool visualSampleReady =
                          olderSample
                              ? olderVisualTransition.Sample(*previousVisualFrame, *latestVisualFrame,
                                                             sampleAlpha, sample, &visualInterpolationTiming)
                              : transitionPrepared &&
                                    preparedVisualTransition.Sample(
                                        *latestVisualFrame, *currentVisualFrame,
                                        sampleAlpha, sample,
                                        &visualInterpolationTiming);
                      ++visualTransitionCount;
                      visualMatchedDrawCount += transition.MatchedDraws;
                      visualStrictUniqueMatchedDrawCount +=
                          transition.StrictUniqueMatchedDraws;
                      visualStrictOrdinalMatchedDrawCount +=
                          transition.StrictOrdinalMatchedDraws;
                      visualStructuralOrdinalMatchedDrawCount +=
                          transition.StructuralOrdinalMatchedDraws;
                      visualPipelineOrdinalMatchedDrawCount +=
                          transition.PipelineOrdinalMatchedDraws;
                      visualChangedContinuousDrawCount +=
                          transition.ChangedContinuousDraws;
                      visualChangedPerInstanceVertexDrawCount +=
                          transition.ChangedPerInstanceVertexDraws;
                      visualChangedPerVertexDrawCount +=
                          transition.ChangedPerVertexDraws;
                      visualAmbiguousPreviousDrawCount +=
                          transition.AmbiguousPreviousDraws;
                      visualAmbiguousCurrentDrawCount +=
                          transition.AmbiguousCurrentDraws;
                      visualUnmatchedPreviousDrawCount +=
                          transition.UnmatchedPreviousDraws;
                      visualUnmatchedCurrentDrawCount +=
                          transition.UnmatchedCurrentDraws;
                      const double continuityThreshold =
                          visualContinuityTracker.CurrentThreshold();
                      const bool continuousTransition =
                          transitionPrepared &&
                          visualContinuityTracker.Accept(transition);
                      latestVisualTransitionContinuous = continuousTransition;
                      static const bool continuityTrace = std::getenv("TRIAEVUM_CONTINUITY_TRACE") != nullptr;
                      if (continuityTrace && !continuousTransition)
                        std::fprintf(stderr,
                                     "TRIAEVUM_DISCONTINUITY prepared=%d coverage=%.3f median=%.3f threshold=%.3f "
                                     "camera=%zu/%zu top=%zu draws=%zu/%zu\n",
                                     transitionPrepared ? 1 : 0, transition.MatchedDrawCoverage,
                                     transition.MedianNormalizedContinuousDelta, continuityThreshold,
                                     transition.CameraDiscontinuousDraws, transition.CameraMatchedDraws,
                                     transition.TopTargetMatchedDraws, transition.PreviousDraws,
                                     transition.CurrentDraws);
                      visualDiscontinuityCount +=
                          continuousTransition ? 0U : 1U;
                      if (launch.ExtendedDiagnostics) {
                        if (visualTransitionEvents.size() == 1024U) {
                          visualTransitionEvents.erase(
                              visualTransitionEvents.begin());
                        }
                        visualTransitionEvents.push_back({
                            {"host_frame", frameCount},
                            {"previous_sequence",
                             latestVisualFrame->Sequence},
                            {"current_sequence",
                             currentVisualFrame->Sequence},
                            {"matched_draws", transition.MatchedDraws},
                            {"strict_unique_matched_draws",
                             transition.StrictUniqueMatchedDraws},
                            {"strict_ordinal_matched_draws",
                             transition.StrictOrdinalMatchedDraws},
                            {"structural_ordinal_matched_draws",
                             transition.StructuralOrdinalMatchedDraws},
                            {"pipeline_ordinal_matched_draws",
                             transition.PipelineOrdinalMatchedDraws},
                            {"changed_per_instance_vertex_draws",
                             transition.ChangedPerInstanceVertexDraws},
                            {"changed_per_vertex_draws",
                             transition.ChangedPerVertexDraws},
                            {"unmatched_previous_draws",
                             transition.UnmatchedPreviousDraws},
                            {"unmatched_current_draws",
                             transition.UnmatchedCurrentDraws},
                            {"top_target_matched_draws",
                             transition.TopTargetMatchedDraws},
                            {"matched_draw_coverage",
                             transition.MatchedDrawCoverage},
                            {"median_normalized_continuous_delta",
                             transition.MedianNormalizedContinuousDelta},
                            {"p90_normalized_continuous_delta",
                             transition.P90NormalizedContinuousDelta},
                            {"continuity_threshold", continuityThreshold},
                            {"continuous", continuousTransition},
                        });
                      }
                      if (continuousTransition && visualSampleReady) {
                        const auto temporalSample =
                            Fast::Oot3d::BuildNativeFrameTemporalSample(
                                frameCompositionPolicy,
                                Fast::Oot3d::NativeFrameTemporalSampleKind::
                                    Transition,
                                olderSample ? previousVisualFrame->Sequence : latestVisualFrame->Sequence,
                                olderSample ? latestVisualFrame->Sequence : currentVisualFrame->Sequence,
                                visualContinuityEpoch, sampleAlpha,
                                static_cast<float>(
                                    presentationElapsedSeconds));
                        executeVisualSample(
                            sample,
                            Oot3dNativeGame::
                                Oot3dPicaPresentationExecutionKind::
                                    InterpolatedFrame,
                            true, temporalSample);
                        ++visualSamplesPresented;
                        visualSampleDrawCount += sample.Draws.size();
                        visualInterpolatedDrawCount +=
                            sample.InterpolatedDraws;
                        visualInterpolatedPerInstanceVertexDrawCount +=
                            sample.InterpolatedPerInstanceVertexDraws;
                        visualInterpolatedPerVertexDrawCount +=
                            sample.InterpolatedPerVertexDraws;
                        presentedVisualSample = true;
                      } else {
                        resynchronizeVisualNamespace = true;
                      }
                    }
                    if (resynchronizeVisualNamespace &&
                        Oot3dNativeGame::ViewOot3dPicaVisualFrame(
                            *currentVisualFrame, sample)) {
                      preparedVisualTransition.Reset();
                      olderVisualTransition.Reset();
                      olderVisualFrame.reset();
                      olderVisualTransitionContinuous = false;
                      ++visualContinuityEpoch;
                      const auto temporalSample =
                          Fast::Oot3d::BuildNativeFrameTemporalSample(
                              frameCompositionPolicy,
                              Fast::Oot3d::NativeFrameTemporalSampleKind::
                                  Resynchronized,
                              currentVisualFrame->Sequence,
                              currentVisualFrame->Sequence,
                              visualContinuityEpoch, 1.0F,
                              static_cast<float>(
                                  presentationElapsedSeconds));
                      executeVisualSample(
                          sample,
                          Oot3dNativeGame::
                              Oot3dPicaPresentationExecutionKind::CurrentFrame,
                          true, temporalSample);
                      ++visualNamespaceResyncCount;
                      ++visualSamplesPresented;
                      visualSampleDrawCount += sample.Draws.size();
                      presentedVisualSample = true;
                    }
                    if (latestVisualFrame.has_value()) {
                      olderVisualFrame = std::move(previousVisualFrame);
                      previousVisualFrame = std::move(latestVisualFrame);
                    } else {
                      latestVisualTransitionContinuous = false;
                    }
                    latestVisualFrame = std::move(currentVisualFrame);
                    // TRIAEVUM_INTERP_EVAL=<file>: each in-between measured
                    // against the real game frame it skips (see the function).
                    static std::FILE* interpolationEval = [] {
                      const char* path = std::getenv("TRIAEVUM_INTERP_EVAL");
                      return path != nullptr ? std::fopen(path, "w") : nullptr;
                    }();
                    if (interpolationEval != nullptr && olderVisualFrame.has_value() &&
                        previousVisualFrame.has_value() &&
                        olderVisualFrame->Sequence + 1U == previousVisualFrame->Sequence &&
                        previousVisualFrame->Sequence + 1U == latestVisualFrame->Sequence) {
                      Oot3dNativeGame::EvaluateOot3dPicaVisualInterpolation(
                          *olderVisualFrame, *previousVisualFrame, *latestVisualFrame, interpolationEval);
                      std::fflush(interpolationEval);
                    }
                  }
                }
              }
              if (!selectedNewVisualFrame && !presentedVisualSample &&
                  visualInterpolationEnabled &&
                  latestVisualTransitionContinuous &&
                  previousVisualFrame.has_value() &&
                  latestVisualFrame.has_value() &&
                  preparedVisualTransition.Ready()) {
                auto &sample = visualFrameSampleScratch;
                const float clockAlpha = drawTimeInterpolationAlpha();
                const bool olderSample = arrivalClockAlpha && clockAlpha < 0.0F &&
                                         olderVisualTransitionContinuous && olderVisualFrame.has_value();
                const float sampleAlpha =
                    olderSample ? clockAlpha + 1.0F : visualSampleCadence.Resolve(false, clockAlpha);
                traceSample(latestVisualFrame->Sequence - (olderSample ? 1U : 0U), sampleAlpha);
                if (olderSample
                        ? olderVisualTransition.Sample(*olderVisualFrame, *previousVisualFrame, sampleAlpha,
                                                       sample, &visualInterpolationTiming)
                        : preparedVisualTransition.Sample(
                              *previousVisualFrame, *latestVisualFrame, sampleAlpha,
                              sample, &visualInterpolationTiming)) {
                  const auto temporalSample =
                      Fast::Oot3d::BuildNativeFrameTemporalSample(
                          frameCompositionPolicy,
                          Fast::Oot3d::NativeFrameTemporalSampleKind::
                              Transition,
                          olderSample ? olderVisualFrame->Sequence : previousVisualFrame->Sequence,
                          olderSample ? previousVisualFrame->Sequence : latestVisualFrame->Sequence,
                          visualContinuityEpoch, sampleAlpha,
                          static_cast<float>(presentationElapsedSeconds));
                  executeVisualSample(
                      sample,
                      Oot3dNativeGame::
                          Oot3dPicaPresentationExecutionKind::RepeatedFrame,
                      true, temporalSample);
                  ++visualSamplesPresented;
                  visualSampleDrawCount += sample.Draws.size();
                  visualInterpolatedDrawCount += sample.InterpolatedDraws;
                  visualInterpolatedPerInstanceVertexDrawCount +=
                      sample.InterpolatedPerInstanceVertexDraws;
                  visualInterpolatedPerVertexDrawCount +=
                      sample.InterpolatedPerVertexDraws;
                  presentedVisualSample = true;
                }
              }
              if (!presentedVisualSample &&
                  picaPresentationScheduler.HasSnapshot(
                      selected->second,
                      kVisualInterpolationRenderTargetNamespace)) {
                const auto started = std::chrono::steady_clock::now();
                if (!picaPresentationScheduler.PresentExisting(
                        api, selected->second,
                        kVisualInterpolationRenderTargetNamespace, &error)) {
                  throw std::runtime_error(
                      "native top-screen scheduled scanout failed: " + error);
                }
                phaseTiming.VisualReplaySeconds += SecondsSince(started);
                ++visualReusedSnapshotPresentations;
              }
            }
          }

          const bool presentNativeBottomFrontend =
              !foreignTitle &&
              Oot3dNativeGame::ShouldPresentNativeBottomFrontend(
                  launch.UiProfile,
                  uiLifecycleBridge.NativeFrontendPresentationActive());
          const auto bottomFramebuffer = hostServices.BottomFramebuffer();
          static const bool layoutDebug =
              std::getenv("TRIAEVUM_LAYOUT_DEBUG") != nullptr;
          if (layoutDebug && foreignTitle &&
              presentationFrameCount % 120U == 0U) {
            std::cerr << "[layout] frame " << presentationFrameCount
                      << std::hex << " top="
                      << (topFramebuffer ? topFramebuffer->AddressLeft : 0U)
                      << " bottom="
                      << (bottomFramebuffer ? bottomFramebuffer->AddressLeft
                                            : 0U)
                      << " outputs=";
            for (const auto &[output, transfer] : displayTransfersByOutput) {
              std::cerr << output << "#" << std::dec << transfer.CompletionId
                        << (picaPresentationScheduler.HasSnapshot(
                                transfer,
                                kVisualInterpolationRenderTargetNamespace)
                                ? "s "
                                : " ")
                        << std::hex;
            }
            std::cerr << std::dec << '\n';
          }
          if (foreignTitle && bottomFramebuffer.has_value() &&
              (screenPlacements.Lower.has_value() ||
               !screenPlacements.LowerCrops.empty())) {
            const uint32_t lowerAddress = bottomFramebuffer->AddressLeft;
            auto &cpuState = lowerScreenCpuStates[lowerAddress];
            auto lower = displayTransfersByOutput.find(lowerAddress);
            if (lower != displayTransfersByOutput.end() &&
                picaPresentationScheduler.LatestSnapshotCompletion(
                    lower->second, kVisualInterpolationRenderTargetNamespace) &&
                lower->second.CompletionId != cpuState.GpuCompletion) {
              cpuState.GpuCompletion = lower->second.CompletionId;
              cpuState.CpuNewest = false;
              lastLowerScreenTransfer = lower->second;
            }
            const uint32_t lowerStride = bottomFramebuffer->Stride;
            const uint32_t lowerFormat = bottomFramebuffer->Format & 7U;
            const uint32_t lowerBpp =
                lowerFormat == 0U ? 4U : (lowerFormat == 1U ? 3U : 2U);
            constexpr uint32_t lowerColumns = 240U;
            constexpr uint32_t lowerRows = 320U;
            const size_t lowerBytes =
                static_cast<size_t>(lowerStride) * lowerRows;
            (void)lowerBytes;
            const auto lowerRecord =
                submissionQueue.LastDisplayTransferToOutput(lowerAddress);
            cpuState.CpuNewest = lowerRecord.has_value() && !lowerRecord->Gpu;
            const auto lowerPhysical =
                picaMemoryView.TranslateGuest(lowerAddress, 1U);
            if (cpuState.CpuNewest &&
                lowerRecord->Serial != cpuState.UploadedSerial &&
                lowerPhysical.has_value() &&
                lowerStride >= lowerColumns * lowerBpp) {
              cpuState.UploadedSerial = lowerRecord->Serial;
              lowerScreenPixels.resize(
                  static_cast<size_t>(lowerColumns) * lowerRows * 4U);
              bool readable = true;
              std::vector<uint8_t> row(lowerStride);
              for (uint32_t y = 0; y < lowerRows && readable; ++y) {
                readable = process.Memory().ReadBytes(
                    lowerAddress + y * lowerStride, row);
                uint8_t *out = lowerScreenPixels.data() +
                               static_cast<size_t>(y) * lowerColumns * 4U;
                for (uint32_t x = 0; x < lowerColumns && readable; ++x) {
                  const uint8_t *in = row.data() + x * lowerBpp;
                  if (lowerFormat == 0U) {  // RGBA8 stored as A,B,G,R
                    out[0] = in[3]; out[1] = in[2]; out[2] = in[1];
                  } else if (lowerFormat == 1U) {  // RGB8 stored as B,G,R
                    out[0] = in[2]; out[1] = in[1]; out[2] = in[0];
                  } else {  // RGB565
                    const uint16_t value =
                        static_cast<uint16_t>(in[0] | (in[1] << 8U));
                    out[0] = static_cast<uint8_t>(((value >> 11U) & 31U) * 255U / 31U);
                    out[1] = static_cast<uint8_t>(((value >> 5U) & 63U) * 255U / 63U);
                    out[2] = static_cast<uint8_t>((value & 31U) * 255U / 31U);
                  }
                  out[3] = 0xFFU;
                  out += 4;
                }
              }
              // Odd key: never the output of a real display transfer.
              cpuState.Uploaded =
                  readable &&
                  api.UploadPicaDisplayImage(
                      kVisualInterpolationRenderTargetNamespace,
                      *lowerPhysical | 1U, lowerColumns, lowerRows,
                      lowerScreenPixels, &error);
            }
            if (layoutDebug && presentationFrameCount % 120U == 0U) {
              const auto latestCompletion =
                  lastLowerScreenTransfer.has_value()
                      ? picaPresentationScheduler.LatestSnapshotCompletion(
                            *lastLowerScreenTransfer,
                            kVisualInterpolationRenderTargetNamespace)
                      : std::nullopt;
              std::cerr << "[lower] cpuNewest=" << cpuState.CpuNewest
                        << " uploaded=" << cpuState.Uploaded
                        << " last=" << lastLowerScreenTransfer.has_value()
                        << " latest=" << latestCompletion.value_or(0U)
                        << " record=" << (lowerRecord ? lowerRecord->Gpu : -1)
                        << '\n';
            }
            // The lower screen itself (inset/overlay), then the HUD parts
            // cut from it.
            std::vector<Oot3dNativeGame::Oot3dPicaScreenPlacement> lowerPlacements;
            if (screenPlacements.Lower.has_value()) {
              const auto &rect = *screenPlacements.Lower;
              lowerPlacements.push_back(
                  {screenPlacements.Upper.has_value()
                       ? Oot3d::Renderer::PicaPresentationMode::Opaque
                       : Oot3d::Renderer::PicaPresentationMode::Replace,
                   rect.X, rect.Y, rect.Width, rect.Height,
                   static_cast<uint16_t>(Oot3dNativeGame::kTriAevumLowerScreenWidth),
                   true});
            }
            // The upper screen as presented when a menu opens (still
            // gameplay) becomes the pause background the guest would have
            // captured from its framebuffer.
            constexpr uint32_t kPauseFreezeAddress = 0x18A00005U;
            if (pauseFreezePending && lastTopScreenTransfer.has_value()) {
              pauseFreezePending = false;
              // The presented upper screen (its display image).
              pauseFrozen = api.CopyPicaDisplayImage(
                  kVisualInterpolationRenderTargetNamespace,
                  lastTopScreenTransfer->OutputPhysicalAddress, kPauseFreezeAddress, &error);
              if (!pauseFrozen && std::getenv("TRIAEVUM_SCREEN_DEBUG") != nullptr) {
                std::cerr << "[pause] freeze failed: " << error << '\n';
              }
              substitutedPauseTextures.clear();
            }
            if (pauseFrozen) {
              static const uint32_t transform = [] {
                const char *value = std::getenv("TRIAEVUM_PAUSE_TRANSFORM");
                // Transposed and mirrored both ways (display images keep
                // the framebuffer's rotation), at the texture's far end (the
                // guest draws all 256 lines, the last 240 visible) and opaque
                // (the capture is RGB565; the framebuffer's alpha is not).
                return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 0))
                                        : 31U;
              }();
              for (const uint32_t texture : widescreenProjection.PauseTextures) {
                if (std::find(substitutedPauseTextures.begin(),
                              substitutedPauseTextures.end(),
                              texture) != substitutedPauseTextures.end()) {
                  continue;
                }
                substitutedPauseTextures.push_back(texture);
                {
                  Fast::Renderer3ds::PicaRenderBackend::PicaTextureSubstitute substitute;
                  substitute.RenderTargetNamespace = kVisualInterpolationRenderTargetNamespace;
                  substitute.DisplayAddress = kPauseFreezeAddress;
                  substitute.TextureAddress = texture;
                  substitute.TextureWidth = 512U;
                  substitute.TextureHeight = 256U;
                  substitute.UsedWidth = 400U;
                  substitute.UsedHeight = 240U;
                  substitute.Transform = transform;
                  if (!api.SetPicaTextureSubstitute(substitute, &error)) {
                    std::cerr << "[pause] background copy failed: " << error << '\n';
                  }
                }
              }
            }
            if (pauseFrozen && std::getenv("TRIAEVUM_PAUSE_SHOW") != nullptr) {
              Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission frozenImage;
              frozenImage.CompletionId = 1U;
              frozenImage.InputPhysicalAddress = 0x18A00005U;
              frozenImage.OutputPhysicalAddress = 0x18A00005U;
              frozenImage.Transfer.InputSize = (400U << 16U) | 240U;
              frozenImage.Transfer.OutputSize = (400U << 16U) | 240U;
              frozenImage.Transfer.Flags = 0x00001000U;
              const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                  Oot3d::Renderer::PicaPresentationMode::Opaque, 0.55F, 0.05F, 0.4F, 0.4F,
                  400U, true};
              (void)Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                  api, frozenImage, true, &error, kVisualInterpolationRenderTargetNamespace,
                  placement.Mode, &placement);
            }
            const auto mergedLayout = mm3dMenuLayouts.find(mergedMenuScreen);
            if (mergedLayout != mm3dMenuLayouts.end() && mergedLayout->second.FrozenBackground &&
                pauseFrozen && screenPlacements.Upper.has_value()) {
              // The frozen gameplay frame over the upper screen (whose menu
              // UI is drawn again below, rearranged).
              Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission frozenImage;
              frozenImage.CompletionId = 1U;
              frozenImage.InputPhysicalAddress = kPauseFreezeAddress;
              frozenImage.OutputPhysicalAddress = kPauseFreezeAddress;
              frozenImage.Transfer.InputSize = (400U << 16U) | 240U;
              frozenImage.Transfer.OutputSize = (400U << 16U) | 240U;
              frozenImage.Transfer.Flags = 0x00001000U;
              const auto &upper = *screenPlacements.Upper;
              const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                  Oot3d::Renderer::PicaPresentationMode::Opaque, upper.X, upper.Y,
                  upper.Width, upper.Height, 0U, true};
              if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                      api, frozenImage, true, &error,
                      kVisualInterpolationRenderTargetNamespace, placement.Mode, &placement)) {
                throw std::runtime_error("menu background presentation failed: " + error);
              }
            }
            if ((!screenPlacements.UpperCrops.empty() || !screenPlacements.UpperFills.empty()) &&
                lastTopScreenTransfer.has_value()) {
              // The upper screen's UI layer, copied like the upper screen.
              auto upperUi = *lastTopScreenTransfer;
              upperUi.InputPhysicalAddress = kUpperUiLayerColorAddress;
              upperUi.OutputPhysicalAddress = kUpperUiLayerColorAddress | 5U;
              upperUi.Transfer.Flags &= ~(7U << 12U);  // RGBA8 output (keep alpha)
              if (Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                      api, upperUi, false, &error,
                      kVisualInterpolationRenderTargetNamespace) &&
                  // Coverage alpha -> straight alpha, then shown from there.
                  api.CopyPicaDisplayImage(kVisualInterpolationRenderTargetNamespace,
                                           upperUi.OutputPhysicalAddress,
                                           kUpperUiLayerColorAddress | 7U, &error, 32U)) {
                upperUi.InputPhysicalAddress = kUpperUiLayerColorAddress | 7U;
                upperUi.OutputPhysicalAddress = kUpperUiLayerColorAddress | 7U;
                // Backdrops first, opaque: the layer as drawn over black
                // (its premultiplied colors, before the conversion).
                auto upperUiOverBlack = upperUi;
                upperUiOverBlack.InputPhysicalAddress = kUpperUiLayerColorAddress | 5U;
                upperUiOverBlack.OutputPhysicalAddress = kUpperUiLayerColorAddress | 5U;
                for (const auto &fill : screenPlacements.UpperFills) {
                  const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                      Oot3d::Renderer::PicaPresentationMode::Opaque,
                      fill.Destination.X, fill.Destination.Y,
                      fill.Destination.Width, fill.Destination.Height, 0U,
                      true, fill.Source.X, fill.Source.Y,
                      fill.Source.X + fill.Source.Width,
                      fill.Source.Y + fill.Source.Height};
                  if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                          api, upperUiOverBlack, true, &error,
                          kVisualInterpolationRenderTargetNamespace, placement.Mode,
                          &placement)) {
                    throw std::runtime_error("menu backdrop presentation failed: " + error);
                  }
                }
                if (std::getenv("TRIAEVUM_UPPER_LAYER_DEBUG") != nullptr) {
                  screenPlacements.UpperCrops = {{{0.0F, 0.0F, 1.0F, 1.0F},
                                                  {0.5F, 0.0F, 0.5F, 0.5F}}};
                }
                for (const auto &crop : screenPlacements.UpperCrops) {
                  const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                      Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
                      crop.Destination.X, crop.Destination.Y,
                      crop.Destination.Width, crop.Destination.Height, 0U,
                      true, crop.Source.X, crop.Source.Y,
                      crop.Source.X + crop.Source.Width,
                      crop.Source.Y + crop.Source.Height};
                  if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                          api, upperUi, true, &error,
                          kVisualInterpolationRenderTargetNamespace,
                          placement.Mode, &placement)) {
                    throw std::runtime_error("upper UI layer presentation failed: " + error);
                  }
                }
              }
            }
            std::optional<Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission> hudLayer;
            if (hudLayerEnabled) {
              const uint64_t hudDraws = api.PicaHudLayerDrawCount();
              hudLayerIdleFrames = hudDraws != hudLayerLastDrawCount
                                       ? 0U
                                       : std::min(hudLayerIdleFrames + 1U, 1000U);
              hudLayerLastDrawCount = hudDraws;
            }
            const bool hudVisible = !hudLayerEnabled || hudLayerIdleFrames < 30U;
            if (!hudVisible) {
              screenPlacements.LowerCrops.clear();
            }
            if (hudLayerEnabled && !screenPlacements.LowerCrops.empty() &&
                !cpuState.CpuNewest && lastLowerScreenTransfer.has_value()) {
              // Copy the HUD layer like the lower screen (same lines).
              hudLayer = *lastLowerScreenTransfer;
              hudLayer->InputPhysicalAddress = kHudLayerColorAddress;
              hudLayer->OutputPhysicalAddress = kHudLayerColorAddress | 5U;
              if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                      api, *hudLayer, false, &error,
                      kVisualInterpolationRenderTargetNamespace)) {
                hudLayer.reset();  // not drawn yet
              }
            }
            std::optional<Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission> slotIconLayer;
            if (hudLayer.has_value() && slotIconLayerApplied) {
              slotIconLayer = *hudLayer;
              slotIconLayer->InputPhysicalAddress = kSlotIconLayerColorAddress;
              slotIconLayer->OutputPhysicalAddress = kSlotIconLayerColorAddress | 5U;
              if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                      api, *slotIconLayer, false, &error,
                      kVisualInterpolationRenderTargetNamespace)) {
                slotIconLayer.reset();  // not drawn yet
              }
            }
            // The slot buttons' bubbles under their icons: the gameplay HUD's
            // diamond, the row in Items/Masks.
            // (Only while the lower screen shows gameplay, as presented and
            // as the guest has it now: a menu's first frame kept them for a frame.)
            const auto gameplayOnly = [](const std::vector<std::string> &layouts) {
              return std::all_of(layouts.begin(), layouts.end(),
                                 [](const std::string &name) { return name == "TouchPanel"; });
            };
            const bool slotButtonsInHud =
                mm3dScreen == 1 && (mm3dAppliedHud == 0 || mm3dAppliedHud == 1) &&
                gameplayOnly(shownLowerLayouts) && gameplayOnly(widescreenProjection.LowerLayoutNames) &&
                !singleScreen.MergedMenu && screenPlacements.Upper.has_value() &&
                singleScreen.InsetBlend <= 0.0F && !singleScreen.LowerOverlay;
            const bool slotButtonsInMenu =
                singleScreen.MergedMenu && (mm3dScreen == 4 || mm3dScreen == 6);
            if (mm3dButtonLabels) mm3dButtonLabels->ClearSprites();
            if (mm3dSlotButtonsEnabled && hudLayer.has_value() &&
                (slotButtonsInHud || slotButtonsInMenu)) {
              constexpr uint32_t kSpriteSize = 192U;
              constexpr uint32_t kSpriteAddress = 0x18E00003U;  // odd: never a transfer output
              const auto texturePack =
                  Fast::Oot3d::GraphicsSettingsRuntime::Instance().Snapshot().TexturePacks.Azahar;
              const auto style = Oot3dNativeGame::Mm3dButtonStyleFromSettings(
                  !texturePack.ButtonStyleDirectory.empty(), texturePack.ButtonStyleDirectory);
              // The game's own button bubble, from the player's RomFS: the HD
              // pack's version of its atlas when the pack has one (the game's
              // 40 pixels blown up to the sprite's 192 were blurred next to
              // the pack's sharp A/B buttons). Again on every pack change.
              static const auto atlas = [&manifest]() -> std::optional<Oot3dNativeGame::Mm3dButtonAtlas> {
                std::string atlasError;
                auto loaded = Oot3dNativeGame::LoadMm3dButtonAtlas(manifest->RomFsImagePath,
                                                                   manifest->RomFsImageOffset,
                                                                   manifest->RomFsImageSize, &atlasError);
                if (!loaded.has_value()) {
                  std::fprintf(stderr, "[hud] button bubble unavailable (%s): drawn rings instead\n",
                               atlasError.c_str());
                }
                return loaded;
              }();
              static std::optional<Oot3dNativeGame::Mm3dButtonRing> bubble;
              static std::pair<uint64_t, size_t> bubblePack{0U, 0U};  // generation, indexed textures
              static int uploadedStyle = -1;
              auto &texturePacks = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
              const auto packStatus = texturePacks.Snapshot();
              if (atlas.has_value() &&
                  bubblePack != std::pair<uint64_t, size_t>{packStatus.Generation, packStatus.IndexedTextures}) {
                bubblePack = {packStatus.Generation, packStatus.IndexedTextures};
                bubble.reset();
                static const bool noHdBubble = std::getenv("TRIAEVUM_NO_HD_BUTTON_BUBBLE") != nullptr;
                if (!noHdBubble && texturePack.LoadCustomTextures) {
                  // The pack's hash is of the level-0 texels (ETC1A4: a byte each).
                  const size_t levelBytes = static_cast<size_t>(atlas->Width) * atlas->Height;
                  const auto replacement = texturePacks.ResolveAndMaybeDump(::Oot3d::Renderer::AzaharTextureRequest{
                      .Width = static_cast<uint16_t>(atlas->Width),
                      .Height = static_cast<uint16_t>(atlas->Height),
                      .NativeFormat = 0x0DU,
                      .MipLevel = 0U,
                      .NativeBytes = std::span<const uint8_t>(atlas->Native.data(),
                                                              std::min(levelBytes, atlas->Native.size())),
                      .NativeRgba8 = std::span<const uint8_t>(atlas->Rgba),
                  });
                  if (replacement != nullptr && replacement->Rgba8 != nullptr)
                    bubble = Oot3dNativeGame::Mm3dButtonRingFromAtlas(*replacement->Rgba8, replacement->Width,
                                                                      replacement->Height);
                  std::fprintf(stderr, "[hud] button bubble: %s\n",
                               bubble.has_value()
                                   ? ("HD pack " + std::to_string(replacement->Width) + "x" +
                                      std::to_string(replacement->Height))
                                         .c_str()
                                   : (replacement != nullptr ? ("pack atlas unusable, the game's: " +
                                                                replacement->SourcePath.string())
                                                                   .c_str()
                                                             : "the game's (not in the pack)"));
                }
                if (!bubble.has_value()) bubble = Oot3dNativeGame::Mm3dButtonRingFromAtlas(atlas->Rgba, atlas->Width,
                                                                                            atlas->Height);
                uploadedStyle = -1;  // the sprites again with this bubble
              }
              static uint32_t uploadedLabels = 0;  // F1 > Buttons revision
              const uint32_t labelRevision = mm3dButtonLabels ? mm3dButtonLabels->Revision() : 0U;
              if (uploadedStyle != static_cast<int>(style) || uploadedLabels != labelRevision) {
                bool uploaded = true;
                uploadedLabels = labelRevision;
                for (int slot = 0; slot < 4 && uploaded; ++slot) {
                  const auto sprite = Oot3dNativeGame::Mm3dSpriteToFramebufferOrder(
                      Oot3dNativeGame::MakeMm3dHudButtonSprite(
                          kSpriteSize, Oot3dNativeGame::Mm3dHudButtonLabel(style, slot),
                          bubble.has_value() ? &*bubble : nullptr,
                          Oot3dNativeGame::Mm3dHudButtonLabelColor(style, slot),
                          mm3dButtonLabels ? mm3dButtonLabels->Label(slot)
                                           : Oot3dNativeGame::Mm3dButtonLabelLayout{}),
                      kSpriteSize);
                  uploaded = api.UploadPicaDisplayImage(kVisualInterpolationRenderTargetNamespace,
                                                        kSpriteAddress + static_cast<uint32_t>(slot) * 0x100U,
                                                        kSpriteSize, kSpriteSize, sprite, &error);
                }
                uploadedStyle = uploaded ? static_cast<int>(style) : -1;
              }
              const float windowWidth = static_cast<float>(width), windowHeight = static_cast<float>(height);
              for (int slot = 0; slot < 4 && uploadedStyle >= 0; ++slot) {
                const auto &button = kMm3dSlotButtons[static_cast<size_t>(slot)];
                // Circle diameter and center in window pixels.
                float diameter = 0.0F, centerX = 0.0F, centerY = 0.0F;
                if (slotButtonsInHud) {
                  const auto &upper = *screenPlacements.Upper;
                  const float upperHeight = upper.Height * windowHeight;
                  diameter = kMm3dSlotDiameter * upperHeight;
                  centerX = (upper.X + upper.Width) * windowWidth - button.CenterFromRight * upperHeight;
                  centerY = upper.Y * windowHeight + button.CenterY * upperHeight;
                } else {
                  diameter = kMm3dMenuSlotDiameter * windowHeight;
                  centerX = button.MenuCenterX * windowHeight;
                  centerY = button.MenuCenterY * windowHeight;
                }
                // The sprite's circle: centered at 0.56 of it, radius 0.42.
                const float size = diameter * 0.5F / 0.42F;
                const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                    Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
                    (centerX - 0.56F * size) / windowWidth, (centerY - 0.56F * size) / windowHeight,
                    size / windowWidth, size / windowHeight, static_cast<uint16_t>(kSpriteSize), true};
                if (mm3dButtonLabels) {
                  mm3dButtonLabels->SetSprite(slot, {placement.X, placement.Y, placement.Width, placement.Height},
                                              Oot3dNativeGame::Mm3dHudButtonLabel(style, slot));
                }
                Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission sprite;
                sprite.CompletionId = 1U;
                sprite.InputPhysicalAddress = kSpriteAddress + static_cast<uint32_t>(slot) * 0x100U;
                sprite.OutputPhysicalAddress = sprite.InputPhysicalAddress;
                sprite.Transfer.InputSize = (kSpriteSize << 16U) | kSpriteSize;
                sprite.Transfer.OutputSize = sprite.Transfer.InputSize;
                sprite.Transfer.Flags = 0x00001000U;
                if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                        api, sprite, true, &error, kVisualInterpolationRenderTargetNamespace,
                        placement.Mode, &placement)) {
                  uploadedStyle = -1;  // display images were reset (state load): upload again
                  break;
                }
                if (slotIconLayer.has_value()) {
                  // 40 lower-screen units of icon fill 62% of the circle.
                  const float unit = diameter * 0.62F / 40.0F;
                  const float iconWidth = (button.SourceX1 - button.SourceX0) * unit;
                  const float iconHeight = (button.SourceY1 - button.SourceY0) * unit;
                  const Oot3dNativeGame::Oot3dPicaScreenPlacement icon{
                      Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
                      (centerX - iconWidth * 0.5F) / windowWidth, (centerY - iconHeight * 0.5F) / windowHeight,
                      iconWidth / windowWidth, iconHeight / windowHeight,
                      static_cast<uint16_t>(Oot3dNativeGame::kTriAevumLowerScreenWidth), true,
                      button.SourceX0 / Oot3dNativeGame::kTriAevumLowerScreenWidth,
                      button.SourceY0 / Oot3dNativeGame::kTriAevumScreenHeight,
                      button.SourceX1 / Oot3dNativeGame::kTriAevumLowerScreenWidth,
                      button.SourceY1 / Oot3dNativeGame::kTriAevumScreenHeight};
                  if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                          api, *slotIconLayer, true, &error, kVisualInterpolationRenderTargetNamespace,
                          icon.Mode, &icon)) {
                    throw std::runtime_error("slot icon presentation failed: " + error);
                  }
                }
              }
            }
            for (const auto &crop : screenPlacements.LowerCrops) {
              if (hudLayer.has_value()) {
                const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                    Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
                    crop.Destination.X, crop.Destination.Y,
                    crop.Destination.Width, crop.Destination.Height,
                    static_cast<uint16_t>(Oot3dNativeGame::kTriAevumLowerScreenWidth),
                    true, crop.Source.X, crop.Source.Y,
                    crop.Source.X + crop.Source.Width,
                    crop.Source.Y + crop.Source.Height};
                if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                        api, *hudLayer, true, &error,
                        kVisualInterpolationRenderTargetNamespace,
                        placement.Mode, &placement)) {
                  throw std::runtime_error("HUD layer presentation failed: " + error);
                }
                continue;
              }
              lowerPlacements.push_back(
                  {Oot3d::Renderer::PicaPresentationMode::Opaque,
                   crop.Destination.X, crop.Destination.Y,
                   crop.Destination.Width, crop.Destination.Height,
                   static_cast<uint16_t>(Oot3dNativeGame::kTriAevumLowerScreenWidth),
                   true, crop.Source.X, crop.Source.Y,
                   crop.Source.X + crop.Source.Width,
                   crop.Source.Y + crop.Source.Height});
            }
            for (const auto &placement : lowerPlacements) {
            if (cpuState.CpuNewest && cpuState.Uploaded &&
                lowerPhysical.has_value()) {
              Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission cpuImage;
              cpuImage.CompletionId = 1U;
              cpuImage.InputPhysicalAddress = *lowerPhysical | 1U;
              cpuImage.OutputPhysicalAddress = *lowerPhysical | 1U;
              cpuImage.Transfer.InputSize = (lowerRows << 16U) | lowerColumns;
              cpuImage.Transfer.OutputSize = (lowerRows << 16U) | lowerColumns;
              cpuImage.Transfer.Flags = 0x00001000U;
              if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                      api, cpuImage, true, &error,
                      kVisualInterpolationRenderTargetNamespace,
                      placement.Mode, &placement)) {
                throw std::runtime_error(
                    "lower-screen presentation failed: " + error);
              }
            } else if (lastLowerScreenTransfer.has_value() &&
                       picaPresentationScheduler.LatestSnapshotCompletion(
                           *lastLowerScreenTransfer,
                           kVisualInterpolationRenderTargetNamespace)) {
              // Present whatever the GPU last wrote to this output.
              auto latest = *lastLowerScreenTransfer;
              latest.CompletionId =
                  *picaPresentationScheduler.LatestSnapshotCompletion(
                      latest, kVisualInterpolationRenderTargetNamespace);
              if (!picaPresentationScheduler.PresentExisting(
                      api, latest,
                      kVisualInterpolationRenderTargetNamespace, &error,
                      &placement)) {
                throw std::runtime_error(
                    "lower-screen presentation failed: " + error);
              }
            }
            }
            // The give-an-item cursor over the lower screen's inset.
            if (mm3dGiveMenu && screenPlacements.Lower.has_value()) {
              constexpr uint32_t kCursorSize = 96U;
              constexpr uint32_t kCursorAddress = 0x18E10003U;  // odd: never a transfer output
              static bool cursorUploaded = false;
              if (!cursorUploaded) {
                std::vector<uint8_t> rgba(kCursorSize * kCursorSize * 4U, 0U);
                for (uint32_t y = 0; y < kCursorSize; ++y) {
                  for (uint32_t x = 0; x < kCursorSize; ++x) {
                    // Rounded square ring, 6 px wide, with a soft outer glow.
                    const float h = kCursorSize * 0.5F, r = 10.0F;
                    const float qx = std::max(std::abs(x + 0.5F - h) - (h - 4.0F - r), 0.0F);
                    const float qy = std::max(std::abs(y + 0.5F - h) - (h - 4.0F - r), 0.0F);
                    const float d = std::hypot(qx, qy) - r;  // 0 on the ring's outer edge
                    const float ring = std::clamp(0.5F - std::abs(d + 3.0F) + 3.0F, 0.0F, 1.0F);
                    const float glow = std::clamp(1.0F - std::abs(d + 3.0F) / 7.0F, 0.0F, 1.0F) * 0.35F;
                    uint8_t *p = &rgba[(y * kCursorSize + x) * 4U];
                    p[0] = 255; p[1] = 216; p[2] = 48;
                    p[3] = static_cast<uint8_t>(std::lround(std::max(ring, glow) * 255.0F));
                  }
                }
                cursorUploaded = api.UploadPicaDisplayImage(kVisualInterpolationRenderTargetNamespace,
                                                            kCursorAddress, kCursorSize, kCursorSize, rgba, &error);
              }
              if (cursorUploaded) {
                const auto &rect = *screenPlacements.Lower;
                const auto cell = mm3dGiveCell(mm3dGiveColumn, mm3dGiveRow);
                const float w = 50.0F / Oot3dNativeGame::kTriAevumLowerScreenWidth * rect.Width;
                const float h = 50.0F / Oot3dNativeGame::kTriAevumScreenHeight * rect.Height;
                const Oot3dNativeGame::Oot3dPicaScreenPlacement placement{
                    Oot3d::Renderer::PicaPresentationMode::AlphaOverlay,
                    rect.X + cell.first / Oot3dNativeGame::kTriAevumLowerScreenWidth * rect.Width - w * 0.5F,
                    rect.Y + cell.second / Oot3dNativeGame::kTriAevumScreenHeight * rect.Height - h * 0.5F,
                    w, h, static_cast<uint16_t>(kCursorSize), true};
                Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission cursor;
                cursor.CompletionId = 1U;
                cursor.InputPhysicalAddress = kCursorAddress;
                cursor.OutputPhysicalAddress = kCursorAddress;
                cursor.Transfer.InputSize = (kCursorSize << 16U) | kCursorSize;
                cursor.Transfer.OutputSize = cursor.Transfer.InputSize;
                cursor.Transfer.Flags = 0x00001000U;
                if (!Oot3dNativeGame::SubmitOot3dPicaVulkanDisplayTransfer(
                        api, cursor, true, &error, kVisualInterpolationRenderTargetNamespace,
                        placement.Mode, &placement)) {
                  cursorUploaded = false;
                }
              }
            }
          }
          if (presentNativeBottomFrontend &&
              bottomFramebuffer.has_value()) {
            ++nativeFrontendPresentationActiveCount;
            auto selectedBottom =
                displayTransfersByOutput.find(bottomFramebuffer->AddressLeft);
            if (selectedBottom == displayTransfersByOutput.end() &&
                bottomFramebuffer->AddressRight != 0U &&
                bottomFramebuffer->AddressRight !=
                    bottomFramebuffer->AddressLeft) {
              selectedBottom = displayTransfersByOutput.find(
                  bottomFramebuffer->AddressRight);
            }
            nativeFrontendBottomTransferHitCount +=
                selectedBottom != displayTransfersByOutput.end() ? 1U : 0U;
            if (selectedBottom != displayTransfersByOutput.end() &&
                picaPresentationScheduler.HasSnapshot(
                    selectedBottom->second,
                    kVisualInterpolationRenderTargetNamespace)) {
              if (!picaPresentationScheduler.PresentExisting(
                      api, selectedBottom->second,
                      kVisualInterpolationRenderTargetNamespace, &error)) {
                throw std::runtime_error(
                    "native bottom-screen frontend presentation failed: " +
                    error);
              }
              ++nativeBottomFrontendPresentationCount;
            }
          }
        }
        phaseTiming.VisualPresentationSeconds +=
            SecondsSince(visualPresentationStart);
      }
      if (advanceGuest) {
        phaseStart = std::chrono::steady_clock::now();
        processResult =
            RunUntilGuestWait(process, launch.WholeAotBlockBudget);
        phaseTiming.GuestSeconds += SecondsSince(phaseStart);
        RequireRunnableGuest(processResult);

        phaseStart = std::chrono::steady_clock::now();
        const auto picaSubmitStart = phaseStart;
        CpuProbe::Scope cpuQueue(CpuProbe::Phase::PicaQueue);
        size_t drainPassesThisRefresh = 0;
        bool refreshHadNativeDraws = false;
        while (drainPassesThisRefresh < kMaximumPicaDrainPassesPerRefresh) {
          publishNativeSceneView();
          submissionQueue.TakePendingDraws(drainedDraws);
          auto &draws = drainedDraws;
          auto completions = submissionQueue.TakePendingCompletions();
          auto displayTransfers = submissionQueue.TakePendingDisplayTransfers();
          auto memoryFills = submissionQueue.TakePendingMemoryFills();
          if (draws.empty() && completions.empty() &&
              displayTransfers.empty() && memoryFills.empty()) {
            break;
          }
          ++drainPassesThisRefresh;
          ++picaDrainPassCount;
          refreshHadNativeDraws = refreshHadNativeDraws || !draws.empty();
          const bool suppressTopScreenFrontendBackdrop =
              Oot3dNativeGame::ShouldSuppressTopScreenFrontendBackdrop(
                  launch.UiProfile,
                  uiLifecycleBridge.NativeFrontendPresentationActive());
          const auto commonBackgroundGuestAddress =
              suppressTopScreenFrontendBackdrop
                  ? uiLifecycleBridge.NativePauseSharedTextureGuestAddress(
                        oot3d::ui::UiPauseSharedTextureSlot::CommonBackground00)
                  : std::nullopt;
          const auto commonBackgroundPhysicalAddress =
              commonBackgroundGuestAddress.has_value()
                  ? picaMemoryView.TranslateGuest(*commonBackgroundGuestAddress,
                                                  1U)
                  : std::nullopt;
          std::vector<Oot3dNativeGame::TopScreenFrontendCanvas>
              frontendBackdropCanvases;
          const auto rememberFrontendBackdropCanvas =
              [&](const Oot3dNativeGame::Oot3dPicaDecodedDrawState& state) {
                if (state.Framebuffer.ColorPhysicalAddress != 0U &&
                    std::none_of(frontendBackdropCanvases.begin(),
                                 frontendBackdropCanvases.end(),
                                 [&](const auto& canvas) {
                                   return canvas.Matches(state);
                                 })) {
                  frontendBackdropCanvases.push_back(
                      {state.Framebuffer.ColorPhysicalAddress, state.Viewport});
                }
              };
          if (commonBackgroundPhysicalAddress.has_value()) {
            for (const auto &draw : draws) {
              const bool usesCommonBackground = std::any_of(
                  draw.State.Textures.begin(), draw.State.Textures.end(),
                  [&](const auto &texture) {
                    return texture.Enabled &&
                           texture.PhysicalAddress ==
                               *commonBackgroundPhysicalAddress;
                  });
              if (usesCommonBackground) {
                rememberFrontendBackdropCanvas(draw.State);
              }
            }
          }
          bool signaledPicaCompletion = false;
          size_t completionIndex = 0;
          size_t memoryFillIndex = 0;
          size_t displayTransferIndex = 0;
          const auto submitMemoryFill =
              [&](const Oot3dNativeGame::Oot3dPicaMemoryFillSubmission &fill) {
                picaSemanticTrace.RecordMemoryFill(
                    presentationFrameCount, frameCount, fill);
                picaPresentationScheduler.Capture(fill);
                if (fill.CompletionId != 0U) {
                  if (!fill.Interrupt.has_value() ||
                      !hostServices.SignalPicaInterrupt(process.Memory(),
                                                        *fill.Interrupt)) {
                    throw std::runtime_error(
                        "native PICA memory fill completion "
                        "could not enter the GSP relay queue");
                  }
                  if (*fill.Interrupt ==
                      Oot3dNativeGame::Oot3dPicaInterruptId::P3d) {
                    ++completedP3dCount;
                  } else if (*fill.Interrupt ==
                             Oot3dNativeGame::Oot3dPicaInterruptId::Ppf) {
                    ++completedPpfCount;
                  }
                  ++recordedCompletionCount;
                  signaledPicaCompletion = true;
                }
              };
          const auto submitDisplayTransfer =
              [&](const Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission
                      &transfer) {
                const auto topFramebuffer = hostServices.TopFramebuffer();
                const bool selectedForTop = topFramebuffer.has_value() &&
                                            (transfer.Transfer.OutputAddress ==
                                                 topFramebuffer->AddressLeft ||
                                             transfer.Transfer.OutputAddress ==
                                                 topFramebuffer->AddressRight);
                picaSemanticTrace.RecordDisplayTransfer(
                    presentationFrameCount, frameCount, transfer,
                    selectedForTop, suppressTopScreenFrontendBackdrop);
                if (launch.ExtendedDiagnostics) {
                  if (displayTransferEvents.size() == 64U) {
                    displayTransferEvents.erase(displayTransferEvents.begin());
                  }
                  displayTransferEvents.push_back({
                      {"input_address", transfer.Transfer.InputAddress},
                      {"output_address", transfer.Transfer.OutputAddress},
                      {"input_physical_address", transfer.InputPhysicalAddress},
                      {"output_physical_address",
                       transfer.OutputPhysicalAddress},
                      {"input_size", transfer.Transfer.InputSize},
                      {"output_size", transfer.Transfer.OutputSize},
                      {"flags", transfer.Transfer.Flags},
                      {"texture_copy_bytes", transfer.Transfer.TextureCopyBytes},
                      {"after_draw_submission_id",
                       transfer.AfterDrawSubmissionId},
                      {"signals_guest_interrupt", transfer.SignalInterrupt},
                      {"selected_for_top_at_submission", selectedForTop},
                      {"topscreen_frontend_backdrop_suppression_active",
                       suppressTopScreenFrontendBackdrop},
                  });
                }
                static const bool transferDebug = std::getenv("TRIAEVUM_TRANSFER_DEBUG") != nullptr;
                if (transferDebug) {
                  std::fprintf(stderr, "[xfer] game %llu in %08x out %08x insz %08x outsz %08x flags %08x copy %u after %llu top %d\n",
                               static_cast<unsigned long long>(frameCount), transfer.InputPhysicalAddress,
                               transfer.OutputPhysicalAddress, transfer.Transfer.InputSize,
                               transfer.Transfer.OutputSize, transfer.Transfer.Flags,
                               static_cast<unsigned>(transfer.Transfer.TextureCopyBytes),
                               static_cast<unsigned long long>(transfer.AfterDrawSubmissionId), selectedForTop ? 1 : 0);
                }
                picaPresentationScheduler.Capture(transfer);
                // A texture copy into VRAM stays on the GPU (a texture the
                // title samples): done as far as the guest is concerned, like a
                // display transfer, without flushing the frame's GPU work.
                static const bool cpuTextureCopies = std::getenv("TRIAEVUM_CPU_TEXTURE_COPIES") != nullptr;
                const bool gpuTextureCopy =
                    !cpuTextureCopies && transfer.Transfer.TextureCopyBytes != 0U &&
                    transfer.OutputPhysicalAddress >= 0x18000000U && transfer.OutputPhysicalAddress < 0x18600000U;
                if (transfer.SignalInterrupt && transfer.Transfer.TextureCopyBytes != 0U && !gpuTextureCopy) {
                  pendingTextureCopyCompletions.insert(transfer.CompletionId);
                }
                if (transfer.SignalInterrupt && (transfer.Transfer.TextureCopyBytes == 0U || gpuTextureCopy)) {
                  if (!hostServices.SignalPicaInterrupt(
                          process.Memory(),
                          Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)) {
                    throw std::runtime_error(
                        "native PICA display transfer completion could not "
                        "enter the GSP relay queue");
                  }
                  ++completedPpfCount;
                  ++recordedCompletionCount;
                  signaledPicaCompletion = true;
                }
                displayTransfersByOutput[transfer.Transfer.OutputAddress] =
                    transfer;
                ++displayTransferCount;
              };
          while (displayTransferIndex < displayTransfers.size() &&
                 displayTransfers[displayTransferIndex].AfterDrawSubmissionId <=
                     lastSubmittedDrawId) {
            submitDisplayTransfer(displayTransfers[displayTransferIndex++]);
          }
          for (auto &draw : draws) {
            while (memoryFillIndex < memoryFills.size() &&
                   memoryFills[memoryFillIndex].BeforeDrawSubmissionId <=
                       draw.Id) {
              submitMemoryFill(memoryFills[memoryFillIndex++]);
            }
            const bool usesCommonBackground =
                commonBackgroundPhysicalAddress.has_value() &&
                std::any_of(draw.State.Textures.begin(),
                            draw.State.Textures.end(),
                            [&](const auto &texture) {
                              return texture.Enabled &&
                                     texture.PhysicalAddress ==
                                         *commonBackgroundPhysicalAddress;
                            });
            const bool targetsFrontendBackdrop =
                std::any_of(frontendBackdropCanvases.begin(),
                            frontendBackdropCanvases.end(),
                            [&](const auto& canvas) {
                              return canvas.Matches(draw.State);
                            });
            const bool isOpaqueTargetInitialization =
                IsNativeFrontendOpaqueTargetInitializationDraw(draw);
            const bool suppressNativeFrontendBackdropDraw =
                suppressTopScreenFrontendBackdrop &&
                targetsFrontendBackdrop &&
                (usesCommonBackground || isOpaqueTargetInitialization);
            Oot3dNativeGame::Oot3dPicaVulkanDrawPlan plan;
            auto drawPhaseStart = std::chrono::steady_clock::now();
            if (!Oot3dNativeGame::
                    BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
                        draw, plan, &error, &shaderSourceCache)) {
              throw std::runtime_error("native PICA Vulkan plan failed: " +
                                       error);
            }
            phaseTiming.PicaPlanSeconds += SecondsSince(drawPhaseStart);
            picaSemanticTrace.RecordDraw(
                presentationFrameCount, frameCount, draw, plan,
                suppressNativeFrontendBackdropDraw, usesCommonBackground,
                isOpaqueTargetInitialization);
            if (suppressNativeFrontendBackdropDraw) {
              nativeMenuBackdropDrawOmissionCount +=
                  usesCommonBackground ? 1U : 0U;
              nativeMenuClearDrawOmissionCount +=
                  isOpaqueTargetInitialization ? 1U : 0U;
            }
            if (launch.ExtendedDiagnostics) {
              drawPhaseStart = std::chrono::steady_clock::now();
              if (drawEvents.size() == 128U) {
                drawEvents.erase(drawEvents.begin());
              }
              nlohmann::json textures = nlohmann::json::array();
              for (size_t slot = 0; slot < draw.State.Textures.size();
                   ++slot) {
                const auto &texture = draw.State.Textures[slot];
                if (!texture.Enabled) {
                  continue;
                }
                const auto planTexture = std::find_if(
                    plan.Textures.begin(), plan.Textures.end(),
                    [slot](const auto &candidate) {
                      return candidate.Slot == slot;
                    });
                const std::span<const uint8_t> nativeBytes =
                    planTexture != plan.Textures.end()
                        ? std::span<const uint8_t>(planTexture->NativeBytes)
                        : std::span<const uint8_t>();
                textures.push_back({
                    {"slot", slot},
                    {"width", texture.Width},
                    {"height", texture.Height},
                    {"physical_address", texture.PhysicalAddress},
                    {"format", texture.Format},
                    {"type", texture.Type},
                    {"native_byte_count", nativeBytes.size()},
                    {"native_byte_hash", HashDiagnosticBytes(nativeBytes)},
                    {"native_nonzero_bytes",
                     std::count_if(nativeBytes.begin(), nativeBytes.end(),
                                   [](const uint8_t byte) {
                                     return byte != 0U;
                                   })},
                });
              }
              nlohmann::json vertexBindings = nlohmann::json::array();
              for (const auto &binding : plan.VertexBindings) {
                constexpr size_t kDiagnosticByteLimit = 256U;
                const auto bindingBytes = binding.ResolvedBytes();
                const size_t byteCount =
                    std::min(bindingBytes.size(), kDiagnosticByteLimit);
                vertexBindings.push_back({
                    {"binding", binding.Binding},
                    {"byte_stride", binding.ByteStride},
                    {"per_instance",
                     binding.InputRate ==
                         Oot3dNativeGame::Oot3dPicaVertexInputRate::
                             PerInstance},
                    {"byte_count", bindingBytes.size()},
                    {"prefix_bytes",
                     std::vector<uint8_t>(bindingBytes.begin(),
                                          bindingBytes.begin() + byteCount)},
                });
              }
              nlohmann::json defaultAttributes = nlohmann::json::array();
              for (size_t attributeIndex = 0;
                   attributeIndex < draw.State.VertexInput.AttributeCount;
                   ++attributeIndex) {
                if (!draw.State.VertexInput.Attributes[attributeIndex]
                         .Default) {
                  continue;
                }
                defaultAttributes.push_back({
                    {"attribute", attributeIndex},
                    {"input_register",
                     draw.State.ShaderInterface
                         .InputRegisterByAttribute[attributeIndex]},
                    {"value", draw.Packet.DefaultAttributes[attributeIndex]},
                });
              }
              constexpr std::array<uint16_t, 6> kTevStageRegisters{
                  0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U};
              nlohmann::json tevStages = nlohmann::json::array();
              for (const uint16_t base : kTevStageRegisters) {
                tevStages.push_back({
                    {"base_register", base},
                    {"sources", draw.Packet.Registers[base]},
                    {"modifiers", draw.Packet.Registers[base + 1U]},
                    {"operations", draw.Packet.Registers[base + 2U]},
                    {"constant", draw.Packet.Registers[base + 3U]},
                    {"scales", draw.Packet.Registers[base + 4U]},
                });
              }
              drawEvents.push_back({
                  {"frame", frameCount},
                  {"submission_id", draw.Id},
                  {"command_list_address", draw.Packet.CommandListAddress},
                  {"command_list_offset_words",
                   draw.Packet.CommandListOffsetWords},
                  {"topscreen_frontend_backdrop_suppressed",
                   suppressNativeFrontendBackdropDraw},
                  {"native_frontend_background_texture", usesCommonBackground},
                  {"native_frontend_target_initialization",
                   isOpaqueTargetInitialization},
                  {"vertex_count", draw.State.VertexInput.VertexCount},
                  {"vertex_offset", draw.State.VertexInput.VertexOffset},
                  {"minimum_vertex_index", draw.MinimumVertexIndex},
                  {"maximum_vertex_index", draw.MaximumVertexIndex},
                  {"indexed", draw.State.VertexInput.Indexed},
                  {"indices_are_16_bit",
                   draw.State.VertexInput.IndicesAre16Bit},
                  {"attribute_count", draw.State.VertexInput.AttributeCount},
                  {"vulkan_base_vertex", plan.BaseVertex},
                  {"vulkan_vertex_bindings", plan.VertexBindings.size()},
                  {"vertex_shader_key", plan.VertexShader.StateKey},
                  {"fragment_shader_key", plan.FragmentShader.StateKey},
                  {"vertex_shader_source",
                   std::string(plan.ResolvedVertexShaderSource())},
                  {"fragment_shader_source",
                   std::string(plan.ResolvedFragmentShaderSource())},
                  {"vertex_uniform_floats", plan.VertexShader.Uniforms.Floats},
                  {"shader_output_mask", draw.State.ShaderInterface.OutputMask},
                  {"shader_output_total",
                   draw.Packet.Registers[0x04FU] & 7U},
                  {"geometry_shader_enabled",
                   draw.State.ShaderInterface.GeometryShaderEnabled},
                  {"geometry_shader_mode",
                   draw.Packet.Registers[0x229U] & 3U},
                  {"shader_output_maps",
                   {draw.Packet.Registers[0x050U],
                    draw.Packet.Registers[0x051U],
                    draw.Packet.Registers[0x052U],
                    draw.Packet.Registers[0x053U],
                    draw.Packet.Registers[0x054U],
                    draw.Packet.Registers[0x055U],
                    draw.Packet.Registers[0x056U]}},
                  {"framebuffer",
                   {{"color_address",
                     draw.State.Framebuffer.ColorPhysicalAddress},
                    {"depth_address",
                     draw.State.Framebuffer.DepthPhysicalAddress},
                    {"width", draw.State.Framebuffer.Width},
                    {"height", draw.State.Framebuffer.Height},
                    {"color_format", draw.State.Framebuffer.ColorFormat},
                    {"depth_format", draw.State.Framebuffer.DepthFormat},
                    {"flipped", draw.State.Framebuffer.Flipped}}},
                  {"viewport",
                   {{"half_width", draw.State.Viewport.HalfWidth},
                    {"half_height", draw.State.Viewport.HalfHeight},
                    {"depth_range", draw.State.Viewport.DepthRange},
                    {"near_plane", draw.State.Viewport.NearPlane},
                    {"z_buffering", draw.State.Viewport.ZBuffering},
                    {"corner_x", draw.State.Viewport.CornerX},
                    {"corner_y", draw.State.Viewport.CornerY}}},
                  {"scissor",
                   {{"mode", static_cast<uint32_t>(draw.State.Scissor.Mode)},
                    {"x1", draw.State.Scissor.X1},
                    {"y1", draw.State.Scissor.Y1},
                    {"x2", draw.State.Scissor.X2},
                    {"y2", draw.State.Scissor.Y2}}},
                  {"topology", static_cast<uint32_t>(draw.State.Topology)},
                  {"cull_mode", static_cast<uint32_t>(draw.State.CullMode)},
                  {"output_merger",
                   {{"fragment_operation_mode",
                     plan.State.OutputMerger.FragmentOperationMode},
                    {"color_write_mask",
                     plan.State.OutputMerger.ColorWriteMask},
                    {"logic_operation",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.LogicOperation)},
                    {"blend_enabled", plan.State.OutputMerger.Blend.Enabled},
                    {"blend_color_equation",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.ColorEquation)},
                    {"blend_source_color",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.SourceColor)},
                    {"blend_destination_color",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.DestinationColor)},
                    {"depth_test_enabled",
                     plan.State.OutputMerger.Depth.TestEnabled},
                    {"depth_write_enabled",
                     plan.State.OutputMerger.Depth.WriteEnabled},
                    {"depth_compare",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Depth.Compare)}}},
                  {"textures", std::move(textures)},
                  {"vertex_bindings", std::move(vertexBindings)},
                  {"default_attributes", std::move(defaultAttributes)},
                  {"tev_stages", std::move(tevStages)},
                  {"combiner_buffer_color", draw.Packet.Registers[0x0FDU]},
              });
              phaseTiming.PicaDiagnosticsSeconds +=
                  SecondsSince(drawPhaseStart);
            }
            if (!suppressNativeFrontendBackdropDraw) {
              picaPresentationScheduler.Capture(std::move(plan));
            }
            while (completionIndex < completions.size() &&
                   completions[completionIndex].AfterDrawSubmissionId ==
                       draw.Id) {
              const auto &completion = completions[completionIndex++];
              if (!hostServices.SignalPicaInterrupt(process.Memory(),
                                                    completion.Interrupt)) {
                throw std::runtime_error(
                    "native PICA draw completion could not "
                    "enter the GSP relay queue");
              }
              if (completion.Interrupt ==
                  Oot3dNativeGame::Oot3dPicaInterruptId::P3d) {
                ++completedP3dCount;
              } else if (completion.Interrupt ==
                         Oot3dNativeGame::Oot3dPicaInterruptId::Ppf) {
                ++completedPpfCount;
              }
              ++recordedCompletionCount;
              signaledPicaCompletion = true;
            }
            lastSubmittedDrawId = draw.Id;
            while (
                displayTransferIndex < displayTransfers.size() &&
                displayTransfers[displayTransferIndex].AfterDrawSubmissionId ==
                    draw.Id) {
              submitDisplayTransfer(displayTransfers[displayTransferIndex++]);
            }
            ++drawCount;
          }
          if (completionIndex != completions.size()) {
            throw std::runtime_error(
                "native PICA completion did not match a submitted draw");
          }
          while (memoryFillIndex < memoryFills.size()) {
            submitMemoryFill(memoryFills[memoryFillIndex++]);
          }
          if (displayTransferIndex != displayTransfers.size()) {
            throw std::runtime_error("native PICA display transfer did not "
                                     "match a submitted draw");
          }
          if (!signaledPicaCompletion) {
            break;
          }
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          const double resumedGuestSeconds = SecondsSince(phaseStart);
          phaseTiming.GuestSeconds += resumedGuestSeconds;
          phaseTiming.GuestInsidePicaSubmitSeconds += resumedGuestSeconds;
          RequireRunnableGuest(processResult);
        }
        if (drainPassesThisRefresh == kMaximumPicaDrainPassesPerRefresh &&
            (!submissionQueue.PendingDraws().empty() ||
             !submissionQueue.PendingCompletions().empty() ||
             !submissionQueue.PendingDisplayTransfers().empty() ||
             !submissionQueue.PendingMemoryFills().empty())) {
          throw std::runtime_error("native PICA completion chain exceeded "
                                   "the per-refresh drain limit");
        }
        maximumPicaDrainPassesPerRefresh = std::max<uint64_t>(
            maximumPicaDrainPassesPerRefresh, drainPassesThisRefresh);
        refreshesWithNativeDraws += refreshHadNativeDraws ? 1U : 0U;
        phaseTiming.PicaSubmitSeconds += SecondsSince(picaSubmitStart);
      }
      frameCount += advanceGuest ? 1U : 0U;
    }

    if (launch.ExtendedDiagnostics) {
      inputConsumerDiagnostics.Observe(process.Memory(), frameCount);
    }

    for (std::size_t subsystemIndex = 0;
         subsystemIndex < oot3d::ui::kUiSubsystemCount; ++subsystemIndex) {
      const auto subsystem =
          static_cast<oot3d::ui::UiSubsystem>(subsystemIndex);
      const bool topScreenProfile =
          launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
      if (topScreenProfile &&
          subsystem != oot3d::ui::UiSubsystem::GameplayHud &&
          subsystem != oot3d::ui::UiSubsystem::TouchControls) {
        continue;
      }
      const bool topScreenPresentation = topScreenProfile;
      if (!topScreenPresentation && !uiLifecycleBridge.Runtime()
                                         .PlanFrame(subsystem)
                                         .run_host_presentation) {
        continue;
      }
      auto primitives =
          topScreenPresentation
              ? uiLifecycleBridge.BuildTopScreenPresentation(subsystem)
              : uiLifecycleBridge.BuildShadowPresentation(subsystem);
      if (topScreenPresentation &&
          subsystem == oot3d::ui::UiSubsystem::GameplayHud) {
        Oot3dNativeGame::AppendTopScreenFreeCameraOptionPresentation(
            nativeCandidateDispatch.TopScreenCamera.Camera, primitives);
      }
      if (topScreenPresentation && subsystem == oot3d::ui::UiSubsystem::TouchControls) {
        ocarinaText.Append(primitives);
      }
      if (topScreenPresentation) {
        lastTopScreenPrimitivesBySubsystem[subsystemIndex] = primitives;
      }
      const auto canvasMode =
          topScreenPresentation
              ? oot3d::ui::N64UiCanvasMode::NativeTopScreen400x240
              : oot3d::ui::N64UiCanvasMode::Widescreen16x9;
      if (!n64UiRenderer.Render(primitives, width, height, canvasMode,
                                &error)) {
        throw std::runtime_error("integrated N64 UI presentation failed: " +
                                 error);
      }
    }

    if (foreignTitle) {
      // The guest's software keyboard: type the text on the PC keyboard.
      static std::array<char, 64> keyboardText{'L', 'i', 'n', 'k'};
      static bool keyboardFocus = true;
      if (const auto maxLength = hostServices.PendingKeyboardMaxLength()) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5F,
                   viewport->Pos.y + viewport->Size.y * 0.5F),
            ImGuiCond_Always, ImVec2(0.5F, 0.5F));
        ImGui::Begin("Enter a name", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(std::max(1.0F, viewport->Size.y / 400.0F));
        ImGui::TextUnformatted("Type a name and press Enter.");
        if (keyboardFocus) {
          ImGui::SetKeyboardFocusHere();
          keyboardFocus = false;
        }
        const size_t capacity =
            *maxLength == 0U
                ? keyboardText.size()
                : std::min<size_t>(keyboardText.size(), *maxLength + 1U);
        bool confirmed = ImGui::InputText("##keyboard_text", keyboardText.data(),
                                          capacity,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        confirmed = ImGui::Button("OK") || confirmed;
        ImGui::End();
        if (confirmed) {
          hostServices.CompleteKeyboardRequest(process.Memory(),
                                               keyboardText.data());
          keyboardFocus = true;
        }
      }
    }
    {
      // The author's thank-you message on the first launch: it closes only
      // after 5 seconds, then never shows again (thanks_shown next to the
      // config). Never in automated runs (input timeline or --max-seconds:
      // tests, Setup's shader tour), nor with TRIAEVUM_NO_THANKS.
      static const std::filesystem::path seenPath =
          std::filesystem::path(launch.Host.ConfigurationPath).parent_path() / "thanks_shown";
      static bool showThanks = [&] {
        std::error_code ignored;
        return launch.Host.InputTimelinePath.empty() && launch.Host.MaxSeconds <= 0.0 &&
               std::getenv("TRIAEVUM_NO_THANKS") == nullptr && !std::filesystem::exists(seenPath, ignored);
      }();
      static std::optional<std::chrono::steady_clock::time_point> thanksSince;
      if (showThanks) {
        const auto now = std::chrono::steady_clock::now();
        if (!thanksSince.has_value()) thanksSince = now;
        const double waited = std::chrono::duration<double>(now - *thanksSince).count();
        const int secondsLeft = static_cast<int>(std::ceil(5.0 - waited));
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5F,
                                       viewport->Pos.y + viewport->Size.y * 0.5F),
                                ImGuiCond_Always, ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(ImVec2(std::min(viewport->Size.x * 0.9F, viewport->Size.y * 1.1F), 0.0F),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("MM3D PC", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetWindowFontScale(std::max(1.0F, viewport->Size.y / 380.0F));
        ImGui::TextWrapped(
            "Hello! Thank you very much for testing, sorry for interrupting but i would like to ask if you "
            "could somehow spread this project around, i'm a single person working on this with only one AI "
            "agent, i'd love to make this as enjoyable as possible for everyone, cheers! :D");
        ImGui::Spacing();
        bool close = false;
        if (secondsLeft > 0) {
          ImGui::BeginDisabled();
          ImGui::Button(("OK (" + std::to_string(secondsLeft) + ")").c_str());
          ImGui::EndDisabled();
        } else {
          close = ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                  ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown);
        }
        ImGui::End();
        if (close) {
          showThanks = false;
          std::error_code ignored;
          std::filesystem::create_directories(seenPath.parent_path(), ignored);
          std::ofstream(seenPath) << "The thank-you message was shown.\n";
        }
      }
    }
    if (const std::string message = saveStateSlots->Message(); !message.empty()) {
      // A save state's result (or the newly selected slot) for 3 seconds, top center.
      const ImGuiViewport *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5F,
                                     viewport->Pos.y + viewport->Size.y * 0.04F),
                              ImGuiCond_Always, ImVec2(0.5F, 0.0F));
      ImGui::SetNextWindowBgAlpha(0.65F);
      ImGui::Begin("##savestate_message", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImGui::SetWindowFontScale(std::max(1.0F, viewport->Size.y / 720.0F));
      ImGui::TextUnformatted(message.c_str());
      ImGui::End();
    }
    phaseStart = std::chrono::steady_clock::now();
    CpuProbe::Scope cpuPresent(CpuProbe::Phase::Present);
    gui->EndDraw();
    MaybeWriteFramebufferScreenshot(
        hostArgs, api, width, height,
        static_cast<uint32_t>(presentationFrameCount), screenshotState,
        capturedTemporalSample ? &*capturedTemporalSample : nullptr);
    capturedTemporalSample.reset();
    const auto waitStart = std::chrono::steady_clock::now();
    CpuProbe::Scope cpuPacing(CpuProbe::Phase::Pacing);
    realtimePacer.WaitForNextRefresh();
    cpuPacing.Stop();
    phaseTiming.PacingWaitSeconds += SecondsSince(waitStart);
    window.EndFrame();
    picaSemanticTrace.RecordFrameBoundary(
        presentationFrameCount, frameCount, guestRefreshesDue != 0U);
    phaseTiming.PresentSeconds += SecondsSince(phaseStart);
    cpuPresent.Stop();
    ++presentationFrameCount;
    Oot3dNativeGame::TriAevumHeartbeat();
    {
      // The window runs but the game itself stopped (no guest frame for 20 s
      // outside the settings menu): reported once per stall.
      static uint32_t lastGuestFrame = 0;
      static auto lastGuestProgress = std::chrono::steady_clock::now();
      static bool stallReported = false;
      const auto now = std::chrono::steady_clock::now();
      if (frameCount != lastGuestFrame || gui->GetMenuOrMenubarVisible()) {
        lastGuestFrame = frameCount;
        lastGuestProgress = now;
        stallReported = false;
      } else if (!stallReported && frameCount != 0U &&
                 now - lastGuestProgress > std::chrono::seconds(20)) {
        stallReported = true;
        std::cerr << "TriAevum: the game has not advanced for 20 s (guest frame "
                  << frameCount << ")\n";
        Oot3dNativeGame::WriteTriAevumCrashReport(
            "freeze", "The window kept running but the game did not advance for more "
                      "than 20 seconds (stuck guest thread).");
      }
    }
    ++runFrameCount;
#if defined(__SWITCH__)
    gSwitchRuntimeHostFrames = runFrameCount;
    gSwitchRuntimeGuestFrames = frameCount;
    // Write once at the first frame and at most once more when a persistent
    // geometry is first observed. Periodic receipt writes would put
    // synchronous SD filesystem work directly in the presentation loop.
    if (runFrameCount == 1U || !gSwitchPicaGeometryCacheObserved) {
      const auto wholeAotStats =
          Oot3dNativeGame::GetOot3dCompiledFunctionStats();
      gSwitchWholeAotBlockLimitExits =
          wholeAotStats.WholeAotBlockLimitExits;
      const auto picaBackend = api.GetNativePicaBackendStats();
      gSwitchPicaGeometryCacheEnabled =
          picaBackend.GeometryCacheEnabled;
      gSwitchPicaGeometryPersistentDraws =
          picaBackend.GeometryPersistentDraws;
      gSwitchPicaGeometryPersistentUploads =
          picaBackend.GeometryPersistentUploads;
      gSwitchPicaGeometryRegistryEntries =
          picaBackend.GeometryRegistryEntries;
      gSwitchPicaGeometryCacheObserved =
          gSwitchPicaGeometryPersistentDraws != 0U &&
          gSwitchPicaGeometryPersistentUploads != 0U &&
          gSwitchPicaGeometryRegistryEntries != 0U;
      if (runFrameCount == 1U || gSwitchPicaGeometryCacheObserved) {
        WriteSwitchBootStage("runtime_running");
      }
    }
#endif
    const auto completedFrameTime = std::chrono::steady_clock::now();
    if (!benchmarkMeasurementStarted &&
        runFrameCount >= hostArgs.BenchmarkWarmupFrames) {
      benchmarkMeasurementStart = completedFrameTime;
      benchmarkMeasurementEnd = completedFrameTime;
      benchmarkMeasurementStarted = true;
      if (cpuProbeRequested) CpuProbe::Thread.StartAt(CpuProbe::Read());
    } else if (benchmarkMeasurementStarted &&
               runFrameCount > hostArgs.BenchmarkWarmupFrames) {
      const auto currentPhases = phaseTiming.Values();
      std::array<double, 15> delta{};
      for (size_t i=0; i<delta.size(); ++i) {
        delta[i]=currentPhases[i]-phaseBeforeFrame[i];
        benchmarkPhaseSeconds[i]+=delta[i];
      }
      const double nestedGuest=phaseTiming.GuestInsidePicaSubmitSeconds-nestedGuestBeforeFrame;
      benchmarkNestedGuestSeconds+=nestedGuest;
      benchmarkGuestClockSeconds+=phaseTiming.GuestClockSeconds-guestClockBeforeFrame;
      const Oot3dNativeGame::PreBackendBudget budget{
        delta[0],delta[7],nestedGuest,delta[8],delta[2],delta[3],delta[5],delta[6]};
      if(budget.Valid()) preBackendTimes.RecordMilliseconds(budget.Total()*1000.0);
      else ++invalidPreBackendIntervals;
      // Present includes pacing. Visual presentation includes backend/replay.
      // Remove each inclusive scope once; keep unclassified host/UI work visible.
      const double envelope=std::chrono::duration<double>(completedFrameTime-benchmarkMeasurementEnd).count()
          -delta[4]-delta[11]-delta[13];
      preBackendEnvelopeTimes.RecordMilliseconds(envelope*1000.0);
      benchmarkUnattributedSeconds+=envelope-budget.Total();
      if (benchmarkFrameTimes) {
        auto phases = phaseTiming.Values();
        for (size_t i = 0; i < phases.size(); ++i)
          phases[i] = (phases[i] - phaseBeforeFrame[i]) * 1000.0;
        slowFrames.Record(runFrameCount, std::chrono::duration<double, std::milli>(
            completedFrameTime - benchmarkMeasurementEnd).count(), phases);
        benchmarkFrameTimes->RecordMilliseconds(
            std::chrono::duration<double, std::milli>(
                completedFrameTime - benchmarkMeasurementEnd).count());
      }
      benchmarkMeasurementEnd = completedFrameTime;
      ++benchmarkMeasuredFrames;
      if (cpuProbeRequested) benchmarkCpuPhases = CpuProbe::Thread.Snapshot();
    }
    if (const int saveSlot = saveStateSlots->TakeSave(); saveSlot != 0) saveSlotPending = saveSlot;
    if (const int loadSlot = saveStateSlots->TakeLoad(); loadSlot != 0) {
      const std::string name = "Slot " + std::to_string(loadSlot);
      std::error_code ignored;
      if (!std::filesystem::exists(saveStateSlots->Path(loadSlot), ignored)) {
        saveStateSlots->Report(name + " is empty");
      } else if (loadState(saveStateSlots->Path(loadSlot))) {
        saveSlotPending = 0;
        saveStateSlots->Report(name + " loaded");
      } else {
        saveStateSlots->Report(name + " could not be loaded (saved by another version of the port?)");
      }
    } else {
      if (saveSlotPending != 0 && saveState(saveStateSlots->Path(saveSlotPending))) {
        saveStateSlots->Report("Saved to slot " + std::to_string(saveSlotPending));
        saveSlotPending = 0;
      }
      if (Oot3dNativeGame::ShouldCaptureNativeA32AutomatedState(
              launch.SaveStateFrameAvailable, automatedStateSaved,
              runFrameCount, launch.SaveStateFrame) &&
          saveState(launch.SaveStatePath)) {
        automatedStateSaved = true;
        automatedStateSavedAtRunFrame = runFrameCount - 1U;
        automatedStateSavedAtGuestFrame = frameCount;
      }
    }
    ApplyWindowDemoFrameLimit(hostArgs, window, runFrameCount);
  }
  CpuProbe::Thread.Active = false;
  if (processResult.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    std::fprintf(stderr, "oot3d_native_game: the game's process ended (all its threads exited)\n");
  }

#if defined(__SWITCH__)
  WriteSwitchBootStage(
      processResult.Kind ==
              Oot3dNativeGame::NativeA32ProcessRunKind::Terminated
          ? "runtime_guest_terminated"
          : "runtime_window_closed");
#endif

  if (!launch.AudioPcmDumpPath.empty()) {
    Oot3dNativeGame::WriteStereoPcm16Wave(
        launch.AudioPcmDumpPath,
        Oot3dNativeGame::NativeA32DspHle::NativeSampleRate, pcmDumpSamples);
  }

  blockTraceWriter.Write();
  picaSemanticTrace.Finish();

  if (!launch.Host.OutputPath.empty()) {
    nlohmann::json topFramebuffer = nullptr;
    if (const auto top = hostServices.TopFramebuffer(); top.has_value()) {
      topFramebuffer = {
          {"address_left", top->AddressLeft},
          {"address_right", top->AddressRight},
          {"stride", top->Stride},
          {"format", top->Format},
          {"buffer_index", top->BufferIndex},
      };
    }
    nlohmann::json bottomFramebuffer = nullptr;
    if (const auto bottom = hostServices.BottomFramebuffer();
        bottom.has_value()) {
      bottomFramebuffer = {
          {"address_left", bottom->AddressLeft},
          {"address_right", bottom->AddressRight},
          {"stride", bottom->Stride},
          {"format", bottom->Format},
          {"buffer_index", bottom->BufferIndex},
      };
    }
    nlohmann::json recentSvcEvents = nlohmann::json::array();
    nlohmann::json filesystemSvcEvents = nlohmann::json::array();
    nlohmann::json ipcSvcEvents = nlohmann::json::array();
    const auto &svcEvents = hostServices.SvcEvents();
    const size_t firstSvcEvent =
        svcEvents.size() > 32U ? svcEvents.size() - 32U : 0U;
    for (size_t index = firstSvcEvent; index < svcEvents.size(); ++index) {
      const auto &event = svcEvents[index];
      recentSvcEvents.push_back({
          {"immediate", event.Immediate},
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    size_t firstFilesystemEvent = 0;
    size_t filesystemEventCount = 0;
    for (size_t index = svcEvents.size(); index > 0; --index) {
      const auto &event = svcEvents[index - 1U];
      if (event.Name.find("fs:USER") == std::string::npos &&
          event.Name.find("file:romfs") == std::string::npos &&
          event.Name.find("file:savedata") == std::string::npos) {
        continue;
      }
      firstFilesystemEvent = index - 1U;
      if (++filesystemEventCount == 64U) {
        break;
      }
    }
    for (size_t index = firstFilesystemEvent; index < svcEvents.size();
         ++index) {
      const auto &event = svcEvents[index];
      if (event.Name.find("fs:USER") == std::string::npos &&
          event.Name.find("file:romfs") == std::string::npos &&
          event.Name.find("file:savedata") == std::string::npos) {
        continue;
      }
      filesystemSvcEvents.push_back({
          {"immediate", event.Immediate},
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    size_t firstIpcEvent = 0;
    size_t ipcEventCount = 0;
    for (size_t index = svcEvents.size(); index > 0; --index) {
      const auto &event = svcEvents[index - 1U];
      if (event.Immediate != 0x32U ||
          event.Name.find("file:romfs:Read") != std::string::npos ||
          event.Name.find("gsp::Gpu:FlushDataCache") != std::string::npos) {
        continue;
      }
      firstIpcEvent = index - 1U;
      if (++ipcEventCount == 128U) {
        break;
      }
    }
    for (size_t index = firstIpcEvent; index < svcEvents.size(); ++index) {
      const auto &event = svcEvents[index];
      if (event.Immediate != 0x32U ||
          event.Name.find("file:romfs:Read") != std::string::npos ||
          event.Name.find("gsp::Gpu:FlushDataCache") != std::string::npos) {
        continue;
      }
      ipcSvcEvents.push_back({
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    std::map<uint32_t, const Oot3dNativeGame::NativeA32CtrSvcEvent *>
        lastSvcByThread;
    for (const auto &event : svcEvents) {
      lastSvcByThread[event.ThreadId] = &event;
    }
    nlohmann::json lastThreadSvcEvents = nlohmann::json::array();
    for (const auto &[threadId, event] : lastSvcByThread) {
      lastThreadSvcEvents.push_back({
          {"thread_id", threadId},
          {"immediate", event->Immediate},
          {"pc", event->Pc},
          {"handled", event->Handled},
          {"name", event->Name},
          {"detail", event->Detail},
      });
    }
    nlohmann::json threads = nlohmann::json::array();
    for (uint32_t threadId = 0; threadId < process.ThreadCount(); ++threadId) {
      const auto *state = process.ThreadState(threadId);
      nlohmann::json commandBufferWords = nlohmann::json::array();
      nlohmann::json registers = nlohmann::json::array();
      nlohmann::json stackWords = nlohmann::json::array();
      nlohmann::json threadArgumentWords = nlohmann::json::array();
      if (state != nullptr) {
        for (uint32_t value : state->r) {
          registers.push_back(value);
        }
        for (uint32_t offset = 0; offset < 0x40U; offset += 4U) {
          uint32_t value = 0;
          if (!process.Memory().Read32(state->r[13] + offset, &value)) {
            break;
          }
          stackWords.push_back(value);
        }
        const uint32_t commandBuffer = state->thread_pointer + 0x80U;
        for (uint32_t offset = 0; offset < 0x30U; offset += 4U) {
          uint32_t value = 0;
          if (!process.Memory().Read32(commandBuffer + offset, &value)) {
            break;
          }
          commandBufferWords.push_back(value);
        }
      }
      const uint32_t threadArgument =
          process.ThreadArgument(threadId).value_or(0U);
      for (uint32_t offset = 0; threadArgument != 0U && offset < 0x20U;
           offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(threadArgument + offset, &value)) {
          break;
        }
        threadArgumentWords.push_back(value);
      }
      threads.push_back({
          {"id", threadId},
          {"entry_address", process.ThreadEntryAddress(threadId).value_or(0U)},
          {"argument", process.ThreadArgument(threadId).value_or(0U)},
          {"priority", process.ThreadPriority(threadId).value_or(0U)},
          {"status", static_cast<uint32_t>(process.ThreadStatus(threadId))},
          {"pc", state != nullptr ? state->r[15] : 0U},
          {"registers", std::move(registers)},
          {"stack", std::move(stackWords)},
          {"command_buffer", std::move(commandBufferWords)},
          {"argument_words", std::move(threadArgumentWords)},
      });
    }
    nlohmann::json projectionCallsites = nlohmann::json::array();
    for (const auto &[key, stats] :
         widescreenProjection.Projection.Callsites) {
      projectionCallsites.push_back({
          {"function", key.first},
          {"return_address", key.second},
          {"calls", stats.Calls},
          {"first_left", stats.FirstLeft},
          {"first_right", stats.FirstRight},
      });
    }
    std::vector<std::pair<uint32_t, uint64_t>> sampledBlocks(
        widescreenProjection.BlockSamples.begin(),
        widescreenProjection.BlockSamples.end());
    std::sort(sampledBlocks.begin(), sampledBlocks.end(),
              [](const auto &left, const auto &right) {
                return left.second != right.second ? left.second > right.second
                                                   : left.first < right.first;
              });
    uint64_t totalBlockSampleCount = 0;
    for (const auto &sampledBlock : sampledBlocks) {
      totalBlockSampleCount += sampledBlock.second;
    }
    constexpr size_t kMaximumReportedA32Blocks = 4096U;
    if (sampledBlocks.size() > kMaximumReportedA32Blocks) {
      sampledBlocks.resize(kMaximumReportedA32Blocks);
    }
    nlohmann::json sampledBlockEntries = nlohmann::json::array();
    uint64_t blockSampleCount = 0;
    for (const auto &[pc, samples] : sampledBlocks) {
      blockSampleCount += samples;
      sampledBlockEntries.push_back({
          {"pc", pc},
          {"samples", samples},
          {"estimated_entries", samples * 64U},
      });
    }
    const auto readQueueState = [&](uint32_t address) {
      nlohmann::json words = nlohmann::json::array();
      std::array<uint32_t, 13> values{};
      size_t valueCount = 0;
      for (uint32_t offset = 0; offset < 0x34U; offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(address + offset, &value)) {
          break;
        }
        values[valueCount++] = value;
        words.push_back({{"offset", offset}, {"value", value}});
      }
      const auto valueAt = [&](size_t index) {
        return index < valueCount ? values[index] : 0U;
      };
      return nlohmann::json{
          {"address", address},
          {"storage", valueAt(0)},
          {"item_semaphore", valueAt(1)},
          {"free_semaphore", valueAt(3)},
          {"capacity", valueAt(8)},
          {"read_index", valueAt(9)},
          {"count", valueAt(10)},
          {"active_consumers", valueAt(11)},
          {"active_producers", valueAt(12)},
          {"words", std::move(words)},
      };
    };
    const auto readRendererCommandState = [&]() {
      nlohmann::json words = nlohmann::json::array();
      for (uint32_t offset = 0x168U; offset <= 0x188U; offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(kRendererCommandQueue + offset, &value)) {
          break;
        }
        words.push_back({{"offset", offset}, {"value", value}});
      }
      return words;
    };
    const auto sceneViewSnapshot =
        sceneViewProbe.Capture(process.Memory());
    const auto playerTimingSnapshot =
        playerTimingProbe.Capture(process.Memory());
    const auto serializeTopScreenPrimitives = [](const auto &primitives) {
      nlohmann::json serialized = nlohmann::json::array();
      for (const auto &primitive : primitives) {
        serialized.push_back(
            {{"role", static_cast<std::uint32_t>(primitive.role)},
             {"owner_address", primitive.owner_address},
             {"descriptor_address", primitive.descriptor_address},
             {"source_quad", primitive.source_quad},
             {"texture", primitive.texture.semantic_name},
             {"destination",
              {primitive.destination.x, primitive.destination.y,
               primitive.destination.width, primitive.destination.height}},
             {"uv",
              {primitive.uv.x, primitive.uv.y, primitive.uv.width,
               primitive.uv.height}},
             {"color",
              {primitive.color.red, primitive.color.green,
               primitive.color.blue, primitive.color.alpha}},
             {"layer", primitive.layer},
             {"visible", primitive.visible}});
      }
      return serialized;
    };
    nlohmann::json topScreenPresentations = nlohmann::json::object();
    for (std::size_t subsystemIndex = 0;
         subsystemIndex < oot3d::ui::kUiSubsystemCount; ++subsystemIndex) {
      const auto subsystem =
          static_cast<oot3d::ui::UiSubsystem>(subsystemIndex);
      topScreenPresentations[oot3d::ui::UiSubsystemName(subsystem)] =
          serializeTopScreenPrimitives(
              lastTopScreenPrimitivesBySubsystem[subsystemIndex]);
    }
    const auto topScreenGameplayPrimitiveAudit =
        serializeTopScreenPrimitives(lastTopScreenPrimitivesBySubsystem[
            static_cast<std::size_t>(oot3d::ui::UiSubsystem::GameplayHud)]);
    const auto graphicsSettings =
        Fast::Oot3d::GraphicsSettingsRuntime::Instance().Snapshot();
    const auto &directionalShadows =
        graphicsSettings.Effects.DirectionalShadows;
    const auto &realtimePacingStats = realtimePacer.Stats();
    const double benchmarkHostSeconds =
        benchmarkMeasuredFrames == 0U
            ? 0.0
            : std::chrono::duration<double>(benchmarkMeasurementEnd -
                                            benchmarkMeasurementStart)
                  .count();
    WriteJsonFile(
        launch.Host.OutputPath,
        {
            {"schema", "oot3d_native_a32_vulkan_runtime_v1"},
            {"structural_scenario", scenarioBootstrap.has_value()
                                        ? scenarioBootstrap->ToJson()
                                        : nlohmann::json(nullptr)},
            {"frames", frameCount},
            {"run_frames", runFrameCount},
            {"guest_refresh_frames", frameCount},
            {"presentation_frames", presentationFrameCount},
            {"pica_composition",
             [&]() {
               const auto &stats = picaCompositionTracker.Stats();
               return nlohmann::json{
                   {"cmb_opaque_pass_entries", stats.CmbOpaquePassEntries},
                   {"cmb_transparent_pass_entries",
                    stats.CmbTransparentPassEntries},
                   {"cmb_pass_exits", stats.CmbPassExits},
                   {"invalid_cmb_pass_entries", stats.InvalidCmbPassEntries},
                   {"invalid_cmb_return_addresses",
                    stats.InvalidCmbReturnAddresses},
                   {"ui_scope_entries", stats.UiScopeEntries},
                   {"ui_scope_exits", stats.UiScopeExits},
                   {"ui_command_spans", stats.UiCommandSpans},
                   {"atmosphere_scope_entries", stats.AtmosphereScopeEntries},
                   {"atmosphere_scope_exits", stats.AtmosphereScopeExits},
                   {"atmosphere_packet_spans", stats.AtmospherePacketSpans},
                   {"direct_atmosphere_spans", stats.DirectAtmosphereSpans},
                   {"direct_atmosphere_bytes", stats.DirectAtmosphereBytes},
                   {"nested_atmosphere_scope_entries",
                    stats.NestedAtmosphereScopeEntries},
                   {"invalid_atmosphere_command_ranges",
                    stats.InvalidAtmosphereCommandRanges},
                   {"recovered_atmosphere_packet_spans",
                    stats.RecoveredAtmospherePacketSpans},
                   {"invalid_atmosphere_return_addresses",
                    stats.InvalidAtmosphereReturnAddresses},
                   {"ignored_unrelated_command_list_executions",
                    stats.IgnoredUnrelatedCommandListExecutions},
                   {"atmosphere_selector_read_failures",
                    stats.AtmosphereSelectorReadFailures},
                   {"primitive_packet_entries",
                    stats.PrimitivePacketEntries},
                   {"primitive_packet_exits", stats.PrimitivePacketExits},
                   {"primitive_packet_spans", stats.PrimitivePacketSpans},
                   {"primitive_packet_bytes", stats.PrimitivePacketBytes},
                   {"nested_primitive_packet_entries",
                    stats.NestedPrimitivePacketEntries},
                   {"invalid_primitive_packet_return_addresses",
                    stats.InvalidPrimitivePacketReturnAddresses},
                   {"invalid_primitive_packet_ranges",
                    stats.InvalidPrimitivePacketRanges},
                   {"unmatched_primitive_packet_exits",
                    stats.UnmatchedPrimitivePacketExits},
                   {"mesh_packet_entries", stats.MeshPacketEntries},
                   {"recorded_packet_spans", stats.RecordedPacketSpans},
                   {"coalesced_packet_spans", stats.CoalescedPacketSpans},
                   {"unattributed_packet_spans",
                    stats.UnattributedPacketSpans},
                   {"invalid_packet_spans", stats.InvalidPacketSpans},
                   {"overlapping_packet_spans",
                    stats.OverlappingPacketSpans},
                   {"consumed_packet_spans", stats.ConsumedPacketSpans},
                   {"partially_overlapping_submissions",
                    stats.PartiallyOverlappingSubmissions},
                   {"evicted_packet_spans", stats.EvictedPacketSpans},
                   {"read_failures", stats.ReadFailures},
               };
             }()},
            {"topscreen_gameplay_primitives",
             topScreenGameplayPrimitiveAudit},
            {"topscreen_presentations", std::move(topScreenPresentations)},
            {"vblanks", vblankCount},
            {"savestates",
             {{"quick_path", quickStatePath.string()},
              {"initial_load_path", launch.LoadStatePath.string()},
              {"automated_save_path", launch.SaveStatePath.string()},
              {"automated_save_frame",
               launch.SaveStateFrameAvailable
                   ? nlohmann::json(launch.SaveStateFrame)
                   : nlohmann::json(nullptr)},
              {"automated_save_actual_run_frame",
               automatedStateSavedAtRunFrame.has_value()
                   ? nlohmann::json(*automatedStateSavedAtRunFrame)
                   : nlohmann::json(nullptr)},
              {"automated_save_actual_guest_frame",
               automatedStateSavedAtGuestFrame.has_value()
                   ? nlohmann::json(*automatedStateSavedAtGuestFrame)
                   : nlohmann::json(nullptr)},
              {"save_count", savestateSaveCount},
              {"load_count", savestateLoadCount},
              {"save_bytes", savestateSaveBytes},
              {"load_bytes", savestateLoadBytes},
              {"save_seconds", savestateSaveSeconds},
              {"load_seconds", savestateLoadSeconds},
              {"capture_seconds", savestateCaptureSeconds},
              {"encode_seconds", savestateEncodeSeconds},
              {"write_seconds", savestateWriteSeconds},
              {"read_seconds", savestateReadSeconds},
              {"decode_seconds", savestateDecodeSeconds},
              {"restore_seconds", savestateRestoreSeconds},
              {"last_semantic_fingerprint",
               std::to_string(lastSavestateSemanticFingerprint)},
              {"legacy_gameplay_clock_restores",
               legacySavestateClockRestoreCount},
              {"legacy_presentation_scheduler_restores",
               legacySavestatePresentationSchedulerRestoreCount},
              {"legacy_frame_rate_policy_restores",
               legacySavestateFrameRatePolicyRestoreCount},
              {"legacy_topscreen_temporal_restores",
               legacySavestateTopScreenTemporalRestoreCount},
              {"legacy_pica_visual_replay_restores",
               legacySavestatePicaVisualReplayRestoreCount},
              {"legacy_pica_texture_cache_restores",
               legacySavestatePicaTextureCacheRestoreCount},
              {"legacy_pica_color_target_restores",
               legacySavestatePicaColorTargetRestoreCount},
              {"legacy_pica_presentation_restores",
               legacySavestatePicaPresentationRestoreCount},
              {"pica_textures_captured",
               savestatePicaTexturesCaptured},
              {"pica_texture_bytes_captured",
               savestatePicaTextureBytesCaptured},
              {"pica_textures_restored",
               savestatePicaTexturesRestored},
              {"pica_texture_bytes_restored",
               savestatePicaTextureBytesRestored},
              {"pica_visual_replay_bytes_captured",
               savestatePicaVisualReplayBytesCaptured},
              {"pica_visual_replay_bytes_restored",
               savestatePicaVisualReplayBytesRestored},
              {"pica_color_targets_captured",
               savestatePicaColorTargetsCaptured},
              {"pica_color_target_bytes_captured",
               savestatePicaColorTargetBytesCaptured},
              {"pica_color_targets_restored",
               savestatePicaColorTargetsRestored},
              {"pica_color_target_bytes_restored",
               savestatePicaColorTargetBytesRestored},
              {"pica_display_images_captured",
               savestatePicaDisplayImagesCaptured},
              {"pica_display_image_bytes_captured",
               savestatePicaDisplayImageBytesCaptured},
              {"pica_display_images_restored",
               savestatePicaDisplayImagesRestored},
              {"pica_display_image_bytes_restored",
               savestatePicaDisplayImageBytesRestored}}},
             {"realtime_pacing",
             {{"enabled", realtimePacer.Enabled()},
              {"multimedia_scheduling_active", realtimePacer.MultimediaSchedulingActive()},
              {"high_resolution_wait_available",
               realtimePacer.HighResolutionWaitAvailable()},
              {"basis", "realtime_when_no_frame_limit"},
              {"waits", realtimePacingStats.Waits},
              {"deadline_misses",
               realtimePacingStats.DeadlineMisses},
              {"carried_deadline_debt",
               realtimePacingStats.CarriedDeadlineDebt},
              {"deadline_resyncs",
               realtimePacingStats.DeadlineResyncs},
              {"sleep_seconds", realtimePacingStats.SleepSeconds},
              {"spin_seconds", realtimePacingStats.SpinSeconds},
              {"host_loop_seconds", realtimePacer.ElapsedSeconds()},
              {"presentation_intervals",
               realtimePacingStats.PresentationIntervals},
              {"mean_interval_seconds",
               realtimePacer.MeanIntervalSeconds()},
              {"minimum_interval_seconds",
               realtimePacingStats.MinimumIntervalSeconds},
              {"maximum_interval_seconds",
               realtimePacingStats.MaximumIntervalSeconds},
              {"rms_interval_error_seconds",
               realtimePacer.RmsIntervalErrorSeconds()},
              {"maximum_interval_error_seconds",
               realtimePacingStats.MaximumIntervalErrorSeconds},
              {"maximum_lateness_seconds",
               realtimePacingStats.MaximumLatenessSeconds},
              {"target_refresh_hz",
               realtimePacer.TargetRateHz() == 0U
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(realtimePacer.TargetRateHz())}}},
            {"guest_clock",
             {{"system_ticks", hostServices.SystemTicks()},
              {"next_vblank_tick", nextVblankTick},
              {"late_vblank_deadlines", lateVblankDeadlineCount},
              {"maximum_vblank_deadline_overshoot_ticks",
               maximumVblankDeadlineOvershootTicks},
              {"maximum_vblank_deadline_overshoot_seconds",
               static_cast<double>(maximumVblankDeadlineOvershootTicks) /
                   static_cast<double>(kCtrArm11TicksPerSecond)}}},
            {"benchmark_window",
             {{"warmup_frames_requested",
               hostArgs.BenchmarkWarmupFrames},
              {"warmup_frames_completed",
               std::min<uint64_t>(runFrameCount,
                                  hostArgs.BenchmarkWarmupFrames)},
              {"measured_frames", benchmarkMeasuredFrames},
              {"thread_cpu_ledger", [&]() -> nlohmann::json {
                nlohmann::json phases = nlohmann::json::object();
                double windowCpu = 0.0;
                for (size_t i = 0; i < benchmarkCpuPhases.size(); ++i) {
                  const auto& p = benchmarkCpuPhases[i];
                  windowCpu += p.User + p.Kernel;
                  phases[CpuProbe::Names[i]] = {
                      {"wall_seconds", p.Wall}, {"thread_cycles", p.Cycles},
                      {"intervals", p.Intervals}};
                }
                return {{"requested", cpuProbeRequested},
                    {"available_in_build", CpuProbe::BuildEnabled},
                    {"clock", "GetThreadTimes_and_QueryThreadCycleTime"},
                    {"read_failures", CpuProbe::Thread.ReadFailures},
                    {"window_thread_cpu_seconds", windowCpu},
                    {"scope", "exclusive_main_thread_same_post_warmup_window"},
                    {"warning", "quantized_CPU_valid_only_for_whole_window; off_CPU_includes_preemption_not_just_waits; phase_cycles_are_not_seconds"},
                    {"phases", std::move(phases)}};
              }()},
              {"pre_backend_cpu", [&]() -> nlohmann::json {
                const auto& p=benchmarkPhaseSeconds;
                const Oot3dNativeGame::PreBackendBudget b{
                  p[0],p[7],benchmarkNestedGuestSeconds,p[8],p[2],p[3],p[5],p[6]};
                return {{"scope","guest_including_gsp_pica_plus_plan_capture_host_audio_before_nri_execute"},
                  {"samples",preBackendTimes.Count()}, {"invalid_intervals",invalidPreBackendIntervals},
                  {"guest_seconds",b.Guest}, {"plan_seconds",b.Plan},
                  {"guest_clock_included_seconds",benchmarkGuestClockSeconds},
                  {"queue_capture_seconds",b.QueueAndCapture()},
                  {"nested_guest_subtracted_seconds",b.GuestInsideSubmission},
                  {"host_seconds",b.Host}, {"input_seconds",b.Input},
                  {"dsp_seconds",b.Dsp}, {"audio_output_seconds",b.AudioOutput},
                  {"total_seconds",b.Total()},
                  {"mean_ms",preBackendTimes.MeanMs()},
                  {"host_envelope_mean_ms",preBackendEnvelopeTimes.MeanMs()},
                  {"host_envelope_p95_upper_ms",preBackendEnvelopeTimes.Count()?nlohmann::json(*preBackendEnvelopeTimes.QuantileUpperMs(.95)):nlohmann::json(nullptr)},
                  {"unattributed_host_ui_seconds",benchmarkUnattributedSeconds},
                  {"p95_upper_ms",preBackendTimes.Count()?nlohmann::json(*preBackendTimes.QuantileUpperMs(.95)):nlohmann::json(nullptr)},
                  {"maximum_ms",preBackendTimes.MaximumMs()}};
              }()},
              {"frame_times", [&]() -> nlohmann::json {
                 if (!benchmarkFrameTimes || !benchmarkFrameTimes->Count()) return nullptr;
                 const auto& times = *benchmarkFrameTimes;
                 return {{"samples", times.Count()}, {"invalid_samples", times.Invalid()},
                         {"quantile_resolution_ms", times.ResolutionMs},
                         {"overflow_samples", times.Overflow()},
                         {"p50_upper_ms", *times.QuantileUpperMs(0.50)},
                         {"p95_upper_ms", *times.QuantileUpperMs(0.95)},
                         {"p99_upper_ms", *times.QuantileUpperMs(0.99)},
                         {"maximum_ms", times.MaximumMs()}, {"mean_ms", times.MeanMs()},
                         {"over_60hz_budget", times.Over60Hz()},
                         {"over_30hz_budget", times.Over30Hz()}};
               }()},
              {"host_seconds",
               benchmarkMeasuredFrames == 0U
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(benchmarkHostSeconds)},
              {"frames_per_second",
               benchmarkMeasuredFrames == 0U || benchmarkHostSeconds <= 0.0
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(
                         static_cast<double>(benchmarkMeasuredFrames) /
                         benchmarkHostSeconds)},
              {"fixed_delta_seconds",
               hostArgs.FixedDeltaSeconds > 0.0
                   ? nlohmann::json(hostArgs.FixedDeltaSeconds)
                   : nlohmann::json(nullptr)},
              {"throughput_mode", hostArgs.ThroughputBenchmark},
              {"vsync",
               hostArgs.ThroughputBenchmark ? false
                                            : graphicsSettings.VSync},
              {"sdl_frame_limiter_enabled",
               !hostArgs.ThroughputBenchmark &&
                   window.GetTargetFps() > 0},
              {"pacing_enabled", realtimePacer.Enabled()}}},
            {"frame_rate",
             [&]() {
                const auto &contract = frameRatePolicy.Contract();
                const auto &stats = frameRatePolicy.Stats();
                const auto &time =
                    frameRatePolicy.GameplayClock().Context();
                const auto &presentationStats = presentationScheduler.Stats();
                const auto &cadenceStats = visualSampleCadence.Stats();
                const auto &playerTemporalStats =
                    playerTemporalBridge.Stats();
                return nlohmann::json{
                    {"mode",
                     Oot3dNativeGame::GameplayTimingModeName(contract.Mode)},
                    {"implementation_status",
                     contract.Mode ==
                             Oot3dNativeGame::GameplayTimingMode::Enhanced60
                         ? "experimental_a32_compatibility"
                         : "baseline"},
                    {"original_simulation_hz",
                     Oot3dNativeGame::kOot3dOriginalSimulationRateHz},
                   {"simulation_hz", contract.SimulationRateHz},
                   {"presentation_hz",
                    contract.PresentationRateUnlimited
                        ? nlohmann::json(nullptr)
                        : nlohmann::json(contract.PresentationRateHz)},
                   {"presentation_unlimited",
                    contract.PresentationRateUnlimited},
                   {"presentation_frames",
                    presentationStats.PresentationFrames},
                   {"guest_refreshes_scheduled",
                    presentationStats.GuestRefreshes},
                   {"deferred_guest_refresh_observations",
                    presentationStats.DeferredGuestRefreshObservations},
                   {"guest_refreshes_dropped",
                    presentationStats.DroppedGuestRefreshes},
                   {"batched_presentation_frames",
                    presentationStats.BatchedPresentationFrames},
                   {"maximum_guest_refresh_batch",
                    presentationStats.MaximumGuestRefreshBatch},
                   {"guest_refresh_batch_limit",
                    Oot3dNativeGame::kMaximumGuestRefreshesPerPresentation},
                   {"maximum_deferred_guest_refreshes",
                    presentationStats.MaximumDeferredGuestRefreshes},
                   {"deferred_guest_refresh_limit",
                    Oot3dNativeGame::kMaximumDeferredGuestRefreshes},
                    {"step_seconds", contract.StepSeconds},
                    {"native_update_rate", contract.NativeUpdateRate},
                    {"native_step_scale", contract.NativeStepScale},
                    {"logical_frame_delta", contract.LogicalFrameDelta},
                    {"simulation_tick", time.SimulationTick},
                    {"previous_logical_frame",
                     time.PreviousLogicalFrame},
                    {"current_logical_frame", time.CurrentLogicalFrame},
                    {"logical_frame_index", time.LogicalFrameIndex},
                    {"crossed_logical_frame", time.CrossedLogicalFrame},
                    {"a32_update_rate", contract.A32UpdateRate},
                    {"a32_update_rate_exact", contract.A32UpdateRateExact},
                    {"visual_interpolation_allowed",
                     contract.VisualInterpolationAllowed},
                    {"visual_interpolation_active",
                     visualInterpolationEnabled},
                    {"visual_interpolation_mode",
                     Fast::Oot3d::NativeVisualInterpolationModeName(
                         frameCompositionPolicy.Interpolation)},
                    {"visual_sample_multiplier",
                     frameCompositionPolicy.FixedSampleMultiplier},
                    {"visual_sample_cadence",
                     {{"fixed_multiplier",
                       visualSampleCadence.FixedSampleMultiplier()},
                      {"new_source_frames",
                       cadenceStats.NewSourceFrames},
                      {"repeated_samples",
                       cadenceStats.RepeatedSamples},
                      {"clamped_repeated_samples",
                       cadenceStats.ClampedRepeatedSamples},
                      {"last_sample_ordinal",
                       cadenceStats.SampleOrdinal}}},
                    {"visual_presentation_rate_hz",
                     frameCompositionPolicy.TargetPresentationRateHz != 0U
                         ? nlohmann::json(
                               frameCompositionPolicy.TargetPresentationRateHz)
                         : nlohmann::json(nullptr)},
                    {"visual_continuity_epoch", visualContinuityEpoch},
                    {"source_owner_graphs_complete",
                     sourceGameplayOwners.EnabledOwnerCount()},
                    {"compatibility_island",
                     contract.Mode ==
                             Oot3dNativeGame::GameplayTimingMode::Enhanced60
                         ? nlohmann::json("a32_gameplay_owner_graph")
                         : nlohmann::json(nullptr)},
                    {"player_temporal_bridge",
                     {{"enabled",
                       widescreenProjection.PlayerTemporalBridge != nullptr},
                      {"collision_entries",
                       playerTemporalStats.CollisionEntries},
                      {"collision_returns",
                       playerTemporalStats.CollisionReturns},
                      {"logical_frame_returns",
                       playerTemporalStats.LogicalFrameReturns},
                      {"intermediate_returns",
                       playerTemporalStats.IntermediateReturns},
                      {"reconciliations",
                       playerTemporalStats.FloorTimerReconciliations},
                      {"immediate_floor_type_changes",
                       playerTemporalStats.ImmediateFloorTypeChanges},
                      {"native_mutation_mismatches",
                       playerTemporalStats.NativeMutationMismatches},
                      {"ledge_increment_paths",
                       playerTemporalStats.LedgeIncrementPaths},
                      {"ledge_wire_overflow_reconciliations",
                       playerTemporalStats
                           .LedgeWireOverflowReconciliations},
                      {"immediate_ledge_type_changes",
                       playerTemporalStats.ImmediateLedgeTypeChanges},
                      {"ledge_timer_resets",
                       playerTemporalStats.LedgeTimerResets},
                      {"ledge_mutation_mismatches",
                       playerTemporalStats.LedgeMutationMismatches},
                      {"dynamic_wall_gate_evaluations",
                       playerTemporalStats.DynamicWallGateEvaluations},
                      {"high_ledge_gate_evaluations",
                       playerTemporalStats.HighLedgeGateEvaluations},
                      {"low_ledge_gate_evaluations",
                       playerTemporalStats.LowLedgeGateEvaluations},
                      {"ledge_gates_reached",
                       playerTemporalStats.LedgeGatesReached},
                      {"unmatched_returns",
                       playerTemporalStats.UnmatchedReturns},
                      {"unmatched_ledge_increment_paths",
                       playerTemporalStats.UnmatchedLedgeIncrementPaths},
                      {"read_failures", playerTemporalStats.ReadFailures},
                      {"write_failures", playerTemporalStats.WriteFailures}}},
                    {"game_state_updates_observed",
                     stats.GameStateUpdatesObserved},
                   {"update_rate_writes", stats.UpdateRateWrites},
                   {"frame_pacing_commits_observed",
                    stats.FramePacingCommitsObserved},
                   {"frame_pacing_frames_observed",
                    stats.FramePacingFramesObserved},
                   {"frame_pacing_writes", stats.FramePacingWrites},
                   {"frame_pacing_decisions_observed",
                    stats.FramePacingDecisionsObserved},
                   {"frame_pacing_deadline_writes",
                    stats.FramePacingDeadlineWrites},
                   {"read_failures", stats.ReadFailures},
                   {"write_failures", stats.WriteFailures},
                   {"last_game_state_address", stats.LastGameStateAddress},
                   {"last_time_state_address", stats.LastTimeStateAddress},
                   {"last_observed_update_rate",
                    stats.HasObservedUpdateRate
                        ? nlohmann::json(stats.LastObservedUpdateRate)
                        : nlohmann::json(nullptr)},
                   {"last_frame_pacing_state_address",
                    stats.LastFramePacingStateAddress},
                   {"last_observed_maximum_interval",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedMaximumInterval)
                        : nlohmann::json(nullptr)},
                   {"last_observed_interval",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedInterval)
                        : nlohmann::json(nullptr)},
                   {"last_observed_deadline",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedDeadline)
                        : nlohmann::json(nullptr)},
                   {"last_observed_vsync_counter",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedVsyncCounter)
                        : nlohmann::json(nullptr)},
               };
             }()},
            {"temporal_event_ledger",
             [&]() {
               const auto &stats = temporalEventLedger.Stats();
               nlohmann::json events = nlohmann::json::array();
               for (const auto &event : temporalEventLedger.Events()) {
                 events.push_back({
                     {"sequence", event.Sequence},
                     {"simulation_tick", event.SimulationTick},
                     {"logical_frame", event.LogicalFrame},
                     {"logical_frame_index", event.LogicalFrameIndex},
                     {"kind",
                      std::string(
                          Oot3dNativeGame::NativeTemporalEventKindName(
                              event.Kind))},
                     {"subject_address", event.SubjectAddress},
                     {"previous_value", event.PreviousValue},
                     {"current_value", event.CurrentValue},
                 });
               }
               return nlohmann::json{
                   {"enabled", launch.ExtendedDiagnostics},
                   {"source", "a32_player_semantic_transition_probe"},
                   {"player_observations", stats.PlayerObservations},
                   {"player_identity_changes",
                    stats.PlayerIdentityChanges},
                   {"events_observed", stats.EventsObserved},
                   {"events_recorded", stats.EventsRecorded},
                   {"events_dropped", stats.EventsDropped},
                   {"baseline_fingerprint",
                    stats.BaselineFingerprint},
                   {"event_fingerprint", stats.EventFingerprint},
                   {"events", std::move(events)},
               };
             }()},
            {"player_timing",
             [&]() {
               const auto &stats = playerTimingProbe.Stats();
               nlohmann::json state = nullptr;
               if (playerTimingSnapshot.has_value()) {
                 const auto &player = *playerTimingSnapshot;
                 state = {
                     {"player_address", player.PlayerAddress},
                     {"play_state_address", player.PlayStateAddress},
                     {"time_state_address", player.TimeStateAddress},
                     {"world_position",
                      {player.WorldX, player.WorldY, player.WorldZ}},
                     {"velocity",
                      {player.VelocityX, player.VelocityY, player.VelocityZ}},
                     {"actor_speed", player.ActorSpeed},
                     {"player_speed", player.PlayerSpeed},
                     {"floor_height", player.FloorHeight},
                     {"underwater_timer", player.UnderwaterTimer},
                     {"melee_weapon_action_timer",
                      player.MeleeWeaponActionTimer},
                     {"melee_weapon_combo_state",
                      player.MeleeWeaponComboState},
                     {"item_action_state_or_burn_timer",
                      player.ItemActionStateOrBurnTimer},
                     {"ledge_climb_type", player.LedgeClimbType},
                     {"ledge_climb_delay_timer",
                      player.LedgeClimbDelayTimer},
                     {"textbox_button_cooldown_timer",
                      player.TextboxButtonCooldownTimer},
                     {"damage_flicker_animation_counter",
                      player.DamageFlickerAnimationCounter},
                     {"damage_run_timer", player.DamageRunTimer},
                     {"item_action_cooldown_timer",
                      player.ItemActionCooldownTimer},
                     {"collision_sfx_cooldown_timer",
                      player.CollisionSfxCooldownTimer},
                     {"invincibility_timer", player.InvincibilityTimer},
                     {"floor_type_timer", player.FloorTypeTimer},
                     {"previous_floor_type", player.PreviousFloorType},
                     {"respawn_damage_state",
                      player.RespawnDamageState},
                     {"random_turn_state", player.RandomTurnState},
                     {"random_turn_timer", player.RandomTurnTimer},
                     {"attention_persistence_counter",
                      player.AttentionPersistenceCounter},
                     {"fairy_revive_grace_timer",
                      player.FairyReviveGraceTimer},
                     {"shape_yaw", player.ShapeYaw},
                     {"background_check_flags", player.BackgroundCheckFlags},
                     {"action_function", player.ActionFunction},
                     {"state_flags_1", player.StateFlags1},
                     {"state_flags_2", player.StateFlags2},
                     {"animation_resource", player.AnimationResource},
                     {"animation_frame", player.AnimationFrame},
                     {"animation_play_speed", player.AnimationPlaySpeed},
                     {"animation_start_frame", player.AnimationStartFrame},
                     {"animation_end_frame", player.AnimationEndFrame},
                     {"animation_mode", player.AnimationMode},
                     {"time_state_update_rate", player.TimeStateUpdateRate},
                 };
               }
               return nlohmann::json{
                   {"source", "code_bin_player_update_layout"},
                   {"update_entries_observed", stats.UpdateEntriesObserved},
                   {"snapshots_captured", stats.SnapshotsCaptured},
                   {"read_failures", stats.ReadFailures},
                   {"last_error", playerTimingProbe.LastError()},
                   {"state", std::move(state)},
                   {"events_truncated",
                    widescreenProjection.PlayerTimingEventsTruncated},
                   {"events",
                    std::move(widescreenProjection.PlayerTimingEvents)},
               };
             }()},
            {"native_scene_view",
             [&]() {
               const auto &stats = sceneViewProbe.Stats();
               nlohmann::json state = nullptr;
               if (sceneViewSnapshot.has_value()) {
                 const auto &view = *sceneViewSnapshot;
                 state = {
                     {"serial", view.Serial},
                     {"play_state_address", view.PlayStateAddress},
                     {"guest_function", view.GuestFunction},
                     {"guest_return_address", view.GuestReturnAddress},
                     {"frustum",
                      {view.Left, view.Right, view.Bottom, view.Top,
                       view.NearPlane, view.FarPlane}},
                     {"eye", view.Eye},
                     {"at", view.At},
                     {"camera_available", view.CameraAvailable},
                 };
               }
               return nlohmann::json{
                   {"source",
                    "code_bin_mtx4x4_frustum_and_playstate_view"},
                   {"perspective_observations",
                    stats.PerspectiveObservations},
                   {"invalid_perspective_observations",
                    stats.InvalidPerspectiveObservations},
                   {"snapshots_captured", stats.SnapshotsCaptured},
                   {"read_failures", stats.ReadFailures},
                   {"published", publishedSceneViewCount},
                   {"last_error", sceneViewProbe.LastError()},
                   {"state", std::move(state)},
               };
             }()},
            {"hid_input",
             [&]() {
               const auto hid = hostServices.HidRuntimeProfile();
               const auto controls =
                   controlConfigRuntime->Snapshot().Config;
               return nlohmann::json{
                   {"controls_config",
                    controlConfigRuntime->Path().generic_string()},
                   {"controls_profile",
                    Oot3dNativeGame::NativeControlProfileName(
                        controls.Profile)},
                   {"native_aim_source",
                    Oot3dNativeGame::NativeMotionSourceName(
                        controls.NativeAimSource)},
                   {"free_camera_source",
                    Oot3dNativeGame::NativeMotionSourceName(
                        controls.FreeCameraSource)},
                   {"gameplay_mouse_owned",
                    nativeControlPollingState.GameplayMouseOwned},
                   {"mouse_polling", {
                       {"eligible", nativeControlPollingState.MouseEligiblePolls},
                       {"released", nativeControlPollingState.MouseReleasedPolls},
                       {"host_ui", nativeControlPollingState.MouseHostUiPolls},
                       {"native_ui", nativeControlPollingState.MouseNativeUiPolls},
                       {"capture_transitions", nativeControlPollingState.MouseCaptureTransitions},
                       {"movement", nativeControlPollingState.MouseMovementPolls}}},
                   {"free_camera_input",
                    {{"pending_x",
                      nativeCandidateDispatch.TopScreenCameraInput.Peek().X},
                     {"pending_y",
                      nativeCandidateDispatch.TopScreenCameraInput.Peek().Y},
                     {"relative_samples_observed",
                      nativeCandidateDispatch.TopScreenCameraInput
                          .RelativeSamplesObserved()},
                     {"relative_samples_consumed",
                      nativeCandidateDispatch.TopScreenCameraInput
                          .RelativeSamplesConsumed()}}},
                   {"timeline_enabled", inputTimeline.Enabled()},
                   {"timeline_path",
                    inputTimeline.SourcePath().generic_string()},
                   {"timeline_segments", inputTimeline.SegmentCount()},
                   {"timeline_frame_origin",
                    Oot3dNativeGame::NativeA32InputTimelineFrameOriginName(
                        inputTimeline.FrameOrigin())},
                   {"sampled_host_frames", inputDiagnostics.SampledFrameCount},
                   {"scripted_host_frames",
                    inputDiagnostics.ScriptedFrameCount},
                   {"active_segment_host_frames",
                    inputDiagnostics.ActiveSegmentFrameCount},
                   {"button_host_frames", inputDiagnostics.ButtonFrameCount},
                   {"circle_pad_host_frames",
                    inputDiagnostics.CirclePadFrameCount},
                   {"touch_host_frames", inputDiagnostics.TouchFrameCount},
                   {"start_edges_observed",
                    nativeCandidateDispatch.StartButtonLatch.ObservedPresses},
                   {"start_short_presses_recovered",
                    nativeCandidateDispatch.StartButtonLatch
                        .RecoveredShortPresses},
                   {"start_edge_pending",
                    nativeCandidateDispatch.StartButtonLatch.PendingPressed},
                   {"shared_memory_mapped", hid.SharedMemoryMapped},
                   {"samples_written", hid.SamplesWritten},
                   {"accelerometer_samples_written",
                    hid.AccelerometerSamplesWritten},
                   {"gyroscope_samples_written",
                    hid.GyroscopeSamplesWritten},
                   {"event_batches_signaled", hid.EventBatchesSignaled},
                   {"last_sample_tick", hid.LastSampleTick},
                   {"last_buttons", hid.LastButtons},
                   {"last_additions", hid.LastAdditions},
                   {"last_removals", hid.LastRemovals},
                   {"last_circle_pad_x", hid.LastCirclePadX},
                   {"last_circle_pad_y", hid.LastCirclePadY},
                   {"next_pad_index", hid.NextPadIndex},
                   {"last_accelerometer", hid.LastAccelerometer},
                   {"last_gyroscope", hid.LastGyroscope},
                   {"next_accelerometer_index",
                    hid.NextAccelerometerIndex},
                   {"next_gyroscope_index",
                    hid.NextGyroscopeIndex},
                   {"native_consumer",
                    {{"samples", inputConsumerDiagnostics.Samples},
                     {"read_failures", inputConsumerDiagnostics.ReadFailures},
                     {"held_frames", inputConsumerDiagnostics.HeldFrames},
                     {"pressed_frames", inputConsumerDiagnostics.PressedFrames},
                     {"released_frames",
                      inputConsumerDiagnostics.ReleasedFrames},
                     {"last_held", inputConsumerDiagnostics.LastHeld},
                     {"last_pressed", inputConsumerDiagnostics.LastPressed},
                     {"last_released", inputConsumerDiagnostics.LastReleased},
                     {"last_raw_current",
                      inputConsumerDiagnostics.LastRawCurrent},
                     {"last_raw_previous",
                      inputConsumerDiagnostics.LastRawPrevious},
                     {"transitions",
                      std::move(inputConsumerDiagnostics.Transitions)}}},
               };
             }()},
            {"dsp_audio",
             {{"enabled", !launch.DisableAudio},
              {"host_output_enabled", !launch.DisableAudio},
              {"guest_dsp_processing_enabled", true},
              {"frames_mixed", dspFramesMixed},
              {"frames_processed", dspFramesMixed},
              {"frames_suppressed", dspFramesSuppressed},
              {"stereo_samples_mixed", dspSamplesMixed},
              {"nonzero_channel_samples", dspNonzeroSamples},
              {"peak_magnitude", dspPeakMagnitude},
              {"pcm_fnv1a64", dspPcmFnv1a64},
              {"pcm_continuity", pcmContinuityDiagnostics.ToJson()},
              {"pcm_dump",
               {{"enabled", !launch.AudioPcmDumpPath.empty()},
                {"path", launch.AudioPcmDumpPath.string()},
                {"channel_samples", pcmDumpSamples.size()}}},
              {"output_initialized",
               audioPlayer != nullptr && audioPlayer->IsInitialized()},
              {"output_sample_rate",
               audioPlayer != nullptr ? audioPlayer->GetSampleRate() : 0},
              {"output_frame_samples",
               audioPlayer != nullptr ? audioPlayer->GetSampleLength() : 0},
              {"output_queue",
               {{"observed", audioOutputDiagnostics.HasBufferObservation},
                {"primed", audioOutputDiagnostics.Primed},
                {"submit_calls", audioOutputDiagnostics.SubmitCalls},
                {"requested_frames", audioOutputDiagnostics.RequestedFrames},
                {"maximum_submit_gap_seconds",
                 audioOutputDiagnostics.MaximumSubmitGapSeconds},
                {"submit_gap_events",
                 std::move(audioOutputDiagnostics.SubmitGapEvents)},
                {"short_growth_submissions",
                 audioOutputDiagnostics.ShortGrowthSubmissions},
                {"estimated_shortfall_frames",
                 audioOutputDiagnostics.EstimatedShortfallFrames},
                {"starvation_observations",
                 audioOutputDiagnostics.StarvationObservations},
                {"starvation_events",
                 std::move(audioOutputDiagnostics.StarvationEvents)},
                {"minimum_buffered_before",
                 audioOutputDiagnostics.MinimumBufferedBefore},
                {"maximum_buffered_before",
                 audioOutputDiagnostics.MaximumBufferedBefore},
                {"minimum_buffered_after",
                 audioOutputDiagnostics.MinimumBufferedAfter},
                {"maximum_buffered_after",
                 audioOutputDiagnostics.MaximumBufferedAfter},
                {"desired_buffered", audioPlayer != nullptr
                                         ? audioPlayer->GetDesiredBuffered()
                                         : 0}}}}},
            {"draws_submitted", drawCount},
            {"refreshes_with_native_draws", refreshesWithNativeDraws},
            {"display_transfers_submitted", displayTransferCount},
            {"gpu_completions_queued", 0},
            {"pica_completions_recorded", recordedCompletionCount},
            {"p3d_completions_delivered", completedP3dCount},
            {"ppf_completions_delivered", completedPpfCount},
            {"pending_gpu_completions", 0},
            {"pica_drain_passes", picaDrainPassCount},
            {"maximum_pica_drain_passes_per_refresh",
             maximumPicaDrainPassesPerRefresh},
            {"pending_draws", submissionQueue.PendingDraws().size()},
            {"pending_completion_markers",
             submissionQueue.PendingCompletions().size()},
            {"pending_display_transfers",
             submissionQueue.PendingDisplayTransfers().size()},
            {"process_run_kind", static_cast<uint32_t>(processResult.Kind)},
            {"ui_profile",
             Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile)},
            {"graphics_settings",
             {{"configuration_path", launch.Host.ConfigurationPath},
              {"preset", static_cast<std::uint32_t>(
                             graphicsSettings.Preset)},
              {"frame_rate_mode",
               static_cast<std::uint32_t>(
                   graphicsSettings.FrameRate)},
              {"fov_multiplier", graphicsSettings.FovMultiplier},
              {"toon_mode",
               static_cast<std::uint32_t>(
                   graphicsSettings.Effects.Toon)},
              {"directional_shadows",
               {{"enabled",
                 directionalShadows.Mode ==
                     Fast::Oot3d::DirectionalShadowMode::
                         SingleCascade},
                {"mode",
                 static_cast<std::uint32_t>(
                     directionalShadows.Mode)},
                {"resolution", directionalShadows.Resolution},
                {"maximum_distance",
                 directionalShadows.MaximumDistance},
                {"depth_padding", directionalShadows.DepthPadding},
                {"depth_bias_constant",
                 directionalShadows.DepthBiasConstant},
                {"depth_bias_slope",
                 directionalShadows.DepthBiasSlope},
                {"strength", directionalShadows.Strength},
                {"pcf_radius", directionalShadows.PcfRadius},
                {"stabilize", directionalShadows.Stabilize}}}}},
            {"topscreen_config",
             launch.TopScreenConfigPath.empty()
                 ? nlohmann::json(nullptr)
                 : nlohmann::json(launch.TopScreenConfigPath.string())},
            {"topscreen_settings",
             {{"source",
               launch.TopScreenConfigPath.empty()
                   ? nlohmann::json("built_in_defaults")
                   : nlohmann::json("external_json")},
              {"hud_layout",
               Oot3dNativeGame::TopScreenHudLayoutName(
                   activeTopScreenConfig.HudLayout)},
              {"hud_scale",
               std::round(static_cast<double>(
                              activeTopScreenConfig.HudScale) *
                          100.0) /
                   100.0},
              {"hud_margin_x", activeTopScreenConfig.HudMarginX},
              {"hud_margin_y", activeTopScreenConfig.HudMarginY},
              {"magic_bar_y", activeTopScreenConfig.MagicBarY},
              {"minimap_visible", activeTopScreenConfig.MinimapVisible},
              {"render_hud", activeTopScreenConfig.RenderHud},
              {"render_dpad_icons",
               activeTopScreenConfig.RenderDpadIcons},
              {"render_items_hint",
               activeTopScreenConfig.RenderItemsHint},
              {"select_action",
               Oot3dNativeGame::TopScreenSelectActionName(
                   activeTopScreenConfig.SelectAction)},
              {"exit_items_to_save_screen",
               activeTopScreenConfig.ExitItemsToSaveScreen},
              {"camera_zoom_percent",
               activeTopScreenConfig.CameraZoomPercent},
              {"camera_fov_percent",
               activeTopScreenConfig.CameraFovPercent},
              {"dpad_child",
               nlohmann::json::array(
                   {Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[0]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[1]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[2]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[3])})},
              {"dpad_adult",
               nlohmann::json::array(
                   {Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[0]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[1]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[2]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[3])})},
              {"free_camera_enabled",
               activeTopScreenConfig.FreeCameraEnabled},
              {"free_camera_speed_level",
               activeTopScreenConfig.FreeCameraSpeedLevel},
              {"free_camera_smoothing",
               Oot3dNativeGame::TopScreenFreeCameraSmoothingName(
                   activeTopScreenConfig.FreeCameraSmoothing)},
              {"free_camera_invert_x",
               activeTopScreenConfig.FreeCameraInvertX},
              {"free_camera_invert_y",
               activeTopScreenConfig.FreeCameraInvertY},
              {"c_stick_aim_speed_level",
               activeTopScreenConfig.CStickAimSpeedLevel},
              {"c_stick_aim_invert_x",
               activeTopScreenConfig.CStickAimInvertX},
              {"c_stick_aim_invert_y",
               activeTopScreenConfig.CStickAimInvertY},
              {"gameplay_actions",
               {{"attempts",
                 nativeCandidateDispatch.TopScreenGameplayActions.Attempts},
                {"eligible",
                 nativeCandidateDispatch.TopScreenGameplayActions.Eligible},
                {"equipment_changes",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .EquipmentChanges},
                {"player_refreshes",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .PlayerRefreshes},
                {"direct_item_assignments",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemAssignments},
                {"direct_item_triggers",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemTriggers},
                {"direct_item_clears",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemClears}}},
              {"guest_save_preferences_used", false},
              {"gameplay_preference_chords_enabled", false}}},
            {"process_state_fingerprint", process.StateFingerprint()},
            {"memory_state_fingerprint", process.Memory().StateFingerprint()},
            {"memory_content_fingerprint",
             process.Memory().ContentFingerprint()},
            {"system_ticks", hostServices.SystemTicks()},
            {"lcd_force_black", hostServices.LcdForceBlack()},
            {"top_framebuffer", std::move(topFramebuffer)},
            {"bottom_framebuffer", std::move(bottomFramebuffer)},
            {"display_transfer_events", std::move(displayTransferEvents)},
            {"draw_events", std::move(drawEvents)},
            {"recent_svc_events", std::move(recentSvcEvents)},
            {"filesystem_svc_events", std::move(filesystemSvcEvents)},
            {"ipc_svc_events", std::move(ipcSvcEvents)},
            {"last_svc_by_thread", std::move(lastThreadSvcEvents)},
            {"threads", std::move(threads)},
            {"screenshot_written", screenshotState.Written},
            {"visual_interpolation",
             {{"snapshot_replay_enabled", visualInterpolationEnabled},
              {"cross_frame_history_enabled", visualInterpolationEnabled},
              {"direct_current_frames_presented",
               visualDirectFramesPresented},
              {"reused_snapshot_presentations",
               visualReusedSnapshotPresentations},
              {"selected_top_transfers", selectedTopTransferCount},
              {"complete_snapshots", visualSnapshotCount},
              {"transitions", visualTransitionCount},
              {"matched_draws", visualMatchedDrawCount},
              {"strict_unique_matched_draws",
               visualStrictUniqueMatchedDrawCount},
              {"strict_ordinal_matched_draws",
               visualStrictOrdinalMatchedDrawCount},
              {"structural_ordinal_matched_draws",
               visualStructuralOrdinalMatchedDrawCount},
              {"pipeline_ordinal_matched_draws",
               visualPipelineOrdinalMatchedDrawCount},
              {"changed_continuous_draws", visualChangedContinuousDrawCount},
              {"changed_per_instance_vertex_draws",
               visualChangedPerInstanceVertexDrawCount},
              {"changed_per_vertex_draws", visualChangedPerVertexDrawCount},
              {"ambiguous_previous_draws", visualAmbiguousPreviousDrawCount},
              {"ambiguous_current_draws", visualAmbiguousCurrentDrawCount},
              {"unmatched_previous_draws", visualUnmatchedPreviousDrawCount},
              {"unmatched_current_draws", visualUnmatchedCurrentDrawCount},
              {"samples_presented", visualSamplesPresented},
              {"discontinuities", visualDiscontinuityCount},
              {"namespace_resynchronizations", visualNamespaceResyncCount},
              {"sample_draws", visualSampleDrawCount},
              {"interpolated_draws", visualInterpolatedDrawCount},
              {"interpolated_per_instance_vertex_draws",
               visualInterpolatedPerInstanceVertexDrawCount},
              {"interpolated_per_vertex_draws",
               visualInterpolatedPerVertexDrawCount},
              {"matching_seconds",
               visualInterpolationTiming.MatchingNanoseconds * 1.0e-9},
              {"analysis_seconds",
               visualInterpolationTiming.AnalysisNanoseconds * 1.0e-9},
              {"sampling_seconds",
               visualInterpolationTiming.SamplingNanoseconds * 1.0e-9},
              {"pending_draws",
               picaPresentationScheduler.PendingDrawCount()},
              {"execution_scheduler",
               [&]() {
                 const auto &stats = picaPresentationScheduler.Stats();
                 const auto &timings = picaPresentationScheduler.Timings();
                 const double measuredSeconds =
                     timings.ValidationSeconds +
                     timings.CompositionPublicationSeconds +
                     timings.DrawSubmissionSeconds +
                     timings.MemoryFillSeconds +
                     timings.SnapshotTransferSeconds +
                     timings.PresentTransferSeconds;
                 return nlohmann::json{
                     {"produced_guest_draws", stats.ProducedGuestDraws},
                     {"captured_memory_fills", stats.CapturedMemoryFills},
                     {"captured_display_transfers",
                      stats.CapturedDisplayTransfers},
                     {"completed_visual_frames",
                      stats.CompletedVisualFrames},
                     {"selected_draws", stats.SelectedDraws},
                     {"executed_draws", stats.ExecutedDraws},
                     {"interpolated_draws", stats.InterpolatedDraws},
                     {"execution_lists", stats.ExecutionLists},
                     {"current_frame_lists", stats.CurrentFrameLists},
                     {"interpolated_frame_lists",
                      stats.InterpolatedFrameLists},
                     {"repeated_frame_lists", stats.RepeatedFrameLists},
                     {"dependency_flushes", stats.DependencyFlushes},
                     {"memory_fills_executed",
                      stats.MemoryFillsExecuted},
                     {"display_snapshots_executed",
                      stats.DisplaySnapshotsExecuted},
                     {"presented_transfers", stats.PresentedTransfers},
                     {"reused_snapshots", stats.ReusedSnapshots},
                     {"duplicate_draw_attempts",
                      stats.DuplicateDrawAttempts},
                     {"redundant_top_snapshot_copies_avoided",
                      stats.RedundantTopSnapshotCopiesAvoided},
                     {"composition_sequences_published",
                      stats.CompositionSequencesPublished},
                     {"composition_draw_references_published",
                      stats.CompositionDrawReferencesPublished},
                     {"composition_publication_failures",
                      stats.CompositionPublicationFailures},
                     {"resets", stats.Resets},
                     {"timing_enabled", timings.Enabled},
                     {"execute_seconds", timings.ExecuteSeconds},
                     {"validation_seconds", timings.ValidationSeconds},
                     {"composition_publication_seconds",
                      timings.CompositionPublicationSeconds},
                     {"draw_submission_seconds",
                      timings.DrawSubmissionSeconds},
                     {"draw_uniform_packing_seconds",
                      timings.DrawUniformPackingSeconds},
                     {"draw_view_construction_seconds",
                      timings.DrawViewConstructionSeconds},
                     {"draw_backend_submission_seconds",
                      timings.DrawBackendSubmissionSeconds},
                     {"memory_fill_seconds", timings.MemoryFillSeconds},
                     {"snapshot_transfer_seconds",
                      timings.SnapshotTransferSeconds},
                     {"present_transfer_seconds",
                      timings.PresentTransferSeconds},
                     {"present_existing_seconds",
                      timings.PresentExistingSeconds},
                     {"execute_overhead_seconds",
                      std::max(0.0,
                               timings.ExecuteSeconds - measuredSeconds)},
                 };
               }()},
              {"recent_transitions", std::move(visualTransitionEvents)}}},
            {"enmag_trace",
             {{"init_calls", widescreenProjection.EnMagInitCalls},
              {"update_calls", widescreenProjection.EnMagUpdateCalls},
              {"draw_calls", widescreenProjection.EnMagDrawCalls},
              {"events", std::move(widescreenProjection.EnMagEvents)}}},
            {"ui_lifecycle_trace",
             {{"call_counts", widescreenProjection.UiLifecycleCallCounts},
              {"events", std::move(widescreenProjection.UiLifecycleEvents)}}},
            {"game_state_trace",
             {{"update_calls", widescreenProjection.GameStateUpdateCalls},
              {"events", std::move(widescreenProjection.GameStateEvents)}}},
            {"n64_integrated_ui",
             [&]() {
               const auto &bridge = uiLifecycleBridge.Stats();
               const auto &ui = uiLifecycleBridge.Runtime().Stats();
               const auto &renderer = n64UiRenderer.Stats();
               const auto &nativeTextures = nativeUiTextureProvider.Stats();
               nlohmann::json subsystemCalls = nlohmann::json::object();
               for (std::size_t index = 0;
                    index < bridge.calls_by_subsystem.size(); ++index) {
                 const auto subsystem =
                     static_cast<oot3d::ui::UiSubsystem>(index);
                 subsystemCalls[oot3d::ui::UiSubsystemName(subsystem)] =
                     bridge.calls_by_subsystem[index];
               }
               nlohmann::json phaseCalls = nlohmann::json::object();
               for (std::size_t index = 0; index < bridge.calls_by_phase.size();
                    ++index) {
                 const auto phase = static_cast<oot3d::ui::UiSeamPhase>(index);
                 phaseCalls[oot3d::ui::UiSeamPhaseName(phase)] =
                     bridge.calls_by_phase[index];
               }
               return nlohmann::json{
                   {"selected_profile",
                    Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile)},
                   {"topscreen_profile_active",
                    nativeCandidateDispatch.TopScreenUiProfile},
                   {"topscreen_gameplay_composition_observed",
                    nativeCandidateDispatch.TopScreenLowerCompositionSkips !=
                        0U},
                   {"backend_profile",
                    uiLifecycleBridge.Runtime().Profile().id},
                   {"matched_entries", bridge.matched_entries},
                   {"topscreen_quest_hook_calls", bridge.topscreen_quest_hook_calls},
                   {"topscreen_quest_transforms", bridge.topscreen_quest_transforms},
                   {"topscreen_quest_hook_failures", bridge.topscreen_quest_hook_failures},
                   {"guest_routed_entries", bridge.guest_routed_entries},
                   {"host_routed_entries", bridge.host_routed_entries},
                   {"state_captures", bridge.state_captures},
                   {"fields_read", bridge.fields_read},
                   {"fields_missing", bridge.fields_missing},
                   {"last_guest_entry", bridge.last_guest_entry},
                   {"calls_by_subsystem", std::move(subsystemCalls)},
                   {"calls_by_phase", std::move(phaseCalls)},
                   {"observed_states", ui.observed_states},
                   {"known_file_select_states", ui.known_file_select_states},
                   {"active_file_select_states", ui.active_file_select_states},
                   {"selected_slot_known", ui.selected_slot_known},
                   {"selected_slot", ui.selected_slot},
                   {"controller_state_known", ui.controller_state_known},
                   {"controller_state", ui.controller_state},
                   {"known_name_entry_states", ui.known_name_entry_states},
                   {"active_name_entry_states", ui.active_name_entry_states},
                   {"name_entry_controller_state_known",
                    ui.name_entry_controller_state_known},
                   {"name_entry_controller_state",
                    ui.name_entry_controller_state},
                   {"name_entry_keyboard_page_known",
                    ui.name_entry_keyboard_page_known},
                   {"name_entry_keyboard_page", ui.name_entry_keyboard_page},
                   {"name_entry_cursor_known", ui.name_entry_cursor_known},
                   {"name_entry_cursor_column", ui.name_entry_cursor_column},
                   {"name_entry_cursor_row", ui.name_entry_cursor_row},
                   {"name_entry_length_known", ui.name_entry_length_known},
                   {"name_entry_length", ui.name_entry_length},
                   {"name_entry_action_choice_known",
                    ui.name_entry_action_choice_known},
                   {"name_entry_action_choice", ui.name_entry_action_choice},
                   {"name_entry_confirmation_choice_known",
                    ui.name_entry_confirmation_choice_known},
                   {"name_entry_confirmation_choice",
                    ui.name_entry_confirmation_choice},
                   {"name_entry_save_commit_timer_known",
                    ui.name_entry_save_commit_timer_known},
                   {"name_entry_save_commit_timer",
                    ui.name_entry_save_commit_timer},
                   {"shadow_presentation_frames",
                    bridge.shadow_presentation_frames},
                   {"topscreen_ocarina_frames", bridge.topscreen_ocarina_frames},
                   {"topscreen_ocarina_text_builds", ocarinaText.Builds()},
                   {"topscreen_ocarina_text_releases", ocarinaText.Releases()},
                   {"topscreen_ocarina_primitives", bridge.topscreen_ocarina_primitives},
                   {"topscreen_ocarina_failures", bridge.topscreen_ocarina_failures},
                   {"topscreen_ocarina_error", bridge.topscreen_ocarina_error},
                   {"shadow_presentation_primitives",
                    bridge.shadow_presentation_primitives},
                   {"topscreen_native_touch_copy_frames",
                    bridge.topscreen_native_touch_copy_frames},
                   {"topscreen_native_touch_copy_primitives",
                    bridge.topscreen_native_touch_copy_primitives},
                   {"topscreen_native_touch_visible_primitives",
                    bridge.topscreen_native_touch_visible_primitives},
                   {"topscreen_horse_stamina_visible_frames",
                    bridge.topscreen_horse_stamina_visible_frames},
                   {"topscreen_horse_stamina_visible_primitives",
                    bridge.topscreen_horse_stamina_visible_primitives},
                   {"native_bottom_frontend_presentation_frames",
                    nativeBottomFrontendPresentationCount},
                   {"native_bottom_overlay_frames", 0U},
                   {"native_menu_backdrop_draws_omitted",
                    nativeMenuBackdropDrawOmissionCount},
                   {"native_menu_clear_draws_omitted",
                    nativeMenuClearDrawOmissionCount},
                   {"native_frontend_composition_draws", 0U},
                   {"native_frontend_presentation_active_frames",
                    nativeFrontendPresentationActiveCount},
                   {"native_frontend_bottom_transfer_hits",
                    nativeFrontendBottomTransferHitCount},
                   {"native_frontend_composed_output_hits", 0U},
                   {"renderer_frames", renderer.frames},
                   {"renderer_primitives_drawn", renderer.primitives_drawn},
                   {"renderer_texture_uploads", renderer.texture_uploads},
                   {"renderer_texture_cache_hits", renderer.texture_cache_hits},
                   {"renderer_native_texture_resolves",
                    renderer.native_texture_resolves},
                   {"renderer_native_texture_failures",
                    renderer.native_texture_failures},
                   {"native_texture_provider_resolves",
                    nativeTextures.resolve_calls},
                   {"native_texture_provider_physical_reads",
                    nativeTextures.physical_reads},
                   {"native_texture_provider_translated_guest_reads",
                    nativeTextures.translated_guest_reads},
                   {"native_texture_provider_bytes_read",
                    nativeTextures.bytes_read},
                   {"native_texture_provider_decode_failures",
                    nativeTextures.decode_failures},
                   {"native_texture_provider_override_checks",
                    nativeTextures.override_checks},
                   {"native_texture_provider_overrides_applied",
                    nativeTextures.overrides_applied},
                   {"native_texture_provider_overrides_already_applied",
                    nativeTextures.overrides_already_applied},
                   {"native_texture_provider_override_no_matches",
                    nativeTextures.override_no_matches},
                   {"topscreen_texture_override_pack_loaded",
                    topScreenTextureOverridePackPointer != nullptr},
                   {"hd_font_pack_loaded", hdFontsLoaded},
                   {"hd_font_native_blits", hdFonts.Blits},
                   {"hd_font_atlas_rebuilds", hdFonts.Rebuilds},
                   {"hd_font_texture_replacements", hdFonts.Replacements},
                   {"topscreen_texture_override_pica",
                    [&]() {
                      if (!topScreenTextureOverrideRuntime.has_value()) {
                        return nlohmann::json(nullptr);
                      }
                      const auto &overrides =
                          topScreenTextureOverrideRuntime->Stats();
                      nlohmann::json targets = nlohmann::json::array();
                      for (const auto &target :
                           topScreenTextureOverrideRuntime->TargetStats()) {
                        targets.push_back({
                            {"physical_address", target.physical_address},
                            {"guest_surface_address",
                             target.guest_surface_address},
                            {"byte_count", target.byte_count},
                            {"semantic_names", target.semantic_names},
                            {"last_width", target.last_width},
                            {"last_height", target.last_height},
                            {"last_format", target.last_format},
                            {"last_payload_hash", target.last_payload_hash},
                            {"payload_checks", target.payload_checks},
                            {"applied", target.applied},
                            {"already_applied", target.already_applied},
                            {"no_match", target.no_match},
                        });
                      }
                      return nlohmann::json{
                          {"observe_calls", overrides.observe_calls},
                          {"identities_observed",
                           overrides.identities_observed},
                          {"target_bindings", overrides.target_bindings},
                          {"payload_checks", overrides.payload_checks},
                          {"applied", overrides.applied},
                          {"already_applied", overrides.already_applied},
                          {"no_match", overrides.no_match},
                          {"targets", std::move(targets)},
                      };
                    }()},
               };
             }()},
            {"renderer_queue_events",
             std::move(widescreenProjection.RendererQueueEvents)},
            {"renderer_queue_call_counts",
             widescreenProjection.RendererQueueCallCounts},
            {"renderer_command_queue_state",
             {{"submission", readQueueState(kRendererCommandQueue)},
              {"completion", readQueueState(kRendererCommandQueue + 0xB4U)},
              {"command_words", readRendererCommandState()}}},
            {"widescreen_projection",
             {{"native_aspect",
               widescreenProjection.Projection.Presentation.NativeAspect},
              {"output_aspect",
               widescreenProjection.Projection.Presentation.OutputAspect},
              {"aspect_extension",
               static_cast<std::uint32_t>(
                   widescreenProjection.Projection.Presentation.Extension)},
              {"horizontal_expansion",
               widescreenProjection.Projection.Presentation
                   .HorizontalFovExpansion},
              {"vertical_expansion",
               widescreenProjection.Projection.Presentation
                   .VerticalFovExpansion},
              {"fov_multiplier",
               widescreenProjection.Projection.FovMultiplier},
              {"frustum_calls",
               widescreenProjection.Projection.FrustumCalls},
              {"orthographic_calls",
               widescreenProjection.Projection.OrthographicCalls},
              {"callsites", std::move(projectionCallsites)},
              {"topscreen_pause_projection",
               {{"calls", widescreenProjection.TopScreenPauseProjectionCalls},
                {"failures",
                 widescreenProjection.TopScreenPauseProjectionFailures},
                {"events", std::move(widescreenProjection
                                         .TopScreenPauseProjectionEvents)}}}}},
            {"a32_block_profile",
             {{"enabled", launch.ProfileA32Blocks},
              {"sample_rate_denominator", 64},
              {"block_entries", widescreenProjection.BlockEntries},
              {"total_samples", totalBlockSampleCount},
              {"reported_samples", blockSampleCount},
              {"blocks", std::move(sampledBlockEntries)}}},
            {"compiled_functions",
              [&]() {
                const auto stats =
                    Oot3dNativeGame::GetOot3dCompiledFunctionStats();
                nlohmann::json externalTargets = nlohmann::json::array();
                for (const auto& target :
                     Oot3dNativeGame::GetOot3dWholeAotExternalTargets()) {
                  externalTargets.push_back({
                      {"entry", target.Entry},
                      {"calls", target.Calls},
                      {"manual_compiled_calls", target.ManualCompiledCalls},
                      {"timing_samples", target.TimingSamples},
                      {"estimated_seconds",
                       target.TimingSampleNanoseconds * 64.0e-9},
                  });
                }
                const auto typedGameplayStats =
                    Oot3dNativeGame::GetOot3dTypedGameplayStats();
               const auto massAotStats =
                   Oot3dNativeGame::GetOot3dMassAotStats();
               return nlohmann::json{
                   {"enabled", !launch.DisableCompiledFunctions},
                   {"typed_gameplay_entry_points",
                    Oot3dNativeGame::Oot3dTypedGameplayEntryPoints().size()},
                   {"typed_gameplay_calls", typedGameplayStats.Calls},
                   {"typed_gameplay_angle_calls",
                    typedGameplayStats.AngleCalls},
                   {"typed_gameplay_actor_calls",
                    typedGameplayStats.ActorCalls},
                   {"typed_gameplay_enko_blink_block_calls",
                    typedGameplayStats.EnKoBlinkBlockCalls},
                   {"typed_gameplay_enko_blink_logical_advances",
                    typedGameplayStats.EnKoBlinkLogicalAdvances},
                   {"typed_gameplay_enko_blink_intermediate_holds",
                    typedGameplayStats.EnKoBlinkIntermediateHolds},
                   {"typed_gameplay_enko_blink_timer_advances",
                    typedGameplayStats.EnKoBlinkTimerAdvances},
                   {"typed_gameplay_enko_blink_sequence_advances",
                    typedGameplayStats.EnKoBlinkSequenceAdvances},
                   {"typed_gameplay_enko_blink_rng_dispatches",
                    typedGameplayStats.EnKoBlinkRngDispatches},
                   {"typed_gameplay_enko_blink_rng_returns",
                    typedGameplayStats.EnKoBlinkRngReturns},
                   {"typed_gameplay_enkanban_phase_block_calls",
                    typedGameplayStats.EnKanbanPhaseBlockCalls},
                   {"typed_gameplay_enkanban_phase_logical_advances",
                    typedGameplayStats.EnKanbanPhaseLogicalAdvances},
                   {"typed_gameplay_enkanban_phase_intermediate_holds",
                    typedGameplayStats.EnKanbanPhaseIntermediateHolds},
                   {"typed_gameplay_enkanban_state0_countdown_block_calls",
                    typedGameplayStats.EnKanbanState0CountdownBlockCalls},
                   {"typed_gameplay_enkanban_state0_countdown_logical_advances",
                    typedGameplayStats
                        .EnKanbanState0CountdownLogicalAdvances},
                   {"typed_gameplay_enkanban_state0_countdown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanState0CountdownIntermediateHolds},
                   {"typed_gameplay_enkanban_actor_flag_countdown_block_calls",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownBlockCalls},
                   {"typed_gameplay_enkanban_actor_flag_countdown_logical_advances",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownLogicalAdvances},
                   {"typed_gameplay_enkanban_actor_flag_countdown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownIntermediateHolds},
                   {"typed_gameplay_enkanban_actor_flag_clears",
                    typedGameplayStats.EnKanbanActorFlagClears},
                   {"typed_gameplay_enkanban_interaction_cooldown_block_calls",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownBlockCalls},
                   {"typed_gameplay_enkanban_interaction_cooldown_logical_advances",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownLogicalAdvances},
                   {"typed_gameplay_enkanban_interaction_cooldown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownIntermediateHolds},
                   {"typed_gameplay_enkanban_interaction_cooldown_ready_passes",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownReadyPasses},
                   {"typed_gameplay_enkanban_interaction_cooldown_blocked_passes",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownBlockedPasses},
                   {"typed_gameplay_enkanban_draw_gate_ramp_block_calls",
                    typedGameplayStats.EnKanbanDrawGateRampBlockCalls},
                   {"typed_gameplay_enkanban_draw_gate_ramp_logical_advances",
                    typedGameplayStats.EnKanbanDrawGateRampLogicalAdvances},
                   {"typed_gameplay_enkanban_draw_gate_ramp_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanDrawGateRampIntermediateHolds},
                   {"typed_gameplay_enkanban_draw_gate_ramp_inactive_passes",
                    typedGameplayStats.EnKanbanDrawGateRampInactivePasses},
                   {"typed_gameplay_enkanban_draw_gate_ramp_increase_steps",
                    typedGameplayStats.EnKanbanDrawGateRampIncreaseSteps},
                   {"typed_gameplay_enkanban_draw_gate_ramp_decrease_steps",
                    typedGameplayStats.EnKanbanDrawGateRampDecreaseSteps},
                   {"typed_gameplay_enkanban_draw_gate_ramp_clamps",
                    typedGameplayStats.EnKanbanDrawGateRampClamps},
                   {"typed_gameplay_enkanban_oscillator_axis_block_calls",
                    typedGameplayStats.EnKanbanOscillatorAxisBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_x_block_calls",
                    typedGameplayStats.EnKanbanOscillatorXBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_y_block_calls",
                    typedGameplayStats.EnKanbanOscillatorYBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_rate_adjusted_steps",
                    typedGameplayStats
                        .EnKanbanOscillatorRateAdjustedSteps},
                   {"typed_gameplay_enkanban_oscillator_ground_resets",
                    typedGameplayStats.EnKanbanOscillatorGroundResets},
                   {"typed_gameplay_enkanban_oscillator_velocity_clamps",
                    typedGameplayStats.EnKanbanOscillatorVelocityClamps},
                   {"typed_gameplay_enkanban_piece_lifetime_block_calls",
                    typedGameplayStats.EnKanbanPieceLifetimeBlockCalls},
                   {"typed_gameplay_enkanban_piece_lifetime_logical_decrements",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeLogicalDecrements},
                   {"typed_gameplay_enkanban_piece_lifetime_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeIntermediateHolds},
                   {"typed_gameplay_enkanban_piece_lifetime_state_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeStateTransitions},
                   {"typed_gameplay_enkanban_piece_lifetime_zero_crossing_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeZeroCrossingTransitions},
                   {"typed_gameplay_enkanban_piece_lifetime_existing_zero_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeExistingZeroTransitions},
                   {"typed_gameplay_enkanban_ripple_gate_calls",
                    typedGameplayStats.EnKanbanRippleGateCalls},
                   {"typed_gameplay_enkanban_ripple_dispatches",
                    typedGameplayStats.EnKanbanRippleDispatches},
                   {"typed_gameplay_enkanban_ripple_intermediate_suppressions",
                    typedGameplayStats
                        .EnKanbanRippleIntermediateSuppressions},
                   {"typed_gameplay_actor_update_all_context_freeze_block_calls",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeBlockCalls},
                   {"typed_gameplay_actor_update_all_context_freeze_logical_advances",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeLogicalAdvances},
                   {"typed_gameplay_actor_update_all_context_freeze_intermediate_holds",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeIntermediateHolds},
                   {"typed_gameplay_actor_update_all_instance_freeze_block_calls",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeBlockCalls},
                   {"typed_gameplay_actor_update_all_instance_freeze_logical_advances",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeLogicalAdvances},
                   {"typed_gameplay_actor_update_all_instance_freeze_intermediate_holds",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeIntermediateHolds},
                   {"typed_gameplay_actor_update_all_instance_freeze_gate_passes",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeGatePasses},
                   {"typed_gameplay_actor_update_all_instance_freeze_gate_skips",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeGateSkips},
                   {"typed_gameplay_actor_update_all_effect_timer_block_calls",
                    typedGameplayStats.ActorUpdateAllEffectTimerBlockCalls},
                   {"typed_gameplay_actor_update_all_effect_timer_logical_field_advances",
                    typedGameplayStats
                        .ActorUpdateAllEffectTimerLogicalFieldAdvances},
                   {"typed_gameplay_actor_update_all_effect_timer_intermediate_field_holds",
                    typedGameplayStats
                        .ActorUpdateAllEffectTimerIntermediateFieldHolds},
                   {"typed_gameplay_actor_init_callback_dispatches",
                    typedGameplayStats.ActorInitCallbackDispatches},
                   {"typed_gameplay_actor_init_callback_returns",
                    typedGameplayStats.ActorInitCallbackReturns},
                   {"typed_gameplay_actor_update_callback_dispatches",
                    typedGameplayStats.ActorUpdateCallbackDispatches},
                   {"typed_gameplay_actor_destroy_callback_dispatches",
                    typedGameplayStats.ActorDestroyCallbackDispatches},
                   {"typed_gameplay_actor_resource_cleanup_dispatches",
                    typedGameplayStats.ActorResourceCleanupDispatches},
                   {"typed_gameplay_actor_destroy_returns",
                    typedGameplayStats.ActorDestroyReturns},
                   {"typed_gameplay_audio_request_reference_acquire_dispatches",
                    typedGameplayStats
                        .AudioRequestReferenceAcquireDispatches},
                   {"typed_gameplay_audio_request_status_query_dispatches",
                    typedGameplayStats.AudioRequestStatusQueryDispatches},
                   {"typed_gameplay_audio_request_reference_cleanup_dispatches",
                    typedGameplayStats
                        .AudioRequestReferenceCleanupDispatches},
                   {"typed_gameplay_audio_request_callback_returns",
                    typedGameplayStats.AudioRequestCallbackReturns},
                   {"typed_gameplay_pause_ui_alpha_pause_state_dispatches",
                    typedGameplayStats.PauseUiAlphaPauseStateDispatches},
                   {"typed_gameplay_pause_ui_alpha_first_step_dispatches",
                    typedGameplayStats.PauseUiAlphaFirstStepDispatches},
                   {"typed_gameplay_pause_ui_alpha_tail_step_dispatches",
                    typedGameplayStats.PauseUiAlphaTailStepDispatches},
                   {"typed_gameplay_pause_ui_alpha_returns",
                    typedGameplayStats.PauseUiAlphaReturns},
                   {"typed_gameplay_dyna_interaction_reset_matches",
                    typedGameplayStats.DynaInteractionResetMatches},
                   {"typed_gameplay_dyna_interaction_reset_misses",
                    typedGameplayStats.DynaInteractionResetMisses},
                   {"typed_gameplay_player_release_lock_on_calls",
                    typedGameplayStats.PlayerReleaseLockOnCalls},
                   {"typed_gameplay_actor_update_record_initialize_calls",
                    typedGameplayStats.ActorUpdateRecordInitializeCalls},
                   {"typed_gameplay_actor_update_record_clear_calls",
                    typedGameplayStats.ActorUpdateRecordClearCalls},
                   {"typed_gameplay_record_initializer_memzero_dispatches",
                    typedGameplayStats.RecordInitializerMemzeroDispatches},
                   {"typed_gameplay_record_initializer_returns",
                    typedGameplayStats.RecordInitializerReturns},
                   {"typed_gameplay_player_calls",
                    typedGameplayStats.PlayerCalls},
                   {"typed_gameplay_math_calls", typedGameplayStats.MathCalls},
                   {"typed_gameplay_animation_calls",
                    typedGameplayStats.AnimationCalls},
                   {"typed_gameplay_renderer_calls",
                    typedGameplayStats.RendererCalls},
                   {"typed_gameplay_cutscene_calls",
                    typedGameplayStats.CutsceneCalls},
                   {"typed_gameplay_camera_calls",
                    typedGameplayStats.CameraCalls},
                   {"typed_gameplay_camera_mode_frame_countdown_block_calls",
                    typedGameplayStats.CameraModeFrameCountdownBlockCalls},
                   {"typed_gameplay_camera_mode_frame_countdown_logical_advances",
                    typedGameplayStats
                        .CameraModeFrameCountdownLogicalAdvances},
                   {"typed_gameplay_camera_mode_frame_countdown_intermediate_holds",
                    typedGameplayStats
                        .CameraModeFrameCountdownIntermediateHolds},
                   {"typed_gameplay_camera_special5_timer_block_calls",
                    typedGameplayStats.CameraSpecial5TimerBlockCalls},
                   {"typed_gameplay_camera_special5_timer_logical_decrements",
                    typedGameplayStats
                        .CameraSpecial5TimerLogicalDecrements},
                   {"typed_gameplay_camera_special5_timer_intermediate_holds",
                    typedGameplayStats
                        .CameraSpecial5TimerIntermediateHolds},
                   {"typed_gameplay_camera_special5_timer_zero_transitions",
                    typedGameplayStats.CameraSpecial5TimerZeroTransitions},
                   {"typed_gameplay_camera_special5_timer_terminal_continues",
                    typedGameplayStats
                        .CameraSpecial5TimerTerminalContinues},
                   {"typed_gameplay_camera_floor_miss_counter_block_calls",
                    typedGameplayStats.CameraFloorMissCounterBlockCalls},
                   {"typed_gameplay_camera_floor_miss_counter_logical_advances",
                    typedGameplayStats
                        .CameraFloorMissCounterLogicalAdvances},
                   {"typed_gameplay_camera_floor_miss_counter_intermediate_holds",
                    typedGameplayStats
                        .CameraFloorMissCounterIntermediateHolds},
                   {"typed_gameplay_camera_interface_delay_block_calls",
                    typedGameplayStats.CameraInterfaceDelayBlockCalls},
                   {"typed_gameplay_camera_interface_delay_logical_advances",
                    typedGameplayStats
                        .CameraInterfaceDelayLogicalAdvances},
                   {"typed_gameplay_camera_interface_delay_intermediate_holds",
                    typedGameplayStats
                        .CameraInterfaceDelayIntermediateHolds},
                   {"typed_gameplay_camera_water_distortion_timer_block_calls",
                    typedGameplayStats
                        .CameraWaterDistortionTimerBlockCalls},
                   {"typed_gameplay_camera_water_distortion_timer_logical_advances",
                    typedGameplayStats
                        .CameraWaterDistortionTimerLogicalAdvances},
                   {"typed_gameplay_camera_water_distortion_timer_intermediate_holds",
                    typedGameplayStats
                        .CameraWaterDistortionTimerIntermediateHolds},
                   {"typed_gameplay_camera_water_distortion_fractional_samples",
                    typedGameplayStats
                        .CameraWaterDistortionFractionalSamples},
                   {"typed_gameplay_camera_water_distortion_flag4_samples",
                    typedGameplayStats.CameraWaterDistortionFlag4Samples},
                   {"typed_gameplay_camera_water_distortion_flag8_samples",
                    typedGameplayStats.CameraWaterDistortionFlag8Samples},
                   {"typed_gameplay_camera_water_distortion_custom_samples",
                    typedGameplayStats.CameraWaterDistortionCustomSamples},
                   {"typed_gameplay_camera_water_distortion_sample_failures",
                    typedGameplayStats.CameraWaterDistortionSampleFailures},
                   {"typed_gameplay_camera_quake_callback_calls",
                    typedGameplayStats.CameraQuakeCallbackCalls},
                   {"typed_gameplay_camera_quake_logical_advances",
                    typedGameplayStats.CameraQuakeLogicalAdvances},
                   {"typed_gameplay_camera_quake_intermediate_holds",
                    typedGameplayStats.CameraQuakeIntermediateHolds},
                   {"typed_gameplay_camera_quake_random_samples",
                    typedGameplayStats.CameraQuakeRandomSamples},
                   {"typed_gameplay_camera_quake_signal_reuses",
                    typedGameplayStats.CameraQuakeSignalReuses},
                   {"typed_gameplay_camera_quake_signal_cache_misses",
                    typedGameplayStats.CameraQuakeSignalCacheMisses},
                   {"typed_gameplay_camera_quake_helper_dispatches",
                    typedGameplayStats.CameraQuakeHelperDispatches},
                   {"typed_gameplay_camera_quake_return_dispatches",
                    typedGameplayStats.CameraQuakeReturnDispatches},
                   {"typed_gameplay_camera_quake_failures",
                    typedGameplayStats.CameraQuakeFailures},
                   {"typed_gameplay_cutscene_normal_frame_block_calls",
                    typedGameplayStats.CutsceneNormalFrameBlockCalls},
                   {"typed_gameplay_cutscene_logical_frame_advances",
                    typedGameplayStats.CutsceneLogicalFrameAdvances},
                   {"typed_gameplay_cutscene_intermediate_holds",
                    typedGameplayStats.CutsceneIntermediateHolds},
                   {"typed_gameplay_cutscene_command_dispatches",
                    typedGameplayStats.CutsceneCommandDispatches},
                   {"typed_gameplay_cutscene_actor_cue_interpolation_calls",
                    typedGameplayStats.CutsceneActorCueInterpolationCalls},
                   {"typed_gameplay_cutscene_actor_cue_fractional_samples",
                    typedGameplayStats.CutsceneActorCueFractionalSamples},
                   {"typed_gameplay_cutscene_camera_binding_observations",
                    typedGameplayStats.CutsceneCameraBindingObservations},
                   {"typed_gameplay_cutscene_camera_rejected_observations",
                    typedGameplayStats.CutsceneCameraRejectedObservations},
                   {"typed_gameplay_cutscene_camera_fractional_samples",
                    typedGameplayStats.CutsceneCameraFractionalSamples},
                   {"typed_gameplay_cutscene_camera_curve_samples",
                    typedGameplayStats.CutsceneCameraCurveSamples},
                   {"typed_gameplay_cutscene_camera_sample_failures",
                    typedGameplayStats.CutsceneCameraSampleFailures},
                   {"typed_gameplay_actor_cutscene_camera_binding_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraBindingObservations},
                   {"typed_gameplay_actor_cutscene_camera_priming_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraPrimingObservations},
                   {"typed_gameplay_actor_cutscene_camera_rejected_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraRejectedObservations},
                   {"typed_gameplay_actor_cutscene_camera_intermediate_holds",
                    typedGameplayStats.ActorCutsceneCameraIntermediateHolds},
                   {"typed_gameplay_actor_cutscene_camera_fractional_samples",
                    typedGameplayStats.ActorCutsceneCameraFractionalSamples},
                   {"typed_gameplay_actor_cutscene_camera_curve_samples",
                    typedGameplayStats.ActorCutsceneCameraCurveSamples},
                   {"typed_gameplay_actor_cutscene_camera_sample_failures",
                    typedGameplayStats.ActorCutsceneCameraSampleFailures},
                   {"typed_gameplay_fishing_state_block_calls",
                    typedGameplayStats.FishingStateBlockCalls},
                   {"typed_gameplay_fishing_state_logical_advances",
                    typedGameplayStats.FishingStateLogicalAdvances},
                   {"typed_gameplay_fishing_state_intermediate_holds",
                    typedGameplayStats.FishingStateIntermediateHolds},
                   {"typed_gameplay_damage_run_timer_block_calls",
                    typedGameplayStats.DamageRunTimerBlockCalls},
                   {"typed_gameplay_damage_run_timer_logical_advances",
                    typedGameplayStats.DamageRunTimerLogicalAdvances},
                   {"typed_gameplay_damage_run_timer_intermediate_holds",
                    typedGameplayStats.DamageRunTimerIntermediateHolds},
                   {"typed_gameplay_invincibility_timer_block_calls",
                    typedGameplayStats.InvincibilityTimerBlockCalls},
                   {"typed_gameplay_invincibility_timer_logical_advances",
                    typedGameplayStats.InvincibilityTimerLogicalAdvances},
                   {"typed_gameplay_invincibility_timer_intermediate_holds",
                    typedGameplayStats.InvincibilityTimerIntermediateHolds},
                   {"typed_gameplay_invincibility_timer_positive_action_holds",
                    typedGameplayStats
                        .InvincibilityTimerPositiveActionHolds},
                   {"typed_gameplay_damage_flicker_counter_block_calls",
                    typedGameplayStats.DamageFlickerCounterBlockCalls},
                   {"typed_gameplay_damage_flicker_counter_logical_advances",
                    typedGameplayStats
                        .DamageFlickerCounterLogicalAdvances},
                   {"typed_gameplay_damage_flicker_counter_intermediate_holds",
                    typedGameplayStats
                        .DamageFlickerCounterIntermediateHolds},
                   {"typed_gameplay_respawn_damage_block_calls",
                    typedGameplayStats.RespawnDamageBlockCalls},
                   {"typed_gameplay_respawn_damage_logical_advances",
                    typedGameplayStats.RespawnDamageLogicalAdvances},
                   {"typed_gameplay_respawn_damage_intermediate_holds",
                    typedGameplayStats.RespawnDamageIntermediateHolds},
                   {"typed_gameplay_respawn_damage_audio_dispatches",
                    typedGameplayStats.RespawnDamageAudioDispatches},
                   {"typed_gameplay_random_turn_timer_decrement_block_calls",
                    typedGameplayStats
                        .RandomTurnTimerDecrementBlockCalls},
                   {"typed_gameplay_random_turn_timer_refresh_block_calls",
                    typedGameplayStats.RandomTurnTimerRefreshBlockCalls},
                   {"typed_gameplay_random_turn_timer_logical_advances",
                    typedGameplayStats.RandomTurnTimerLogicalAdvances},
                   {"typed_gameplay_random_turn_timer_intermediate_holds",
                    typedGameplayStats.RandomTurnTimerIntermediateHolds},
                   {"typed_gameplay_random_turn_timer_rng_dispatches",
                    typedGameplayStats.RandomTurnTimerRngDispatches},
                   {"typed_gameplay_random_turn_timer_rng_intermediate_suppressions",
                    typedGameplayStats
                        .RandomTurnTimerRngIntermediateSuppressions},
                   {"typed_gameplay_attention_persistence_block_calls",
                    typedGameplayStats.AttentionPersistenceBlockCalls},
                   {"typed_gameplay_attention_persistence_logical_advances",
                    typedGameplayStats
                        .AttentionPersistenceLogicalAdvances},
                   {"typed_gameplay_attention_persistence_intermediate_holds",
                    typedGameplayStats
                        .AttentionPersistenceIntermediateHolds},
                   {"typed_gameplay_attention_persistence_saturation_holds",
                    typedGameplayStats
                        .AttentionPersistenceSaturationHolds},
                   {"typed_gameplay_common_countdown_block_calls",
                    typedGameplayStats.CommonCountdownBlockCalls},
                   {"typed_gameplay_common_countdown_logical_field_advances",
                    typedGameplayStats.CommonCountdownLogicalFieldAdvances},
                   {"typed_gameplay_common_countdown_logical_field_intermediate_holds",
                    typedGameplayStats
                        .CommonCountdownLogicalFieldIntermediateHolds},
                   {"typed_gameplay_fairy_revive_grace_timer_advances",
                    typedGameplayStats.FairyReviveGraceTimerAdvances},
                   {"typed_gameplay_underwater_timer_reset_block_calls",
                    typedGameplayStats.UnderwaterTimerResetBlockCalls},
                   {"typed_gameplay_underwater_timer_increment_block_calls",
                    typedGameplayStats.UnderwaterTimerIncrementBlockCalls},
                   {"typed_gameplay_underwater_timer_logical_advances",
                    typedGameplayStats.UnderwaterTimerLogicalAdvances},
                   {"typed_gameplay_underwater_timer_intermediate_holds",
                    typedGameplayStats.UnderwaterTimerIntermediateHolds},
                   {"typed_gameplay_melee_action_timer_block_calls",
                    typedGameplayStats.MeleeActionTimerBlockCalls},
                   {"typed_gameplay_melee_action_timer_logical_advances",
                    typedGameplayStats.MeleeActionTimerLogicalAdvances},
                   {"typed_gameplay_melee_action_timer_intermediate_holds",
                    typedGameplayStats.MeleeActionTimerIntermediateHolds},
                   {"typed_gameplay_melee_action_combo_clears",
                    typedGameplayStats.MeleeActionComboClears},
                   {"typed_gameplay_melee_weapon_tip_combo_block_calls",
                    typedGameplayStats.MeleeWeaponTipComboBlockCalls},
                   {"typed_gameplay_melee_weapon_tip_combo_logical_advances",
                    typedGameplayStats
                        .MeleeWeaponTipComboLogicalAdvances},
                   {"typed_gameplay_melee_weapon_tip_combo_intermediate_holds",
                    typedGameplayStats
                        .MeleeWeaponTipComboIntermediateHolds},
                   {"typed_gameplay_retained_aot_fallbacks",
                    typedGameplayStats.RetainedAotFallbacks},
                   {"typed_gameplay_read_failures",
                    typedGameplayStats.ReadFailures},
                   {"typed_gameplay_write_failures",
                    typedGameplayStats.WriteFailures},
                   {"mass_aot_enabled",
                    !launch.DisableCompiledFunctions &&
                        !launch.DisableMassAot &&
                        Oot3dNativeGame::Oot3dMassAotAvailable()},
                   {"mass_aot_entry_points",
                    Oot3dNativeGame::Oot3dMassAotBlockEntryPoints().size()},
                   {"mass_aot_excluded_boundary_entries",
                    Oot3dNativeGame::Oot3dMassAotExcludedBoundaryCount()},
                   {"mass_aot_calls", massAotStats.Calls},
                   {"mass_aot_blocks", massAotStats.Blocks},
                   {"mass_aot_region_exits", massAotStats.RegionExits},
                   {"mass_aot_svc_exits", massAotStats.SvcExits},
                   {"mass_aot_block_limit_exits", massAotStats.BlockLimitExits},
                   {"mass_aot_memory_faults", massAotStats.MemoryFaults},
                   {"mass_aot_missing_blocks", massAotStats.MissingBlocks},
                   {"mass_aot_fallback_exits", massAotStats.FallbackExits},
                   {"mass_aot_unsupported_exits",
                    massAotStats.UnsupportedExits},
                   {"whole_aot_available", wholeAotAvailable},
                   {"whole_aot_requested",
                    !launch.DisableCompiledFunctions &&
                        !launch.DisableWholeAot},
                   {"whole_aot_enabled", enableWholeAot},
                   {"manual_compiled_functions_enabled",
                    !launch.DisableCompiledFunctions &&
                    enableManualCompiledFunctions},
                   {"entry_points",
                    Oot3dNativeGame::Oot3dCompiledFunctionEntryPoints(
                        enableWholeAot,
                        enableCompiledFunctions &&
                            enableManualCompiledFunctions)
                        .size()},
                   {"calls", stats.Calls},
                   {"bytes_processed", stats.BytesProcessed},
                   {"retained_arm_fallbacks", stats.RetainedArmFallbacks},
                   {"memcpy_overlap_fallbacks", stats.MemcpyOverlapFallbacks},
                   {"memcpy_overlap_bytes", stats.MemcpyOverlapBytes},
                   {"memcpy_destination_after_source_fallbacks",
                    stats.MemcpyDestinationAfterSourceFallbacks},
                    {"memcpy_destination_before_source_fallbacks",
                     stats.MemcpyDestinationBeforeSourceFallbacks},
                    {"mtx3x4_fast_calls", stats.Mtx3x4FastCalls},
                    {"mtx3x4_soft_calls", stats.Mtx3x4SoftCalls},
                    {"mtx3x4_validation_calls",
                     stats.Mtx3x4ValidationCalls},
                    {"mtx3x4_value_mismatches",
                     stats.Mtx3x4ValueMismatches},
                    {"mtx3x4_flag_mismatches",
                     stats.Mtx3x4FlagMismatches},
                    {"whole_aot_calls", stats.WholeAotCalls},
                   {"whole_aot_direct_calls", stats.WholeAotDirectCalls},
                   {"whole_aot_indirect_calls", stats.WholeAotIndirectCalls},
                   {"whole_aot_resolved_indirect_calls",
                    stats.WholeAotResolvedIndirectCalls},
                    {"whole_aot_external_calls", stats.WholeAotExternalCalls},
                    {"whole_aot_external_targets",
                     std::move(externalTargets)},
                    {"whole_aot_svc_exits", stats.WholeAotSvcExits},
                   {"whole_aot_block_budget", launch.WholeAotBlockBudget},
                   {"whole_aot_block_limit_exits",
                    stats.WholeAotBlockLimitExits},
                   {"whole_aot_memory_faults", stats.WholeAotMemoryFaults},
                   {"whole_aot_unsupported_exits",
                    stats.WholeAotUnsupportedExits},
               };
             }()},
            {"true_aot_blocks",
             [&]() {
               const auto stats = Oot3dNativeGame::GetOot3dTrueAotBlockStats();
               return nlohmann::json{
                   {"enabled", !launch.DisableCompiledFunctions &&
                                   !launch.DisableTrueAotBlocks},
                   {"entry_points",
                    Oot3dNativeGame::Oot3dTrueAotBlockEntryPoints().size()},
                   {"calls", stats.Calls},
                   {"iterations", stats.Iterations},
                   {"memory_faults", stats.MemoryFaults},
               };
             }()},
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
            {"source_gameplay_profile",
             {{"requested", sourceGameplayOwners.ProfileRequested},
              {"required_owner_count",
               Oot3dNativeGame::kSourceGameplayOwnerCount},
              {"enabled_owner_count",
               sourceGameplayOwners.EnabledOwnerCount()},
              {"complete", sourceGameplayOwners.Complete()},
              {"owners",
               {{"game_state_update",
                 sourceGameplayOwners.GameStateUpdate},
                {"actor_init_context",
                 sourceGameplayOwners.ActorInitContext},
                {"actor_update_all",
                 sourceGameplayOwners.ActorUpdateAll},
                {"cutscene_update_frame",
                 sourceGameplayOwners.CutsceneUpdateFrame},
                {"cutscene_process_commands",
                 sourceGameplayOwners.CutsceneProcessCommands},
                {"camera_update",
                 sourceGameplayOwners.CameraUpdate},
                {"player_update",
                 sourceGameplayOwners.PlayerUpdate},
                {"player_update_common",
                 sourceGameplayOwners.PlayerUpdateCommon}}}}},
            {"source_actor_update_all",
             [&]() {
               const auto stats = sourceActorUpdateAll.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceActorUpdateAll},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"svc_calls", stats.SvcCalls},
                   {"callback_calls", stats.CallbackCalls},
                   {"init_callback_calls", stats.InitCallbackCalls},
                   {"update_callback_calls", stats.UpdateCallbackCalls},
                   {"source_callback_calls", stats.SourceCallbackCalls},
                   {"source_obj_hana_init_calls",
                    stats.SourceObjHanaInitCalls},
                   {"source_callback_failures",
                    stats.SourceCallbackFailures},
                   {"destroy_calls", stats.DestroyCalls},
                   {"free_calls", stats.FreeCalls},
                   {"registry_release_scans", stats.RegistryReleaseScans},
                   {"failures", stats.Failures},
                   {"last_error", sourceActorUpdateAll.LastError()},
               };
             }()},
            {"source_actor_init_context",
             [&]() {
               const auto stats = sourceActorInitContext.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceActorInitContext},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_factory_calls", stats.DynamicFactoryCalls},
                   {"dynamic_allocator_calls", stats.DynamicAllocatorCalls},
                   {"actor_spawn_calls", stats.ActorSpawnCalls},
                   {"light_setup_calls", stats.LightSetupCalls},
                   {"tail_helper_calls", stats.TailHelperCalls},
                   {"failures", stats.Failures},
                   {"last_error", sourceActorInitContext.LastError()},
               };
             }()},
            {"source_cutscene_update_frame",
             [&]() {
               const auto stats = sourceCutsceneUpdateFrame.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCutsceneUpdateFrame},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"clock_query_calls", stats.ClockQueryCalls},
                   {"clock_commit_calls", stats.ClockCommitCalls},
                   {"process_command_calls", stats.ProcessCommandCalls},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourceCutsceneUpdateFrame.LastError()},
               };
              }()},
            {"source_cutscene_process_commands",
             [&]() {
               const auto stats =
                   sourceCutsceneProcessCommands.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCutsceneProcessCommands},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_callback_calls",
                    stats.DynamicCallbackCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"conversion_operations",
                    stats.ConversionOperations},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourceCutsceneProcessCommands.LastError()},
               };
             }()},
            {"source_camera_update",
             [&]() {
               const auto stats = sourceCameraUpdate.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCameraUpdate},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_camera_function_calls",
                    stats.DynamicCameraFunctionCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"square_root_operations",
                    stats.SquareRootOperations},
                   {"failures", stats.Failures},
                   {"last_error", sourceCameraUpdate.LastError()},
               };
             }()},
            {"source_player_update",
             [&]() {
               const auto stats = sourcePlayerUpdate.Stats();
               return nlohmann::json{
                   {"enabled", enableSourcePlayerUpdate},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"player_update_common_calls",
                    stats.PlayerUpdateCommonCalls},
                   {"actor_spawn_calls", stats.ActorSpawnCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"vfp_operations", stats.VfpOperations},
                   {"float_conversions", stats.FloatConversions},
                   {"failures", stats.Failures},
                   {"last_error", sourcePlayerUpdate.LastError()},
               };
             }()},
            {"source_player_update_common",
             [&]() {
               const auto stats = sourcePlayerUpdateCommon.Stats();
               return nlohmann::json{
                   {"enabled", enableSourcePlayerUpdateCommon},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_player_action_calls",
                    stats.DynamicPlayerActionCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"vfp_operations", stats.VfpOperations},
                   {"float_conversions", stats.FloatConversions},
                   {"square_root_operations",
                    stats.SquareRootOperations},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourcePlayerUpdateCommon.LastError()},
               };
             }()},
            {"source_csab_curves",
             [&]() {
               const auto stats = sourceCsabCurve.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCsabCurves},
                   {"s16_calls", stats.S16Calls},
                   {"f32_calls", stats.F32Calls},
                   {"guest_bytes_read", stats.GuestBytesRead},
                   {"fallbacks", stats.Fallbacks},
                   {"fpscr_exception_updates",
                    stats.FpscrExceptionUpdates},
                   {"last_error", sourceCsabCurve.LastError()},
               };
             }()},
#endif
            {"a32_runtime_profile",
             [&]() {
               const auto timing = process.TimingStats();
               const auto ctrTiming = hostServices.RuntimeProfile();
               const auto picaSubmissionTiming =
                   submissionQueue.RuntimeProfile();
               const auto picaBackend = api.GetNativePicaBackendStats();
               constexpr double nanosecondsToSeconds = 1.0e-9;
               const uint64_t candidateSampleNanoseconds =
                   nativeCandidateDispatch
                       .SourceActorInitContextSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceActorUpdateAllSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCutsceneUpdateFrameSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCutsceneProcessCommandsSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCameraUpdateSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourcePlayerUpdateSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourcePlayerUpdateCommonSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCsabCurveSampleNanoseconds +
                   nativeCandidateDispatch.TypedGameplaySampleNanoseconds +
                   nativeCandidateDispatch.CompiledSampleNanoseconds +
                   nativeCandidateDispatch.TrueAotSampleNanoseconds +
                   nativeCandidateDispatch.UnhandledSampleNanoseconds;
               nlohmann::json svcByImmediate = nlohmann::json::array();
               for (size_t immediate = 0;
                    immediate < timing.SvcCallsByImmediate.size();
                    ++immediate) {
                 if (timing.SvcCallsByImmediate[immediate] == 0U) {
                   continue;
                 }
                 svcByImmediate.push_back({
                     {"immediate", immediate},
                     {"calls", timing.SvcCallsByImmediate[immediate]},
                     {"seconds", timing.SvcNanosecondsByImmediate[immediate] *
                                     nanosecondsToSeconds},
                 });
               }
               const auto &ipcNameCounts = hostServices.IpcNameCounts();
               nlohmann::json ipcByName = nlohmann::json::array();
               for (const auto &[name, calls] : ipcNameCounts) {
                 ipcByName.push_back({
                     {"name", name},
                     {"calls", calls},
                 });
               }
               const uint64_t measuredTriggerNanoseconds =
                   ctrTiming.CommandListReadNanoseconds +
                   ctrTiming.PicaFrontendSubmitNanoseconds;
               const uint64_t triggerPostNanoseconds =
                   ctrTiming.TriggerCommandQueueNanoseconds >
                           measuredTriggerNanoseconds
                       ? ctrTiming.TriggerCommandQueueNanoseconds -
                             measuredTriggerNanoseconds
                       : 0U;
               return nlohmann::json{
                   {"enabled", launch.ProfileA32Runtime},
                   {"sample_rate_denominator", kA32RuntimeSampleDenominator},
                   {"process_run_calls", timing.ProcessRunCalls},
                   {"dispatch_calls", timing.DispatchCalls},
                   {"dispatch_seconds",
                    timing.DispatchNanoseconds * nanosecondsToSeconds},
                   {"svc_calls", timing.SvcCalls},
                   {"svc_seconds",
                    timing.SvcNanoseconds * nanosecondsToSeconds},
                   {"svc_event_count", hostServices.SvcEventCount()},
                   {"svc_by_immediate", std::move(svcByImmediate)},
                   {"ipc_by_name", std::move(ipcByName)},
                   {"romfs_stream_open_calls",
                    ctrTiming.RomFsStreamOpenCalls},
                   {"romfs_read_calls", ctrTiming.RomFsReadCalls},
                   {"romfs_read_bytes", ctrTiming.RomFsReadBytes},
                   {"romfs_read_seconds",
                    ctrTiming.RomFsReadNanoseconds * nanosecondsToSeconds},
                   {"gsp_trigger_command_queue_calls",
                    ctrTiming.TriggerCommandQueueCalls},
                   {"gsp_trigger_command_queue_seconds",
                    ctrTiming.TriggerCommandQueueNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_command_packets", ctrTiming.CommandPackets},
                   {"gsp_command_list_bytes", ctrTiming.CommandListBytes},
                   {"gsp_command_list_read_seconds",
                    ctrTiming.CommandListReadNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_pica_frontend_submit_seconds",
                    ctrTiming.PicaFrontendSubmitNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_trigger_post_seconds",
                    triggerPostNanoseconds * nanosecondsToSeconds},
                   {"pica_submission_draw_calls",
                    picaSubmissionTiming.DrawCalls},
                   {"pica_submission_total_seconds",
                    picaSubmissionTiming.TotalNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_packet_copy_seconds",
                    picaSubmissionTiming.PacketCopyNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_decode_seconds",
                    picaSubmissionTiming.DecodeNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_vertex_capture_seconds",
                    picaSubmissionTiming.VertexCaptureNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_texture_capture_seconds",
                    picaSubmissionTiming.TextureCaptureNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_enqueue_seconds",
                    picaSubmissionTiming.EnqueueNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_index_bytes",
                    picaSubmissionTiming.IndexBytes},
                   {"pica_submission_vertex_bytes",
                    picaSubmissionTiming.VertexBytes},
                   {"pica_submission_texture_bytes",
                    picaSubmissionTiming.TextureBytes},
                   {"pica_submission_texture_snapshot_cache_hits",
                    picaSubmissionTiming.TextureSnapshotCacheHits},
                   {"pica_submission_texture_snapshot_cache_misses",
                    picaSubmissionTiming.TextureSnapshotCacheMisses},
                   {"pica_submission_texture_snapshot_compared_bytes",
                    picaSubmissionTiming.TextureSnapshotComparedBytes},
                   {"pica_submission_texture_snapshot_copied_bytes",
                    picaSubmissionTiming.TextureSnapshotCopiedBytes},
                   {"pica_submission_geometry_snapshot_cache_hits",
                    picaSubmissionTiming.GeometrySnapshotCacheHits},
                   {"pica_submission_geometry_snapshot_cache_misses",
                    picaSubmissionTiming.GeometrySnapshotCacheMisses},
                   {"pica_submission_geometry_snapshot_version_hits",
                    picaSubmissionTiming.GeometrySnapshotVersionHits},
                   {"pica_submission_geometry_snapshot_version_misses",
                    picaSubmissionTiming.GeometrySnapshotVersionMisses},
                   {"pica_submission_geometry_snapshot_compared_bytes",
                    picaSubmissionTiming.GeometrySnapshotComparedBytes},
                   {"pica_submission_geometry_snapshot_copied_bytes",
                    picaSubmissionTiming.GeometrySnapshotCopiedBytes},
                   {"pica_backend_geometry_registry_hits",
                    picaBackend.GeometryRegistryHits},
                   {"pica_backend_stats_available",
                    picaBackend.Available},
                   {"pica_backend_geometry_cache_enabled",
                    picaBackend.GeometryCacheEnabled},
                   {"pica_backend_geometry_registry_misses",
                    picaBackend.GeometryRegistryMisses},
                   {"pica_backend_geometry_registry_updates",
                    picaBackend.GeometryRegistryUpdates},
                   {"pica_backend_geometry_registry_evictions",
                    picaBackend.GeometryRegistryEvictions},
                   {"pica_backend_geometry_registry_entries",
                    picaBackend.GeometryRegistryEntries},
                   {"pica_backend_geometry_persistent_draws",
                    picaBackend.GeometryPersistentDraws},
                   {"pica_backend_geometry_streaming_draws",
                    picaBackend.GeometryStreamingDraws},
                   {"pica_backend_geometry_persistent_uploads",
                    picaBackend.GeometryPersistentUploads},
                   {"pica_backend_geometry_persistent_upload_bytes",
                    picaBackend.GeometryPersistentUploadBytes},
                   {"pica_backend_geometry_streaming_uploads",
                    picaBackend.GeometryStreamingUploads},
                   {"pica_backend_geometry_streaming_upload_bytes",
                    picaBackend.GeometryStreamingUploadBytes},
                   {"pica_backend_uniform_uploads",
                    picaBackend.UniformUploads},
                   {"pica_backend_uniform_upload_bytes",
                    picaBackend.UniformUploadBytes},
                   {"fallback_calls", timing.FallbackCalls},
                   {"fallback_seconds",
                    timing.FallbackNanoseconds * nanosecondsToSeconds},
                   {"candidate_calls", nativeCandidateDispatch.Calls},
                   {"topscreen_source_port_calls",
                    nativeCandidateDispatch.TopScreenSourcePortCalls},
                   {"topscreen_title_logo_fade_hold_calls",
                    nativeCandidateDispatch.TopScreenTitleLogoFadeHoldCalls},
                   {"topscreen_lower_composition_skips",
                    nativeCandidateDispatch.TopScreenLowerCompositionSkips},
                   {"topscreen_pause_target_observations",
                    widescreenProjection.TopScreenPauseTargetObservations},
                   {"topscreen_pause_target_commands",
                    widescreenProjection.TopScreenPauseTargetCommands},
                   {"topscreen_pause_target_rewrites",
                    widescreenProjection.TopScreenPauseTargetRewrites},
                   {"topscreen_pause_start_close_calls",
                    nativeCandidateDispatch.TopScreenPauseStartCloseCalls},
                   {"topscreen_pause_system_open_calls",
                    nativeCandidateDispatch.TopScreenPauseSystemOpenCalls},
                   {"topscreen_aim_projectile_cycle_attempts",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleAttempts},
                   {"topscreen_aim_projectile_cycle_updates",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleUpdates},
                   {"topscreen_aim_projectile_cycle_sound_calls",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleSoundCalls},
                   {"topscreen_gameplay_dpad_attempts",
                    nativeCandidateDispatch.TopScreenGameplayDpadAttempts},
                   {"topscreen_navi_view_activations",
                    nativeCandidateDispatch.TopScreenNaviViewActivations},
                   {"topscreen_ocarina_queries",
                    nativeCandidateDispatch.TopScreenOcarinaQueries},
                   {"topscreen_ocarina_activations",
                    nativeCandidateDispatch.TopScreenOcarinaActivations},
                   {"topscreen_pause_last_closed_page",
                    static_cast<uint32_t>(
                        nativeCandidateDispatch.TopScreenPauseLastClosedPage)},
                   {"topscreen_pause_system_source_page",
                    static_cast<uint32_t>(nativeCandidateDispatch
                                              .TopScreenPauseSystemSourcePage)},
                   {"topscreen_pause_child_suppressions",
                    widescreenProjection.TopScreenPauseChildSuppressions},
                   {"topscreen_pause_child_native_draws",
                    widescreenProjection.TopScreenPauseChildNativeDraws},
                   {"topscreen_pause_page_composition_calls",
                    widescreenProjection.TopScreenPausePageCompositionCalls},
                   {"topscreen_pause_page_redraws",
                    widescreenProjection.TopScreenPausePageRedraws},
                   {"topscreen_pause_page_redraw_skips",
                    widescreenProjection.TopScreenPausePageRedrawSkips},
                   {"topscreen_pause_last_route_active",
                    widescreenProjection.LastTopScreenPauseRouteActive},
                   {"topscreen_pause_last_children_suppressed",
                    widescreenProjection
                        .LastTopScreenPauseChildrenSuppressed},
                   {"topscreen_pause_last_inputs",
                    widescreenProjection.HasTopScreenPauseDrawInputs
                        ? nlohmann::json{
                              {"has_scene",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.HasScene},
                              {"scene_mode",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneMode},
                              {"scene_variant",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneVariant},
                              {"scene_sequence",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneSequence},
                              {"runtime_mode",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.RuntimeMode},
                              {"native_transition_active",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .NativeTransitionActive},
                              {"alternate_path_ready",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AlternatePathReady},
                              {"alternate_path_visible",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AlternatePathVisible},
                              {"suppression_override",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .SuppressionOverride},
                              {"any_page_gate_active",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AnyPageGateActive},
                              {"gear_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[0]},
                              {"items_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[1]},
                              {"dungeon_map_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[2]},
                              {"system_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[3]},
                              {"selector_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                    .ChildStates[4]},
                              {"touch_button_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[5]},
                          }
                        : nlohmann::json(nullptr)},
                   {"topscreen_pause_route_state",
                    {{"previous_runtime_mode",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .PreviousRuntimeMode},
                     {"transition_phase",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .TransitionPhase},
                     {"remaining_calls",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .RemainingCalls}}},
                   {"topscreen_temporal_state",
                    {{"viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenViewportDrawPhase},
                     {"scene_viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenSceneViewportDrawPhase},
                     {"overlay_viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenOverlayViewportDrawPhase},
                     {"touch_coordinate_update_phase",
                      nativeCandidateDispatch
                          .TopScreenTouchCoordinateUpdatePhase},
                     {"alternate_renderer_restore",
                      nativeCandidateDispatch.TopScreenAlternateRendererRestore
                          .has_value()},
                     {"aot_route_previous_runtime_mode",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .PreviousRuntimeMode},
                     {"aot_route_transition_phase",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .TransitionPhase},
                     {"aot_route_remaining_calls",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .RemainingCalls},
                     {"touch_runtime_scene_latch",
                      nativeCandidateDispatch.TopScreenTouchCoordinateRoute
                          .RuntimeSceneLatch},
                     {"renderer_visibility_fade_step",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .FadeStep},
                     {"renderer_visibility_delay_calls",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .DelayCalls},
                     {"renderer_visibility_conflict",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .RendererConflict},
                     {"pause_controller_routed_latch",
                      nativeCandidateDispatch.TopScreenPauseController
                          .RoutedRendererLatched},
                     {"pause_controller_draw_phase",
                      nativeCandidateDispatch
                          .TopScreenPauseControllerDrawPhase},
                     {"pending_items_selection",
                      nativeCandidateDispatch.TopScreenItems.PendingSelection},
                     {"pending_target",
                      widescreenProjection.PendingTopScreenPauseTarget
                          .has_value()},
                     {"pending_target_renderer",
                      widescreenProjection.PendingTopScreenPauseRenderer},
                     {"draw_native_transition_latched",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .NativeTransitionLatched},
                     {"draw_suppression_delay_armed",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .SuppressionDelayArmed},
                     {"draw_suppression_delay_commands",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .SuppressionDelayCommands},
                     {"page_redraw_delay_calls",
                      widescreenProjection.TopScreenPausePageRedraw.DelayCalls},
                     {"page_redraw_active",
                      widescreenProjection.TopScreenPausePageRedrawActive},
                     {"pending_child_restore",
                      widescreenProjection.PendingTopScreenPauseChildRestore
                          .has_value()}}},
                   {"topscreen_camera_normal1_calls",
                    nativeCandidateDispatch.TopScreenCameraNormal1Calls},
                   {"topscreen_camera_update_calls",
                    nativeCandidateDispatch.TopScreenCameraUpdateCalls},
                   {"topscreen_camera_active_calls",
                    nativeCandidateDispatch.TopScreenCameraActiveCalls},
                   {"topscreen_camera_enabled",
                    nativeCandidateDispatch.TopScreenCamera.Camera.Enabled},
                   {"topscreen_camera_n64_style_zoom",
                    nativeCandidateDispatch.TopScreenCamera.Camera
                        .N64StyleZoom},
                   {"topscreen_camera_last_blockers",
                    nativeCandidateDispatch.TopScreenCamera
                        .LastOwnershipBlockers},
                   {"topscreen_camera_last_ownership",
                    {{"game_mode",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.GameMode},
                     {"entrance_index",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.EntranceIndex},
                     {"cutscene_index",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CutsceneIndex},
                     {"scene",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.Scene},
                     {"is_main_camera",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.IsMainCamera},
                     {"has_player",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.HasPlayer},
                     {"player_state_flags_1",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.PlayerStateFlags1},
                     {"player_state_flags_2",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.PlayerStateFlags2},
                     {"status",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CameraStatus},
                     {"setting",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CameraSetting},
                     {"right_stick_x",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.RightStickX},
                     {"right_stick_y",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.RightStickY}}},
                   {"topscreen_item_query_calls",
                    nativeCandidateDispatch.TopScreenItems.QueryCalls},
                   {"topscreen_item_query_trace", [&] {
                      auto rows = nlohmann::json::array();
                      for (const auto& row : nativeCandidateDispatch.TopScreenItems.QueryTrace)
                        rows.push_back({{"entry", row.Entry}, {"return_pc", row.ReturnPc},
                            {"flags", row.SuppressionFlags}, {"native", row.NativeResult},
                            {"result", row.Result}, {"zr_pressed", row.Input.ZrPressed},
                            {"zl_pressed", row.Input.ZlPressed}, {"zr_held", row.Input.ZrHeld},
                            {"zl_held", row.Input.ZlHeld}});
                      return rows;
                    }()},
                   {"topscreen_input_native_updates",
                    nativeCandidateDispatch.TopScreenInputClock.Updates},
                   {"topscreen_input_zr_press_updates",
                    nativeCandidateDispatch.TopScreenInputClock.ZrPressUpdates},
                   {"topscreen_input_zl_press_updates",
                    nativeCandidateDispatch.TopScreenInputClock.ZlPressUpdates},
                   {"topscreen_items_selection_updates",
                    nativeCandidateDispatch.TopScreenItems.SelectionUpdates},
                   {"topscreen_item_query_true_results",
                    nativeCandidateDispatch.TopScreenItems.QueryTrueResults},
                   {"topscreen_slot_item_attempts",
                    nativeCandidateDispatch.TopScreenItems.SlotAttempts},
                   {"topscreen_slot_item_overrides",
                    nativeCandidateDispatch.TopScreenItems.SlotOverrides},
                   {"topscreen_slot_item_native_fallbacks",
                    nativeCandidateDispatch.TopScreenItems.SlotNativeFallbacks},
                   {"topscreen_item_query_entries",
                    nativeCandidateDispatch.TopScreenItems.QueryEntries},
                   {"topscreen_slot_item_entries",
                    nativeCandidateDispatch.TopScreenItems.SlotEntries},
                   {"topscreen_items_selection_begins",
                    nativeCandidateDispatch.TopScreenItems.SelectionBegins},
                   {"topscreen_items_selection_completions",
                    nativeCandidateDispatch.TopScreenItems.SelectionCompletions},
                   {"candidate_samples", nativeCandidateDispatch.Samples},
                   {"candidate_estimated_seconds",
                    candidateSampleNanoseconds * kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"source_actor_init_context_samples",
                    nativeCandidateDispatch.SourceActorInitContextSamples},
                   {"source_actor_init_context_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceActorInitContextSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_actor_update_all_samples",
                    nativeCandidateDispatch.SourceActorUpdateAllSamples},
                   {"source_actor_update_all_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceActorUpdateAllSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_cutscene_update_frame_samples",
                    nativeCandidateDispatch
                        .SourceCutsceneUpdateFrameSamples},
                   {"source_cutscene_update_frame_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCutsceneUpdateFrameSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_cutscene_process_commands_samples",
                    nativeCandidateDispatch
                        .SourceCutsceneProcessCommandsSamples},
                   {"source_cutscene_process_commands_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCutsceneProcessCommandsSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_camera_update_samples",
                    nativeCandidateDispatch.SourceCameraUpdateSamples},
                   {"source_camera_update_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCameraUpdateSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_player_update_samples",
                    nativeCandidateDispatch.SourcePlayerUpdateSamples},
                   {"source_player_update_estimated_seconds",
                    nativeCandidateDispatch
                            .SourcePlayerUpdateSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_player_update_common_samples",
                    nativeCandidateDispatch
                        .SourcePlayerUpdateCommonSamples},
                   {"source_player_update_common_estimated_seconds",
                    nativeCandidateDispatch
                            .SourcePlayerUpdateCommonSampleNanoseconds *
                        kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"source_csab_curve_samples",
                    nativeCandidateDispatch.SourceCsabCurveSamples},
                   {"source_csab_curve_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCsabCurveSampleNanoseconds *
                        kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"typed_gameplay_samples",
                    nativeCandidateDispatch.TypedGameplaySamples},
                   {"typed_gameplay_estimated_seconds",
                    nativeCandidateDispatch.TypedGameplaySampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"compiled_samples",
                    nativeCandidateDispatch.CompiledSamples},
                   {"compiled_estimated_seconds",
                    nativeCandidateDispatch.CompiledSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"true_aot_samples", nativeCandidateDispatch.TrueAotSamples},
                   {"true_aot_estimated_seconds",
                    nativeCandidateDispatch.TrueAotSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"unhandled_samples",
                    nativeCandidateDispatch.UnhandledSamples},
                   {"unhandled_estimated_seconds",
                    nativeCandidateDispatch.UnhandledSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
               };
             }()},
            {"phase_timing",
             {{"slow_frames", [&]() {
                 constexpr const char* names[]{"guest", "frame_start", "host_frame_start", "input_poll",
                   "renderer_frame_start", "dsp_mix", "audio_output", "pica_submit", "pica_plan",
                   "pica_backend", "pica_diagnostics", "visual_presentation", "visual_replay", "present", "pacing_wait"};
                 auto samples = nlohmann::json::array();
                 for (const auto& sample : slowFrames.Samples()) {
                   nlohmann::json phases;
                   for (size_t i = 0; i < std::size(names); ++i) phases[names[i]] = sample.PhaseMilliseconds[i];
                   samples.push_back({{"frame", sample.Frame}, {"total_ms", sample.Milliseconds}, {"phases_ms", phases}});
                 }
                 return samples;
               }()},
              {"slow_frame_note", "Nested phases overlap; do not sum parent and child timers. Measured benchmark frames only."},
              {"guest_seconds", phaseTiming.GuestSeconds},
              {"frame_start_seconds", phaseTiming.FrameStartSeconds},
              {"host_frame_start_seconds", phaseTiming.HostFrameStartSeconds},
              {"input_poll_seconds", phaseTiming.InputPollSeconds},
              {"renderer_frame_start_seconds", phaseTiming.RendererFrameStartSeconds},
              {"dsp_mix_seconds", phaseTiming.DspMixSeconds},
              {"audio_output_seconds", phaseTiming.AudioOutputSeconds},
              {"pica_submit_seconds", phaseTiming.PicaSubmitSeconds},
              {"pica_plan_seconds", phaseTiming.PicaPlanSeconds},
              {"pica_backend_seconds", phaseTiming.PicaBackendSeconds},
              {"pica_diagnostics_seconds", phaseTiming.PicaDiagnosticsSeconds},
              {"visual_presentation_seconds",
               phaseTiming.VisualPresentationSeconds},
              {"visual_replay_seconds", phaseTiming.VisualReplaySeconds},
              {"present_seconds", phaseTiming.PresentSeconds},
              {"pacing_wait_seconds", phaseTiming.PacingWaitSeconds}}},
            {"shader_source_cache",
             {{"vertex_entries", shaderSourceCache.VertexSources.size()},
              {"vertex_structural_entries",
               shaderSourceCache.VertexStructuralStates.size()},
              {"vertex_hits", shaderSourceCache.VertexHits},
              {"vertex_misses", shaderSourceCache.VertexMisses},
              {"fragment_entries", shaderSourceCache.FragmentSources.size()},
              {"fragment_hits", shaderSourceCache.FragmentHits},
              {"fragment_misses", shaderSourceCache.FragmentMisses}}},
            {"authority", "oot3d_a32_process_pica_vulkan"},
        });
  }

  if (launch.ScenarioStrict &&
      (!scenarioBootstrap.has_value() || !scenarioBootstrap->Complete())) {
    const std::string detail =
        scenarioBootstrap.has_value()
            ? scenarioBootstrap->Stats().Error
            : "structural scenario bootstrap is unavailable";
    throw std::runtime_error("structural scenario did not complete capture" +
                             (detail.empty() ? std::string() : ": " + detail));
  }
}
