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

#include "SPDCS.h"

#define A_CPU
#include "ffx_a.h"
#include "ffx_spd.h"

namespace CAULDRON_DX12
{
    void SPDCS::OnCreate(
        Device *pDevice,
        UploadHeap *pUploadHeap,
        ResourceViewHeaps *pResourceViewHeaps,
        DynamicBufferRing *pConstantBufferRing,
        SPDLoad spdLoad,
        SPDWaveOps spdWaveOps,
        SPDPacked spdPacked
    )
    {
        m_pDevice = pDevice;
        m_pResourceViewHeaps = pResourceViewHeaps;
        m_pConstantBufferRing = pConstantBufferRing;

        m_spdLoad = spdLoad;
        m_spdWaveOps = spdWaveOps;
        m_spdPacked = spdPacked;

        D3D12_SHADER_BYTECODE shaderByteCode = {};
        DefineList defines;

        if (m_spdWaveOps == SPDWaveOps::SPDNoWaveOps) {
            defines["SPD_NO_WAVE_OPERATIONS"] = 1;
        }
        if (m_spdPacked == SPDPacked::SPDPacked) {
            defines["A_HALF"] = 1;
            defines["SPD_PACKED_ONLY"] = 1;
        }

        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            CompileShaderFromFile("SPDIntegrationLinearSampler.hlsl", &defines, "main", "-T cs_6_2 /Zi /Zss", &shaderByteCode);
        }
        else {
            CompileShaderFromFile("SPDIntegration.hlsl", &defines, "main", "-T cs_6_2 /Zi /Zss", &shaderByteCode);
        }

        // Create root signature
        // Spd Load version
        if (m_spdLoad == SPDLoad::SPDLoad)
        {
            CD3DX12_DESCRIPTOR_RANGE DescRange[5];
            CD3DX12_ROOT_PARAMETER RTSlot[5];

            // we'll always have a constant buffer
            int parameterCount = 0;
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
            RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

            // UAV table + global counter buffer
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[1], D3D12_SHADER_VISIBILITY_ALL);

