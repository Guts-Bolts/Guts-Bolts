#include "SprungWheels.h"
#include "LegacyJoints.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Vehicles.h"

#include <algorithm>
#include <cmath>

namespace {

float numAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number && std::isfinite(a->n) ? (float)a->n : fallback;
}
int boolAttr(const SceneNode* n, const char* name) {   // -1 = not set
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Bool ? (a->b ? 1 : 0) : -1;
}

bool namedWheel(const SceneNode* n) {
    return n->name == "Wheel" || std::find(n->tags.begin(), n->tags.end(), "Wheel") != n->tags.end();
}

bool hasAttachment(const SceneNode* n) {
    for (const auto& c : n->children)
        if (c->isAttachment()) return true;
    return false;
}

bool insideLoosePart(const SceneNode* n) {
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return true;
    return false;
}

// The Model a part belongs to (not the Workspace itself).
SceneNode* modelOf(const SceneNode* n) {
    for (SceneNode* p = n->parent; p && p->parent; p = p->parent)
        if (p->kind == NodeKind::Model) return p;
    return nullptr;
}

SceneNode* findSeat(SceneNode* model) {
    std::vector<SceneNode*> stack{model};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (Vehicles::isVehicleSeat(n)) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

// The loose part a seat rides on (the seat itself if it's loose).
SceneNode* chassisOf(SceneNode* seat) {
    SceneNode* top = seat->anchored ? nullptr : seat;
    for (SceneNode* p = seat->parent; p && p->isPart(); p = p->parent) {
        if (p->anchored) return nullptr;   // (bolted to the world: it can't drive)
        top = p;
    }
    return top;
}

glm::vec3 unit(const glm::vec3& v, const glm::vec3& fallback) {
    const float l = glm::length(v);
    return l > 1e-6f ? v / l : fallback;
}

// Which way a part's surface faces at a point on it (a ball: out from the middle; anything
// else: the side of its box the point is on).
glm::vec3 surfaceNormal(const SceneNode* part, const glm::vec3& p) {
    const glm::mat4 m = part->worldMatrix();
    if (part->primitiveType == PrimitiveType::Sphere) return unit(p - glm::vec3(m[3]), {0, 1, 0});
    const glm::vec3 local = glm::vec3(glm::inverse(m) * glm::vec4(p, 1.0f));   // (the box is -0.5..0.5 here)
    int best = 1;
    for (int i = 0; i < 3; ++i)
        if (std::abs(local[i]) > std::abs(local[best]) + 1e-4f) best = i;
    return unit(glm::vec3(m[best]) * (local[best] < 0.0f ? -1.0f : 1.0f), {0, 1, 0});
}

// How much a surface lets a tyre grip: 1 on plastic (most baseplates), more on concrete
// or grass, a lot less on ice.
float gripOn(float friction) { return std::clamp(std::sqrt(std::max(0.0f, friction) / 0.3f), 0.1f, 1.6f); }

// How heavy the chassis feels when pushed along `d` at `r` from its middle.
float pushMass(const SprungWheels::Body& b, const glm::vec3& r, const glm::vec3& d) {
    const float k = b.invMass + glm::dot(d, glm::cross(b.invInertia * glm::cross(r, d), r));
    return k > 1e-9f ? 1.0f / k : 0.0f;
}

void push(SprungWheels::Body& b, const glm::vec3& r, const glm::vec3& j) {
    b.v += j * b.invMass;
    b.w += b.invInertia * glm::cross(r, j);
}

} // namespace

bool SprungWheels::isWheel(Scene& scene, const SceneNode* n) {
    (void)scene;
    if (!n->isPart() || n->anchored || !namedWheel(n) || hasAttachment(n) || insideLoosePart(n)) return false;
    SceneNode* model = modelOf(n);
    if (!model) return false;
    auto it = m_seatOfModel.find(model);
    if (it == m_seatOfModel.end()) it = m_seatOfModel.emplace(model, findSeat(model)).first;
    SceneNode* seat = it->second;
    if (!seat || seat == n || !chassisOf(seat) || chassisOf(seat) == n) return false;
    m_seen.push_back(const_cast<SceneNode*>(n));
    return true;
}

