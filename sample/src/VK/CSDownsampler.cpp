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

#include "CSDownsampler.h"

namespace CAULDRON_VK
{
    void CSDownsampler::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;

        // create the descriptor set layout
        // the shader needs
        // source image: texture + sampler
        // destination image: storage image
        // single pass: destination images count # of mips
        // single pass: global atomic counter, storage buffer
        {
            std::vector<VkDescriptorSetLayoutBinding> layoutBindings(3);
            layoutBindings[0].binding = 0;
            layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            layoutBindings[0].descriptorCount = 1;
            layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[0].pImmutableSamplers = NULL;

            layoutBindings[1].binding = 1;
            layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            layoutBindings[1].descriptorCount = 1;
            layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings[1].pImmutableSamplers = NULL;

            layoutBindings[2].binding = 2;
            layoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
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

        // The sampler we want to use
        // average: linear
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

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap , "..\\media\\envmaps\\papermill\\specular.dds", true, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        pUploadHeap->FlushAndFinish();

        VkPipelineShaderStageCreateInfo computeShader;
        DefineList defines;

        VkResult res = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT,
            "CSDownsampler.glsl", "main", "", &defines, &computeShader);
        assert(res == VK_SUCCESS);

        // Create pipeline layout
        //
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pPipelineLayoutCreateInfo.pNext = NULL;

        // push constants: input size, inverse output size
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(cbDownsample);
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

        // the naive solution uses for each mip a separate pass
        // single pass: only one descriptor set
        for (int i = 0; i < CS_MAX_MIP_LEVELS; i++)
        {
            m_pResourceViewHeaps->AllocDescriptor(m_descriptorSetLayout, &m_mip[i].m_descriptorSet);
        }

        // populate descriptor sets
        for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
        {
            m_cubeTexture.CreateSRV(&m_mip[i].m_SRV, i); // texture2DArray
            m_cubeTexture.CreateRTV(&m_mip[i].m_UAV, i + 1); // texture2DArray

            // Create and initialize the Descriptor Sets (all of them use the same Descriptor Layout)        
            // Create and initialize descriptor set for sampled image
            VkDescriptorImageInfo desc_sampled_image = {};
            desc_sampled_image.sampler = VK_NULL_HANDLE;
            desc_sampled_image.imageView = m_mip[i].m_SRV;
            desc_sampled_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::vector<VkWriteDescriptorSet> writes(3);
            writes[0] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].pNext = NULL;
            writes[0].dstSet = m_mip[i].m_descriptorSet;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[0].pImageInfo = &desc_sampled_image;
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;

            // Create and initialize descriptor set for sampler
            VkDescriptorImageInfo desc_sampler = {};
            desc_sampler.sampler = m_sampler;

