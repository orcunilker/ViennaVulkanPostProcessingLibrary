
#include <vulkan/vulkan.h>
#include "VVPPL.h"
#include <vector>
#include <iostream>
#include <string>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

// Allocate once, before warm-up and timing.
VkCommandBuffer allocateCommandBuffer(VkDevice device, VkCommandPool pool) {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS) {
		throw std::runtime_error("vkAllocateCommandBuffers failed");
	}
	return cmd;
}

// The previous submission has finished before we reset and record again.
void beginCommand(VkCommandBuffer cmd) {
	if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
		throw std::runtime_error("vkResetCommandBuffer failed");
	}
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("vkBeginCommandBuffer failed");
	}

}

// Submission and waiting are included in the measured wall-clock interval.
void submitAndWait(VkQueue queue, VkCommandBuffer cmd) {
	if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
		throw std::runtime_error("vkEndCommandBuffer failed");
	}

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;

	if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		throw std::runtime_error("vkQueueSubmit failed");
	}
	if (vkQueueWaitIdle(queue) != VK_SUCCESS) {
		throw std::runtime_error("vkQueueWaitIdle failed");
	}

}

// sucht einen Speichertyp, der zu den Anforderungen passt
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
		bool typeOk = typeFilter & (1 << i);
		bool propsOk = (memProps.memoryTypes[i].propertyFlags & properties) == properties;
		if(typeOk && propsOk) return i;
	}
	throw std::runtime_error("kein passender Speichertyp gefunden");
}


// creates an image with its own device-local memory
VkImage createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
	VkFormat format, VkImageUsageFlags usage, VkDeviceMemory& memory) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent = { width, height, 1 };
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

	// allocate memory that fits the image and bind it
	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(device, image, &memReq);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
		throw std::runtime_error("vkAllocateMemory failed");
	}
	if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
		throw std::runtime_error("vkBindImageMemory failed");
	}
	return image;
}

// Upload fixed HDR ramps and a checkerboard before warm-up, outside timing.
void uploadPattern(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue,
	VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height) {
	// Exact half-float encodings of 0, 0.125, 0.25, ..., 2.0.
	constexpr uint16_t levels[] = {0x0000, 0x3000, 0x3400, 0x3600, 0x3800,
		0x3900, 0x3a00, 0x3b00, 0x3c00, 0x3c80, 0x3d00, 0x3d80,
		0x3e00, 0x3e80, 0x3f00, 0x3f80, 0x4000};
	const VkDeviceSize bytes = VkDeviceSize(width) * height * 4 * sizeof(uint16_t);

	// A temporary buffer carries CPU-written pixels to the source image.
	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = bytes;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBuffer buffer = VK_NULL_HANDLE;
	if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("vkCreateBuffer failed");
	}
	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(device, buffer, &requirements);
	VkMemoryAllocateInfo allocation{};
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory memory = VK_NULL_HANDLE;
	if (vkAllocateMemory(device, &allocation, nullptr, &memory) != VK_SUCCESS ||
		vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
		throw std::runtime_error("staging allocation failed");
	}
	void* mapped = nullptr;
	if (vkMapMemory(device, memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
		throw std::runtime_error("vkMapMemory failed");
	}
	// Red increases horizontally, green vertically; blue alternates in 16-by-16 tiles.
	auto* pixels = static_cast<uint16_t*>(mapped);
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const size_t offset = (size_t(y) * width + x) * 4;
			pixels[offset] = levels[x * 16 / (width - 1)];
			pixels[offset + 1] = levels[y * 16 / (height - 1)];
			pixels[offset + 2] = levels[((x * 16 / width + y * 16 / height) % 2) ? 12 : 1];
			pixels[offset + 3] = levels[8];
		}
	}
	vkUnmapMemory(device, memory);

	beginCommand(cmd);
	VkBufferImageCopy copy{};
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = {width, height, 1};
	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	submitAndWait(queue, cmd);
	vkDestroyBuffer(device, buffer, nullptr);
	vkFreeMemory(device, memory, nullptr);
}

