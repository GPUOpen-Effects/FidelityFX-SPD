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

//--------------------------------------------------------------------------------------
// Push Constants
//--------------------------------------------------------------------------------------
[[vk::push_constant]]
cbuffer spdConstants {
    uint mips;
    uint numWorkGroups;
    // [SAMPLER]
    float2 invInputSize;
};
//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
[[vk::binding(0)]] Texture2D<float4> imgSrc :register(u0);
[[vk::binding(1)]] globallycoherent RWTexture2D<float4> imgDst[12] :register(u1);
// [SAMPLER]
[[vk::binding(3)]] SamplerState srcSampler :register(s0);

//--------------------------------------------------------------------------------------
// Buffer definitions - global atomic counter
//--------------------------------------------------------------------------------------
struct globalAtomicBuffer
{
    uint counter;
};
[[vk::binding(2)]] globallycoherent RWStructuredBuffer<globalAtomicBuffer> globalAtomic;

#define A_GPU
#define A_HLSL

#include "ffx_a.h"

groupshared AU1 spd_counter;

// define fetch and store functions
#ifndef SPD_PACKED_ONLY
groupshared AF1 spd_intermediateR[16][16];
groupshared AF1 spd_intermediateG[16][16];
groupshared AF1 spd_intermediateB[16][16];
groupshared AF1 spd_intermediateA[16][16];
//AF4 DSLoadSourceImage(ASU2 tex){return imgSrc[tex];}
//[SAMPLER]
AF4 SpdLoadSourceImage(ASU2 p){
    AF2 textureCoord = p * invInputSize + invInputSize;
    return imgSrc.SampleLevel(srcSampler, textureCoord, 0);
}
AF4 SpdLoad(ASU2 tex){return imgDst[5][tex];}
void SpdStore(ASU2 pix, AF4 outValue, AU1 index){imgDst[index][pix] = outValue;}
void SpdIncreaseAtomicCounter(){InterlockedAdd(globalAtomic[0].counter, 1, spd_counter);}
AU1 SpdGetAtomicCounter(){return spd_counter;}
AF4 SpdLoadIntermediate(AU1 x, AU1 y){
    return AF4(
    spd_intermediateR[x][y], 
    spd_intermediateG[x][y], 
    spd_intermediateB[x][y], 
    spd_intermediateA[x][y]);}
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value){
    spd_intermediateR[x][y] = value.x;
    spd_intermediateG[x][y] = value.y;
    spd_intermediateB[x][y] = value.z;
    spd_intermediateA[x][y] = value.w;}
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return (v0+v1+v2+v3)*0.25;}
#endif

// define fetch and store functions Packed
#ifdef A_HALF
groupshared AH2 spd_intermediateRG[16][16];
groupshared AH2 spd_intermediateBA[16][16];
AH4 SpdLoadSourceImageH(ASU2 p){
    AF2 textureCoord = p * invInputSize + invInputSize;
    return AH4(imgSrc.SampleLevel(srcSampler, textureCoord, 0));
}
AH4 SpdLoadH(ASU2 p){return AH4(imgDst[5][p]);}
void SpdStoreH(ASU2 p, AH4 value, AU1 mip){imgDst[mip][p] = AF4(value);}
void SpdIncreaseAtomicCounter(){InterlockedAdd(globalAtomic[0].counter, 1, spd_counter);}
AU1 SpdGetAtomicCounter(){return spd_counter;}
AH4 SpdLoadIntermediateH(AU1 x, AU1 y){
    return AH4(
    spd_intermediateRG[x][y].x,
    spd_intermediateRG[x][y].y,
    spd_intermediateBA[x][y].x,
    spd_intermediateBA[x][y].y);}
void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value){
    spd_intermediateRG[x][y] = value.xy;
    spd_intermediateBA[x][y] = value.zw;}
AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3){return (v0+v1+v2+v3)*AH1(0.25);}
#endif

#define SPD_LINEAR_SAMPLER

#include "ffx_spd.h"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
[numthreads(256,1,1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
#ifndef A_HALF
    SpdDownsample(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),  
        AU1(mips),
        AU1(numWorkGroups));
#else
    SpdDownsampleH(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),  
        AU1(mips),
        AU1(numWorkGroups));
#endif
}