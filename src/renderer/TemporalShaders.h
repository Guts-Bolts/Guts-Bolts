#pragma once
// GLSL for the scalable lighting (see Scalability.h):
//
//  temporalFrag: the temporal upscaler (like Unreal's TSR / TAA). The scene is drawn at
//    fewer pixels, moved by a fraction of a pixel every frame (the jitter). This pass puts
//    each new frame on top of the ones before it, at the full screen size: last frame's
//    result is moved to where things are now (from the depth and both cameras), kept
//    inside the colours really around each pixel now (so nothing smears or ghosts), and
//    mixed with a little of the new frame. Over a few frames every screen pixel has been
//    seen from several spots inside it, so edges come out smooth and fine detail sharp.
//
//  ssgiFrag: bounced light traced across the screen (a small version of Unreal's
//    Lumen screen traces). From each pixel a few rays go out over the surface; where one
//    hits something else on screen, that thing's colour lights this pixel a little (light
//    bouncing off a red wall turns the floor next to it reddish), and every ray that hits
//    something nearby also means less sky reaches here (dark corners, like ambient
//    occlusion). The lit picture it reads already has last frame's bounce in it, so light
//    keeps bouncing again every frame (more bounces for free). It's kept smooth over time
//    the same way as the upscaler, and read by the lit shader in the next frame.
namespace Shaders {

inline const char* temporalFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uCur;        // this frame, at the drawing size, moved by uJitter
uniform sampler2D uDepth;      // ...and its depth
uniform sampler2D uHist;       // the result so far, at the screen size
uniform mat4  uInvViewProj;    // this frame's camera (without the jitter)
uniform mat4  uPrevViewProj;   // last frame's camera
uniform vec2  uJitter;         // how far this frame was moved, in drawing pixels
uniform vec2  uInSize;         // drawing size
uniform vec2  uOutSize;        // screen size
uniform bool  uHistOk;         // false on the first frame (or after a camera cut)
uniform float uBlend;          // how much of the new frame goes in (0.1 = a tenth)
uniform int   uQuality;        // 1 TAA, 2 TSR, 3 TSR (keeps more)
out vec4 FragColor;

vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// Mixed in a squashed range, so one very bright pixel can't flicker on and off.
vec3 squash(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 unsquash(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-3); }

// Last frame's result at uv. TSR reads it with a sharper filter (Catmull-Rom, 5 taps)
// so it doesn't get a bit blurrier every frame.
vec3 histAt(vec2 uv) {
    if (uQuality < 2) return texture(uHist, uv).rgb;
    vec2 pos = uv * uOutSize;
    vec2 c = floor(pos - 0.5) + 0.5;
    vec2 f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t12 = (c + w2 / w12) / uOutSize;
    vec2 t0 = (c - 1.0) / uOutSize;
    vec2 t3 = (c + 2.0) / uOutSize;
    vec3 r = texture(uHist, vec2(t12.x, t0.y)).rgb * (w12.x * w0.y)
           + texture(uHist, vec2(t0.x, t12.y)).rgb * (w0.x * w12.y)
           + texture(uHist, t12).rgb * (w12.x * w12.y)
           + texture(uHist, vec2(t3.x, t12.y)).rgb * (w3.x * w12.y)
           + texture(uHist, vec2(t12.x, t3.y)).rgb * (w12.x * w3.y);
    float ws = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / ws, vec3(0.0));
}

void main() {
    // Where this screen pixel lands on the drawing (in drawing pixels), and the drawing
    // pixel whose (moved) middle is nearest to it.
    vec2 p = vUV * uInSize;
    ivec2 isz = ivec2(uInSize);
    ivec2 base = ivec2(floor(p + uJitter));
    vec3 sum = vec3(0.0), m1 = vec3(0.0), m2 = vec3(0.0);
    float wsum = 0.0, wbest = 0.0, nearest = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(base + ivec2(x, y), ivec2(0), isz - 1);
            vec3 c = squash(max(texelFetch(uCur, q, 0).rgb, vec3(0.0)));
            vec2 d = vec2(q) + 0.5 - uJitter - p;
            float w = exp(-2.29 * dot(d, d));
            sum += c * w;
            wsum += w;
            wbest = max(wbest, w);
            vec3 yc = toYCoCg(c);
            m1 += yc;
            m2 += yc * yc;
            nearest = min(nearest, texelFetch(uDepth, q, 0).r);
        }
    vec3 cur = sum / max(wsum, 1e-4);

    // Where was this spot last frame? (The nearest depth around, so edges of things in
    // front move with them.)
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, nearest * 2.0 - 1.0, 1.0);
    wp /= wp.w;
    vec4 pc = uPrevViewProj * wp;
    vec2 prevUV = pc.xy / pc.w * 0.5 + 0.5;
    bool ok = uHistOk && pc.w > 0.0 && prevUV.x >= 0.0 && prevUV.y >= 0.0 && prevUV.x <= 1.0 && prevUV.y <= 1.0;

    float a = 1.0;
    vec3 hist = cur;
    if (ok) {
        // Keep the old colour inside the range of colours around this pixel now: anything
        // that moved or appeared can't leave a ghost behind.
        vec3 mean = m1 / 9.0;
        vec3 sd = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
        float gamma = uQuality >= 3 ? 1.25 : 1.0;
        vec3 center = mean, ext = sd * gamma + 1e-4;
        vec3 h = toYCoCg(squash(histAt(prevUV)));
        vec3 off = h - center;
        vec3 ts = abs(off / ext);
        float mx = max(ts.x, max(ts.y, ts.z));
        if (mx > 1.0) h = center + off / mx;
        hist = fromYCoCg(h);
        // Less of the new frame where its nearest sample is far from this screen pixel
        // (that's what fills in the detail when upscaling); more when things move fast.
        vec2 vel = (vUV - prevUV) * uOutSize;
        a = clamp(uBlend * (0.35 + 0.65 * wbest) + min(length(vel) * 0.004, 0.2), 0.02, 1.0);
    }
    vec3 res = mix(hist, cur, a);
    if (any(isnan(res)) || any(isinf(res))) res = cur;
    FragColor = vec4(unsquash(res), 1.0);
}
)";

