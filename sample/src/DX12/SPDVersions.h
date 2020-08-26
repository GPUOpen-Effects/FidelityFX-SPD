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
#include "SPDCS.h"

namespace CAULDRON_DX12
{
    class SPDVersions
    {
    public:
        void OnCreate(
            Device *pDevice,
            UploadHeap *pUploadHeap,
            ResourceViewHeaps *pResourceViewHeaps,
            DynamicBufferRing *pConstantBufferRing
        );
        void OnDestroy();

        void Dispatch(ID3D12GraphicsCommandList2 *pCommandList, SPDLoad spdLoad, SPDWaveOps spdWaveOps, SPDPacked spdPacked);
        void GUI(SPDLoad spdLoad, SPDWaveOps spdWaveOps, SPDPacked spdPacked, int *pSlice);

    private:
        Device                  *m_pDevice = nullptr;

        SPDCS                    m_spd_WaveOps_NonPacked;
        SPDCS                    m_spd_No_WaveOps_NonPacked;

        SPDCS                    m_spd_WaveOps_Packed;
        SPDCS                    m_spd_No_WaveOps_Packed;

        SPDCS                    m_spd_WaveOps_NonPacked_Linear_Sampler;
        SPDCS                    m_spd_No_WaveOps_NonPacked_Linear_Sampler;

        SPDCS                    m_spd_WaveOps_Packed_Linear_Sampler;
        SPDCS                    m_spd_No_WaveOps_Packed_Linear_Sampler;

        uint32_t GetMaxMIPLevelCount(uint32_t Width, uint32_t Height);
    };
}