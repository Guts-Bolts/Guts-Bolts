#pragma once
// Vehicles, like Roblox's VehicleSeat: sit in one and the keys drive whatever it's built into.
//
// A part is a VehicleSeat when it's called VehicleSeat or tagged "VehicleSeat" (it's a Seat
// too, so you sit in it). Its settings are attributes, in the same units as WalkSpeed:
//   MaxSpeed  (12)  top speed, studs a second
//   Torque    (10)  how quickly it speeds up
//   TurnSpeed (1)   how quickly it turns
// While someone sits in it, W/S (or the stick) is the Throttle and A/D the Steer, -1..1.
// Scripts can read them (seat.Throttle, seat.SteerFloat...) and set them on an empty seat
// to drive it themselves.
//
// What it drives is the vehicle: the Model the seat is in.
//  * Wheels: every hinge with a motor in the vehicle spins its wheel so the vehicle rolls
//    forward (Throttle), the left and right wheels at different speeds to turn (Steer),
//    like a tank. Balls or cylinders on HingeConstraints make good wheels.
//  * No motor hinges (boats, hovercraft, sleds): the whole vehicle is pushed forward and
//    turned, gripping sideways a little so it doesn't skid about.
// The host does the driving (it runs the physics); joined players send their keys.
#include <cstdint>
#include <unordered_map>

class Scene;
class SceneNode;

class Vehicles {
public:
    static bool isVehicleSeat(const SceneNode* n);
    static float maxSpeed(const SceneNode* seat);
    static float torque(const SceneNode* seat);
    static float turnSpeed(const SceneNode* seat);
    static constexpr float kMaxSpeed = 12.0f, kTorque = 10.0f, kTurnSpeed = 1.0f;

    // Someone in the seat is pressing these (-1..1). `driver` is their character's id.
    // Called every frame they sit there; it lapses half a second after they stop.
    void drive(uint64_t seatId, uint64_t driver, float throttle, float steer);
    // A script set seat.Throttle / seat.Steer (they stay until changed; a driver wins).
    void scriptSet(uint64_t seatId, bool throttle, float value);

    // What the seat is doing right now (0 if nothing).
    float    throttle(uint64_t seatId) const;
    float    steer(uint64_t seatId) const;
    uint64_t driver(uint64_t seatId) const;   // the character driving it, or 0

    // The host, once a frame before the physics step: spin the wheels / push the vehicles.
    // (Everyone else just counts the time, so a driver's keys lapse when they stop sending.)
    void update(Scene& scene, float dt, bool host);
    void clear() { m_seats.clear(); m_now = 0.0; }

private:
    struct Input {
        float throttle = 0.0f, steer = 0.0f;   // from the driver
        uint64_t driver = 0;
        double at = -1e9;                      // when the driver last sent them
        float scriptThrottle = 0.0f, scriptSteer = 0.0f;
        bool  wasDriving = false;              // (its wheels get stopped once when it stops)
    };
    bool live(const Input& in) const { return in.driver && m_now - in.at < 0.5; }
    std::unordered_map<uint64_t, Input> m_seats;
    double m_now = 0.0;
};
