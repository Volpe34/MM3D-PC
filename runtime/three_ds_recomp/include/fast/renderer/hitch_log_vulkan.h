#pragma once
// Timed, call-site-labelled Vulkan idle waits for MM3D_HITCH_LOG. Identical to
// the plain calls when the log is off.
#include "fast/renderer/hitch_log.h"

#include <vulkan/vulkan.h>

namespace Fast::Renderer::Hitch {
inline VkResult TimedDeviceWaitIdle(VkDevice device, const char* file, int line) {
    if (!gEnabled) return vkDeviceWaitIdle(device);
    const uint64_t start = NowNanoseconds();
    const VkResult result = vkDeviceWaitIdle(device);
    RecordWait(file, line, NowNanoseconds() - start);
    return result;
}
inline VkResult TimedQueueWaitIdle(VkQueue queue, const char* file, int line) {
    if (!gEnabled) return vkQueueWaitIdle(queue);
    const uint64_t start = NowNanoseconds();
    const VkResult result = vkQueueWaitIdle(queue);
    RecordWait(file, line, NowNanoseconds() - start);
    return result;
}
} // namespace Fast::Renderer::Hitch

#define HITCH_DEVICE_WAIT_IDLE(device) ::Fast::Renderer::Hitch::TimedDeviceWaitIdle((device), __FILE__, __LINE__)
#define HITCH_QUEUE_WAIT_IDLE(queue) ::Fast::Renderer::Hitch::TimedQueueWaitIdle((queue), __FILE__, __LINE__)
