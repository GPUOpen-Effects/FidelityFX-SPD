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
#include "Base\DynamicBufferRing.h"
#include "Base\StaticBufferPool.h"
#include "Base\UploadHeap.h"
#include "Base\Texture.h"
#include "Base\Helper.h"
#include "SPD_Versions.h"


namespace CAULDRON_VK
{
    void SPD_Versions::OnCreate(Device* pDevice, ResourceViewHeaps *pResourceViewHeaps, VkFormat outFormat)
    {
        m_pDevice = pDevice;

        // check if subgroup operations are supported, otherwise we need to fallback to the LDS only version
        if (pDevice->GetPhysicalDeviceSubgroupProperties().supportedOperations 
            & VK_SUBGROUP_FEATURE_QUAD_BIT)
        {
            m_spd_WaveOps_NonPacked.OnCreate(pDevice, pResourceViewHeaps, outFormat, false, false);
            m_spd_WaveOps_Packed.OnCreate(pDevice, pResourceViewHeaps, outFormat, false, true);

            m_spd_WaveOps_NonPacked_Linear_Sampler.OnCreate(pDevice, pResourceViewHeaps, outFormat, false, false);
            m_spd_WaveOps_Packed_Linear_Sampler.OnCreate(pDevice, pResourceViewHeaps, outFormat, false, true);
        }

        // fallback path
        m_spd_No_WaveOps_NonPacked.OnCreate(pDevice, pResourceViewHeaps, outFormat, true, false);
        m_spd_No_WaveOps_Packed.OnCreate(pDevice, pResourceViewHeaps, outFormat, true, true);

        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnCreate(pDevice, pResourceViewHeaps, outFormat, true, false);
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnCreate(pDevice, pResourceViewHeaps, outFormat, true, true);
    }

    void SPD_Versions::OnDestroy()
    {
        m_spd_No_WaveOps_NonPacked.OnDestroy();
        m_spd_No_WaveOps_Packed.OnDestroy();

        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnDestroy();
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnDestroy();

        if (m_pDevice->GetPhysicalDeviceSubgroupProperties().supportedOperations 
            & VK_SUBGROUP_FEATURE_QUAD_BIT)
        {
            m_spd_WaveOps_NonPacked.OnDestroy();
            m_spd_WaveOps_Packed.OnDestroy();

            m_spd_WaveOps_NonPacked_Linear_Sampler.OnDestroy();
            m_spd_WaveOps_Packed_Linear_Sampler.OnDestroy();
        }
    }

    uint32_t SPD_Versions::GetMaxMipLevelCount(uint32_t Width, uint32_t Height)
    {
        int resolution = max(Width, Height);
        return (static_cast<int>(min(1.0f + floor(log2(resolution)), 12)) - 1);
    }

    void SPD_Versions::OnCreateWindowSizeDependentResources(VkCommandBuffer cmd_buf, uint32_t Width, uint32_t Height, Texture *pInput)
    {
        if (m_pDevice->GetPhysicalDeviceSubgroupProperties().supportedOperations 
            & VK_SUBGROUP_FEATURE_QUAD_BIT)
        {
            m_spd_WaveOps_NonPacked.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
            m_spd_WaveOps_Packed.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));

            m_spd_WaveOps_NonPacked_Linear_Sampler.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
            m_spd_WaveOps_Packed_Linear_Sampler.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
        }
        m_spd_No_WaveOps_NonPacked.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
        m_spd_No_WaveOps_Packed.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));

        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnCreateWindowSizeDependentResources(cmd_buf, Width, Height, pInput, GetMaxMipLevelCount(Width, Height));
    }

    void SPD_Versions::OnDestroyWindowSizeDependentResources()
    {
        if (m_pDevice->GetPhysicalDeviceSubgroupProperties().supportedOperations 
            & VK_SUBGROUP_FEATURE_QUAD_BIT)
        {
            m_spd_WaveOps_NonPacked.OnDestroyWindowSizeDependentResources();
            m_spd_WaveOps_Packed.OnDestroyWindowSizeDependentResources();

            m_spd_WaveOps_NonPacked_Linear_Sampler.OnDestroyWindowSizeDependentResources();
            m_spd_WaveOps_Packed_Linear_Sampler.OnDestroyWindowSizeDependentResources();
        }
        m_spd_No_WaveOps_NonPacked.OnDestroyWindowSizeDependentResources();
        m_spd_No_WaveOps_Packed.OnDestroyWindowSizeDependentResources();

        m_spd_No_WaveOps_NonPacked_Linear_Sampler.OnDestroyWindowSizeDependentResources();
        m_spd_No_WaveOps_Packed_Linear_Sampler.OnDestroyWindowSizeDependentResources();
    }

    void SPD_Versions::Dispatch(VkCommandBuffer cmd_buf, SPD_Version dsVersion, SPD_Packed dsPacked)
    {
        switch (dsVersion)
        {
        case SPD_Version::SPD_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_WaveOps_NonPacked.Draw(cmd_buf);
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_WaveOps_Packed.Draw(cmd_buf);
                break;
            }
            break;
        case SPD_Version::SPD_No_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_No_WaveOps_NonPacked.Draw(cmd_buf);
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_No_WaveOps_Packed.Draw(cmd_buf);
                break;
            }
        }
    }

    void SPD_Versions::DispatchLinearSamplerVersion(VkCommandBuffer cmd_buf, SPD_Version dsVersion, SPD_Packed dsPacked)
    {
        switch (dsVersion)
        {
        case SPD_Version::SPD_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_WaveOps_NonPacked_Linear_Sampler.Draw(cmd_buf);
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_WaveOps_Packed_Linear_Sampler.Draw(cmd_buf);
                break;
            }
            break;
        case SPD_Version::SPD_No_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_No_WaveOps_NonPacked_Linear_Sampler.Draw(cmd_buf);
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_No_WaveOps_Packed_Linear_Sampler.Draw(cmd_buf);
                break;
            }
        }
    }

    void SPD_Versions::Gui(SPD_Version dsVersion, SPD_Packed dsPacked)
    {
        switch (dsVersion)
        {
        case SPD_Version::SPD_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_WaveOps_NonPacked.Gui();
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_WaveOps_Packed.Gui();
                break;
            }
            break;
        case SPD_Version::SPD_No_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_No_WaveOps_NonPacked.Gui();
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_No_WaveOps_Packed.Gui();
                break;
            }
        }
    }

    void SPD_Versions::GuiLinearSamplerVersion(SPD_Version dsVersion, SPD_Packed dsPacked)
    {
        switch (dsVersion)
        {
        case SPD_Version::SPD_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_WaveOps_NonPacked_Linear_Sampler.Gui();
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_WaveOps_Packed_Linear_Sampler.Gui();
                break;
            }
            break;
        case SPD_Version::SPD_No_WaveOps:
            switch (dsPacked)
            {
            case SPD_Packed::SPD_Non_Packed:
                m_spd_No_WaveOps_NonPacked_Linear_Sampler.Gui();
                break;
            case SPD_Packed::SPD_Packed:
                m_spd_No_WaveOps_Packed_Linear_Sampler.Gui();
                break;
            }
        }
    }
}