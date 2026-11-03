// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// tri_tex.vert -- S2.5 textured triangle (vertex stage).
// AOT-compiled to SPIR-V by glslangValidator at build time (see CMake).

#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragColor = inColor;
    fragUV = inUV;
}
