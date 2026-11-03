// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// tri.vert -- S2.4 hardcoded-pipeline triangle (vertex stage).
// AOT-compiled to SPIR-V by glslangValidator at build time (see CMake).
// Positions come from a host-visible vertex buffer (proves VBO plumbing).

#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragColor = inColor;
}
