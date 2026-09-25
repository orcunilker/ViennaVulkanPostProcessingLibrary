/**
 * @file VVPPL.h
 * @brief Public interface of VVPPL, the Vienna Vulkan Post-Processing Library.
 *
 * The library records a chain of compute shader effects into a command buffer of the host application.
 * It owns only its intermediate images, descriptor sets and pipelines. Hosts that load Vulkan with volk
 * build it with the CMake option VVPPL_USE_VOLK.
 */
#pragma once

#ifdef VVPPL_USE_VOLK
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif

#include <cstdint>
#include <vector>

/// @brief Namespace of the library.
namespace vvppl {

    /// @brief Greyscale: replaces the color by its luminance (Rec. 709 weights).
    struct GreyscaleSettings {
        float strength{1.0f}; ///< Blends from the original (0) to full greyscale (1).
    };

    /// @brief Vignette: darkens the image towards the corners. Distances are in image heights from the center.
    struct VignetteSettings {
        float intensity{0.5};  ///< Blends from no effect (0) to the full falloff down to black (1).
        float radius{0.4};     ///< Distance at which the falloff starts.
        float smoothness{0.3}; ///< Width of the falloff.
    };

    /// @brief Film grain: adds random noise to every pixel.
    struct FilmGrainSettings {
        float intensity{0.08f}; ///< Amplitude of the noise, which lies in -intensity / 2 .. intensity / 2.
        float time{0.0f};       ///< Running time of the host in seconds, seeds the noise.
    };

    /// @brief Chromatic aberration: splits red and blue apart towards the border.
    struct ChromaticSettings {
        float intensity{0.005f}; ///< Shift per distance from the center, 0.01 shifts by 0.5 % at the border.
    };

    /// @brief Tone mapping: maps the HDR color to 0..1 with the ACES approximation by Narkowicz.
    struct TonemapSettings {
        float exposure{1.0f}; ///< Scales the color before the curve.
    };

    /// @brief Color grading: gain and lift, then gamma, contrast and saturation. The defaults change nothing.
    struct ColorGradeSettings {
        float saturation{1.0f};           ///< Scales the distance from the luminance, 0 is greyscale.
        float contrast{1.0f};             ///< Scales the distance from middle grey 0.5.
        float lift[3]{0.0f, 0.0f, 0.0f};  ///< Added per RGB channel.
        float gamma[3]{1.0f, 1.0f, 1.0f}; ///< Per RGB channel, the color becomes color^(1 / gamma).
        float gain[3]{1.0f, 1.0f, 1.0f};  ///< Multiplied per RGB channel.
    };

    /// @brief Dithering: adds a 4x4 Bayer pattern that hides banding in 8-bit output.
    struct DitherSettings {
        float strength{1.0f}; ///< Scales the pattern, at 1 it spans up to half an 8-bit sRGB step.
    };

    /// @brief Solarization: inverts every channel above a threshold.
    struct SolarizeSettings {
        float threshold{0.5f}; ///< Channels above become 1 - value, values above 1.0 become black.
    };

    /// @brief Sabattier effect: mirrors every channel above a threshold back down.
    struct SabattierSettings {
        float threshold{0.5f}; ///< Channels above become 2 * threshold - value.
        float strength{0.75f}; ///< Blends from the original (0) to the full reversal (1).
    };

    /// @brief Emboss: 3x3 relief filter along the diagonal.
    struct EmbossSettings {
        float strength{1.0f}; ///< Blends from the original (0) to the relief (1).
    };

    /// @brief Sobel edge detection on the luminance.
    struct SobelSettings {
        float strength{1.0f}; ///< Blends from the original (0) to white edges on black (1).
    };

    /// @brief Speed lines: white radial lines that change 15 times per second.
    struct SpeedLinesSettings {
        float intensity{0.5f};  ///< Blends towards white.
        float lineCount{60.0f}; ///< Number of lines.
        float radius{0.25f};    ///< Start distance from the center in image heights, plus a random 0..0.3.
        float time{0.0f};       ///< Running time of the host in seconds.
    };

    /// @brief Color highlight: keeps colors close to a key color and desaturates the rest.
    struct HighlightSettings {
        float key[3]{1.0f, 0.0f, 0.0f}; ///< RGB color to keep; only its direction counts, not its brightness.
        float tolerance{0.1f};          ///< Allowed 1 - cosine between a color and the key.
        float strength{1.0f};           ///< Blends the rest from the original (0) to greyscale (1).
    };

    /// @brief Hue segmentation: replaces every color by the nearest of a few fully saturated hues.
    struct SegmentationSettings {
        float segments{6.0f};   ///< Number of hues, starting at red.
        float minChroma{0.05f}; ///< Colors with less chroma become middle grey (0.18).
        float strength{1.0f};   ///< Blends from the original (0) to the segment colors (1).
    };

    /**
     * @brief Applies a chain of post-processing effects to an image of the host.
     *
     * add<Effect>() returns a reference to the settings of an effect. apply() records them as push constants,
     * so a change takes effect with the next apply(). Colors are linear RGB and can be above 1.0 before tone
     * mapping. Needs Vulkan 1.1 (SPIR-V 1.3). Not thread-safe.
     */
    class PostProcessing {
    public:
        /**
         * @brief Creates the intermediate images, the descriptor sets and the pipeline layout.
         *
         * @param device          Device that all objects of the library are created with.
         * @param physicalDevice  Only used to find memory types.
         * @param width,height    Size of the images passed to apply().
         * @param framesInFlight  Number of command buffers with an apply() that can execute at the same time.
         * @throws std::runtime_error if a Vulkan object cannot be created.
         */
        PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                       uint32_t framesInFlight = 1);

