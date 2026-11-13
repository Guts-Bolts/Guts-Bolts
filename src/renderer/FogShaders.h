#pragma once
// GLSL for the volumetric fog (see FogVolume.h for how it works, in plain words):
//
//  fogTraceFrag: for each pixel of a picture half as wide and high as the screen, walk a
//    ray from the camera to whatever is there, adding up how much fog is in the way (how
//    much of the thing behind still shows) and how much light the fog throws towards the
//    camera: sunlight where the sun reaches (so shadows cut dark shafts through it), and
//    the glow of lamps nearby. The fog is thicker low down and drifts in wisps. The
//    result is mixed with last frame's, so a few steps a frame add up to a smooth picture.
//
//  fogApplyFrag: puts that small picture onto the full-size one. Each pixel takes the
//    fog from the small pixels that were about as far away as it is, so fog doesn't leak
//    round the edges of things.
namespace Shaders {

inline const char* fogTraceFrag = R"(#version 410 core
#ifdef GL_ES
precision highp sampler3D;
#endif
in vec2 vUV;
uniform sampler2D uDepth;        // the scene's depth (solid things only)
uniform sampler2D uShadowMap;    // the sun's shadow map
uniform sampler3D uNoise;        // the wisp pattern (repeats)
uniform sampler2D uHist;         // last frame's fog (rgb light, a how much shows through)
uniform sampler2D uHistDist;     // ...and how far away each of its pixels was
uniform mat4  uInvViewProj;
uniform mat4  uPrevViewProj;
uniform mat4  uLightSpace;
uniform vec3  uViewPos;
uniform bool  uHistOk;
uniform bool  uShadows;
uniform float uShadowStrength;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform float uFogFalloff;
uniform float uFogSunGlow;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform int   uSteps;
uniform float uMaxDist;
uniform float uFrame;
uniform float uKeep;             // how much of the past is kept
uniform float uWisps;            // 0 smooth fog .. 1 very wispy
uniform vec3  uDriftBig;         // how far the big and the small wisps have drifted
uniform vec3  uDriftSmall;
#define FOG_LIGHTS 8
uniform int   uLightCount;
uniform vec4  uLightPosRange[FOG_LIGHTS];
uniform vec4  uLightColor[FOG_LIGHTS];   // rgb, w 1 = a spot light
uniform vec4  uLightDir[FOG_LIGHTS];     // xyz, w cos(half angle)
layout(location = 0) out vec4 OutFog;
layout(location = 1) out vec4 OutDist;
#pragma gb_common

float heightFactor(float y) {
    return uFogFalloff > 0.0 ? exp(clamp(-uFogFalloff * (y - uFogHeight), -30.0, 8.0)) : 1.0;
}

float densityAt(vec3 x) {
    float d = uFogDensity * heightFactor(x.y);
    if (uWisps > 0.0) {
        float n = texture(uNoise, x / 48.0 + uDriftBig).r * 0.65 + texture(uNoise, x / 13.0 + uDriftSmall).r * 0.35;
        d *= max(0.0, 1.0 + uWisps * 2.4 * (n - 0.5));
    }
    return d;
}

float sunReaches(vec3 x) {
    if (!uShadows) return 1.0;
    vec4 lc = uLightSpace * vec4(x, 1.0);
    vec3 p = lc.xyz / lc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float lit = texture(uShadowMap, p.xy).r < p.z - 0.0006 ? 0.0 : 1.0;
    return mix(1.0, lit, uShadowStrength);
}

vec3 lampLight(vec3 x) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < FOG_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        vec3  toL = uLightPosRange[i].xyz - x;
        float dist = length(toL);
        float range = uLightPosRange[i].w;
        if (dist > range) continue;
        float fall = pow(clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        if (uLightColor[i].w > 0.5) {
            float cd = dot(-toL / max(dist, 1e-4), uLightDir[i].xyz);
            float c0 = uLightDir[i].w;
            fall *= smoothstep(c0, mix(c0, 1.0, 0.25), cd);
        }
        sum += uLightColor[i].rgb * fall * 8.0;   // (brighter than on a wall: a lamp's glow in mist should show)
    }
    return sum;
}

