// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// tri_tex.frag -- S2.5 textured triangle (fragment stage).

#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D texDiffuse;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texDiffuse, fragUV) * vec4(fragColor, 1.0);
}
