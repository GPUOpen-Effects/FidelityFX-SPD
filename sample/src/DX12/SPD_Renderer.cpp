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

#include "SPD_Renderer.h"

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnCreate(Device* pDevice, SwapChain *pSwapChain)
{
    m_pDevice = pDevice;

    m_format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    // Initialize helpers

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount = 2000;
    const uint32_t srvDescriptorCount = 2000;
    const uint32_t uavDescriptorCount = 10;
    const uint32_t dsvDescriptorCount = 10;
    const uint32_t rtvDescriptorCount = 60;
    const uint32_t samplerDescriptorCount = 20;
    m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);

    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 20 * 1024 * 1024;    
    m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_resourceViewHeaps);

    // Create a 'static' pool for vertices, indices and constant buffers
    const uint32_t staticGeometryMemSize = 128 * 1024 * 1024;
    m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");

    // initialize the GPU time stamps module
    m_GPUTimer.OnCreate(pDevice, backBufferCount);

    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    // for 4K textures we'll need 100Megs
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)

    // Create the depth buffer view
    m_resourceViewHeaps.AllocDSVDescriptor(1, &m_depthBufferDSV);

    // Create a Shadowmap atlas to hold 4 cascades/spotlights
    m_shadowMap.InitDepthStencil(pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, 2*1024, 2*1024, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
    m_resourceViewHeaps.AllocDSVDescriptor(1, &m_ShadowMapDSV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMapSRV);
    m_shadowMap.CreateDSV(0, &m_ShadowMapDSV);
    m_shadowMap.CreateSRV(0, &m_ShadowMapSRV);

    m_skyDome.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 4);
    m_skyDomeProc.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_format, 4);
    m_wireframe.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 4);
    m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);

    m_PSDownsampler.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_format);
    m_CSDownsampler.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, m_format);
    m_SPD_Versions.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, m_format);

    // Create tonemapping pass
    m_toneMapping.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

    // Initialize UI rendering resources
    m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat());

    m_resourceViewHeaps.AllocRTVDescriptor(1, &m_HDRRTV);
    m_resourceViewHeaps.AllocRTVDescriptor(1, &m_HDRRTVMSAA);

    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_HDRSRV);

    // Make sure upload heap has finished uploading before continuing
#if (USE_VID_MEM==true)
    m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
    m_UploadHeap.FlushAndFinish();
#endif
}

//--------------------------------------------------------------------------------------
//
// OnDestroy 
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnDestroy()
{
    m_toneMapping.OnDestroy();
    m_ImGUI.OnDestroy();

    m_PSDownsampler.OnDestroy();
    m_CSDownsampler.OnDestroy();
    m_SPD_Versions.OnDestroy();

    m_wireframeBox.OnDestroy();
    m_wireframe.OnDestroy();
    m_skyDomeProc.OnDestroy();
    m_skyDome.OnDestroy();
    m_shadowMap.OnDestroy();

    m_UploadHeap.OnDestroy();
    m_GPUTimer.OnDestroy();
    m_VidMemBufferPool.OnDestroy();
    m_ConstantBufferRing.OnDestroy();
    m_resourceViewHeaps.OnDestroy();
    m_CommandListRing.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnCreateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, uint32_t Width, uint32_t Height)
{
    m_Width = Width;
    m_Height = Height;

    // Set the viewport
    //
    m_viewPort = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };

    // Create scissor rectangle
    //
    m_RectScissor = { 0, 0, (LONG)Width, (LONG)Height };

    // Create depth buffer    
    //
    m_depthBuffer.InitDepthStencil(m_pDevice, "depthbuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, Width, Height, 1, 1, 4, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE));
    m_depthBuffer.CreateDSV(0, &m_depthBufferDSV);

    // Create Texture + RTV with x4 MSAA
    //
    CD3DX12_RESOURCE_DESC RDescMSAA = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, Width, Height, 1, 1, 4, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    m_HDRMSAA.InitRenderTarget(m_pDevice, "HDRMSAA", &RDescMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_HDRMSAA.CreateRTV(0, &m_HDRRTVMSAA);

    // Create Texture + RTV, to hold the resolved scene 
    //
    CD3DX12_RESOURCE_DESC RDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    m_HDR.InitRenderTarget(m_pDevice, "HDR", &RDesc, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_HDR.CreateSRV(0, &m_HDRSRV);
    m_HDR.CreateRTV(0, &m_HDRRTV);   

    // update downscaling effect
    //
    {
        int resolution = max(m_Width, m_Height);
        int mipLevel = (static_cast<int>(min(1.0f + floor(log2(resolution)), 12)) - 1);

        m_PSDownsampler.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR, mipLevel);
        m_CSDownsampler.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR, mipLevel);
        m_SPD_Versions.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR);
    }
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnDestroyWindowSizeDependentResources()
{
    m_PSDownsampler.OnDestroyWindowSizeDependentResources();
    m_CSDownsampler.OnDestroyWindowSizeDependentResources();
    m_SPD_Versions.OnDestroyWindowSizeDependentResources();

    m_HDR.OnDestroy();
    m_HDRMSAA.OnDestroy();
    m_depthBuffer.OnDestroy();
}


