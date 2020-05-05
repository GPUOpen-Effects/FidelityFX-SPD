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
#include "Base/ShaderCompilerHelper.h"

#include "CSDownsampler.h"

namespace CAULDRON_DX12
{
    void CSDownsampler::OnCreate(
        Device *pDevice,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing,
        DXGI_FORMAT outFormat
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;
        m_outFormat = outFormat;

        D3D12_SHADER_BYTECODE shaderByteCode = {};
        DefineList defines;
        CompileShaderFromFile("CSDownsampler.hlsl", &defines, "main", "cs_6_2", 0, &shaderByteCode);

        // Create root signature
        //
        {
            CD3DX12_DESCRIPTOR_RANGE DescRange[3];
            CD3DX12_ROOT_PARAMETER RTSlot[3];

            // we'll always have a constant buffer
            int parameterCount = 0;
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
            RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

            // UAV table
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[1], D3D12_SHADER_VISIBILITY_ALL);

            // SRV table
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[2], D3D12_SHADER_VISIBILITY_ALL);

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
            SamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // the root signature contains 4 slots to be used
            CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
            descRootSignature.NumParameters = parameterCount;
            descRootSignature.pParameters = RTSlot;
            descRootSignature.NumStaticSamplers = 1; // numStaticSamplers;
            descRootSignature.pStaticSamplers = &SamplerDesc; //pStaticSamplers;

            // deny uneccessary access to certain pipeline stages   
            descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ID3DBlob *pOutBlob, *pErrorBlob = NULL;
            ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
            ThrowIfFailed(
                pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature))
            );
            SetName(m_pRootSignature, std::string("PostProcCS::") + "CSDownsampler");

            pOutBlob->Release();
            if (pErrorBlob)
                pErrorBlob->Release();
        }

        //{
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS = shaderByteCode;
            descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature = m_pRootSignature;
            descPso.NodeMask = 0;

            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pPipeline)));
        //}

        // Allocate descriptors for the mip chain
        //
        for (int i = 0; i < CS_MAX_MIP_LEVELS; i++)
        {
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[i].m_constBuffer);
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[i].m_SRV);
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[i].m_RTV);
        }
    }

    void CSDownsampler::OnCreateWindowSizeDependentResources(uint32_t Width, uint32_t Height, Texture *pInput, int mipCount)
    {
        m_Width = Width;
        m_Height = Height;
        m_mipCount = mipCount;
        m_pInput = pInput;

        m_result.InitRenderTarget(
            m_pDevice, 
            "CSDownsampler::m_result", 
            &CD3DX12_RESOURCE_DESC::Tex2D(
                m_outFormat, 
                m_Width >> 1, 
                m_Height >> 1, 
                1, 
                mipCount, 
                1, 
                0, 
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // Create views for the mip chain
        //
        for (int i = 0; i < m_mipCount; i++)
        {
            // source 
            //
            if (i == 0)
            {
                pInput->CreateSRV(0, &m_mip[i].m_SRV, 0);
            }
            else
            {
                m_result.CreateSRV(0, &m_mip[i].m_SRV, i - 1);
            }

            // destination 
            //
            m_result.CreateUAV(0, &m_mip[i].m_RTV, i);
        }
    }

    void CSDownsampler::OnDestroyWindowSizeDependentResources()
    {
        m_result.OnDestroy();
    }

    void CSDownsampler::OnDestroy()
    {
        if (m_pPipeline != NULL)
        {
            m_pPipeline->Release();
            m_pPipeline = NULL;
        }

        if (m_pRootSignature != NULL)
        {
            m_pRootSignature->Release();
            m_pRootSignature = NULL;
        }
    }

    void CSDownsampler::Draw(ID3D12GraphicsCommandList* pCommandList)
    {
        UserMarker marker(pCommandList, "CSDownsampler");

        // downsample
        //
        for (int i = 0; i < m_mipCount; i++)
        {
            pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_result.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i));

            D3D12_GPU_VIRTUAL_ADDRESS cbHandle;
            uint32_t* pConstMem;
            m_pConstantBufferRing->AllocConstantBuffer(sizeof(cbDownscale), (void**)&pConstMem, &cbHandle);
            cbDownscale constants;
            constants.outWidth = (m_Width >> (i + 1));
            constants.outHeight = (m_Height >> (i + 1));
            constants.invWidth = 1.0f / (float)(m_Width >> i);
            constants.invHeight = 1.0f / (float)(m_Height >> i);
            memcpy(pConstMem, &constants, sizeof(cbDownscale));

            // Bind Descriptor heaps and the root signature
            //                
            ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
            pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
            pCommandList->SetComputeRootSignature(m_pRootSignature);

            // Bind Descriptor the descriptor sets
            //                
            int params = 0;
            pCommandList->SetComputeRootConstantBufferView(params++, cbHandle);
            pCommandList->SetComputeRootDescriptorTable(params++, m_mip[i].m_RTV.GetGPU());
            pCommandList->SetComputeRootDescriptorTable(params++, m_mip[i].m_SRV.GetGPU());

            // Bind Pipeline
            //
            pCommandList->SetPipelineState(m_pPipeline);

            // Dispatch
            //
            uint32_t dispatchX = ((m_Width >> (i + 1)) + 7) / 8;
            uint32_t dispatchY = ((m_Height >> (i + 1)) + 7) / 8;
            uint32_t dispatchZ = 1;
            pCommandList->Dispatch(dispatchX, dispatchY, dispatchZ);

            pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_result.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, i));
        }
    }

    void CSDownsampler::Gui()
    {
        bool opened = true;
        ImGui::Begin("Downsample", &opened);

        for (int i = 0; i < m_mipCount; i++)
        {
            ImGui::Image((ImTextureID)&m_mip[i].m_SRV, ImVec2(320, 180));
        }

        ImGui::End();
    }
}
