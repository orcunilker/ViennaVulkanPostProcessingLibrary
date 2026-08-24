#include "VVPPL.h"
#include <stdexcept>
#include <string>
#include <invert_spv.h>
#include <greyscale_spv.h>
#include <vignette_spv.h>
#include <filmgrain_spv.h>
#include <chromatic_spv.h>
#include <tonemap_spv.h>


namespace { // anonymer namespace, damit es nur in diesem file sichtbar ist
    
    // kopiert aus testlib.cpp
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

    VkPipeline createPipeline(VkDevice device, VkPipelineLayout layout, const uint32_t* code, size_t sizeInBytes){
        
        // ### compute pipeline

        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize	= sizeInBytes;
        shaderInfo.pCode	= code;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VkResult res = vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateShaderModule failed: " + std::to_string(res));
        }

        // die Compute Pipeline besteht nur aus einem Shader und diesem Layout
        VkComputePipelineCreateInfo pipeInfo{};
        pipeInfo.sType		= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.stage	= VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.module	= shaderModule;
        pipeInfo.stage.pName	= "main";
        pipeInfo.layout			= layout;

        VkPipeline pipeline{};
        res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateComputePipelines failed: " + std::to_string(res));
        }
        // braucht man nicht mehr, pipeline hält shader code selbst
        vkDestroyShaderModule(device, shaderModule, nullptr);

        return pipeline;
    }


}
namespace vvppl {

    // createImages, destroyImages und writeDescriptorSets wurden in eine eigen Klasse gelegt, damit resize möglich ist
    // da müssen nämlich bilder zerstört und wieder erstellt werden.
    // writeDescriptorSets, benötigt man, damit die descriptorsets auf diese neuen Images geupdated werden
    void PostProcessing::createImages(){
        VkResult res;

        // ### images anlegen - in einer schleife
        for(int i = 0; i < 2; ++i){
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            imageInfo.extent = { m_width, m_height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            res = vkCreateImage(m_device, &imageInfo, nullptr, &m_images[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImage failed: " + std::to_string(res));
            }

            // vkCreateImage legt keinen Speicher an - den besorgen und binden wir selbst
            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(m_device, m_images[i], &memReq);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize =  memReq.size;
            allocInfo.memoryTypeIndex = findMemoryType(m_physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            res = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_imageMemorys[i]);
            if (res!=VK_SUCCESS) {
                throw std::runtime_error("vkAllocateMemory failed: " + std::to_string(res));
            }

            vkBindImageMemory(m_device, m_images[i], m_imageMemorys[i], 0);

            // der View beschreibt, wie ein Shader auf das Image schaut
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType	= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image	= m_images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format	= VK_FORMAT_R16G16B16A16_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            res = vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageViews[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImageView failed: " + std::to_string(res));
            }
            // std::cout << "image ok\n";
        }
    }
    void PostProcessing::destroyImages(){
        for (int i = 0; i < 2; i++)
        {  
            vkDestroyImageView(m_device, m_imageViews[i], nullptr);
            vkDestroyImage(m_device, m_images[i], nullptr);
            vkFreeMemory(m_device, m_imageMemorys[i], nullptr);
        }
    }
    void PostProcessing::writeDescriptorSets(){
        // welches konkrete Image gemeint ist und in welchem layout es beim Zugriff sein wird
        VkDescriptorImageInfo imgInfos[2]{};
        imgInfos[0].imageView	= m_imageViews[0];
        imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        imgInfos[1].imageView	= m_imageViews[1];
        imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // legt Binding 0 und 1 fest für shader fest
        VkWriteDescriptorSet writes[4]{};
        writes[0].sType		        = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount 	= 1;
        writes[0].dstSet	        = m_descriptorSets[0];
        writes[0].pImageInfo		= &imgInfos[0];
        writes[0].dstBinding	    = 0;

        writes[1]                   = writes[0];
        writes[1].dstSet	        = m_descriptorSets[0];
        writes[1].pImageInfo		= &imgInfos[1];
        writes[1].dstBinding	    = 1;

        writes[2]                   = writes[0];
        writes[2].dstSet	        = m_descriptorSets[1];
        writes[2].pImageInfo		= &imgInfos[0];
        writes[2].dstBinding	    = 1;

        writes[3]                   = writes[0];
        writes[3].dstSet	        = m_descriptorSets[1];
        writes[3].pImageInfo		= &imgInfos[1];
        writes[3].dstBinding	    = 0;

        vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);
        // std::cout << "descriptor ok\n";
    }

    // Konstruktor
	PostProcessing::PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height)
        : m_device(device), m_physicalDevice(physicalDevice), m_width(width), m_height(height)
    {
        VkResult res;
        
        createImages();

        // ### descriptor set layout + pool + set, storage image einbinden

        // beschreibt eine einzelne ressource binding 0, ein storage image, sichtbar im compute-shader
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding			= 0;
        bindings[0].descriptorType 	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount	= 1;
        bindings[0].stageFlags		= VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1]                 = bindings[0];
        bindings[1].binding         = 1;

