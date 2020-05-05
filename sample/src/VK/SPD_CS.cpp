// SPDSample
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "stdafx.h"
#include "Base\Device.h"
#include "Base\ShaderCompilerHelper.h"
#include "Base\ExtDebugMarkers.h"
#include "Base\Imgui.h"

#include "SPD_CS.h"

namespace CAULDRON_VK
{
    void SPD_CS::OnCreate(
        Device* pDevice,
        ResourceViewHeaps *pResourceViewHeaps,
        VkFormat outFormat,
        bool fallback,
        bool packed
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_outFormat = outFormat;

        // create the descriptor set layout
        // the shader needs
        // source image: storage image (read-only)
        // destination image: storage image
        // global atomic counter: storage buffer
        {
            std::vector<VkDescriptorSetLayoutBinding> layoutBindings(3);
            layoutBindings[0].binding = 0;
            layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            layoutBindings[0].descriptorCount = 1;
            layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[0].pImmutableSamplers = NULL;

            layoutBindings[1].binding = 1;
            layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            layoutBindings[1].descriptorCount = SPD_MAX_MIP_LEVELS;
            layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[1].pImmutableSamplers = NULL;

            layoutBindings[2].binding = 2;
            layoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layoutBindings[2].descriptorCount = 1;
            layoutBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[2].pImmutableSamplers = NULL;

            VkDescriptorSetLayoutCreateInfo descriptor_layout = {};
            descriptor_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_layout.pNext = NULL;
            descriptor_layout.bindingCount = (uint32_t)layoutBindings.size();
            descriptor_layout.pBindings = layoutBindings.data();

            VkResult res = vkCreateDescriptorSetLayout(pDevice->GetDevice(), &descriptor_layout, NULL, &m_descriptorSetLayout);
            assert(res == VK_SUCCESS);
        }

        // Create global atomic counter
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.flags = 0;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            bufferInfo.queueFamilyIndexCount = 0;
            bufferInfo.pQueueFamilyIndices = NULL;
            bufferInfo.size = sizeof(int) * 1;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            VmaAllocationCreateInfo bufferAllocCreateInfo = {};
            bufferAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            bufferAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
            bufferAllocCreateInfo.pUserData = "SpdGlobalAtomicCounter";
            VmaAllocationInfo bufferAllocInfo = {};
            vmaCreateBuffer(m_pDevice->GetAllocator(), &bufferInfo, &bufferAllocCreateInfo, &m_globalCounter, 
                &m_globalCounterAllocation, &bufferAllocInfo);
        }

        VkPipelineShaderStageCreateInfo computeShader;
        DefineList defines;

        if (fallback) {
            defines["SPD_NO_WAVE_OPERATIONS"] = std::to_string(1);
        }
        if (packed) {
            defines["A_HALF"] = std::to_string(1);
            defines["SPD_PACKED_ONLY"] = std::to_string(1);
        }

