#pragma once
// Roblox classes that Instance.new can make although Guts&Bolts has no object of its own
// for them (ObjectValue, Configuration, MeshPart, Atmosphere, SelectionBox, Dialog...).
// Each one is an object of a kind we do have (a Model, a Part, a Light, a Frame...) with a
// "RobloxClass" attribute, and its properties are kept as attributes, so scripts can make
// them, set them, read them back and find them by class. The ones marked `works` do
// something here too (an Atmosphere sets the fog, a MeshPart loads its MeshId...); the
// others print one warning the first time a script makes one.
#include <string>
#include <vector>

namespace StandIns {

enum class Type { Bool, Number, String, Vector3, Vector2, Color3, Instance, CFrame };

struct Prop {
    const char* name;
    Type type;
    double n = 0;            // Bool / Number
    float x = 0, y = 0, z = 0; // Vector3 / Vector2 / Color3 (0..1)
    const char* s = "";      // String (enum items are their names)
};

// What the object really is underneath.
enum class Base { Model, Part, Remote, SpotLight, Decal, Frame };

struct Class {
    const char* name;
    Base base;
    bool works;                         // false: warn once that it does nothing yet
    bool valueLike;                     // a ValueBase: .Changed gives the new Value
    std::vector<const char*> isA;       // the Roblox classes it also counts as
    std::vector<Prop> props;
};

const Class* find(const std::string& name);
const Prop* prop(const Class* c, const char* name);
// Every class (for the tests and the script checker).
const std::vector<Class>& all();

} // namespace StandIns
