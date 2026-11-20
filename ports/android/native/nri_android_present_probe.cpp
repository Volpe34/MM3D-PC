#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRISwapChain.h>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <vector>

#include "nri_probe_triangle_spirv.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NriPresentProbe", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NriPresentProbe", __VA_ARGS__)

struct Probe {
    android_app* app{};
    nri::Device* device{};
    nri::CoreInterface core{};
    nri::SwapChainInterface swapChainApi{};
    nri::PipelineLayout* pipelineLayout{};
    nri::Pipeline* pipeline{};
    nri::Queue* queue{};
    nri::SwapChain* swapChain{};
    nri::CommandAllocator* commandAllocator{};
    nri::CommandBuffer* commandBuffer{};
    nri::Fence* acquireFence{};
    nri::Fence* releaseFence{};
    std::vector<nri::Descriptor*> colorViews;
    uint32_t width{};
    uint32_t height{};
    bool ready{};
    bool loggedPresent{};
};

static bool checkNri(nri::Result result, const char* call) {
    if (result == nri::Result::SUCCESS) return true;
    LOGE("%s failed: NRI result=%u", call, unsigned(result));
    return false;
}

static void NRI_CALL nriMessage(nri::Message type, const char*, uint32_t, const char* message, void*) {
    if (type == nri::Message::ERROR) LOGE("NRI: %s", message);
    else LOGI("NRI: %s", message);
}

static void destroy(Probe& p) {
    p.ready = false;
    p.loggedPresent = false;
    if (p.device && p.core.DeviceWaitIdle) p.core.DeviceWaitIdle(p.device);
    if (p.pipeline) p.core.DestroyPipeline(p.pipeline);
    if (p.pipelineLayout) p.core.DestroyPipelineLayout(p.pipelineLayout);
    p.pipeline = nullptr;
    p.pipelineLayout = nullptr;
    if (p.commandBuffer && p.core.DestroyCommandBuffer) p.core.DestroyCommandBuffer(p.commandBuffer);
    if (p.commandAllocator && p.core.DestroyCommandAllocator) p.core.DestroyCommandAllocator(p.commandAllocator);
    p.commandBuffer = nullptr; p.commandAllocator = nullptr;
    for (auto* view : p.colorViews) if (view && p.core.DestroyDescriptor) p.core.DestroyDescriptor(view);
    p.colorViews.clear();
    if (p.acquireFence && p.core.DestroyFence) p.core.DestroyFence(p.acquireFence);
    if (p.releaseFence && p.core.DestroyFence) p.core.DestroyFence(p.releaseFence);
    p.acquireFence = nullptr; p.releaseFence = nullptr;
    if (p.swapChain && p.swapChainApi.DestroySwapChain) p.swapChainApi.DestroySwapChain(p.swapChain);
    p.swapChain = nullptr;
    if (p.device) nri::nriDestroyDevice(p.device);
    p.device = nullptr; p.queue = nullptr;
    p.width = p.height = 0;
    p.core = {}; p.swapChainApi = {};
}

