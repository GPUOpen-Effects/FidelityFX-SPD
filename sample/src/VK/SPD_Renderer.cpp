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
void SPD_Renderer::OnCreate(Device *pDevice, SwapChain *pSwapChain)
{
    m_Format = VK_FORMAT_R16G16B16A16_SFLOAT;

    m_pDevice = pDevice;

    // Initialize helpers

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount = 2000;
    const uint32_t srvDescriptorCount = 2000;
    const uint32_t uavDescriptorCount = 10;
    const uint32_t samplerDescriptorCount = 20;
    m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, samplerDescriptorCount);

    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer);

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 20 * 1024 * 1024;
    m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, "Uniforms");

    // Create a 'static' pool for vertices and indices 
    const uint32_t staticGeometryMemSize = 128 * 1024 * 1024;
    const uint32_t systemGeometryMemSize = 32 * 1024;    
    m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");
    m_SysMemBufferPool.OnCreate(pDevice, systemGeometryMemSize, false, "PostProcGeom");

    // initialize the GPU time stamps module
    m_GPUTimer.OnCreate(pDevice, backBufferCount);

    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    // for 4K textures we'll need 100Megs
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_UploadHeap.OnCreate(pDevice, staticGeometryMemSize);    // initialize an upload heap (uses suballocation for faster results)

    // Create a 2Kx2K Shadowmap atlas to hold 4 cascades/spotlights
    m_shadowMap.InitDepthStencil(m_pDevice, 2 * 1024, 2 * 1024, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, "ShadowMap");
    m_shadowMap.CreateSRV(&m_shadowMapSRV);
    m_shadowMap.CreateDSV(&m_shadowMapDSV);

    // Create render pass shadow
    //
    {
        /* Need attachments for render target and depth buffer */
        VkAttachmentDescription depthAttachments;
        AttachClearBeforeUse(m_shadowMap.GetFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachments);
        m_render_pass_shadow = CreateRenderPassOptimal(m_pDevice->GetDevice(), 0, NULL, &depthAttachments);

        // Create frame buffer, its size is now window dependant so we can do this here.
        //
        VkImageView attachmentViews[1] = { m_shadowMapDSV };
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = NULL;
        fb_info.renderPass = m_render_pass_shadow;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachmentViews;
        fb_info.width = m_shadowMap.GetWidth();
        fb_info.height = m_shadowMap.GetHeight();
        fb_info.layers = 1;
        VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_pFrameBuffer_shadow);
        assert(res == VK_SUCCESS);
    }

    // Create HDR MSAA render pass + clear, for the sky, PBR and Wireframe passes 
    //
    {
        VkAttachmentDescription colorAttachment, depthAttachment;
        AttachClearBeforeUse(m_Format, VK_SAMPLE_COUNT_4_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &colorAttachment);
        AttachClearBeforeUse(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_4_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &depthAttachment);
        m_render_pass_HDR_MSAA = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &colorAttachment, &depthAttachment);
    }

    // Create HDR render pass, for the GUI
    //
    {
        VkAttachmentDescription colorAttachment;
        AttachBlending(m_Format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachment);
        m_render_pass_PBR_HDR = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &colorAttachment, NULL);
    }

    m_skyDome.OnCreate(pDevice, m_render_pass_HDR_MSAA, &m_UploadHeap, m_Format, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", VK_SAMPLE_COUNT_4_BIT);
    m_skyDomeProc.OnCreate(pDevice, m_render_pass_HDR_MSAA, &m_UploadHeap, m_Format, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_4_BIT);
    m_wireframe.OnCreate(pDevice, m_render_pass_HDR_MSAA, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_4_BIT);
    m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);

    // Create downsampling passes

    m_PSDownsampler.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_Format);
    m_CSDownsampler.OnCreate(pDevice, &m_resourceViewHeaps, m_Format);
    m_SPD_Versions.OnCreate(pDevice, &m_resourceViewHeaps, m_Format);

    // Create tonemapping pass
    m_toneMapping.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_resourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing);

    // Initialize UI rendering resources
    m_ImGUI.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_UploadHeap, &m_ConstantBufferRing);

    // Make sure upload heap has finished uploading before continuing
