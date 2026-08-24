#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace vvppl {

	struct GreyscaleSettings	{
		float strength{1.0f};
	};
	

	class PostProcessing {
		public:
			PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height);
			~PostProcessing();

			// API Guide - nicht kopierbar machen
			PostProcessing(const PostProcessing&) 				= delete;
			PostProcessing& operator=(const PostProcessing&)	= delete;

			// zeichnet den Effekt in den Command Buffer des Aufrufers auf
			void apply(VkCommandBuffer cmd, VkImage src, VkImage dst);
			void resize(uint32_t width, uint32_t height);

			// add effect
			void 				addInvert();
			GreyscaleSettings& 	addGreyscale();

		private:
			VkDevice m_device;
			VkPhysicalDevice m_physicalDevice;
			uint32_t	m_width;
			uint32_t	m_height;

			VkImage			m_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
			VkDeviceMemory	m_imageMemorys[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
			VkImageView		m_imageViews[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};

			VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
			VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
			VkDescriptorSet m_descriptorSets[2]{VK_NULL_HANDLE};
			VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};


			// ein Effekt: eine Pipeline plus ein Zeiger auf seine Parameter
			struct Effect {
				VkPipeline pipeline{VK_NULL_HANDLE};
				const void* params{nullptr};
				uint32_t paramSize{0};
			};

			std::vector<Effect>	m_effects;
			GreyscaleSettings	m_greyscaleSettings;
	};

}
