#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_compute_shader : enable

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

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

//--------------------------------------------------------------------------------------
// Push Constants
//--------------------------------------------------------------------------------------
layout(push_constant) uniform pushConstants {
    vec2 u_outputTextureSize;
    vec2 u_inputInvTextureSize;
} myPerMip;

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
layout(set=0, binding=0) uniform texture2D inputTexture;
layout(set=0, binding=1) uniform sampler inputSampler;

layout(set=0, binding=2, rgba16f) uniform writeonly image2D outputTexture;

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
void main()
{
    if (gl_GlobalInvocationID.x >= myPerMip.u_outputTextureSize.x || gl_GlobalInvocationID.y >= myPerMip.u_outputTextureSize.y)
        return;

    ivec2 pixel_coord = ivec2(gl_GlobalInvocationID.xy);
    vec2 texcoord = myPerMip.u_inputInvTextureSize.xy * gl_GlobalInvocationID.xy * 2.0f + myPerMip.u_inputInvTextureSize.xy;

    imageStore(outputTexture, pixel_coord, texture(sampler2D(inputTexture, inputSampler), texcoord));
}