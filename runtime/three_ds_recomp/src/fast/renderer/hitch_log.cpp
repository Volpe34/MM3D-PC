#include "fast/renderer/hitch_log.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Fast::Renderer::Hitch {
namespace {

constexpr const char* kEventNames[] = {
    "hd_wait",      "spirv",        "shader_compile", "shader_compile_worker", "aot_miss",
    "artifact_miss", "async_skip",  "pipe_uncached",  "pipe_create",           "pipe_create_worker",
    "pipe_async_wait", "pipe_prewarm", "tex_create",            "tex_decode",
    "tex_hash",     "tex_hd_resolve", "tex_grass",    "tex_image",             "tex_alloc",
    "hd_image",     "upload_buffer", "sampler",       "tex_evict",             "idle_wait",
    "raw_copy",     "fence_wait",   "acquire",        "file_read",             "grass",
    "shadow",       "draw",         "d_variant",      "d_shader",              "d_texture",
    "d_grass_surface", "d_target",  "d_reflection",   "d_temporal",            "d_pipeline",
    "d_upload",     "d_descriptor", "d_command",
};
static_assert(std::size(kEventNames) == kEventCount, "kEventNames must match Hitch::Event");

constexpr size_t kWaitSites = 64;
constexpr size_t kRecordCapacity = 1024;
constexpr size_t kFrameTimeCapacity = 8192;
constexpr double kHistogramBucketMs = 0.1;
constexpr size_t kHistogramBuckets = 10000;  // 0..1000 ms; longer frames go to the last bucket

struct WaitSite {
    std::atomic<const char*> File{nullptr};
    std::atomic<int> Line{0};
    std::atomic<uint64_t> Count{0};
    std::atomic<uint64_t> Nanoseconds{0};
};
WaitSite gWaitSites[kWaitSites];
std::atomic<size_t> gWaitSiteCount{0};
std::mutex gWaitSiteMutex;
std::atomic<uint64_t> gFrameLongestWait{0};
std::atomic<size_t> gFrameLongestWaitSite{kWaitSites};

const char* BaseName(const char* path) {
    const char* base = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    return base;
}

struct EventDelta {
    uint64_t Count = 0;
    uint64_t Nanoseconds = 0;
    uint64_t Bytes = 0;
};

struct Record {
    enum class Kind : uint8_t { Hitch, Mark } Type = Kind::Hitch;
    double Seconds = 0;
    HostFrameTiming Timing;
    double OtherMilliseconds = 0;
    std::array<EventDelta, kEventCount> Events{};
    size_t LongestWaitSite = kWaitSites;
    uint64_t LongestWaitNanoseconds = 0;
    uint32_t MarkNumber = 0;
};

struct FrameTime {
    uint64_t HostFrame;
    float Milliseconds;
};

class Log {
  public:
    Log() {
        const char* value = std::getenv("MM3D_HITCH_LOG");
        const char* file = std::getenv("MM3D_HITCH_LOG_FILE");
        std::string path;
        if (value != nullptr && std::strcmp(value, "1") != 0) path = value;
        else if (file != nullptr && file[0] != '\0') path = file;
        mOutput = path.empty() ? stderr : std::fopen(path.c_str(), "w");
        if (mOutput == nullptr) {
            std::fprintf(stderr, "MM3D_HITCH_LOG: cannot open %s, writing to stderr\n", path.c_str());
            mOutput = stderr;
        }
        if (const char* threshold = std::getenv("MM3D_HITCH_MS")) {
            const double parsed = std::strtod(threshold, nullptr);
            if (parsed > 0.0) mThresholdMs = parsed;
        }
        mRecords.resize(kRecordCapacity);
        mWriterRecords.resize(kRecordCapacity);
        mFrameTimes.resize(kFrameTimeCapacity);
        mWriterFrameTimes.resize(kFrameTimeCapacity);
        mHistogram.assign(kHistogramBuckets, 0U);
        const std::time_t now = std::time(nullptr);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
        std::fprintf(mOutput, "# MM3D_HITCH_LOG v1 start=%s threshold_ms=%.1f\n# events", stamp, mThresholdMs);
        for (const char* name : kEventNames) std::fprintf(mOutput, " %s", name);
        std::fprintf(mOutput,
                     "\n# HITCH fields: name=count/ms[/MB]; nested events are also inside their parent "
                     "(see hitch_log.h); other_ms = frame - guest - present - outermost events\n");
        std::fflush(mOutput);
        mStart = NowNanoseconds();
        mWriter = std::thread([this] { WriterMain(); });
    }

