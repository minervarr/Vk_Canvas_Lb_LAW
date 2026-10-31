// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

uint32_t Renderer::Impl::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_dev_, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    // Index 0 is a real type; returning it here was a silent invalid bind.
    return UINT32_MAX;
}

void Renderer::Impl::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vk_canvas";
    app_info.apiVersion       = VK_API_VERSION_1_1;

    // Windowing extensions come from the platform's SurfaceProvider
    // (VK_KHR_surface + VK_KHR_android_surface / VK_KHR_win32_surface / ...).
    std::vector<const char*> extensions = surface_provider_.instance_extensions();

    // Optional extensions: request only what the loader reports, otherwise
    // instance creation would fail. VK_EXT_swapchain_colorspace exposes HDR
    // colorspaces (BT2020/HLG/PQ); absence just means we fall back to SDR.
    uint32_t ext_count = 0;
    VCE_VK_CHECK_THROW(vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr));
    std::vector<VkExtensionProperties> avail(ext_count);
    if (ext_count > 0)
        VCE_VK_CHECK_THROW(vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, avail.data()));
    auto has = [&](const char* name) {
        for (auto& e : avail)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    if (has("VK_KHR_external_memory_capabilities"))
        extensions.push_back("VK_KHR_external_memory_capabilities");
    if (has("VK_KHR_get_physical_device_properties2"))
        extensions.push_back("VK_KHR_get_physical_device_properties2");
    if (has("VK_EXT_swapchain_colorspace")) {
        extensions.push_back("VK_EXT_swapchain_colorspace");
        ext_swapchain_colorspace_ = true;
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app_info;

#if defined(VCE_VK_VALIDATION)
    // Debug-only: request the Khronos validation layer when it is installed.
    // Missing layer is not fatal — a build machine without the SDK still runs.
    uint32_t layer_count = 0;
    VCE_VK_CHECK_THROW(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    if (layer_count > 0)
        VCE_VK_CHECK_THROW(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            ci.enabledLayerCount = 1;
            static const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
            ci.ppEnabledLayerNames = &kValidationLayer;
            LOGI("Validation layer enabled");
            break;
        }
    }
#endif // VCE_VK_VALIDATION
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        instance_ = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateInstance failed");
    }
}

void Renderer::Impl::create_surface() {
    surface_ = surface_provider_.create(instance_);
    if (surface_ == VK_NULL_HANDLE)
        throw std::runtime_error("SurfaceProvider::create failed");
}

static const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
    default:                                     return "other";
    }
}

