// [TrussC] Internal single-window Vulkan host shared by sokol_app_tc.h's
// X11 and Win32 adapters. Ported from sokol_app.h 93aa187b; the parent header's
// zlib/libpng license applies. Include once for types, then with
// _SAPP_TC_VK_IMPL after the native window and process records are defined.
#ifndef _SAPP_TC_VK_TYPES_INCLUDED
#define _SAPP_TC_VK_TYPES_INCLUDED
// [TrussC] Shared single-window Vulkan host ported from upstream 93aa187b.
// Bound (driver): swapchains exceeding eight images fail creation before views or sync objects are created.
#define _SAPP_VK_MAX_SWAPCHAIN_IMAGES (8)

typedef struct {
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
} _sapp_tc_vk_swapchain_surface_t;

typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR surface_format;
    VkPhysicalDevice physical_device;
    uint32_t queue_family_index;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    bool swapchain_valid;
    bool swapchain_acquired;
    uint32_t num_swapchain_images;
    uint32_t cur_swapchain_image_index;
    VkImage swapchain_images[_SAPP_VK_MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchain_views[_SAPP_VK_MAX_SWAPCHAIN_IMAGES];
    _sapp_tc_vk_swapchain_surface_t msaa;
    _sapp_tc_vk_swapchain_surface_t depth;
    uint32_t sync_slot;
    struct {
        VkSemaphore render_finished_sem;
        VkSemaphore present_complete_sem;
    } sync[_SAPP_VK_MAX_SWAPCHAIN_IMAGES];
} _sapp_tc_vk_t;
#endif

#if defined(_SAPP_TC_VK_IMPL)
// [TrussC] Upstream 93aa187b Vulkan host, adapted to the shared X11/Win32 window.
#define _SAPP_TC_VK_STRUCT(TYPE, NAME) TYPE NAME = {}
#define _SAPP_TC_VK_PANIC(code) do { _sapp_tc_log(&_sapp_tc.app.desc.logger, 0, (uint32_t)SAPP_LOGITEM_##code, __LINE__, "panic: ", "%s", #code); abort(); } while (0)
#define _SAPP_TC_VK_ERROR(code) _sapp_tc_log(&_sapp_tc.app.desc.logger, 1, (uint32_t)SAPP_LOGITEM_##code, __LINE__, "error: ", "%s", #code)
#define _SAPP_TC_VK_WARN(code) _sapp_tc_log(&_sapp_tc.app.desc.logger, 2, (uint32_t)SAPP_LOGITEM_##code, __LINE__, "warn: ", "%s", #code)

#define _SAPP_VK_ZERO_COUNT_AND_ARRAY(num, type, count_name, array_name) uint32_t count_name = 0; type array_name[num] = {}
#define _SAPP_VK_MAX_COUNT_AND_ARRAY(num, type, count_name, array_name) uint32_t count_name = num; type array_name[num] = {}


static void _sapp_tc_vk_set_object_label(VkObjectType, uint64_t, const char*) {}

static int _sapp_tc_vk_mem_find_memory_type_index(uint32_t type_filter, VkMemoryPropertyFlags props) {
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceMemoryProperties, mem_props);
    vkGetPhysicalDeviceMemoryProperties(_sapp_tc.vk.physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props)) {
            return (int)i;
        }
    }
    return -1;
}