        /// @brief Destroys all Vulkan objects.
        /// @pre The GPU has finished every command buffer that apply() recorded into.
        ~PostProcessing();

        /// Not copyable: the instance owns raw Vulkan handles, a copy would destroy them twice.
        PostProcessing(const PostProcessing&) = delete;
        PostProcessing& operator=(const PostProcessing&) = delete;

        /**
         * @brief Records the effect chain into @p cmd.
         *
         * Copies @p src into an internal rgba16f image, runs one compute dispatch per effect and copies the
         * result into @p dst; an empty chain is a plain copy. apply() waits for all earlier writes to @p src
         * itself, but places no barrier on @p dst, so the host prepares a different @p dst itself. Afterwards
         * @p dst was last written by a transfer (VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT).
         *
         * @param cmd         Command buffer of the host in the recording state.
         * @param src         Image to read, needs VK_IMAGE_USAGE_TRANSFER_SRC_BIT.
         * @param dst         Image to write, needs VK_IMAGE_USAGE_TRANSFER_DST_BIT, may be @p src.
         * @param frameIndex  Frame in flight of the host, less than framesInFlight.
         * @pre @p cmd is outside of a render pass or dynamic rendering; its queue family supports graphics and compute.
         * @pre @p src and @p dst have the configured size, are in VK_IMAGE_LAYOUT_GENERAL, are single-sampled
         *      and have a color format that supports blits.
         * @pre Frames in flight at the same time use different values of @p frameIndex.
         */
        void apply(VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t frameIndex = 0);

        /**
         * @brief Recreates the intermediate images in a new size; the chain and the settings stay.
         * @pre The GPU no longer uses the old images: vkDeviceWaitIdle or the fences of all frames in flight.
         * @throws std::runtime_error if a Vulkan object cannot be created.
         */
        void resize(uint32_t width, uint32_t height);

        /**
         * @name Effect chain
         * add returns the settings of an effect, remove takes it out of the chain. The chain always runs in the
         * order of this list, no matter in which order the effects were added (lens effects on the HDR image
         * first, dithering last). An effect is in the chain at most once. Removing keeps its settings and its
         * pipeline, so adding it again is cheap. Both are allowed between any two apply() calls, also while
         * recorded command buffers are in flight. The first add of an effect creates its pipeline and can throw
         * std::runtime_error.
         */
        ///@{
        ChromaticSettings& addChromatic();
        void removeChromatic();
        VignetteSettings& addVignette();
        void removeVignette();
        TonemapSettings& addTonemap();
        void removeTonemap();
        ColorGradeSettings& addColorGrade();
        void removeColorGrade();
        SegmentationSettings& addSegmentation();
        void removeSegmentation();
        HighlightSettings& addHighlight();
        void removeHighlight();
        GreyscaleSettings& addGreyscale();
        void removeGreyscale();
        void addInvert();
        void removeInvert();
        SolarizeSettings& addSolarize();
        void removeSolarize();
        SabattierSettings& addSabattier();
        void removeSabattier();
        EmbossSettings& addEmboss();
        void removeEmboss();
        SobelSettings& addSobel();
        void removeSobel();
        SpeedLinesSettings& addSpeedLines();
        void removeSpeedLines();
        FilmGrainSettings& addFilmGrain();
        void removeFilmGrain();
        DitherSettings& addDither();
        void removeDither();
        ///@}

    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_framesInFlight{1};

        // two ping-pong images per frame in flight, frame i uses the images 2 * i and 2 * i + 1
        std::vector<VkImage> m_images;
        std::vector<VkDeviceMemory> m_imageMemorys;
        std::vector<VkImageView> m_imageViews;

        // two descriptor sets per frame in flight, one for each direction between its two images
        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet> m_descriptorSets;

        // shared by all effect pipelines
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};

        // used by the constructor and by resize(), which replaces the images
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

        // one entry of the chain: its pipeline and a pointer to its settings, which become the push constants
        struct Effect {
            EffectType type{EFFECT_TYPE_COUNT};
            VkPipeline pipeline{VK_NULL_HANDLE};
            const void* params{nullptr};
            uint32_t paramSize{0};
        };

        void addEffect(EffectType type, const uint32_t* code, size_t sizeInBytes, const void* params,
                       uint32_t paramSize);
        void removeEffect(EffectType type);

        // the chain, always sorted by type
        std::vector<Effect> m_effects;
        // one pipeline per effect type, created on the first add and destroyed in the destructor
        VkPipeline m_pipelines[EFFECT_TYPE_COUNT]{};

        // the settings of every effect, also of effects that are not in the chain
        GreyscaleSettings m_greyscaleSettings;
        VignetteSettings m_vignetteSettings;
        FilmGrainSettings m_filmGrainSettings;
        ChromaticSettings m_chromaticSettings;
        TonemapSettings m_tonemapSettings;
        ColorGradeSettings m_colorGradeSettings;
        DitherSettings m_ditherSettings;
        SolarizeSettings m_solarizeSettings;
        SabattierSettings m_sabattierSettings;
        EmbossSettings m_embossSettings;
        SobelSettings m_sobelSettings;
        SpeedLinesSettings m_speedLinesSettings;
        HighlightSettings m_highlightSettings;
        SegmentationSettings m_segmentationSettings;
    };

} // namespace vvppl
