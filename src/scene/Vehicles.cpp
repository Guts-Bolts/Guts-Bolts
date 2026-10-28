#include "Vehicles.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

float numAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}

// The vehicle a seat drives: the Model it's in, or else the loose part it's built on.
SceneNode* vehicleOf(SceneNode* seat) {
    for (SceneNode* p = seat->parent; p && p->parent; p = p->parent)
        if (p->kind == NodeKind::Model) return p;
    SceneNode* top = seat;
    for (SceneNode* p = seat->parent; p && p->isPart() && !p->anchored; p = p->parent) top = p;
    return top;
}

bool movesByItself(const SceneNode* n) {   // an unanchored part that isn't carried by another
    if (!n->isPart() || n->anchored) return false;
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return false;
    return true;
}

glm::vec3 axisOf(const SceneNode* n, int i) {
    const glm::vec3 a(n->worldMatrix()[i]);
    const float l = glm::length(a);
    return l > 1e-6f ? a / l : glm::vec3(i == 0, i == 1, i == 2);
}

float volumeOf(const SceneNode* n) {
    const glm::vec3 s = n->transform.scale;
    return std::max(1e-4f, s.x * s.y * s.z);
}

} // namespace

bool Vehicles::isVehicleSeat(const SceneNode* n) {
    if (!n || !n->isPart()) return false;
    if (n->name == "VehicleSeat" || std::find(n->tags.begin(), n->tags.end(), "VehicleSeat") != n->tags.end()) return true;
    const Attribute* a = n->findAttribute("VehicleSeat");
    return a && a->type == Attribute::Bool && a->b;
}
float Vehicles::maxSpeed(const SceneNode* seat)  { return std::max(0.0f, numAttr(seat, "MaxSpeed", kMaxSpeed)); }
float Vehicles::torque(const SceneNode* seat)    { return std::max(0.0f, numAttr(seat, "Torque", kTorque)); }
float Vehicles::turnSpeed(const SceneNode* seat) { return std::max(0.0f, numAttr(seat, "TurnSpeed", kTurnSpeed)); }

void Vehicles::drive(uint64_t seatId, uint64_t driver, float throttle, float steer) {
    Input& in = m_seats[seatId];
    in.driver = driver;
    in.throttle = std::clamp(throttle, -1.0f, 1.0f);
    in.steer = std::clamp(steer, -1.0f, 1.0f);
    in.at = m_now;
}

void Vehicles::scriptSet(uint64_t seatId, bool throttle, float value) {
    Input& in = m_seats[seatId];
    (throttle ? in.scriptThrottle : in.scriptSteer) = std::clamp(value, -1.0f, 1.0f);
}

float Vehicles::throttle(uint64_t seatId) const {
    auto it = m_seats.find(seatId);
    if (it == m_seats.end()) return 0.0f;
    return live(it->second) ? it->second.throttle : it->second.scriptThrottle;
}
float Vehicles::steer(uint64_t seatId) const {
    auto it = m_seats.find(seatId);
    if (it == m_seats.end()) return 0.0f;
    return live(it->second) ? it->second.steer : it->second.scriptSteer;
}
uint64_t Vehicles::driver(uint64_t seatId) const {
    auto it = m_seats.find(seatId);
    return it != m_seats.end() && live(it->second) ? it->second.driver : 0;
}