static void _sapp_tc_vk_create_instance(void) {
    SOKOL_ASSERT(0 == _sapp_tc.vk.instance);

    _SAPP_TC_VK_STRUCT(VkApplicationInfo, app_info);
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "sokol-app"; // FIXME: override via sapp_desc?
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "sokol";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    // Bound (port): no instance layers are enabled; the fixed list has 32 slots.
    _SAPP_VK_ZERO_COUNT_AND_ARRAY(32, const char*, layer_count, layer_names);

    // Bound (port): surface, platform-surface and debug-utils names fit in 32 slots.
    _SAPP_VK_ZERO_COUNT_AND_ARRAY(32, const char*, ext_count, ext_names);
    ext_names[ext_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
    #if defined(VK_USE_PLATFORM_XLIB_KHR)
        ext_names[ext_count++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
    #elif defined(VK_USE_PLATFORM_WIN32_KHR)
        ext_names[ext_count++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
    #endif
    // [TrussC] sokol-gfx's debug Vulkan backend requires object naming support.
    #if defined(SOKOL_DEBUG) || !defined(NDEBUG)
        ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    #endif
    SOKOL_ASSERT(ext_count <= 32);

    _SAPP_TC_VK_STRUCT(VkInstanceCreateInfo, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.flags = 0;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;
    VkResult res = vkCreateInstance(&create_info, 0, &_sapp_tc.vk.instance);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_CREATE_INSTANCE_FAILED);
    }
    SOKOL_ASSERT(_sapp_tc.vk.instance);
}

static void _sapp_tc_vk_destroy_instance(void) {
    SOKOL_ASSERT(_sapp_tc.vk.instance);
    vkDestroyInstance(_sapp_tc.vk.instance, 0);
    _sapp_tc.vk.instance = 0;
}

static uint32_t _sapp_tc_vk_required_device_extensions(const char** out_names, uint32_t max_count) {
    SOKOL_ASSERT(out_names && (max_count > 0));
    uint32_t count = 0;
    out_names[count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    out_names[count++] = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;
    SOKOL_ASSERT(count <= max_count); (void)(max_count);
    return count;
}

static bool _sapp_tc_vk_check_device_extensions(VkPhysicalDevice pdev, const char** required_exts, uint32_t num_required_exts) {
    SOKOL_ASSERT(pdev && required_exts && num_required_exts > 0);
    uint32_t ext_count = 0;
    VkResult res = vkEnumerateDeviceExtensionProperties(pdev, 0, &ext_count, 0);
    SOKOL_ASSERT(res == VK_SUCCESS); (void)(res);
    if (ext_count == 0) {
        return false;
    }
    VkExtensionProperties* ext_props = (VkExtensionProperties*) malloc(sizeof(VkExtensionProperties) * ext_count);
    SOKOL_ASSERT(ext_props);
    res = vkEnumerateDeviceExtensionProperties(pdev, 0, &ext_count, ext_props);
    bool all_supported = true;
    for (uint32_t req_ext_idx = 0; req_ext_idx < num_required_exts; req_ext_idx++) {
        const char* req_ext_name = required_exts[req_ext_idx];
        bool required_ext_supported = false;
        for (uint32_t ext_idx = 0; ext_idx < ext_count; ext_idx++) {
            const VkExtensionProperties* ext_prop = &ext_props[ext_idx];
            if (0 == strcmp(req_ext_name, ext_prop->extensionName)) {
                required_ext_supported = true;
                break;
            }
        }
        if (!required_ext_supported) {
            all_supported = false;
        }
    }
    free(ext_props);
    return all_supported;
}

static void _sapp_tc_vk_pick_physical_device(void) {
    SOKOL_ASSERT(_sapp_tc.vk.instance);
    SOKOL_ASSERT(_sapp_tc.vk.surface);
    SOKOL_ASSERT(0 == _sapp_tc.vk.physical_device);
    VkResult res = VK_SUCCESS;

    // Bound (driver): consider the first eight physical devices when enumeration is incomplete.
    _SAPP_VK_MAX_COUNT_AND_ARRAY(8, VkPhysicalDevice, physical_device_count, physical_devices);
    res = vkEnumeratePhysicalDevices(_sapp_tc.vk.instance, &physical_device_count, physical_devices);
    if ((res != VK_SUCCESS) && (res != VK_INCOMPLETE)) {
        _SAPP_TC_VK_PANIC(VULKAN_ENUMERATE_PHYSICAL_DEVICES_FAILED);
    }
    if (physical_device_count == 0) {
        _SAPP_TC_VK_PANIC(VULKAN_NO_PHYSICAL_DEVICES_FOUND);
    }
    // Bound (port): the required device extension list contains two names, within 32 slots.
    _SAPP_VK_ZERO_COUNT_AND_ARRAY(32, const char*, ext_count, ext_names);
    ext_count = _sapp_tc_vk_required_device_extensions(ext_names, 32);

    VkPhysicalDevice picked_pdev = 0;
    for (uint32_t pdev_idx = 0; pdev_idx < physical_device_count; pdev_idx++) {
        const VkPhysicalDevice pdev = physical_devices[pdev_idx];
        _SAPP_TC_VK_STRUCT(VkPhysicalDeviceProperties, dev_props);
        vkGetPhysicalDeviceProperties(pdev, &dev_props);
        if (dev_props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        if (!_sapp_tc_vk_check_device_extensions(pdev, ext_names, ext_count)) {
            continue;
        }
        // FIXME: handle theoretical case where graphics and present aren't supported by the same queue family index
        // Bound (driver): search only the first eight queue families for a suitable queue.
        _SAPP_VK_MAX_COUNT_AND_ARRAY(8, VkQueueFamilyProperties, queue_family_props_count, queue_family_props);
        vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queue_family_props_count, queue_family_props);
        bool has_required_queues = false;
        const VkQueueFlags required_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        for (uint32_t qfp_idx = 0; qfp_idx < queue_family_props_count; qfp_idx++) {
            const VkQueueFlags queue_flags = queue_family_props[qfp_idx].queueFlags;
            if ((queue_flags & required_flags) == required_flags) {
                _sapp_tc.vk.queue_family_index = qfp_idx;
                has_required_queues = true;
                break;
            }
        }
        if (!has_required_queues) {
            continue;
        }

        VkBool32 presentation_supported = false;
        res = vkGetPhysicalDeviceSurfaceSupportKHR(pdev, _sapp_tc.vk.queue_family_index, _sapp_tc.vk.surface, &presentation_supported);
        SOKOL_ASSERT(VK_SUCCESS == res);
        if (!presentation_supported) {
            continue;
        }

        // if we arrive here, found a suitable device
        picked_pdev = pdev;
        break;
    }
    if (0 == picked_pdev) {
        _SAPP_TC_VK_PANIC(VULKAN_NO_SUITABLE_PHYSICAL_DEVICE_FOUND);
    }
    _sapp_tc.vk.physical_device = picked_pdev;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(picked_pdev, &properties);
    _sapp_tc_log(&_sapp_tc.app.desc.logger, 3, (uint32_t)SAPP_LOGITEM_OK, __LINE__, "",
        "Vulkan host: device=%s", properties.deviceName);
#endif
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
}

static void _sapp_tc_vk_create_device(void) {
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
    SOKOL_ASSERT(0 == _sapp_tc.vk.device);

    const float queue_priority = 0.0f;
    _SAPP_TC_VK_STRUCT(VkDeviceQueueCreateInfo, queue_create_info);
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = _sapp_tc.vk.queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    // Bound (port): the required device extension list contains two names, within 32 slots.
    _SAPP_VK_ZERO_COUNT_AND_ARRAY(32, const char*, ext_count, ext_names);
    ext_count = _sapp_tc_vk_required_device_extensions(ext_names, 32);

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceFeatures2, supports);
    supports.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(_sapp_tc.vk.physical_device, &supports);

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceDescriptorBufferFeaturesEXT, descriptor_buffer_features);
    descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptor_buffer_features.descriptorBuffer = VK_TRUE;

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceExtendedDynamicStateFeaturesEXT, xds_features);
    xds_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    xds_features.pNext = &descriptor_buffer_features;
    xds_features.extendedDynamicState = VK_TRUE;

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceVulkan12Features, vk12_features);
    vk12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12_features.pNext = &xds_features;
    vk12_features.bufferDeviceAddress = VK_TRUE;

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceVulkan13Features, vk13_features);
    vk13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13_features.pNext = &vk12_features;
    vk13_features.dynamicRendering = VK_TRUE;
    vk13_features.synchronization2 = VK_TRUE;

    _SAPP_TC_VK_STRUCT(VkPhysicalDeviceFeatures2,  required);
    required.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    required.pNext = &vk13_features;
    required.features.samplerAnisotropy = VK_TRUE;
    required.features.dualSrcBlend = VK_TRUE;
    required.features.fragmentStoresAndAtomics = VK_TRUE;
    if (supports.features.textureCompressionBC) {
        required.features.textureCompressionBC = VK_TRUE;
    }
    if (supports.features.textureCompressionETC2) {
        required.features.textureCompressionETC2 = VK_TRUE;
    }
    if (supports.features.textureCompressionASTC_LDR) {
        required.features.textureCompressionASTC_LDR = VK_TRUE;
    }
    _SAPP_TC_VK_STRUCT(VkDeviceCreateInfo, dev_create_info);
    dev_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_create_info.pNext = &required;
    dev_create_info.queueCreateInfoCount = 1;
    dev_create_info.pQueueCreateInfos = &queue_create_info;
    dev_create_info.enabledExtensionCount = ext_count;
    dev_create_info.ppEnabledExtensionNames = ext_names;

    VkResult res = vkCreateDevice(_sapp_tc.vk.physical_device, &dev_create_info, 0, &_sapp_tc.vk.device);
    if (res != VK_SUCCESS) {
        switch (res) {
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                _SAPP_TC_VK_PANIC(VULKAN_CREATE_DEVICE_FAILED_EXTENSION_NOT_PRESENT);
                break;
            case VK_ERROR_FEATURE_NOT_PRESENT:
                _SAPP_TC_VK_PANIC(VULKAN_CREATE_DEVICE_FAILED_FEATURE_NOT_PRESENT);
                break;
            case VK_ERROR_INITIALIZATION_FAILED:
                _SAPP_TC_VK_PANIC(VULKAN_CREATE_DEVICE_FAILED_INITIALIZATION_FAILED);
                break;
            default:
                _SAPP_TC_VK_PANIC(VULKAN_CREATE_DEVICE_FAILED_OTHER);
                break;
        }
    }
    SOKOL_ASSERT(_sapp_tc.vk.device);

    SOKOL_ASSERT(0 == _sapp_tc.vk.queue);
    vkGetDeviceQueue(_sapp_tc.vk.device, _sapp_tc.vk.queue_family_index, 0, &_sapp_tc.vk.queue);
    SOKOL_ASSERT(_sapp_tc.vk.queue);
}