            writes[1] = {};
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].pNext = NULL;
            writes[1].dstSet = m_mip[i].m_descriptorSet;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[1].pImageInfo = &desc_sampler;
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;

            // Create and initialize descriptor set for storage image
            VkDescriptorImageInfo desc_storage_image = {};
            desc_storage_image.sampler = VK_NULL_HANDLE;
            desc_storage_image.imageView = m_mip[i].m_UAV;
            desc_storage_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[2] = {};
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].pNext = NULL;
            writes[2].dstSet = m_mip[i].m_descriptorSet;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[2].pImageInfo = &desc_storage_image;
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;

            vkUpdateDescriptorSets(m_pDevice->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, NULL);
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
                    &m_imGUISRV[slice * m_cubeTexture.GetMipCount() + mip]);
                assert(res == VK_SUCCESS);
            }
        }
    }

    void CSDownsampler::OnDestroy()
    {
        for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() * 6; i++)
        {
            vkDestroyImageView(m_pDevice->GetDevice(), m_imGUISRV[i], NULL);
        }

        for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
        {
            vkDestroyImageView(m_pDevice->GetDevice(), m_mip[i].m_SRV, NULL);
            vkDestroyImageView(m_pDevice->GetDevice(), m_mip[i].m_UAV, NULL);
        }

        for (int i = 0; i < CS_MAX_MIP_LEVELS; i++)
        {
            m_pResourceViewHeaps->FreeDescriptor(m_mip[i].m_descriptorSet);
        }

        vkDestroyPipeline(m_pDevice->GetDevice(), m_pipeline, NULL);
        vkDestroyPipelineLayout(m_pDevice->GetDevice(), m_pipelineLayout, NULL);
        vkDestroyDescriptorSetLayout(m_pDevice->GetDevice(), m_descriptorSetLayout, NULL);
        vkDestroySampler(m_pDevice->GetDevice(), m_sampler, nullptr);
        m_cubeTexture.OnDestroy();
    }

    void CSDownsampler::Draw(VkCommandBuffer cmd_buf)
    {
        // downsample
        //

        // transition layout undefined to general layout?
        VkImageMemoryBarrier imageMemoryBarrier = {};
        imageMemoryBarrier = {};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.pNext = NULL;
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
        imageMemoryBarrier.subresourceRange.levelCount = 1;
        imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        imageMemoryBarrier.subresourceRange.layerCount = 6;
        imageMemoryBarrier.image = m_cubeTexture.Resource();

        // transition general layout if detination image to shader read only for source image
        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

        SetPerfMarkerBegin(cmd_buf, "DownsampleCS");

        // Bind Pipeline
        //
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            VkImageMemoryBarrier imageMemoryBarrierArray = {};
            imageMemoryBarrierArray.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrierArray.pNext = NULL;
            imageMemoryBarrierArray.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imageMemoryBarrierArray.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            imageMemoryBarrierArray.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrierArray.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageMemoryBarrierArray.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrierArray.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrierArray.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageMemoryBarrierArray.subresourceRange.baseMipLevel = 1;
            imageMemoryBarrierArray.subresourceRange.levelCount = m_cubeTexture.GetMipCount() - 1;
            imageMemoryBarrierArray.subresourceRange.baseArrayLayer = 0;
            imageMemoryBarrierArray.subresourceRange.layerCount = 6;
            imageMemoryBarrierArray.image = m_cubeTexture.Resource();

            // transition general layout if destination image to shader read only for source image
            vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrierArray);

            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {
                uint32_t dispatchX = ((m_cubeTexture.GetWidth() >> (i + 1)) + 7) / 8;
                uint32_t dispatchY = ((m_cubeTexture.GetHeight() >> (i + 1)) + 7) / 8;
                uint32_t dispatchZ = 1;

                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_mip[i].m_descriptorSet, 0, nullptr);

                // Bind push constants
                //
                cbDownsample data;
                data.outputSize[0] = (float)(m_cubeTexture.GetWidth() >> (i + 1));
                data.outputSize[1] = (float)(m_cubeTexture.GetHeight() >> (i + 1));
                data.invInputSize[0] = 1.0f / (float)(m_cubeTexture.GetWidth() >> i);
                data.invInputSize[1] = 1.0f / (float)(m_cubeTexture.GetHeight() >> i);
                data.slice = slice;
                vkCmdPushConstants(cmd_buf, m_pipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cbDownsample), (void*)&data);

                // Draw
                //
                vkCmdDispatch(cmd_buf, dispatchX, dispatchY, dispatchZ);

                VkImageMemoryBarrier imageMemoryBarrier = {};
                imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageMemoryBarrier.pNext = NULL;
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageMemoryBarrier.subresourceRange.baseMipLevel = i + 1;
                imageMemoryBarrier.subresourceRange.levelCount = 1;
                imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
                imageMemoryBarrier.subresourceRange.layerCount = 6;
                imageMemoryBarrier.image = m_cubeTexture.Resource();

                // transition general layout if destination image to shader read only for source image
                vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            }
        }

        SetPerfMarkerEnd(cmd_buf);
    }

    void CSDownsampler::GUI(int* pSlice)
    {
        bool opened = true;
        std::string header = "Downsample";
        ImGui::Begin(header.c_str(), &opened);

        if (ImGui::CollapsingHeader("CS Multipass", ImGuiTreeNodeFlags_DefaultOpen))
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
                ImGui::Image((ImTextureID)m_imGUISRV[*pSlice * m_cubeTexture.GetMipCount() + i], ImVec2(static_cast<float>(512 >> i), static_cast<float>(512 >> i)));
            }
        }

        ImGui::End();
    }
}