void Renderer::Impl::pick_physical_device() {
    uint32_t count = 0;
    VCE_VK_CHECK_THROW(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    if (count == 0) throw std::runtime_error("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(count);
    VCE_VK_CHECK_THROW(vkEnumeratePhysicalDevices(instance_, &count, devs.data()));

    // A device is usable only if it can PRESENT to this surface. Enumerate the
    // candidates, then pick one; see
    // docs/decisions/0006-device-swapchain-and-output.md.
    struct Candidate {
        VkPhysicalDevice dev;
        uint32_t family;
        VkPhysicalDeviceType type;
    };
    std::vector<Candidate> candidates;

    for (VkPhysicalDevice dev : devs) {
        uint32_t ext_count = 0;
        VCE_VK_CHECK_THROW(vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr));
        std::vector<VkExtensionProperties> exts(ext_count);
        if (ext_count > 0)
            VCE_VK_CHECK_THROW(vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data()));
        bool has_swapchain = false;
        for (const auto& e : exts) {
            if (!std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                has_swapchain = true;
                break;
            }
        }
        if (!has_swapchain) continue;

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, families.data());

        uint32_t family = UINT32_MAX;
        for (uint32_t i = 0; i < family_count; ++i) {
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present) == VK_SUCCESS &&
                present == VK_TRUE) {
                family = i;
                break;
            }
        }
        if (family == UINT32_MAX) continue;

        VkPhysicalDeviceProperties cand_props{};
        vkGetPhysicalDeviceProperties(dev, &cand_props);
        candidates.push_back({dev, family, cand_props.deviceType});
    }

    if (candidates.empty()) {
        // Do not refuse to start; log the fallback so a later swapchain
        // failure is diagnosable instead of reading as "the UI is broken".
        physical_dev_ = devs[0];
        queue_family_ = 0;
        LOGI("Device selection: no device with VK_KHR_swapchain and a graphics+present "
             "queue family; falling back to device 0, queue family 0");
    } else {
        // Prefer a non-CPU device, and among those keep ENUMERATION ORDER. Do
        // not rank by device type: on a hybrid laptop that would silently move
        // the app to the dGPU and its battery, which is the user's decision.
        const Candidate* pick = &candidates.front();
        for (const Candidate& c : candidates) {
            if (c.type != VK_PHYSICAL_DEVICE_TYPE_CPU) {
                pick = &c;
                break;
            }
        }
        physical_dev_ = pick->dev;
        queue_family_ = pick->family;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_dev_, &props);
    device_name_ = props.deviceName;
    device_type_ = props.deviceType;
    LOGI("Device: %s (%s)%s, queue family %u", device_name_.c_str(),
         deviceTypeName(device_type_), isSoftwareDevice() ? " [SOFTWARE]" : "", queue_family_);

    // Capability scaffolding: optional techniques gate on caps_, never on the
    // platform, so a capable phone and a weak desktop GPU each get what they
    // can actually do.
    caps_.api_version                       = props.apiVersion;
    caps_.max_image_dim_2d                  = props.limits.maxImageDimension2D;
    caps_.max_image_array_layers            = props.limits.maxImageArrayLayers;
    caps_.max_compute_workgroup_invocations = props.limits.maxComputeWorkGroupInvocations;
    caps_.max_storage_buffer_range          = props.limits.maxStorageBufferRange;

    uint32_t ext_count = 0;
    VCE_VK_CHECK_THROW(vkEnumerateDeviceExtensionProperties(physical_dev_, nullptr, &ext_count, nullptr));
    std::vector<VkExtensionProperties> exts(ext_count);
    if (ext_count > 0)
        VCE_VK_CHECK_THROW(vkEnumerateDeviceExtensionProperties(physical_dev_, nullptr, &ext_count, exts.data()));
    for (auto& e : exts) {
        if (!std::strcmp(e.extensionName, "VK_KHR_sampler_ycbcr_conversion"))
            caps_.has_sampler_ycbcr_conversion = true;
        if (!std::strcmp(e.extensionName, "VK_ANDROID_external_memory_android_hardware_buffer"))
            caps_.has_external_memory_ahb = true;
    }
    // Both, or neither. The format list is core in Vulkan 1.2 and an
    // extension on 1.1; this instance is 1.1, so a pNext list without the
    // extension enabled is illegal. Mutable swapchain images are useless
    // without that list.
    bool hasMutable = false, hasList = false;
    for (auto& e : exts) {
        if (!std::strcmp(e.extensionName, "VK_KHR_swapchain_mutable_format"))
            hasMutable = true;
        if (!std::strcmp(e.extensionName, "VK_KHR_image_format_list"))
            hasList = true;
    }
    device_mutable_swapchain_ = hasMutable && hasList;
}

void Renderer::Impl::create_logical_device() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> dev_exts = { "VK_KHR_swapchain" };
    if (device_mutable_swapchain_) {
        dev_exts.push_back("VK_KHR_swapchain_mutable_format");
        dev_exts.push_back("VK_KHR_image_format_list");
    }
#if defined(__ANDROID__)
    // Camera AHardwareBuffer import chain (Android-only).
    dev_exts.insert(dev_exts.end(), {
        "VK_KHR_sampler_ycbcr_conversion",
        "VK_KHR_external_memory",
        "VK_ANDROID_external_memory_android_hardware_buffer",
        "VK_EXT_queue_family_foreign",
        "VK_KHR_bind_memory2",
        "VK_KHR_maintenance1"
    });
#endif

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_dev_, &supported);
    VkPhysicalDeviceVulkan11Features supported11{};
    supported11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceFeatures2 supported2{};
    supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported2.pNext = &supported11;
    vkGetPhysicalDeviceFeatures2(physical_dev_, &supported2);

    // Enable only what this device reports; requesting an unsupported feature
    // is invalid usage.
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy;
    features.shaderStorageImageReadWithoutFormat =
        supported.shaderStorageImageReadWithoutFormat;
    features.shaderStorageImageWriteWithoutFormat =
        supported.shaderStorageImageWriteWithoutFormat;

    // The compiled shaders declare the SPIR-V DrawParameters capability (any
    // vertex shader that uses the vertex index does under glslang). It is an
    // optional Vulkan 1.1 feature, so it has to be enabled explicitly even when
    // the device supports it.
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = supported11.shaderDrawParameters;
    if (!supported11.shaderDrawParameters)
        LOGE("Device does not support shaderDrawParameters; shaders may be rejected");

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = &features11;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = static_cast<uint32_t>(dev_exts.size());
    ci.ppEnabledExtensionNames = dev_exts.data();
    ci.pEnabledFeatures        = &features;

    if (vkCreateDevice(physical_dev_, &ci, nullptr, &device_) != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateDevice failed");
    }

    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (queue_ == VK_NULL_HANDLE)
        throw std::runtime_error("vkGetDeviceQueue returned no queue");