static void _sapp_tc_vk_destroy_device(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    vkDestroyDevice(_sapp_tc.vk.device, 0);
    _sapp_tc.vk.device = 0;
    _sapp_tc.vk.queue = 0;
}

static void _sapp_tc_vk_create_surface(void) {
    SOKOL_ASSERT(_sapp_tc.vk.instance);
    SOKOL_ASSERT(0 == _sapp_tc.vk.surface);
    VkResult res = VK_SUCCESS;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    // [TrussC] Upstream 93aa187b Win32 surface, using the fork's main HWND.
    _SAPP_TC_VK_STRUCT(VkWin32SurfaceCreateInfoKHR, win32_info);
    win32_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    win32_info.hinstance = GetModuleHandleW(NULL);
    win32_info.hwnd = _sapp_tc.app.main->hwnd;
    res = vkCreateWin32SurfaceKHR(_sapp_tc.vk.instance, &win32_info, 0, &_sapp_tc.vk.surface);
#else
    _SAPP_TC_VK_STRUCT(VkXlibSurfaceCreateInfoKHR, xlib_info);
    xlib_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    xlib_info.dpy = _sapp_tc.display;
    xlib_info.window = _sapp_tc.app.main->xwin;
    res = vkCreateXlibSurfaceKHR(_sapp_tc.vk.instance, &xlib_info, 0, &_sapp_tc.vk.surface);
#endif
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_CREATE_SURFACE_FAILED);
    }
    SOKOL_ASSERT(_sapp_tc.vk.surface);
}

