#pragma once
// GLSL source for the scene renderer. Everything is rendered in linear HDR,
// then post-processed (ambient occlusion, bloom, tone mapping, colour grading,
// FXAA) into the final 8-bit image.

namespace Shaders {

// Shared helpers pasted into several shaders: a line reading
// "#pragma gb_common" is replaced with this (see Shader.cpp).
inline const char* common = R"(
vec3 lin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }   // colour picker -> linear
float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash13(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return v;
}
// Procedural sky colour in a direction (also used for reflections).
vec3 skyGradient(vec3 dir, vec3 zenith, vec3 horizon, vec3 ground) {
    float t = dir.y;
    if (t > 0.0) return mix(horizon, zenith, pow(clamp(t, 0.0, 1.0), 0.45));
    return mix(horizon, ground, pow(clamp(-t, 0.0, 1.0), 0.5));
}
)";

// ---------------------------------------------------------------------------
// Lit geometry
// ---------------------------------------------------------------------------

inline const char* litVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBlend;   // terrain: how much grass, dirt, sand, rock (snow and mud in aUV)

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;
out vec4 vBlend;
out vec3 vLocalPos;      // in the part's own space (-0.5..0.5): for faces painted on heads
out vec3 vLocalNormal;

void main() {
    vUV = aUV;
    vBlend = aBlend;
    vLocalPos = aPos;
    vLocalNormal = aNormal;
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * world;
}
)";

inline const char* litFrag = R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;
in vec4 vBlend;
in vec3 vLocalPos;
in vec3 vLocalNormal;

uniform bool      uUseDecal;   // drawing a Decal: its picture colours the surface
uniform bool      uFace;       // a head: its face picture (uDecal) is painted onto the front of it
uniform bool      uClothing;   // a shirt / pants picture (uDecal) painted over the part's colour
uniform bool      uHasTShirt;  // a torso with a T-shirt: its picture flat on the front
uniform sampler2D uTShirt;
uniform sampler2D uDecal;

uniform vec3  uColor;
uniform int   uMaterial;      // 0 plastic 1 metal 2 neon 3 wood 4 glass 5 concrete 6 ice (7 water, 8 liquid, 9 terrain)
uniform float uAlpha;
uniform bool  uSelected;
uniform vec3  uViewPos;
uniform float uTime;

uniform vec3  uSunDir;        // surface -> sun
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uAmbientSky;
uniform vec3  uAmbientGround;
uniform float uAmbientIntensity;
uniform float uReflections;
uniform vec3  uZenith, uHorizon, uGround;

uniform bool  uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uRenderDist;    // render distance: things fade into the sky towards it (0 = no limit)
// Wetness: splashed and dripped-on patches (position, radius; how wet), and a whole
// part the liquid ran over or a character just out of the water.
uniform int   uWetCount;
uniform vec4  uWetSpots[16];
uniform float uWetAmt[16];
uniform float uWetPart;
uniform float uFogSunGlow;

uniform bool      uShadowsEnabled;
uniform int       uShadowQuality;     // 0 hard, 1 soft, 2 PCSS
uniform float     uShadowSoftness;
uniform float     uShadowStrength;
uniform float     uShadowTexelWorld;  // world size of one shadow-map texel
uniform float     uShadowDepthRange;  // world distance covered by depth 0..1
uniform mat4      uLightSpace;
uniform sampler2D uShadowMap;
// A second, sharper shadow map close to the camera (a "cascade").
uniform bool      uNearCascade;
uniform mat4      uLightSpaceNear;
uniform sampler2D uShadowMapNear;
uniform float     uShadowTexelNear;

#define MAX_LIGHTS 32
uniform int  uLightCount;
uniform vec4 uLightPosRange[MAX_LIGHTS];   // xyz position, w range
uniform vec4 uLightColor[MAX_LIGHTS];      // rgb colour * brightness, w: 0 point 1 spot
uniform vec4 uLightDir[MAX_LIGHTS];        // xyz direction, w cos(half angle)

out vec4 FragColor;
#pragma gb_common
const float PI = 3.14159265;

const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

// 1 = lit, 0 = in shadow, from one shadow map. Soft shadows get softer the
// further the shadow is from whatever casts it (contact-hardening, like real
// sunlight). `edge` says how close to the side of the map the point is (0 middle, 1 edge).
float shadowFrom(sampler2D map, mat4 lightSpace, float texelWorld, vec3 N, vec3 L, out float edge) {
    // Normal offset: move the lookup point off the surface a little (more on surfaces
    // turned away from the sun), so a surface never shadows itself ("acne"). Small, so
    // a box sitting on the ground still shadows the ground right up to its edge.
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    vec3 wp = vWorldPos + N * texelWorld * (0.5 + 1.5 * (1.0 - NoL));
    vec4 lc = lightSpace * vec4(wp, 1.0);
    vec3 p  = lc.xyz / lc.w * 0.5 + 0.5;
    edge = max(abs(p.x - 0.5), abs(p.y - 0.5)) * 2.0;
    if (p.z > 1.0 || edge > 1.0) { edge = 2.0; return 1.0; }

    float bias  = 0.00004 + 0.0001 * (1.0 - NoL);
    vec2  texel = 1.0 / vec2(textureSize(map, 0));
    if (uShadowQuality == 0)
        return texture(map, p.xy).r < p.z - bias ? 0.0 : 1.0;

    float a = hash12(gl_FragCoord.xy) * 6.2831853;
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));

    float radius = 1.0 + 1.5 * uShadowSoftness;          // in texels
    if (uShadowQuality == 2) {
        float search = 3.0 + 8.0 * uShadowSoftness;
        float sum = 0.0; int n = 0;
        for (int i = 0; i < 16; ++i) {
            float d = texture(map, p.xy + rot * POISSON[i] * search * texel).r;
            if (d < p.z - bias) { sum += d; ++n; }
        }
        if (n == 0) return 1.0;
        float dist = (p.z - sum / float(n)) * uShadowDepthRange;       // world units
        float penumbra = dist * 0.05 * uShadowSoftness / texelWorld;
        radius = clamp(penumbra, 1.0, 24.0);
    }

    float lit = 0.0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(map, p.xy + rot * POISSON[i] * radius * texel).r;
        lit += (p.z - bias > d) ? 0.0 : 1.0;
    }
    return lit / 16.0;
}

float sunVisibility(vec3 N, vec3 L) {
    float edgeFar, edgeNear;
    float far = shadowFrom(uShadowMap, uLightSpace, uShadowTexelWorld, N, L, edgeFar);
    // Fade out towards the edge of the shadow area instead of a hard cut.
    far = mix(far, 1.0, smoothstep(0.85, 1.0, edgeFar));
    if (!uNearCascade) return far;
    float near = shadowFrom(uShadowMapNear, uLightSpaceNear, uShadowTexelNear, N, L, edgeNear);
    // Close up: the sharp map; blend into the wide one near its edge.
    return mix(near, far, smoothstep(0.75, 0.95, edgeNear));
}

