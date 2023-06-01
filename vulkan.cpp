#include "vulkan.hpp"
#include <vulkan/vulkan_core.h>

void IchigoVulkan::Context::init(const char **extensions, u32 num_extensions) {
    // Instance creation
    static const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Music Player";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Ichigo";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = num_extensions;
    create_info.ppEnabledExtensionNames = extensions;
    create_info.enabledLayerCount = 0;
    create_info.ppEnabledLayerNames = layers;

    auto ret = vkCreateInstance(&create_info, nullptr, &vk_instance);
    std::printf("ret=%d\n", ret);
    assert(ret == VK_SUCCESS);

    // Physical GPU selection
    u32 gpu_count = 0;
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, nullptr);
    assert(gpu_count != 0);
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus.data());

    selected_gpu = VK_NULL_HANDLE;

    for (const auto gpu : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected_gpu = gpu;
            break;
        }
    }

    if (selected_gpu == VK_NULL_HANDLE)
        selected_gpu = gpus[0];

    // Queue creation
    queue_family_index = 0;
    u32 queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_gpu, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selected_gpu, &queue_count,
                                             queue_family_properties.data());

    for (u32 i = 0; i < queue_family_properties.size(); ++i) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = i;
            break;
        }
    }

    static const float queue_priority[] = {1.0f};
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = queue_priority;

    static const char *device_extensions[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledExtensionCount = 1;
    device_create_info.ppEnabledExtensionNames = device_extensions;

    assert(vkCreateDevice(selected_gpu, &device_create_info, nullptr,
                          &logical_device) == VK_SUCCESS);
    vkGetDeviceQueue(logical_device, queue_family_index, 0, &queue);

    // Descriptor pool creation
    static const VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                                      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * (sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize));
    pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);
    pool_info.pPoolSizes = pool_sizes;

    assert(vkCreateDescriptorPool(logical_device, &pool_info, nullptr,
                                  &descriptor_pool) == VK_SUCCESS);
}