bool SprungWheels::hasWheels(const SceneNode* vehicle) {
    std::vector<const SceneNode*> stack{vehicle};
    while (!stack.empty()) {
        const SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isPart() && !n->anchored && namedWheel(n) && !hasAttachment(n) && !insideLoosePart(n)) return true;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return false;
}

float SprungWheels::wheelRadius(const SceneNode* wheel, const glm::vec3& up) {
    const glm::mat4 m = wheel->worldMatrix();
    float r = 0.0f;   // how far it reaches below its middle
    for (int i = 0; i < 3; ++i) r += std::abs(glm::dot(glm::vec3(m[i]), up)) * 0.5f;
    if (wheel->primitiveType == PrimitiveType::Sphere) {
        r = 0.0f;
        for (int i = 0; i < 3; ++i) r += glm::length(glm::vec3(m[i])) / 6.0f;   // (half the average size)
    }
    return std::max(0.1f, r);
}

float SprungWheels::springRate(const Car& car, float mass, float gravity) {
    // Strong enough that, with Stiffness 1, each wheel's spring holds up its share of the
    // car halfway through its travel: so it rests the way it was built.
    const float n = (float)std::max<size_t>(1, car.wheels.size());
    const float sag = std::max(0.05f, car.set.travel * 0.5f);
    return std::max(0.05f, car.set.stiffness) * (mass * std::max(1.0f, gravity) / n) / sag;
}