        VkResult res = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT,
            "SPD_Integration.glsl", "main", &defines, &computeShader);
        assert(res == VK_SUCCESS);

        // Create pipeline layout
        //
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pPipelineLayoutCreateInfo.pNext = NULL;

        // push constants
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        pPipelineLayoutCreateInfo.setLayoutCount = 1;
        pPipelineLayoutCreateInfo.pSetLayouts = &m_descriptorSetLayout;

        res = vkCreatePipelineLayout(pDevice->GetDevice(), &pPipelineLayoutCreateInfo, NULL, &m_pipelineLayout);
        assert(res == VK_SUCCESS);

        // Create pipeline
        //
        VkComputePipelineCreateInfo pipeline = {};
        pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline.pNext = NULL;
        pipeline.flags = 0;
        pipeline.layout = m_pipelineLayout;
        pipeline.stage = computeShader;
        pipeline.basePipelineHandle = VK_NULL_HANDLE;
        pipeline.basePipelineIndex = 0;

        res = vkCreateComputePipelines(pDevice->GetDevice(), pDevice->GetPipelineCache(), 1, &pipeline, NULL, &m_pipeline);
        assert(res == VK_SUCCESS);

        m_pResourceViewHeaps->AllocDescriptor(m_descriptorSetLayout, &m_descriptorSet);
    }

    void SPD_CS::OnCreateWindowSizeDependentResources(
        VkCommandBuffer cmd_buf,
        uint32_t Width,
        uint32_t Height,
        Texture *pInput,
        int mips
    )
    {
        m_Width = Width;
        m_Height = Height;
        m_mipCount = mips;

        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.pNext = NULL;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = m_outFormat;
        image_info.extent.width = m_Width >> 1;
        image_info.extent.height = m_Height >> 1;
        image_info.extent.depth = 1;
        image_info.mipLevels = m_mipCount;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.queueFamilyIndexCount = 0;
        image_info.pQueueFamilyIndices = NULL;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.usage = (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);   
        image_info.flags = 0;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        m_result.Init(m_pDevice, &image_info, "SpdDestinationMips");

        // transition layout undefined to general layout?
        VkImageMemoryBarrier imageMemoryBarrier = {};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.pNext = NULL;
        imageMemoryBarrier.srcAccessMask = 0;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
        imageMemoryBarrier.subresourceRange.levelCount = m_mipCount;
        imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        imageMemoryBarrier.subresourceRange.layerCount = 1;
        imageMemoryBarrier.image = m_result.Resource();

        // transition general layout if detination image to shader read only for source image
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

        // Create views for the mip chain
        //
        // source -----------
        //
        pInput->CreateSRV(&m_SRV, 0);

        // Create and initialize the Descriptor Sets (all of them use the same Descriptor Layout)        
        // Create and initialize descriptor set for sampled image
        VkDescriptorImageInfo desc_source_image = {};
        desc_source_image.sampler = VK_NULL_HANDLE;
        desc_source_image.imageView = m_SRV;
        desc_source_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::vector<VkWriteDescriptorSet> writes(3);
        writes[0] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].pNext = NULL;
        writes[0].dstSet = m_descriptorSet;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &desc_source_image;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;

        // Create and initialize descriptor set for storage image
        std::vector<VkDescriptorImageInfo> desc_storage_images(m_mipCount);

        for (int i = 0; i < m_mipCount; i++)
        {
            // destination -----------
            m_result.CreateRTV(&m_RTV[i], i);

            desc_storage_images[i] = {};
            desc_storage_images[i].sampler = VK_NULL_HANDLE;
            desc_storage_images[i].imageView = m_RTV[i];
            desc_storage_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        writes[1] = {};
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].pNext = NULL;
        writes[1].dstSet = m_descriptorSet;
        writes[1].descriptorCount = m_mipCount;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = desc_storage_images.data();
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;

        VkDescriptorBufferInfo desc_buffer = {};
        desc_buffer.buffer = m_globalCounter;
        desc_buffer.offset = 0;
        desc_buffer.range = sizeof(int) * 1;

        writes[2] = {};
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].pNext = NULL;
        writes[2].dstSet = m_descriptorSet;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &desc_buffer;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;

        vkUpdateDescriptorSets(m_pDevice->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, NULL);
    }

    void SPD_CS::OnDestroyWindowSizeDependentResources()
    {
        vkDestroyImageView(m_pDevice->GetDevice(), m_SRV, NULL);
        for (int i = 0; i < m_mipCount; i++)
        {
            vkDestroyImageView(m_pDevice->GetDevice(), m_RTV[i], NULL);
        }

        m_result.OnDestroy();
    }

    void SPD_CS::OnDestroy()
    {

        m_pResourceViewHeaps->FreeDescriptor(m_descriptorSet);

        vmaDestroyBuffer(m_pDevice->GetAllocator(), m_globalCounter, m_globalCounterAllocation);

        vkDestroyPipeline(m_pDevice->GetDevice(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_pDevice->GetDevice(), m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_pDevice->GetDevice(), m_descriptorSetLayout, NULL);
    }

    void SPD_CS::Draw(VkCommandBuffer cmd_buf)
    {
        // downsample
        //

        // initialize global atomic counter to 0
        vmaMapMemory(m_pDevice->GetAllocator(), m_globalCounterAllocation, (void**)&m_pCounter);
        *m_pCounter = 0;
        vmaUnmapMemory(m_pDevice->GetAllocator(), m_globalCounterAllocation);

        // transition general layout if detination image to shader read only for source image
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 0, nullptr);

        SetPerfMarkerBegin(cmd_buf, "SPD_CS");

        // Bind Pipeline
        //
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

        // should be / 64
        uint32_t dispatchX = (((m_Width + 63) >> (6)));
        uint32_t dispatchY = (((m_Height + 63) >> (6)));
        uint32_t dispatchZ = 1;

        // single pass for storage buffer?
        //uint32_t uniformOffsets[1] = { (uint32_t)constantBuffer.offset };
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind push constants
        //
        PushConstants data;
        data.mips = m_mipCount;
        data.numWorkGroups = dispatchX * dispatchY * dispatchZ;
        vkCmdPushConstants(cmd_buf, m_pipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), (void*)&data);

        // Draw
        //
        vkCmdDispatch(cmd_buf, dispatchX, dispatchY, dispatchZ);

        // transition general layout if detination image to shader read only for source image
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 0, nullptr);

        SetPerfMarkerEnd(cmd_buf);
    }

    void SPD_CS::Gui()
    {
        bool opened = true;
        ImGui::Begin("Downsample", &opened);

        ImGui::Image((ImTextureID)m_SRV, ImVec2(320, 180));

        for (int i = 0; i < m_mipCount; i++)
        {
            ImGui::Image((ImTextureID)m_RTV[i], ImVec2(320, 180));
        }

        ImGui::End();
    }
}