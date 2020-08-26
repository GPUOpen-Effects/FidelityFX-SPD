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
#include "base/Device.h"
#include "base/DynamicBufferRing.h"
#include "base/StaticBufferPool.h"
#include "base/UploadHeap.h"
#include "base/Texture.h"
#include "base/Imgui.h"
#include "base/Helper.h"

#include "PSDownsampler.h"

namespace CAULDRON_DX12
{
    void PSDownsampler::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing,
        StaticBufferPool  *pStaticBufferPool
    )
    {
        m_pDevice = pDevice;
        m_pStaticBufferPool = pStaticBufferPool;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;

        // Use helper class to create the fullscreen pass
        //

        D3D12_STATIC_SAMPLER_DESC SamplerDesc = {};
        SamplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        SamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        SamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        SamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        SamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        SamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        SamplerDesc.MinLOD = 0.0f;
        SamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        SamplerDesc.MipLODBias = 0;
        SamplerDesc.MaxAnisotropy = 1;
        SamplerDesc.ShaderRegister = 0;
        SamplerDesc.RegisterSpace = 0;
        SamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap, "..\\media\\envmaps\\papermill\\specular.dds", true, 1.0f, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        pUploadHeap->FlushAndFinish();

        m_downsample.OnCreate(pDevice, "PSDownsampler.hlsl", m_pResourceViewHeaps, 
            m_pStaticBufferPool, 1, 1, &SamplerDesc, m_cubeTexture.GetFormat());

        // Allocate descriptors for the mip chain
        //
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {
                m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_SRV);
                m_pResourceViewHeaps->AllocRTVDescriptor(1, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_RTV);

                m_cubeTexture.CreateSRV(0, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_SRV, i, 1, slice);
                m_cubeTexture.CreateRTV(0, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_RTV, i + 1, 1, slice);
            }
        }

        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t mip = 0; mip < m_cubeTexture.GetMipCount(); mip++)
            {
                m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_imGUISRV[slice * m_cubeTexture.GetMipCount() + mip]);
                m_cubeTexture.CreateSRV(0, &m_imGUISRV[slice * m_cubeTexture.GetMipCount() + mip], mip, 1, slice);
            }
        }
    }

    void PSDownsampler::OnDestroy()
    {
        m_cubeTexture.OnDestroy();
        m_downsample.OnDestroy();
    }
    
    void PSDownsampler::Draw(ID3D12GraphicsCommandList *pCommandList)
    {
        UserMarker marker(pCommandList, "PSDownsampler");

        // downsample
        //
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {
                pCommandList->ResourceBarrier(1, 
                    &CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), 
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
                        D3D12_RESOURCE_STATE_RENDER_TARGET, 
                        slice * m_cubeTexture.GetMipCount() + i + 1));

                pCommandList->OMSetRenderTargets(1, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_RTV.GetCPU(), true, NULL);
                SetViewportAndScissor(pCommandList, 0, 0, m_cubeTexture.GetWidth() >> (i + 1), m_cubeTexture.GetHeight() >> (i + 1));

                cbDownsample* data;
                D3D12_GPU_VIRTUAL_ADDRESS constantBuffer;
                m_pConstantBufferRing->AllocConstantBuffer(sizeof(cbDownsample), (void**)&data, &constantBuffer);
                data->outWidth = (float)(m_cubeTexture.GetWidth() >> (i + 1));
                data->outHeight = (float)(m_cubeTexture.GetHeight() >> (i + 1));
                data->invWidth = 1.0f / (float)(m_cubeTexture.GetWidth() >> i);
                data->invHeight = 1.0f / (float)(m_cubeTexture.GetHeight() >> i);
                data->slice = slice;

                m_downsample.Draw(pCommandList, 1, &m_mip[slice * (m_cubeTexture.GetMipCount() - 1) + i].m_SRV, constantBuffer);

                pCommandList->ResourceBarrier(1, 
                    &CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), 
                        D3D12_RESOURCE_STATE_RENDER_TARGET, 
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
                        slice * m_cubeTexture.GetMipCount() + i + 1));
            }
        }
    }

    void PSDownsampler::GUI(int *pSlice)
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
                ImGui::Image((ImTextureID)&m_imGUISRV[*pSlice * m_cubeTexture.GetMipCount() + i], ImVec2(static_cast<float>(512 >> i), static_cast<float>(512 >> i)));
            }
        }

        ImGui::End();
    }
}