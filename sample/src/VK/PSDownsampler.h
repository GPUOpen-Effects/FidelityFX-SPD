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

#include "PostProc/PostProcPS.h"
#include "Base/StaticBufferPool.h"
#include "Base/Texture.h"

namespace CAULDRON_VK
{
#define PS_MAX_MIP_LEVELS 12

    class PSDownsampler
    {
    public:
        void OnCreate(Device *pDevice, UploadHeap *pUploadHeap, ResourceViewHeaps *pResourceViewHeaps, DynamicBufferRing *m_pConstantBufferRing, StaticBufferPool *pStaticBufferPool);
        void OnDestroy();

        void Draw(VkCommandBuffer cmd_buf);
        Texture *GetTexture() { return &m_cubeTexture; }
        void GUI(int *pSlice);

        struct cbDownsample
        {
            float outWidth, outHeight;
            float invWidth, invHeight;
            uint32_t slice;
            uint32_t padding[3];
        };

    private:
        Device                     *m_pDevice = nullptr;

        Texture                     m_cubeTexture;

        struct Pass
        {
            VkImageView     m_SRV; //src
            VkImageView     m_RTV; //dst
            VkFramebuffer   m_frameBuffer;
            VkDescriptorSet m_descriptorSet;
        };

        Pass                         m_mip[PS_MAX_MIP_LEVELS * 6] = {};

        StaticBufferPool            *m_pStaticBufferPool = nullptr;
        ResourceViewHeaps           *m_pResourceViewHeaps = nullptr;
        DynamicBufferRing           *m_pConstantBufferRing = nullptr;

        VkDescriptorSetLayout        m_descriptorSetLayout = VK_NULL_HANDLE;

        PostProcPS                   m_downsample;

        VkRenderPass                 m_in = VK_NULL_HANDLE;

        VkSampler                    m_sampler = VK_NULL_HANDLE;
    };
}


