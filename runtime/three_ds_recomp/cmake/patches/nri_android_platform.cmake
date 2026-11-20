if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

function(replace_once path old_text new_text description)
    file(READ "${path}" content)
    string(FIND "${content}" "${new_text}" already_patched)
    if(NOT already_patched EQUAL -1)
        return()
    endif()
    string(FIND "${content}" "${old_text}" old_location)
    if(old_location EQUAL -1)
        message(FATAL_ERROR "Pinned NRI ${description} changed; review the Android patch")
    endif()
    string(REPLACE "${old_text}" "${new_text}" content "${content}")
    file(WRITE "${path}" "${content}")
endfunction()

replace_once("${NRI_SOURCE_DIR}/CMakeLists.txt"
    [=[if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE))]=]
    [=[if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE OR ANDROID))]=]
    "platform guard")

replace_once("${NRI_SOURCE_DIR}/CMakeLists.txt"
    [=[$<$<BOOL:${NRI_ENABLE_WAYLAND_SUPPORT}>:
                VK_USE_PLATFORM_WAYLAND_KHR
            >
    )]=]
    [=[$<$<BOOL:${NRI_ENABLE_WAYLAND_SUPPORT}>:
                VK_USE_PLATFORM_WAYLAND_KHR
            >
            $<$<BOOL:${ANDROID}>:
                VK_USE_PLATFORM_ANDROID_KHR
            >
    )]=]
    "Android Vulkan compile definition")

replace_once("${NRI_SOURCE_DIR}/Include/Extensions/NRISwapChain.h"
    [=[NriStruct(Window) {
    // Only one entity must be initialized
    Nri(WindowsWindow) windows;
    Nri(X11Window) x11;
    Nri(WaylandWindow) wayland;
    Nri(MetalWindow) metal;
};]=]
    [=[NriStruct(AndroidWindow) { // Expects VK_USE_PLATFORM_ANDROID_KHR
    void* window;           // ANativeWindow*
};

NriStruct(Window) {
    // Only one entity must be initialized
    Nri(WindowsWindow) windows;
    Nri(X11Window) x11;
    Nri(WaylandWindow) wayland;
    Nri(MetalWindow) metal;
    Nri(AndroidWindow) android;
};]=]
    "Android swapchain window descriptor")

replace_once("${NRI_SOURCE_DIR}/Source/VK/DispatchTable.h"
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
    VK_FUNC(CreateMetalSurfaceEXT);
#endif
]=]
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
    VK_FUNC(CreateMetalSurfaceEXT);
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    VK_FUNC(CreateAndroidSurfaceKHR);
#endif
]=]
    "Android surface dispatch entry")

replace_once("${NRI_SOURCE_DIR}/Source/VK/DeviceVK.hpp"
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        desiredInstanceExts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
    }
]=]
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        desiredInstanceExts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        desiredInstanceExts.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
    }
]=]
    "Android instance extension enablement")

replace_once("${NRI_SOURCE_DIR}/Source/VK/DeviceVK.hpp"
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        GET_INSTANCE_FUNC(CreateMetalSurfaceEXT);
#endif
    }
]=]
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        GET_INSTANCE_FUNC(CreateMetalSurfaceEXT);
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        GET_INSTANCE_FUNC(CreateAndroidSurfaceKHR);
#endif
    }
]=]
    "Android surface function loading")

replace_once("${NRI_SOURCE_DIR}/Source/VK/SwapChainVK.hpp"
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        if (swapChainDesc.window.metal.caMetalLayer) {
            VkMetalSurfaceCreateInfoEXT metalSurfaceCreateInfo = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
            metalSurfaceCreateInfo.pLayer = (CAMetalLayer*)swapChainDesc.window.metal.caMetalLayer;

            VkResult vkResult = vk.CreateMetalSurfaceEXT(m_Device, &metalSurfaceCreateInfo, m_Device.GetVkAllocationCallbacks(), &m_Surface);
            NRI_RETURN_ON_BAD_VKRESULT(&m_Device, vkResult, "vkCreateMetalSurfaceEXT");
        }
#endif
        VkBool32 supported = VK_FALSE;]=]
    [=[#ifdef VK_USE_PLATFORM_METAL_EXT
        if (swapChainDesc.window.metal.caMetalLayer) {
            VkMetalSurfaceCreateInfoEXT metalSurfaceCreateInfo = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
            metalSurfaceCreateInfo.pLayer = (CAMetalLayer*)swapChainDesc.window.metal.caMetalLayer;

            VkResult vkResult = vk.CreateMetalSurfaceEXT(m_Device, &metalSurfaceCreateInfo, m_Device.GetVkAllocationCallbacks(), &m_Surface);
            NRI_RETURN_ON_BAD_VKRESULT(&m_Device, vkResult, "vkCreateMetalSurfaceEXT");
        }
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        if (swapChainDesc.window.android.window) {
            VkAndroidSurfaceCreateInfoKHR androidSurfaceInfo = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
            androidSurfaceInfo.window = (ANativeWindow*)swapChainDesc.window.android.window;

            VkResult vkResult = vk.CreateAndroidSurfaceKHR(m_Device, &androidSurfaceInfo, m_Device.GetVkAllocationCallbacks(), &m_Surface);
            NRI_RETURN_ON_BAD_VKRESULT(&m_Device, vkResult, "vkCreateAndroidSurfaceKHR");
        }
#endif
        VkBool32 supported = VK_FALSE;]=]
    "Android surface creation")