#if defined(__ANDROID__)
    vkGetAndroidHardwareBufferPropertiesANDROID_ =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(device_, "vkGetAndroidHardwareBufferPropertiesANDROID");
    vkCreateSamplerYcbcrConversion_ =
        (PFN_vkCreateSamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkCreateSamplerYcbcrConversion");
    vkDestroySamplerYcbcrConversion_ =
        (PFN_vkDestroySamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkDestroySamplerYcbcrConversion");
    if (!vkCreateSamplerYcbcrConversion_) {
        vkCreateSamplerYcbcrConversion_ = (PFN_vkCreateSamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkCreateSamplerYcbcrConversionKHR");
        vkDestroySamplerYcbcrConversion_ = (PFN_vkDestroySamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkDestroySamplerYcbcrConversionKHR");
    }
#endif
}

void Renderer::Impl::resolve_output_target() {
    // Output target: what surface we present into. SDR (the default) pins
    // 8-bit sRGB; HDR is opt-in per consumer and resolved against what this
    // surface actually advertises. See
    // docs/decisions/0006-device-swapchain-and-output.md, output_target.hh and
    // USAGE_hdr_output.md.
    //
    // Resolved EXACTLY ONCE, from the constructor, before the first
    // create_swapchain(). The design is deliberately "one target per process":
    // the render pass and every graphics pipeline bake this in (the render
    // pass from swapchain_format_, the pipelines from output_.encode as the
    // OUTPUT_ENCODE specialization constant), and recreate_swapchain() rebuilds
    // neither. Re-resolving on resize would therefore leave the pipelines
    // encoding for a target the swapchain no longer uses — silently wrong
    // colour, not a validation error. If a runtime target switch is ever
    // wanted, it has to tear down and rebuild the render pass and all
    // pipelines, and this guard is where that decision starts.
    if (output_resolved_) return;
    output_resolved_ = true;

    uint32_t fmt_count = 0;
    VCE_VK_CHECK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_dev_, surface_, &fmt_count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    if (fmt_count)
        VCE_VK_CHECK_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_dev_, surface_, &fmt_count, formats.data()));

    // Log the FULL enumeration, once per process. We do not yet know what real
    // drivers expose (Samsung/Pixel/Windows ICDs all differ) and this log is
    // how we find out — do not trim it without a device session to replace it.
    LOGI("Swapchain: surface advertises %u format/colorspace pairs%s", fmt_count,
         ext_swapchain_colorspace_ ? "" : " (VK_EXT_swapchain_colorspace ABSENT)");
    for (uint32_t i = 0; i < fmt_count; ++i)
        LOGI("  [%u] format=%d colorspace=%d", i, formats[i].format, formats[i].colorSpace);

    output_ = pickTarget(formats, ext_swapchain_colorspace_, requested_output_);
    swapchain_format_     = outputFormatToVk(output_.format);
    swapchain_colorspace_ = outputColorSpaceToVk(output_.colorspace);

    if (output_.fellBack)
        LOGE("Swapchain: %s requested but unsupported here — falling back to SDR 8-bit sRGB",
             outputTargetName(requested_output_));
    if (output_.degraded)
        LOGE("Swapchain: %s resolved to a PASSTHROUGH colorspace — the compositor will NOT "
             "interpret our encoding; correct output depends on the panel already being in "
             "that mode", outputTargetName(output_.target));
    LOGI("Swapchain: target=%s encode=%s fmt=%d colorspace=%d hdr=%d",
         outputTargetName(output_.target), outputEncodeName(output_.encode),
         (int)swapchain_format_, (int)swapchain_colorspace_, output_.hdr ? 1 : 0);
}