void Vehicles::update(Scene& scene, float dt, bool host) {
    m_now += dt;
    if (!host || dt <= 0.0f) return;
    for (auto it = m_seats.begin(); it != m_seats.end();) {
        Input& in = it->second;
        SceneNode* seat = scene.findById(it->first);
        if (!seat || !isVehicleSeat(seat)) { it = m_seats.erase(it); continue; }
        const bool occupied = live(in);
        const float th = occupied ? in.throttle : in.scriptThrottle, st = occupied ? in.steer : in.scriptSteer;
        const bool driving = occupied || th != 0.0f || st != 0.0f;
        if (!driving && !in.wasDriving) {
            if (!in.driver && in.scriptThrottle == 0.0f && in.scriptSteer == 0.0f) { it = m_seats.erase(it); continue; }
            ++it;
            continue;
        }
        in.wasDriving = driving;

        SceneNode* vehicle = vehicleOf(seat);
        const glm::vec3 seatPos(seat->worldMatrix()[3]);
        glm::vec3 right = axisOf(seat, 0), fwd = -axisOf(seat, 2);   // (a seat faces its Front, -Z)
        const float top = maxSpeed(seat), turn = turnSpeed(seat);
        std::vector<SceneNode*> motors, parts;
        std::vector<SceneNode*> stack{vehicle};
        while (!stack.empty()) {
            SceneNode* n = stack.back();
            stack.pop_back();
            if (n->isConstraint() && n->enabled && n->constraintType == ConstraintType::Hinge && n->motorTorque > 0.0f) motors.push_back(n);
            if (movesByItself(n)) parts.push_back(n);
            for (auto& c : n->children) stack.push_back(c.get());
        }

        if (!motors.empty()) {
            // Wheels: roll forward at the throttle's speed; the two sides differ to turn.
            for (SceneNode* m : motors) {
                SceneNode* a0 = scene.findById(m->ref0);
                SceneNode* a1 = scene.findById(m->ref1);
                if (!a0 || !a1 || !a0->parent || !a1->parent) continue;
                // The wheel is the smaller of the two parts; the spin is the second one's,
                // measured against the first's, about the first attachment's X.
                const bool firstIsWheel = volumeOf(a0->parent) < volumeOf(a1->parent);
                SceneNode* wheel = firstIsWheel ? a0->parent : a1->parent;
                const glm::vec3 axis = axisOf(a0, 0);
                const glm::vec3 s = wheel->transform.scale;
                const float radius = std::max(0.1f, 0.5f * std::max(s.x, std::max(s.y, s.z)));
                const float side = glm::dot(glm::vec3(wheel->worldMatrix()[3]) - seatPos, right);
                float speed = driving ? th * top + st * turn * top * 0.35f * (side > 0.0f ? -1.0f : 1.0f) : 0.0f;
                // Rolling forward is a spin about the vehicle's left (-right).
                float spin = -glm::dot(right, axis) * speed / radius;
                if (firstIsWheel) spin = -spin;
                m->motorSpeed = spin;
                wheel->sleepTime = 0.0f;
            }
            for (SceneNode* p : parts) p->sleepTime = 0.0f;
            ++it;
            continue;
        }
        if (!driving || parts.empty()) { ++it; continue; }

        // No wheels: push the whole thing along (a boat's propeller), and turn it.
        right.y = 0.0f; fwd.y = 0.0f;
        if (glm::length(fwd) < 1e-3f || glm::length(right) < 1e-3f) { ++it; continue; }
        right = glm::normalize(right); fwd = glm::normalize(fwd);
        float mass = 0.0f, spinY = 0.0f;
        glm::vec3 vel(0.0f), center(0.0f);
        for (SceneNode* p : parts) {
            const float m = Physics::densityOf(p) * volumeOf(p);
            mass += m;
            vel += p->velocity * m;
            spinY += p->angularVelocity.y * m;
            center += glm::vec3(p->worldMatrix()[3]) * m;
        }
        vel /= mass; spinY /= mass; center /= mass;
        const float vf = glm::dot(vel, fwd), vs = glm::dot(vel, right);
        const float accel = torque(seat) * (th != 0.0f ? 1.0f : 0.5f);   // (letting go slows it gently)
        const float dvf = std::clamp(th * top - vf, -accel * dt, accel * dt);
        const float dvs = -vs * std::min(1.0f, 3.0f * dt);                 // grips sideways, a bit
        // Steer right = turn clockwise from above. Going backwards, it turns the other way;
        // standing still, it turns slowly.
        const float pace = 0.3f + 0.7f * std::min(1.0f, std::abs(vf) / 4.0f);
        const float wantY = -st * turn * 1.5f * pace * (vf < -0.5f ? -1.0f : 1.0f);
        const float dwy = std::clamp(wantY - spinY, -6.0f * dt, 6.0f * dt);
        const glm::vec3 dv = fwd * dvf + right * dvs, dw(0.0f, dwy, 0.0f);
        for (SceneNode* p : parts) {
            p->velocity += dv + glm::cross(dw, glm::vec3(p->worldMatrix()[3]) - center);
            p->angularVelocity += dw;
            p->sleepTime = 0.0f;
        }
        ++it;
    }
}
