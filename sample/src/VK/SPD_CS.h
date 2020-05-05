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
#pragma once

#include "Base/StaticBufferPool.h"
#include "Base/Texture.h"
#include "Base/DynamicBufferRing.h"

namespace CAULDRON_VK
{
#define SPD_MAX_MIP_LEVELS 12

    class SPD_CS
    {
    public:
        void OnCreate(Device* pDevice, ResourceViewHeaps *pResourceViewHeaps, VkFormat outFormat, bool fallback, bool packed);
        void OnDestroy();

        void OnCreateWindowSizeDependentResources(VkCommandBuffer cmd_buf, uint32_t Width, uint32_t Height, Texture *pInput, int mips);
        void OnDestroyWindowSizeDependentResources();

        void Draw(VkCommandBuffer cmd_buf);
        Texture *GetTexture() { return &m_result; }
        VkImageView GetTextureView(int i) { return m_RTV[i]; }
        void Gui();

        struct PushConstants
        {
            int mips;
            int numWorkGroups;
            int padding[2];
        };

    private:
        Device *m_pDevice;
        VkFormat m_outFormat;

        Texture m_result;

        VkImageView m_RTV[SPD_MAX_MIP_LEVELS]; // destinations (mips)
        VkImageView m_SRV; // source
        VkDescriptorSet m_descriptorSet;

        ResourceViewHeaps *m_pResourceViewHeaps;
        DynamicBufferRing *m_pConstantBufferRing;

        uint32_t m_Width;
        uint32_t m_Height;
        int m_mipCount;

        VkDescriptorSetLayout m_descriptorSetLayout;

        VkPipelineLayout m_pipelineLayout;
        VkPipeline m_pipeline;

        VkBuffer m_globalCounter;
        VmaAllocation m_globalCounterAllocation;

        uint32_t* m_pCounter;
    };
}