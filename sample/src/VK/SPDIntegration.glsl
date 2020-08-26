#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_compute_shader : enable
#extension GL_ARB_shader_group_vote : enable

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

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

//--------------------------------------------------------------------------------------
// Push Constants
//--------------------------------------------------------------------------------------
layout(push_constant) uniform SpdConstants
{
    uint mips;
    uint numWorkGroups;
    ivec2 workGroupOffset;
} spdConstants;

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
layout(set=0, binding=0, rgba16f) uniform image2DArray imgDst[13]; // don't access mip [6]
layout(set=0, binding=1, rgba16f) coherent uniform image2DArray imgDst6;

//--------------------------------------------------------------------------------------
// Buffer definitions - global atomic counter
//--------------------------------------------------------------------------------------
layout(std430, binding=2) coherent buffer spdGlobalAtomicBuffer
{
    uint counter[6];
} spdGlobalAtomic;

#define A_GPU
#define A_GLSL

#include "ffx_a.h"

shared AU1 spdCounter;

// define fetch and store functions Non-Packed
#ifndef SPD_PACKED_ONLY
shared AF1 spdIntermediateR[16][16];
shared AF1 spdIntermediateG[16][16];
shared AF1 spdIntermediateB[16][16];
shared AF1 spdIntermediateA[16][16];

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    return imageLoad(imgDst[0], ivec3(p,slice));
}
AF4 SpdLoad(ASU2 p, AU1 slice)
{
    return imageLoad(imgDst6,ivec3(p,slice));
}
void SpdStore(ASU2 p, AF4 value, AU1 mip, AU1 slice)
{
    if (mip == 5)
    {
        imageStore(imgDst6, ivec3(p,slice), value);
        return;
    }
    imageStore(imgDst[mip+1], ivec3(p,slice), value);
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    spdCounter = atomicAdd(spdGlobalAtomic.counter[slice], 1);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic.counter[slice] = 0;
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
shared AH2 spdIntermediateRG[16][16];
shared AH2 spdIntermediateBA[16][16];

AH4 SpdLoadSourceImageH(ASU2 p, AU1 slice)
{
    return AH4(imageLoad(imgDst[0], ivec3(p,slice)));
}
AH4 SpdLoadH(ASU2 p, AU1 slice)
{
    return AH4(imageLoad(imgDst6, ivec3(p,slice)));
}
void SpdStoreH(ASU2 p, AH4 value, AU1 mip, AU1 slice)
{
    if (mip == 5)
    {
        imageStore(imgDst6, ivec3(p,slice), AF4(value));
        return;
    }
    imageStore(imgDst[mip+1], ivec3(p,slice), AF4(value));
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    spdCounter = atomicAdd(spdGlobalAtomic.counter[slice], 1);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic.counter[slice] = 0;
}
AH4 SpdLoadIntermediateH(AU1 x, AU1 y)
{
    return AH4(
    spdIntermediateRG[x][y].x,
    spdIntermediateRG[x][y].y,
    spdIntermediateBA[x][y].x,
    spdIntermediateBA[x][y].y);}
void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value){
    spdIntermediateRG[x][y] = value.xy;
    spdIntermediateBA[x][y] = value.zw;
}
AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3)
{
    return (v0+v1+v2+v3)*AH1(0.25f);
}
#endif

#include "ffx_spd.h"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
void main()
{
#ifndef A_HALF
    SpdDownsample(
        AU2(gl_WorkGroupID.xy), 
        AU1(gl_LocalInvocationIndex), 
        AU1(spdConstants.mips), 
        AU1(spdConstants.numWorkGroups),
        AU1(gl_WorkGroupID.z),
        AU2(spdConstants.workGroupOffset));
#else
    SpdDownsampleH(
        AU2(gl_WorkGroupID.xy), 
        AU1(gl_LocalInvocationIndex), 
        AU1(spdConstants.mips), 
        AU1(spdConstants.numWorkGroups),
        AU1(gl_WorkGroupID.z),
        AU2(spdConstants.workGroupOffset));
#endif
}