// Linux test-only LD_PRELOAD observer. All calls reach the real Vulkan loader.
// Inject OUT_OF_DATE before acquisition (no semaphore signal), and SUBOPTIMAL
// only after a successful acquire/present, preserving the Vulkan WSI contract.
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

namespace {
template<class T> T next(const char* name) {
    auto fn = reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
    if (!fn) std::abort();
    return fn;
}
int swapchains = 0, images = 0, views = 0, memory = 0, semaphores = 0;
unsigned acquisitions = 0, presents = 0, creations = 0;
bool inject() { return std::getenv("TC_VK_INJECT_WSI") != nullptr; }
}

#define OBSERVE_CREATE(Name, Info, Handle, Count) \
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreate##Name(VkDevice device, const Info* info, \
    const VkAllocationCallbacks* allocator, Handle* handle) { \
    const auto result = next<PFN_vkCreate##Name>("vkCreate" #Name)(device, info, allocator, handle); \
    if (result == VK_SUCCESS) ++Count; \
    return result; \
}
#define OBSERVE_DESTROY(Name, Handle, Count) \
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroy##Name(VkDevice device, Handle handle, \
    const VkAllocationCallbacks* allocator) { \
    if (handle) --Count; \
    next<PFN_vkDestroy##Name>("vkDestroy" #Name)(device, handle, allocator); \
}
OBSERVE_CREATE(Image, VkImageCreateInfo, VkImage, images)
OBSERVE_DESTROY(Image, VkImage, images)
OBSERVE_CREATE(ImageView, VkImageViewCreateInfo, VkImageView, views)
OBSERVE_DESTROY(ImageView, VkImageView, views)
OBSERVE_CREATE(Semaphore, VkSemaphoreCreateInfo, VkSemaphore, semaphores)
OBSERVE_DESTROY(Semaphore, VkSemaphore, semaphores)
OBSERVE_DESTROY(SwapchainKHR, VkSwapchainKHR, swapchains)

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(VkInstance instance,
    const VkXlibSurfaceCreateInfoKHR* info, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    std::fprintf(stderr, "VK_PROBE window=%lu\n", info->window);
    return next<PFN_vkCreateXlibSurfaceKHR>("vkCreateXlibSurfaceKHR")(instance, info, allocator, surface);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical,
    const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
    VkPhysicalDeviceProperties properties{};
    next<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(physical, &properties);
    std::fprintf(stderr, "VK_PROBE device=%s vendor=%04x device=%04x\n",
        properties.deviceName, properties.vendorID, properties.deviceID);
    return next<PFN_vkCreateDevice>("vkCreateDevice")(physical, info, allocator, device);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,
    const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* handle) {
    const auto result = next<PFN_vkAllocateMemory>("vkAllocateMemory")(device, info, allocator, handle);
    if (result == VK_SUCCESS) ++memory;
    return result;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory handle,
    const VkAllocationCallbacks* allocator) {
    if (handle) --memory;
    next<PFN_vkFreeMemory>("vkFreeMemory")(device, handle, allocator);
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,
    const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* handle) {
    const auto result = next<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(device, info, allocator, handle);
    if (result == VK_SUCCESS) {
        ++swapchains;
        ++creations;
        std::fprintf(stderr, "VK_PROBE swapchain=%u extent=%ux%u\n", creations,
            info->imageExtent.width, info->imageExtent.height);
    }
    return result;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device,
    VkSwapchainKHR chain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* index) {
    ++acquisitions;
    if (inject() && acquisitions == 7) {
        std::fputs("VK_PROBE injected acquire OUT_OF_DATE\n", stderr);
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    const auto result = next<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(
        device, chain, timeout, semaphore, fence, index);
    if (result != VK_SUCCESS) std::fprintf(stderr, "VK_PROBE native acquire result=%d\n", result);
    if (inject() && acquisitions == 12 && result == VK_SUCCESS) {
        std::fputs("VK_PROBE injected acquire SUBOPTIMAL\n", stderr);
        return VK_SUBOPTIMAL_KHR;
    }
    return result;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    const auto result = next<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(queue, info);
    if (result != VK_SUCCESS) std::fprintf(stderr, "VK_PROBE native present result=%d\n", result);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        ++presents;
        if (presents == 16) std::fputs("VK_PROBE successful presents=16\n", stderr);
    }
    // Drivers may already return SUBOPTIMAL right after a resize; inject on
    // the first VK_SUCCESS from the 18th present on.
    static bool presentInjected = false;
    if (inject() && !presentInjected && presents >= 18 && result == VK_SUCCESS) {
        presentInjected = true;
        std::fputs("VK_PROBE injected present SUBOPTIMAL\n", stderr);
        return VK_SUBOPTIMAL_KHR;
    }
    return result;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer command,
    VkStencilFaceFlags faces, uint32_t ref) {
    static bool seen[256]{};
    if (ref < 256 && !seen[ref]) {
        seen[ref] = true;
        std::fprintf(stderr, "VK_PROBE stencil faces=%u ref=%u\n", faces, ref);
    }
    next<PFN_vkCmdSetStencilReference>("vkCmdSetStencilReference")(command, faces, ref);
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    std::fprintf(stderr, "VK_PROBE close swapchains=%d images=%d views=%d memory=%d semaphores=%d creations=%u\n",
        swapchains, images, views, memory, semaphores, creations);
    next<PFN_vkDestroyDevice>("vkDestroyDevice")(device, allocator);
}