// All fifteen effects; the library determines their execution order.
const std::vector<std::string> fullChain = {
	"tonemap", "chromatic", "greyscale", "vignette", "filmgrain", "colorgrade", "solarize",
	"sabattier", "emboss", "sobel", "speedlines", "highlight", "segmentation", "dither", "invert"
};

// adds one effect by its command-line name, with the default settings of the library
void addEffect(vvppl::PostProcessing& pp, const std::string& name) {
	if (name == "tonemap") pp.addTonemap();
	else if (name == "chromatic") pp.addChromatic();
	else if (name == "greyscale") pp.addGreyscale();
	else if (name == "vignette") pp.addVignette();
	else if (name == "filmgrain") pp.addFilmGrain();
	else if (name == "colorgrade") pp.addColorGrade();
	else if (name == "solarize") pp.addSolarize();
	else if (name == "sabattier") pp.addSabattier();
	else if (name == "emboss") pp.addEmboss();
	else if (name == "sobel") pp.addSobel();
	else if (name == "speedlines") pp.addSpeedLines();
	else if (name == "highlight") pp.addHighlight();
	else if (name == "segmentation") pp.addSegmentation();
	else if (name == "dither") pp.addDither();
	else if (name == "invert") pp.addInvert();
	else throw std::runtime_error("unknown effect: " + name);
}



