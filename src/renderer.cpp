module;

#include <volk.h>

#include "fragment_spv.hpp"
#include "vertex_spv.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module renderengine;

namespace renderengine {
namespace {
void require(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) +
                                 ")");
    }
}

template <typename T> T info(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

struct Validation {
    std::atomic_uint errors{0};
    std::atomic_uint warnings{0};
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    auto &state = *static_cast<Validation *>(user);
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ++state.errors;
        std::cerr << "validation error: " << data->pMessage << '\n';
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        ++state.warnings;
        std::cerr << "validation warning: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
};

// One owner, one queue, one submission. Null handles make partial initialization safe.
// Destruction is explicitly dependency-ordered, including exceptional paths.
struct Resources {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    Image color;
    Image depth;
    Buffer vertices;
    Buffer readback;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    Resources() = default;
    Resources(const Resources &) = delete;
    Resources &operator=(const Resources &) = delete;
    ~Resources() {
        if (device != VK_NULL_HANDLE) {
            // Required even if submission succeeded but a later operation threw.
            (void)vkDeviceWaitIdle(device);
            vkDestroyFence(device, fence, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, layout, nullptr);
            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyShaderModule(device, vertexShader, nullptr);
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            vkDestroyRenderPass(device, renderPass, nullptr);
            for (auto *image : {&depth, &color}) {
                vkDestroyImageView(device, image->view, nullptr);
                vkDestroyImage(device, image->image, nullptr);
                vkFreeMemory(device, image->memory, nullptr);
            }
            for (auto *buffer : {&readback, &vertices}) {
                if (buffer->mapped != nullptr) {
                    vkUnmapMemory(device, buffer->memory);
                }
                vkDestroyBuffer(device, buffer->buffer, nullptr);
                vkFreeMemory(device, buffer->memory, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (messenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

bool extensionAvailable(const char *name, const char *layer = nullptr) {
    std::uint32_t count = 0;
    require(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr), "enumerate extensions");
    std::vector<VkExtensionProperties> extensions(count);
    require(vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data()), "enumerate extensions");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

void createInstance(Resources &resources, Validation &validation, bool enabled) {
    require(volkInitialize(), "load Vulkan (install a Vulkan runtime supporting version 1.2)");
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != nullptr) {
        require(vkEnumerateInstanceVersion(&loaderVersion), "query Vulkan loader version");
    }
    if (loaderVersion < VK_API_VERSION_1_2) {
        throw std::runtime_error("Vulkan loader 1.2 or newer is required");
    }
    auto application = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "renderengine";
    application.apiVersion = VK_API_VERSION_1_2;
    auto create = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    create.pApplicationInfo = &application;
    const char *layer = "VK_LAYER_KHRONOS_validation";
    std::array<const char *, 2> extensions{VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                           VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    auto debug =
        info<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debug.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debugCallback;
    debug.pUserData = &validation;
    const VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    auto features = info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = &sync;
    features.pNext = &debug;
    if (enabled) {
        std::uint32_t count = 0;
        require(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate validation layers");
        std::vector<VkLayerProperties> layers(count);
        require(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate validation layers");
        if (std::none_of(
                layers.begin(), layers.end(),
                [layer](const auto &candidate) { return std::strcmp(candidate.layerName, layer) == 0; }) ||
            !extensionAvailable(extensions[0]) || !extensionAvailable(extensions[1], layer)) {
            throw std::runtime_error(
                "--validation requires Khronos validation with debug-utils and "
                "synchronization validation support; install the Vulkan SDK validation layer");
        }
        create.enabledLayerCount = 1;
        create.ppEnabledLayerNames = &layer;
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        create.pNext = &features;
    }
    require(vkCreateInstance(&create, nullptr, &resources.instance), "create Vulkan 1.2 instance");
    volkLoadInstance(resources.instance);
    if (enabled) {
        require(vkCreateDebugUtilsMessengerEXT(resources.instance, &debug, nullptr, &resources.messenger),
                "create validation messenger");
    }
}

bool supportsImage(VkPhysicalDevice device, VkFormat format, VkImageUsageFlags usage, std::uint32_t width,
                   std::uint32_t height) {
    VkImageFormatProperties properties{};
    const auto result = vkGetPhysicalDeviceImageFormatProperties(
        device, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties);
    return result == VK_SUCCESS && width <= properties.maxExtent.width &&
           height <= properties.maxExtent.height && (properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0;
}

bool isHardware(VkPhysicalDeviceType type) {
    return type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
           type == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
}

void createDevice(Resources &resources, const Settings &settings) {
    std::uint32_t count = 0;
    require(vkEnumeratePhysicalDevices(resources.instance, &count, nullptr), "enumerate Vulkan devices");
    std::vector<VkPhysicalDevice> devices(count);
    require(vkEnumeratePhysicalDevices(resources.instance, &count, devices.data()),
            "enumerate Vulkan devices");
    // Prefer hardware, but only among devices satisfying every required capability.
    std::stable_sort(devices.begin(), devices.end(), [](auto left, auto right) {
        VkPhysicalDeviceProperties a{}, b{};
        vkGetPhysicalDeviceProperties(left, &a);
        vkGetPhysicalDeviceProperties(right, &b);
        return isHardware(a.deviceType) && !isHardware(b.deviceType);
    });
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        const bool hardware = isHardware(properties.deviceType);
        const bool software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        if (properties.apiVersion < VK_API_VERSION_1_2 ||
            (settings.device == DeviceClass::hardware && !hardware) ||
            (settings.device == DeviceClass::software && !software) ||
            settings.width > properties.limits.maxFramebufferWidth ||
            settings.height > properties.limits.maxFramebufferHeight ||
            !supportsImage(device, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           settings.width, settings.height) ||
            !supportsImage(device, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           settings.width, settings.height)) {
            continue;
        }
        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
        for (std::uint32_t family = 0; family < queueCount; ++family) {
            if (queues[family].queueCount == 0 || (queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            resources.physical = device;
            resources.family = family;
            const float priority = 1.0F;
            auto queue = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
            create.queueCreateInfoCount = 1;
            create.pQueueCreateInfos = &queue;
            require(vkCreateDevice(device, &create, nullptr, &resources.device), "create graphics device");
            volkLoadDevice(resources.device);
            vkGetDeviceQueue(resources.device, family, 0, &resources.queue);
            std::cout << "device="
                      << (hardware   ? "hardware"
                          : software ? "software"
                                     : "other")
                      << " api=" << VK_VERSION_MAJOR(properties.apiVersion) << '.'
                      << VK_VERSION_MINOR(properties.apiVersion) << " shader=SPIR-V1.5 slang=2026.17\n";
            return;
        }
    }
    throw std::runtime_error("no compatible device for requested class; requires Vulkan 1.2, graphics queue, "
                             "RGBA8 color/transfer and D32 depth support (check runtime or --device)");
}

std::uint32_t memoryType(VkPhysicalDevice physical, std::uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1U << i)) != 0 && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    throw std::runtime_error("device has no compatible memory type");
}

void createImage(Resources &resources, Image &image, const Settings &settings, VkFormat format,
                 VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format;
    create.extent = {settings.width, settings.height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = usage;
    require(vkCreateImage(resources.device, &create, nullptr, &image.image), "create attachment image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(resources.device, image.image, &requirements);
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(resources.physical, requirements.memoryTypeBits, 0);
    require(vkAllocateMemory(resources.device, &allocation, nullptr, &image.memory), "allocate image memory");
    require(vkBindImageMemory(resources.device, image.image, image.memory, 0), "bind image memory");
    auto view = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    require(vkCreateImageView(resources.device, &view, nullptr, &image.view), "create attachment view");
}

void createBuffer(Resources &resources, Buffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
    auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    create.size = size;
    create.usage = usage;
    require(vkCreateBuffer(resources.device, &create, nullptr, &buffer.buffer), "create buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(resources.device, buffer.buffer, &requirements);
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        memoryType(resources.physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    require(vkAllocateMemory(resources.device, &allocation, nullptr, &buffer.memory),
            "allocate host-visible memory");
    require(vkBindBufferMemory(resources.device, buffer.buffer, buffer.memory, 0), "bind buffer memory");
    require(vkMapMemory(resources.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped),
            "map buffer memory");
}

constexpr std::array<Vertex, 6> fixtureVertices{{
    {{-0.8F, -0.8F, 0.7F}, {1, 0, 0}},
    {{0.8F, -0.8F, 0.7F}, {1, 0, 0}},
    {{0.0F, 0.8F, 0.7F}, {1, 0, 0}},
    {{-0.8F, 0.8F, 0.2F}, {0, 1, 0}},
    {{0.8F, 0.8F, 0.2F}, {0, 1, 0}},
    {{0.0F, -0.8F, 0.2F}, {0, 1, 0}},
}};

void validateInput(const Settings &settings, std::span<const Vertex> vertices) {
    if (settings.width < 32 || settings.width > 2048 || settings.height < 32 || settings.height > 2048) {
        throw std::invalid_argument("dimensions must be between 32 and 2048");
    }
    if (vertices.size() > max_vertices || vertices.size() % 3 != 0) {
        throw std::invalid_argument("vertex count must be a multiple of three and at most 196608");
    }
    for (const auto &vertex : vertices) {
        for (std::size_t component = 0; component < 3; ++component) {
            const auto position = vertex.position[component];
            const auto color = vertex.color[component];
            const auto minimum = component == 2 ? 0.0F : -1.0F;
            if (!std::isfinite(position) || position < minimum || position > 1) {
                throw std::invalid_argument("positions must be finite with x/y in [-1,1] and z in [0,1]");
            }
            if (!std::isfinite(color) || color < 0 || color > 1) {
                throw std::invalid_argument("colors must be finite and in [0,1]");
            }
        }
    }
}

void createPipeline(Resources &resources, const Settings &settings) {
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    auto renderPass = info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    renderPass.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    renderPass.pAttachments = attachments.data();
    renderPass.subpassCount = 1;
    renderPass.pSubpasses = &subpass;
    renderPass.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    renderPass.pDependencies = dependencies.data();
    require(vkCreateRenderPass(resources.device, &renderPass, nullptr, &resources.renderPass),
            "create render pass");
    const std::array<VkImageView, 2> views{resources.color.view, resources.depth.view};
    auto framebuffer = info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    framebuffer.renderPass = resources.renderPass;
    framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size());
    framebuffer.pAttachments = views.data();
    framebuffer.width = settings.width;
    framebuffer.height = settings.height;
    framebuffer.layers = 1;
    require(vkCreateFramebuffer(resources.device, &framebuffer, nullptr, &resources.framebuffer),
            "create framebuffer");
    auto shader = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    shader.codeSize = sizeof(vertex_spv);
    shader.pCode = vertex_spv;
    require(vkCreateShaderModule(resources.device, &shader, nullptr, &resources.vertexShader),
            "create vertex shader");
    shader.codeSize = sizeof(fragment_spv);
    shader.pCode = fragment_spv;
    require(vkCreateShaderModule(resources.device, &shader, nullptr, &resources.fragmentShader),
            "create fragment shader");
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = resources.vertexShader;
    stages[0].pName = "main";
    stages[1] = info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = resources.fragmentShader;
    stages[1].pName = "main";
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 2> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    }};
    auto input =
        info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport viewport{0, 0, static_cast<float>(settings.width), static_cast<float>(settings.height),
                              0, 1};
    const VkRect2D scissor{{0, 0}, {settings.width, settings.height}};
    auto viewportState =
        info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    auto raster = info<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    auto multisample =
        info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto depthState = info<VkPipelineDepthStencilStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depthState.depthTestEnable = VK_TRUE;
    depthState.depthWriteEnable = VK_TRUE;
    depthState.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    auto blending =
        info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    auto layout = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    require(vkCreatePipelineLayout(resources.device, &layout, nullptr, &resources.layout),
            "create pipeline layout");
    auto pipeline = info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
    pipeline.pStages = stages.data();
    pipeline.pVertexInputState = &input;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewportState;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depthState;
    pipeline.pColorBlendState = &blending;
    pipeline.layout = resources.layout;
    pipeline.renderPass = resources.renderPass;
    require(vkCreateGraphicsPipelines(resources.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                      &resources.pipeline),
            "create graphics pipeline");
}

std::vector<std::uint8_t> draw(Resources &resources, const Settings &settings,
                               std::span<const Vertex> vertices) {
    createImage(resources, resources.color, settings, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
    createImage(resources, resources.depth, settings, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!vertices.empty()) {
        createBuffer(resources, resources.vertices, vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (settings.reverse) {
            auto *destination = static_cast<std::byte *>(resources.vertices.mapped);
            for (std::size_t i = 0; i < vertices.size(); i += 3) {
                std::memcpy(destination + i * sizeof(Vertex), vertices.data() + vertices.size() - i - 3,
                            3 * sizeof(Vertex));
            }
        } else {
            std::memcpy(resources.vertices.mapped, vertices.data(), vertices.size_bytes());
        }
        auto vertexRange = info<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
        vertexRange.memory = resources.vertices.memory;
        vertexRange.size = VK_WHOLE_SIZE;
        // Flush is also valid for coherent memory; whole allocation satisfies atom alignment.
        require(vkFlushMappedMemoryRanges(resources.device, 1, &vertexRange), "flush vertex upload");
    }
    const VkDeviceSize pixelCount = static_cast<VkDeviceSize>(settings.width) * settings.height;
    createBuffer(resources, resources.readback, pixelCount * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    createPipeline(resources, settings);
    auto pool = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool.queueFamilyIndex = resources.family;
    require(vkCreateCommandPool(resources.device, &pool, nullptr, &resources.pool), "create command pool");
    auto allocation = info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocation.commandPool = resources.pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    require(vkAllocateCommandBuffers(resources.device, &allocation, &command), "allocate command buffer");
    auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    require(vkBeginCommandBuffer(command, &begin), "begin commands");
    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0, 0, 0, 1}};
    clears[1].depthStencil = {1, 0};
    auto renderPass = info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    renderPass.renderPass = resources.renderPass;
    renderPass.framebuffer = resources.framebuffer;
    renderPass.renderArea.extent = {settings.width, settings.height};
    renderPass.clearValueCount = static_cast<std::uint32_t>(clears.size());
    renderPass.pClearValues = clears.data();
    vkCmdBeginRenderPass(command, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipeline);
    if (!vertices.empty()) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &resources.vertices.buffer, &offset);
        vkCmdDraw(command, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {settings.width, settings.height, 1};
    vkCmdCopyImageToBuffer(command, resources.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           resources.readback.buffer, 1, &copy);
    auto hostBarrier = info<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = resources.readback.buffer;
    hostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                         1, &hostBarrier, 0, nullptr);
    require(vkEndCommandBuffer(command), "end commands");
    auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    require(vkCreateFence(resources.device, &fence, nullptr, &resources.fence), "create completion fence");
    auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    require(vkQueueSubmit(resources.queue, 1, &submit, resources.fence), "submit frame");
    require(vkWaitForFences(resources.device, 1, &resources.fence, VK_TRUE, 10'000'000'000ULL),
            "wait for frame (10 second fence deadline)");
    auto readRange = info<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
    readRange.memory = resources.readback.memory;
    readRange.size = VK_WHOLE_SIZE;
    require(vkInvalidateMappedMemoryRanges(resources.device, 1, &readRange), "invalidate readback memory");
    const auto *rgba = static_cast<const std::uint8_t *>(resources.readback.mapped);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(pixelCount) * 3);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::copy_n(rgba + i * 4, 3, rgb.data() + i * 3);
    }
    return rgb;
}
} // namespace

std::vector<std::uint8_t> render(const Settings &settings) { return render(settings, fixtureVertices); }

std::vector<std::uint8_t> render(const Settings &settings, std::span<const Vertex> vertices) {
    validateInput(settings, vertices);
    Validation validation;
    std::vector<std::uint8_t> pixels;
    {
        Resources resources;
        createInstance(resources, validation, settings.validation);
        createDevice(resources, settings);
        pixels = draw(resources, settings, vertices);
    } // Include resource/device teardown in the validation result.
    if (settings.validation) {
        std::cout << "validation=sync errors=" << validation.errors.load()
                  << " warnings=" << validation.warnings.load() << '\n';
        if (validation.errors != 0 || validation.warnings != 0) {
            throw std::runtime_error("validation reported errors or warnings; inspect diagnostics");
        }
    }
    return pixels;
}
} // namespace renderengine
