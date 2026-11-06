/// @file pixeltest.cpp
/// @brief Headless test: applies zero, one and two effects to a 2x2 sRGB image and compares every pixel with values
/// calculated on the CPU. This checks the format conversion of both blits and both ping-pong images, which hold the
/// result after an even and after an odd number of effects.

#include <vulkan/vulkan.h>
#include "VVPPL.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

/// @brief Allocates a command buffer and begins recording for a single submit.
VkCommandBuffer beginSingleTime(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

/// @brief Ends the command buffer, submits it, waits until the GPU is done and frees it.
void endSingleTime(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

/// @brief Finds a memory type that @p typeFilter allows and that has all requested properties.
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        bool typeOk = typeFilter & (1 << i);
        bool propsOk = (memProps.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeOk && propsOk)
            return i;
    }
    throw std::runtime_error("no suitable memory type found");
}

/// @brief Creates a 2D image in the swapchain format of VVE v2 and VSG, with device-local memory.
VkImage createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                    VkImageUsageFlags usage, VkDeviceMemory& memory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage failed");
    }

    // vkCreateImage allocates no memory, so allocate device-local memory and bind it
    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory failed");
    }
    vkBindImageMemory(device, image, memory, 0);
    return image;
}

/// @brief sRGB transfer function of one 8-bit channel, as the first blit decodes it.
float srgbToLinear(uint8_t value) {
    float c = value / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// @brief Inverse sRGB transfer function, as the second blit clamps and encodes a linear value into 8 bits.
int linearToSrgb(float linear) {
    float l = std::clamp(linear, 0.0f, 1.0f);
    float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return (int)std::lround(c * 255.0f);
}

/// @brief Runs the test; returns 0 if every pixel matches and 1 otherwise.
int main() {

    // instance with the validation layer, device with one queue, as in testlib.cpp

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VVPPL Pixel Test";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    // the validation layer reports errors that would otherwise go unnoticed
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

    // the first GPU and a queue family with graphics and compute support
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "no GPU with Vulkan support found\n";
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    const VkQueueFlags neededFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    uint32_t queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if ((families[i].queueFlags & neededFlags) == neededFlags) {
            queueFamily = i;
            break;
        }
    }
    if (queueFamily == UINT32_MAX) {
        std::cerr << "no queue family with graphics and compute found\n";
        return 1;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
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
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "vkCreateCommandPool failed\n";
        return 1;
    }

    // source and target image in B8G8R8A8_SRGB; separate images, so a missing copy leaves the cleared target

    const uint32_t width = 2;
    const uint32_t height = 2;
    // the four test colors as 8-bit sRGB values (red, green, blue), chosen so that no expected value lies close to
    // the middle between two 8-bit steps
    const uint8_t colors[4][3] = {{224, 128, 0}, {0, 128, 224}, {128, 192, 0}, {192, 0, 128}};

    VkDeviceMemory srcMemory = VK_NULL_HANDLE;
    VkImage src = createImage(device, physicalDevice, width, height,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, srcMemory);
    VkDeviceMemory dstMemory = VK_NULL_HANDLE;
    VkImage dst = createImage(device, physicalDevice, width, height,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, dstMemory);

    // host-visible buffer for the upload of the colors and the readback of the result, 4 bytes per pixel
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = width * height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        std::cerr << "vkCreateBuffer failed\n";
        return 1;
    }
    VkMemoryRequirements bufReq{};
    vkGetBufferMemoryRequirements(device, buffer, &bufReq);

    VkMemoryAllocateInfo bufAlloc{};
    bufAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufAlloc.allocationSize = bufReq.size;
    bufAlloc.memoryTypeIndex =
        findMemoryType(physicalDevice, bufReq.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &bufAlloc, nullptr, &bufferMemory) != VK_SUCCESS) {
        std::cerr << "vkAllocateMemory failed\n";
        return 1;
    }
    vkBindBufferMemory(device, buffer, bufferMemory, 0);

    uint8_t* pixels = nullptr;
    vkMapMemory(device, bufferMemory, 0, bufferInfo.size, 0, (void**)&pixels);

    // the format stores blue, green, red, alpha
    for (int i = 0; i < 4; i++) {
        pixels[i * 4 + 0] = colors[i][2];
        pixels[i * 4 + 1] = colors[i][1];
        pixels[i * 4 + 2] = colors[i][0];
        pixels[i * 4 + 3] = 255;
    }

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};

    // both images enter VK_IMAGE_LAYOUT_GENERAL, the layout apply() expects, and the colors are copied into src
    VkCommandBuffer cmd = beginSingleTime(device, commandPool);

    VkImageMemoryBarrier toGeneral[2]{};
    toGeneral[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral[0].image = src;
    toGeneral[0].srcAccessMask = 0;
    toGeneral[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral[0].subresourceRange = range;
    toGeneral[1] = toGeneral[0];
    toGeneral[1].image = dst;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, toGeneral);

    vkCmdCopyBufferToImage(cmd, buffer, src, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    endSingleTime(device, commandPool, queue, cmd);

    // the three cases: no effect, invert alone and greyscale with invert; the result is in the first ping-pong image
    // after an even number of effects and in the second one after an odd number
    bool passed = true;
    for (int effects = 0; effects <= 2; effects++) {
        vvppl::PostProcessing pp(device, physicalDevice, width, height);
        if (effects >= 1) {
            pp.addInvert();
        }
        if (effects == 2) {
            // the chain runs greyscale before invert, whatever the order of the add calls
            pp.addGreyscale();
        }

        cmd = beginSingleTime(device, commandPool);

        // clear the target to transparent black, so that a missing copy shows
        VkClearColorValue black{};
        vkCmdClearColorImage(cmd, dst, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &range);

        // apply() places no barrier on dst, so the host orders its clear before the second blit
        VkImageMemoryBarrier dstBarrier = toGeneral[1];
        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &dstBarrier);

        pp.apply(cmd, src, dst);

        // wait for the second blit of apply(), then copy the target into the buffer and make it visible to the CPU
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &dstBarrier);
        vkCmdCopyImageToBuffer(cmd, dst, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &copy);

        VkMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0,
                             nullptr, 0, nullptr);

        endSingleTime(device, commandPool, queue, cmd);

        std::cout << effects << (effects == 1 ? " effect" : " effects") << ": input -> expected / result\n";
        for (int i = 0; i < 4; i++) {
            // the expected color: decoded by the first blit, changed by the effects, encoded by the second blit
            float r = srgbToLinear(colors[i][0]);
            float g = srgbToLinear(colors[i][1]);
            float b = srgbToLinear(colors[i][2]);
            if (effects == 2) {
                // Rec. 709 luminance, as in greyscale.slang with the default strength 1
                float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = g = b = lum;
            }
            if (effects >= 1) {
                r = 1.0f - r;
                g = 1.0f - g;
                b = 1.0f - b;
            }
            const int expected[4] = {linearToSrgb(r), linearToSrgb(g), linearToSrgb(b), 255};
            const int result[4] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4 + 0], pixels[i * 4 + 3]};

            // the GPU stores intermediate values as 16-bit floats and may round the last step differently
            bool match = true;
            for (int c = 0; c < 4; c++) {
                match = match && std::abs(expected[c] - result[c]) <= 1;
            }
            passed = passed && match;

            std::cout << "  (" << int(colors[i][0]) << ", " << int(colors[i][1]) << ", " << int(colors[i][2])
                      << ") -> (" << expected[0] << ", " << expected[1] << ", " << expected[2] << ", " << expected[3]
                      << ") / (" << result[0] << ", " << result[1] << ", " << result[2] << ", " << result[3] << ")"
                      << (match ? "" : "  MISMATCH") << "\n";
        }
    } // every pp is destroyed before the device

    std::cout << (passed ? "all pixels match\n" : "pixel test failed\n");

    // destroy everything in reverse order of creation
    vkUnmapMemory(device, bufferMemory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, bufferMemory, nullptr);
    vkDestroyImage(device, dst, nullptr);
    vkFreeMemory(device, dstMemory, nullptr);
    vkDestroyImage(device, src, nullptr);
    vkFreeMemory(device, srcMemory, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return passed ? 0 : 1;
}
