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

#include "SPD_Sample.h"

const bool VALIDATION_ENABLED = true;

SPD_Sample::SPD_Sample(LPCSTR name) : FrameworkWindows(name)
{
    m_lastFrameTime = MillisecondsNow();
    m_time = 0;
    m_bPlay = true;

    m_pGltfLoader = NULL;
}

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SPD_Sample::OnCreate(HWND hWnd)
{
    // Create Device
    //
    m_device.OnCreate("FFX_SPD_Sample", "Cauldron", VALIDATION_ENABLED, hWnd);
    m_device.CreatePipelineCache();

    //init the shader compiler
    CreateShaderCache();

    // Create Swapchain
    //
    uint32_t dwNumberOfBackBuffers = 2;
    m_swapChain.OnCreate(&m_device, dwNumberOfBackBuffers, hWnd);

    // Create a instance of the renderer and initialize it, we need to do that for each GPU
    //
    m_Node = new SPD_Renderer();
    m_Node->OnCreate(&m_device, &m_swapChain);

    // init GUI (non gfx stuff)
    //
    ImGUI_Init((void *)hWnd);

    // Init Camera, looking at the origin
    //
    m_roll = 0.0f;
    m_pitch = 0.0f;
    m_distance = 3.5f;

    // init GUI state   
    m_state.toneMapper = 0;
    m_state.skyDomeType = 0;
    m_state.exposure = 1.0f;
    m_state.iblFactor = 2.0f;
    m_state.emmisiveFactor = 1.0f;
    m_state.bDrawLightFrustum = false;
    m_state.bDrawBoundingBoxes = false;
    m_state.camera.LookAt(m_roll, m_pitch, m_distance, XMVectorSet(0, 0, 0, 0));

    m_state.spotlightCount = 1;

    m_state.spotlight[0].intensity = 10.0f;
    m_state.spotlight[0].color = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_state.spotlight[0].light.SetFov(XM_PI / 2.0f, 1024, 1024, 0.1f, 100.0f);
    m_state.spotlight[0].light.LookAt(XM_PI / 2.0f, 0.58f, 3.5f, XMVectorSet(0, 0, 0, 0));

    m_state.downsampler = Downsampler::SPD_CS;
    m_state.spdVersion = SPD_Version::SPD_WaveOps;
    m_state.spdPacked = SPD_Packed::SPD_Non_Packed;
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void SPD_Sample::OnDestroy()
{
    ImGUI_Shutdown();
    
    m_device.GPUFlush();

    // Fullscreen state should always be false before exiting the app.
    m_swapChain.SetFullScreen(false);

    m_Node->UnloadScene();
    m_Node->OnDestroyWindowSizeDependentResources();
    m_Node->OnDestroy();

    delete m_Node;

    m_swapChain.OnDestroyWindowSizeDependentResources();
    m_swapChain.OnDestroy();

    //shut down the shader compiler 
    DestroyShaderCache(&m_device);

    if (m_pGltfLoader)
    {
        delete m_pGltfLoader;
        m_pGltfLoader = NULL;
    }

    m_device.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnEvent
//
//--------------------------------------------------------------------------------------
bool SPD_Sample::OnEvent(MSG msg)
{
    if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
        return true;

    return true;
}

//--------------------------------------------------------------------------------------
//
// SetFullScreen
//
//--------------------------------------------------------------------------------------
void SPD_Sample::SetFullScreen(bool fullscreen)
{
    m_device.GPUFlush();

    m_swapChain.SetFullScreen(fullscreen);    
}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void SPD_Sample::OnResize(uint32_t width, uint32_t height)
{
    if (m_Width != width || m_Height != height)
    {
        // Flush GPU
        //
        m_device.GPUFlush();

        // If resizing but no minimizing
        //
        if (m_Width > 0 && m_Height > 0)
        {
            m_Node->OnDestroyWindowSizeDependentResources();
            m_swapChain.OnDestroyWindowSizeDependentResources();
        }

        m_Width = width;
        m_Height = height;

        // if resizing but not minimizing the recreate it with the new size
        //
        if (m_Width > 0 && m_Height > 0)
        {
            m_swapChain.OnCreateWindowSizeDependentResources(m_Width, m_Height, false, DISPLAYMODE_SDR);
            m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height);
        }
    }
    m_state.camera.SetFov(XM_PI / 4, m_Width, m_Height, 0.1f, 1000.0f);
}

//--------------------------------------------------------------------------------------
//
// OnRender, updates the state from the UI, animates, transforms and renders the scene
//
//--------------------------------------------------------------------------------------
void SPD_Sample::OnRender()
{
    // Get timings
    //
    double timeNow = MillisecondsNow();
    m_deltaTime = timeNow - m_lastFrameTime;
    m_lastFrameTime = timeNow;

    // Build UI and set the scene state. Note that the rendering of the UI happens later.
    //    
    ImGUI_UpdateIO();
    ImGui::NewFrame();

    static int loadingStage = 0;
    if (loadingStage >= 0)
    {
        // LoadScene needs to be called a number of times, the scene is not fully loaded until it returns -1
        // This is done so we can display a progress bar when the scene is loading
        if (m_pGltfLoader == NULL)
        {
            m_pGltfLoader = new GLTFCommon();
            m_pGltfLoader->Load("..\\media\\DamagedHelmet\\glTF\\", "DamagedHelmet.gltf");
            loadingStage = 0;
        }
        loadingStage = m_Node->LoadScene(m_pGltfLoader, loadingStage);
    }
    else
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FrameBorderSize = 1.0f;

        bool opened = true;
        ImGui::Begin("Stats", &opened);

        if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Resolution       : %ix%i", m_Width, m_Height);
        }

        if (ImGui::CollapsingHeader("Downsampler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Downsample settings
            const char* downsampleItemNames[] =
            {
                "PS",
                "Multipass CS",
                "SPD CS",
                "SPD CS Linear Sampler",
            };
            ImGui::Combo("Downsampler Options", (int*)&m_state.downsampler, downsampleItemNames, _countof(downsampleItemNames));

            // Downsample settings
            const char* spdVersionItemNames[] =
            {
                "No-WaveOps",
                "WaveOps",
            };
            ImGui::Combo("SPD Version", (int*)&m_state.spdVersion, spdVersionItemNames, _countof(spdVersionItemNames));

            // NON-PACKED or PACKED Version
            const char* spdPackedNames[] =
            {
                "Non-Packed",
                "Packed",
            };
            ImGui::Combo("SPD Non-Packed / Packed Version", (int*)&m_state.spdPacked, spdPackedNames, _countof(spdPackedNames));
        }

        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("exposure", &m_state.exposure, 0.0f, 2.0f);
            ImGui::SliderFloat("emmisive", &m_state.emmisiveFactor, 1.0f, 1000.0f, NULL, 1.0f); 
            ImGui::SliderFloat("iblFactor", &m_state.iblFactor, 0.0f, 2.0f);
        }

        const char * tonemappers[] = { "Timothy", "DX11DSK", "Reinhard", "Uncharted2Tonemap", "ACES", "No tonemapper" };
        ImGui::Combo("tone mapper", &m_state.toneMapper, tonemappers, _countof(tonemappers));
        
        const char * skyDomeType[] = { "Procedural Sky", "cubemap", "Simple clear" };
        ImGui::Combo("SkyDome", &m_state.skyDomeType, skyDomeType, _countof(skyDomeType));

        const char * cameraControl[] = { "WASD", "Orbit" };
        static int cameraControlSelected = 1;
        ImGui::Combo("Camera", &cameraControlSelected, cameraControl, _countof(cameraControl));

        if (ImGui::CollapsingHeader("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::vector<TimeStamp> timeStamps = m_Node->GetTimingValues();
            if (timeStamps.size() > 0)
            {
                for (uint32_t i = 1; i < timeStamps.size(); i++)
                {
                    float DeltaTime = ((float)(timeStamps[i].m_microseconds - timeStamps[i - 1].m_microseconds));
                    ImGui::Text("%-17s: %7.1f us", timeStamps[i].m_label.c_str(), DeltaTime);
                }

                //scrolling data and average computing
                static float values[128];
                values[127] = (float)(timeStamps.back().m_microseconds - timeStamps.front().m_microseconds);
                float average = values[0];
                for (uint32_t i = 0; i < 128 - 1; i++) { values[i] = values[i + 1]; average += values[i]; }
                average /= 128;

                ImGui::Text("%-17s: %7.1f us", "TotalGPUTime", average);
                ImGui::PlotLines("", values, 128, 0, "", 0.0f, 30000.0f, ImVec2(0, 80));
            }
        }

        ImGui::End();

        // If the mouse was not used by the GUI then it's for the camera
        //
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse == false) 
        {
            if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
            {
                m_roll -= io.MouseDelta.x / 100.f;
                m_pitch += io.MouseDelta.y / 100.f;
            }

            // Choose camera movement depending on setting
            //

            if (cameraControlSelected == 0)
            {
                //  WASD
                //
                m_state.camera.UpdateCameraWASD(m_roll, m_pitch, io.KeysDown, io.DeltaTime);
            }
            else if (cameraControlSelected == 1)
            {
                //  Orbiting
                //
                m_distance -= (float)io.MouseWheel / 3.0f;
                m_distance = std::max<float>(m_distance, 0.1f);

                bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

                m_state.camera.UpdateCameraPolar(m_roll, m_pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_distance );
            }
        }
    }
    
    // Set animation time
    //
    if (m_bPlay)
    {
        m_time += (float)m_deltaTime / 1000.0f;
    }    

    // Animate and transform the scene
    //
    if (m_pGltfLoader)
    {
        m_pGltfLoader->SetAnimationTime(0, m_time);
        m_pGltfLoader->TransformScene(0, XMMatrixIdentity());
    }

    m_state.time = m_time;

    // Do Render frame using AFR 
    //
    m_Node->OnRender(&m_state, &m_swapChain);
    
    m_swapChain.Present();
}


//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    LPCSTR Name = "FFX SPD SampleDX12 v1.0";
    uint32_t Width = 1920;
    uint32_t Height = 1080;

    // create new DX sample
    return RunFramework(hInstance, lpCmdLine, nCmdShow, Width, Height, new SPD_Sample(Name));
}