std::vector<SprungWheels::Car*>& SprungWheels::prepare(Scene& scene, float dt) {
    (void)dt;
    m_list.clear();
    // Group this frame's wheels by their seat.
    std::unordered_set<uint64_t> live;
    std::unordered_map<uint64_t, Wheel> before;   // (what each wheel was doing last frame)
    for (auto& [id, car] : m_cars)
        for (const Wheel& wh : car.wheels) before[wh.id] = wh;
    for (SceneNode* w : m_seen) {
        SceneNode* seat = m_seatOfModel[modelOf(w)];
        SceneNode* chassis = seat ? chassisOf(seat) : nullptr;
        if (!seat || !chassis) continue;
        Car& car = m_cars[seat->id];
        if (!live.count(seat->id)) {
            live.insert(seat->id);
            if (car.chassis != chassis) { car = Car(); }   // (new, or rebuilt)
            car.chassis = chassis;
            car.seat = seat;
            car.model = modelOf(w);
            car.wheels.clear();
        }
        Wheel wh;
        if (auto b = before.find(w->id); b != before.end()) wh = b->second;   // (a parked car sleeps: it keeps how it was)
        wh.node = w;
        wh.id = w->id;
        car.wheels.push_back(wh);
    }
    m_seen.clear();
    m_seatOfModel.clear();
    for (auto it = m_cars.begin(); it != m_cars.end();) it = live.count(it->first) ? std::next(it) : m_cars.erase(it);

    const Vehicles& vehicles = scene.vehicles();
    for (auto& [seatId, car] : m_cars) {
        car.simulated = false;
        SceneNode* seat = car.seat;
        const glm::mat4 frame = LegacyJoints::frameOf(car.chassis);
        const glm::mat4 toChassis = glm::inverse(frame);
        const glm::mat3 rot(frame);
        const glm::vec3 up = rot[1];
        // Which way the seat faces, in the chassis's frame (a seat faces its Front, -Z).
        const glm::mat4 seatFrame = LegacyJoints::frameOf(seat);
        glm::vec3 fwd = glm::transpose(rot) * -glm::vec3(seatFrame[2]);
        fwd.y = 0.0f;
        car.fwdLocal = unit(fwd, {0, 0, -1});
        car.rightLocal = glm::cross(car.fwdLocal, glm::vec3(0, 1, 0));

        Settings& s = car.set;
        const Settings was = s;
        s.maxSpeed   = Vehicles::maxSpeed(seat);
        s.torque     = Vehicles::torque(seat);
        s.turnSpeed  = Vehicles::turnSpeed(seat);
        s.grip       = std::clamp(numAttr(seat, "Grip", 1.0f), 0.0f, 5.0f);
        s.suspension = std::clamp(numAttr(seat, "Suspension", 0.5f), 0.0f, 2.0f);
        s.stiffness  = std::clamp(numAttr(seat, "Stiffness", 1.0f), 0.1f, 10.0f);
        s.travel     = std::clamp(numAttr(seat, "SpringTravel", 1.0f), 0.1f, 10.0f);
        // Changed the springs (a script, say)? A parked car that's dozed off wakes up to feel it.
        if (s.stiffness != was.stiffness || s.travel != was.travel || s.suspension != was.suspension) car.chassis->sleepTime = 0.0f;
        car.throttle = vehicles.throttle(seatId);
        car.steer    = vehicles.steer(seatId);
        car.parked   = vehicles.driver(seatId) == 0 && car.throttle == 0.0f && car.steer == 0.0f;

        // Each wheel: how it was built (the first time we see it), and the ground under it.
        float frontMost = -1e9f, backMost = 1e9f;
        for (Wheel& wh : car.wheels) {
            Rest& r = m_rest[wh.id];
            if (r.chassis != car.chassis->id) {
                r.chassis = car.chassis->id;
                r.rest = toChassis * LegacyJoints::frameOf(wh.node);
                r.spin = 0.0f;
                r.radius = wheelRadius(wh.node, up);   // (as built: once it rolls, its box turns)
            }
            wh.rest = r.rest;
            wh.spin = r.spin;
            wh.radius = r.radius;
            const float ahead = glm::dot(glm::vec3(wh.rest[3]), car.fwdLocal);
            frontMost = std::max(frontMost, ahead);
            backMost = std::min(backMost, ahead);
        }
        for (Wheel& wh : car.wheels) {
            const float ahead = glm::dot(glm::vec3(wh.rest[3]), car.fwdLocal);
            const int steer = boolAttr(wh.node, "Steer"), drive = boolAttr(wh.node, "Drive");
            // The front axle steers (when there's more than one axle).
            wh.steers = steer >= 0 ? steer == 1 : frontMost - backMost > 0.5f && ahead > frontMost - 0.25f * (frontMost - backMost);
            wh.drives = drive >= 0 ? drive == 1 : true;

            // Look down from the top of the spring, past the bottom of the tyre.
            const glm::vec3 top = glm::vec3(frame * glm::vec4(glm::vec3(wh.rest[3]) + glm::vec3(0, s.travel * 0.5f, 0), 1.0f));
            const SceneNode* model = car.model;
            auto counts = [&scene, model](const SceneNode* n) {
                if (n == model || scene.isCharacterPart(n)) return false;
                return !n->isPart() || n->canCollide;
            };
            float dist = 0.0f;
            const float reach = s.travel + wh.radius + 0.5f;
            SceneNode* hit = Physics::raycastIf(scene, top, -up, &dist, counts);
            wh.ground = hit && dist <= reach;
            if (!wh.ground) continue;
            wh.groundPoint = top - up * dist;
            if (hit == scene.terrainNode()) {
                // The terrain: its slope from two more looks, a little ahead and to the side.
                const glm::vec3 a = glm::vec3(rot * car.fwdLocal) * 0.4f, b = glm::vec3(rot * car.rightLocal) * 0.4f;
                float da = 0.0f, db = 0.0f;
                const bool ha = Physics::raycastIf(scene, top + a, -up, &da, counts) == hit && da < reach + 1.0f;
                const bool hb = Physics::raycastIf(scene, top + b, -up, &db, counts) == hit && db < reach + 1.0f;
                wh.groundNormal = up;
                if (ha && hb) {
                    const glm::vec3 pa = top + a - up * da, pb = top + b - up * db;
                    wh.groundNormal = unit(glm::cross(pb - wh.groundPoint, pa - wh.groundPoint), up);
                    if (glm::dot(wh.groundNormal, up) < 0.0f) wh.groundNormal = -wh.groundNormal;
                }
                wh.groundGrip = gripOn(0.5f);
            } else {
                wh.groundNormal = surfaceNormal(hit, wh.groundPoint);
                wh.groundGrip = gripOn(Physics::frictionOf(hit));
            }
        }
        m_list.push_back(&car);
    }
    return m_list;
}

