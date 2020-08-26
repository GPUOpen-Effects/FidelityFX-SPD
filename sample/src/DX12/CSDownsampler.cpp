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
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;

        D3D12_SHADER_BYTECODE shaderByteCode = {};
        DefineList defines;
        CompileShaderFromFile("CSDownsampler.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);

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

            ID3DBlob* pOutBlob, * pErrorBlob = NULL;
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

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap, "..\\media\\envmaps\\papermill\\specular.dds", true, 1.0f, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        pUploadHeap->FlushAndFinish();

        // Allocate and create descriptors for the mip chain
        //
        for (uint32_t mip = 0; mip < m_cubeTexture.GetMipCount() - 1; mip++)
        {
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[mip].m_constBuffer);
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[mip].m_SRV);
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_mip[mip].m_UAV);

            m_cubeTexture.CreateSRV(0, &m_mip[mip].m_SRV, mip);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
            uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // can't create SRGB UAV
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.ArraySize = m_cubeTexture.GetArraySize();
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.MipSlice = mip + 1;
            uavDesc.Texture2DArray.PlaneSlice = 0;

            m_cubeTexture.CreateUAV(0, NULL, &m_mip[mip].m_UAV, &uavDesc);
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

    void CSDownsampler::OnDestroy()
    {
        m_cubeTexture.OnDestroy();

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

    void CSDownsampler::Draw(ID3D12GraphicsCommandList *pCommandList)
    {
        UserMarker marker(pCommandList, "CSDownsampler");

        // downsample
        //
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t i = 0; i < m_cubeTexture.GetMipCount() - 1; i++)
            {
                pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i + 1));

                D3D12_GPU_VIRTUAL_ADDRESS cbHandle;
                uint32_t* pConstMem;
                m_pConstantBufferRing->AllocConstantBuffer(sizeof(cbDownsample), (void**)&pConstMem, &cbHandle);
                cbDownsample constants;
                constants.outWidth = (m_cubeTexture.GetWidth() >> (i + 1));
                constants.outHeight = (m_cubeTexture.GetHeight() >> (i + 1));
                constants.invWidth = 1.0f / (float)(m_cubeTexture.GetWidth() >> i);
                constants.invHeight = 1.0f / (float)(m_cubeTexture.GetHeight() >> i);
                constants.slice = slice;
                memcpy(pConstMem, &constants, sizeof(cbDownsample));

                // Bind Descriptor heaps and the root signature
                //                
                ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
                pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
                pCommandList->SetComputeRootSignature(m_pRootSignature);

                // Bind Descriptor the descriptor sets
                //                
                int params = 0;
                pCommandList->SetComputeRootConstantBufferView(params++, cbHandle);
                pCommandList->SetComputeRootDescriptorTable(params++, m_mip[i].m_UAV.GetGPU());
                pCommandList->SetComputeRootDescriptorTable(params++, m_mip[i].m_SRV.GetGPU());

                // Bind Pipeline
                //
                pCommandList->SetPipelineState(m_pPipeline);

                // Dispatch
                //
                uint32_t dispatchX = ((m_cubeTexture.GetWidth() >> (i + 1)) + 7) / 8;
                uint32_t dispatchY = ((m_cubeTexture.GetHeight() >> (i + 1)) + 7) / 8;
                uint32_t dispatchZ = 1;
                pCommandList->Dispatch(dispatchX, dispatchY, dispatchZ);

                pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, i + 1));
            }
        }
    }

    void CSDownsampler::GUI(int *pSlice)
    {
        bool opened = true;
        std::string header = "Downsample";
        ImGui::Begin(header.c_str(), &opened);

        if (ImGui::CollapsingHeader("CS Multipass", ImGuiTreeNodeFlags_DefaultOpen))
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
