#pragma once
// Cars with real suspension: wheels on springs.
//
// How to build one (no script needed):
//   * a Model with a VehicleSeat in it (Vehicles.h), on an unanchored body part (the chassis:
//     the seat sits inside it, or is it),
//   * and parts called "Wheel" (or tagged "Wheel") in the same Model, loose (unanchored),
//     NOT joined to anything (no hinges: hinge wheels are the old tank-style cars).
// Press Play: each wheel hangs from the chassis on its own spring. The car rests the way
// you built it, the springs squash going over bumps and stretch over dips, the wheels
// steer (the front ones), spin, and grip the ground until pushed too hard (then it slides).
//
// Each wheel is a "ray": every frame we look straight down from where it's mounted to find
// the ground, and every physics step the spring pushes the chassis up there, with the tyre
// pushing it along (the engine), holding it from sliding sideways (grip) and braking.
// The wheels themselves aren't physics parts: they're moved to where the spring puts them.
//
// Settings are attributes on the VehicleSeat (Lua: seat.Stiffness = 2, or SetAttribute):
//   MaxSpeed (12), Torque (10), TurnSpeed (1)   as for every VehicleSeat
//   Grip         (1)   how hard the tyres hold before sliding (0.3 = ice, 2 = racing tyres)
//   Suspension (0.5)   how much the springs soak up bounces (0 = bouncy, 2 = stiff and dead)
//   Stiffness    (1)   how strong the springs are (0.5 = soft, sits low; 2 = firm, sits high)
//   SpringTravel (1)   how far (studs) each wheel can move up and down
// And on a wheel (optional): Steer (true/false) and Drive (true/false). Without them the
// front wheels steer and every wheel drives.
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Scene;
class SceneNode;

class SprungWheels {
public:
    // The chassis, as the physics sees it during a step (world space).
    struct Body {
        glm::vec3 pos{0.0f};
        glm::quat rot{1, 0, 0, 0};
        glm::vec3 v{0.0f}, w{0.0f};
        float     invMass = 0.0f;
        glm::mat3 invInertia{0.0f};   // world space
    };
    // What a car's seat asks for, and how it's set up.
    struct Settings {
        float maxSpeed = 12.0f, torque = 10.0f, turnSpeed = 1.0f;
        float grip = 1.0f, suspension = 0.5f, stiffness = 1.0f, travel = 1.0f;
    };
    struct Wheel {
        SceneNode* node = nullptr;
        uint64_t   id = 0;
        glm::mat4  rest{1.0f};        // its place in the chassis's frame, as built
        float      radius = 0.5f;
        bool       steers = false, drives = true;
        // The ground under it this frame (found once a frame, used every physics step).
        bool       ground = false;
        glm::vec3  groundPoint{0.0f}, groundNormal{0, 1, 0};
        float      groundGrip = 1.0f;    // the ground's friction, as a grip multiplier
        // What the spring and tyre did on the last step.
        float      compression = 0.0f;   // 0 (hanging) .. travel (pushed right up)
        float      load = 0.0f;          // how hard the spring pushed (studs/s^2 x mass)
        bool       touching = false, slipping = false;
        float      rollSpeed = 0.0f;     // studs/s along the ground
        float      spin = 0.0f;          // how far it has turned (radians), for drawing
    };
    struct Car {
        SceneNode* chassis = nullptr;
        SceneNode* seat = nullptr;
        SceneNode* model = nullptr;
        std::vector<Wheel> wheels;
        Settings  set;
        glm::vec3 fwdLocal{0, 0, -1}, rightLocal{1, 0, 0};   // the seat's facing, in the chassis's frame
        float     throttle = 0.0f, steer = 0.0f;
        bool      parked = true;          // nobody driving: the handbrake is on
        float     steerAngle = 0.0f;      // radians, + = right
        bool      simulated = false;      // stepped this frame (so its wheels get moved)
        float     speed = 0.0f;           // forward, studs/s
        int       onGround = 0;           // wheels touching
    };

    // A part that's a sprung wheel (see above). The physics leaves these alone.
    bool isWheel(Scene& scene, const SceneNode* n);
    // Does this vehicle Model have sprung wheels? (the VehicleSeat then doesn't push it like a boat)
    static bool hasWheels(const SceneNode* vehicle);

    // Once a frame, before the physics: find the cars (from the wheels isWheel said yes to
    // since the last call), read their seats, and look for the ground under every wheel.
    std::vector<Car*>& prepare(Scene& scene, float dt);
    // One physics step (h seconds) of a car: springs and tyres push the chassis.
    static void step(Car& car, Body& body, float h, float gravity);
    // After the physics: put the wheels where their springs are (and turn them).
    void pose(float dt);

    const Car* carOf(uint64_t seatId) const;
    void clear() { m_cars.clear(); m_rest.clear(); m_seen.clear(); m_seatOfModel.clear(); m_list.clear(); }

    // The maths, out in the open for the tests.
    static float springRate(const Car& car, float mass, float gravity);   // per wheel
    static float wheelRadius(const SceneNode* wheel, const glm::vec3& up);

private:
    struct Rest { uint64_t chassis = 0; glm::mat4 rest{1.0f}; float spin = 0.0f, radius = 0.5f; };
    std::unordered_map<uint64_t, Car> m_cars;               // by seat id
    std::unordered_map<uint64_t, Rest> m_rest;              // by wheel id: how it was built
    std::vector<SceneNode*> m_seen;                         // this frame's wheels
    std::unordered_map<const SceneNode*, SceneNode*> m_seatOfModel;   // this frame (null = none)
    std::vector<Car*> m_list;
};
