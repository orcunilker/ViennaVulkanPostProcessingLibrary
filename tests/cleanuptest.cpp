/// @file cleanuptest.cpp
/// @brief Headless test: lets the constructor fail at different points and destroys the device afterwards. The
/// validation layer then reports every Vulkan object that the failed constructor did not destroy again.

#include <vulkan/vulkan.h>
#include "VVPPL.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

// The library calls the Vulkan functions of the loader. This program defines two of them itself, and the linker uses
// these definitions for the calls of the library as well. They fail on request and pass every other call on to the
// function of the device, which goes through the validation layer.
int g_allocations = 0;
int g_failAllocation = 0;
bool g_failPipelineLayout = false;

/// @brief Fails the allocation number g_failAllocation, counted from the last reset of g_allocations.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
                                                          const VkAllocationCallbacks* allocator,
                                                          VkDeviceMemory* memory) {
    if (++g_allocations == g_failAllocation) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    auto next = (PFN_vkAllocateMemory)vkGetDeviceProcAddr(device, "vkAllocateMemory");
    return next(device, info, allocator, memory);
}

/// @brief Fails while g_failPipelineLayout is set; the pipeline layout is the last object of the constructor.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device,
                                                                const VkPipelineLayoutCreateInfo* info,
                                                                const VkAllocationCallbacks* allocator,
                                                                VkPipelineLayout* layout) {
    if (g_failPipelineLayout) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    auto next = (PFN_vkCreatePipelineLayout)vkGetDeviceProcAddr(device, "vkCreatePipelineLayout");
    return next(device, info, allocator, layout);
}

/// @brief Runs the constructor and returns true if it threw std::runtime_error.
bool constructorThrows(VkDevice device, VkPhysicalDevice physicalDevice) {
    try {
        // two frames in flight, so four intermediate images with one allocation each
        vvppl::PostProcessing pp(device, physicalDevice, 64, 64, 2);
    } catch (const std::runtime_error& e) {
        std::cout << "  constructor threw: " << e.what() << "\n";
        return true;
    }
    return false;
}

/// @brief Runs the test; returns 1 if a constructor did not throw, leftover objects are reported by the layer.
int main() {

    // instance with the validation layer, device with one queue, as in testlib.cpp

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VVPPL Cleanup Test";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    // the validation layer lists every object that still exists when the device is destroyed
    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};

    // MoltenVK is only a portability implementation, the loader lists it only with this extension and flag
    std::vector<const char*> extensions = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = (uint32_t)layers.size();
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed\n";
        return 1;
    }

    // the first GPU; the library records no commands in its constructor, so the device needs no queue of a kind
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "no GPU with Vulkan support found\n";
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = 0;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // VK_KHR_portability_subset must be enabled on every device that reports it, such as MoltenVK
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availableExts.data());
    std::vector<const char*> deviceExtensions;
    for (const auto& e : availableExts) {
        if (strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
            deviceExtensions.push_back("VK_KHR_portability_subset");
        }
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed\n";
        return 1;
    }

    bool passed = true;

    // each of the four image allocations fails once, among them the second one, after the first image is complete
    for (int failAt = 1; failAt <= 4; failAt++) {
        std::cout << "allocation " << failAt << " of 4 fails\n";
        g_allocations = 0;
        g_failAllocation = failAt;
        passed = constructorThrows(device, physicalDevice) && passed;
    }
    g_failAllocation = 0;

    // the last object fails, after all images, the descriptor pool and the descriptor set layout exist
    std::cout << "pipeline layout fails\n";
    g_failPipelineLayout = true;
    passed = constructorThrows(device, physicalDevice) && passed;
    g_failPipelineLayout = false;

    // a constructor without a failure for comparison, its object is destroyed by the destructor
    std::cout << "no failure\n";
    passed = !constructorThrows(device, physicalDevice) && passed;

    // every object that is still alive now makes the validation layer report an error for vkDestroyDevice
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    std::cout << (passed ? "every failure threw; the test passes if the validation layer reported no error\n"
                         : "a constructor behaved unexpectedly\n");
    return passed ? 0 : 1;
}