void SprungWheels::step(Car& car, Body& b, float h, float gravity) {
    if (car.wheels.empty() || b.invMass <= 0.0f || h <= 0.0f) return;
    const float mass = 1.0f / b.invMass;
    const float n = (float)car.wheels.size();
    const Settings& s = car.set;
    const glm::mat3 rot = glm::mat3_cast(b.rot);
    const glm::vec3 up = rot[1];
    const glm::vec3 fwd = rot * car.fwdLocal, right = rot * car.rightLocal;
    const float k = springRate(car, mass, gravity);
    // Damping: Suspension 0.5 lets it bob once or twice, 0 lets it bounce, 2 stops it dead.
    const float zeta = 0.12f + 0.5f * s.suspension;
    const float c = 2.0f * zeta * std::sqrt(k * mass / n);
    const float carSpeed = glm::dot(b.v, fwd);
    car.speed = carSpeed;

    // Steering: the wheels turn towards where the driver points them (less at speed, so
    // it doesn't flip over), at a wheel-turning pace.
    const float most = glm::radians(std::clamp(32.0f * s.turnSpeed, 5.0f, 50.0f)) / (1.0f + std::abs(carSpeed) / 30.0f);
    const float want = std::clamp(car.steer, -1.0f, 1.0f) * most;
    car.steerAngle += std::clamp(want - car.steerAngle, -3.0f * h, 3.0f * h);

    int driven = 0;
    for (const Wheel& wh : car.wheels) driven += wh.drives ? 1 : 0;
    const float th = std::clamp(car.throttle, -1.0f, 1.0f);
    // Pressing against the way it's going brakes (then reverses, once it's stopped).
    const bool braking = th != 0.0f && carSpeed * th < -0.5f;
    // Reaching the top speed (half that backwards), the engine eases off.
    const float cap = th > 0.0f ? s.maxSpeed * th : s.maxSpeed * 0.5f * -th;
    const float ease = cap <= 0.0f ? 0.0f : std::clamp((cap - carSpeed * (th > 0.0f ? 1.0f : -1.0f)) / std::max(0.5f, cap * 0.15f), 0.0f, 1.0f);

    car.onGround = 0;
    for (Wheel& wh : car.wheels) {
        wh.touching = wh.slipping = false;
        wh.load = 0.0f;
        const glm::vec3 top = b.pos + rot * (glm::vec3(wh.rest[3]) + glm::vec3(0, s.travel * 0.5f, 0));
        const glm::vec3 N = wh.groundNormal;
        const float cosA = glm::dot(up, N);
        if (!wh.ground || cosA < 0.3f) { wh.compression = 0.0f; continue; }
        // How far down the spring the wheel's middle is, sitting on the ground.
        const float centre = (glm::dot(top - wh.groundPoint, N) - wh.radius) / cosA;
        const float x = s.travel - centre;   // how squashed the spring is
        if (x <= 0.0f) { wh.compression = 0.0f; continue; }   // (hanging in the air)
        wh.compression = std::min(x, s.travel);
        wh.touching = true;
        ++car.onGround;
        const glm::vec3 contact = top - up * centre - N * wh.radius;
        const glm::vec3 r = contact - b.pos;

        // The spring (and damper): squashed, it pushes the chassis up; squashing fast, more.
        glm::vec3 vc = b.v + glm::cross(b.w, r);
        float force = k * wh.compression - c * glm::dot(vc, N);
        if (x > s.travel) force += 8.0f * k * (x - s.travel);   // (bottomed out: the bump stop)
        force = std::clamp(force, 0.0f, mass * std::max(1.0f, gravity) * 8.0f / n);
        wh.load = force;
        push(b, r, N * (force * h));

        // The tyre, along the ground: which way it points (steered), and sideways.
        glm::vec3 heading = fwd;
        if (wh.steers) heading = fwd * std::cos(car.steerAngle) + right * std::sin(car.steerAngle);
        const glm::vec3 f = unit(heading - N * glm::dot(heading, N), fwd);
        const glm::vec3 side = unit(glm::cross(f, N), right);
        vc = b.v + glm::cross(b.w, r);
        const float vf = glm::dot(vc, f), vs = glm::dot(vc, side);
        wh.rollSpeed = vf;

        // Sideways: the tyre stops it sliding (all the grip it has).
        const float jSide = -vs * pushMass(b, r, side) * 0.9f;
        // Along: the engine, the brakes, or just rolling.
        float jAlong = 0.0f;
        const float share = mass / n;
        if (braking) {
            const float most = 2.5f * s.torque * share * h;
            jAlong = std::clamp(-vf * pushMass(b, r, f), -most, most);
        } else if (th != 0.0f) {
            if (wh.drives && driven > 0) jAlong = th * s.torque * (mass / driven) * ease * h;
        } else if (car.parked) {
            jAlong = -vf * pushMass(b, r, f) * 0.9f;   // (the handbrake: parked cars don't roll off)
        } else {
            const float most = 0.25f * s.torque * share * h;   // (letting go: it slows gently)
            jAlong = std::clamp(-vf * pushMass(b, r, f), -most, most);
        }
        // A tyre can only push as hard as it's pressed onto the ground (the friction circle):
        // ask for more and it slides (a drift, a wheelspin, a skid).
        glm::vec3 j = side * jSide + f * jAlong;
        const float limit = s.grip * wh.groundGrip * 1.1f * force * h;
        const float len = glm::length(j);
        if (len > limit) {
            j *= limit / std::max(len, 1e-9f);
            wh.slipping = len > limit * 1.15f;
        }
        push(b, r, j);
    }
    car.simulated = true;
}

