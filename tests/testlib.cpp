/// @file testlib.cpp
/// @brief Headless test: clears a 256x256 image to red, applies all effects and writes output.ppm.

#include <vulkan/vulkan.h>
#include "VVPPL.h"
#include <cstring>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <fstream>

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

/// @brief Runs the test; returns 0 on success and 1 if a Vulkan call fails.
int main() {

    // instance with the validation layer

    // metadata only, the driver may log it
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VVPPL Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VVPPL";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    // the validation layer reports errors that would otherwise go unnoticed
    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};

    // on macOS Vulkan runs on MoltenVK, which is only a portability implementation;
    // without this extension and the flag below the loader finds no device
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
    VkResult res = vkCreateInstance(&createInfo, nullptr, &instance);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed: " << res << "\n";
        return 1;
    }

    std::cout << "instance ok\n";

    // physical device and a queue family with graphics and compute support

    // Vulkan returns lists in two calls: first the count, then the data into a vector of that size
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "no GPU with Vulkan support found\n";
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // take the first GPU, a Mac has only one
    VkPhysicalDevice physicalDevice = devices[0];

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    std::cout << "GPU: " << props.deviceName << "\n";

    // the same two calls for the queue families
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    // apply() needs graphics for its blits and compute for its dispatches
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
    std::cout << "queue family: " << queueFamily << "\n";

    // logical device with one queue

    // every queue needs a priority between 0.0 and 1.0, even if there is only one
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // MoltenVK reports VK_KHR_portability_subset, and the specification requires enabling it
    // on every device that reports it
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availibleExts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availibleExts.data());

    std::vector<const char*> deviceExtensions;
    for (const auto& e : availibleExts) {
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
    res = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed: " << res << "\n";
        return 1;
    }

    // the queue is not created but fetched, it comes into existence with the device
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    std::cout << "device ok\n";

    // command pool for the single-time command buffers
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    res = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateCommandPool failed: " << res << "\n";
        return 1;
    }

    // smoke test: record and submit an empty command buffer
    VkCommandBuffer cmd = beginSingleTime(device, commandPool);
    endSingleTime(device, commandPool, queue, cmd);
    std::cout << "command buffer ok\n";

    // the test image, with memory and view

    const uint32_t width = 256;
    const uint32_t height = 256;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    res = vkCreateImage(device, &imageInfo, nullptr, &image);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateImage failed: " << res << "\n";
        return 1;
    }

    // vkCreateImage allocates no memory, so allocate device-local memory and bind it
    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    res = vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
    if (res != VK_SUCCESS) {
        std::cerr << "vkAllocateMemory failed: " << res << "\n";
        return 1;
    }

    vkBindImageMemory(device, image, imageMemory, 0);

    // the view describes how a shader sees the image
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    res = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateImageView failed: " << res << "\n";
        return 1;
    }

    std::cout << "image ok\n";

    // clear the image to red

    VkCommandBuffer cmd2 = beginSingleTime(device, commandPool);

    // layout transition UNDEFINED -> GENERAL, the image cannot be used before
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // waits for nothing before it (TOP_OF_PIPE), the clear (TRANSFER) waits for the transition
    vkCmdPipelineBarrier(cmd2, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkClearColorValue clearColor{};
    clearColor.float32[0] = 1.0f;
    clearColor.float32[1] = 0.0f;
    clearColor.float32[2] = 0.0f;
    clearColor.float32[3] = 1.0f;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(cmd2, image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);

    endSingleTime(device, commandPool, queue, cmd2);
    std::cout << "clear ok\n";

    // apply every effect in place; the block destroys pp before the device
    {
        vvppl::PostProcessing pp(device, physicalDevice, width, height);

        // the chain runs in its fixed order, independent of the order of these calls
        pp.addInvert();
        auto& greyscale = pp.addGreyscale();
        greyscale.strength = 0.8f;
        pp.addVignette();
        auto& filmgrain = pp.addFilmGrain();
        filmgrain.intensity = 0.2;
        filmgrain.time = 2346;
        auto& chromatic = pp.addChromatic();
        chromatic.intensity = 0.03;
        auto& tonemap = pp.addTonemap();
        tonemap.exposure = 0.9;
        auto& colorgrade = pp.addColorGrade();
        colorgrade.saturation = 1.5F;
        pp.addSolarize();
        pp.addSabattier();
        pp.addEmboss();
        auto& sobel = pp.addSobel();
        sobel.strength = 0.3F;
        auto& speedlines = pp.addSpeedLines();
        speedlines.time = 2346;
        pp.addHighlight();
        auto& segmentation = pp.addSegmentation();
        segmentation.strength = 0.5F;
        pp.addDither();

        VkCommandBuffer cmd4 = beginSingleTime(device, commandPool);
        pp.apply(cmd4, image, image);
        endSingleTime(device, commandPool, queue, cmd4);
    }

    // copy the image into a host-visible buffer and write it as a PPM file

    // staging buffer the CPU can read
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = width * height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    res = vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateBuffer failed: " << res << "\n";
        return 1;
    }

    // host-visible and coherent memory for the buffer, so no flush is needed before reading
    VkMemoryRequirements bufReq{};
    vkGetBufferMemoryRequirements(device, stagingBuffer, &bufReq);

    VkMemoryAllocateInfo bufAlloc{};
    bufAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufAlloc.allocationSize = bufReq.size;
    bufAlloc.memoryTypeIndex =
        findMemoryType(physicalDevice, bufReq.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    res = vkAllocateMemory(device, &bufAlloc, nullptr, &stagingMemory);
    if (res != VK_SUCCESS) {
        std::cerr << "vkAllocateMemory failed: " << res << "\n";
        return 1;
    }

    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    VkCommandBuffer cmd3 = beginSingleTime(device, commandPool);

    // GENERAL -> TRANSFER_SRC_OPTIMAL for the copy into the buffer; apply() wrote the image last with a blit
    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = image;
    toSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.subresourceRange = range;

    vkCmdPipelineBarrier(cmd3, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toSrc);

    // the CPU cannot read device-local memory directly, so copy the image into the staging buffer
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd3, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &copy);

    endSingleTime(device, commandPool, queue, cmd3);

    // map the buffer and write the pixels as binary PPM, without alpha
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, bufferInfo.size, 0, &data);

    const uint8_t* pixels = static_cast<const uint8_t*>(data);
    std::ofstream file("output.ppm", std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t i = 0; i < width * height; ++i) {
        file.write(reinterpret_cast<const char*>(&pixels[i * 4]), 3);
    }
    file.close();

    vkUnmapMemory(device, stagingMemory);
    std::cout << "ppm ok\n";

    // destroy everything in reverse order of creation

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    vkDestroyImageView(device, imageView, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, imageMemory, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