    void EndFrame(const HostFrameTiming& timing) {
        std::array<EventDelta, kEventCount> delta{};
        for (size_t index = 0; index < kEventCount; ++index) {
            auto& slot = Detail::gSlots[index];
            const uint64_t count = slot.Count.load(std::memory_order_relaxed);
            const uint64_t nanoseconds = slot.Nanoseconds.load(std::memory_order_relaxed);
            const uint64_t bytes = slot.Bytes.load(std::memory_order_relaxed);
            delta[index] = {count - mPrevious[index].Count, nanoseconds - mPrevious[index].Nanoseconds,
                            bytes - mPrevious[index].Bytes};
            mPrevious[index] = {count, nanoseconds, bytes};
        }
        const uint64_t topLevel = Detail::gTopLevelNanoseconds.load(std::memory_order_relaxed);
        const double topLevelMs = static_cast<double>(topLevel - mPreviousTopLevel) / 1e6;
        mPreviousTopLevel = topLevel;
        const uint64_t longestWait = gFrameLongestWait.exchange(0, std::memory_order_relaxed);
        const size_t longestSite = gFrameLongestWaitSite.exchange(kWaitSites, std::memory_order_relaxed);

        const double frameMs = timing.FrameMilliseconds;
        ++mFrames;
        mMaxMs = std::max(mMaxMs, frameMs);
        mTotalMs += frameMs;
        const size_t bucket = std::min(kHistogramBuckets - 1,
                                       static_cast<size_t>(std::max(0.0, frameMs) / kHistogramBucketMs));
        ++mHistogram[bucket];
        for (size_t index = 0; index < std::size(kHitchBounds); ++index)
            if (frameMs > kHitchBounds[index]) ++mOver[index];
        for (size_t index = 0; index < kEventCount; ++index) {
            mTotals[index].Count += delta[index].Count;
            mTotals[index].Nanoseconds += delta[index].Nanoseconds;
            mTotals[index].Bytes += delta[index].Bytes;
        }
        const bool hitch = frameMs >= mThresholdMs;
        if (hitch) {
            ++mHitches;
            mHitchMs += frameMs;
            for (size_t index = 0; index < kEventCount; ++index) {
                mHitchTotals[index].Count += delta[index].Count;
                mHitchTotals[index].Nanoseconds += delta[index].Nanoseconds;
            }
        }
        const double seconds = static_cast<double>(NowNanoseconds() - mStart) / 1e9;
        std::lock_guard lock(mMutex);
        if (mFrameTimeCount < kFrameTimeCapacity)
            mFrameTimes[mFrameTimeCount++] = {timing.HostFrame, static_cast<float>(frameMs)};
        else
            ++mDroppedFrameTimes;
        if (hitch) {
            if (mRecordCount < kRecordCapacity) {
                auto& record = mRecords[mRecordCount++];
                record.Type = Record::Kind::Hitch;
                record.Seconds = seconds;
                record.Timing = timing;
                record.OtherMilliseconds = std::max(
                    0.0, frameMs - timing.GuestMilliseconds - timing.PresentMilliseconds - topLevelMs);
                record.Events = delta;
                record.LongestWaitSite = longestSite;
                record.LongestWaitNanoseconds = longestWait;
            } else {
                ++mDroppedRecords;
            }
            mWake.notify_one();
        }
    }

    void MarkFrame() {
        std::lock_guard lock(mMutex);
        if (mRecordCount >= kRecordCapacity) {
            ++mDroppedRecords;
            return;
        }
        auto& record = mRecords[mRecordCount++];
        record = {};
        record.Type = Record::Kind::Mark;
        record.Seconds = static_cast<double>(NowNanoseconds() - mStart) / 1e9;
        record.MarkNumber = ++mMarks;
        record.Timing.HostFrame = mFrames;
        mWake.notify_one();
    }

    void Stop() {
        {
            std::lock_guard lock(mMutex);
            if (mStopped) return;
            mStopped = true;
        }
        mWake.notify_one();
        if (mWriter.joinable()) mWriter.join();
        WriteSummary();
        std::fflush(mOutput);
        if (mOutput != stderr) std::fclose(mOutput);
        mOutput = nullptr;
    }

  private:
    static constexpr double kHitchBounds[] = {25.0, 50.0, 100.0, 250.0, 1000.0};

    void WriterMain() {
        for (;;) {
            size_t records = 0;
            size_t frameTimes = 0;
            bool stop = false;
            {
                std::unique_lock lock(mMutex);
                mWake.wait_for(lock, std::chrono::milliseconds(500),
                               [&] { return mStopped || mRecordCount >= kRecordCapacity / 2; });
                std::swap(mRecords, mWriterRecords);
                std::swap(mFrameTimes, mWriterFrameTimes);
                records = mRecordCount;
                frameTimes = mFrameTimeCount;
                mRecordCount = 0;
                mFrameTimeCount = 0;
                stop = mStopped;
            }
            WriteFrameTimes(frameTimes);
            for (size_t index = 0; index < records; ++index) WriteRecord(mWriterRecords[index]);
            std::fflush(mOutput);
            if (stop) return;
        }
    }