// Cook-Torrance GGX specular + Lambert diffuse.
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metal, vec3 radiance) {
    vec3  H   = normalize(L + V);
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    float NoV = max(dot(N, V), 1e-3);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float a  = max(rough * rough, 0.002);
    float a2 = a * a;
    float dd = NoH * NoH * (a2 - 1.0) + 1.0;
    float D  = a2 / (PI * dd * dd);
    float k  = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G  = (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
    vec3  f0 = mix(vec3(0.04), albedo, metal);
    vec3  F  = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);

    vec3 spec = D * G * F / (4.0 * NoV * NoL + 1e-4);
    vec3 kd   = (1.0 - F) * (1.0 - metal);
    return (kd * albedo / PI + spec) * radiance * NoL;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 albedo = lin(uColor);
    float decalAlpha = 1.0;
    if (uUseDecal) {
        vec4 px = texture(uDecal, vUV);
        if (px.a < 0.01) discard;
        albedo *= lin(px.rgb);
        decalAlpha = px.a;
    }
    if (uClothing) {   // see-through bits of the clothing show the body colour
        vec4 px = texture(uDecal, vUV);
        albedo = mix(albedo, lin(px.rgb), px.a);
    }
    if (uHasTShirt && vLocalNormal.z > 0.0) {
        // A T-shirt: the picture flat on the front of the torso (over the shirt), seen
        // straight on, fading out where the torso curves round to the sides.
        vec2 tuv = vec2(vLocalPos.x + 0.5, vLocalPos.y + 0.5);
        vec4 px = texture(uTShirt, tuv);
        float front = smoothstep(0.35, 0.6, normalize(vLocalNormal).z);
        albedo = mix(albedo, lin(px.rgb), px.a * front);
    }
    if (uFace && vLocalNormal.z > 0.0) {
        // Like Roblox: the face picture is flat, seen straight on from the front, and
        // painted onto the head's own (round) surface, so it hugs the head exactly.
        // The picture covers the whole front of the head; only the front gets it.
        vec4 px = texture(uDecal, vLocalPos.xy + 0.5);
        albedo = mix(albedo, lin(px.rgb), px.a * smoothstep(0.0, 0.3, normalize(vLocalNormal).z));
    }

    // --- Material look ---
    // Fine surface detail fades out when it gets smaller than a pixel (no shimmering).
    float detail = clamp(1.5 - length(fwidth(vWorldPos)) * 24.0, 0.0, 1.0);
    float rough = 0.5, metal = 0.0;
    if (uMaterial == 1) { rough = 0.3; metal = 1.0;
        albedo *= mix(1.0, 0.92 + 0.08 * vnoise(vec2(dot(vWorldPos.xz, vec2(1.0)) * 60.0, vWorldPos.y * 2.0)), detail * 0.5); }
    else if (uMaterial == 3) {                                    // wood grain
        float rings = sin((vWorldPos.x * 0.7 + vWorldPos.z * 0.3 + fbm(vWorldPos.xy * 1.5) * 1.2) * 14.0);
        albedo *= 0.78 + 0.22 * (0.5 + 0.5 * rings);
        rough = 0.75;
    }
    else if (uMaterial == 9) {                                    // terrain: a mix of grounds (Terrain.h)
        float n1 = fbm(vWorldPos.xz * 0.35), n2 = vnoise(vWorldPos.xz * 4.0);
        float strata = 0.5 + 0.5 * sin(vWorldPos.y * 1.7 + n1 * 4.0);
        // How much of each: grass, dirt, sand, rock, snow, mud. A little noise on
        // the weights so the edges between grounds are ragged, not straight.
        float w[6] = float[6](vBlend.x, vBlend.y, vBlend.z, vBlend.w, vUV.x, vUV.y);
        vec3 col[6] = vec3[6](mix(vec3(0.27, 0.45, 0.16), vec3(0.40, 0.58, 0.22), n1),
                              mix(vec3(0.40, 0.29, 0.19), vec3(0.52, 0.39, 0.26), n1),
                              mix(vec3(0.80, 0.72, 0.50), vec3(0.90, 0.83, 0.62), n1),
                              mix(vec3(0.36, 0.35, 0.34), vec3(0.52, 0.50, 0.47), strata * 0.6 + n2 * 0.4),
                              mix(vec3(0.86, 0.89, 0.94), vec3(0.98, 0.98, 1.0), n1),
                              mix(vec3(0.22, 0.16, 0.10), vec3(0.32, 0.24, 0.15), n1));
        float rr[6] = float[6](0.9, 0.95, 0.9, 0.85, 0.6, 0.35);
        vec3 c = vec3(0.0);
        float r = 0.0, total = 0.0;
        for (int i = 0; i < 6; ++i) {
            float k = pow(clamp(w[i] + (n2 - 0.5) * 0.35 * w[i] * (1.0 - w[i]) * 4.0, 0.0, 1.0), 2.0);
            c += lin(col[i]) * k;
            r += rr[i] * k;
            total += k;
        }
        albedo = total > 1e-4 ? c / total : lin(col[0]);
        rough = total > 1e-4 ? r / total : 0.9;
        albedo *= mix(1.0, 0.85 + 0.3 * n2, detail);
    }
    else if (uMaterial == 4) { rough = 0.04; }                    // glass
    else if (uMaterial == 5) {                                    // concrete speckle
        albedo *= mix(0.92, 0.85 + 0.25 * hash13(floor(vWorldPos * 24.0)) * vnoise(vWorldPos.xz * 3.0), detail);
        rough = 0.95;
    }
    else if (uMaterial == 6) { rough = 0.08; albedo = mix(albedo, vec3(0.8, 0.9, 1.0), 0.2); }
    else if (uMaterial == 8) {                                    // wet liquid (blood, oil): glossy, a bit darker in the middle
        rough = 0.15;
        albedo *= 0.7;
    }
    else if (uMaterial == 7) {                                    // water: little moving ripples on top
        rough = 0.02;
        if (N.y > 0.3) {
            vec2 p = vWorldPos.xz;
            float t = uTime;
            vec2 slope = vec2(sin(p.x * 3.1 + t * 1.9) + sin(p.x * 1.3 + p.y * 2.3 + t * 1.3) + 0.5 * sin(p.x * 7.0 - p.y * 5.0 + t * 3.1),
                              cos(p.y * 2.7 - t * 1.6) + cos(p.x * 2.1 - p.y * 1.4 + t * 1.1) + 0.5 * cos(p.y * 6.3 + p.x * 4.1 - t * 2.7));
            N = normalize(N + vec3(slope.x, 0.0, slope.y) * 0.035 * (0.3 + 0.7 * detail));
        }
    }

    // Wet things: darker (water soaks into wood, concrete and the like) and shinier,
    // with little puddles on flat tops. Patchy as they dry.
    if (uMaterial != 2 && uMaterial != 7 && uMaterial != 8) {
        float wet = uWetPart;
        for (int i = 0; i < uWetCount; ++i) {
            float d = length(vWorldPos - uWetSpots[i].xyz);
            wet = max(wet, uWetAmt[i] * (1.0 - smoothstep(uWetSpots[i].w * 0.5, uWetSpots[i].w, d)));
        }
        if (wet > 0.0) {
            wet *= smoothstep(0.15, 0.55, vnoise(vWorldPos.xz * 1.3 + vWorldPos.y * 0.7) * 0.6 + wet * 0.6);
            float porous = (uMaterial == 3 || uMaterial == 5) ? 0.5 : (uMaterial == 0 ? 0.72 : 0.88);
            albedo *= mix(1.0, porous, wet);
            float puddle = wet * smoothstep(0.75, 0.95, N.y);
            rough = mix(rough, 0.05, max(wet * 0.75, puddle));
        }
    }

    vec3 color;
    float alpha = uAlpha * decalAlpha;

    if (uMaterial == 2) {
        // Neon glows on its own (and blooms).
        color = albedo * 4.0;
    } else {
        vec3 L = normalize(uSunDir);
        float vis = 1.0;
        if (uShadowsEnabled) vis = mix(1.0, sunVisibility(N, L), uShadowStrength);
        color = shade(N, V, L, albedo, rough, metal, lin(uSunColor) * uSunIntensity * PI * vis);

        // Point / spot lights.
        for (int i = 0; i < uLightCount; ++i) {
            vec3  toL  = uLightPosRange[i].xyz - vWorldPos;
            float dist = length(toL);
            float range = uLightPosRange[i].w;
            if (dist > range) continue;
            vec3  Ld   = toL / max(dist, 1e-4);
            float fall = pow(clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
            if (uLightColor[i].w > 0.5) {
                float cd = dot(-Ld, uLightDir[i].xyz);
                float c0 = uLightDir[i].w;
                fall *= smoothstep(c0, mix(c0, 1.0, 0.25), cd);
            }
            color += shade(N, V, Ld, albedo, rough, metal, uLightColor[i].rgb * fall * 6.0);
        }

        // Ambient: sky light from above, bounce light from below.
        vec3 hemi = mix(lin(uAmbientGround), lin(uAmbientSky), N.y * 0.5 + 0.5);
        color += albedo * (1.0 - metal) * hemi * uAmbientIntensity;

        // Reflections of the sky on shiny surfaces.
        vec3  f0  = mix(vec3(0.04), albedo, metal);
        float NoV = max(dot(N, V), 0.0);
        vec3  F   = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - NoV, 5.0);
        vec3  R   = reflect(-V, N);
        vec3  env = lin(skyGradient(R, uZenith, uHorizon, uGround));
        vec3  avg = lin((uZenith + uHorizon + uGround) / 3.0);
        env = mix(env, avg, rough);
        color += env * F * (uMaterial == 7 ? max(uReflections, 0.8) : uMaterial == 8 ? uReflections * 0.2 : uReflections) * (1.0 - rough * 0.7);

        // Glass & ice get more opaque at glancing angles.
        if (uMaterial == 4 || uMaterial == 6 || uMaterial == 7)
            alpha = mix(alpha, 1.0, pow(1.0 - NoV, 3.0) * (uMaterial == 7 ? 0.85 : 0.7));
    }

    if (uSelected) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        color = mix(color, vec3(2.0, 0.9, 0.2), rim * 0.8);
    }

    if (uFogEnabled) {
        float dist = length(uViewPos - vWorldPos);
        float f = 1.0 - exp(-uFogDensity * dist);
        vec3 fog = lin(uFogColor);
        float toward = pow(max(dot(-V, normalize(uSunDir)), 0.0), 8.0);
        fog += lin(uSunColor) * uSunIntensity * toward * uFogSunGlow * 0.6;
        color = mix(color, fog, clamp(f, 0.0, 1.0));
    }
    if (uRenderDist > 0.0) {
        // Near the edge of the render distance, fade into the sky behind (no popping).
        vec3 away = vWorldPos - uViewPos;
        float edge = smoothstep(uRenderDist * 0.8, uRenderDist, length(away));
        vec3 skyBehind = lin(skyGradient(normalize(away), uZenith, uHorizon, uGround)) * uAmbientIntensity;
        if (uFogEnabled) skyBehind = lin(uFogColor);
        color = mix(color, skyBehind, edge);
    }

    FragColor = vec4(color, alpha);
}
)";

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