        // das layout fasst alle bidnings zusammen - die form eiens descriptor sets
        VkDescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount	= 2;
        layoutInfo.pBindings	= bindings;

        res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateDescriptorSetLayout failed: " + std::to_string(res));
        }

        // wie viele descriptors welchen tryps des pool insegesamt vorhalten muss
        VkDescriptorPoolSize poolSize{};
        poolSize.type			= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize.descriptorCount = 4;

        // der pool ist der Speicher, aus dem descroptor sets allozieert werden
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.maxSets	= 2;
        descPoolInfo.poolSizeCount	= 1;
        descPoolInfo.pPoolSizes = &poolSize;

        res = vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &m_descriptorPool);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateDescriptorPool failed: " + std::to_string(res));
        }

        // ein Set nach diesem layout aus dem pool holen
        VkDescriptorSetAllocateInfo setAlloc{};
        setAlloc.sType			= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAlloc.descriptorPool	= m_descriptorPool;
        setAlloc.descriptorSetCount	= 2;
        // dadruch dass wir für jeden set ein layout übergeben müssen einfach zwei mal das gleiche in einem array
        VkDescriptorSetLayout setLayouts[2] {m_descriptorSetLayout, m_descriptorSetLayout}; 
        setAlloc.pSetLayouts	= setLayouts;

        res = vkAllocateDescriptorSets(device, &setAlloc, m_descriptorSets);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkAllocateDescriptorSets failed: " + std::to_string(res));
        }

        writeDescriptorSets();


        // ## create Pipelines

        // Push constants für parameter für shader
        VkPushConstantRange pushRange{};
        pushRange.stageFlags    = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset        = 0;
        pushRange.size          = 128;

        // das Pipeline Layout sagt, welche Descriptor Sets die Pipeline erwartet
        VkPipelineLayoutCreateInfo pipeLayoutInfo{};
        pipeLayoutInfo.sType	                = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeLayoutInfo.setLayoutCount	        = 1;
        pipeLayoutInfo.pSetLayouts	            = &m_descriptorSetLayout;
        pipeLayoutInfo.pushConstantRangeCount   = 1;
        pipeLayoutInfo.pPushConstantRanges      = &pushRange;

        res = vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_pipelineLayout);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreatePipelineLayout failed: " + std::to_string(res));
        }
    }



    PostProcessing::~PostProcessing(){
        for (size_t i = 0; i < m_effects.size(); i++)
        {  
            vkDestroyPipeline(m_device, m_effects[i].pipeline, nullptr);
        }

        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        destroyImages();
    }


    
    // add effects fncs
    void PostProcessing::addInvert() {
        m_effects.push_back({
            createPipeline(m_device, m_pipelineLayout, invert_spv, invert_spv_sizeInBytes),
            nullptr,
            0
        });
    }

    GreyscaleSettings& PostProcessing::addGreyscale() {
        m_effects.push_back({
            createPipeline(m_device, m_pipelineLayout, greyscale_spv, greyscale_spv_sizeInBytes),
            &m_greyscaleSettings,
            sizeof(GreyscaleSettings)
        });
        return m_greyscaleSettings;
    }

    VignetteSettings& PostProcessing::addVignette() {
        m_effects.push_back({
            createPipeline(m_device, m_pipelineLayout, vignette_spv, vignette_spv_sizeInBytes),
            &m_vignetteSettings,
            sizeof(VignetteSettings)
        });
        return m_vignetteSettings;
    }

	FilmGrainSettings& PostProcessing::addFilmGrain() {
		m_effects.push_back({
			createPipeline(m_device, m_pipelineLayout, filmgrain_spv, filmgrain_spv_sizeInBytes),
			&m_filmGrainSettings,
			sizeof(FilmGrainSettings)
		});
		return m_filmGrainSettings;
	}

	ChromaticSettings& PostProcessing::addChromatic() {
		m_effects.push_back({
			createPipeline(m_device, m_pipelineLayout, chromatic_spv, chromatic_spv_sizeInBytes),
			&m_chromaticSettings,
			sizeof(ChromaticSettings)
		});
		return m_chromaticSettings;
	}

	TonemapSettings& PostProcessing::addTonemap() {
		m_effects.push_back({
			createPipeline(m_device, m_pipelineLayout, tonemap_spv, tonemap_spv_sizeInBytes),
			&m_tonemapSettings,
			sizeof(TonemapSettings)
		});
		return m_tonemapSettings;
	}


    void PostProcessing::apply(VkCommandBuffer cmd, VkImage src, VkImage dst){

        VkImageSubresourceRange range{};
        range.aspectMask    = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        // eigenes Bild für den Blit vorbereiten, Inhalt wird ohnehin komplett überschrieben
        VkImageMemoryBarrier blitInBarrier{};
        blitInBarrier.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        blitInBarrier.oldLayout             = VK_IMAGE_LAYOUT_UNDEFINED;
        blitInBarrier.newLayout             = VK_IMAGE_LAYOUT_GENERAL;
        blitInBarrier.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        blitInBarrier.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        blitInBarrier.image                 = m_images[0];
        blitInBarrier.srcAccessMask         = 0; // war noch kein zugriff darauf, kein cache(?)
        blitInBarrier.dstAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT; // wird jetzt für transfer write verwendet
        blitInBarrier.subresourceRange      = range;

        // die Library weiss nicht, wer das Quellbild beschrieben hat - daher konservativ
        VkImageMemoryBarrier srcReadyBarrier{};
        srcReadyBarrier.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcReadyBarrier.oldLayout             = VK_IMAGE_LAYOUT_GENERAL;
        srcReadyBarrier.newLayout             = VK_IMAGE_LAYOUT_GENERAL;
        srcReadyBarrier.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        srcReadyBarrier.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        srcReadyBarrier.image                 = src;
        // access mask: die gpu hat verschiedene caches
        // man muss ihm sagen, in welchem cache er vorher gearbeitet hat
        // , damit er weiß wo die neuesten daten liegen
        // es kann nämlich sein, dass er die daten noch nicht in den vram geschrieben hat
        srcReadyBarrier.srcAccessMask         = VK_ACCESS_MEMORY_WRITE_BIT; // memory write = irgendwein write war davor
        srcReadyBarrier.dstAccessMask         = VK_ACCESS_TRANSFER_READ_BIT; // wird für transfer read verwendet
        srcReadyBarrier.subresourceRange      = range;

        VkImageMemoryBarrier before [] = {blitInBarrier, srcReadyBarrier};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
            0, 0, nullptr, 0, nullptr, 2, before);


        // quellbild ins eigene rgba16f-bild kopieren, der Blit konvertiert das Format
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel        = 0;
        blit.srcSubresource.baseArrayLayer  = 0;
        blit.srcSubresource.layerCount      = 1;
        blit.srcOffsets[0]                  = { 0, 0, 0};
        blit.srcOffsets[1]                  = { (int32_t)m_width, (int32_t)m_height, 1};
        blit.dstSubresource                 = blit.srcSubresource;
        blit.dstOffsets[0]                  = blit.srcOffsets[0];
        blit.dstOffsets[1]                  = blit.srcOffsets[1];

        vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, m_images[0], VK_IMAGE_LAYOUT_GENERAL, 
            1, &blit, VK_FILTER_NEAREST);

        // blit fertig, jetzt darf der shader ran

        // TODO falls pipelines leer -> src zu dst blitten

        // ## jeden shader/pipeline anwenden
        // ping pong, immer von einen Image ins andere den nächsten shader/pipeline anwenden
        VkImageMemoryBarrier dispatchBarrier[2];
        for (size_t i = 0; i < m_effects.size(); i++)
        {        
            dispatchBarrier[0]                = blitInBarrier;
            dispatchBarrier[0].image          = m_images[i % 2];
            dispatchBarrier[0].oldLayout      = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[0].newLayout      = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[0].srcAccessMask  = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            dispatchBarrier[0].dstAccessMask  = VK_ACCESS_SHADER_READ_BIT;

            dispatchBarrier[1]                = blitInBarrier;
            dispatchBarrier[1].image          = m_images[(i+1) % 2];
            dispatchBarrier[1].oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
            dispatchBarrier[1].newLayout      = VK_IMAGE_LAYOUT_GENERAL;
            dispatchBarrier[1].srcAccessMask  = 0;
            dispatchBarrier[1].dstAccessMask  = VK_ACCESS_SHADER_WRITE_BIT;
            
            vkCmdPipelineBarrier(cmd, 
                VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 2, dispatchBarrier);
            
            // bind und dispatch
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_effects[i].pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 
                0, 1, &m_descriptorSets[i % 2], 0, nullptr);
            // falls pushconstants für effect vorhanden
            if(m_effects[i].paramSize > 0) {
                vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                    0, m_effects[i].paramSize, m_effects[i].params);
            }
            vkCmdDispatch(cmd, (m_width + 7) / 8, (m_height + 7) / 8, 1); // dieses führt den shader aus
        }        
        const size_t lastImage = m_effects.size() % 2;

        // Shader fertig, jetzt zurück ins zielbild
        VkImageMemoryBarrier blitOutBarrier = dispatchBarrier[1];
        blitOutBarrier.image            = m_images[lastImage];
        blitOutBarrier.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        blitOutBarrier.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &blitOutBarrier);
        
        // und das bild auf dem zuletzt gerendert wurde wieder von lokal raus zum dst blitten
        vkCmdBlitImage(cmd, m_images[lastImage], VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL,
            1, &blit, VK_FILTER_NEAREST);
    }

    void PostProcessing::resize(uint32_t width, uint32_t height) {
        destroyImages();
        m_width = width;
        m_height = height;
        createImages();
        writeDescriptorSets();
    }

}