static void _sapp_tc_vk_destroy_surface(void) {
    SOKOL_ASSERT(_sapp_tc.vk.instance);
    SOKOL_ASSERT(_sapp_tc.vk.surface);
    vkDestroySurfaceKHR(_sapp_tc.vk.instance, _sapp_tc.vk.surface, 0);
    _sapp_tc.vk.surface = 0;
}

static VkSurfaceFormatKHR _sapp_tc_vk_pick_surface_format(bool wants_srgb) {
    SOKOL_ASSERT(_sapp_tc.vk.instance);
    SOKOL_ASSERT(_sapp_tc.vk.surface);
    // Bound (driver): select a surface format from the first 64 returned formats.
    _SAPP_VK_MAX_COUNT_AND_ARRAY(64, VkSurfaceFormatKHR, fmt_count, formats);
    VkResult res = vkGetPhysicalDeviceSurfaceFormatsKHR(_sapp_tc.vk.physical_device, _sapp_tc.vk.surface, &fmt_count, formats);
    SOKOL_ASSERT((res == VK_SUCCESS) || (res == VK_INCOMPLETE)); (void)(res);
    SOKOL_ASSERT(fmt_count > 0);
    // first try to find SRGB format if requested
    if (wants_srgb) {
        for (uint32_t i = 0; i < fmt_count; i++) {
            switch (formats[i].format) {
                case VK_FORMAT_B8G8R8A8_SRGB:
                case VK_FORMAT_R8G8B8A8_SRGB:
                    return formats[i];
                default: break;
            }
        }
    }
    // fallthrough or no srgb format requested
    for (uint32_t i = 0; i < fmt_count; i++) {
        switch (formats[i].format) {
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_UNORM:
                return formats[i];
            default: break;
        }
    }
    // fallthrough: just take whatever's offered
    return formats[0];
}

static void _sapp_tc_vk_create_sync_objects(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(_sapp_tc.vk.num_swapchain_images > 0);
    _SAPP_TC_VK_STRUCT(VkSemaphoreCreateInfo, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkResult res;
    (void)(res);
    for (uint32_t i = 0; i < _sapp_tc.vk.num_swapchain_images; i++) {
        SOKOL_ASSERT(0 == _sapp_tc.vk.sync[i].present_complete_sem);
        SOKOL_ASSERT(0 == _sapp_tc.vk.sync[i].render_finished_sem);
        res = vkCreateSemaphore(_sapp_tc.vk.device, &create_info, 0, &_sapp_tc.vk.sync[i].present_complete_sem);
        SOKOL_ASSERT((res == VK_SUCCESS) && (_sapp_tc.vk.sync[i].present_complete_sem));
        _sapp_tc_vk_set_object_label(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)_sapp_tc.vk.sync[i].present_complete_sem, "present_complete_sem");
        res = vkCreateSemaphore(_sapp_tc.vk.device, &create_info, 0, &_sapp_tc.vk.sync[i].render_finished_sem);
        SOKOL_ASSERT((res == VK_SUCCESS) && (_sapp_tc.vk.sync[i].render_finished_sem));
        _sapp_tc_vk_set_object_label(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)_sapp_tc.vk.sync[i].render_finished_sem, "render_finished_sem");
    }
}

static void _sapp_tc_vk_destroy_sync_objects(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(_sapp_tc.vk.num_swapchain_images > 0);
    for (uint32_t i = 0; i < _sapp_tc.vk.num_swapchain_images; i++) {
        SOKOL_ASSERT(_sapp_tc.vk.sync[i].render_finished_sem);
        if (_sapp_tc.vk.sync[i].present_complete_sem) {
            vkDestroySemaphore(_sapp_tc.vk.device, _sapp_tc.vk.sync[i].present_complete_sem, 0);
            _sapp_tc.vk.sync[i].present_complete_sem = 0;
        }
        if (_sapp_tc.vk.sync[i].render_finished_sem) {
            vkDestroySemaphore(_sapp_tc.vk.device, _sapp_tc.vk.sync[i].render_finished_sem, 0);
            _sapp_tc.vk.sync[i].render_finished_sem = 0;
        }
    }
}

static VkDeviceMemory _sapp_tc_vk_mem_alloc_image_memory(const VkMemoryRequirements* mem_reqs) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(mem_reqs);
    int mem_type_index = _sapp_tc_vk_mem_find_memory_type_index(mem_reqs->memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (-1 == mem_type_index) {
        _SAPP_TC_VK_ERROR(VULKAN_ALLOC_DEVICE_MEMORY_NO_SUITABLE_MEMORY_TYPE);
        return 0;
    }
    _SAPP_TC_VK_STRUCT(VkMemoryAllocateInfo, alloc_info);
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs->size;
    alloc_info.memoryTypeIndex = (uint32_t) mem_type_index;
    VkDeviceMemory vk_dev_mem = 0;
    VkResult res = vkAllocateMemory(_sapp_tc.vk.device, &alloc_info, 0, &vk_dev_mem);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_ERROR(VULKAN_ALLOCATE_MEMORY_FAILED);
        return 0;
    }
    SOKOL_ASSERT(vk_dev_mem);
    return vk_dev_mem;
}

static void _sapp_tc_vk_mem_free_image_memory(VkDeviceMemory vk_dev_mem) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(vk_dev_mem);
    vkFreeMemory(_sapp_tc.vk.device, vk_dev_mem, 0);
}

