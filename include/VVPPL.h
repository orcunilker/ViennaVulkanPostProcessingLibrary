#pragma once

#ifdef VVPPL_USE_VOLK
	#include <volk.h>
#else
	#include <vulkan/vulkan.h>
#endif

#include <cstdint>
#include <vector>

namespace vvppl {

	struct GreyscaleSettings {
		float strength{1.0f};
	};
	struct VignetteSettings {
		float intensity{0.5};
		float radius{0.4};
		float smoothness{0.3};
	};	
	struct FilmGrainSettings {
		float intensity{0.08f};
		float time{0.0f};
	};
	struct ChromaticSettings {
		float intensity{0.005f};
	};
	struct TonemapSettings {
		float exposure{1.0f};
	};
	
	// This library is meant to be used by any Vulkan application (1.1 or higher). 
	// It can apply several configurable Post Processing effects on a VkImage.
	// The command buffer, source, and destination-image are provided by the host
	class PostProcessing {
		public:
			PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, uint32_t framesInFlight = 1);
			~PostProcessing();

			// API Guide - nicht kopierbar machen
			// Copying is prohibited, as the instances hold raw Vulkan handles
			// Destroying one is affecting the other, destroying both is problematic
			PostProcessing(const PostProcessing&) 				= delete;
			PostProcessing& operator=(const PostProcessing&)	= delete;

			// Applies the configured Post Processing Effect on the image
			// cmd is provided by the host
			// src and dst must:
			// - have the configured size (resize() if needed)
			// - be in VK_IMAGE_LAYOUT_GENERAL, and 
			// - src needs VK_IMAGE_USAGE_TRANSFER_SRC_BIT, dst needs VK_IMAGE_USAGE_TRANSFER_DST_BIT 
			// - src and dst can be the same image
			// - the call has to be outside of a render pass
			// frameInFlight Image Index (of the Vulkan Application)
			// The effects are applied in the same order they were added
			void apply(VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t fifIndex = 0);

			// Recreates the internal images in the new size
			// The caller must ensure the GPU is no longer using hte old images
			// e.g. via vkDeviceWaitIdle or by waiting on the frame's fence.
			void resize(uint32_t width, uint32_t height);

			// add effect
			void 				addInvert();
			// Multiple instances share the same settings
			GreyscaleSettings& 	addGreyscale();
			VignetteSettings& 	addVignette();
			FilmGrainSettings& 	addFilmGrain();
			ChromaticSettings& addChromatic();
			TonemapSettings& 	addTonemap();

		private:
			VkDevice m_device;
			VkPhysicalDevice m_physicalDevice;
			uint32_t	m_width;
			uint32_t	m_height;
			uint32_t	m_framesInFlight{1};

			std::vector<VkImage>			m_images;
			std::vector<VkDeviceMemory>	m_imageMemorys;
			std::vector<VkImageView>		m_imageViews;

			VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
			VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
			std::vector<VkDescriptorSet> m_descriptorSets;
			VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};

			void createImages();
			void destroyImages();
			void writeDescriptorSets();

			// ein Effekt: eine Pipeline plus ein Zeiger auf seine Parameter
			struct Effect {
				VkPipeline pipeline{VK_NULL_HANDLE};
				const void* params{nullptr};
				uint32_t paramSize{0};
			};

			std::vector<Effect>	m_effects;
			GreyscaleSettings	m_greyscaleSettings;
			VignetteSettings	m_vignetteSettings;
			FilmGrainSettings	m_filmGrainSettings;
			ChromaticSettings	m_chromaticSettings;
			TonemapSettings 	m_tonemapSettings;
	};

}
