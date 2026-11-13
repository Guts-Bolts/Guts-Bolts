#pragma once
#include "Mesh.h"
#include <memory>
#include <cmath>

namespace Primitives {

inline std::shared_ptr<Mesh> createCube() {
    std::vector<Vertex>   v;
    std::vector<uint32_t> idx;

    // Per-face vertices so each face gets its correct flat normal
    const glm::vec3 faceVerts[6][4] = {
        {{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f}}, // +X
        {{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}}, // -X
        {{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}}, // +Y
        {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}}, // -Y
        {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}}, // +Z
        {{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}}, // -Z
    };
    const glm::vec3 normals[6] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    };
    const glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};

    for (int f = 0; f < 6; ++f) {
        auto base = (uint32_t)v.size();
        for (int i = 0; i < 4; ++i)
            v.push_back({faceVerts[f][i], normals[f], uvs[i]});
        idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
    }
    return std::make_shared<Mesh>(v, idx);
}

inline std::shared_ptr<Mesh> createSphere(int stacks = 16, int slices = 32) {
    std::vector<Vertex>   v;
    std::vector<uint32_t> idx;
    constexpr float PI = 3.14159265f;

    for (int i = 0; i <= stacks; ++i) {
        float phi = PI * i / stacks;
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * PI * j / slices;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            v.push_back({{x*0.5f,y*0.5f,z*0.5f}, {x,y,z}, {(float)j/slices,(float)i/stacks}});
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            auto a = (uint32_t)(i*(slices+1)+j), b = a+slices+1;
            idx.insert(idx.end(), {a,b,a+1, b,b+1,a+1});
        }
    return std::make_shared<Mesh>(v, idx, /*lods*/ true);   // (1,024 triangles: far balls get simpler copies)
}

inline std::shared_ptr<Mesh> createPlane(int divs = 1) {
    std::vector<Vertex>   v;
    std::vector<uint32_t> idx;
    float step = 1.0f / divs;
    for (int y = 0; y <= divs; ++y)
        for (int x = 0; x <= divs; ++x)
            v.push_back({{x*step-0.5f, 0.0f, y*step-0.5f}, {0,1,0}, {x*step, y*step}});
    for (int y = 0; y < divs; ++y)
        for (int x = 0; x < divs; ++x) {
            auto i = (uint32_t)(y*(divs+1)+x);
            idx.insert(idx.end(), {i,i+(uint32_t)(divs+1),i+(uint32_t)(divs+1)+1,
                                   i,i+(uint32_t)(divs+1)+1,i+1});
        }
    return std::make_shared<Mesh>(v, idx);
}

inline std::shared_ptr<Mesh> createCylinder(int slices = 32) {
    std::vector<Vertex>   v;
    std::vector<uint32_t> idx;
    constexpr float PI = 3.14159265f;

    // Side
    for (int i = 0; i <= slices; ++i) {
        float a = 2.0f*PI*i/slices;
        float cx = std::cos(a)*0.5f, cz = std::sin(a)*0.5f;
        glm::vec3 n{std::cos(a), 0, std::sin(a)};
        v.push_back({{cx,-0.5f,cz}, n, {(float)i/slices, 0}});
        v.push_back({{cx, 0.5f,cz}, n, {(float)i/slices, 1}});
    }
    for (int i = 0; i < slices; ++i) {
        auto b = (uint32_t)(i*2);
        idx.insert(idx.end(), {b,b+2,b+1, b+1,b+2,b+3});
    }

    // Caps
    auto addCap = [&](float y, glm::vec3 n) {
        auto center = (uint32_t)v.size();
        v.push_back({{0,y,0}, n, {0.5f,0.5f}});
        for (int i = 0; i <= slices; ++i) {
            float a = 2.0f*PI*i/slices;
            float cx = std::cos(a)*0.5f, cz = std::sin(a)*0.5f;
            v.push_back({{cx,y,cz}, n, {cx+0.5f,cz+0.5f}});
        }
        for (int i = 0; i < slices; ++i) {
            auto a = center+1+(uint32_t)i, b = a+1;
            if (n.y > 0) idx.insert(idx.end(), {center,a,b});
            else         idx.insert(idx.end(), {center,b,a});
        }
    };
    addCap(-0.5f, {0,-1,0});
    addCap( 0.5f, {0, 1,0});

    return std::make_shared<Mesh>(v, idx);
}

} // namespace Primitives