int main(int argc, char* argv[]) {

	// ### command line: bench <width> <height> <config> <warmup> <iterations>
	if (argc != 6) {
		std::cerr << "usage: bench <width> <height> <empty|full|effect> <warmup> <iterations>\n";
		return 1;
	}
	const uint32_t width = (uint32_t)std::stoul(argv[1]);
	const uint32_t height = (uint32_t)std::stoul(argv[2]);
	const std::string config = argv[3];
	const int warmup = std::stoi(argv[4]);
	const int iterations = std::stoi(argv[5]);
	if (width < 2 || height < 2 || width > 16384 || height > 16384 || warmup < 0 || iterations <= 0) {
		std::cerr << "invalid dimensions or iteration counts\n";
		return 1;
	}

	// ### VkInstance erzeugen

	// reine Metadaten, der Treiber darf sie protokollieren
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VVPPL Test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "VVPPL";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	// validation layer only in the debug build, in the release build it would distort the timings
	std::vector<const char*> layers;
#ifndef NDEBUG
	layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

	// MacOS: Vulkan laefut ueber MoltenVK und ist nur portability-konform.
	// ohne diese extension und das Flag findet der Loader kein Geraet.
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
	if (res != VK_SUCCESS){
		std::cerr << "vkCreateInstance failed: " << res << "\n";
		return 1;
	}
	

	// ### Physical Device finden und die Compute-Queue-Family bestimmen

	// Vulkan liefert Listen immer in zwei Aufrufen:
	// erst die Anzahl abfragen, dann den passend grossen Vektor fuellen
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		std::cerr << "keine GPU mit Vulkan-Unterstuetzung gefunden \n";
		return 1;
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

	// wir nehmen einfach die erste GPU, auf dem Mac gibt es ohnehin nur eine
	VkPhysicalDevice physicalDevice = devices[0];

	// dasselbe Zwei-Aufruf-Muster fuer die Queue Families
	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

	// Blits need graphics support, and the effects need compute support.
	uint32_t computeFamily = UINT32_MAX;
	for (uint32_t i = 0; i < familyCount; ++i){
		if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
			(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
			computeFamily = i;
			break;
		}
	}
	if (computeFamily == UINT32_MAX){
		std::cerr << "keine Compute-Queue-Family gefunden\n";
		return 1;
	}


	// ### logical device (VkDevice)

	// jede Queue braucht eine Priorität zwischen 0.0 und 1.0, auch wenn es nur eine gibt
	float queuePriority = 1.0f;

	VkDeviceQueueCreateInfo queueInfo{};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = computeFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriority;

	// MoltenVK meldet VK_KHR_portabilitysubset. Wenn ein Gerät diese Extension meldet
	// muss man sie laut spezifikation aktivieren.
	uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> availibleExts(extCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availibleExts.data());

	std::vector<const char*> deviceExtensions;
	for (const auto& e : availibleExts){
		if (strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
				deviceExtensions.push_back("VK_KHR_portability_subset");
		}
	}

	VkDeviceCreateInfo deviceInfo{};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = (uint32_t) deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

	VkDevice device = VK_NULL_HANDLE;
	res = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
	if (res != VK_SUCCESS) {
		std::cerr << "vkCreateDevice failed: " << res << "\n";
		return 1;
	}

	// die Queue wird nicht erzeugt, sondern nur abgeholt - sie entsteht mit dem Device
	VkQueue computeQueue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, computeFamily, 0, &computeQueue);


	// ### Command Pool und One Shot Submit Helper

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = computeFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	res = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
	if (res != VK_SUCCESS) {
		std::cerr << "vkCreateCommandPool failed: " << res << "\n";
		return 1;
	}



	// ### source and destination image, in the formats of the HDR image and the swapchain in V3

	VkDeviceMemory srcMemory = VK_NULL_HANDLE;
	VkImage src = createImage(device, physicalDevice, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, srcMemory);

	VkDeviceMemory dstMemory = VK_NULL_HANDLE;
	VkImage dst = createImage(device, physicalDevice, width, height, VK_FORMAT_B8G8R8A8_SRGB,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT, dstMemory);



	// Both images enter GENERAL before the one-time pattern upload.

	VkCommandBuffer cmd = allocateCommandBuffer(device, commandPool);
	beginCommand(cmd);

	// Layout-Uebergang UNDEFINED -> GENERAL, vorher duerfen wir das Image nicht benutzen
	VkImageMemoryBarrier barrier{};
	barrier.sType							= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout 						= VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout 						= VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex 			= VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex 			= VK_QUEUE_FAMILY_IGNORED;
	barrier.image							= src;
	barrier.srcAccessMask 					= 0;
	barrier.dstAccessMask 					= VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.subresourceRange.aspectMask 	= VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel 	= 0;
	barrier.subresourceRange.levelCount 	= 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount 	= 1;

	// the destination needs the same transition
	VkImageMemoryBarrier dstBarrier = barrier;
	dstBarrier.image = dst;
	VkImageMemoryBarrier barriers[] = {barrier, dstBarrier};

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // worauf gewartet wird: auf nichts
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 2, barriers);

	submitAndWait(computeQueue, cmd);
	uploadPattern(device, physicalDevice, computeQueue, cmd, src, width, height);



	// ### timing: one run of this program gives one CSV line
	{
		vvppl::PostProcessing pp(device, physicalDevice, width, height);
		if (config == "full") {
			for (const std::string& name : fullChain) addEffect(pp, name);
		} else if (config != "empty") {
			addEffect(pp, config);
		}

		// warm-up without timing, so first submits and driver caches are not measured
		for (int i = 0; i < warmup; ++i) {
			beginCommand(cmd);
			pp.apply(cmd, src, dst);
			submitAndWait(computeQueue, cmd);
		}

		// timed: record, submit and wait for one apply call
		std::chrono::duration<double, std::milli> total{0};
		for (int i = 0; i < iterations; ++i) {
			auto start = std::chrono::steady_clock::now();
			beginCommand(cmd);
			pp.apply(cmd, src, dst);
			submitAndWait(computeQueue, cmd);
			total += std::chrono::steady_clock::now() - start;
		}

		// config,width,height,warmup,iterations,mean_ms
		std::cout << config << "," << width << "," << height << "," << warmup << ","
			<< iterations << "," << total.count() / iterations << "\n";
	} // pp has to be destroyed before the device




	// ### final destroy
	vkFreeCommandBuffers(device, commandPool, 1, &cmd);
	vkDestroyImage(device, dst, nullptr);
	vkFreeMemory(device, dstMemory, nullptr);

	vkDestroyImage(device, src, nullptr);
	vkFreeMemory(device, srcMemory, nullptr);

	vkDestroyCommandPool(device, commandPool, nullptr);

	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
	return 0;
}
