#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

Camera::Camera() = default;

void Camera::orbit(float dx, float dy) {
    yaw   += dx * 0.35f;
    pitch  = std::clamp(pitch + dy * 0.35f, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy) {
    float speed = distance * 0.0012f;
    glm::mat4 v = view();
    glm::vec3 right = {v[0][0], v[1][0], v[2][0]};
    glm::vec3 up    = {v[0][1], v[1][1], v[2][1]};
    pivot -= right * (dx * speed);
    pivot += up    * (dy * speed);
}

void Camera::zoom(float delta) {
    distance = std::max(0.3f, distance - delta * (distance * 0.15f));
}

void Camera::look(float dx, float dy) {
    glm::vec3 eye = position();
    yaw   += dx * 0.3f;
    pitch  = std::clamp(pitch + dy * 0.3f, -89.0f, 89.0f);
    // Keep the eye where it is: move the point we orbit around instead.
    glm::vec3 back = position() - pivot;
    pivot = eye - back;
}

void Camera::fly(float forward, float right, float up) {
    glm::vec3 eye = position();
    glm::vec3 fwd = glm::normalize(pivot - eye);
    glm::vec3 side = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    pivot += fwd * forward + side * right + glm::vec3(0, 1, 0) * up;
}

void Camera::resize(int w, int h) { m_w = w; m_h = h; }

glm::vec3 Camera::position() const {
    if (useCFrame) return glm::vec3(cframe[3]);
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    const float d = shownDistance();
    return pivot + glm::vec3(
        d * std::cos(pr) * std::cos(yr),
        d * std::sin(pr),
        d * std::cos(pr) * std::sin(yr)
    );
}

glm::vec3 Camera::forward() const {
    if (useCFrame) {
        const glm::vec3 z(cframe[2]);
        const float len = glm::length(z);
        return len > 1e-6f ? -z / len : glm::vec3(0, 0, -1);
    }
    float yr = glm::radians(yaw), pr = glm::radians(pitch);
    return -glm::vec3(std::cos(pr) * std::cos(yr), std::sin(pr), std::cos(pr) * std::sin(yr));
}

glm::mat4 Camera::view() const {
    if (useCFrame) {   // a script's CFrame: tilted (rolled) views work too
        glm::mat4 m = cframe;
        for (int i = 0; i < 3; ++i) {   // (no scale or shear, whatever it was built from)
            glm::vec3 c(m[i]);
            m[i] = glm::vec4(glm::length(c) > 1e-6f ? glm::normalize(c) : glm::vec3(i == 0, i == 1, i == 2), 0.0f);
        }
        return glm::inverse(m);
    }
    // Looks along the orbit direction, so it also works at distance 0 (first person).
    glm::vec3 eye = position();
    return glm::lookAt(eye, eye + forward(), glm::vec3(0,1,0));
}

float Camera::viewHeight() const {
    if (orthoSize > 0.0f) return orthoSize;
    return std::max(shownDistance(), 0.5f) * 2.0f * std::tan(glm::radians(fov) * 0.5f);
}

glm::mat4 Camera::projection() const {
    float aspect = (m_h > 0) ? (float)m_w / (float)m_h : 1.0f;
    if (orthographic) {
        // The near side reaches back behind the camera too, so nothing between it and
        // what it looks at gets cut off (with no perspective, nearer isn't bigger).
        const float h = viewHeight() * 0.5f, w = h * aspect;
        return glm::ortho(-w, w, -h, h, -500.0f, 1000.0f);
    }
    return glm::perspective(glm::radians(fov), aspect, 0.1f, 1000.0f);
}