//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int SPD_Renderer::LoadScene(GLTFCommon *pGLTFCommon, int stage)
{
    // show loading progress
    //
    ImGui::OpenPopup("Loading");
    if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        float progress = (float)stage / 12.0f;
        ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
        ImGui::EndPopup();
    }

    // Loading stages
    //
    if (stage == 0)
    {
    }
    else if (stage == 5)
    {
        Profile p("m_pGltfLoader->Load");

        m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
        m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
    }
    else if (stage == 6)
    {
        Profile p("LoadTextures");

        // here we are loading onto the GPU all the textures and the inverse matrices
        // this data will be used to create the PBR and Depth passes       
        m_pGLTFTexturesAndBuffers->LoadTextures();
    }
    else if (stage == 7)
    {
        Profile p("m_gltfDepth->OnCreate");

        //create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
        m_gltfDepth = new GltfDepthPass();
        m_gltfDepth->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers
        );
    }
    else if (stage == 8)
    {
        Profile p("m_gltfPBR->OnCreate");
        
        // same thing as above but for the PBR pass
        m_gltfPBR = new GltfPbrPass();
        m_gltfPBR->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            &m_skyDome,
            false,
            m_HDRMSAA.GetFormat(),
            DXGI_FORMAT_UNKNOWN,
            DXGI_FORMAT_UNKNOWN,
            4
        );
    }
    else if (stage == 9)
    {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
        m_gltfBBox->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            &m_wireframe
        );
#if (USE_VID_MEM==true)
        // we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