inline const char* skyVert = R"(#version 410 core
// Fullscreen triangle generated from gl_VertexID — no vertex buffer needed.
out vec2 vNdc;
void main() {
    float x = float((gl_VertexID & 1) << 2) - 1.0;  // -1, 3, -1
    float y = float((gl_VertexID & 2) << 1) - 1.0;  // -1, -1, 3
    vNdc = vec2(x, y);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

inline const char* skyFrag = R"(#version 410 core
in vec2 vNdc;
uniform mat4  uInvViewProj;
uniform vec3  uZenith;
uniform vec3  uHorizon;
uniform vec3  uGround;
uniform float uSkyBrightness;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform float uSunSize;
uniform vec3  uAmbient;
uniform bool  uClouds;
uniform float uCloudCover;
uniform float uCloudSpeed;
uniform vec3  uCloudColor;
uniform bool  uStars;
uniform float uTime;
// A Sky object in Lighting (uSkyObj): a skybox (six pictures, when they all load),
// how many stars, and its own sun and moon (sizes in degrees, pictures or drawn here).
uniform bool  uSkyObj;
uniform bool  uSkybox;
uniform sampler2D uSkyBk, uSkyDn, uSkyFt, uSkyLf, uSkyRt, uSkyUp;
uniform float uStarDensity;    // StarCount / 3000
uniform bool  uCelestial;      // CelestialBodiesShown
uniform bool  uNight;          // uSunDir is the moon
uniform float uSunAngle;       // SunAngularSize
uniform float uMoonAngle;      // MoonAngularSize
uniform bool  uSunTexOn, uMoonTexOn;
uniform sampler2D uSunTex, uMoonTex;
out vec4 FragColor;
#pragma gb_common
// Which of the six skybox pictures a direction looks at, and where on it (v = up).
// Ft is -Z (the way things face), Bk +Z, Rt +X, Lf -X, Up +Y, Dn -Y.
vec3 skyboxAt(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x > 0.0) return lin(texture(uSkyRt, vec2(0.5 + 0.5 * d.z / a.x, 0.5 + 0.5 * d.y / a.x)).rgb);
        return lin(texture(uSkyLf, vec2(0.5 - 0.5 * d.z / a.x, 0.5 + 0.5 * d.y / a.x)).rgb);
    }
    if (a.y >= a.z) {
        if (d.y > 0.0) return lin(texture(uSkyUp, vec2(0.5 + 0.5 * d.x / a.y, 0.5 - 0.5 * d.z / a.y)).rgb);
        return lin(texture(uSkyDn, vec2(0.5 + 0.5 * d.x / a.y, 0.5 + 0.5 * d.z / a.y)).rgb);
    }
    if (d.z < 0.0) return lin(texture(uSkyFt, vec2(0.5 + 0.5 * d.x / a.z, 0.5 + 0.5 * d.y / a.z)).rgb);
    return lin(texture(uSkyBk, vec2(0.5 - 0.5 * d.x / a.z, 0.5 + 0.5 * d.y / a.z)).rgb);
}
// A picture of the sun or moon centred on L, `deg` degrees across (rgb, coverage).
vec4 bodyPicture(sampler2D tex, vec3 dir, vec3 L, float deg) {
    float facing = dot(dir, L);
    if (facing <= 0.0) return vec4(0.0);
    vec3 right = normalize(abs(L.y) > 0.99 ? cross(L, vec3(1, 0, 0)) : cross(L, vec3(0, 1, 0)));
    vec3 up = cross(right, L);
    float t = tan(radians(max(deg, 0.01)) * 0.5);
    vec2 q = vec2(dot(dir, right), dot(dir, up)) / facing / t;
    if (abs(q.x) > 1.0 || abs(q.y) > 1.0) return vec4(0.0);
    vec4 c = texture(tex, q * 0.5 + 0.5);
    return vec4(lin(c.rgb), c.a);
}
void main() {
    vec4 near = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 far  = uInvViewProj * vec4(vNdc,  1.0, 1.0);
    vec3 dir  = normalize(far.xyz / far.w - near.xyz / near.w);
    vec3 L    = normalize(uSunDir);

    vec3 sky = uSkybox ? skyboxAt(dir) * uSkyBrightness : lin(skyGradient(dir, uZenith, uHorizon, uGround)) * uSkyBrightness;

    // Stars, fading in as the sky gets dark (a Sky's StarCount says how many).
    float density = uSkyObj ? uStarDensity : 1.0;
    if (uStars && dir.y > 0.0 && density > 0.0) {
        float dark = 1.0 - clamp(dot(lin(uZenith), vec3(0.3, 0.6, 0.1)) * 12.0, 0.0, 1.0);
        vec3 cell = floor(dir * 300.0);
        float h = hash13(cell);
        float edge = 1.0 - 0.0025 * density;
        if (h > edge) {
            float tw = 0.6 + 0.4 * sin(uTime * 3.0 + h * 400.0);
            sky += vec3(1.0, 0.95, 0.9) * (h - edge) / (0.0025 * density) * 2.25 * tw * dark * smoothstep(0.0, 0.2, dir.y);
        }
    }

    // Sun disc + glow.
    float d    = max(dot(dir, L), 0.0);
    float s2   = uSunSize * uSunSize;
    float disc = smoothstep(1.0 - 0.0009 * s2, 1.0 - 0.0004 * s2, d);
    float glow = pow(d, 300.0 / max(uSunSize, 0.1)) * 1.5 + pow(d, 10.0) * 0.12;
    vec3  sunC = lin(uSunColor) * uSunIntensity;

    // Clouds: animated noise on a flat layer above the world.
    float cover = 0.0;
    if (uClouds && !uSkybox && dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.12) * 0.7 + uTime * 0.008 * uCloudSpeed * vec2(1.0, 0.35);
        float n = fbm(uv * 1.3);
        cover = smoothstep(1.05 - uCloudCover, 1.35 - uCloudCover, n + 0.25) * smoothstep(0.0, 0.2, dir.y);
        float thick = fbm(uv * 2.6 + 3.0);
        vec3 lit = sunC * 0.35 * (0.55 + 0.45 * pow(d, 2.0)) + lin(uAmbient) * 0.8;
        vec3 cloud = lin(uCloudColor) * lit * (0.75 + 0.35 * thick);
        cloud += sunC * pow(d, 12.0) * 0.6 * (1.0 - thick);            // silver lining
        sky = mix(sky, cloud, cover * 0.95);
    }

    if (!uSkyObj) {
        sky += sunC * (disc * 40.0 + glow) * (1.0 - cover * 0.9);
    } else if (uCelestial && !uNight) {
        // The Sky's sun: its picture, or the drawn disc scaled to SunAngularSize (21 = as usual).
        if (uSunTexOn) {
            vec4 p = bodyPicture(uSunTex, dir, L, uSunAngle);
            sky += p.rgb * p.a * sunC * 6.0 * (1.0 - cover * 0.9);
        } else {
            float k = uSunAngle / 21.0;
            float sd = smoothstep(1.0 - 0.0009 * s2 * k * k, 1.0 - 0.0004 * s2 * k * k, d);
            float sg = pow(d, 300.0 / max(uSunSize * k, 0.1)) * 1.5 + pow(d, 10.0) * 0.12;
            sky += sunC * (sd * 40.0 + sg) * (1.0 - cover * 0.9);
        }
    } else if (uCelestial) {
        // The moon (at night the light comes from it).
        float fade = 1.0 - cover * 0.9;
        if (uMoonTexOn) {
            vec4 p = bodyPicture(uMoonTex, dir, L, uMoonAngle);
            sky = mix(sky, p.rgb * 1.6, clamp(p.a * max(max(p.r, p.g), p.b) * 3.0, 0.0, 1.0) * fade);
        } else {
            float r = radians(uMoonAngle * 0.15);
            float ang = acos(clamp(dot(dir, L), -1.0, 1.0));
            float m = 1.0 - smoothstep(r * 0.92, r, ang);
            if (m > 0.0) {
                vec3 right = normalize(abs(L.y) > 0.99 ? cross(L, vec3(1, 0, 0)) : cross(L, vec3(0, 1, 0)));
                vec2 q = vec2(dot(dir, right), dot(dir, cross(right, L))) / max(r, 1e-4);
                float spots = 0.75 + 0.25 * vnoise(q * 3.0 + 7.0);
                sky = mix(sky, vec3(0.85, 0.87, 0.92) * spots * 1.4, m * fade);
            }
            sky += vec3(0.6, 0.65, 0.8) * 0.08 * pow(max(d, 0.0), 6000.0 / max(uMoonAngle, 0.5)) * fade;
        }
    }
    FragColor = vec4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Editor grid / shadow depth
// ---------------------------------------------------------------------------

inline const char* gridVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
out float vDist;
void main() {
    vDist = length(aPos.xz);
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

inline const char* gridFrag = R"(#version 410 core
in float vDist;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    float a = clamp(1.0 - vDist / 22.0, 0.0, 1.0);
    FragColor = vec4(uColor, a * 0.6);
}
)";

// Debug drawing on top of the world (Studio's navmesh view): see-through coloured
// triangles and lines, one colour per corner.
inline const char* overlayVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

inline const char* overlayFrag = R"(#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vec4(pow(vColor.rgb, vec3(2.2)), vColor.a); }
)";

// Explosion puffs (smoke, fire, dust, spray: see Blast.h): camera-facing soft balls
// with wispy edges, lit by the sun like a little sphere; fire glows (and blooms).
inline const char* puffVert = R"(#version 410 core
layout(location=0) in vec3 aCenter;
layout(location=1) in vec2 aCorner;   // -1..1
layout(location=2) in vec4 aColor;    // rgb, alpha
layout(location=3) in vec3 aInfo;     // radius, glow, seed
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vCorner;
out vec4 vColor;
out vec2 vInfo;    // glow, seed
void main() {
    vCorner = aCorner;
    vColor = aColor;
    vInfo = aInfo.yz;
    vec4 c = uView * vec4(aCenter, 1.0);
    c.xy += aCorner * aInfo.x;
    gl_Position = uProj * c;
}
)";

inline const char* puffFrag = R"(#version 410 core
in vec2 vCorner;
in vec4 vColor;
in vec2 vInfo;
uniform vec3 uSunView;    // the sun's direction, in view space
uniform vec3 uSunColor;
uniform vec3 uAmbient;
out vec4 FragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    float d2 = dot(vCorner, vCorner);
    if (d2 > 1.0) discard;
    vec2 q = vCorner * 2.3 + vec2(vInfo.y * 3.1, vInfo.y * 1.7);
    float n = noise(q) * 0.55 + noise(q * 2.1) * 0.3 + noise(q * 4.3) * 0.15;
    // Thick in the middle, wispy at the edge.
    float density = pow(1.0 - d2, 1.4) * (0.55 + 0.9 * n);
    float a = clamp(vColor.a * density, 0.0, 1.0);
    if (a < 0.01) discard;
    // Lit like a lumpy sphere.
    vec3 nrm = normalize(vec3(vCorner + (n - 0.5) * 0.6, sqrt(max(0.0, 1.0 - d2))));
    float lit = max(dot(nrm, normalize(uSunView)), 0.0);
    vec3 col = vColor.rgb * (uAmbient + uSunColor * lit * 0.9);
    // Fire: its own light, brightest in the middle.
    col += vColor.rgb * vInfo.x * (0.35 + 0.65 * (1.0 - d2)) * (0.7 + 0.6 * n);
    FragColor = vec4(col, a);
}
)";

inline const char* depthVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uLightSpace;
uniform mat4 uModel;
void main() { gl_Position = uLightSpace * uModel * vec4(aPos, 1.0); }
)";

inline const char* depthFrag = R"(#version 410 core
void main() {}
)";

// Trails and Beams: flat ribbons, coloured along their length, with an optional
// picture (scrolling for beams). Not lit; LightEmission makes them glow.
inline const char* ribbonVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

inline const char* ribbonFrag = R"(#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
uniform bool  uHasTex;
uniform float uGlow;          // LightEmission
uniform vec3  uLight;         // the scene's light (unlit ribbons still dim at night)
out vec4 FragColor;
void main() {
    vec4 c = vec4(pow(vColor.rgb, vec3(2.2)), vColor.a);
    if (uHasTex) {
        vec4 t = texture(uTex, vUV);
        c *= vec4(pow(t.rgb, vec3(2.2)), t.a);
    }
    c.rgb *= mix(uLight, vec3(1.5), uGlow);
    if (c.a < 0.003) discard;
    FragColor = c;
}
)";

// Highlights, step 1: the highlighted parts' shape on screen. Red = covered, green =
// covered and not behind anything else (compared with the scene's depth).
inline const char* highlightMaskVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main() { gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }
)";

inline const char* highlightMaskFrag = R"(#version 410 core
uniform sampler2D uDepth;     // the scene's depth
uniform vec2  uSize;          // its size in pixels
uniform vec2  uDepthParams;   // (proj[3][2], proj[2][2]): depth -> distance
uniform bool  uOrtho;
out vec4 FragColor;
float dist(float d) { return uOrtho ? d : uDepthParams.x / (d * 2.0 - 1.0 + uDepthParams.y); }
void main() {
    float scene = texture(uDepth, gl_FragCoord.xy / uSize).r;
    // The part itself wrote that depth, so allow a little slack.
    float a = dist(gl_FragCoord.z), b = dist(scene);
    bool seen = uOrtho ? gl_FragCoord.z <= scene + 0.0005 : a <= b * 1.004 + 0.02;
    FragColor = vec4(1.0, seen ? 1.0 : 0.0, 0.0, 1.0);
}
)";

// Highlights, step 2: fill the shape, and draw an outline just outside it.
inline const char* highlightFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uMask;
uniform vec2  uTexel;         // one pixel of the screen, in uv
uniform bool  uOnTop;         // AlwaysOnTop: the whole shape; else only the parts you can see
uniform vec4  uFill;          // colour, opacity
uniform vec4  uOutline;
out vec4 FragColor;
float cover(vec2 uv) {
    vec2 m = texture(uMask, uv).rg;
    return uOnTop ? m.r : m.g;
}
void main() {
    float c = cover(vUV);
    if (c > 0.5) { FragColor = uFill; return; }
    // Near the edge? Look round in a little circle (about 2.5 pixels).
    float edge = 0.0;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 0.5235988;
        vec2 d = vec2(cos(a), sin(a));
        edge = max(edge, cover(vUV + d * uTexel * 1.25));
        edge = max(edge, cover(vUV + d * uTexel * 2.5));
    }
    if (edge < 0.5) discard;
    FragColor = uOutline;
}
)";

