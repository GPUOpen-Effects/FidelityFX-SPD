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
#include "SPD_CS.h"
#include "SPD_CS_Linear_Sampler.h"

namespace CAULDRON_DX12
{
    enum class SPD_Version
    {
        SPD_No_WaveOps,
        SPD_WaveOps,
    };

    enum class SPD_Packed
    {
        SPD_Non_Packed,
        SPD_Packed,
    };

    class SPD_Versions
    {
    public:
        void OnCreate(
            Device *pDevice, 
            ResourceViewHeaps *pResourceViewHeaps, 
            DynamicBufferRing *pConstantBufferRing, 
            DXGI_FORMAT outFormat
        );
        void OnDestroy();

        void OnCreateWindowSizeDependentResources(int Width, int Height, Texture *pInput);
        void OnDestroyWindowSizeDependentResources();

        void Dispatch(ID3D12GraphicsCommandList2* pCommandList, SPD_Version dsVersion, SPD_Packed dsPacked);
        void Gui(SPD_Version dsVersion, SPD_Packed dsPacked);

        void DispatchLinearSamplerVersion(ID3D12GraphicsCommandList2* pCommandList, SPD_Version dsVersion, SPD_Packed dsPacked);
        void GuiLinearSamplerVersion(SPD_Version dsVersion, SPD_Packed dsPacked);

    private:
        Device                  *m_pDevice;

        SPD_CS m_spd_WaveOps_NonPacked;
        SPD_CS m_spd_No_WaveOps_NonPacked;

        SPD_CS m_spd_WaveOps_Packed;
        SPD_CS m_spd_No_WaveOps_Packed;

        SPD_CS_Linear_Sampler m_spd_WaveOps_NonPacked_Linear_Sampler;
        SPD_CS_Linear_Sampler m_spd_No_WaveOps_NonPacked_Linear_Sampler;

        SPD_CS_Linear_Sampler m_spd_WaveOps_Packed_Linear_Sampler;
        SPD_CS_Linear_Sampler m_spd_No_WaveOps_Packed_Linear_Sampler;

        int GetMaxMipLevelCount(int Width, int Height);
    };
}