#if (USE_VID_MEM==true)
    m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
    m_UploadHeap.FlushAndFinish();
#endif

    // Create allocator
    //
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.pNext = NULL;
    cmd_pool_info.queueFamilyIndex = pDevice->GetGraphicsQueueFamilyIndex();
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkResult res = vkCreateCommandPool(pDevice->GetDevice(), &cmd_pool_info, NULL, &m_CommandPool);
    assert(res == VK_SUCCESS);

    // Create command buffers
    //
    VkCommandBufferAllocateInfo cmd = {};
    cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.pNext = NULL;
    cmd.commandPool = m_CommandPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    res = vkAllocateCommandBuffers(pDevice->GetDevice(), &cmd, &m_CommandBufferInit);
    assert(res == VK_SUCCESS);
}

//--------------------------------------------------------------------------------------
//
// OnDestroy 
//
//--------------------------------------------------------------------------------------
void SPD_Renderer::OnDestroy()
{
    m_ImGUI.OnDestroy();
    m_toneMapping.OnDestroy();
    m_wireframeBox.OnDestroy();
    m_wireframe.OnDestroy();
    m_skyDomeProc.OnDestroy();
    m_skyDome.OnDestroy();
    m_shadowMap.OnDestroy();

    m_PSDownsampler.OnDestroy();
    m_CSDownsampler.OnDestroy();
    m_SPD_Versions.OnDestroy();

    vkDestroyImageView(m_pDevice->GetDevice(), m_shadowMapDSV, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_shadowMapSRV, nullptr);

    vkDestroyRenderPass(m_pDevice->GetDevice(), m_render_pass_shadow, nullptr);
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_render_pass_HDR_MSAA, nullptr);
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_render_pass_PBR_HDR, nullptr);

    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_pFrameBuffer_shadow, nullptr);

    m_UploadHeap.OnDestroy();
    m_GPUTimer.OnDestroy();
    m_VidMemBufferPool.OnDestroy();
    m_SysMemBufferPool.OnDestroy();
    m_ConstantBufferRing.OnDestroy();
    m_resourceViewHeaps.OnDestroy();
    m_CommandListRing.OnDestroy();

    vkFreeCommandBuffers(m_pDevice->GetDevice(), m_CommandPool, 1, &m_CommandBufferInit);
    vkDestroyCommandPool(m_pDevice->GetDevice(), m_CommandPool, NULL);
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
    m_viewport.x = 0;
    m_viewport.y = (float)Height;
    m_viewport.width = (float)Width;
    m_viewport.height = -(float)(Height);
    m_viewport.minDepth = (float)0.0f;
    m_viewport.maxDepth = (float)1.0f;

    // Create scissor rectangle
    //
    m_scissor.extent.width = Width;
    m_scissor.extent.height = Height;
    m_scissor.offset.x = 0;
    m_scissor.offset.y = 0;

    // Create depth buffer
    //
    m_depthBuffer.InitDepthStencil(m_pDevice, Width, Height, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_4_BIT, "DepthBuffer");
    m_depthBuffer.CreateDSV(&m_depthBufferView);

    // Create Texture + RTV with x4 MSAA
    //
    m_HDRMSAA.InitRenderTarget(m_pDevice, m_Width, m_Height, m_Format, VK_SAMPLE_COUNT_4_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT), false, "HDRMSAA");
    m_HDRMSAA.CreateRTV(&m_HDRMSAASRV);

    // Create Texture + RTV, to hold the resolved scene 
    //
    m_HDR.InitRenderTarget(m_pDevice, m_Width, m_Height, m_Format, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT), false, "HDR");
    m_HDR.CreateSRV(&m_HDRSRV);
    m_HDR.CreateSRV(&m_HDRUAV);

    // Create framebuffer for the MSAA RT
    //
    {
        VkImageView attachments[2] = { m_HDRMSAASRV, m_depthBufferView };
        VkImageView attachments_PBR_HDR[1] = { m_HDRSRV };

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = NULL;
        fb_info.renderPass = m_render_pass_HDR_MSAA;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments;
        fb_info.width = Width;
        fb_info.height = Height;
        fb_info.layers = 1;

        VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_pFrameBuffer_HDR_MSAA);
        assert(res == VK_SUCCESS);

        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachments_PBR_HDR;
        fb_info.renderPass = m_render_pass_PBR_HDR;
        res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_pFrameBuffer_PBR_HDR);
        assert(res == VK_SUCCESS);
    }

    // update downscaling effect
    //
    {
        int resolution = max(m_Width, m_Height);
        int mipLevel = (static_cast<int>(min(1.0f + floor(log2(resolution)), 12)) - 1);

        {
            VkCommandBufferBeginInfo cmd_buf_info;
            cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmd_buf_info.pNext = NULL;
            cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            cmd_buf_info.pInheritanceInfo = NULL;
            VkResult res = vkBeginCommandBuffer(m_CommandBufferInit, &cmd_buf_info);
            assert(res == VK_SUCCESS);
        }

        m_PSDownsampler.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR, mipLevel);
        m_CSDownsampler.OnCreateWindowSizeDependentResources(m_CommandBufferInit, m_Width, m_Height, &m_HDR, mipLevel);
        m_SPD_Versions.OnCreateWindowSizeDependentResources(m_CommandBufferInit, m_Width, m_Height, &m_HDR);

        {
            VkResult res = vkEndCommandBuffer(m_CommandBufferInit);
            assert(res == VK_SUCCESS);

            VkSubmitInfo submit_info;
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.pNext = NULL;
            submit_info.waitSemaphoreCount = 0;
            submit_info.pWaitSemaphores = NULL;
            submit_info.pWaitDstStageMask = NULL;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &m_CommandBufferInit;
            submit_info.signalSemaphoreCount = 0;
            submit_info.pSignalSemaphores = NULL;
            res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
            assert(res == VK_SUCCESS);
        }
    }

    m_toneMapping.UpdatePipelines(pSwapChain->GetRenderPass()),

    m_ImGUI.UpdatePipeline(pSwapChain->GetRenderPass());
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

    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_pFrameBuffer_HDR_MSAA, nullptr);
    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_pFrameBuffer_PBR_HDR, nullptr);

    vkDestroyImageView(m_pDevice->GetDevice(), m_depthBufferView, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_HDRMSAASRV, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_HDRSRV, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_HDRUAV, nullptr);
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
            m_render_pass_shadow,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers
        );