// ---------------------------------------------------------------------------
// Post-processing (all fullscreen passes)
// ---------------------------------------------------------------------------

inline const char* fullscreenVert = R"(#version 410 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Screen-space ambient occlusion from the depth buffer.
inline const char* ssaoFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uDepth;
uniform mat4  uProj;
uniform mat4  uInvProj;
uniform vec2  uTexel;
uniform float uRadius;
out float FragAO;
#pragma gb_common
vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0) { FragAO = 1.0; return; }
    vec3 P = viewPos(vUV);

    // Normal from neighbouring depths (pick the flatter side to avoid edges).
    vec3 r = viewPos(vUV + vec2(uTexel.x, 0)) - P, l = P - viewPos(vUV - vec2(uTexel.x, 0));
    vec3 u = viewPos(vUV + vec2(0, uTexel.y)) - P, b = P - viewPos(vUV - vec2(0, uTexel.y));
    vec3 dx = abs(r.z) < abs(l.z) ? r : l;
    vec3 dy = abs(u.z) < abs(b.z) ? u : b;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, -P) < 0.0) N = -N;

    float ang = hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 rnd = vec3(cos(ang), sin(ang), 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float occ = 0.0;
    const int S = 16;
    for (int i = 0; i < S; ++i) {
        float fi = (float(i) + 0.5) / float(S);
        float th = float(i) * 2.39996;
        float ph = acos(sqrt(1.0 - fi));
        vec3 s = vec3(cos(th) * sin(ph), sin(th) * sin(ph), cos(ph));
        s = normalize(vec3(s.xy, max(s.z, 0.35)));   // no samples lying flat on the surface
        s *= mix(0.1, 1.0, fi * fi);
        vec3 sp = P + TBN * s * uRadius;
        vec4 o = uProj * vec4(sp, 1.0);
        vec2 suv = o.xy / o.w * 0.5 + 0.5;
        float sceneZ = viewPos(suv).z;
        float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
        // The bias grows with distance: far-away depth values are less precise.
        float bias = 0.02 + 0.006 * abs(P.z);
        occ += (sceneZ >= sp.z + bias ? 1.0 : 0.0) * range;
    }
    FragAO = clamp(1.0 - occ / float(S), 0.0, 1.0);
}
)";

// Bloom: keep only the bright parts.
inline const char* bloomPrefilterFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform float uThreshold;
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - uThreshold + 0.5, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
    FragColor = vec4(min(c * contrib, vec3(60.0)), 1.0);
}
)";

inline const char* bloomDownFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;     // of the source
out vec4 FragColor;
void main() {
    vec3 a = texture(uSrc, vUV + uTexel * vec2(-1, -1)).rgb;
    vec3 b = texture(uSrc, vUV + uTexel * vec2( 1, -1)).rgb;
    vec3 c = texture(uSrc, vUV + uTexel * vec2(-1,  1)).rgb;
    vec3 d = texture(uSrc, vUV + uTexel * vec2( 1,  1)).rgb;
    vec3 e = texture(uSrc, vUV).rgb;
    FragColor = vec4((a + b + c + d) * 0.125 + e * 0.5, 1.0);
}
)";

inline const char* bloomUpFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 FragColor;
void main() {
    // 3x3 tent filter.
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV + uTexel * vec2(-1, 0)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2( 1, 0)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(0, -1)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(0,  1)).rgb * 2.0;
    s += texture(uSrc, vUV + uTexel * vec2(-1, -1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2( 1, -1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2(-1,  1)).rgb;
    s += texture(uSrc, vUV + uTexel * vec2( 1,  1)).rgb;
    FragColor = vec4(s / 16.0, 1.0);
}
)";

// SunRaysEffect: light streaming out from the sun past whatever is in front of it.
// Small (a quarter of the screen): each pixel walks toward the sun and adds up how
// much open, bright sky it passes.
inline const char* sunRaysFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uHdr;
uniform sampler2D uDepth;
uniform vec2  uSun;          // where the sun is on screen (0..1, can be off it)
uniform float uSpread;       // how much of the way to the sun each pixel looks (0..1)
uniform float uStrength;
uniform float uAspect;       // width / height
uniform int   uSamples;
out vec4 FragColor;
float skyLight(vec2 uv) {
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.0;
    if (texture(uDepth, uv).r < 0.9999) return 0.0;              // something solid
    // Open sky round the sun (clouds in front of it dim it a little).
    vec3 c = texture(uHdr, uv).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec2 d = (uv - uSun) * vec2(uAspect, 1.0);
    return (0.3 + 0.7 * clamp(l * 0.25, 0.0, 1.0)) * exp(-dot(d, d) * 10.0);
}
void main() {
    vec2 step = (uSun - vUV) * (0.15 + 0.85 * uSpread) / float(uSamples);
    // Each pixel starts a little further along (no stripes from the steps).
    vec2 p = vUV + step * fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float sum = 0.0, w = 1.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSamples) break;
        sum += skyLight(p) * w;
        w *= 0.965;
        p += step;
    }
    FragColor = vec4(sum / float(uSamples) * uStrength, 0.0, 0.0, 1.0);
}
)";

// Final image: AO, bloom, exposure, filmic tone mapping, colour grading.
inline const char* compositeFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform sampler2D uAO;
uniform bool  uUseBloom;
uniform bool  uUseAO;
uniform bool  uPost;
uniform vec2  uAOTexel;
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uAOIntensity;
uniform float uContrast;
uniform float uSaturation;
uniform float uVignette;
uniform vec3  uTint;
uniform bool  uLumaAlpha;   // store luma in alpha for the FXAA pass
// Underwater (the camera is in water): the view wobbles, and the further away
// something is the more the water hides it, red first (Beer's law), fading into
// the water's own colour.
uniform bool  uUnderwater;
uniform sampler2D uDepth;
uniform vec2  uDepthParams;    // proj[3][2], proj[2][2]: distance from the depth picture
uniform bool  uOrthoDepth;     // ...for an orthographic camera (depth is already even)
uniform vec3  uWaterSigma;     // how much each colour fades per stud
uniform vec3  uWaterFog;       // the colour far-away things fade into
uniform float uTime;
uniform mat4  uInvViewProj;    // (underwater: where each pixel is, for caustics)
uniform float uWaterTop;       // the surface above the camera
uniform float uCaustics;       // how strong the sun's caustics are down here
uniform float uDrip;           // just surfaced: water running down the screen (1 .. 0)
// Screen effects (Lighting / Camera): BlurEffect and DepthOfFieldEffect mix in blurrier
// copies of the picture (uBlur1..4, each about twice as blurry), SunRaysEffect adds
// light streaks (uRays), ColorCorrectionEffect changes the finished colours.
uniform bool  uBlurOn;
uniform sampler2D uBlur1, uBlur2, uBlur3, uBlur4;
uniform float uBlurLevel;      // BlurEffect: 0 = sharp .. 4 = the blurriest copy
uniform bool  uDof;
uniform vec4  uDofParams;      // FocusDistance, InFocusRadius, NearIntensity, FarIntensity
uniform bool  uRaysOn;
uniform sampler2D uRays;
uniform vec3  uRaysColor;
uniform bool  uGrade;
uniform vec4  uGradeParams;    // Brightness, Contrast (1 = same), Saturation (1 = same), unused
uniform vec3  uGradeTint;
out vec4 FragColor;

vec3 blurAt(vec2 uv, float lv) {
    // Between two neighbouring copies, so the blur grows smoothly.
    vec3 a, b;
    float f = fract(lv);
    if (lv < 1.0)      { a = texture(uHdr, uv).rgb;   b = texture(uBlur1, uv).rgb; }
    else if (lv < 2.0) { a = texture(uBlur1, uv).rgb; b = texture(uBlur2, uv).rgb; }
    else if (lv < 3.0) { a = texture(uBlur2, uv).rgb; b = texture(uBlur3, uv).rgb; }
    else if (lv < 4.0) { a = texture(uBlur3, uv).rgb; b = texture(uBlur4, uv).rgb; }
    else               { a = texture(uBlur4, uv).rgb; b = a; }
    return mix(a, b, f);
}

