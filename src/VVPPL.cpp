/// @file VVPPL.cpp
/// @brief Implementation of vvppl::PostProcessing.

#include "VVPPL.h"

#include <stdexcept>
#include <string>

// SPIR-V of the effect shaders, compiled from shaders/*.slang at build time (see CMakeLists.txt)
#include <invert_spv.h>
#include <greyscale_spv.h>
#include <vignette_spv.h>
#include <filmgrain_spv.h>
#include <chromatic_spv.h>
#include <tonemap_spv.h>
#include <colorgrade_spv.h>
#include <dither_spv.h>
#include <solarize_spv.h>
#include <sabattier_spv.h>
#include <emboss_spv.h>
#include <sobel_spv.h>
#include <speedlines_spv.h>
#include <highlight_spv.h>
#include <segmentation_spv.h>

// helpers that are only visible in this file
namespace {

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

    /// @brief Creates the compute pipeline of one effect from its SPIR-V code.
    VkPipeline createPipeline(VkDevice device, VkPipelineLayout layout, const uint32_t* code, size_t sizeInBytes) {
        // shader module from the SPIR-V code
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = sizeInBytes;
        shaderInfo.pCode = code;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VkResult res = vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkCreateShaderModule failed: " + std::to_string(res));
        }

        // a compute pipeline consists of one shader stage and the layout shared by all effects
        VkComputePipelineCreateInfo pipeInfo{};
        pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.module = shaderModule;
        pipeInfo.stage.pName = "main";
        pipeInfo.layout = layout;

        VkPipeline pipeline{};
        res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkCreateComputePipelines failed: " + std::to_string(res));
        }

        // the pipeline no longer needs the module once it is created
        vkDestroyShaderModule(device, shaderModule, nullptr);

        return pipeline;
    }

} // namespace

namespace vvppl {

    // creates two rgba16f ping-pong images per frame in flight, each with its memory and view
    void PostProcessing::createImages() {
        VkResult res;

        const uint32_t imageCount = 2 * m_framesInFlight;
        m_images.resize(imageCount);
        m_imageMemorys.resize(imageCount);
        m_imageViews.resize(imageCount);

        for (int i = 0; i < imageCount; ++i) {
            // storage image for the shaders, transfer source and destination for the blits
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            imageInfo.extent = {m_width, m_height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage =
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            res = vkCreateImage(m_device, &imageInfo, nullptr, &m_images[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImage failed: " + std::to_string(res));
            }

            // vkCreateImage allocates no memory, so allocate device-local memory and bind it
            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(m_device, m_images[i], &memReq);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex =
                findMemoryType(m_physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            res = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_imageMemorys[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkAllocateMemory failed: " + std::to_string(res));
            }

            vkBindImageMemory(m_device, m_images[i], m_imageMemorys[i], 0);

            // the descriptor sets refer to the image through this view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            res = vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageViews[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImageView failed: " + std::to_string(res));
            }
        }
    }

    // empties the lists, so that a failing createImages() in resize() cannot destroy the images twice
    void PostProcessing::destroyImages() {
        for (int i = 0; i < m_images.size(); i++) {
            vkDestroyImageView(m_device, m_imageViews[i], nullptr);
            vkDestroyImage(m_device, m_images[i], nullptr);
            vkFreeMemory(m_device, m_imageMemorys[i], nullptr);
        }
        m_imageViews.clear();
        m_images.clear();
        m_imageMemorys.clear();
    }

    // points every descriptor set at its two images: binding 0 is the input, binding 1 the output
    void PostProcessing::writeDescriptorSets() {
        const uint32_t imageCount = 2 * m_framesInFlight;

        // the shaders access every image in VK_IMAGE_LAYOUT_GENERAL
        std::vector<VkDescriptorImageInfo> imgInfos(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            imgInfos[i].imageView = m_imageViews[i];
            imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        // two writes per set, one for each binding
        std::vector<VkWriteDescriptorSet> writes(2 * imageCount);
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            const uint32_t base = i * 2;
            for (uint32_t j = 0; j < 2; ++j) {
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w.descriptorCount = 1;
                w.dstSet = m_descriptorSets[base + j];

                // set base + 0 reads image base + 0 and writes base + 1, set base + 1 the other way round
                w.dstBinding = 0;
                w.pImageInfo = &imgInfos[base + j];
                writes[(base + j) * 2] = w;

                w.dstBinding = 1;
                w.pImageInfo = &imgInfos[base + (1 - j)];
                writes[(base + j) * 2 + 1] = w;
            }
        }

        vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
    }

    PostProcessing::PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                                   uint32_t framesInFlight)
        : m_device(device), m_physicalDevice(physicalDevice), m_width(width), m_height(height),
          m_framesInFlight(framesInFlight) {
        VkResult res;

        createImages();

        // two storage images for the compute shader: binding 0 is read, binding 1 is written
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1] = bindings[0];
        bindings[1].binding = 1;

        // the layout is the same for every descriptor set of the library
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;

        res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorSetLayout failed: " + std::to_string(res));
        }