#endif    
    }
    else if (stage == 10)
    {
        Profile p("Flush");

        m_UploadHeap.FlushAndFinish();

#if (USE_VID_MEM==true)
        //once everything is uploaded we dont need he upload heaps anymore
        m_VidMemBufferPool.FreeUploadHeap();
#endif    
        // tell caller that we are done loading the map
        return -1;
    }

    stage++;
    return stage;
}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::UnloadScene()
{
    if (m_gltfPBR)
    {
        m_gltfPBR->OnDestroy();
        delete m_gltfPBR;
        m_gltfPBR = NULL;
    }

    if (m_gltfDepth)
    {
        m_gltfDepth->OnDestroy();
        delete m_gltfDepth;
        m_gltfDepth = NULL;
    }

    if (m_gltfBBox)
    {
        m_gltfBBox->OnDestroy();
        delete m_gltfBBox;
        m_gltfBBox = NULL;
    }

    if (m_pGLTFTexturesAndBuffers)
    {
        m_pGLTFTexturesAndBuffers->OnDestroy();
        delete m_pGLTFTexturesAndBuffers;
        m_pGLTFTexturesAndBuffers = NULL;
    }

}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnRender(State *pState, SwapChain *pSwapChain)
{
    // Timing values
    //
    UINT64 gpuTicksPerSecond;
    m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&gpuTicksPerSecond);
    
    // Let our resource managers do some house keeping 
    //
    m_ConstantBufferRing.OnBeginFrame();
    m_GPUTimer.OnBeginFrame(gpuTicksPerSecond, &m_TimeStamps);
    
    // Sets the perFrame data (Camera and lights data), override as necessary and set them as constant buffers --------------
    //
    per_frame *pPerFrame = NULL;
    if (m_pGLTFTexturesAndBuffers)
    {
        pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->camera);

        // Set some lighting factors
        pPerFrame->iblFactor = pState->iblFactor;
        pPerFrame->emmisiveFactor = pState->emmisiveFactor;

        // Set shadowmaps bias and an index that indicates the rectangle of the atlas in which depth will be rendered
        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Spot))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 70.0f / 100000.0f;
            }
            else if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Directional))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 1000.0f / 100000.0f;
            }
            else
            {
                pPerFrame->lights[i].shadowMapIndex = -1;   // no shadow for this light
            }
        }

        m_pGLTFTexturesAndBuffers->SetPerFrameConstants();
        m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
    }

    // command buffer calls
    //    
    ID3D12GraphicsCommandList2* pCmdLst1 = m_CommandListRing.GetNewCommandList();

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

    // Clear GBuffer and depth stencil
    //
    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clears -----------------------------------------------------------------------
    //
    pCmdLst1->ClearDepthStencilView(m_ShadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear Shadow Map");

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    pCmdLst1->ClearRenderTargetView(m_HDRRTVMSAA.GetCPU(), clearColor, 0, nullptr);
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear HDR");

    pCmdLst1->ClearDepthStencilView(m_depthBufferDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear Depth");

    // Render to shadow map atlas for spot lights ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL)
    {        
        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if (pPerFrame->lights[i].type != LightType_Spot)
                continue;

            // Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
            uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
            uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
            uint32_t viewportWidth = m_shadowMap.GetWidth() / 2;
            uint32_t viewportHeight = m_shadowMap.GetHeight() / 2;
            SetViewportAndScissor(pCmdLst1, viewportOffsetsX[i] * viewportWidth, viewportOffsetsY[i] * viewportHeight, viewportWidth, viewportHeight);
            pCmdLst1->OMSetRenderTargets(0, NULL, true, &m_ShadowMapDSV.GetCPU());

            GltfDepthPass::per_frame *cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants();
            cbDepthPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

            m_gltfDepth->Draw(pCmdLst1);
            
            m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow map");
            shadowMapIndex++;
        }        
    }

    // Render Scene to the MSAA HDR RT ------------------------------------------------
    //
    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    pCmdLst1->RSSetViewports(1, &m_viewPort);
    pCmdLst1->RSSetScissorRects(1, &m_RectScissor);
    pCmdLst1->OMSetRenderTargets(1, &m_HDRRTVMSAA.GetCPU(), true, &m_depthBufferDSV.GetCPU());

    if (pPerFrame != NULL)
    {
        // Render skydome
        //
        if (pState->skyDomeType == 1)
        {
            XMMATRIX clipToView = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
            m_skyDome.Draw(pCmdLst1, clipToView);
            m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome");
        }
        else if (pState->skyDomeType == 0)
        {
            SkyDomeProc::Constants skyDomeConstants;
            skyDomeConstants.invViewProj = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
            skyDomeConstants.vSunDirection = XMVectorSet(1.0f, 0.05f, 0.0f, 0.0f);
            skyDomeConstants.turbidity = 10.0f;
            skyDomeConstants.rayleigh = 2.0f;
            skyDomeConstants.mieCoefficient = 0.005f;
            skyDomeConstants.mieDirectionalG = 0.8f;
            skyDomeConstants.luminance = 1.0f;
            skyDomeConstants.sun = false;
            m_skyDomeProc.Draw(pCmdLst1, skyDomeConstants);

            m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome Proc");
        }

        // Render scene to color buffer
        //    
        if (m_gltfPBR && pPerFrame != NULL)
        {
            //set per frame constant buffer values
            m_gltfPBR->Draw(pCmdLst1, &m_ShadowMapSRV);
        }

        // draw object's bounding boxes
        //
        if (m_gltfBBox && pPerFrame != NULL)
        {
            if (pState->bDrawBoundingBoxes)
            {
                m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraViewProj);

                m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
            }
        }

        // draw light's frustums
        //
        if (pState->bDrawLightFrustum && pPerFrame != NULL)
        {
            UserMarker(pCmdLst1, "light frustrums");

            XMVECTOR vCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
            XMVECTOR vRadius = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
            XMVECTOR vColor  = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
            for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
            {
                XMMATRIX spotlightMatrix = XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
                XMMATRIX worldMatrix = spotlightMatrix * pPerFrame->mCameraViewProj;
                m_wireframeBox.Draw(pCmdLst1, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
            }

            m_GPUTimer.GetTimeStamp(pCmdLst1, "Light's frustum");
        }
    }
    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Rendering Scene");

    // Resolve MSAA ------------------------------------------------------------------------
    //
    {
        UserMarker(pCmdLst1, "Resolving MSAA");

        D3D12_RESOURCE_BARRIER preResolve[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_HDRMSAA.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
        };
        pCmdLst1->ResourceBarrier(2, preResolve);

        pCmdLst1->ResolveSubresource(m_HDR.GetResource(), 0, m_HDRMSAA.GetResource(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);

        D3D12_RESOURCE_BARRIER postResolve[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_HDRMSAA.GetResource(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        pCmdLst1->ResourceBarrier(2, postResolve);

        m_GPUTimer.GetTimeStamp(pCmdLst1, "Resolve");
    }

    // Post proc---------------------------------------------------------------------------
    //
    {
        switch (pState->downsampler)
        {
        case Downsampler::PS:
            m_PSDownsampler.Draw(pCmdLst1);
            m_PSDownsampler.Gui();
            break;
        case Downsampler::Multipass_CS:
            m_CSDownsampler.Draw(pCmdLst1);
            m_CSDownsampler.Gui();
            break;
        case Downsampler::SPD_CS:
            m_SPD_Versions.Dispatch(pCmdLst1, pState->spdVersion, pState->spdPacked);
            m_SPD_Versions.Gui(pState->spdVersion, pState->spdPacked);
            break;
        case Downsampler::SPD_CS_Linear_Sampler:
            m_SPD_Versions.DispatchLinearSamplerVersion(pCmdLst1, pState->spdVersion, pState->spdPacked);
            m_SPD_Versions.GuiLinearSamplerVersion(pState->spdVersion, pState->spdPacked);
            break;
        }

        m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsampler");
    }

    ThrowIfFailed(pCmdLst1->Close());
    ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

    // Wait for swapchain (we are going to render to it) -----------------------------------
    //
    pSwapChain->WaitForSwapChain();
    m_pDevice->GPUFlush();
    m_CommandListRing.OnBeginFrame();

    ID3D12GraphicsCommandList* pCmdLst2 = m_CommandListRing.GetNewCommandList();

    // Tonemapping ------------------------------------------------------------------------
    //
    {
        pCmdLst2->RSSetViewports(1, &m_viewPort);
        pCmdLst2->RSSetScissorRects(1, &m_RectScissor);
        pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);

        m_toneMapping.Draw(pCmdLst2, &m_HDRSRV, pState->exposure, pState->toneMapper);
        m_GPUTimer.GetTimeStamp(pCmdLst2, "Tone mapping");
    }

    // Render HUD  ------------------------------------------------------------------------
    //
    {
        pCmdLst2->RSSetViewports(1, &m_viewPort);
        pCmdLst2->RSSetScissorRects(1, &m_RectScissor);
        pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);

        m_ImGUI.Draw(pCmdLst2);

        m_GPUTimer.GetTimeStamp(pCmdLst2, "ImGUI Rendering");
    }

    // Transition swapchain into present mode
    D3D12_RESOURCE_BARRIER postGUI[2] = {
           CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
           CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
    };
    pCmdLst2->ResourceBarrier(2, postGUI);

    m_GPUTimer.OnEndFrame();

    m_GPUTimer.CollectTimings(pCmdLst2);

    // Close & Submit the command list ----------------------------------------------------
    //
    ThrowIfFailed(pCmdLst2->Close());

    ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);
}