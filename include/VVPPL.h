#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace vvppl {

	class PostProcessing {
		public:
			PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height);
			~PostProcessing();

			// zeichnet den Effekt in den Command Buffer des Aufrufers auf
			void apply(VkCommandBuffer cmd, VkImage src, VkImage dst);

		private:
			VkDevice m_device;
			VkPhysicalDevice m_physicalDevice;
			uint32_t	m_width;
			uint32_t	m_height;

			VkImage			m_image{VK_NULL_HANDLE};
			VkDeviceMemory	m_imageMemory{VK_NULL_HANDLE};
			VkImageView		m_imageView{VK_NULL_HANDLE};

			VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
			VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
			VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
			VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
			VkPipeline m_pipeline{VK_NULL_HANDLE};
	};

}
