#pragma once
// MM3D_HITCH_LOG: frame-hitch attribution. Off unless the variable is set.
//
// Code that may stall a frame wraps the work in a Hitch::Scope (or calls
// Hitch::Add/Count). The counters are fixed process-wide atomics: no heap
// allocation, locks or I/O on the hot path. Once per host frame the host calls
// Hitch::EndHostFrame (hitch_log.cpp), which takes the per-frame deltas, keeps
// every frame time and queues a record for each frame over the threshold; a
// background thread formats and writes them. With the variable unset, every
// hook is one predictable branch and nothing else runs.
//
//   MM3D_HITCH_LOG=1            enable, write to MM3D_HITCH_LOG_FILE or stderr
//   MM3D_HITCH_LOG=<path>       enable, write to <path>
//   MM3D_HITCH_MS=<ms>          hitch threshold (default 25)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Fast::Renderer::Hitch {

// Keep in sync with kEventNames in hitch_log.cpp. "Nested" events also count
// inside an enclosing one; the report says which (they are not summed twice).
enum class Event : uint8_t {
    HdTextureWait,         // StartFrame waits for queued custom textures
    SpirvResolve,          // SPIR-V cache resolve on the render thread (incl. compile)
    ShaderCompile,         // nested in SpirvResolve: shaderc on the render thread
    ShaderCompileWorker,   // off-thread: async shader workers
    AotShaderMiss,         // count: AOT shader pack lookups that missed
    FragmentArtifactMiss,  // count
    AsyncSkippedDraw,      // count: draws skipped while their shader/pipeline builds
    PipelineUncached,      // render-thread pipeline lookup that missed the memo (incl. creation)
    PipelineCreate,        // nested: driver pipeline creation on the render thread
    PipelineCreateWorker,  // off-thread: async pipeline workers
    PipelineAsyncWait,     // nested: render thread waits for a worker-built pipeline
    PipelinePrewarm,       // StartFrame prewarm pass (startup/profile change)
    TextureCreate,         // a new native PICA texture (incl. everything below)
    TextureDecode,         // nested: PICA decode of the mip chain
    TextureHash,           // nested: content hashes of native bytes
    TextureHdResolve,      // nested: Azahar hash + index lookup / queue
    TextureGrassObserve,   // nested: GrassTextureSourceCache::ObserveDecoded
    TextureImage,          // image creation + upload recording (bytes = uploaded)
    TextureAlloc,          // nested in TextureImage: image + memory allocation
    HdTextureImage,        // count/bytes: custom (HD) images created
    UploadBufferCreate,    // NRI host-upload staging buffer created
    SamplerCreate,         // nested in TextureImage: VkSampler (re)creation
    TextureEvict,          // unused textures destroyed (count = textures)
    IdleWait,              // vkDeviceWaitIdle / vkQueueWaitIdle (labelled)
    RawCopySubmit,         // raw PICA copy prefix submitted and waited
    FrameFenceWait,        // StartFrame: frame-slot fence + present waits
    SwapchainAcquire,      // StartFrame: next swapchain image
    GuestFileRead,         // guest FS reads (romfs/save), bytes = requested
    GrassRender,           // interactive grass placement + draw recording
    ShadowRender,          // directional shadow map recording
    PicaDraw,              // one native PICA draw on the render thread (incl. its stages below)
    DrawShaderVariant,     // stages of PicaDraw (the renderer's own per-draw CPU timers)
    DrawShaderCache,
    DrawTexture,
    DrawGrassSurface,
    DrawTarget,
    DrawReflection,
    DrawTemporal,
    DrawPipeline,
    DrawUpload,
    DrawDescriptor,
    DrawCommand,
    Count
};
inline constexpr size_t kEventCount = static_cast<size_t>(Event::Count);