    void WriteFrameTimes(size_t count) {
        // FT <first host frame> <ms> <ms> ...: every frame, for exact percentiles.
        for (size_t start = 0; start < count; start += 64) {
            const size_t end = std::min(count, start + 64);
            std::fprintf(mOutput, "FT %llu", static_cast<unsigned long long>(mWriterFrameTimes[start].HostFrame));
            for (size_t index = start; index < end; ++index)
                std::fprintf(mOutput, " %.2f", mWriterFrameTimes[index].Milliseconds);
            std::fputc('\n', mOutput);
        }
    }

    void WriteRecord(const Record& record) {
        if (record.Type == Record::Kind::Mark) {
            std::fprintf(mOutput, "MARK n=%u t=%.3f host=%llu\n", record.MarkNumber, record.Seconds,
                         static_cast<unsigned long long>(record.Timing.HostFrame));
            return;
        }
        const auto& timing = record.Timing;
        std::fprintf(mOutput,
                     "HITCH t=%.3f host=%llu guest=%llu ms=%.2f guest_ms=%.2f start_ms=%.2f submit_ms=%.2f "
                     "visual_ms=%.2f present_ms=%.2f pacing_ms=%.2f other_ms=%.2f",
                     record.Seconds, static_cast<unsigned long long>(timing.HostFrame),
                     static_cast<unsigned long long>(timing.GuestFrame), timing.FrameMilliseconds,
                     timing.GuestMilliseconds, timing.RendererFrameStartMilliseconds,
                     timing.PicaSubmitMilliseconds, timing.VisualPresentationMilliseconds,
                     timing.PresentMilliseconds, timing.PacingWaitMilliseconds, record.OtherMilliseconds);
        for (size_t index = 0; index < kEventCount; ++index) {
            const auto& event = record.Events[index];
            if (event.Count == 0 && event.Nanoseconds == 0) continue;
            std::fprintf(mOutput, " %s=%llu/%.2f", kEventNames[index],
                         static_cast<unsigned long long>(event.Count),
                         static_cast<double>(event.Nanoseconds) / 1e6);
            if (event.Bytes != 0) std::fprintf(mOutput, "/%.2fMB", static_cast<double>(event.Bytes) / 1048576.0);
        }
        if (record.LongestWaitSite < gWaitSiteCount.load(std::memory_order_acquire)) {
            const auto& site = gWaitSites[record.LongestWaitSite];
            std::fprintf(mOutput, " wait_site=%s:%d/%.2f", BaseName(site.File.load()), site.Line.load(),
                         static_cast<double>(record.LongestWaitNanoseconds) / 1e6);
        }
        std::fputc('\n', mOutput);
    }

    double Percentile(double fraction) const {
        if (mFrames == 0) return 0.0;
        const uint64_t target = static_cast<uint64_t>(fraction * static_cast<double>(mFrames - 1)) + 1;
        uint64_t seen = 0;
        for (size_t bucket = 0; bucket < kHistogramBuckets; ++bucket) {
            seen += mHistogram[bucket];
            if (seen >= target)
                return bucket + 1 == kHistogramBuckets ? mMaxMs
                                                        : (static_cast<double>(bucket) + 1.0) * kHistogramBucketMs;
        }
        return mMaxMs;
    }

    void WriteSummary() {
        if (mOutput == nullptr) return;
        std::fprintf(mOutput,
                     "SUMMARY frames=%llu seconds=%.1f threshold_ms=%.1f hitches=%llu hitch_ms=%.1f over25=%llu "
                     "over50=%llu over100=%llu over250=%llu over1000=%llu p50=%.1f p95=%.1f p99=%.1f p999=%.1f "
                     "max=%.2f dropped_records=%llu dropped_frame_times=%llu marks=%u\n",
                     static_cast<unsigned long long>(mFrames), mTotalMs / 1000.0, mThresholdMs,
                     static_cast<unsigned long long>(mHitches), mHitchMs,
                     static_cast<unsigned long long>(mOver[0]), static_cast<unsigned long long>(mOver[1]),
                     static_cast<unsigned long long>(mOver[2]), static_cast<unsigned long long>(mOver[3]),
                     static_cast<unsigned long long>(mOver[4]), Percentile(0.50), Percentile(0.95),
                     Percentile(0.99), Percentile(0.999), mMaxMs,
                     static_cast<unsigned long long>(mDroppedRecords),
                     static_cast<unsigned long long>(mDroppedFrameTimes), mMarks);
        for (size_t index = 0; index < kEventCount; ++index) {
            const auto& total = mTotals[index];
            if (total.Count == 0 && total.Nanoseconds == 0) continue;
            std::fprintf(mOutput, "EVENT %s count=%llu ms=%.2f hitch_count=%llu hitch_ms=%.2f mb=%.2f\n",
                         kEventNames[index], static_cast<unsigned long long>(total.Count),
                         static_cast<double>(total.Nanoseconds) / 1e6,
                         static_cast<unsigned long long>(mHitchTotals[index].Count),
                         static_cast<double>(mHitchTotals[index].Nanoseconds) / 1e6,
                         static_cast<double>(total.Bytes) / 1048576.0);
        }
        const size_t sites = gWaitSiteCount.load(std::memory_order_acquire);
        for (size_t index = 0; index < sites; ++index) {
            const auto& site = gWaitSites[index];
            std::fprintf(mOutput, "WAIT %s:%d count=%llu ms=%.2f\n", BaseName(site.File.load()), site.Line.load(),
                         static_cast<unsigned long long>(site.Count.load()),
                         static_cast<double>(site.Nanoseconds.load()) / 1e6);
        }
    }

