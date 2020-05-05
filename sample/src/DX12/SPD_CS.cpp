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
#include "base\Device.h"
#include "base\DynamicBufferRing.h"
#include "base\StaticBufferPool.h"
#include "base\UploadHeap.h"
#include "base\Texture.h"
#include "base\Imgui.h"
#include "base\Helper.h"
#include "Base\ShaderCompilerHelper.h"

#include "SPD_CS.h"

namespace CAULDRON_DX12
{
    void SPD_CS::OnCreate(
        Device *pDevice,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing,
        DXGI_FORMAT outFormat,
        bool fallback,
        bool packed
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;
        m_outFormat = outFormat;

        D3D12_SHADER_BYTECODE shaderByteCode = {};
        DefineList defines;

        if (fallback) {
            defines["SPD_NO_WAVE_OPERATIONS"] = std::to_string(1);
        }
        if (packed) {
            defines["A_HALF"] = std::to_string(1);
            defines["SPD_PACKED_ONLY"] = std::to_string(1);
        }

    CompileShaderFromFile("SPD_Integration.hlsl", &defines, "main", "cs_6_0", 0, &shaderByteCode);

        // Create root signature
        //
        {
            CD3DX12_DESCRIPTOR_RANGE DescRange[4];
            CD3DX12_ROOT_PARAMETER RTSlot[4];

            // we'll always have a constant buffer
            int parameterCount = 0;
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
            RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

            // SRV table
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[1], D3D12_SHADER_VISIBILITY_ALL);

            // UAV table + global counter buffer
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[2], D3D12_SHADER_VISIBILITY_ALL);

            // output mips
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, SPD_MAX_MIP_LEVELS, 2);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[3], D3D12_SHADER_VISIBILITY_ALL);

            // when using AMD shader intrinsics
            /*if (!fallback)
            {
                //*** add AMD Intrinsic Resource ***
                DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, AGS_DX12_SHADER_INSTRINSICS_SPACE_ID); // u0
                RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[4], D3D12_SHADER_VISIBILITY_ALL);
            }*/

            // the root signature contains 4 slots to be used
            CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
            descRootSignature.NumParameters = parameterCount;
            descRootSignature.pParameters = RTSlot;
            descRootSignature.NumStaticSamplers = 0;
            descRootSignature.pStaticSamplers = NULL;

            // deny uneccessary access to certain pipeline stages   
            descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ID3DBlob *pOutBlob, *pErrorBlob = NULL;
            ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
            ThrowIfFailed(
                pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature))
            );
            SetName(m_pRootSignature, std::string("PostProcCS::") + "SPD_CS");

            pOutBlob->Release();
            if (pErrorBlob)
                pErrorBlob->Release();
        }

        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS = shaderByteCode;
            descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature = m_pRootSignature;
            descPso.NodeMask = 0;

            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pPipeline)));
        }

        // Allocate descriptors for the mip chain
        //
        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_constBuffer);
        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_sourceSRV);
        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(SPD_MAX_MIP_LEVELS, m_UAV);
        for (int i = 0; i < SPD_MAX_MIP_LEVELS; i++)
        {
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_SRV[i]);
        }

        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_globalCounter);
    }

    void SPD_CS::OnCreateWindowSizeDependentResources(uint32_t Width, uint32_t Height, Texture *pInput, int mipCount)
    {
        m_Width = Width;
        m_Height = Height;
        m_mipCount = mipCount;
        m_pInput = pInput;

        m_result.InitRenderTarget(
            m_pDevice, 
            "SPD_CS::m_result", 
            &CD3DX12_RESOURCE_DESC::Tex2D(
                m_outFormat, 
                m_Width >> 1, 
                m_Height >> 1, 
                1, 
                mipCount, 
                1, 
                0, 
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // Create views for the mip chain
        //

        // source 
        //
        pInput->CreateSRV(0, &m_sourceSRV, 0);

        // destination 
        //
        for (int i = 0; i < m_mipCount; i++)
        {
            m_result.CreateUAV(i, m_UAV, i);
            m_result.CreateSRV(0, &m_SRV[i], i);
        }

        m_globalCounterBuffer.InitBuffer(m_pDevice, "SPD_CS::m_globalCounterBuffer",
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_globalCounterBuffer.CreateBufferUAV(0, NULL, &m_globalCounter);
    }

    void SPD_CS::OnDestroyWindowSizeDependentResources()
    {
        m_globalCounterBuffer.OnDestroy();
        m_result.OnDestroy();
    }

    void SPD_CS::OnDestroy()
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

    void SPD_CS::Draw(ID3D12GraphicsCommandList2* pCommandList)
    {
        UserMarker marker(pCommandList, "SPD_CS");

        // downsample
        uint32_t dispatchX = (((m_Width + 63) >> (6)));
        uint32_t dispatchY = (((m_Height + 63) >> (6)));
        uint32_t dispatchZ = 1;

        D3D12_GPU_VIRTUAL_ADDRESS cbHandle;
        uint32_t* pConstMem;
        m_pConstantBufferRing->AllocConstantBuffer(sizeof(cbDownscale), (void**)&pConstMem, &cbHandle);
        cbDownscale constants;
        constants.mips = m_mipCount;
        constants.numWorkGroups = dispatchX * dispatchY * dispatchZ;
        memcpy(pConstMem, &constants, sizeof(cbDownscale));

        D3D12_RANGE range = { 0, sizeof(uint32_t) };

        // Bind Descriptor heaps and the root signature
        //                
        ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
        pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
        pCommandList->SetComputeRootSignature(m_pRootSignature);

        // Bind Descriptor the descriptor sets
        //                
        int params = 0;
        pCommandList->SetComputeRootConstantBufferView(params++, cbHandle);
        pCommandList->SetComputeRootDescriptorTable(params++, m_sourceSRV.GetGPU());
        pCommandList->SetComputeRootDescriptorTable(params++, m_globalCounter.GetGPU());
        pCommandList->SetComputeRootDescriptorTable(params++, m_UAV[0].GetGPU());

        // Bind Pipeline
        //
        pCommandList->SetPipelineState(m_pPipeline);

        // set counter to 0
        pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_globalCounterBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, 0));

        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER pParams = { m_globalCounterBuffer.GetResource()->GetGPUVirtualAddress(), 0 };
        pCommandList->WriteBufferImmediate(1, &pParams, NULL);

        D3D12_RESOURCE_BARRIER resourceBarriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_globalCounterBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0),
            CD3DX12_RESOURCE_BARRIER::Transition(m_result.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        pCommandList->ResourceBarrier(2, resourceBarriers);

        // Dispatch
        //
        pCommandList->Dispatch(dispatchX, dispatchY, dispatchZ);
        pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_result.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    void SPD_CS::Gui()
    {
        bool opened = true;
        ImGui::Begin("Downsample", &opened);

        ImGui::Image((ImTextureID)&m_sourceSRV, ImVec2(320, 180));
        for (int i = 0; i < m_mipCount; i++)
        {
            ImGui::Image((ImTextureID)&m_SRV[i], ImVec2(320, 180));
        }

        ImGui::End();
    }
}