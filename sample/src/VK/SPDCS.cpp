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

#include "SPDCS.h"

#define A_CPU
#include "ffx_a.h"
#include "ffx_spd.h"

namespace CAULDRON_VK
{
    void SPDCS::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        SPDLoad spdLoad,
        SPDWaveOps spdWaveOps,
        SPDPacked spdPacked
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;

        m_spdLoad = spdLoad;
        m_spdWaveOps = spdWaveOps;
        m_spdPacked = spdPacked;

        uint32_t bindingCount = 3;

        // create the descriptor set layout
        // the shader needs
        // image: source image + destination mips
        // global atomic counter: storage buffer
        {
            VkDescriptorSetLayoutBinding layoutBindings[5];
            layoutBindings[0].binding = 0;
            layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            layoutBindings[0].descriptorCount = SPD_MAX_MIP_LEVELS + 1;
            layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[0].pImmutableSamplers = NULL;

            layoutBindings[1].binding = 1;
            layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            layoutBindings[1].descriptorCount = 1;
            layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[1].pImmutableSamplers = NULL;

            layoutBindings[2].binding = 2;
            layoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layoutBindings[2].descriptorCount = 1;
            layoutBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[2].pImmutableSamplers = NULL;

            if (m_spdLoad == SPDLoad::SPDLinearSampler)
            {
                bindingCount = 5;

                // bind source texture as sampled image and sampler
                layoutBindings[3].binding = 3;
                layoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                layoutBindings[3].descriptorCount = 1;
                layoutBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                layoutBindings[3].pImmutableSamplers = NULL;

                layoutBindings[4].binding = 4;
                layoutBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                layoutBindings[4].descriptorCount = 1;
                layoutBindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                layoutBindings[4].pImmutableSamplers = NULL;
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
            bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount = bindingCount;
            std::vector<VkDescriptorBindingFlags> bindingFlags(bindingCount);
            bindingFlags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            bindingFlagsInfo.pBindingFlags = bindingFlags.data();

            VkDescriptorSetLayoutCreateInfo descriptor_layout = {};
            descriptor_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_layout.pNext = &bindingFlagsInfo;
            descriptor_layout.bindingCount = bindingCount;
            descriptor_layout.pBindings = layoutBindings;

            VkResult res = vkCreateDescriptorSetLayout(pDevice->GetDevice(), &descriptor_layout, NULL, &m_descriptorSetLayout);
            assert(res == VK_SUCCESS);
        }

        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            // The sampler we want to use, needs to match the SPD Reduction function in the shader
            // linear sampler:
            // -> AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return (v0+v1+v2+v3)*0.25;}
            // point sampler:
            // -> AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return v3;}
            {
                VkSamplerCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = VK_FILTER_LINEAR;
                info.minFilter = VK_FILTER_LINEAR;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                info.minLod = -1000;
                info.maxLod = 1000;
                info.maxAnisotropy = 1.0f;
                VkResult res = vkCreateSampler(pDevice->GetDevice(), &info, NULL, &m_sampler);
                assert(res == VK_SUCCESS);
            }
        }

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap, "..\\media\\envmaps\\papermill\\specular.dds", true, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        pUploadHeap->FlushAndFinish();

        // Create global atomic counter
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.flags = 0;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            bufferInfo.queueFamilyIndexCount = 0;
            bufferInfo.pQueueFamilyIndices = NULL;
            bufferInfo.size = sizeof(int) * m_cubeTexture.GetArraySize(); // number of slices
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            VmaAllocationCreateInfo bufferAllocCreateInfo = {};
            bufferAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            bufferAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
            bufferAllocCreateInfo.pUserData = "SpdGlobalAtomicCounter";
            VmaAllocationInfo bufferAllocInfo = {};
            vmaCreateBuffer(m_pDevice->GetAllocator(), &bufferInfo, &bufferAllocCreateInfo, &m_globalCounter, 
                &m_globalCounterAllocation, &bufferAllocInfo);

