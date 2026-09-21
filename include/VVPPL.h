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
	struct ColorGradeSettings {
		float saturation{1.0f};
		float contrast{1.0f};
		float lift[3]{0.0f, 0.0f, 0.0f};
		float gamma[3]{1.0f, 1.0f, 1.0f};
		float gain[3]{1.0f, 1.0f, 1.0f};
	};
	struct DitherSettings {
		float strength{1.0f};
	};
	struct SolarizeSettings {
		float threshold{0.5f};
	};
	struct SabattierSettings {
		float threshold{0.5f};
		float strength{0.75f};
	};
	struct EmbossSettings {
		float strength{1.0f};
	};
	struct SobelSettings {
		float strength{1.0f};
	};
	struct SpeedLinesSettings {
		float intensity{0.5f};
		float lineCount{60.0f};
		float radius{0.25f};
		float time{0.0f};
	};
	struct HighlightSettings {
		float key[3]{1.0f, 0.0f, 0.0f};
		float tolerance{0.1f};
		float strength{1.0f};
	};
	struct SegmentationSettings {
		float segments{6.0f};
		float minChroma{0.05f};
		float strength{1.0f};
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
			// The effects are applied in the fixed order of the add/remove list below
			void apply(VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t fifIndex = 0);

			// Recreates the internal images in the new size
			// The caller must ensure the GPU is no longer using hte old images
			// e.g. via vkDeviceWaitIdle or by waiting on the frame's fence.
			void resize(uint32_t width, uint32_t height);

			// add and remove effects
			// The chain is always applied in the order of this list, no matter in which order effects are added:
			// lens effects on the HDR image, tone mapping, color correction, effects that read the hue,
			// effects that discard or reverse colors, neighborhood effects, overlay, noise, dithering last.
			// Every effect is in the chain at most once, adding it again only returns its settings.
			// Removing keeps the settings and the pipeline, so a removed effect can be added again cheaply.
			// Both are allowed between two apply() calls, also while recorded command buffers are in flight.
			ChromaticSettings& 	addChromatic();
			void 				removeChromatic();
			VignetteSettings& 	addVignette();
			void 				removeVignette();
			TonemapSettings& 	addTonemap();
			void 				removeTonemap();
			ColorGradeSettings& addColorGrade();
			void 				removeColorGrade();
			SegmentationSettings& addSegmentation();
			void 				removeSegmentation();
			HighlightSettings& 	addHighlight();
			void 				removeHighlight();
			GreyscaleSettings& 	addGreyscale();
			void 				removeGreyscale();
			void 				addInvert();
			void 				removeInvert();
			SolarizeSettings& 	addSolarize();
			void 				removeSolarize();
			SabattierSettings& 	addSabattier();
			void 				removeSabattier();
			EmbossSettings& 	addEmboss();
			void 				removeEmboss();
			SobelSettings& 		addSobel();
			void 				removeSobel();
			SpeedLinesSettings& addSpeedLines();
			void 				removeSpeedLines();
			FilmGrainSettings& 	addFilmGrain();
			void 				removeFilmGrain();
			DitherSettings& 	addDither();
			void 				removeDither();

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

			// the fixed place of every effect in the chain, same order as the add/remove list above
			enum EffectType : uint32_t {
				EFFECT_CHROMATIC,
				EFFECT_VIGNETTE,
				EFFECT_TONEMAP,
				EFFECT_COLOR_GRADE,
				EFFECT_SEGMENTATION,
				EFFECT_HIGHLIGHT,
				EFFECT_GREYSCALE,
				EFFECT_INVERT,
				EFFECT_SOLARIZE,
				EFFECT_SABATTIER,
				EFFECT_EMBOSS,
				EFFECT_SOBEL,
				EFFECT_SPEED_LINES,
				EFFECT_FILM_GRAIN,
				EFFECT_DITHER,
				EFFECT_TYPE_COUNT
			};

			// ein Effekt: eine Pipeline plus ein Zeiger auf seine Parameter
			struct Effect {
				EffectType type{EFFECT_TYPE_COUNT};
				VkPipeline pipeline{VK_NULL_HANDLE};
				const void* params{nullptr};
				uint32_t paramSize{0};
			};

			void addEffect(EffectType type, const uint32_t* code, size_t sizeInBytes, const void* params, uint32_t paramSize);
			void removeEffect(EffectType type);

			// the chain, always sorted by type
			std::vector<Effect>	m_effects;
			// one pipeline per effect type, created on the first add and destroyed in the destructor
			VkPipeline m_pipelines[EFFECT_TYPE_COUNT]{};
			GreyscaleSettings	m_greyscaleSettings;
			VignetteSettings	m_vignetteSettings;
			FilmGrainSettings	m_filmGrainSettings;
			ChromaticSettings	m_chromaticSettings;
			TonemapSettings 	m_tonemapSettings;
			ColorGradeSettings 	m_colorGradeSettings;
			DitherSettings		m_ditherSettings;
			SolarizeSettings	m_solarizeSettings;
			SabattierSettings	m_sabattierSettings;
			EmbossSettings		m_embossSettings;
			SobelSettings		m_sobelSettings;
			SpeedLinesSettings	m_speedLinesSettings;
			HighlightSettings	m_highlightSettings;
			SegmentationSettings m_segmentationSettings;
	};

}
