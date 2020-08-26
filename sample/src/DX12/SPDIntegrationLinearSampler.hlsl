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

// when using amd shader intrinscs
// #include "ags_shader_intrinsics_dx12.h"

//--------------------------------------------------------------------------------------
// Constant Buffer
//--------------------------------------------------------------------------------------
cbuffer spdConstants : register(b0)
{
    uint mips;
    uint numWorkGroups;
    uint2 workGroupOffset;
    float2 invInputSize;
}

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
globallycoherent RWTexture2DArray<float4>            imgDst5         : register(u2);
RWTexture2DArray<float4>                             imgDst[12]      : register(u3); // do no access MIP [5]
Texture2DArray<float4>                               imgSrc          : register(t0);
SamplerState                                         srcSampler      : register(s0);

//--------------------------------------------------------------------------------------
// Buffer definitions - global atomic counter
//--------------------------------------------------------------------------------------
struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};
globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic :register(u1);

#define A_GPU
#define A_HLSL

#include "ffx_a.h"

groupshared AU1 spdCounter;

#ifndef SPD_PACKED_ONLY
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    AF2 textureCoord = p * invInputSize + invInputSize;
    AF4 result = imgSrc.SampleLevel(srcSampler, float3(textureCoord, slice), 0);
    result = AF4(AToSrgbF1(result.x), AToSrgbF1(result.y), AToSrgbF1(result.z), result.w);
    return result;
}
AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    return imgDst5[uint3(tex, slice)];
}
void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    if (index == 5)
    {
        imgDst5[uint3(pix, slice)] = outValue;
        return;
    }
    imgDst[index][uint3(pix, slice)] = outValue;
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[slice] = 0;
}
AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return AF4(
    spdIntermediateR[x][y], 
    spdIntermediateG[x][y], 
    spdIntermediateB[x][y], 
    spdIntermediateA[x][y]);
}
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return (v0+v1+v2+v3)*0.25;
}
#endif

// define fetch and store functions Packed
#ifdef A_HALF
groupshared AH2 spdIntermediateRG[16][16];
groupshared AH2 spdIntermediateBA[16][16];

AH4 SpdLoadSourceImageH(ASU2 p, AU1 slice)
{
    AF2 textureCoord = p * invInputSize + invInputSize;
    AF4 result = imgSrc.SampleLevel(srcSampler, float3(textureCoord, slice), 0);
    result = AF4(AToSrgbF1(result.x), AToSrgbF1(result.y), AToSrgbF1(result.z), result.w);
    return AH4(result);
}
AH4 SpdLoadH(ASU2 p, AU1 slice)
{
    return AH4(imgDst5[uint3(p, slice)]);
}
void SpdStoreH(ASU2 p, AH4 value, AU1 mip, AU1 slice)
{
    if (mip == 5)
    {
        imgDst5[uint3(p, slice)] = AF4(value);
        return;
    }
    imgDst[mip][uint3(p, slice)] = AF4(value);
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[slice] = 0;
}
AH4 SpdLoadIntermediateH(AU1 x, AU1 y)
{
    return AH4(
    spdIntermediateRG[x][y].x,
    spdIntermediateRG[x][y].y,
    spdIntermediateBA[x][y].x,
    spdIntermediateBA[x][y].y);
}
void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value)
{
    spdIntermediateRG[x][y] = value.xy;
    spdIntermediateBA[x][y] = value.zw;
}
AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3)
{
    return (v0+v1+v2+v3)*AH1(0.25);
}
#endif

#define SPD_LINEAR_SAMPLER

#include "ffx_spd.h"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
#ifndef A_HALF
    SpdDownsample(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),  
        AU1(mips),
        AU1(numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(workGroupOffset));
#else
    SpdDownsampleH(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),  
        AU1(mips),
        AU1(numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(workGroupOffset));
#endif
 }