inline const char* ssgiFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uDepth;       // this frame's depth
uniform sampler2D uColor;       // this frame's lit picture (with last frame's bounce in it)
uniform sampler2D uHist;        // the bounce worked out before (rgb light, a how much sky gets here)
uniform sampler2D uHistDepth;   // ...and how far away each of its pixels was
uniform mat4  uProj;            // this frame's camera, as the scene was drawn (with the jitter)
uniform mat4  uInvProj;
uniform mat4  uInvView;
uniform mat4  uPrevView;        // the camera of the bounce worked out before
uniform mat4  uPrevViewProj;
uniform bool  uHistOk;
uniform vec2  uTexel;           // one pixel of uDepth
uniform int   uRays;
uniform int   uSteps;
uniform float uRadius;          // how far a ray goes (studs)
uniform float uFrame;
uniform float uKeep;            // how much of the past is kept (0.8 = most)
layout(location = 0) out vec4 OutGI;
layout(location = 1) out vec4 OutDist;
#pragma gb_common
vec3 viewAt(vec2 uv, float d) {
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
vec3 viewPos(vec2 uv) { return viewAt(uv, texture(uDepth, uv).r); }
void main() {
    // (On the middle of one of the depth picture's pixels: this picture is smaller, so its
    // own middles fall between them, and the neighbours below would sometimes be the same pixel.)
    vec2 uv = (floor(vUV / uTexel) + 0.5) * uTexel;
    float depth = texture(uDepth, uv).r;
    if (depth >= 1.0) { OutGI = vec4(0.0, 0.0, 0.0, 1.0); OutDist = vec4(1e4); return; }
    vec3 P = viewPos(uv);
    // The surface's way out, from the depths next to it (the flatter side, so edges are clean).
    vec3 r = viewPos(uv + vec2(uTexel.x, 0)) - P, l = P - viewPos(uv - vec2(uTexel.x, 0));
    vec3 u = viewPos(uv + vec2(0, uTexel.y)) - P, b = P - viewPos(uv - vec2(0, uTexel.y));
    vec3 N = normalize(cross(abs(r.z) < abs(l.z) ? r : l, abs(u.z) < abs(b.z) ? u : b));
    if (dot(N, -P) < 0.0) N = -N;

    // Different rays every frame (they're averaged over time).
    float rnd  = hash12(gl_FragCoord.xy + vec2(uFrame * 17.31, uFrame * 5.13));
    float rnd2 = hash12(gl_FragCoord.yx * 1.37 + vec2(uFrame * 3.7, 11.0));
    vec3 helper = abs(N.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 T = normalize(cross(helper, N));
    vec3 B = cross(N, T);
    vec3 start = P + N * (0.05 + 0.002 * abs(P.z));

    vec3 bounce = vec3(0.0);
    float occ = 0.0;
    for (int i = 0; i < uRays; ++i) {
        // Spread over the half-sphere, more of them straight out (how light really falls).
        float u1 = fract((float(i) + rnd2) / float(uRays));
        float u2 = fract(float(i) * 0.618034 + rnd);
        float rr = sqrt(u1), ph = 6.2831853 * u2;
        vec3 d = T * (rr * cos(ph)) + B * (rr * sin(ph)) + N * sqrt(1.0 - u1);
        for (int s = 0; s < uSteps; ++s) {
            float k = (float(s) + 0.5 + rnd * 0.5) / float(uSteps);
            float t = uRadius * k * k;
            vec3 sp = start + d * t;
            vec4 o = uProj * vec4(sp, 1.0);
            if (o.w <= 0.0) break;
            vec2 suv = o.xy / o.w * 0.5 + 0.5;
            if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) break;   // left the screen
            float sd = texture(uDepth, suv).r;
            if (sd >= 1.0) continue;
            float diff = viewAt(suv, sd).z - sp.z;   // > 0: something on screen is in front of the ray here
            if (diff > 0.0 && diff < 0.6 + 0.05 * abs(sp.z)) {
                float near = 1.0 - k * k * 0.6;     // close things count more
                bounce += min(texture(uColor, suv).rgb, vec3(6.0)) * near;
                occ += near;
                break;
            }
        }
    }
    vec4 cur = vec4(bounce / float(uRays), 1.0 - occ / float(uRays));

    // Mix with the bounce worked out before, where it saw the same spot.
    if (uHistOk) {
        vec4 W = uInvView * vec4(P, 1.0);
        vec4 pc = uPrevViewProj * W;
        vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
        if (pc.w > 0.0 && puv.x >= 0.0 && puv.y >= 0.0 && puv.x <= 1.0 && puv.y <= 1.0) {
            float expect = -(uPrevView * W).z;
            float got = texture(uHistDepth, puv).r;
            if (abs(got - expect) < 0.08 * expect + 0.1) cur = mix(cur, texture(uHist, puv), uKeep);
        }
    }
    if (any(isnan(cur))) cur = vec4(0.0, 0.0, 0.0, 1.0);
    OutGI = cur;
    OutDist = vec4(-P.z);
}
)";

} // namespace Shaders
