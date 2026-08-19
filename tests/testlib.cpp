
#include <vulkan/vulkan.h>
#include "VVPPL.h"
#include <vector>
#include <iostream>
#include <fstream>

#include "invert_spv.h"

// zeichnet einen Command Buffer fuer einmalige enutzung auf
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

// beendet, schickt ab und wartet, bis die GPU fertig ist
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


int main() {

	// ### VkInstance erzeugen

	// reine Metadaten, der Treiber darf sie protokollieren
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VVPPL Test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "VVPPL";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	// der Validation Layer meldet Fehler, die man sonst nicht sieht
	std::vector<const char*> layers = { "VK_LAYER_KHRONOS_validation"};

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

	std::cout << "instance ok\n";
	

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

	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	std::cout << "GPU: " << props.deviceName << "\n";

	// dasselbe Zwei-Aufruf-Muster fuer die Queue Families
	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

	//wir brauchen eine Family, die Compute kann
	uint32_t computeFamily = UINT32_MAX;
	for (uint32_t i = 0; i < familyCount; ++i){
		if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT){
			computeFamily = i;
			break;
		}
	}
	if (computeFamily == UINT32_MAX){
		std::cerr << "keine Compute-Queue-Family gefunden\n";
		return 1;
	}
	std::cout << "compute queue family: " << computeFamily << "\n";


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

	std::cout << "device ok\n";


	// ### Command Pool und One Shot Submit Helper

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = computeFamily;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	res = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
	if (res != VK_SUCCESS) {
		std::cerr << "vkCreateCommandPool failed: " << res << "\n";
		return 1;
	}

	// Testlauf: leeren Command Buffer aufzeichnen und abschicken
	VkCommandBuffer cmd = beginSingleTime(device, commandPool);
	endSingleTime(device, commandPool, computeQueue, cmd);
	std::cout << "command buffer ok\n";


	// ### vkImage + speicher + view

	const uint32_t width = 256;
	const uint32_t height = 256;

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent = { width, height, 1 };
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

	// vkCreateImage legt keinen Speicher an - den besorgen und binden wir selbst
	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(device, image, &memReq);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize =  memReq.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkDeviceMemory imageMemory = VK_NULL_HANDLE;
	res = vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
	if (res!=VK_SUCCESS) {
		std::cerr << "vkAllocateMemory failed: " << res << "\n";
		return 1;
	}

	vkBindImageMemory(device, image, imageMemory, 0);

	// der View beschreibt, wie ein Shader auf das Image schaut
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType	= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image	= image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format	= VK_FORMAT_R8G8B8A8_UNORM;
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


	// ### Image Barrier nach General + vkCmdClearColorImage

	VkCommandBuffer cmd2 = beginSingleTime(device, commandPool);

	// Layout-Uebergang UNDEFINED -> GENERAL, vorher duerfen wir das Image nicht benutzen
	VkImageMemoryBarrier barrier{};
	barrier.sType							= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout 						= VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout 						= VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex 			= VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex 			= VK_QUEUE_FAMILY_IGNORED;
	barrier.image							= image;
	barrier.srcAccessMask 					= 0;
	barrier.dstAccessMask 					= VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.subresourceRange.aspectMask 	= VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel 	= 0;
	barrier.subresourceRange.levelCount 	= 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount 	= 1;

	vkCmdPipelineBarrier(cmd2,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // worauf gewartet wird: auf nichts
		VK_PIPELINE_STAGE_TRANSFER_BIT, // was warten muss: der Clear
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	// jetzt darf geschrieben werden
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

	endSingleTime(device, commandPool, computeQueue, cmd2);
	std::cout << "clear ok\n";


	// ### descriptor set layout + pool + set, storage image einbinden

	// beschreibt eine einzelne ressource binding 0, ein storage image, sichtbar im compute-shader
	VkDescriptorSetLayoutBinding binding{};
	binding.binding			= 0;
	binding.descriptorType 	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	binding.descriptorCount	= 1;
	binding.stageFlags		= VK_SHADER_STAGE_COMPUTE_BIT;

	// das layout fasst alle bidnings zusammen - die form eiens descriptor sets
	VkDescriptorSetLayoutCreateInfo layoutInfo {};
	layoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount	= 1;
	layoutInfo.pBindings	= &binding;

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
	if(res != VK_SUCCESS){
		std::cerr << "vkCreateDescriptorSetLayout failed: " << res << "\n";
		return 1;
	}

	// wie viele descriptors welchen tryps des pool insegesamt vorhalten muss
	VkDescriptorPoolSize poolSize{};
	poolSize.type			= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSize.descriptorCount = 1;

	// der pool ist der Speicher, aus dem descroptor sets allozieert werden
	VkDescriptorPoolCreateInfo descPoolInfo{};
	descPoolInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descPoolInfo.maxSets	= 1;
	descPoolInfo.poolSizeCount	= 1;
	descPoolInfo.pPoolSizes = &poolSize;

	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	res = vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &descriptorPool);
	if(res != VK_SUCCESS){
		std::cerr << "vkCreateDescriptorPool failed: " << res << "\n";
		return 1;
	}

	// ein Set nach diesem layout aus dem pool holen
	VkDescriptorSetAllocateInfo setAlloc{};
	setAlloc.sType			= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAlloc.descriptorPool	= descriptorPool;
	setAlloc.descriptorSetCount	= 1;
	setAlloc.pSetLayouts	= &descriptorSetLayout;

	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	res = vkAllocateDescriptorSets(device, &setAlloc, &descriptorSet);
	if(res != VK_SUCCESS){
		std::cerr << "vkAllocateDescriptorSets failed: " << res << "\n";
		return 1;
	}

	// welches konkrete Image gemeint ist und in welchem layout es beim Zugriff sein wird
	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageView	= imageView;
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	// schreibt das Image in Binding 0 des Sets
	VkWriteDescriptorSet write{};
	write.sType		= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet	= descriptorSet;
	write.dstBinding	= 0;
	write.descriptorCount 	= 1;
	write.descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	write.pImageInfo		= &imgInfo;

	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	std::cout << "descriptor ok\n";


	// ### compute pipeline + vkCmdDispatch -> invertiertes .ppm

	VkShaderModuleCreateInfo shaderInfo{};
	shaderInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize	= invert_spv_sizeInBytes;
	shaderInfo.pCode	= invert_spv;

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	res = vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule);
	if(res != VK_SUCCESS){
		std::cerr << "vkCreateShaderModule failed: " << res << "\n";
		return 1;
	}

	// das Pipeline Layout sagt, welche Descriptor Sets die Pipeline erwartet
	VkPipelineLayoutCreateInfo pipeLayoutInfo{};
	pipeLayoutInfo.sType	= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeLayoutInfo.setLayoutCount	= 1;
	pipeLayoutInfo.pSetLayouts	= &descriptorSetLayout;

	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	res = vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &pipelineLayout);
	if(res != VK_SUCCESS){
		std::cerr << "vkCreatePipelineLayout failed: " << res << "\n";
		return 1;
	} 

	// die Compute Pipeline besteht nur aus einem Shader und diesem Layout
	VkComputePipelineCreateInfo pipeInfo{};
	pipeInfo.sType		= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeInfo.stage.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeInfo.stage.stage	= VK_SHADER_STAGE_COMPUTE_BIT;
	pipeInfo.stage.module	= shaderModule;
	pipeInfo.stage.pName	= "main";
	pipeInfo.layout			= pipelineLayout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline);
	if(res != VK_SUCCESS){
		std::cerr << "vkCreateComputePipelines failed: " << res << "\n";
		return 1;
	}
	// braucht man nicht mehr, pipeline hält shader code selbst
	vkDestroyShaderModule(device, shaderModule, nullptr); 


	// # Dispatch

	VkCommandBuffer cmd4 = beginSingleTime(device, commandPool);

	// der Clear hat per Transfer geschreiben, jetzt liest der Shader - das muss geordnet werden
	VkImageMemoryBarrier toCompute {};
	toCompute.sType		= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toCompute.oldLayout	= VK_IMAGE_LAYOUT_GENERAL;
	toCompute.newLayout	= VK_IMAGE_LAYOUT_GENERAL;
	toCompute.srcQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED;
	toCompute.dstQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED;
	toCompute.image		= image;
	toCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toCompute.dstAccessMask	= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	toCompute.subresourceRange	= range;

	vkCmdPipelineBarrier(cmd4, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	0, 0, nullptr, 0, nullptr, 1, &toCompute);

	vkCmdBindPipeline(cmd4, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(cmd4, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

	// eine workgroup deckt 8x8 Pixel ab, also aufrunden
	vkCmdDispatch(cmd4, (width + 7) / 8, (height + 7) / 8, 1);

	endSingleTime(device, commandPool, computeQueue, cmd4);
	std::cout << "dispatch ok\n";


	// ### Image in Host-Buffer kopieren und als .ppm rausschreiben
	
	// Staging nBuffer - Speicher, an den die CPU herankommt
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType	= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size		= width * height * 4;
	bufferInfo.usage	= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	res = vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
	if (res != VK_SUCCESS) {
		std::cerr << "vkCreateBuffer failed: " << res << "\n"; return 1;
	}

	// Memory für den Buffer erstellen
	// dafür erst mal nachschauen was für memory requirments der Buffer hat
	VkMemoryRequirements bufReq{};
	vkGetBufferMemoryRequirements(device, stagingBuffer, &bufReq);

	VkMemoryAllocateInfo bufAlloc{};
	bufAlloc.sType	= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	bufAlloc.allocationSize = bufReq.size;
	bufAlloc.memoryTypeIndex = findMemoryType(physicalDevice, bufReq.memoryTypeBits, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// und dann endlich den Memory erstellen
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	vkAllocateMemory(device, &bufAlloc, nullptr, &stagingMemory);
	
	// Memory an Buffer binden
	vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

	// command buffer erstellen und anfangen zu füllen
	VkCommandBuffer cmd3 = beginSingleTime(device, commandPool);

	// GENERAL -> TRANSFER_SRC_OPTIMAL, Image layout optimieren für aktion
	VkImageMemoryBarrier toSrc{};
	toSrc.sType		= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toSrc.oldLayout	= VK_IMAGE_LAYOUT_GENERAL;
	toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSrc.image = image;
	toSrc.srcAccessMask	= VK_ACCESS_SHADER_WRITE_BIT;
	toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	toSrc.subresourceRange = range;

	vkCmdPipelineBarrier(cmd3, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1 , &toSrc);
	
	// Image zum Buffer kopieren, damit wir es vom Host lesen können 
	// (weil von der GRam kann man es nicht direkt lesen)
	VkBufferImageCopy copy{}; // info glaub ich
	copy.bufferOffset	= 0;
	copy.bufferRowLength = 0; // 0 heißt dicht gepackt
	copy.bufferImageHeight = 0;
	copy.imageSubresource.aspectMask= VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.mipLevel	= 0;
	copy.imageSubresource.baseArrayLayer = 0;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset = {0, 0, 0};
	copy.imageExtent = {width, height, 1};

	vkCmdCopyImageToBuffer(cmd3, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &copy);

	// und jetzt flushen und den command buffer ausführen
	endSingleTime(device, commandPool, computeQueue, cmd3);

	// Speicher einblenden und als PPM rausschreiben
	void* data = nullptr;
	vkMapMemory(device, stagingMemory, 0, bufferInfo.size, 0, &data);
	
	const uint8_t* pixels = static_cast<const uint8_t*>(data);
	std::ofstream file("output.ppm", std::ios::binary);
	file << "P6\n" << width << " " << height << "\n255\n";
	for (uint32_t i = 0; i < width * height; ++i){
		file.write(reinterpret_cast<const char*>(&pixels[i*4]), 3); // rgb, alpha weglassen
	}
	file.close();

	vkUnmapMemory(device, stagingMemory);
	std::cout << "ppm ok\n";


	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

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