static void _sapp_tc_vk_swapchain_destroy_surface(_sapp_tc_vk_swapchain_surface_t* surf) {
    SOKOL_ASSERT(surf);
    SOKOL_ASSERT(surf->img);
    SOKOL_ASSERT(surf->mem);
    SOKOL_ASSERT(surf->view);
    vkDestroyImageView(_sapp_tc.vk.device, surf->view, 0);
    surf->view = 0;
    vkDestroyImage(_sapp_tc.vk.device, surf->img, 0);
    surf->img = 0;
    _sapp_tc_vk_mem_free_image_memory(surf->mem);
    surf->mem = 0;
}

static void _sapp_tc_vk_swapchain_create_surface(
    _sapp_tc_vk_swapchain_surface_t* surf,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    VkSampleCountFlagBits sample_count_flags,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect_mask,
    const char* image_debug_label,
    const char* view_debug_label)
{
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(surf);
    SOKOL_ASSERT(0 == surf->img);
    SOKOL_ASSERT(0 == surf->mem);
    SOKOL_ASSERT(0 == surf->view);
    VkResult res;

    _SAPP_TC_VK_STRUCT(VkImageCreateInfo, img_create_info);
    img_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_create_info.imageType = VK_IMAGE_TYPE_2D;
    img_create_info.format = format;
    img_create_info.extent.width = width;
    img_create_info.extent.height = height;
    img_create_info.extent.depth = 1;
    img_create_info.mipLevels = 1;
    img_create_info.arrayLayers = 1;
    img_create_info.samples = sample_count_flags;
    img_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_create_info.usage = usage;
    img_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    res = vkCreateImage(_sapp_tc.vk.device, &img_create_info, 0, &surf->img);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_SWAPCHAIN_CREATE_IMAGE_FAILED);
    }
    SOKOL_ASSERT(surf->img);
    _sapp_tc_vk_set_object_label(VK_OBJECT_TYPE_IMAGE, (uint64_t)surf->img, image_debug_label);

    _SAPP_TC_VK_STRUCT(VkMemoryRequirements, mem_reqs);
    vkGetImageMemoryRequirements(_sapp_tc.vk.device, surf->img, &mem_reqs);
    surf->mem = _sapp_tc_vk_mem_alloc_image_memory(&mem_reqs);
    if (0 == surf->mem) {
        _SAPP_TC_VK_PANIC(VULKAN_SWAPCHAIN_ALLOC_IMAGE_DEVICE_MEMORY_FAILED);
    }
    res = vkBindImageMemory(_sapp_tc.vk.device, surf->img, surf->mem, 0);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_SWAPCHAIN_BIND_IMAGE_MEMORY_FAILED);
    }
    SOKOL_ASSERT(surf->mem);

    _SAPP_TC_VK_STRUCT(VkImageViewCreateInfo, view_create_info);
    view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create_info.image = surf->img;
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create_info.format = format;
    view_create_info.subresourceRange.aspectMask = aspect_mask;
    view_create_info.subresourceRange.levelCount = 1;
    view_create_info.subresourceRange.layerCount = 1;
    res = vkCreateImageView(_sapp_tc.vk.device, &view_create_info, 0, &surf->view);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_SWAPCHAIN_CREATE_IMAGE_VIEW_FAILED);
    }
    SOKOL_ASSERT(surf->view);
    _sapp_tc_vk_set_object_label(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)surf->view, view_debug_label);
}

static uint32_t _sapp_tc_vk_swapchain_min_image_count(const VkSurfaceCapabilitiesKHR* surf_caps) {
    // FIXME: figure out why at least 3 swapchain images are required to appease the validation layer
    // (on the Linux Intel driver, present-mode-fifo has a surf_caps.minImageCount == 3, while
    // on Windows surf_caps.minImageCount == 2, and using this directly causes validation layer
    // errors about the present-complete semaphore (to reproduce simply change the '= 3' below to '= 2')
    SOKOL_ASSERT(surf_caps);
    const uint32_t required_image_count = 3;
    uint32_t min_image_count = surf_caps->minImageCount;
    if (min_image_count < required_image_count) {
        min_image_count = required_image_count;
    }
    return min_image_count;
}

static void _sapp_tc_vk_create_swapchain_image_view(uint32_t image_index) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(image_index < _sapp_tc.vk.num_swapchain_images);
    SOKOL_ASSERT(_sapp_tc.vk.swapchain_images[image_index]);
    SOKOL_ASSERT(0 == _sapp_tc.vk.swapchain_views[image_index]);

    _SAPP_TC_VK_STRUCT(VkImageViewCreateInfo, view_create_info);
    view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create_info.format = _sapp_tc.vk.surface_format.format;
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_create_info.subresourceRange.levelCount = 1;
    view_create_info.subresourceRange.layerCount = 1;
    view_create_info.image = _sapp_tc.vk.swapchain_images[image_index];
    VkResult res = vkCreateImageView(_sapp_tc.vk.device, &view_create_info, 0, &_sapp_tc.vk.swapchain_views[image_index]);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_SWAPCHAIN_CREATE_IMAGE_VIEW_FAILED);
    }
    SOKOL_ASSERT(_sapp_tc.vk.swapchain_views[image_index]);
    _sapp_tc_vk_set_object_label(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)_sapp_tc.vk.swapchain_views[image_index], "swapchain_view");
}