float causticsAt(vec2 x, float t) {
    float c = 0.0;
    for (int i = 0; i < 3; ++i) {
        float k = 1.0 + 0.6 * float(i);
        vec2 q = x * k + vec2(t * 0.35, -t * 0.27) * k;
        q += 0.7 * vec2(sin(q.y * 1.3 + t * 0.9), sin(q.x * 1.1 - t * 0.8));
        c += pow(1.0 - abs(sin(q.x) * sin(q.y)), 8.0);
    }
    return c / 3.0;
}
float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
// Drops of water sliding down the screen after coming up for air: how much each pixel
// is bent (xy) and how much drop is there (z).
vec3 screenDrops(vec2 uv, float t) {
    vec3 r = vec3(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        vec2 grid = vec2(10.0, 4.0) * (1.0 + float(layer) * 0.7);
        vec2 st = uv * grid;
        vec2 id = floor(st);
        float h = h21(id + float(layer) * 17.0);
        if (h < 0.4) continue;                                   // not every cell has a drop
        float x = fract(st.x) - 0.5 - (h - 0.5) * 0.5;
        float y = fract(st.y + t * (0.15 + h * 0.35) + h * 7.0) - 0.5;
        float d = length(vec2(x * 1.6, y));
        float drop = smoothstep(0.2, 0.1, d);
        float trail = smoothstep(0.05, 0.015, abs(x)) * smoothstep(-0.02, 0.45, y) * 0.35;
        r.xy += vec2(x, y) * drop * 0.9 / grid + vec2(x, 0.0) * trail * 0.4 / grid;
        r.z = max(r.z, max(drop, trail));
    }
    return r;
}

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = vUV;
    vec3 drops = vec3(0.0);
    if (uDrip > 0.0) { drops = screenDrops(vUV, uTime) * uDrip; uv += drops.xy; }
    if (uUnderwater)
        uv += vec2(sin(vUV.y * 28.0 + uTime * 1.9), cos(vUV.x * 23.0 + uTime * 1.6)) * 0.0022;
    vec3 c;
    if (uUnderwater) {
        float ndc = texture(uDepth, uv).r * 2.0 - 1.0;
        float dist = ndc > 0.9999 ? 1e4 : uOrthoDepth ? (uDepthParams.x - ndc) / uDepthParams.y : uDepthParams.x / (ndc + uDepthParams.y);
        // Things further away go blurry, and the colours split a little at the edges of
        // the view (like looking through a diving mask).
        float blur = clamp(dist * 0.00035, 0.0, 0.004);
        vec2 ca = (uv - 0.5) * 0.006;
        c = vec3(0.0);
        const vec2 taps[5] = vec2[](vec2(0.0), vec2(1.0, 1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(-1.0, -1.0));
        for (int i = 0; i < 5; ++i) {
            vec2 o = taps[i] * blur;
            c += vec3(texture(uHdr, uv + o + ca).r, texture(uHdr, uv + o).g, texture(uHdr, uv + o - ca).b);
        }
        c /= 5.0;
        // Caustics on everything down here: where is this pixel in the world?
        if (ndc < 0.9999 && uCaustics > 0.0) {
            vec4 wp = uInvViewProj * vec4(uv * 2.0 - 1.0, ndc, 1.0);
            wp /= wp.w;
            if (wp.y < uWaterTop)
                c *= 1.0 + causticsAt(wp.xz * 1.4, uTime) * uCaustics * exp(-(uWaterTop - wp.y) * 0.12);
        }
        vec3 keep = exp(-min(dist, 400.0) * uWaterSigma);
        c = c * keep + uWaterFog * (1.0 - keep);
    } else {
        c = texture(uHdr, uv).rgb;
    }
    if (uBlurOn) {
        float lv = uBlurLevel;
        if (uDof) {
            float ndc = texture(uDepth, uv).r * 2.0 - 1.0;
            float dist = ndc > 0.9999 ? 1e4 : uOrthoDepth ? (uDepthParams.x - ndc) / uDepthParams.y : uDepthParams.x / (ndc + uDepthParams.y);
            float nearEdge = uDofParams.x - uDofParams.y, farEdge = uDofParams.x + uDofParams.y;
            float farK = clamp((dist - farEdge) / max(farEdge, 20.0), 0.0, 1.0) * uDofParams.w;
            float nearK = nearEdge > 0.0 ? clamp((nearEdge - dist) / max(nearEdge, 1.0), 0.0, 1.0) * uDofParams.z : 0.0;
            lv = max(lv, max(farK, nearK) * 3.5);
        }
        if (lv > 0.001) c = blurAt(uv, min(lv, 4.0));
    }
    if (uRaysOn) {   // (smoothed a little: it was worked out small)
        vec2 rt = 1.0 / vec2(textureSize(uRays, 0));
        float r = texture(uRays, vUV + rt * vec2(-0.75, -0.75)).r + texture(uRays, vUV + rt * vec2(0.75, -0.75)).r +
                  texture(uRays, vUV + rt * vec2(-0.75, 0.75)).r + texture(uRays, vUV + rt * vec2(0.75, 0.75)).r;
        c += r * 0.25 * uRaysColor;
    }
    c *= 1.0 + drops.z * 0.12;   // (the drops catch the light a little)
    if (uUseAO) {
        float ao = 0.0;
        ao += texture(uAO, vUV + uAOTexel * vec2(-1.5, -1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2( 1.5, -1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2(-1.5,  1.5)).r;
        ao += texture(uAO, vUV + uAOTexel * vec2( 1.5,  1.5)).r;
        ao *= 0.25;
        c *= mix(1.0, ao, clamp(uAOIntensity, 0.0, 2.0) * 0.85);
    }
    if (uUseBloom) c += texture(uBloom, vUV).rgb * uBloomIntensity * 0.15;
    c *= uExposure;

    if (uPost) {
        c = aces(c);
        c = pow(c, vec3(1.0 / 2.2));
        c = (c - 0.5) * uContrast + 0.5;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(l), c, uSaturation) * uTint;
        float v = length(vUV - 0.5) * 1.41421;
        c *= 1.0 - (uVignette + (uUnderwater ? 0.35 : 0.0)) * smoothstep(0.35, 1.1, v);
    } else {
        c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    if (uGrade) {   // ColorCorrectionEffect(s)
        c = clamp(c + uGradeParams.x, 0.0, 1.0);
        c = (c - 0.5) * uGradeParams.y + 0.5;
        float gl = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(gl), c, uGradeParams.z) * uGradeTint;
    }
    c = clamp(c, 0.0, 1.0);
    FragColor = vec4(c, uLumaAlpha ? dot(c, vec3(0.299, 0.587, 0.114)) : 1.0);
}
)";

inline const char* fxaaFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 FragColor;
void main() {
    vec3 rgbNW = texture(uSrc, vUV + vec2(-1, -1) * uTexel).rgb;
    vec3 rgbNE = texture(uSrc, vUV + vec2( 1, -1) * uTexel).rgb;
    vec3 rgbSW = texture(uSrc, vUV + vec2(-1,  1) * uTexel).rgb;
    vec3 rgbSE = texture(uSrc, vUV + vec2( 1,  1) * uTexel).rgb;
    vec3 rgbM  = texture(uSrc, vUV).rgb;
    vec3 lw = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, lw), lNE = dot(rgbNE, lw), lSW = dot(rgbSW, lw), lSE = dot(rgbSE, lw);
    float lM = dot(rgbM, lw);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * uTexel;

    vec3 a = 0.5 * (texture(uSrc, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(uSrc, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uSrc, vUV + dir * -0.5).rgb +
                               texture(uSrc, vUV + dir *  0.5).rgb);
    float lB = dot(b, lw);
    FragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Real liquid (Liquid.cpp), drawn the way film and Blender previews do it in
// real time: the drops are splatted as little spheres into a depth picture,
// which is smoothed until they melt into one surface; then that surface is lit
// with reflections of the sky, the scene bent through it and deeper = bluer.
// ---------------------------------------------------------------------------

inline const char* fluidVert = R"(#version 410 core
layout(location=0) in vec4 aDrop;   // position, w (kind x 4096 + neighbours x 64 + speed)
layout(location=1) in vec4 aShape;  // the axis it's squashed along, and how much (1 = round)
uniform mat4  uView;
uniform mat4  uProj;
uniform float uPointScale;
uniform float uRadius;
uniform vec3  uFluidColor[16];      // each kind of liquid's colour
uniform float uRenderDist;          // drops further away than this aren't drawn (0 = no limit)
out vec3  vCenter;
out float vSpeed;
out float vRadius;
out vec3  vColor;
out vec3  vAxis;     // (view space)
out float vFlat;     // radius along the axis, as a fraction
out float vLong;     // radius across it, as a fraction
void main() {
    vec4 vp = uView * vec4(aDrop.xyz, 1.0);
    vCenter = vp.xyz;
    // Squashed along its axis (a flat disc along a surface or a film on the floor),
    // and wider across it, so the discs overlap into one smooth sheet.
    float l = length(aShape.xyz);
    vAxis = l > 1e-4 ? mat3(uView) * (aShape.xyz / l) : vec3(0.0, 1.0, 0.0);
    vFlat = clamp(aShape.w, 0.2, 1.0);
    vLong = min(1.7, inversesqrt(vFlat));
    // w = kind of liquid x 4096 + neighbours x 64 + speed. A drop on its own is a
    // little droplet; one among others is drawn full size so they all melt into one surface.
    float kind = floor(aDrop.w / 4096.0);
    float rest = aDrop.w - kind * 4096.0;
    float nearby = floor(rest / 64.0);
    vSpeed = rest - nearby * 64.0;
    vColor = uFluidColor[int(clamp(kind, 0.0, 15.0))];
    vRadius = uRadius * mix(0.5, 1.0, clamp(nearby / 5.0, 0.0, 1.0));
    gl_Position = uProj * vp;
    gl_PointSize = clamp(2.1 * vRadius * vLong * uPointScale / max(0.05, -vp.z), 1.0, 256.0);
    if (uRenderDist > 0.0 && -vp.z > uRenderDist) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // too far: skipped
}
)";

// Where the ray through this pixel goes into and out of the drop (a squashed ball),
// in view space: hit = ray * t0 .. ray * t1. For the drop passes below.
inline const char* fluidRay = R"(
bool hitDrop(out vec3 ray, out float t0, out float t1) {
    vec2 ndc = gl_FragCoord.xy * uTexel * 2.0 - 1.0;
    ray = normalize(vec3(ndc.x / uProj[0][0], ndc.y / uProj[1][1], -1.0));
    // Stretch space so the drop becomes a ball of radius 1, and hit that.
    float ia = 1.0 / (vRadius * vLong), ib = 1.0 / (vRadius * vFlat);
    vec3 oc = -vCenter;
    vec3 o2 = oc * ia + vAxis * (dot(oc, vAxis) * (ib - ia));
    vec3 d2 = ray * ia + vAxis * (dot(ray, vAxis) * (ib - ia));
    float A = dot(d2, d2), B = dot(o2, d2), C = dot(o2, o2) - 1.0;
    float disc = B * B - A * C;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    t0 = (-B - s) / A;
    t1 = (-B + s) / A;
    return t1 > 0.0;
}
)";

// Pass 1: how far away the nearest liquid is (in each pixel).
inline const char* fluidDepthFrag = R"(#version 410 core
in vec3  vCenter;
in float vSpeed;
in float vRadius;
in vec3  vColor;
in vec3  vAxis;
in float vFlat;
in float vLong;
uniform mat4  uProj;
uniform sampler2D uSceneDepth;
uniform vec2  uTexel;
out vec4 FragColor;
#pragma gb_fluidray
void main() {
    vec3 ray; float t0, t1;
    if (!hitDrop(ray, t0, t1)) discard;
    vec3 pos = ray * max(t0, 0.0);
    vec4 clip = uProj * vec4(pos, 1.0);
    float z = clip.z / clip.w * 0.5 + 0.5;
    if (z > texture(uSceneDepth, gl_FragCoord.xy * uTexel).r) discard;   // behind something solid
    gl_FragDepth = z;
    FragColor = vec4(-pos.z, 0.0, 0.0, 1.0);
}
)";

// Pass 2: how much liquid there is along each pixel (added up), and how fast it's going.
inline const char* fluidThickFrag = R"(#version 410 core
in vec3  vCenter;
in float vSpeed;
in float vRadius;
in vec3  vColor;
in vec3  vAxis;
in float vFlat;
in float vLong;
uniform mat4  uProj;
uniform sampler2D uSceneDepth;
uniform vec2  uTexel;
out vec4 FragColor;
#pragma gb_fluidray
void main() {
    vec3 ray; float t0, t1;
    if (!hitDrop(ray, t0, t1)) discard;
    vec4 clip = uProj * vec4(ray * max(0.5 * (t0 + t1), 0.0), 1.0);
    if (clip.z / clip.w * 0.5 + 0.5 > texture(uSceneDepth, gl_FragCoord.xy * uTexel).r) discard;
    float t = (t1 - max(t0, 0.0)) * 0.6;   // how much water this drop puts along the ray (drops overlap)
    FragColor = vec4(t, vSpeed * t, t, 1.0);
}
)";

