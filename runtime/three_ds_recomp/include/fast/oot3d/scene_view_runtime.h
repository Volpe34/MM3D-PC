#pragma once

#include "fast/renderer3ds/pica_scene_payloads.h"

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace Fast::Oot3d {

using PerspectiveViewState =
    ::Fast::Renderer3ds::PicaPerspectiveCameraState;

// Thread-safe hand-off between the guest camera/frustum hook and renderer
// passes. It deliberately contains no Vulkan state.
class SceneViewRuntime final {
  public:
    static SceneViewRuntime& Instance();
    // sourceFrame: the title's source frame whose draws use this view (0: unknown).
    void PublishPerspective(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right, float bottom,
                            float top, float nearPlane, float farPlane, uint64_t sourceFrame = 0);
    void PublishPerspectiveCamera(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right,
                                  float bottom, float top, float nearPlane, float farPlane,
                                  const std::array<float, 3>& eye, const std::array<float, 3>& at,
                                  uint64_t sourceFrame = 0);
    void PublishCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at);
    // The view of the presentation being drawn: its source frame's camera when
    // known (else the newest), with its in-between view adjustment applied.
    [[nodiscard]] std::optional<PerspectiveViewState> LatestPerspective() const;
    // The presentation being drawn: its current source frame and the title's
    // in-between view transform (row-major 3x4, view' = A * view), nullopt for
    // a frame drawn as the game produced it.
    void SetPresentation(uint64_t sourceFrame, const std::optional<std::array<float, 12>>& adjustment);
    void Reset();

  private:
    void Remember(const PerspectiveViewState& view, uint64_t sourceFrame);

    mutable std::mutex mMutex;
    uint64_t mNextSerial = 1;
    std::optional<PerspectiveViewState> mLatest;
    std::deque<std::pair<uint64_t, PerspectiveViewState>> mHistory;
    uint64_t mPresentedSourceFrame = 0;
    std::optional<std::array<float, 12>> mAdjustment;
};

} // namespace Fast::Oot3d
