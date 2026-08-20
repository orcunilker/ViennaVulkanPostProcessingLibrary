#include "VVPPL.h"
#include <stdexcept>
#include <string>
#include <invert_spv.h>


namespace vvppl {

	PostProcessing::PostProcessing(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height)
        : m_device(device), m_physicalDevice(physicalDevice), m_width(width), m_height(height)
    {
        // ### setup um pp pipeline zu erzeugen, durch die bei jedem apply(jeder frame)
        // durchgegangen wird

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

        VkResult res = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
        if(res != VK_SUCCESS){
            throw std::runtime_error("vkCreateDescriptorSetLayout failed: " + std::to_string(res));
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
    }

    void PostProcessing::apply(VkCommandBuffer cmd, VkImage image, VkImageView view){
        // welches konkrete Image gemeint ist und in welchem layout es beim Zugriff sein wird
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView	= view;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // schreibt das Image in Binding 0 des Sets
        VkWriteDescriptorSet write{};
        write.sType		= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet	= m_descriptorSet;
        write.dstBinding	= 0;
        write.descriptorCount 	= 1;
        write.descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo		= &imgInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        // std::cout << "descriptor ok\n";

        // TODO bis hierher nur bis image nicht im vvppl

        // kopiert
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        // der eigentliche teil von apply
        // # vkCmdDispatch -> invertiertes .ppm

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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toCompute);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // eine workgroup deckt 8x8 Pixel ab, also aufrunden
        vkCmdDispatch(cmd, (m_width + 7) / 8, (m_height + 7) / 8, 1);

        // std::cout << "dispatch ok\n";
    }

}