// Pass 2b (more than one kind of liquid): the colour along each pixel, weighted
// by how much of each liquid there is.
inline const char* fluidColorFrag = R"(#version 410 core
in vec3  vCenter;
in float vSpeed;
in float vRadius;
in vec3  vColor;
in vec3  vAxis;
in float vFlat;
in float vLong;
uniform mat4  uProj;
uniform sampler2D uSceneDepth;
uniform vec2  uTexel;
out vec4 FragColor;
#pragma gb_fluidray
void main() {
    vec3 ray; float t0, t1;
    if (!hitDrop(ray, t0, t1)) discard;
    vec4 clip = uProj * vec4(ray * max(0.5 * (t0 + t1), 0.0), 1.0);
    if (clip.z / clip.w * 0.5 + 0.5 > texture(uSceneDepth, gl_FragCoord.xy * uTexel).r) discard;
    float t = (t1 - max(t0, 0.0)) * 0.6;
    FragColor = vec4(vColor * t, t);
}
)";

// Pass 3: smooth the depth so the drops become one surface, keeping the edges
// sharp. An "a-trous" filter: a small 5x5 blur run several times with the gaps
// between its samples doubling each time (1, 2, 4, 8...), so it smooths a wide
// area without skipping any bumps. Nearby depths are averaged, but one far in
// front or behind (another stream) only counts as if it were at the edge of the
// range ("narrow range"), so steps stay steps.
inline const char* fluidBlurFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2  uTexel;        // one pixel
uniform float uStep;         // this pass's gap between samples, in world units (doubles each pass)
uniform float uRange;        // depth differences bigger than this are edges
uniform float uPointScale;
out vec4 FragColor;
void main() {
    float d = texture(uSrc, vUV).r;
    if (d <= 0.0) {
        // A pinhole between drops: fill it if there's liquid on opposite sides of it.
        float a = texture(uSrc, vUV + vec2(uTexel.x, 0.0)).r, b = texture(uSrc, vUV - vec2(uTexel.x, 0.0)).r;
        float c = texture(uSrc, vUV + vec2(0.0, uTexel.y)).r, e = texture(uSrc, vUV - vec2(0.0, uTexel.y)).r;
        if (a > 0.0 && b > 0.0) d = 0.5 * (a + b);
        else if (c > 0.0 && e > 0.0) d = 0.5 * (c + e);
        else { FragColor = vec4(0.0); return; }
    }
    float px = clamp(uStep * uPointScale / d, 1.0, 24.0);   // the gap in pixels (nearer = wider)
    const float K[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            float s = texture(uSrc, vUV + vec2(float(x), float(y)) * px * uTexel).r;
            if (s <= 0.0) continue;
            float dz = clamp(s - d, -uRange, uRange);
            float w = K[x + 2] * K[y + 2] * exp(-(dz * dz) / (uRange * uRange) * 3.0);
            sum += (d + dz) * w;
            wsum += w;
        }
    FragColor = vec4(sum / max(wsum, 1e-6), 0.0, 0.0, 1.0);
}
)";

