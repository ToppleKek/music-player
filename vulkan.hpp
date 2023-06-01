#pragma once

#include <vulkan/vulkan.h>
#include "common.hpp"
#include "thirdparty/imgui/imgui_impl_vulkan.h"
#include <vector>
#include <string>

namespace IchigoVulkan {
struct Context {
    VkQueue queue;
    VkDevice logical_device;
    VkInstance vk_instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice selected_gpu;
    VkDescriptorPool descriptor_pool;
    ImGui_ImplVulkanH_Window imgui_window_data;  // TODO: replace this with our own swapchain creation code
    u32 queue_family_index;

    void init(const char **extensions, u32 num_extensions);
};
}  // namespace IchigoVulkan