static void _sapp_tc_vk_destroy_swapchain_image_view(uint32_t image_index) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    SOKOL_ASSERT(image_index < _sapp_tc.vk.num_swapchain_images);
    SOKOL_ASSERT(_sapp_tc.vk.swapchain_views[image_index]);
    vkDestroyImageView(_sapp_tc.vk.device, _sapp_tc.vk.swapchain_views[image_index], 0);
    _sapp_tc.vk.swapchain_views[image_index] = 0;
}

static void _sapp_tc_vk_destroy_associated_swapchain_resources(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    if (_sapp_tc.vk.msaa.img) {
        _sapp_tc_vk_swapchain_destroy_surface(&_sapp_tc.vk.msaa);
    }
    if (_sapp_tc.vk.depth.img) {
        _sapp_tc_vk_swapchain_destroy_surface(&_sapp_tc.vk.depth);
    }
    for (uint32_t i = 0; i < _sapp_tc.vk.num_swapchain_images; i++) {
        _sapp_tc_vk_destroy_swapchain_image_view(i);
        _sapp_tc.vk.swapchain_images[i] = 0;
    }
    _sapp_tc_vk_destroy_sync_objects();
}

static void _sapp_tc_vk_destroy_swapchain(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    if (_sapp_tc.vk.swapchain_valid) {
        _sapp_tc_vk_destroy_associated_swapchain_resources();
    }
    if (_sapp_tc.vk.swapchain) {
        vkDestroySwapchainKHR(_sapp_tc.vk.device, _sapp_tc.vk.swapchain, 0);
        _sapp_tc.vk.swapchain = 0;
    }
    _sapp_tc.vk.num_swapchain_images = 0;
    _sapp_tc.vk.swapchain_valid = false;
}

static bool _sapp_tc_vk_has_present_mode(VkPresentModeKHR mode) {
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
    SOKOL_ASSERT(_sapp_tc.vk.surface);
    // Bound (driver): an incomplete 32-mode query falls back to guaranteed FIFO presentation.
    _SAPP_VK_MAX_COUNT_AND_ARRAY(32, VkPresentModeKHR, present_mode_count, present_modes);
    VkResult res = vkGetPhysicalDeviceSurfacePresentModesKHR(_sapp_tc.vk.physical_device, _sapp_tc.vk.surface, &present_mode_count, present_modes);
    if (VK_SUCCESS == res) {
        for (uint32_t i = 0; i < present_mode_count; i++) {
            if (present_modes[i] == mode) {
                return true;
            }
        }
    }
    return false;
}