// Pass 4: light the surface and put it into the picture.
inline const char* fluidShadeFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uDepth;       // smoothed depth
uniform sampler2D uThick;       // thickness, speed
uniform sampler2D uScene;       // what's behind the water
uniform sampler2D uSceneDepth;  // ... and how far away it is
uniform mat4  uProj;
uniform mat4  uInvView;
uniform vec2  uTexel;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uZenith;
uniform vec3  uHorizon;
uniform vec3  uGround;
uniform float uSkyBrightness;
uniform vec3  uAmbient;
uniform bool  uTinted;          // more than one kind: the colour comes from uColorTex
uniform sampler2D uColorTex;    // colour x thickness, thickness
uniform vec3  uTint;            // (one kind) its colour
uniform bool  uWaterShadows;    // the sun's shadow map (so shade falls on the water too)
uniform sampler2D uShadowMap;
uniform mat4  uLightSpace;
uniform float uShadowStrength;
uniform bool  uReflections;     // mirror the scene (not just the sky)
uniform float uTime;
out vec4 FragColor;
#pragma gb_common
vec3 eyePos(vec2 uv, float d) {
    vec2 ndc = uv * 2.0 - 1.0;
    if (uProj[3][3] > 0.5) return vec3(ndc.x / uProj[0][0], ndc.y / uProj[1][1], -d);   // orthographic camera
    return vec3(ndc.x * d / uProj[0][0], ndc.y * d / uProj[1][1], -d);
}
// Distance to the solid scene in a pixel (from the depth picture).
float sceneDist(vec2 uv) {
    float ndc = texture(uSceneDepth, uv).r * 2.0 - 1.0;
    if (uProj[3][3] > 0.5) return (uProj[3][2] - ndc) / uProj[2][2];   // orthographic camera
    return uProj[3][2] / (ndc + uProj[2][2]);
}
const float kIor = 1.333;       // water bends light this much (Snell's law)
// How much sun reaches this bit of water (1 = all, 0 = in something's shadow).
float sunLight(vec3 wp) {
    if (!uWaterShadows) return 1.0;
    vec4 lc = uLightSpace * vec4(wp, 1.0);
    vec3 s = lc.xyz / lc.w * 0.5 + 0.5;
    if (s.z > 1.0 || any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(uShadowMap, s.xy + vec2(x, y) * texel * 1.5).r < s.z - 0.0002 ? 0.0 : 1.0;
    return mix(1.0, lit / 9.0, uShadowStrength);
}
// Screen-space reflection: walk along the reflected ray through the picture until it
// goes behind something solid; that's what the water mirrors there. `found` says how
// sure it is (0 = nothing found: use the sky).
vec3 mirror(vec3 p, vec3 R, out float found) {
    found = 0.0;
    if (!uReflections) return vec3(0.0);
    float stepLen = 0.25;
    vec3 q = p + R * 0.1;
    for (int i = 0; i < 40; ++i) {
        q += R * stepLen;
        stepLen *= 1.12;
        if (q.z > -0.05) return vec3(0.0);                  // came back past the camera
        vec4 c = uProj * vec4(q, 1.0);
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec3(0.0);
        float behind = -q.z - sceneDist(uv);
        if (behind > 0.0) {
            if (behind > stepLen * 2.0 + 0.5) return vec3(0.0);   // went behind a thin thing, not into it
            // Fade out near the sides of the picture (where the answer suddenly runs out).
            vec2 e = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
            found = e.x * e.y * (1.0 - float(i) / 40.0);
            return texture(uScene, uv).rgb;
        }
    }
    return vec3(0.0);
}
// Caustics: the wavy surface bunches sunlight into bright moving lines on the floor
// below it. (Warped grid lines, a few layers drifting different ways.)
float caustics(vec2 x, float t) {
    float c = 0.0;
    for (int i = 0; i < 3; ++i) {
        float k = 1.0 + 0.6 * float(i);
        vec2 q = x * k + vec2(t * 0.35, -t * 0.27) * k;
        q += 0.7 * vec2(sin(q.y * 1.3 + t * 0.9), sin(q.x * 1.1 - t * 0.8));
        c += pow(1.0 - abs(sin(q.x) * sin(q.y)), 8.0);
    }
    return c / 3.0;
}
void main() {
    float d = texture(uDepth, vUV).r;
    if (d <= 0.0) discard;
    vec3 p = eyePos(vUV, d);
    // Normal from the neighbouring depths (whichever side is closer, so edges stay clean).
    // (Two pixels either side: steadier than the next pixel over.)
    vec2 tx = vec2(uTexel.x * 2.0, 0.0), ty = vec2(0.0, uTexel.y * 2.0);
    float dR = texture(uDepth, vUV + tx).r, dL = texture(uDepth, vUV - tx).r;
    float dU = texture(uDepth, vUV + ty).r, dD = texture(uDepth, vUV - ty).r;
    // At the edge of the liquid there's no neighbour on one side: treat it as flat there.
    if (dR <= 0.0) dR = d; if (dL <= 0.0) dL = d; if (dU <= 0.0) dU = d; if (dD <= 0.0) dD = d;
    // Both sides where the surface carries on smoothly (steady); only the nearer side
    // at an edge (so the edge of a stream doesn't bend towards what's behind it).
    vec3 ddx = eyePos(vUV + tx, dR) - p, ddx2 = p - eyePos(vUV - tx, dL);
    if (abs(ddx.z - ddx2.z) < 0.3) ddx = 0.5 * (ddx + ddx2);
    else if (abs(ddx2.z) < abs(ddx.z)) ddx = ddx2;
    vec3 ddy = eyePos(vUV + ty, dU) - p, ddy2 = p - eyePos(vUV - ty, dD);
    if (abs(ddy.z - ddy2.z) < 0.3) ddy = 0.5 * (ddy + ddy2);
    else if (abs(ddy2.z) < abs(ddy.z)) ddy = ddy2;
    vec3 n = normalize(cross(ddx, ddy));
    if (dot(n, -p) < 0.0) n = -n;

    vec4 th = texture(uThick, vUV);
    float thick = th.r;
    float speed = th.g / max(th.b, 1e-4);

    vec3 V = normalize(-p);
    vec3 nW = normalize(mat3(uInvView) * n);
    vec3 vW = normalize(mat3(uInvView) * -V);
    // Reflection of the sky, and the sun glinting off it.
    vec3 refl = reflect(vW, nW);
    vec3 sky = lin(skyGradient(refl, uZenith, uHorizon, uGround)) * uSkyBrightness;
    // Where the surface turns a lot within a pixel, the sun's sharp glint spreads
    // into a softer sheen (Toksvig): no single-pixel sparkles on small ripples.
    float wobble = clamp(length(fwidth(nW)) * 3.0, 0.0, 1.0);
    float shine = mix(600.0, 40.0, wobble);
    float sd = max(dot(refl, normalize(uSunDir)), 0.0);
    float sunSpec = pow(sd, shine) * 40.0 * (shine + 2.0) / 602.0 + pow(sd, 60.0) * 0.6;
    vec3 wp = (uInvView * vec4(p, 1.0)).xyz;
    float sun = sunLight(wp);
    float found;
    vec3 seen = mirror(p, reflect(-V, n), found);
    vec3 reflCol = mix(sky, seen, found) + uSunColor * uSunIntensity * sunSpec * sun;
    // Looking through it. How far the light goes through water before it hits
    // something solid: the water's thickness, or less if the ground is closer.
    // With the ground right behind the water (a pool), that's the distance from the
    // surface to the ground: smooth and exact. A stream in mid-air has nothing close
    // behind it: then it's the water's own thickness (added up from the drops).
    float solid = sceneDist(vUV);
    float gap = max(solid - d, 0.0);
    float path = clamp(gap < thick * 1.6 + 0.4 ? gap : thick, 0.0, 12.0);
    // Snell's law: the ray bends as it goes from air into water. Follow the bent
    // ray that far and look up what's there (the "warp" of things underwater).
    vec3 T = refract(-V, n, 1.0 / kIor);
    vec4 hitClip = uProj * vec4(p + T * max(path, 0.15), 1.0);
    vec2 bent = clamp(hitClip.xy / hitClip.w * 0.5 + 0.5, vec2(0.001), vec2(0.999));
    if (sceneDist(bent) < d) bent = vUV;          // (never show something that's in front of the water)
    vec3 behind = texture(uScene, bent).rgb;
    // Where we're looking at the pool floor, the sun's caustics play across it
    // (stronger in the sun and in shallow water, fading as it gets deeper).
    if (gap < thick * 1.6 + 0.4) {
        vec3 floorW = (uInvView * vec4(p + T * path, 1.0)).xyz;
        float amount = sun * uSunIntensity * smoothstep(0.05, 0.3, path) * exp(-path * 0.2) * max(normalize(uSunDir).y, 0.0);
        behind *= 1.0 + caustics(floorW.xz * 1.6, uTime) * 1.6 * amount;
    }
    // The liquid's colour here (a mix, where different liquids meet).
    vec3 tint = uTint;
    if (uTinted) { vec4 ct = texture(uColorTex, vUV); if (ct.a > 1e-4) tint = ct.rgb / ct.a; }
    vec3 deep = pow(clamp(tint, 0.0, 1.0), vec3(2.2));
    // Beer's law: the liquid soaks up the colours it isn't, the deeper the more. Water
    // soaks up red light first, then green, so deep water looks darker and bluer.
    // (Per stud. Real water is clearer still, but a little tint reads as "water" in a game.)
    vec3 sigma = 0.03 + 0.25 * -log(max(tint, vec3(0.02)));
    vec3 absorb = exp(-path * sigma);
    vec3 scatter = deep * uAmbient * 1.4 + deep * uSunColor * uSunIntensity * 0.25 * max(nW.y, 0.0) * sun;
    vec3 through = behind * absorb + scatter * (1.0 - absorb);
    // Fresnel (Schlick): a mirror at grazing angles, clear looking straight in.
    // Water's straight-on reflectance is ((1.333 - 1) / (1.333 + 1))^2 = 2%.
    float f0 = ((kIor - 1.0) / (kIor + 1.0)) * ((kIor - 1.0) / (kIor + 1.0));
    float fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 col = mix(through, reflCol, fres);
    // White water where it's fast and thin (spray, the front of the stream).
    float foam = smoothstep(14.0, 30.0, speed) * clamp(1.4 - path, 0.0, 1.0);
    col = mix(col, (uAmbient * 1.6 + uSunColor * uSunIntensity * 0.5 * sun) * 0.9, foam * 0.7);
    // Where it's very thin (the edge of a puddle, a lone drop) it fades into what's
    // behind, instead of ending in a hard rim.
    col = mix(texture(uScene, vUV).rgb, col, smoothstep(0.02, 0.3, thick));
    vec4 clip = uProj * vec4(p, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    FragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Water parts (pools, lakes, rivers, FluidVolumes)
// ---------------------------------------------------------------------------
// Drawn after the solid scene, reading a copy of its colour and depth:
//  * Gerstner waves move the surface (the same ones the physics floats things on).
//  * How deep the water is at each pixel = the distance from the surface to the
//    ground behind it, so it fades from crystal clear at the shore to dark blue in
//    the deep (Beer-Lambert), by its Clarity.
//  * What's under the water is bent by Snell's law (index of refraction 1.333).
//  * Fresnel reflections of the sky and the scene, the sun's glint, caustics on the
//    bottom, foam where it meets the shore and on breaking crests.
inline const char* waterVert = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;      // wave meshes: x = foam from the ripples, y = 1 on the moving surface
uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProj;
uniform mat3  uNormalMat;
uniform bool  uWaveMesh;             // the moving surface (while playing), not a plain box
uniform float uWaveAmp;              // Gerstner: total height (0 = calm)
uniform float uSteep;                // how much the water bunches towards the crests
uniform vec4  uWaves[4];             // direction x, z, length, share of the height
uniform float uWavePhase[4];
uniform float uTime;
uniform vec3  uBodyMin, uBodyMax;
out vec3  vWorldPos;
out vec3  vNormal;
out float vFoam;
out float vCrest;
void main() {
    vec3 w = (uModel * vec4(aPos, 1.0)).xyz;
    vec3 n = normalize(uNormalMat * aNormal);
    float crest = 0.0;
    if (uWaveMesh && uWaveAmp > 0.0 && aUV.y > 0.5) {
        // Near the sides the water only rises and falls (so it can't poke through walls).
        float edge = clamp(min(min(w.x - uBodyMin.x, uBodyMax.x - w.x), min(w.z - uBodyMin.z, uBodyMax.z - w.z)) * 0.5, 0.0, 1.0);
        float q = uSteep / 4.0;
        vec3 d = vec3(0.0);
        vec3 gn = vec3(0.0, 1.0, 0.0);
        for (int i = 0; i < 4; ++i) {
            vec2 dir = uWaves[i].xy;
            float k = 6.2831853 / uWaves[i].z;
            float A = uWaveAmp * uWaves[i].w;
            float ph = k * dot(dir, w.xz) - sqrt(9.8 * k) * uTime + uWavePhase[i];
            float c = cos(ph), s = sin(ph);
            d.xz += dir * (q / k) * c * edge;
            d.y  += A * s;
            gn.x -= dir.x * k * A * c;
            gn.z -= dir.y * k * A * c;
            gn.y -= q * s * edge;
            crest += q * s * edge;
        }
        w += d;
        if (n.y > 0.5) n = normalize(gn + (n - vec3(0.0, 1.0, 0.0)));   // plus the ripples
    }
    vWorldPos = w;
    vNormal = n;
    vFoam = uWaveMesh ? aUV.x : 0.0;
    vCrest = crest;   // how bunched up the water is here (whitecaps on big, steep waves)
    gl_Position = uProj * uView * vec4(w, 1.0);
}
)";

inline const char* waterFrag = R"(#version 410 core
in vec3  vWorldPos;
in vec3  vNormal;
in float vFoam;
in float vCrest;
uniform sampler2D uScene;        // the solid scene behind the water
uniform sampler2D uSceneDepth;   // ... and how far away it is
uniform mat4  uView;
uniform mat4  uProj;
uniform vec2  uTexel;
uniform vec3  uViewPos;
uniform vec3  uColor;            // the water's colour
uniform float uClarity;          // 1 crystal clear .. 0 murky
uniform float uTime;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uZenith, uHorizon, uGround;
uniform float uSkyBrightness;
uniform vec3  uAmbient;
uniform bool  uReflections;
uniform bool  uWaterShadows;
uniform sampler2D uShadowMap;
uniform mat4  uLightSpace;
uniform float uShadowStrength;
uniform bool  uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogSunGlow;
uniform float uRenderDist;
uniform bool  uSelected;
// The current: a flow map (x, z speed per grid point, bent around rocks) or one
// direction for the whole body. The little ripples are carried along it.
uniform bool  uHasFlowMap;
uniform sampler2D uFlowMap;
uniform vec3  uFlowGrid;         // cell size, points across x, points across z
uniform vec2  uFlowBase;
uniform vec3  uBodyMin, uBodyMax;
out vec4 FragColor;
#pragma gb_common
const float kIor = 1.333;

// View-space distance to the solid scene in a pixel.
float sceneZ(vec2 uv) {
    float ndc = texture(uSceneDepth, uv).r * 2.0 - 1.0;
    if (uProj[3][3] > 0.5) return (uProj[3][2] - ndc) / uProj[2][2];   // orthographic camera
    return uProj[3][2] / (ndc + uProj[2][2]);
}
vec2 toScreen(vec3 w) {
    vec4 c = uProj * uView * vec4(w, 1.0);
    return clamp(c.xy / c.w * 0.5 + 0.5, vec2(0.001), vec2(0.999));
}
// Little ripples on top of the big waves (the "normal map", made up as we go).
// `drift` moves the two layers of ripples (they drift different ways on still water).
vec3 rippleNormal(vec2 p, vec2 driftA, vec2 driftB) {
    vec2 e = vec2(0.08, 0.0);
    vec2 a = p * 1.3 + driftA, b = p * 2.9 + driftB;
    float h  = vnoise(a) * 0.6 + vnoise(b) * 0.4;
    float hx = vnoise(a + e.xy * 1.3) * 0.6 + vnoise(b + e.xy * 2.9) * 0.4;
    float hz = vnoise(a + e.yx * 1.3) * 0.6 + vnoise(b + e.yx * 2.9) * 0.4;
    return normalize(vec3((h - hx) / e.x, 1.0, (h - hz) / e.x) * vec3(0.12, 1.0, 0.12));
}
vec2 flowHere(vec2 xz) {
    if (!uHasFlowMap) return uFlowBase;
    vec2 uv = ((xz - uBodyMin.xz) / uFlowGrid.x + 0.5) / uFlowGrid.yz;
    return texture(uFlowMap, uv).rg;
}
// Flow mapping: the ripples are carried along the current. Two copies of them slide
// along it, each restarting when the other is at its strongest, and blend, so they
// flow forever without stretching.
vec3 detailNormal(vec2 p, vec2 flow) {
    float t = uTime;
    if (dot(flow, flow) < 0.01) return rippleNormal(p, vec2(t * 0.35, t * 0.21), -vec2(t * 0.27, -t * 0.4));
    float ph0 = fract(t * 0.4), ph1 = fract(t * 0.4 + 0.5);
    float w0 = 1.0 - abs(2.0 * ph0 - 1.0);
    vec2 s0 = -flow * ph0 * 2.5, s1 = -flow * ph1 * 2.5 + vec2(3.7, 1.9);
    vec3 n0 = rippleNormal(p + s0, s0 * 0.3, s0 * 1.1);
    vec3 n1 = rippleNormal(p + s1, s1 * 0.3, s1 * 1.1);
    return normalize(mix(n1, n0, w0));
}
// Foam lace carried along by the current the same way.
float laceAt(vec2 p, vec2 flow) {
    float t = uTime;
    if (dot(flow, flow) < 0.01) return vnoise(p * 3.1 + t * 0.4) * 0.6 + vnoise(p * 7.3 - t * 0.3) * 0.4;
    float ph0 = fract(t * 0.4), ph1 = fract(t * 0.4 + 0.5);
    float w0 = 1.0 - abs(2.0 * ph0 - 1.0);
    vec2 q0 = p - flow * ph0 * 2.5, q1 = p - flow * ph1 * 2.5 + vec2(3.7, 1.9);
    float l0 = vnoise(q0 * 3.1) * 0.6 + vnoise(q0 * 7.3) * 0.4;
    float l1 = vnoise(q1 * 3.1) * 0.6 + vnoise(q1 * 7.3) * 0.4;
    return mix(l1, l0, w0);
}
float sunLight(vec3 wp) {
    if (!uWaterShadows) return 1.0;
    vec4 lc = uLightSpace * vec4(wp, 1.0);
    vec3 s = lc.xyz / lc.w * 0.5 + 0.5;
    if (s.z > 1.0 || any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(uShadowMap, s.xy + vec2(x, y) * texel * 1.5).r < s.z - 0.0002 ? 0.0 : 1.0;
    return mix(1.0, lit / 9.0, uShadowStrength);
}
// Screen-space reflection: walk along the reflected ray (view space) until it goes
// behind something solid.
vec2 projUV(vec3 q) { vec4 c = uProj * vec4(q, 1.0); return c.xy / c.w * 0.5 + 0.5; }
vec3 mirror(vec3 p, vec3 R, out float found) {
    found = 0.0;
    if (!uReflections) return vec3(0.0);
    // (A random start per pixel turns the steps' banding into fine noise.)
    float stepLen = 0.2 + 0.25 * hash12(gl_FragCoord.xy);
    vec3 q = p + R * 0.1, prev = q;
    for (int i = 0; i < 40; ++i) {
        prev = q;
        q += R * stepLen;
        stepLen *= 1.12;
        if (q.z > -0.05) return vec3(0.0);
        vec2 uv = projUV(q);
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec3(0.0);
        float behind = -q.z - sceneZ(uv);
        if (behind > 0.0) {
            if (behind > stepLen * 2.0 + 0.5) return vec3(0.0);   // went behind a thin thing, not into it
            // Home in on where it went in (binary search between the last two steps).
            vec3 a = prev, b = q;
            for (int k = 0; k < 5; ++k) {
                vec3 m = (a + b) * 0.5;
                if (-m.z - sceneZ(projUV(m)) > 0.0) b = m; else a = m;
            }
            uv = projUV(b);
            vec2 e = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
            found = e.x * e.y * (1.0 - float(i) / 40.0);
            return texture(uScene, uv).rgb;
        }
    }
    return vec3(0.0);
}
float caustics(vec2 x, float t) {
    float c = 0.0;
    for (int i = 0; i < 3; ++i) {
        float k = 1.0 + 0.6 * float(i);
        vec2 q = x * k + vec2(t * 0.35, -t * 0.27) * k;
        q += 0.7 * vec2(sin(q.y * 1.3 + t * 0.9), sin(q.x * 1.1 - t * 0.8));
        c += pow(1.0 - abs(sin(q.x) * sin(q.y)), 8.0);
    }
    return c / 3.0;
}
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    vec3 wp = vWorldPos;
    vec3 N = normalize(vNormal);
    vec3 toEye = uViewPos - wp;
    float eyeDist = length(toEye);
    vec2 flow = flowHere(wp.xz);
    // Little ripples close up; far away they're smaller than a pixel, so they'd only sparkle.
    if (N.y > 0.3) N = normalize(N + (detailNormal(wp.xz, flow) - vec3(0.0, 1.0, 0.0)) * clamp(1.0 - eyeDist / 70.0, 0.0, 1.0));
    vec3 V = toEye / eyeDist;
    bool below = dot(N, V) < 0.0;          // looking up at the surface from underwater
    if (below) N = -N;
    vec3 tint = clamp(uColor, vec3(0.03), vec3(1.0));
    vec3 deep = lin(tint);
    float sun = sunLight(wp);
    vec3 sunRad = uSunColor * uSunIntensity;
    // Light that bounces around inside the water: its own colour, the deep-water look.
    vec3 inscatter = deep * (uAmbient * 0.9 + sunRad * 0.22 * sun);
    float f0 = ((kIor - 1.0) / (kIor + 1.0)) * ((kIor - 1.0) / (kIor + 1.0));
    float cosV = max(dot(N, V), 0.0);
    vec3 col;
    vec4 pv = uView * vec4(wp, 1.0);
    float waterZ = -pv.z;
    if (!below) {
        // How far the light goes through water: from the surface to what's behind it.
        float rayScale = eyeDist / max(waterZ, 1e-3);
        float path = max(sceneZ(uv) - waterZ, 0.0) * rayScale;
        // Snell's law: follow the bent ray that far and see what's there.
        vec3 T = refract(-V, N, 1.0 / kIor);
        vec2 ruv = toScreen(wp + T * clamp(path, 0.2, 8.0));
        float rz = sceneZ(ruv);
        if (rz < waterZ) { ruv = uv; rz = sceneZ(uv); }        // (never something in front of the water)
        float rpath = max(rz - waterZ, 0.0) * rayScale;
        vec3 behind = texture(uScene, ruv).rgb;
        // Caustics dance on the bottom, strongest in shallow sunny water.
        vec3 floorW = wp + T * rpath;
        float amount = sun * uSunIntensity * smoothstep(0.05, 0.4, rpath) * exp(-rpath * 0.15) * max(normalize(uSunDir).y, 0.0);
        // The pattern is where the waves above focus the light: bent by the surface right
        // above, so it shifts in step with the waves (and drifts with the current).
        vec2 cp = floorW.xz * 1.4 + N.xz * (1.5 + rpath * 0.4) - flow * uTime * 0.3;
        behind *= 1.0 + caustics(cp, uTime) * 1.2 * amount;
        // Beer-Lambert: the deeper, the more of each colour is soaked up (red first).
        vec3 sigma = mix(0.35, 0.012, uClarity) * (0.4 + -log(tint));
        vec3 keep = exp(-min(rpath, 200.0) * sigma);
        vec3 through = behind * keep + inscatter * (1.0 - keep);
        // Reflections: the scene where we can find it, the sky everywhere else.
        vec3 R = reflect(-V, N);
        vec3 sky = lin(skyGradient(R, uZenith, uHorizon, uGround)) * uSkyBrightness;
        float found;
        vec3 seen = mirror(pv.xyz, normalize(mat3(uView) * R), found);
        // The sun's glint (Cook-Torrance: GGX highlights, Smith shadowing, Schlick
        // Fresnel): blinding sparkles on the crests facing the sun, breaking up over
        // choppy water. Rougher where the surface turns a lot within a pixel, so it's a
        // sheen instead of single-pixel sparkles.
        vec3 L = normalize(uSunDir);
        vec3 H = normalize(L + V);
        float NoL = max(dot(N, L), 0.0), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
        float wobble = clamp(length(fwidth(N)) * 4.0, 0.0, 1.0);
        float a = mix(0.035, 0.22, wobble), a2 = a * a;
        float dd = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (3.14159 * dd * dd);
        float k = a * 0.5;
        float G = (cosV / (cosV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
        float Fs = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
        vec3 spec = sunRad * sun * min(D * G * Fs / (4.0 * max(cosV, 0.05) * max(NoL, 1e-3) + 1e-4) * NoL, 80.0) * 3.14159;
        float fres = f0 + (1.0 - f0) * pow(1.0 - cosV, 5.0);
        col = mix(through, mix(sky, seen, found), fres) + spec;
        // Foam: a lacy band where the water meets the shore, on breaking crests and
        // where the surface was churned up.
        float lace = laceAt(wp.xz, flow);
        float foam = (1.0 - smoothstep(0.02, 0.25, path)) * smoothstep(0.4, 0.7, lace);
        foam = max(foam, smoothstep(0.62, 0.85, vCrest) * smoothstep(0.5, 0.8, lace) * 0.8);
        foam = max(foam, clamp(vFoam, 0.0, 1.0) * smoothstep(0.3, 0.6, lace));
        // White water where the current is squeezed faster past a rock, or turned.
        float baseSpeed = length(uFlowBase);
        if (baseSpeed > 0.1) {
            float bent = length(flow - uFlowBase) / baseSpeed;
            foam = max(foam, smoothstep(0.45, 1.1, bent) * smoothstep(0.45, 0.8, lace) * 0.85);
        }
        col = mix(col, (uAmbient * 1.4 + sunRad * 0.6 * sun) * 0.95, clamp(foam, 0.0, 1.0) * 0.85);
        // Right at the waterline it fades into what's behind (no hard edge).
        col = mix(texture(uScene, uv).rgb, col, smoothstep(0.0, 0.06, path));
    } else {
        // From underwater: the world above is bent through the surface, and past the
        // critical angle the surface is a mirror of the deep (total internal reflection).
        vec3 T = refract(-V, N, kIor);
        float fres = f0 + (1.0 - f0) * pow(1.0 - cosV, 5.0);
        if (dot(T, T) < 1e-4) col = inscatter;
        else {
            vec3 above = texture(uScene, toScreen(wp + T * 6.0)).rgb;
            col = mix(above, inscatter, fres);
        }
    }
    if (uSelected) col = mix(col, vec3(2.0, 0.9, 0.2), pow(1.0 - cosV, 3.0) * 0.8);
    if (uFogEnabled) {
        float f = 1.0 - exp(-uFogDensity * eyeDist);
        vec3 fog = lin(uFogColor);
        float toward = pow(max(dot(-V, normalize(uSunDir)), 0.0), 8.0);
        fog += lin(uSunColor) * uSunIntensity * toward * uFogSunGlow * 0.6;
        col = mix(col, fog, clamp(f, 0.0, 1.0));
    }
    if (uRenderDist > 0.0) {
        float edge = smoothstep(uRenderDist * 0.8, uRenderDist, eyeDist);
        col = mix(col, lin(skyGradient(-V, uZenith, uHorizon, uGround)) * uSkyBrightness, edge);
    }
    FragColor = vec4(col, 1.0);
}
)";

// Phones without float pictures: each drop drawn as a shiny little ball.
inline const char* fluidSimpleFrag = R"(#version 410 core
in vec3  vCenter;
in float vSpeed;
in float vRadius;
in vec3  vColor;
in vec3  vAxis;
in float vFlat;
in float vLong;
uniform mat4  uProj;
uniform vec3  uLightDir;     // (view space)
uniform vec3  uAmbient;
out vec4 FragColor;
void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    c.y = -c.y;
    float r2 = dot(c, c);
    if (r2 > 1.0) discard;
    vec3 n = vec3(c, sqrt(1.0 - r2));
    vec3 pos = vCenter + n * vRadius;
    vec4 clip = uProj * vec4(pos, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    float diff = max(dot(n, uLightDir), 0.0);
    float spec = pow(max(dot(reflect(-uLightDir, n), vec3(0, 0, 1)), 0.0), 60.0);
    FragColor = vec4(pow(vColor, vec3(2.2)) * (uAmbient + diff) + vec3(spec), 0.75);
}
)";

} // namespace Shaders