            // output mips
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[2], D3D12_SHADER_VISIBILITY_ALL);

            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, SPD_MAX_MIP_LEVELS + 1, 3);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[3], D3D12_SHADER_VISIBILITY_ALL);

            if (m_spdLoad == SPDLoad::SPDLinearSampler)
            {
                // SRV table
                DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
                RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[4], D3D12_SHADER_VISIBILITY_ALL);
            }

            // the root signature contains 4 slots to be used
            CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
            descRootSignature.NumParameters = parameterCount;
            descRootSignature.pParameters = RTSlot;
            descRootSignature.NumStaticSamplers = 0;
            descRootSignature.pStaticSamplers = NULL;

            if (m_spdLoad == SPDLoad::SPDLinearSampler)
            {
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

                descRootSignature.NumStaticSamplers = 1;
                descRootSignature.pStaticSamplers = &SamplerDesc;
            }

            // deny uneccessary access to certain pipeline stages   
            descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ID3DBlob* pOutBlob, * pErrorBlob = NULL;
            ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
            ThrowIfFailed(
                pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature))
            );
            SetName(m_pRootSignature, std::string("PostProcCS::") + "SPD_CS");

            pOutBlob->Release();
            if (pErrorBlob)
                pErrorBlob->Release();
        }

        // SPD Linear Sampler version
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            CD3DX12_DESCRIPTOR_RANGE DescRange[5];
            CD3DX12_ROOT_PARAMETER RTSlot[5];

            // we'll always have a constant buffer
            int parameterCount = 0;
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
            RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

            // UAV table + global counter buffer
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[1], D3D12_SHADER_VISIBILITY_ALL);

            // output mips
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[2], D3D12_SHADER_VISIBILITY_ALL);

            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, SPD_MAX_MIP_LEVELS + 1, 3);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[3], D3D12_SHADER_VISIBILITY_ALL);

            // SRV table
            DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
            RTSlot[parameterCount++].InitAsDescriptorTable(1, &DescRange[4], D3D12_SHADER_VISIBILITY_ALL);

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
            descRootSignature.NumStaticSamplers = 1;
            descRootSignature.pStaticSamplers = &SamplerDesc;

            // deny uneccessary access to certain pipeline stages   
            descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ID3DBlob* pOutBlob, * pErrorBlob = NULL;
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

        m_cubeTexture.InitFromFile(pDevice, pUploadHeap, "..\\media\\envmaps\\papermill\\specular.dds", true, 1.0f, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        pUploadHeap->FlushAndFinish();

        // Allocate descriptors for the mip chain
        //
        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_constBuffer);

        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_sourceSRV);
            m_cubeTexture.CreateSRV(0, &m_sourceSRV, 0, m_cubeTexture.GetArraySize(), 0);
        }

        uint32_t numUAVs = m_cubeTexture.GetMipCount();
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            // we need one UAV less because source texture will be bound as SRV and not as UAV
            numUAVs = m_cubeTexture.GetMipCount() - 1;
        }

        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(numUAVs, m_UAV);

        // Create views for the mip chain
        //
        // destination 
        //
        for (uint32_t i = 0; i < numUAVs; i++)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
            uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // can't create SRGB UAV
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.ArraySize = m_cubeTexture.GetArraySize();
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            if (m_spdLoad == SPDLoad::SPDLinearSampler)
            {
                uavDesc.Texture2DArray.MipSlice = i + 1;
            }
            else {
                uavDesc.Texture2DArray.MipSlice = i;
            }
            uavDesc.Texture2DArray.PlaneSlice = 0;

            m_cubeTexture.CreateUAV(i, NULL, m_UAV, &uavDesc);
        }

        // for GUI
        for (uint32_t slice = 0; slice < m_cubeTexture.GetArraySize(); slice++)
        {
            for (uint32_t mip = 0; mip < m_cubeTexture.GetMipCount(); mip++)
            {
                m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_SRV[slice * m_cubeTexture.GetMipCount() + mip]);
                m_cubeTexture.CreateSRV(0, &m_SRV[slice * m_cubeTexture.GetMipCount() + mip], mip, 1, slice);
            }
        }

        m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_globalCounter);
        m_globalCounterBuffer.InitBuffer(m_pDevice, "SPD_CS::m_globalCounterBuffer",
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * m_cubeTexture.GetArraySize(), // 6 slices
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_globalCounterBuffer.CreateBufferUAV(0, NULL, &m_globalCounter);
    }

    void SPDCS::OnDestroy()
    {
        m_globalCounterBuffer.OnDestroy();
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

    void SPDCS::Draw(ID3D12GraphicsCommandList2 *pCommandList)
    {
        UserMarker marker(pCommandList, "SPDCS");

        varAU2(dispatchThreadGroupCountXY);
        varAU2(workGroupOffset); // needed if Left and Top are not 0,0
        varAU2(numWorkGroupsAndMips);
        varAU4(rectInfo) = initAU4(0, 0, m_cubeTexture.GetWidth(), m_cubeTexture.GetHeight()); // left, top, width, height
        SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        // downsample
        uint32_t dispatchX = dispatchThreadGroupCountXY[0];
        uint32_t dispatchY = dispatchThreadGroupCountXY[1];
        uint32_t dispatchZ = m_cubeTexture.GetArraySize();

        D3D12_GPU_VIRTUAL_ADDRESS cbHandle;
        uint32_t* pConstMem;
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            m_pConstantBufferRing->AllocConstantBuffer(sizeof(SpdLinearSamplerConstants), (void**)&pConstMem, &cbHandle);
            SpdLinearSamplerConstants constants;
            constants.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
            constants.mips = numWorkGroupsAndMips[1];
            constants.workGroupOffset[0] = workGroupOffset[0];
            constants.workGroupOffset[1] = workGroupOffset[1];
            constants.invInputSize[0] = 1.0f / m_cubeTexture.GetWidth();
            constants.invInputSize[1] = 1.0f / m_cubeTexture.GetHeight();
            memcpy(pConstMem, &constants, sizeof(SpdLinearSamplerConstants));
        }
        else {
            m_pConstantBufferRing->AllocConstantBuffer(sizeof(SpdConstants), (void**)&pConstMem, &cbHandle);
            SpdConstants constants;
            constants.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
            constants.mips = numWorkGroupsAndMips[1];
            constants.workGroupOffset[0] = workGroupOffset[0];
            constants.workGroupOffset[1] = workGroupOffset[1];
            memcpy(pConstMem, &constants, sizeof(SpdConstants));
        }

        // Bind Descriptor heaps and the root signature
        //                
        ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
        pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
        pCommandList->SetComputeRootSignature(m_pRootSignature);

        // Bind Descriptor the descriptor sets
        //                
        int params = 0;
        pCommandList->SetComputeRootConstantBufferView(params++, cbHandle);
        pCommandList->SetComputeRootDescriptorTable(params++, m_globalCounter.GetGPU());
        if (m_spdLoad == SPDLoad::SPDLinearSampler)
        {
            pCommandList->SetComputeRootDescriptorTable(params++, m_UAV[0].GetGPU(5));
        }
        else {
            pCommandList->SetComputeRootDescriptorTable(params++, m_UAV[0].GetGPU(6));
        }
        // bind UAVs
        pCommandList->SetComputeRootDescriptorTable(params++, m_UAV[0].GetGPU());

        // bind SRV
        if (m_spdLoad == SPDLoad::SPDLinearSampler) {
            pCommandList->SetComputeRootDescriptorTable(params++, m_sourceSRV.GetGPU());
        }
        // Bind Pipeline
        //
        pCommandList->SetPipelineState(m_pPipeline);

        // set counter to 0
        pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_globalCounterBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, 0));

        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER pParams[6];
        for (int i = 0; i < 6; i++)
        {
            pParams[i] = { m_globalCounterBuffer.GetResource()->GetGPUVirtualAddress() + sizeof(uint32_t) * i, 0 };
        }
        pCommandList->WriteBufferImmediate(6, pParams, NULL); // 6 counter per slice, each initialized to 0

        D3D12_RESOURCE_BARRIER resourceBarriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_globalCounterBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0),
            CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        pCommandList->ResourceBarrier(2, resourceBarriers);

        // Dispatch
        //
        pCommandList->Dispatch(dispatchX, dispatchY, dispatchZ);
        pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_cubeTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    void SPDCS::GUI(int *pSlice)
    {
        bool opened = true;
        std::string header = "Downsample";
        ImGui::Begin(header.c_str(), &opened);

        std::string downsampleHeader = "SPD CS";
        if (m_spdLoad == SPDLoad::SPDLoad) {
            downsampleHeader += " Load";
        }
        else {
            downsampleHeader += " Linear Sampler";
        }

        if (m_spdWaveOps == SPDWaveOps::SPDWaveOps)
        {
            downsampleHeader += " WaveOps";
        }
        else {
            downsampleHeader += " No WaveOps";
        }

        if (m_spdPacked == SPDPacked::SPDNonPacked)
        {
            downsampleHeader += " Non Packed";
        }
        else {
            downsampleHeader += " Packed";
        }

        if (ImGui::CollapsingHeader(downsampleHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
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
                ImGui::Image((ImTextureID)&m_SRV[*pSlice * m_cubeTexture.GetMipCount() + i], ImVec2(static_cast<float>(512 >> i), static_cast<float>(512 >> i)));
            }
        }

        ImGui::End();
    }
}