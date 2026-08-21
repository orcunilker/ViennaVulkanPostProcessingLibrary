#include "VVPPL.h"
#include <stdexcept>
#include <string>
#include <invert_spv.h>


namespace {
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
}
namespace vvppl {


	PostProcessing::PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height)
        : m_device(device), m_physicalDevice(physicalDevice), m_width(width), m_height(height)
    {
        VkResult res;
        // ### images anlegen - in einer schleife

        for(int i = 0; i < 2; ++i){
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            imageInfo.extent = { width, height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            res = vkCreateImage(device, &imageInfo, nullptr, &m_images[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImage failed: " + std::to_string(res));
            }

            // vkCreateImage legt keinen Speicher an - den besorgen und binden wir selbst
            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(device, m_images[i], &memReq);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize =  memReq.size;
            allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            res = vkAllocateMemory(device, &allocInfo, nullptr, &m_imageMemorys[i]);
            if (res!=VK_SUCCESS) {
                throw std::runtime_error("vkAllocateMemory failed: " + std::to_string(res));
            }

            vkBindImageMemory(device, m_images[i], m_imageMemorys[i], 0);

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

            res = vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]);
            if (res != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImageView failed: " + std::to_string(res));
            }
            // std::cout << "image ok\n";
        }



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
        poolSize.descriptorCount = 2;

        // der pool ist der Speicher, aus dem descroptor sets allozieert werden
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.maxSets	= 1;
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
        setAlloc.descriptorSetCount	= 1;
        setAlloc.pSetLayouts	= &m_descriptorSetLayout;

        res = vkAllocateDescriptorSets(device, &setAlloc, &m_descriptorSet);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkAllocateDescriptorSets failed: " + std::to_string(res));
        }


        // welches konkrete Image gemeint ist und in welchem layout es beim Zugriff sein wird
        VkDescriptorImageInfo imgInfos[2]{};
        imgInfos[0].imageView	= m_imageViews[0];
        imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        imgInfos[1].imageView	= m_imageViews[1];
        imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // schreibt das Image in Binding 0 des Sets
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType		        = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet	        = m_descriptorSet;
        writes[0].dstBinding	    = 0;
        writes[0].descriptorCount 	= 1;
        writes[0].descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo		= &imgInfos[0];

        writes[1]                   = writes[0];
        writes[1].dstBinding	    = 1;
        writes[1].pImageInfo		= &imgInfos[1];

        vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
        // std::cout << "descriptor ok\n";



        // ### compute pipeline

        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize	= invert_spv_sizeInBytes;
        shaderInfo.pCode	= invert_spv;

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        res = vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateShaderModule failed: " + std::to_string(res));
        }

        // das Pipeline Layout sagt, welche Descriptor Sets die Pipeline erwartet
        VkPipelineLayoutCreateInfo pipeLayoutInfo{};
        pipeLayoutInfo.sType	= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeLayoutInfo.setLayoutCount	= 1;
        pipeLayoutInfo.pSetLayouts	= &m_descriptorSetLayout;

        res = vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_pipelineLayout);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreatePipelineLayout failed: " + std::to_string(res));
        } 

        // die Compute Pipeline besteht nur aus einem Shader und diesem Layout
        VkComputePipelineCreateInfo pipeInfo{};
        pipeInfo.sType		= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.stage	= VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.module	= shaderModule;
        pipeInfo.stage.pName	= "main";
        pipeInfo.layout			= m_pipelineLayout;

        res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateComputePipelines failed: " + std::to_string(res));
        }
        // braucht man nicht mehr, pipeline hält shader code selbst
        vkDestroyShaderModule(device, shaderModule, nullptr);

    }



    PostProcessing::~PostProcessing(){
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        for (int i = 0; i < 2; i++)
        {  
            vkDestroyImageView(m_device, m_imageViews[i], nullptr);
            vkDestroyImage(m_device, m_images[i], nullptr);
            vkFreeMemory(m_device, m_imageMemorys[i], nullptr);
        }
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
        // 2 weil image 0 zum lesen und image 1 zum schreiben bereit gemacht wird
        VkImageMemoryBarrier dispatchBarrier[2];
        dispatchBarrier[0]                = blitInBarrier;
        dispatchBarrier[0].oldLayout      = VK_IMAGE_LAYOUT_GENERAL;
        dispatchBarrier[0].newLayout      = VK_IMAGE_LAYOUT_GENERAL;
        dispatchBarrier[0].srcAccessMask  = VK_ACCESS_TRANSFER_WRITE_BIT;
        dispatchBarrier[0].dstAccessMask  = VK_ACCESS_SHADER_READ_BIT;

        dispatchBarrier[1]                = blitInBarrier;
        dispatchBarrier[1].image          = m_images[1];
        dispatchBarrier[1].oldLayout      = VK_IMAGE_LAYOUT_UNDEFINED;
        dispatchBarrier[1].newLayout      = VK_IMAGE_LAYOUT_GENERAL;
        dispatchBarrier[1].srcAccessMask  = 0;
        dispatchBarrier[1].dstAccessMask  = VK_ACCESS_SHADER_WRITE_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 2, dispatchBarrier);
        
        // bind und dispatch
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, (m_width + 7) / 8, (m_height + 7) / 8, 1); // dieses führt den shader aus
        

        // Shader fertig, jetzt zurück ins zielbild
        VkImageMemoryBarrier blitOutBarrier = dispatchBarrier[1];
        blitOutBarrier.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        blitOutBarrier.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        blitOutBarrier.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &blitOutBarrier);
        
        // und bild 1 wieder von lokal raus zum dst blitten
        vkCmdBlitImage(cmd, m_images[1], VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL,
            1, &blit, VK_FILTER_NEAREST);
    }

}