#if (USE_VID_MEM==true)
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
        m_UploadHeap.FlushAndFinish();
#endif
    }
    else if (stage == 8)
    {
        Profile p("m_gltfPBR->OnCreate");

        // same thing as above but for the PBR pass
        m_gltfPBR = new GltfPbrPass();
        m_gltfPBR->OnCreate(
        m_pDevice,
        m_render_pass_HDR_MSAA,
        &m_UploadHeap,
        &m_resourceViewHeaps,
        &m_ConstantBufferRing,
        &m_VidMemBufferPool,
        m_pGLTFTexturesAndBuffers,
        &m_skyDome,
        m_shadowMapSRV,
        true,
        true,
        false,
        VK_SAMPLE_COUNT_4_BIT
        );
#if (USE_VID_MEM==true)
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
        m_UploadHeap.FlushAndFinish();
#endif
    }
    else if (stage == 9)
    {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
        m_gltfBBox->OnCreate(
        m_pDevice,
        m_render_pass_HDR_MSAA,
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
    // Let our resource managers do some house keeping 
    //
    m_ConstantBufferRing.OnBeginFrame();

    // command buffer calls
    //    
    VkCommandBuffer cmd_buf = m_CommandListRing.GetNewCommandList();

    {
        VkCommandBufferBeginInfo cmd_buf_info;
        cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buf_info.pNext = NULL;
        cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmd_buf_info.pInheritanceInfo = NULL;
        VkResult res = vkBeginCommandBuffer(cmd_buf, &cmd_buf_info);
        assert(res == VK_SUCCESS);
    }

    m_GPUTimer.OnBeginFrame(cmd_buf, &m_TimeStamps);

    // Sets the perFrame data (Camera and lights data), override as necessary and set them as constant buffers --------------
    //
    per_frame *pPerFrame = NULL;
    if (m_pGLTFTexturesAndBuffers)
    {
        pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->camera);

        // Set some lighting factors
        pPerFrame->iblFactor = pState->iblFactor;
        pPerFrame->emmisiveFactor = pState->emmisiveFactor;

        // Up to 4 spotlights can have shadowmaps. Each spot the light has a shadowMap index which is used to find the sadowmap in the atlas
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

    // Render to shadow map atlas for spot lights ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL)
    {
        SetPerfMarkerBegin(cmd_buf, "ShadowPass");

        VkClearValue depth_clear_values[1];
        depth_clear_values[0].depthStencil.depth = 1.0f;
        depth_clear_values[0].depthStencil.stencil = 0;

        {
            VkRenderPassBeginInfo rp_begin;
            rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin.pNext = NULL;
            rp_begin.renderPass = m_render_pass_shadow;
            rp_begin.framebuffer = m_pFrameBuffer_shadow;
            rp_begin.renderArea.offset.x = 0;
            rp_begin.renderArea.offset.y = 0;
            rp_begin.renderArea.extent.width = m_shadowMap.GetWidth();
            rp_begin.renderArea.extent.height = m_shadowMap.GetHeight();
            rp_begin.clearValueCount = 1;
            rp_begin.pClearValues = depth_clear_values;

            vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            m_GPUTimer.GetTimeStamp(cmd_buf, "Clear Shadow Map");
        }

        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if (pPerFrame->lights[i].type != LightType_Spot || pPerFrame->lights[i].type == LightType_Directional)
                continue;

            // Set the RT's quadrant where to render the shadowmap (these viewport offsets need to match the ones in shadowFiltering.h)
            uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
            uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
            uint32_t viewportWidth = m_shadowMap.GetWidth() / 2;
            uint32_t viewportHeight = m_shadowMap.GetHeight() / 2;
            SetViewportAndScissor(cmd_buf, viewportOffsetsX[shadowMapIndex] * viewportWidth, viewportOffsetsY[shadowMapIndex] * viewportHeight, viewportWidth, viewportHeight);

            //set per frame constant buffer values
            GltfDepthPass::per_frame *cbPerFrame = m_gltfDepth->SetPerFrameConstants();
            cbPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

            m_gltfDepth->Draw(cmd_buf);

            m_GPUTimer.GetTimeStamp(cmd_buf, "Shadow maps");
            shadowMapIndex++;
        }
        vkCmdEndRenderPass(cmd_buf);
        
        SetPerfMarkerEnd(cmd_buf);
    }

    // Render Scene to the MSAA HDR RT ------------------------------------------------
    //
    {
        SetPerfMarkerBegin(cmd_buf, "Color pass");
        m_GPUTimer.GetTimeStamp(cmd_buf, "before color RP");
        VkClearValue clear_values[2];
        clear_values[0].color.float32[0] = 0.0f;
        clear_values[0].color.float32[1] = 0.0f;
        clear_values[0].color.float32[2] = 0.0f;
        clear_values[0].color.float32[3] = 0.0f;
        clear_values[1].depthStencil.depth = 1.0f;
        clear_values[1].depthStencil.stencil = 0;

        VkRenderPassBeginInfo rp_begin;
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.pNext = NULL;
        rp_begin.renderPass = m_render_pass_HDR_MSAA;
        rp_begin.framebuffer = m_pFrameBuffer_HDR_MSAA;
        rp_begin.renderArea.offset.x = 0;
        rp_begin.renderArea.offset.y = 0;
        rp_begin.renderArea.extent.width = m_Width;
        rp_begin.renderArea.extent.height = m_Height;
        rp_begin.clearValueCount = 2;
        rp_begin.pClearValues = clear_values;

        vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetScissor(cmd_buf, 0, 1, &m_scissor);
        vkCmdSetViewport(cmd_buf, 0, 1, &m_viewport);
        m_GPUTimer.GetTimeStamp(cmd_buf, "after color RP");
    }

    if (pPerFrame != NULL)
    {
        // Render skydome
        //
        if (pState->skyDomeType == 1)
        {
            XMMATRIX clipToView = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
            m_skyDome.Draw(cmd_buf, clipToView);

            m_GPUTimer.GetTimeStamp(cmd_buf, "Skydome cube");
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
            m_skyDomeProc.Draw(cmd_buf, skyDomeConstants);

            m_GPUTimer.GetTimeStamp(cmd_buf, "Skydome Proc");
        }

        // Render scene to color buffer
        //
        if (m_gltfBBox && pPerFrame != NULL)
        {
            m_gltfPBR->Draw(cmd_buf);
            m_GPUTimer.GetTimeStamp(cmd_buf, "Rendering Scene");
        }

        // draw object's bounding boxes
        //
        if (m_gltfBBox && pPerFrame != NULL)
        {
            if (pState->bDrawBoundingBoxes)
            {
                m_gltfBBox->Draw(cmd_buf, pPerFrame->mCameraViewProj);

                m_GPUTimer.GetTimeStamp(cmd_buf, "Bounding Box");
            }
        }

        // draw light's frustums
        //
        if (pState->bDrawLightFrustum && pPerFrame != NULL)
        {
            SetPerfMarkerBegin(cmd_buf, "light frustrum");

            XMVECTOR vCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
            XMVECTOR vRadius = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
            XMVECTOR vColor = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
            for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
            {
                XMMATRIX spotlightMatrix = XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
                XMMATRIX worldMatrix = spotlightMatrix * pPerFrame->mCameraViewProj;
                m_wireframeBox.Draw(cmd_buf, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
            }

            m_GPUTimer.GetTimeStamp(cmd_buf, "Light's frustum");

            SetPerfMarkerEnd(cmd_buf);
        }
    }

    {
        vkCmdEndRenderPass(cmd_buf);
        SetPerfMarkerEnd(cmd_buf);
    }

    // Resolve MSAA ------------------------------------------------------------------------
    //
    {
        SetPerfMarkerBegin(cmd_buf, "resolve MSAA");
        {
            VkImageMemoryBarrier barrier[2] = {};
            barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier[0].pNext = NULL;
            barrier[0].srcAccessMask = 0;
            barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[0].subresourceRange.baseMipLevel = 0;
            barrier[0].subresourceRange.levelCount = 1;
            barrier[0].subresourceRange.baseArrayLayer = 0;
            barrier[0].subresourceRange.layerCount = 1;
            barrier[0].image = m_HDR.Resource();

            barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier[1].pNext = NULL;
            barrier[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[1].subresourceRange.baseMipLevel = 0;
            barrier[1].subresourceRange.levelCount = 1;
            barrier[1].subresourceRange.baseArrayLayer = 0;
            barrier[1].subresourceRange.layerCount = 1;
            barrier[1].image = m_HDRMSAA.Resource();

            vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barrier);
        }

        {
            VkImageResolve re = {};
            re.srcOffset.x = 0;
            re.srcOffset.y = 0;
            re.extent.width = m_Width;
            re.extent.height = m_Height;
            re.extent.depth = 1;
            re.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            re.srcSubresource.layerCount = 1;
            re.dstOffset.x = 0;
            re.dstOffset.y = 0;
            re.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            re.dstSubresource.layerCount = 1;
            vkCmdResolveImage(cmd_buf, m_HDRMSAA.Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_HDR.Resource(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &re);
        }

        {
            VkImageMemoryBarrier barrier[2] = {};
            barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier[0].pNext = NULL;
            barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // we need to read from it for the post-processing
            barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[0].subresourceRange.baseMipLevel = 0;
            barrier[0].subresourceRange.levelCount = 1;
            barrier[0].subresourceRange.baseArrayLayer = 0;
            barrier[0].subresourceRange.layerCount = 1;
            barrier[0].image = m_HDR.Resource();

            barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier[1].pNext = NULL;
            barrier[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[1].subresourceRange.baseMipLevel = 0;
            barrier[1].subresourceRange.levelCount = 1;
            barrier[1].subresourceRange.baseArrayLayer = 0;
            barrier[1].subresourceRange.layerCount = 1;
            barrier[1].image = m_HDRMSAA.Resource();

            vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 2, barrier);
        }

        m_GPUTimer.GetTimeStamp(cmd_buf, "Resolve");
        SetPerfMarkerEnd(cmd_buf);
    }

    // Post proc---------------------------------------------------------------------------
    //
    {
        SetPerfMarkerBegin(cmd_buf, "post proc");

        switch (pState->downsampler)
        {
        case Downsampler::PS:
            m_PSDownsampler.Draw(cmd_buf);
            m_PSDownsampler.Gui();
            break;
        case Downsampler::Multipass_CS:
            m_CSDownsampler.Draw(cmd_buf);
            m_CSDownsampler.Gui();
            break;
        case Downsampler::SPD_CS:
            m_SPD_Versions.Dispatch(cmd_buf, pState->spdVersion, pState->spdPacked);
            m_SPD_Versions.Gui(pState->spdVersion, pState->spdPacked);
            break;
        case Downsampler::SPD_CS_Linear_Sampler:
            m_SPD_Versions.DispatchLinearSamplerVersion(cmd_buf, pState->spdVersion, pState->spdPacked);
            m_SPD_Versions.GuiLinearSamplerVersion(pState->spdVersion, pState->spdPacked);
            break;
        }

        m_GPUTimer.GetTimeStamp(cmd_buf, "Downsampler");

        SetPerfMarkerEnd(cmd_buf);
    }

    {
        VkResult res = vkEndCommandBuffer(cmd_buf);
        assert(res == VK_SUCCESS);

        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = NULL;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buf;
        submit_info.signalSemaphoreCount = 0;
        submit_info.pSignalSemaphores = NULL;
        res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
        assert(res == VK_SUCCESS);
    }
    

    // Wait for swapchain (we are going to render to it) -----------------------------------
    //

    int imageIndex = pSwapChain->WaitForSwapChain();

    m_CommandListRing.OnBeginFrame();

    VkCommandBuffer cmdBuf2 = m_CommandListRing.GetNewCommandList();

    {
        VkCommandBufferBeginInfo cmd_buf_info;
        cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buf_info.pNext = NULL;
        cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmd_buf_info.pInheritanceInfo = NULL;
        VkResult res = vkBeginCommandBuffer(cmdBuf2, &cmd_buf_info);
        assert(res == VK_SUCCESS);
    }
    SetPerfMarkerBegin(cmdBuf2, "rendering to swap chain");

    // prepare render pass
    {
        VkRenderPassBeginInfo rp_begin = {};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.pNext = NULL;
        rp_begin.renderPass = pSwapChain->GetRenderPass();
        rp_begin.framebuffer = pSwapChain->GetFramebuffer(imageIndex);
        rp_begin.renderArea.offset.x = 0;
        rp_begin.renderArea.offset.y = 0;
        rp_begin.renderArea.extent.width = m_Width;
        rp_begin.renderArea.extent.height = m_Height;
        rp_begin.clearValueCount = 0;
        rp_begin.pClearValues = NULL;
        vkCmdBeginRenderPass(cmdBuf2, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdSetScissor(cmdBuf2, 0, 1, &m_scissor);
    vkCmdSetViewport(cmdBuf2, 0, 1, &m_viewport);

    // Tonemapping ------------------------------------------------------------------------
        //
    {
        m_toneMapping.Draw(cmdBuf2, m_HDRSRV, pState->exposure, pState->toneMapper);
        m_GPUTimer.GetTimeStamp(cmdBuf2, "Tone mapping");
    }

    // Render HUD  ------------------------------------------------------------------------
    //
    {
        m_ImGUI.Draw(cmdBuf2);
        m_GPUTimer.GetTimeStamp(cmdBuf2, "ImGUI Rendering");
    }

    SetPerfMarkerEnd(cmdBuf2);

    m_GPUTimer.OnEndFrame();

    vkCmdEndRenderPass(cmdBuf2);


    // Close & Submit the command list ----------------------------------------------------
    //
    {
        VkResult res = vkEndCommandBuffer(cmdBuf2);
        assert(res == VK_SUCCESS);

        VkSemaphore ImageAvailableSemaphore;
        VkSemaphore RenderFinishedSemaphores;
        VkFence CmdBufExecutedFences;
        pSwapChain->GetSemaphores(&ImageAvailableSemaphore, &RenderFinishedSemaphores, &CmdBufExecutedFences);

        VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &ImageAvailableSemaphore;
        submit_info.pWaitDstStageMask = &submitWaitStage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmdBuf2;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &RenderFinishedSemaphores;

        res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info, CmdBufExecutedFences);
        assert(res == VK_SUCCESS);
    }
}