            // initialize global atomic counter to 0
            uint32_t pCounter[6]; // one counter per slice
            vmaMapMemory(m_pDevice->GetAllocator(), m_globalCounterAllocation, (void**)&pCounter);
            for (uint32_t i = 0; i < m_cubeTexture.GetArraySize(); i++)
            {
                pCounter[i] = 0;
            }
            vmaUnmapMemory(m_pDevice->GetAllocator(), m_globalCounterAllocation);
        }

        VkPipelineShaderStageCreateInfo computeShader;
        DefineList defines;

        if (m_spdWaveOps == SPDWaveOps::SPDNoWaveOps) {
            defines["SPD_NO_WAVE_OPERATIONS"] = 1;
        }
        if (m_spdPacked == SPDPacked::SPDPacked) {
            defines["A_HALF"] = 1;
            defines["SPD_PACKED_ONLY"] = 1;
        }

        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            VkResult res = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT,
                "SPDIntegrationLinearSampler.hlsl", "main", "-T cs_6_0", &defines, &computeShader);
            assert(res == VK_SUCCESS);
        }
        else {
            VkResult res = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT,
                "SPDIntegration.hlsl", "main", "-T cs_6_0", &defines, &computeShader);
            assert(res == VK_SUCCESS);
        }

        // Create pipeline layout
        //
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pPipelineLayoutCreateInfo.pNext = NULL;

        // push constants
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.offset = 0;
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            pushConstantRange.size = sizeof(SpdLinearSamplerConstants);
        }
        else {
            pushConstantRange.size = sizeof(SpdConstants);
        }
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        pPipelineLayoutCreateInfo.setLayoutCount = 1;
        pPipelineLayoutCreateInfo.pSetLayouts = &m_descriptorSetLayout;

        VkResult res = vkCreatePipelineLayout(pDevice->GetDevice(), &pPipelineLayoutCreateInfo, NULL, &m_pipelineLayout);
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

        // Create and initialize descriptor set for storage image
        // std::vector<VkDescriptorImageInfo> desc_storage_images(SPD_MAX_MIP_LEVELS + 1);

        uint32_t numUAVs = m_cubeTexture.GetMipCount();
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            // we need one UAV less because source texture will be bound as SRV and not as UAV
            numUAVs = m_cubeTexture.GetMipCount() - 1;
        }

        std::vector<VkDescriptorImageInfo> desc_storage_images(numUAVs);
        for (uint32_t i = 0; i < numUAVs; i++)
        {
            // destination -----------
            if (m_spdLoad == SPDLoad::SPDLinearSampler)
            {
                // first UAV is MIP 1
                m_cubeTexture.CreateRTV(&m_UAV[i], i + 1);
            }
            else {
                // first UAV is source texture, MIP 0
                m_cubeTexture.CreateRTV(&m_UAV[i], i);
            }

            desc_storage_images[i] = {};
            desc_storage_images[i].sampler = VK_NULL_HANDLE;
            desc_storage_images[i].imageView = m_UAV[i];
            desc_storage_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        // update descriptors
        // SPD Load version
        if (m_spdLoad == SPDLoad::SPDLoad)
        {
            VkWriteDescriptorSet writes[3];
            writes[0] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].pNext = NULL;
            writes[0].dstSet = m_descriptorSet;
            writes[0].descriptorCount = numUAVs;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo = desc_storage_images.data();
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;

            writes[1] = {};
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].pNext = NULL;
            writes[1].dstSet = m_descriptorSet;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &desc_storage_images[6];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;

            VkDescriptorBufferInfo desc_buffer = {};
            desc_buffer.buffer = m_globalCounter;
            desc_buffer.offset = 0;
            desc_buffer.range = sizeof(int) * m_cubeTexture.GetArraySize(); // number of slices

            writes[2] = {};
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].pNext = NULL;
            writes[2].dstSet = m_descriptorSet;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &desc_buffer;
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;

            vkUpdateDescriptorSets(m_pDevice->GetDevice(), 3, writes, 0, NULL);
        }

        // update descriptors
        // SPD Linear Sampler version
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            VkWriteDescriptorSet writes[5];
            writes[0] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].pNext = NULL;
            writes[0].dstSet = m_descriptorSet;
            writes[0].descriptorCount = numUAVs;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo = desc_storage_images.data();
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;

            writes[1] = {};
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].pNext = NULL;
            writes[1].dstSet = m_descriptorSet;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &desc_storage_images[5];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;

            VkDescriptorBufferInfo desc_buffer = {};
            desc_buffer.buffer = m_globalCounter;
            desc_buffer.offset = 0;
            desc_buffer.range = sizeof(int) * m_cubeTexture.GetArraySize(); // number of slices

            writes[2] = {};
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].pNext = NULL;
            writes[2].dstSet = m_descriptorSet;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &desc_buffer;
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;

            m_cubeTexture.CreateSRV(&m_sourceSRV, 0);

            VkDescriptorImageInfo desc_sampled_image = {};
            desc_sampled_image.sampler = VK_NULL_HANDLE;
            desc_sampled_image.imageView = m_sourceSRV;
            desc_sampled_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[3] = {};
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].pNext = NULL;
            writes[3].dstSet = m_descriptorSet;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[3].pImageInfo = &desc_sampled_image;
            writes[3].dstBinding = 3;
            writes[3].dstArrayElement = 0;

            // Create and initialize descriptor set for sampler
            VkDescriptorImageInfo desc_sampler = {};
            desc_sampler.sampler = m_sampler;

            writes[4] = {};
            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].pNext = NULL;
            writes[4].dstSet = m_descriptorSet;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[4].pImageInfo = &desc_sampler;
            writes[4].dstBinding = 4;
            writes[4].dstArrayElement = 0;

            vkUpdateDescriptorSets(m_pDevice->GetDevice(), 5, writes, 0, NULL);
        }

        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t mip = 0; mip < m_cubeTexture.GetMipCount(); mip++)
            {
                VkImageViewUsageCreateInfo imageViewUsageInfo = {};
                imageViewUsageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
                imageViewUsageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

                VkImageViewCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                info.pNext = &imageViewUsageInfo;
                info.image = m_cubeTexture.Resource();
                info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                info.subresourceRange.baseArrayLayer = slice;
                info.subresourceRange.layerCount = 1;

                switch (m_cubeTexture.GetFormat())
                {
                    case VK_FORMAT_B8G8R8A8_UNORM: info.format = VK_FORMAT_B8G8R8A8_SRGB; break;
                    case VK_FORMAT_R8G8B8A8_UNORM: info.format = VK_FORMAT_R8G8B8A8_SRGB; break;
                    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: info.format = VK_FORMAT_BC1_RGB_SRGB_BLOCK; break;
                    case VK_FORMAT_BC2_UNORM_BLOCK: info.format = VK_FORMAT_BC2_SRGB_BLOCK; break;
                    case VK_FORMAT_BC3_UNORM_BLOCK: info.format = VK_FORMAT_BC3_SRGB_BLOCK; break;
                    default: info.format = m_cubeTexture.GetFormat();
                }

                info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                info.subresourceRange.baseMipLevel = mip;
                info.subresourceRange.levelCount = 1;

                VkResult res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL,
                    &m_SRV[slice * m_cubeTexture.GetMipCount() + mip]);
                assert(res == VK_SUCCESS);
            }
        }
    }

    void SPDCS::OnDestroy()
    {
        for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() * m_cubeTexture.GetArraySize(); i++)
        {
            vkDestroyImageView(m_pDevice->GetDevice(), m_SRV[i], NULL);
        }

        uint32_t numUAVs = m_cubeTexture.GetMipCount();
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            // we needed one UAV less because source texture is bound as SRV and not as UAV
            numUAVs = m_cubeTexture.GetMipCount() - 1;

            // also destroy SRV and sampler
            vkDestroyImageView(m_pDevice->GetDevice(), m_sourceSRV, NULL);
            vkDestroySampler(m_pDevice->GetDevice(), m_sampler, NULL);
        }

        for (uint32_t i = 0; i < numUAVs; i++)
        {
            vkDestroyImageView(m_pDevice->GetDevice(), m_UAV[i], NULL);
        }

        m_cubeTexture.OnDestroy();

        m_pResourceViewHeaps->FreeDescriptor(m_descriptorSet);

        vmaDestroyBuffer(m_pDevice->GetAllocator(), m_globalCounter, m_globalCounterAllocation);

        vkDestroyPipeline(m_pDevice->GetDevice(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_pDevice->GetDevice(), m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_pDevice->GetDevice(), m_descriptorSetLayout, NULL);
    }

    void SPDCS::Draw(VkCommandBuffer cmd_buf)
    {
        // downsample
        //
        varAU2(dispatchThreadGroupCountXY);
        varAU2(workGroupOffset);  // needed if Left and Top are not 0,0
        varAU2(numWorkGroupsAndMips);
        varAU4(rectInfo) = initAU4(0, 0, m_cubeTexture.GetWidth(), m_cubeTexture.GetHeight()); // left, top, width, height
        SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        VkImageMemoryBarrier imageMemoryBarrier[2];
            
        uint32_t numBarriers = 1;
        imageMemoryBarrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier[0].pNext = NULL;
        imageMemoryBarrier[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        imageMemoryBarrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        imageMemoryBarrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            imageMemoryBarrier[0].subresourceRange.baseMipLevel = 1;
            imageMemoryBarrier[0].subresourceRange.levelCount = m_cubeTexture.GetMipCount() - 1;
        }
        else {
            imageMemoryBarrier[0].subresourceRange.baseMipLevel = 0;
            imageMemoryBarrier[0].subresourceRange.levelCount = m_cubeTexture.GetMipCount();
        }
        imageMemoryBarrier[0].subresourceRange.baseArrayLayer = 0;
        imageMemoryBarrier[0].subresourceRange.layerCount = m_cubeTexture.GetArraySize();
        imageMemoryBarrier[0].image = m_cubeTexture.Resource();

        if (m_spdLoad == SPDLoad::SPDLinearSampler) {
            numBarriers = 2;
            imageMemoryBarrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier[1].pNext = NULL;
            imageMemoryBarrier[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            imageMemoryBarrier[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            imageMemoryBarrier[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageMemoryBarrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageMemoryBarrier[1].subresourceRange.baseMipLevel = 0;
            imageMemoryBarrier[1].subresourceRange.levelCount = 1;
            imageMemoryBarrier[1].subresourceRange.baseArrayLayer = 0;
            imageMemoryBarrier[1].subresourceRange.layerCount = m_cubeTexture.GetArraySize();
            imageMemoryBarrier[1].image = m_cubeTexture.Resource();
        }

        // transition general layout
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, numBarriers, imageMemoryBarrier);

        SetPerfMarkerBegin(cmd_buf, "SPDCS");

        // Bind Pipeline
        //
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

        // should be / 64
        uint32_t dispatchX = dispatchThreadGroupCountXY[0];
        uint32_t dispatchY = dispatchThreadGroupCountXY[1];
        uint32_t dispatchZ = m_cubeTexture.GetArraySize(); // slices

        // single pass for storage buffer?
        //uint32_t uniformOffsets[1] = { (uint32_t)constantBuffer.offset };
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind push constants
        //
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            SpdLinearSamplerConstants data;
            data.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
            data.mips = numWorkGroupsAndMips[1];
            data.workGroupOffset[0] = workGroupOffset[0];
            data.workGroupOffset[1] = workGroupOffset[1];
            data.invInputSize[0] = 1.0f / m_cubeTexture.GetWidth();
            data.invInputSize[1] = 1.0f / m_cubeTexture.GetHeight();
            vkCmdPushConstants(cmd_buf, m_pipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpdLinearSamplerConstants), (void*)&data);
        }
        else {
            SpdConstants data;
            data.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
            data.mips = numWorkGroupsAndMips[1];
            data.workGroupOffset[0] = workGroupOffset[0];
            data.workGroupOffset[1] = workGroupOffset[1];
            vkCmdPushConstants(cmd_buf, m_pipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpdConstants), (void*)&data);
        }

        // Draw
        //
        vkCmdDispatch(cmd_buf, dispatchX, dispatchY, dispatchZ);

        imageMemoryBarrier[0] = {};
        imageMemoryBarrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier[0].pNext = NULL;
        imageMemoryBarrier[0].srcAccessMask = 0;
        imageMemoryBarrier[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageMemoryBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            imageMemoryBarrier[0].subresourceRange.baseMipLevel = 1;
            imageMemoryBarrier[0].subresourceRange.levelCount = m_cubeTexture.GetMipCount() - 1;
        }
        else {
            imageMemoryBarrier[0].subresourceRange.baseMipLevel = 0;
            imageMemoryBarrier[0].subresourceRange.levelCount = m_cubeTexture.GetMipCount();
        }
        imageMemoryBarrier[0].subresourceRange.baseArrayLayer = 0;
        imageMemoryBarrier[0].subresourceRange.layerCount = m_cubeTexture.GetArraySize();
        imageMemoryBarrier[0].image = m_cubeTexture.Resource();

        // transition general layout if detination image to shader read only for source image
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, imageMemoryBarrier);

        SetPerfMarkerEnd(cmd_buf);
    }

    void SPDCS::GUI(int* pSlice)
    {
        bool opened = true;
        std::string header = "Downsample";
        ImGui::Begin(header.c_str(), &opened);

        std::string downsampleHeader = "SPD CS";
        if (m_spdLoad == SPDLoad::SPDLoad) {
            downsampleHeader += " Load";
        }
        else {
            downsampleHeader += " Linear Sampler";
        }

        if (m_spdWaveOps == SPDWaveOps::SPDWaveOps)
        {
            downsampleHeader += " WaveOps";
        }
        else {
            downsampleHeader += " No WaveOps";
        }

        if (m_spdPacked == SPDPacked::SPDNonPacked)
        {
            downsampleHeader += " Non Packed";
        }
        else {
            downsampleHeader += " Packed";
        }

        if (ImGui::CollapsingHeader(downsampleHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* sliceItemNames[] =
            {
                "Slice 0",
                "Slice 1",
                "Slice 2",
                "Slice 3",
                "Slice 4",
                "Slice 5"
            };
            ImGui::Combo("Slice of Cube Texture", pSlice, sliceItemNames, _countof(sliceItemNames));

            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount(); i++)
            {
                ImGui::Image((ImTextureID)m_SRV[*pSlice * m_cubeTexture.GetMipCount() + i], ImVec2(static_cast<float>(512 >> i), static_cast<float>(512 >> i)));
            }
        }
        
        ImGui::End();
    }
}