static bool createDeviceAndSwapChain(Probe& p) {
    uint32_t adapterCount = 0;
    if (!checkNri(nri::nriEnumerateAdapters(nullptr, adapterCount), "nriEnumerateAdapters(count)") || adapterCount == 0) {
        LOGE("NRI returned no Vulkan adapters"); return false;
    }
    std::vector<nri::AdapterDesc> adapters(adapterCount);
    if (!checkNri(nri::nriEnumerateAdapters(adapters.data(), adapterCount), "nriEnumerateAdapters(list)")) return false;

    nri::DeviceCreationDesc deviceDesc{};
    deviceDesc.graphicsAPI = nri::GraphicsAPI::VK;
    deviceDesc.adapterDesc = adapters.data();
    deviceDesc.disableVKRayTracing = true;
    deviceDesc.callbackInterface.MessageCallback = nriMessage;
    if (!checkNri(nri::nriCreateDevice(deviceDesc, p.device), "nriCreateDevice") || !p.device) return false;
    if (!checkNri(nri::nriGetInterface(*p.device, NRI_INTERFACE(nri::CoreInterface), &p.core), "CoreInterface") ||
        !checkNri(nri::nriGetInterface(*p.device, NRI_INTERFACE(nri::SwapChainInterface), &p.swapChainApi), "SwapChainInterface") ||
        !checkNri(p.core.GetQueue(*p.device, nri::QueueType::GRAPHICS, 0, p.queue), "GetQueue") || !p.queue) return false;

    nri::SwapChainDesc desc{};
    desc.window.android.window = p.app->window;
    desc.queue = p.queue;
    desc.width = static_cast<nri::Dim_t>(ANativeWindow_getWidth(p.app->window));
    desc.height = static_cast<nri::Dim_t>(ANativeWindow_getHeight(p.app->window));
    desc.textureNum = 3;
    desc.format = nri::SwapChainFormat::BT709_G22_8BIT;
    desc.flags = nri::SwapChainBits::VSYNC;
    desc.queuedFrameNum = 1;
    if (!checkNri(p.swapChainApi.CreateSwapChain(*p.device, desc, p.swapChain), "NRI CreateSwapChain") || !p.swapChain) return false;

    uint32_t imageCount = 0;
    nri::Texture* const* textures = p.swapChainApi.GetSwapChainTextures(*p.swapChain, imageCount);
    if (!textures || imageCount < 2) { LOGE("NRI swapchain returned fewer than two images"); return false; }
    const nri::TextureDesc& first = p.core.GetTextureDesc(*textures[0]);
    p.width = first.width; p.height = first.height;
    if (!p.width || !p.height) { LOGE("NRI swapchain returned an empty image extent"); return false; }

    p.colorViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        const nri::TextureDesc& texture = p.core.GetTextureDesc(*textures[i]);
        if (texture.width != first.width || texture.height != first.height || texture.format != first.format) {
            LOGE("NRI swapchain images have inconsistent descriptions"); return false;
        }
        nri::TextureViewDesc viewDesc{};
        viewDesc.texture = textures[i];
        viewDesc.type = nri::TextureView::COLOR_ATTACHMENT;
        viewDesc.format = texture.format;
        viewDesc.mipNum = 1;
        viewDesc.layerNum = 1;
        if (!checkNri(p.core.CreateTextureView(viewDesc, p.colorViews[i]), "NRI CreateTextureView") || !p.colorViews[i]) return false;
    }

    if (!checkNri(p.core.CreateCommandAllocator(*p.queue, p.commandAllocator), "NRI CreateCommandAllocator") || !p.commandAllocator ||
        !checkNri(p.core.CreateCommandBuffer(*p.commandAllocator, p.commandBuffer), "NRI CreateCommandBuffer") || !p.commandBuffer ||
        !checkNri(p.core.CreateFence(*p.device, nri::SWAPCHAIN_SEMAPHORE, p.acquireFence), "NRI CreateFence(acquire)") || !p.acquireFence ||
        !checkNri(p.core.CreateFence(*p.device, nri::SWAPCHAIN_SEMAPHORE, p.releaseFence), "NRI CreateFence(release)") || !p.releaseFence) return false;

    nri::PipelineLayoutDesc layout{};
    layout.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
    if (!checkNri(p.core.CreatePipelineLayout(*p.device, layout, p.pipelineLayout), "NRI CreatePipelineLayout"))
        return false;

    const nri::ShaderDesc shaders[] = {
        {nri::StageBits::VERTEX_SHADER, triangle_vert, sizeof(triangle_vert), "main"},
        {nri::StageBits::FRAGMENT_SHADER, triangle_frag, sizeof(triangle_frag), "main"},
    };
    nri::ColorAttachmentDesc color{};
    color.format = first.format;
    color.colorWriteMask = nri::ColorWriteBits::RGBA;
    nri::GraphicsPipelineDesc pipeline{};
    pipeline.pipelineLayout = p.pipelineLayout;
    pipeline.inputAssembly.topology = nri::Topology::TRIANGLE_LIST;
    pipeline.rasterization.fillMode = nri::FillMode::SOLID;
    pipeline.rasterization.cullMode = nri::CullMode::NONE;
    pipeline.outputMerger.colors = &color;
    pipeline.outputMerger.colorNum = 1;
    pipeline.shaders = shaders;
    pipeline.shaderNum = 2;
    if (!checkNri(p.core.CreateGraphicsPipeline(*p.device, pipeline, p.pipeline), "NRI CreateGraphicsPipeline"))
        return false;

    p.ready = true;
    LOGI("NRI Android swapchain ready: GPU=%s, %u images, %ux%u; NRI command rendering enabled",
         adapters[0].name, imageCount, p.width, p.height);
    return true;
}