        // two sets per frame in flight with two storage images each
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize.descriptorCount = 4 * framesInFlight;

        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.maxSets = 2 * framesInFlight;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;

        res = vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &m_descriptorPool);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool failed: " + std::to_string(res));
        }

        // one set for each direction of the ping-pong, per frame in flight
        m_descriptorSets.resize(2 * framesInFlight);

        VkDescriptorSetAllocateInfo setAlloc{};
        setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAlloc.descriptorPool = m_descriptorPool;
        setAlloc.descriptorSetCount = m_descriptorSets.size();
        // vkAllocateDescriptorSets takes one layout per set, here always the same
        std::vector<VkDescriptorSetLayout> setLayouts(m_descriptorSets.size(), m_descriptorSetLayout);
        setAlloc.pSetLayouts = setLayouts.data();

        res = vkAllocateDescriptorSets(device, &setAlloc, m_descriptorSets.data());
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateDescriptorSets failed: " + std::to_string(res));
        }

        writeDescriptorSets();

        // push constants carry the settings of the current effect, 128 bytes is the minimum every device supports
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = 128;

        // one pipeline layout shared by all effects
        VkPipelineLayoutCreateInfo pipeLayoutInfo{};
        pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeLayoutInfo.setLayoutCount = 1;
        pipeLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        pipeLayoutInfo.pushConstantRangeCount = 1;
        pipeLayoutInfo.pPushConstantRanges = &pushRange;

        res = vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_pipelineLayout);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed: " + std::to_string(res));
        }

        // the effect pipelines are created by the first add of each effect
    }

    PostProcessing::~PostProcessing() {
        // also the pipelines of removed effects, never created ones are VK_NULL_HANDLE and ignored
        for (size_t i = 0; i < EFFECT_TYPE_COUNT; i++) {
            vkDestroyPipeline(m_device, m_pipelines[i], nullptr);
        }

        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        destroyImages();
    }

    // inserts an effect at its fixed place in the chain, every effect is in the chain at most once
    void PostProcessing::addEffect(EffectType type, const uint32_t* code, size_t sizeInBytes, const void* params,
                                   uint32_t paramSize) {
        // the pipeline stays until the destructor, so removing an effect never destroys
        // a pipeline that an already recorded command buffer still uses
        if (m_pipelines[type] == VK_NULL_HANDLE) {
            m_pipelines[type] = createPipeline(m_device, m_pipelineLayout, code, sizeInBytes);
        }

        // skip all effects that come before this one
        size_t i = 0;
        while (i < m_effects.size() && m_effects[i].type < type) {
            i++;
        }

        // already in the chain
        if (i < m_effects.size() && m_effects[i].type == type) {
            return;
        }

        m_effects.insert(m_effects.begin() + i, Effect{type, m_pipelines[type], params, paramSize});
    }

    // takes an effect out of the chain, its settings and pipeline stay for a later add
    void PostProcessing::removeEffect(EffectType type) {
        for (size_t i = 0; i < m_effects.size(); i++) {
            if (m_effects[i].type == type) {
                m_effects.erase(m_effects.begin() + i);
                return;
            }
        }
    }

    // add and remove of every effect, in the order of the chain
    ChromaticSettings& PostProcessing::addChromatic() {
        addEffect(EFFECT_CHROMATIC, chromatic_spv, chromatic_spv_sizeInBytes, &m_chromaticSettings,
                  sizeof(ChromaticSettings));
        return m_chromaticSettings;
    }
    void PostProcessing::removeChromatic() { removeEffect(EFFECT_CHROMATIC); }

    VignetteSettings& PostProcessing::addVignette() {
        addEffect(EFFECT_VIGNETTE, vignette_spv, vignette_spv_sizeInBytes, &m_vignetteSettings,
                  sizeof(VignetteSettings));
        return m_vignetteSettings;
    }
    void PostProcessing::removeVignette() { removeEffect(EFFECT_VIGNETTE); }

    TonemapSettings& PostProcessing::addTonemap() {
        addEffect(EFFECT_TONEMAP, tonemap_spv, tonemap_spv_sizeInBytes, &m_tonemapSettings, sizeof(TonemapSettings));
        return m_tonemapSettings;
    }
    void PostProcessing::removeTonemap() { removeEffect(EFFECT_TONEMAP); }

    ColorGradeSettings& PostProcessing::addColorGrade() {
        addEffect(EFFECT_COLOR_GRADE, colorgrade_spv, colorgrade_spv_sizeInBytes, &m_colorGradeSettings,
                  sizeof(ColorGradeSettings));
        return m_colorGradeSettings;
    }
    void PostProcessing::removeColorGrade() { removeEffect(EFFECT_COLOR_GRADE); }

    SegmentationSettings& PostProcessing::addSegmentation() {
        addEffect(EFFECT_SEGMENTATION, segmentation_spv, segmentation_spv_sizeInBytes, &m_segmentationSettings,
                  sizeof(SegmentationSettings));
        return m_segmentationSettings;
    }
    void PostProcessing::removeSegmentation() { removeEffect(EFFECT_SEGMENTATION); }

    HighlightSettings& PostProcessing::addHighlight() {
        addEffect(EFFECT_HIGHLIGHT, highlight_spv, highlight_spv_sizeInBytes, &m_highlightSettings,
                  sizeof(HighlightSettings));
        return m_highlightSettings;
    }
    void PostProcessing::removeHighlight() { removeEffect(EFFECT_HIGHLIGHT); }

    GreyscaleSettings& PostProcessing::addGreyscale() {
        addEffect(EFFECT_GREYSCALE, greyscale_spv, greyscale_spv_sizeInBytes, &m_greyscaleSettings,
                  sizeof(GreyscaleSettings));
        return m_greyscaleSettings;
    }
    void PostProcessing::removeGreyscale() { removeEffect(EFFECT_GREYSCALE); }

    // invert has no settings and therefore no push constants
    void PostProcessing::addInvert() { addEffect(EFFECT_INVERT, invert_spv, invert_spv_sizeInBytes, nullptr, 0); }
    void PostProcessing::removeInvert() { removeEffect(EFFECT_INVERT); }

    SolarizeSettings& PostProcessing::addSolarize() {
        addEffect(EFFECT_SOLARIZE, solarize_spv, solarize_spv_sizeInBytes, &m_solarizeSettings,
                  sizeof(SolarizeSettings));
        return m_solarizeSettings;
    }
    void PostProcessing::removeSolarize() { removeEffect(EFFECT_SOLARIZE); }

    SabattierSettings& PostProcessing::addSabattier() {
        addEffect(EFFECT_SABATTIER, sabattier_spv, sabattier_spv_sizeInBytes, &m_sabattierSettings,
                  sizeof(SabattierSettings));
        return m_sabattierSettings;
    }
    void PostProcessing::removeSabattier() { removeEffect(EFFECT_SABATTIER); }

    EmbossSettings& PostProcessing::addEmboss() {
        addEffect(EFFECT_EMBOSS, emboss_spv, emboss_spv_sizeInBytes, &m_embossSettings, sizeof(EmbossSettings));
        return m_embossSettings;
    }
    void PostProcessing::removeEmboss() { removeEffect(EFFECT_EMBOSS); }

    SobelSettings& PostProcessing::addSobel() {
        addEffect(EFFECT_SOBEL, sobel_spv, sobel_spv_sizeInBytes, &m_sobelSettings, sizeof(SobelSettings));
        return m_sobelSettings;
    }
    void PostProcessing::removeSobel() { removeEffect(EFFECT_SOBEL); }

    SpeedLinesSettings& PostProcessing::addSpeedLines() {
        addEffect(EFFECT_SPEED_LINES, speedlines_spv, speedlines_spv_sizeInBytes, &m_speedLinesSettings,
                  sizeof(SpeedLinesSettings));
        return m_speedLinesSettings;
    }
    void PostProcessing::removeSpeedLines() { removeEffect(EFFECT_SPEED_LINES); }

    FilmGrainSettings& PostProcessing::addFilmGrain() {
        addEffect(EFFECT_FILM_GRAIN, filmgrain_spv, filmgrain_spv_sizeInBytes, &m_filmGrainSettings,
                  sizeof(FilmGrainSettings));
        return m_filmGrainSettings;
    }
    void PostProcessing::removeFilmGrain() { removeEffect(EFFECT_FILM_GRAIN); }

    DitherSettings& PostProcessing::addDither() {
        addEffect(EFFECT_DITHER, dither_spv, dither_spv_sizeInBytes, &m_ditherSettings, sizeof(DitherSettings));
        return m_ditherSettings;
    }
    void PostProcessing::removeDither() { removeEffect(EFFECT_DITHER); }

    // copy in, one dispatch per effect between the two ping-pong images, copy out
    void PostProcessing::apply(VkCommandBuffer cmd, VkImage src, VkImage dst, uint32_t frameIndex) {
        // the two images and descriptor sets of this frame start at base
        const size_t base = 2 * frameIndex;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        // the first internal image is overwritten completely by the blit, so its old content can be discarded
        VkImageMemoryBarrier blitInBarrier{};
        blitInBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        blitInBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        blitInBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        blitInBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blitInBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blitInBarrier.image = m_images[base];
        blitInBarrier.srcAccessMask = 0;
        blitInBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        blitInBarrier.subresourceRange = range;

        // the library does not know which command wrote src, so it waits for every earlier write
        // and makes it visible to the blit's read
        VkImageMemoryBarrier srcReadyBarrier{};
        srcReadyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcReadyBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcReadyBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcReadyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcReadyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcReadyBarrier.image = src;
        srcReadyBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        srcReadyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcReadyBarrier.subresourceRange = range;

        VkImageMemoryBarrier before[] = {blitInBarrier, srcReadyBarrier};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 2, before);

        // copy src into the first internal image, the blit converts the format to rgba16f
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = 0;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {(int32_t)m_width, (int32_t)m_height, 1};
        blit.dstSubresource = blit.srcSubresource;
        blit.dstOffsets[0] = blit.srcOffsets[0];
        blit.dstOffsets[1] = blit.srcOffsets[1];

        vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, m_images[base], VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                       VK_FILTER_NEAREST);

        // one dispatch per effect, each reads one ping-pong image and writes the other;
        // with an empty chain the result of the first blit is copied out directly
        VkImageMemoryBarrier dispatchBarrier[2]{};
        for (size_t i = 0; i < m_effects.size(); i++) {
            // input: wait for the blit or the dispatch that wrote it
            dispatchBarrier[0] = blitInBarrier;
            dispatchBarrier[0].image = m_images[base + (i % 2)];
            dispatchBarrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            dispatchBarrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            // output: wait until earlier reads are done, its old content is discarded
            dispatchBarrier[1] = blitInBarrier;
            dispatchBarrier[1].image = m_images[base + ((i + 1) % 2)];
            dispatchBarrier[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dispatchBarrier[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[1].srcAccessMask = 0;
            dispatchBarrier[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, dispatchBarrier);

            // descriptor set base + i % 2 reads image base + i % 2 and writes the other one
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_effects[i].pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1,
                                    &m_descriptorSets[base + (i % 2)], 0, nullptr);
            // effects without settings have no push constants
            if (m_effects[i].paramSize > 0) {
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, m_effects[i].paramSize,
                                   m_effects[i].params);
            }
            // one invocation per pixel in 8x8 workgroups, rounded up to cover the whole image
            vkCmdDispatch(cmd, (m_width + 7) / 8, (m_height + 7) / 8, 1);
        }
        const size_t lastImage = base + (m_effects.size() % 2);

        // wait for the last dispatch, or for the first blit if the chain is empty
        VkImageMemoryBarrier blitOutBarrier = blitInBarrier;
        blitOutBarrier.image = m_images[lastImage];
        blitOutBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        blitOutBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &blitOutBarrier);

        // copy the result into dst, the blit converts back to the format of dst
        vkCmdBlitImage(cmd, m_images[lastImage], VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                       VK_FILTER_NEAREST);
    }

    void PostProcessing::resize(uint32_t width, uint32_t height) {
        destroyImages();
        m_width = width;
        m_height = height;
        createImages();
        writeDescriptorSets();
    }

} // namespace vvppl