namespace Detail {
inline bool ReadEnabled() noexcept {
    const char* value = std::getenv("MM3D_HITCH_LOG");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}
struct Slot {
    std::atomic<uint64_t> Count{0};
    std::atomic<uint64_t> Nanoseconds{0};
    std::atomic<uint64_t> Bytes{0};
};
inline Slot gSlots[kEventCount];
} // namespace Detail

inline const bool gEnabled = Detail::ReadEnabled();
inline bool Enabled() noexcept { return gEnabled; }

inline uint64_t NowNanoseconds() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void Add(Event event, uint64_t nanoseconds, uint64_t bytes = 0, uint64_t count = 1) noexcept {
    if (!gEnabled) return;
    auto& slot = Detail::gSlots[static_cast<size_t>(event)];
    slot.Count.fetch_add(count, std::memory_order_relaxed);
    slot.Nanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    if (bytes != 0) slot.Bytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline void Count(Event event, uint64_t count = 1, uint64_t bytes = 0) noexcept { Add(event, 0, bytes, count); }

// Events that are not the frame thread's own blocking time: worker threads,
// and guest file reads (already inside the guest's time).
constexpr bool OutsideFrameAccounting(Event event) noexcept {
    return event == Event::ShaderCompileWorker || event == Event::PipelineCreateWorker ||
           event == Event::GuestFileRead;
}
namespace Detail {
// Outermost scopes only, so nested events are not counted twice in the
// frame's attributed time.
inline thread_local int tScopeDepth = 0;
inline std::atomic<uint64_t> gTopLevelNanoseconds{0};
} // namespace Detail

// Add() for a measured interval of the frame's own thread that no Scope encloses
// already counts towards the frame's attributed time.
inline void AddBlocking(Event event, uint64_t nanoseconds, uint64_t bytes = 0, uint64_t count = 1) noexcept {
    if (!gEnabled) return;
    Add(event, nanoseconds, bytes, count);
    if (Detail::tScopeDepth == 0)
        Detail::gTopLevelNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
}

// Times its own lifetime into one event.
class Scope {
  public:
    explicit Scope(Event event, uint64_t bytes = 0) noexcept : mEvent(event), mBytes(bytes) {
        if (!gEnabled) return;
        if (!OutsideFrameAccounting(event)) mOutermost = Detail::tScopeDepth++ == 0;
        mStart = NowNanoseconds();
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() {
        if (!gEnabled) return;
        const uint64_t elapsed = NowNanoseconds() - mStart;
        Add(mEvent, elapsed, mBytes);
        if (OutsideFrameAccounting(mEvent)) return;
        --Detail::tScopeDepth;
        if (mOutermost) Detail::gTopLevelNanoseconds.fetch_add(elapsed, std::memory_order_relaxed);
    }
    void AddBytes(uint64_t bytes) noexcept { mBytes += bytes; }

  private:
    Event mEvent;
    uint64_t mBytes;
    uint64_t mStart = 0;
    bool mOutermost = false;
};

// Idle waits keep their call site (file:line); see hitch_log_vulkan.h.
void RecordWait(const char* file, int line, uint64_t nanoseconds) noexcept;

struct HostFrameTiming {
    uint64_t HostFrame = 0;
    uint64_t GuestFrame = 0;
    double FrameMilliseconds = 0;  // wall time since the previous frame ended
    double GuestMilliseconds = 0;
    double RendererFrameStartMilliseconds = 0;
    double PicaSubmitMilliseconds = 0;
    double VisualPresentationMilliseconds = 0;
    double PresentMilliseconds = 0;  // includes PacingWait
    double PacingWaitMilliseconds = 0;
};
// Host thread, once per completed host frame. No-op when disabled.
void EndHostFrame(const HostFrameTiming& timing) noexcept;
// F9: a numbered marker in the log (to separate passes of a route).
void Mark() noexcept;
// Writes the summary and stops the writer. Safe to call more than once.
void Shutdown() noexcept;

} // namespace Fast::Renderer::Hitch