void SprungWheels::pose(float dt) {
    for (auto& [seatId, car] : m_cars) {
        if (!car.simulated || !car.chassis) continue;
        const glm::mat4 frame = LegacyJoints::frameOf(car.chassis);
        const glm::vec3 up(0, 1, 0);
        const glm::vec3 axle = -car.rightLocal;   // (rolling forward turns a wheel this way round)
        for (Wheel& wh : car.wheels) {
            // Rolling: as fast as it goes along the ground; spinning faster when the engine
            // spins it on a slippery ground.
            float roll = wh.rollSpeed;
            if (wh.slipping && wh.drives && car.throttle != 0.0f) roll += car.throttle * 8.0f;
            if (!wh.touching) roll *= std::max(0.0f, 1.0f - 1.5f * dt);   // (in the air it slows down)
            wh.rollSpeed = roll;
            wh.spin = std::fmod(wh.spin + roll / std::max(0.1f, wh.radius) * dt, 6.2831853f);
            m_rest[wh.id].spin = wh.spin;
            // Up and down the spring (as built = halfway), turned by the steering and the roll.
            glm::mat4 local = wh.rest;
            local[3] += glm::vec4(up * (wh.compression - car.set.travel * 0.5f), 0.0f);
            const glm::mat3 restTurn(wh.rest);
            glm::quat turn = glm::angleAxis(wh.spin, axle);
            if (wh.steers) turn = glm::angleAxis(-car.steerAngle, up) * turn;
            const glm::mat3 r = glm::mat3_cast(turn) * restTurn;
            for (int i = 0; i < 3; ++i) local[i] = glm::vec4(r[i], 0.0f);
            LegacyJoints::setFrame(wh.node, frame * local);
            wh.node->velocity = glm::vec3(0.0f);
            wh.node->angularVelocity = glm::vec3(0.0f);
        }
    }
}

const SprungWheels::Car* SprungWheels::carOf(uint64_t seatId) const {
    auto it = m_cars.find(seatId);
    return it == m_cars.end() ? nullptr : &it->second;
}