static void draw(Probe& p) {
    if (!p.ready) return;
    uint32_t index = 0;
    if (!checkNri(p.swapChainApi.AcquireNextTexture(*p.swapChain, *p.acquireFence, index), "NRI AcquireNextTexture")) return;
    if (index >= p.colorViews.size()) { LOGE("NRI returned an invalid swapchain image index"); return; }
    p.core.ResetCommandAllocator(*p.commandAllocator);
    if (!checkNri(p.core.BeginCommandBuffer(*p.commandBuffer, nullptr), "NRI BeginCommandBuffer")) return;

    nri::AttachmentDesc color{};
    color.descriptor = p.colorViews[index];
    color.clearValue.color.f = {0.06f, 0.22f, 0.48f, 1.0f};
    color.loadOp = nri::LoadOp::CLEAR;
    color.storeOp = nri::StoreOp::STORE;
    nri::RenderingDesc rendering{};
    rendering.colors = &color;
    rendering.colorNum = 1;
    p.core.CmdBeginRendering(*p.commandBuffer, rendering);
    const nri::Viewport viewport{0, 0, float(p.width), float(p.height), 0, 1, false};
    const nri::Rect scissor{0, 0, nri::Dim_t(p.width), nri::Dim_t(p.height)};
    const nri::DrawDesc triangle{3, 1, 0, 0};
    p.core.CmdSetViewports(*p.commandBuffer, &viewport, 1);
    p.core.CmdSetScissors(*p.commandBuffer, &scissor, 1);
    p.core.CmdSetPipelineLayout(*p.commandBuffer, nri::BindPoint::GRAPHICS, *p.pipelineLayout);
    p.core.CmdSetPipeline(*p.commandBuffer, *p.pipeline);
    p.core.CmdDraw(*p.commandBuffer, triangle);
    p.core.CmdEndRendering(*p.commandBuffer);
    if (!checkNri(p.core.EndCommandBuffer(*p.commandBuffer), "NRI EndCommandBuffer")) return;

    nri::FenceSubmitDesc wait{};
    wait.fence = p.acquireFence;
    wait.stages = nri::StageBits::COLOR_ATTACHMENT;
    nri::FenceSubmitDesc signal{};
    signal.fence = p.releaseFence;
    signal.stages = nri::StageBits::COLOR_ATTACHMENT;
    nri::CommandBuffer* commandBuffers[] = {p.commandBuffer};
    nri::QueueSubmitDesc submit{};
    submit.waitFences = &wait; submit.waitFenceNum = 1;
    submit.commandBuffers = commandBuffers; submit.commandBufferNum = 1;
    submit.signalFences = &signal; submit.signalFenceNum = 1;
    if (!checkNri(p.core.QueueSubmit(*p.queue, submit), "NRI QueueSubmit")) return;
    const nri::Result present = p.swapChainApi.QueuePresent(*p.swapChain, *p.releaseFence);
    if (!checkNri(present, "NRI QueuePresent")) return;
    if (!checkNri(p.core.QueueWaitIdle(p.queue), "NRI QueueWaitIdle")) return;
    if (!p.loggedPresent) {
        LOGI("NRI CmdDraw -> QueueSubmit -> QueuePresent succeeded; orange triangle submitted");
        p.loggedPresent = true;
    }
}

static void onCommand(android_app* app, int32_t command) {
    auto& p = *static_cast<Probe*>(app->userData);
    if (command == APP_CMD_INIT_WINDOW && app->window) {
        if (!createDeviceAndSwapChain(p)) { LOGE("NRI Android command-rendering setup failed"); destroy(p); }
    } else if (command == APP_CMD_TERM_WINDOW) {
        destroy(p);
    }
}

void android_main(android_app* app) {
    Probe p{}; p.app = app; app->userData = &p; app->onAppCmd = onCommand;
    LOGI("Starting NRI Android command-rendering probe");
    while (!app->destroyRequested) {
        int events; android_poll_source* source;
        while (ALooper_pollOnce(p.ready ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
        }
        if (p.ready) draw(p);
    }
    destroy(p);
}
