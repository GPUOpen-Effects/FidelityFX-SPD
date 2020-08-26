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
#include "Base/DynamicBufferRing.h"
#include "Base/StaticBufferPool.h"
#include "Base/ExtDebugMarkers.h"
#include "Base/UploadHeap.h"
#include "Base/Texture.h"
#include "Base/Imgui.h"
#include "Base/Helper.h"

#include "PostProc/PostProcPS.h"
#include "PSDownsampler.h"

namespace CAULDRON_VK
{
    void PSDownsampler::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing,
        StaticBufferPool *pStaticBufferPool
    )
    {
        m_pDevice = pDevice;
        m_pStaticBufferPool = pStaticBufferPool;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;

        // Create Descriptor Set Layout, the shader needs a uniform dynamic buffer and a texture + sampler
        // The Descriptor Sets will be created and initialized once we know the input to the shader, that happens in OnCreateWindowSizeDependentResources()
        {
            std::vector<VkDescriptorSetLayoutBinding> layoutBindings(2);
            layoutBindings[0].binding = 0;
            layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            layoutBindings[0].descriptorCount = 1;
            layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            layoutBindings[0].pImmutableSamplers = NULL;

            layoutBindings[1].binding = 1;
            layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            layoutBindings[1].descriptorCount = 1;
            layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            layoutBindings[1].pImmutableSamplers = NULL;

            VkDescriptorSetLayoutCreateInfo descriptor_layout = {};
            descriptor_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_layout.pNext = NULL;
            descriptor_layout.bindingCount = (uint32_t)layoutBindings.size();
            descriptor_layout.pBindings = layoutBindings.data();

            VkResult res = vkCreateDescriptorSetLayout(pDevice->GetDevice(), &descriptor_layout, NULL, &m_descriptorSetLayout);
            assert(res == VK_SUCCESS);
        }

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap, "..\\media\\envmaps\\papermill\\specular.dds", true, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        pUploadHeap->FlushAndFinish();

        // In Render pass
        //
        // color RT
        VkAttachmentDescription attachments[1];
        attachments[0].format = m_cubeTexture.GetFormat();
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // we don't care about the previous contents, this is for a full screen pass with no blending
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[0].flags = 0;

        VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.flags = 0;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = NULL;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pResolveAttachments = NULL;
        subpass.pDepthStencilAttachment = NULL;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = NULL;

        VkSubpassDependency dep = {};
        dep.dependencyFlags = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstSubpass = VK_SUBPASS_EXTERNAL;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcSubpass = 0;

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.pNext = NULL;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = attachments;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 1;
        rp_info.pDependencies = &dep;

        VkResult res = vkCreateRenderPass(pDevice->GetDevice(), &rp_info, NULL, &m_in);
        assert(res == VK_SUCCESS);

        // The sampler we want to use for downsampling, all linear
        //
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

        // Use helper class to create the fullscreen pass
        //
        m_downsample.OnCreate(pDevice, m_in, "PSDownsampler.glsl", "main", "", pStaticBufferPool, pConstantBufferRing, m_descriptorSetLayout);

        // Allocate descriptors for the mip chain
        //
        for (int i = 0; i < DOWNSAMPLEPS_MAX_MIP_LEVELS * 6; i++)
        {
            m_pResourceViewHeaps->AllocDescriptor(m_descriptorSetLayout, &m_mip[i].m_descriptorSet);
        }

        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t mip = 0; mip < m_cubeTexture.GetMipCount() - 1; mip++)
            {

                VkImageViewCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                info.image = m_cubeTexture.Resource();
                info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                info.subresourceRange.baseArrayLayer = slice;
                info.subresourceRange.layerCount = 1;
                info.format = m_cubeTexture.GetFormat();
                info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                info.subresourceRange.baseMipLevel = mip;
                info.subresourceRange.levelCount = 1;

                VkResult res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL,
                    &m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_SRV);
                assert(res == VK_SUCCESS);

                // Create and initialize the Descriptor Sets (all of them use the same Descriptor Layout)        
                m_pConstantBufferRing->SetDescriptorSet(0, sizeof(DownSamplePS::cbDownscale), m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_descriptorSet);
                SetDescriptorSet(m_pDevice->GetDevice(), 1, m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_SRV, &m_sampler, m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_descriptorSet);

                info.subresourceRange.baseMipLevel = mip + 1;

                res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL,
                    &m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_RTV);
                assert(res == VK_SUCCESS);

                VkImageView attachments[1] = { m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_RTV };

                VkFramebufferCreateInfo fb_info = {};
                fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fb_info.pNext = NULL;
                fb_info.renderPass = m_in;
                fb_info.attachmentCount = 1;
                fb_info.pAttachments = attachments;
                fb_info.width = m_cubeTexture.GetWidth() >> (mip + 1);
                fb_info.height = m_cubeTexture.GetHeight() >> (mip + 1);
                fb_info.layers = 1;
                res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_mip[slice * m_cubeTexture.GetMipCount() + mip].m_frameBuffer);
                assert(res == VK_SUCCESS);
            }
        }
    }

    void PSDownsampler::OnDestroy()
    {
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {
                vkDestroyImageView(m_pDevice->GetDevice(), m_mip[slice * m_cubeTexture.GetMipCount() + i].m_SRV, NULL);
                vkDestroyImageView(m_pDevice->GetDevice(), m_mip[slice * m_cubeTexture.GetMipCount() + i].m_RTV, NULL);
                vkDestroyFramebuffer(m_pDevice->GetDevice(), m_mip[slice * m_cubeTexture.GetMipCount() + i].m_frameBuffer, NULL);
            }
        }

        m_cubeTexture.OnDestroy();

        for (int i = 0; i < DOWNSAMPLEPS_MAX_MIP_LEVELS * 6; i++)
        {
            m_pResourceViewHeaps->FreeDescriptor(m_mip[i].m_descriptorSet);
        }

        m_downsample.OnDestroy();
        vkDestroyDescriptorSetLayout(m_pDevice->GetDevice(), m_descriptorSetLayout, NULL);
        vkDestroySampler(m_pDevice->GetDevice(), m_sampler, NULL);

        vkDestroyRenderPass(m_pDevice->GetDevice(), m_in, NULL);
    }

    void PSDownsampler::Draw(VkCommandBuffer cmd_buf)
    {
        SetPerfMarkerBegin(cmd_buf, "Downsample");

        // downsample
        //
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {

                VkRenderPassBeginInfo rp_begin = {};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.pNext = NULL;
                rp_begin.renderPass = m_in;
                rp_begin.framebuffer = m_mip[slice * m_cubeTexture.GetMipCount() + i].m_frameBuffer;
                rp_begin.renderArea.offset.x = 0;
                rp_begin.renderArea.offset.y = 0;
                rp_begin.renderArea.extent.width = m_cubeTexture.GetWidth() >> (i + 1);
                rp_begin.renderArea.extent.height = m_cubeTexture.GetHeight() >> (i + 1);
                rp_begin.clearValueCount = 0;
                rp_begin.pClearValues = NULL;
                vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                SetViewportAndScissor(cmd_buf, 0, 0, m_cubeTexture.GetWidth() >> (i + 1), m_cubeTexture.GetHeight() >> (i + 1));

                cbDownsample* data;
                VkDescriptorBufferInfo constantBuffer;
                m_pConstantBufferRing->AllocConstantBuffer(sizeof(cbDownsample), (void**)&data, &constantBuffer);
                data->outWidth = (float)(m_cubeTexture.GetWidth() >> (i + 1));
                data->outHeight = (float)(m_cubeTexture.GetHeight() >> (i + 1));
                data->invWidth = 1.0f / (float)(m_cubeTexture.GetWidth() >> i);
                data->invHeight = 1.0f / (float)(m_cubeTexture.GetHeight() >> i);
                data->slice = slice;

                m_downsample.Draw(cmd_buf, constantBuffer, m_mip[slice * m_cubeTexture.GetMipCount() + i].m_descriptorSet);

                vkCmdEndRenderPass(cmd_buf);
            }
        }

        SetPerfMarkerEnd(cmd_buf);
    }

    void PSDownsampler::GUI(int* pSlice)
    {
        bool opened = true;
        std::string header = "Downsample";
        ImGui::Begin(header.c_str(), &opened);

        if (ImGui::CollapsingHeader("PS", ImGuiTreeNodeFlags_DefaultOpen))
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
                ImGui::Image((ImTextureID)m_mip[*pSlice * m_cubeTexture.GetMipCount() + i].m_SRV, ImVec2(static_cast<float>(512 >> i), static_cast<float>(512 >> i)));
            }
        }

        ImGui::End();
    }
}