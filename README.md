# FidelityFX SPD
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# Single Pass Downsampler - SPD

FidelityFX Single Pass Downsampler (SPD) provides an RDNA-optimized solution for generating up to 12 MIP levels of a texture.

# Sample Build Instructions

1. Clone submodules by running 'git submodule update --init --recursive' (so you get the Cauldron framework too)
2. Run sample/build/GenerateSolutions.bat
3. open solution, build + run + have fun 😊

# SPD Files
You can find them in ffx-spd
- ffx_a.h: helper file
- ffx_spd: contains the SPD function and integration documentation

# Sample
Downsampler
- PS: computes each mip in a separate pixel shader pass
- Multipass CS: computes each mip in a separate compute shader pass
- SPD CS: uses the SPD library, computes all mips (up to a source texture of size 4096²) in a single pass
- SPD CS linear sampler: uses the SPD library and for sampling the source texture a linear sampler

SPD Versions
- NO-WaveOps: uses only LDS to share the data between threads
- WaveOps: uses Intrinsics and LDS to share the data between threads

SPD Non-Packed / Packed Version
- Non-Packed: uses fp32
- Packed: uses fp16, reduced register pressure

# Recommendations
We recommend to use the WapeOps path when supported. If higher precision is not needed, you can enable the packed mode - it has less register pressure and can run a bit faster as well.