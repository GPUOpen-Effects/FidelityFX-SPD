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

#include "CSDownsampler.h"
#include "PSDownsampler.h"
#include "SPD_Versions.h"

static const int backBufferCount = 3;

#define USE_VID_MEM true

using namespace CAULDRON_VK;

//
// This class deals with the GPU side of the sample.
//

enum class Downsampler
{
    PS,
    Multipass_CS,
    SPD_CS,
    SPD_CS_Linear_Sampler,
};

class SPD_Renderer
{
public:
    struct Spotlight
    {
        Camera light;
        XMVECTOR color;
        float intensity;
    };

    struct State
    {
        float time;
        Camera camera;

        float exposure;
        float iblFactor;
        float emmisiveFactor;

        int   toneMapper;
        int   skyDomeType;
        bool  bDrawBoundingBoxes;

        uint32_t  spotlightCount;
        Spotlight spotlight[4];
        bool  bDrawLightFrustum;

        Downsampler downsampler;
        SPD_Version spdVersion;
        SPD_Packed spdPacked;
    };

    void OnCreate(Device *pDevice, SwapChain *pSwapChain);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, uint32_t Width, uint32_t Height);
    void OnDestroyWindowSizeDependentResources();

    int LoadScene(GLTFCommon *pGLTFCommon, int stage = 0);
    void UnloadScene();

    const std::vector<TimeStamp> &GetTimingValues() { return m_TimeStamps; }

    void OnRender(State *pState, SwapChain *pSwapChain);

private:
    Device *m_pDevice;

    uint32_t m_Width;
    uint32_t m_Height;

    VkRect2D                        m_scissor;
    VkViewport                      m_viewport;

    // Initialize helper classes
    ResourceViewHeaps               m_resourceViewHeaps;
    UploadHeap                      m_UploadHeap;
    DynamicBufferRing               m_ConstantBufferRing;
    StaticBufferPool                m_VidMemBufferPool;
    StaticBufferPool                m_SysMemBufferPool;
    CommandListRing                 m_CommandListRing;
    GPUTimestamps                   m_GPUTimer;

    //gltf passes
    GLTFTexturesAndBuffers         *m_pGLTFTexturesAndBuffers;
    GltfPbrPass                    *m_gltfPBR;
    GltfDepthPass                  *m_gltfDepth;
    GltfBBoxPass                   *m_gltfBBox;

    // effects
    SkyDome                         m_skyDome;
    SkyDomeProc                     m_skyDomeProc;
    ToneMapping                     m_toneMapping;

    // downsampling - m_HDR
    PSDownsampler                   m_PSDownsampler;
    CSDownsampler                   m_CSDownsampler;
    SPD_Versions                    m_SPD_Versions;

    VkCommandPool                   m_CommandPool;
    VkCommandBuffer                 m_CommandBufferInit;

    // GUI
    ImGUI                           m_ImGUI;

    // Temporary render targets

    // depth buffer
    Texture                         m_depthBuffer;
    VkImageView                     m_depthBufferView;

    // shadowmaps
    Texture                         m_shadowMap;
    VkImageView                     m_shadowMapDSV;
    VkImageView                     m_shadowMapSRV;

    // MSAA RT
    Texture                         m_HDRMSAA;
    VkImageView                     m_HDRMSAASRV;

    // Resolved RT
    Texture                         m_HDR;
    VkImageView                     m_HDRSRV;
    VkImageView                     m_HDRUAV;

    // widgets
    Wireframe                       m_wireframe;
    WireframeBox                    m_wireframeBox;

    VkRenderPass                    m_render_pass_shadow;
    VkRenderPass                    m_render_pass_HDR_MSAA;
    VkRenderPass                    m_render_pass_PBR_HDR;

    VkFramebuffer                   m_pFrameBuffer_shadow;
    VkFramebuffer                   m_pFrameBuffer_HDR_MSAA;
    VkFramebuffer                   m_pFrameBuffer_PBR_HDR;

    std::vector<TimeStamp>          m_TimeStamps;

    VkFormat                        m_Format;
};
