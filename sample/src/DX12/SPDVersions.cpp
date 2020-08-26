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
#include "base/DynamicBufferRing.h"
#include "base/StaticBufferPool.h"
#include "base/UploadHeap.h"
#include "base/Texture.h"
#include "base/Helper.h"
#include "SPDVersions.h"

namespace CAULDRON_DX12
{
    void SPDVersions::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing
    )
    {
        m_pDevice = pDevice;

        m_spd_WaveOps_NonPacked.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLoad, SPDWaveOps::SPDWaveOps, SPDPacked::SPDNonPacked);
        m_spd_WaveOps_Packed.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLoad, SPDWaveOps::SPDWaveOps, SPDPacked::SPDPacked);
        m_spd_No_WaveOps_NonPacked.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLoad, SPDWaveOps::SPDNoWaveOps, SPDPacked::SPDNonPacked);
        m_spd_No_WaveOps_Packed.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLoad, SPDWaveOps::SPDNoWaveOps, SPDPacked::SPDPacked);

        m_spd_WaveOps_NonPacked_Linear_Sampler.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLinearSampler, SPDWaveOps::SPDWaveOps, SPDPacked::SPDNonPacked);
        m_spd_WaveOps_Packed_Linear_Sampler.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLinearSampler, SPDWaveOps::SPDWaveOps, SPDPacked::SPDPacked);
        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLinearSampler, SPDWaveOps::SPDNoWaveOps, SPDPacked::SPDNonPacked);
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnCreate(pDevice, pUploadHeap, pResourceViewHeaps, pConstantBufferRing, SPDLoad::SPDLinearSampler, SPDWaveOps::SPDNoWaveOps, SPDPacked::SPDPacked);
    }

    uint32_t SPDVersions::GetMaxMIPLevelCount(uint32_t Width, uint32_t Height)
    {
        uint32_t resolution = max(Width, Height);
        return (static_cast<uint32_t>(min(floor(log2(resolution)), 12)));
    }

    void SPDVersions::OnDestroy()
    {
        m_spd_WaveOps_NonPacked.OnDestroy();
        m_spd_WaveOps_Packed.OnDestroy();
        m_spd_No_WaveOps_NonPacked.OnDestroy();
        m_spd_No_WaveOps_Packed.OnDestroy();

        m_spd_WaveOps_NonPacked_Linear_Sampler.OnDestroy();
        m_spd_WaveOps_Packed_Linear_Sampler.OnDestroy();
        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnDestroy();
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnDestroy();
    }

    void SPDVersions::Dispatch(ID3D12GraphicsCommandList2 *pCommandList, SPDLoad spdLoad, SPDWaveOps spdWaveOps, SPDPacked spdPacked)
    {
        switch (spdLoad)
        {
        case SPDLoad::SPDLoad:
        {
            switch (spdWaveOps)
            {
            case SPDWaveOps::SPDWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_WaveOps_NonPacked.Draw(pCommandList);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_WaveOps_Packed.Draw(pCommandList);
                    break;
                }
                break;
            case SPDWaveOps::SPDNoWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_No_WaveOps_NonPacked.Draw(pCommandList);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_No_WaveOps_Packed.Draw(pCommandList);
                    break;
                }
            }
            break;
        }
        case SPDLoad::SPDLinearSampler:
        {
            switch (spdWaveOps)
            {
            case SPDWaveOps::SPDWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_WaveOps_NonPacked_Linear_Sampler.Draw(pCommandList);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_WaveOps_Packed_Linear_Sampler.Draw(pCommandList);
                    break;
                }
                break;
            case SPDWaveOps::SPDNoWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_No_WaveOps_NonPacked_Linear_Sampler.Draw(pCommandList);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_No_WaveOps_Packed_Linear_Sampler.Draw(pCommandList);
                    break;
                }
            }
            break;
        }
        }
    }

    void SPDVersions::GUI(SPDLoad spdLoad, SPDWaveOps spdWaveOps, SPDPacked spdPacked, int *pSlice)
    {
        switch (spdLoad)
        {
        case SPDLoad::SPDLoad:
        {
            switch (spdWaveOps)
            {
            case SPDWaveOps::SPDWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_WaveOps_NonPacked.GUI(pSlice);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_WaveOps_Packed.GUI(pSlice);
                    break;
                }
                break;
            case SPDWaveOps::SPDNoWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_No_WaveOps_NonPacked.GUI(pSlice);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_No_WaveOps_Packed.GUI(pSlice);
                    break;
                }
            }
            break;
        }
        case SPDLoad::SPDLinearSampler:
        {
            switch (spdWaveOps)
            {
            case SPDWaveOps::SPDWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_WaveOps_NonPacked_Linear_Sampler.GUI(pSlice);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_WaveOps_Packed_Linear_Sampler.GUI(pSlice);
                    break;
                }
                break;
            case SPDWaveOps::SPDNoWaveOps:
                switch (spdPacked)
                {
                case SPDPacked::SPDNonPacked:
                    m_spd_No_WaveOps_NonPacked_Linear_Sampler.GUI(pSlice);
                    break;
                case SPDPacked::SPDPacked:
                    m_spd_No_WaveOps_Packed_Linear_Sampler.GUI(pSlice);
                    break;
                }
            }
            break;
        }
        }
    }
}