void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 0.99999) {              // the sky: left as it is
        OutFog = vec4(0.0, 0.0, 0.0, 1.0);
        OutDist = vec4(10000.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 ray = P - uViewPos;
    float dist = max(length(ray), 1e-3);
    vec3 dir = ray / dist;
    float D = min(dist, uMaxDist);

    vec3 fogCol = lin(uFogColor);
    float toward = pow(max(dot(dir, normalize(uSunDir)), 0.0), 8.0);
    vec3 sunGlow = lin(uSunColor) * uSunIntensity * toward * uFogSunGlow * 0.6;

    // Every pixel starts its steps at a different spot, and a different one each frame:
    // mixed over frames, that fills in the gaps between the steps.
    float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uFrame * 0.61803399);
    float T = 1.0;                       // how much of what's behind still shows
    vec3  S = vec3(0.0);                 // light the fog sends to the camera
    float t0 = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSteps || T < 0.01) break;
        float t1 = D * pow(float(i + 1) / float(uSteps), 1.6);   // (short steps close by, longer far off)
        vec3 x = uViewPos + dir * mix(t0, t1, jit);
        float blocked = 1.0 - exp(-densityAt(x) * (t1 - t0));
        float sun = sunReaches(x);
        // In full sun this is exactly the simple fog's colour, so every quality level
        // has the same look; in shadow the fog is darker, which is what draws the shafts.
        vec3 light = fogCol * (0.35 + 0.65 * sun) + sunGlow * sun + lampLight(x);
        S += T * blocked * light;
        T *= 1.0 - blocked;
        t0 = t1;
    }
    if (dist > D) {                      // past the walk: the simple sum for the rest
        vec3 a = uViewPos + dir * D;
        float k = uFogDensity * (dist - D);
        if (uFogFalloff > 0.0) {
            float t = uFogFalloff * (P.y - a.y);
            k *= heightFactor(a.y) * (abs(t) > 0.01 ? (1.0 - exp(-t)) / t : 1.0 - t * 0.5);
        }
        float blocked = 1.0 - exp(-max(k, 0.0));
        S += T * blocked * (fogCol + sunGlow);
        T *= 1.0 - blocked;
    }

    vec4 fog = vec4(S, T);
    if (uHistOk) {
        vec4 pc = uPrevViewProj * vec4(P, 1.0);
        vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
        if (pc.w > 0.0 && puv.x >= 0.0 && puv.y >= 0.0 && puv.x <= 1.0 && puv.y <= 1.0) {
            float was = texture(uHistDist, puv).r;
            if (abs(was - dist) < dist * 0.1 + 0.5)   // (it saw this same thing there last frame)
                fog = mix(fog, texture(uHist, puv), uKeep);
        }
    }
    OutFog = fog;
    OutDist = vec4(dist, 0.0, 0.0, 1.0);
}
)";

inline const char* fogApplyFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uFog;
uniform sampler2D uFogDist;
uniform sampler2D uDepth;
uniform mat4  uInvViewProj;
uniform vec3  uViewPos;
out vec4 FragColor;
void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 0.99999) { FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    float dist = length(wp.xyz / wp.w - uViewPos);
    // Of the 4 nearest small pixels, use the ones that were about as far away as this.
    vec2 sz = vec2(textureSize(uFog, 0));
    vec2 gp = vUV * sz - 0.5;
    vec2 gf = fract(gp);
    ivec2 g0 = ivec2(floor(gp));
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    vec4 nearest = vec4(0.0, 0.0, 0.0, 1.0);
    float nearestGap = 1e30;
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k - (k / 2) * 2, k / 2);
        ivec2 q = clamp(g0 + o, ivec2(0), ivec2(sz) - 1);
        float gap = abs(texelFetch(uFogDist, q, 0).r - dist);
        vec4 f = texelFetch(uFog, q, 0);
        float w = (o.x == 1 ? gf.x : 1.0 - gf.x) * (o.y == 1 ? gf.y : 1.0 - gf.y) * exp(-gap / max(dist * 0.05, 0.1));
        acc += f * w;
        wsum += w;
        if (gap < nearestGap) { nearestGap = gap; nearest = f; }
    }
    // (Drawn with blending: the picture is multiplied by a, and rgb is added.)
    FragColor = wsum > 1e-4 ? acc / wsum : nearest;
}
)";

} // namespace Shaders