static void _sapp_tc_vk_create_swapchain(void) {
    SOKOL_ASSERT(_sapp_tc.vk.physical_device);
    SOKOL_ASSERT(_sapp_tc.vk.surface);
    SOKOL_ASSERT(_sapp_tc.vk.device);
    if (_sapp_tc.vk.swapchain_valid) {
        SOKOL_ASSERT(_sapp_tc.vk.swapchain);
        SOKOL_ASSERT(_sapp_tc.vk.num_swapchain_images > 0);
        SOKOL_ASSERT(_sapp_tc.vk.swapchain_images[0]);
        SOKOL_ASSERT(_sapp_tc.vk.swapchain_views[0]);
    } else {
        SOKOL_ASSERT(0 == _sapp_tc.vk.swapchain);
        SOKOL_ASSERT(0 == _sapp_tc.vk.num_swapchain_images);
        SOKOL_ASSERT(0 == _sapp_tc.vk.swapchain_images[0]);
        SOKOL_ASSERT(0 == _sapp_tc.vk.swapchain_views[0]);
    }

    const bool wants_msaa = _sapp_tc.app.desc.sample_count > 1;
    const bool wants_depth = _sapp_tc.app.main->depth_format != SAPP_PIXELFORMAT_NONE;
    const bool wants_stencil = _sapp_tc.app.main->depth_format == SAPP_PIXELFORMAT_DEPTH_STENCIL;
    const bool wants_srgb = false; // TrussC native windows use linear RGBA8/BGRA8.

    _SAPP_TC_VK_STRUCT(VkSurfaceCapabilitiesKHR, surf_caps);
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_sapp_tc.vk.physical_device, _sapp_tc.vk.surface, &surf_caps);
    SOKOL_ASSERT(res == VK_SUCCESS);
    const uint32_t fb_width = surf_caps.currentExtent.width;
    const uint32_t fb_height = surf_caps.currentExtent.height;

    // minimized window has zero width/height on some platforms (e.g. Windows)
    if ((0 == fb_width) || (0 == fb_height)) {
        if (_sapp_tc.vk.swapchain_valid) {
            _sapp_tc_vk_destroy_swapchain();
        }
        // NOTE: keep stored framebuffer width/height unchanged here instead
        // of resetting to 0 is intended! (harmonizes behavior with other
        // platforms and backends, and avoids things like Dear ImGui
        // piling up its windows in the top-left corner)
        return;
    }

    VkSwapchainKHR old_swapchain = _sapp_tc.vk.swapchain;
    _sapp_tc.vk.surface_format = _sapp_tc_vk_pick_surface_format(wants_srgb);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (_sapp_tc.app.desc.swap_interval == 0 && _sapp_tc_vk_has_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    _SAPP_TC_VK_STRUCT(VkSwapchainCreateInfoKHR, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.flags = 0;
    create_info.surface = _sapp_tc.vk.surface;
    create_info.minImageCount = _sapp_tc_vk_swapchain_min_image_count(&surf_caps);
    create_info.imageFormat = _sapp_tc.vk.surface_format.format;
    create_info.imageColorSpace = _sapp_tc.vk.surface_format.colorSpace;
    create_info.imageExtent.width = fb_width;
    create_info.imageExtent.height = fb_height;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = surf_caps.currentTransform;
    // FIXME: composite mode
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = true;
    create_info.oldSwapchain = old_swapchain;
    res = vkCreateSwapchainKHR(_sapp_tc.vk.device, &create_info, 0, &_sapp_tc.vk.swapchain);
    if (res != VK_SUCCESS) {
        _SAPP_TC_VK_PANIC(VULKAN_CREATE_SWAPCHAIN_FAILED);
    }
    SOKOL_ASSERT(_sapp_tc.vk.swapchain);

    if (old_swapchain) {
        _sapp_tc_vk_destroy_associated_swapchain_resources();
        vkDestroySwapchainKHR(_sapp_tc.vk.device, old_swapchain, 0);
    }

    uint32_t image_count = _SAPP_VK_MAX_SWAPCHAIN_IMAGES;
    res = vkGetSwapchainImagesKHR(_sapp_tc.vk.device,
        _sapp_tc.vk.swapchain,
        &image_count,
        _sapp_tc.vk.swapchain_images);
    if ((res != VK_SUCCESS) || (image_count > _SAPP_VK_MAX_SWAPCHAIN_IMAGES)) {
        _SAPP_TC_VK_ERROR(VULKAN_CREATE_SWAPCHAIN_FAILED);
        vkDestroySwapchainKHR(_sapp_tc.vk.device, _sapp_tc.vk.swapchain, 0);
        _sapp_tc.vk.swapchain = 0;
        _sapp_tc.vk.swapchain_valid = false;
        _sapp_tc.vk.swapchain_acquired = false;
        _sapp_tc.vk.num_swapchain_images = 0;
        memset(_sapp_tc.vk.swapchain_images, 0, sizeof(_sapp_tc.vk.swapchain_images));
        return;
    }
    _sapp_tc.vk.num_swapchain_images = image_count;
    SOKOL_ASSERT(_sapp_tc.vk.num_swapchain_images >= surf_caps.minImageCount);

    for (uint32_t i = 0; i < _sapp_tc.vk.num_swapchain_images; i++) {
        _sapp_tc_vk_create_swapchain_image_view(i);
    }

    // create depth-stencil buffer
    if (wants_depth) {
        VkFormat format = VK_FORMAT_D32_SFLOAT;
        VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (wants_stencil) {
            format = VK_FORMAT_D32_SFLOAT_S8_UINT;
            aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        _sapp_tc_vk_swapchain_create_surface(&_sapp_tc.vk.depth,
            format,
            fb_width,
            fb_height,
            (VkSampleCountFlagBits)_sapp_tc.app.desc.sample_count,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            aspect_mask,
            "swapchain_depthstencil_image",
            "swapchain_depthstencil_view");
    }

    // optionally create MSAA surface
    if (wants_msaa) {
        _sapp_tc_vk_swapchain_create_surface(&_sapp_tc.vk.msaa,
            _sapp_tc.vk.surface_format.format,
            fb_width,
            fb_height,
            (VkSampleCountFlagBits)_sapp_tc.app.desc.sample_count,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "swapchain_msaa_image",
            "swapchain_msaa_view");
    }

    // Publish the actual swapchain extent alongside the native window dimensions.
    // The Runtime reads these values after acquisition.
    _sapp_tc.app.main->fb_width = (int)fb_width;
    _sapp_tc.app.main->fb_height = (int)fb_height;
    auto* w = _sapp_tc.app.main;
    switch (_sapp_tc.vk.surface_format.format) {
        case VK_FORMAT_R8G8B8A8_UNORM: w->color_format = SAPP_PIXELFORMAT_RGBA8; break;
        case VK_FORMAT_B8G8R8A8_UNORM: w->color_format = SAPP_PIXELFORMAT_BGRA8; break;
        case VK_FORMAT_R8G8B8A8_SRGB: w->color_format = SAPP_PIXELFORMAT_SRGB8A8; break;
        case VK_FORMAT_B8G8R8A8_SRGB: w->color_format = SAPP_PIXELFORMAT_SBGR8A8; break;
        default: _SAPP_TC_VK_PANIC(VULKAN_CREATE_SWAPCHAIN_FAILED);
    }
    w->desc.sample_count = _sapp_tc.app.desc.sample_count;
    _sapp_tc.vk.sync_slot = 0;
    _sapp_tc_vk_create_sync_objects();
    _sapp_tc.vk.swapchain_valid = true;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    _sapp_tc_log(&_sapp_tc.app.desc.logger, 3, (uint32_t)SAPP_LOGITEM_OK, __LINE__, "",
        "Vulkan host: swapchain created");
#endif
}

static void _sapp_tc_vk_recreate_swapchain(VkResult reason = VK_SUCCESS) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    vkDeviceWaitIdle(_sapp_tc.vk.device);
    if (reason == VK_ERROR_SURFACE_LOST_KHR) {
        _sapp_tc_vk_destroy_swapchain();
        _sapp_tc_vk_destroy_surface();
        _sapp_tc_vk_create_surface();
    }
    _sapp_tc_vk_create_swapchain();
}

static void _sapp_tc_vk_init(void) {
    _sapp_tc_vk_create_instance();
    _sapp_tc_vk_create_surface();
    _sapp_tc_vk_pick_physical_device();
    _sapp_tc_vk_create_device();
    _sapp_tc_vk_create_swapchain();
}

static void _sapp_tc_vk_discard(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    vkDeviceWaitIdle(_sapp_tc.vk.device);
    _sapp_tc_vk_destroy_swapchain();
    _sapp_tc_vk_destroy_device();
    _sapp_tc_vk_destroy_surface();
    _sapp_tc_vk_destroy_instance();
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    _sapp_tc_log(&_sapp_tc.app.desc.logger, 3, (uint32_t)SAPP_LOGITEM_OK, __LINE__, "",
        "Vulkan host: teardown complete");
#endif
}

static void _sapp_tc_vk_swapchain_next(void) {
    SOKOL_ASSERT(_sapp_tc.vk.device);
    // Repeated queries in a frame share the already acquired image.
    // Presentation releases this acquisition for the next frame.
    if (_sapp_tc.vk.swapchain_acquired) return;
    if (!_sapp_tc.vk.swapchain_valid) {
        // try to re-create swapchain and resume normal operation when succeeded
        // (the failure path may be triggered on Windows with NVIDIA while the
        // application window is minimized)
        _sapp_tc_vk_recreate_swapchain();
        if (!_sapp_tc.vk.swapchain_valid) {
            return;
        }
    }
    SOKOL_ASSERT(_sapp_tc.vk.swapchain);
    VkResult res = vkAcquireNextImageKHR(
        _sapp_tc.vk.device,
        _sapp_tc.vk.swapchain,
        UINT64_MAX,     // timeout
        _sapp_tc.vk.sync[_sapp_tc.vk.sync_slot].present_complete_sem, // semaphore to signal
        0,  // fence to signal
        &_sapp_tc.vk.cur_swapchain_image_index);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (res != VK_SUCCESS) _sapp_tc_log(&_sapp_tc.app.desc.logger, 3, (uint32_t)SAPP_LOGITEM_OK,
        __LINE__, "", "Vulkan host: acquire result=%d", (int)res);
#endif
    // NOTE: on some configs (e.g. Linux+Intel, Windows+Intel, Windows+NVIDIA) vkAcquireNextImageKHR
    // never returns with VK_ERROR_OUT_OF_DATE, and essentially always succeeds.
    // This makes the following error handling hard to test reliably, we're basically
    // doing the same here as in the SaschaWillems samples: when vkAcquireNextImage returns
    // with VK_ERROR_OUT_OF_DATE, we just recreate the swapchain but don't attempt to
    // re-acquire and instead skip the next frame (via sapp_acquire_swapchain returning
    // an 'invalid' swapchain which then causes sokol-gfx to skip rendering and presentation)
    //
    // also see ticket: https://github.com/floooh/sokol/issues/1564
    //
    if ((res == VK_SUCCESS) || (res == VK_SUBOPTIMAL_KHR)) {
        // only place where the swapchain_acquired flag is set to true
        _sapp_tc.vk.swapchain_acquired = true;
    } else if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        _sapp_tc_vk_recreate_swapchain();
    } else {
        _SAPP_TC_VK_ERROR(VULKAN_ACQUIRE_NEXT_IMAGE_FAILED);
    }
}