    std::FILE* mOutput = nullptr;
    double mThresholdMs = 25.0;
    uint64_t mStart = 0;
    std::thread mWriter;
    std::mutex mMutex;
    std::condition_variable mWake;
    bool mStopped = false;
    std::vector<Record> mRecords, mWriterRecords;
    size_t mRecordCount = 0;
    std::vector<FrameTime> mFrameTimes, mWriterFrameTimes;
    size_t mFrameTimeCount = 0;
    uint64_t mDroppedRecords = 0;
    uint64_t mDroppedFrameTimes = 0;
    uint32_t mMarks = 0;
    // Host thread only.
    std::array<EventDelta, kEventCount> mPrevious{};
    uint64_t mPreviousTopLevel = 0;
    std::array<EventDelta, kEventCount> mTotals{};
    std::array<EventDelta, kEventCount> mHitchTotals{};
    std::vector<uint32_t> mHistogram;
    uint64_t mFrames = 0;
    uint64_t mHitches = 0;
    double mHitchMs = 0;
    double mTotalMs = 0;
    double mMaxMs = 0;
    std::array<uint64_t, std::size(kHitchBounds)> mOver{};
};

std::mutex gLogMutex;
Log* gLog = nullptr;  // never destroyed: Shutdown() stops it, exit order does not matter
bool gShutDown = false;

Log* Instance() {
    if (!gEnabled) return nullptr;
    std::lock_guard lock(gLogMutex);
    if (gShutDown) return nullptr;
    if (gLog == nullptr) {
        gLog = new Log();
        std::atexit([] { Shutdown(); });
    }
    return gLog;
}

} // namespace

void RecordWait(const char* file, int line, uint64_t nanoseconds) noexcept {
    if (!gEnabled) return;
    Add(Event::IdleWait, nanoseconds);
    if (Detail::tScopeDepth == 0) Detail::gTopLevelNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    size_t index = kWaitSites;
    const size_t known = gWaitSiteCount.load(std::memory_order_acquire);
    for (size_t candidate = 0; candidate < known; ++candidate) {
        if (gWaitSites[candidate].File.load(std::memory_order_relaxed) == file &&
            gWaitSites[candidate].Line.load(std::memory_order_relaxed) == line) {
            index = candidate;
            break;
        }
    }
    if (index == kWaitSites) {
        std::lock_guard lock(gWaitSiteMutex);
        const size_t count = gWaitSiteCount.load(std::memory_order_relaxed);
        for (size_t candidate = 0; candidate < count; ++candidate)
            if (gWaitSites[candidate].File.load() == file && gWaitSites[candidate].Line.load() == line)
                index = candidate;
        if (index == kWaitSites && count < kWaitSites) {
            gWaitSites[count].File.store(file);
            gWaitSites[count].Line.store(line);
            gWaitSiteCount.store(count + 1, std::memory_order_release);
            index = count;
        }
    }
    if (index == kWaitSites) return;
    gWaitSites[index].Count.fetch_add(1, std::memory_order_relaxed);
    gWaitSites[index].Nanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    if (nanoseconds > gFrameLongestWait.load(std::memory_order_relaxed)) {
        gFrameLongestWait.store(nanoseconds, std::memory_order_relaxed);
        gFrameLongestWaitSite.store(index, std::memory_order_relaxed);
    }
}

void EndHostFrame(const HostFrameTiming& timing) noexcept {
    if (!gEnabled) return;
    try {
        if (Log* log = Instance()) log->EndFrame(timing);
    } catch (...) {
    }
}

void Mark() noexcept {
    if (!gEnabled) return;
    try {
        if (Log* log = Instance()) log->MarkFrame();
    } catch (...) {
    }
}

void Shutdown() noexcept {
    Log* log = nullptr;
    {
        std::lock_guard lock(gLogMutex);
        if (gShutDown) return;
        gShutDown = true;
        log = gLog;
    }
    if (log != nullptr) log->Stop();
}

} // namespace Fast::Renderer::Hitch
