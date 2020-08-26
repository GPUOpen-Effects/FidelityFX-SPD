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

    enum class SPDWaveOps
    {
        SPDNoWaveOps,
        SPDWaveOps,
    };

    enum class SPDPacked
    {
        SPDNonPacked,
        SPDPacked,
    };

    enum class SPDLoad
    {
        SPDLoad,
        SPDLinearSampler,
    };

    class SPDCS
    {
    public:
        void OnCreate(Device *pDevice, UploadHeap *pUploadHeap, ResourceViewHeaps *pResourceViewHeaps,
            SPDLoad spdLoad, SPDWaveOps spdWaveOps, SPDPacked spdPacked);
        void OnDestroy();

        void Draw(VkCommandBuffer cmd_buf);
        Texture *GetTexture() { return &m_cubeTexture; }
        void GUI(int* pSlice);

        struct SpdConstants
        {
            int mips;
            int numWorkGroupsPerSlice;
            int workGroupOffset[2];
        };

        struct SpdLinearSamplerConstants
        {
            int mips;
            int numWorkGroupsPerSlice;
            int workGroupOffset[2];
            float invInputSize[2];
            float padding[2];
        };

    private:
        Device                        *m_pDevice = nullptr;

        Texture                        m_cubeTexture;

        VkImageView                    m_UAV[SPD_MAX_MIP_LEVELS + 1] = {}; // source + destinations (mips)
        VkImageView                    m_SRV[SPD_MAX_MIP_LEVELS * 6] = {}; // for display of MIPS using imGUI
        VkImageView                    m_sourceSRV = VK_NULL_HANDLE; // source when linear sampler is used
        VkSampler                      m_sampler = VK_NULL_HANDLE; // linear sampler
        VkDescriptorSet                m_descriptorSet = VK_NULL_HANDLE;

        ResourceViewHeaps             *m_pResourceViewHeaps = nullptr;
        DynamicBufferRing             *m_pConstantBufferRing = nullptr;

        VkDescriptorSetLayout          m_descriptorSetLayout = VK_NULL_HANDLE;

        VkPipelineLayout               m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline                     m_pipeline = VK_NULL_HANDLE;

        VkBuffer                       m_globalCounter = VK_NULL_HANDLE;
        VmaAllocation                  m_globalCounterAllocation;

        SPDLoad                        m_spdLoad;
        SPDWaveOps                     m_spdWaveOps;
        SPDPacked                      m_spdPacked;
    };
}