static void _sapp_tc_vk_present(void) {
    SOKOL_ASSERT(_sapp_tc.vk.queue);
    if (_sapp_tc.vk.swapchain_acquired) {
        // only place where swapchain_acquired flag is set to false
        _sapp_tc.vk.swapchain_acquired = false;
        _SAPP_TC_VK_STRUCT(VkPresentInfoKHR, present_info);
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        // NOTE: using the current swapchain image index here instead of `sync_slot` is *NOT* a bug! The render_finished_semaphore *must*
        // be associated with the current swapchain image in case the swapchain implementation doesn't return swapchain images in order
        present_info.pWaitSemaphores = &_sapp_tc.vk.sync[_sapp_tc.vk.cur_swapchain_image_index].render_finished_sem;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &_sapp_tc.vk.swapchain;
        present_info.pImageIndices = &_sapp_tc.vk.cur_swapchain_image_index;
        VkResult res = vkQueuePresentKHR(_sapp_tc.vk.queue, &present_info);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        if (res != VK_SUCCESS) _sapp_tc_log(&_sapp_tc.app.desc.logger, 3, (uint32_t)SAPP_LOGITEM_OK,
            __LINE__, "", "Vulkan host: present result=%d", (int)res);
#endif
        // NOTE: the different behavior for VK_SUBOPTIMAL_KHR here compared to
        // to the acquire-swapchain code is intended, it's the same as in the
        // SaschaWillems Vulkan samples
        if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR) || (res == VK_ERROR_SURFACE_LOST_KHR)) {
            _sapp_tc_vk_recreate_swapchain(res);
        } else if (res != VK_SUCCESS) {
            _SAPP_TC_VK_WARN(VULKAN_QUEUE_PRESENT_FAILED);
        }
    }
}


#endif
