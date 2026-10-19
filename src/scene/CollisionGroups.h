#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Collision groups (like Roblox's PhysicsService): every part is in one group
// (BasePart.CollisionGroup, "Default" to start with) and a table says which
// groups bump into which. Two parts in groups that don't collide pass through
// each other, as do a character and a part (the character's group is its
// HumanoidRootPart's). Up to 32 groups, "Default" always the first.
struct CollisionGroups {
    static constexpr int kMax = 32;
    static constexpr const char* kDefault = "Default";
    static constexpr size_t kMaxNameLength = 100;

    std::array<std::string, kMax> names;   // "" = a free slot (slot 0 is always "Default")
    std::array<uint32_t, kMax>    masks;   // bit b of masks[a]: groups a and b collide

    CollisionGroups() {
        names[0] = kDefault;
        masks.fill(~0u);
    }

    // The slot of a group (-1 = not registered).
    int find(const std::string& name) const {
        for (int i = 0; i < kMax; ++i)
            if (!names[i].empty() && names[i] == name) return i;
        return -1;
    }
    // The slot a part in group `name` acts as: an unknown group is the same as Default.
    int slotOf(const std::string& name) const {
        if (name.empty() || name == kDefault || onlyDefault()) return 0;
        const int i = find(name);
        return i < 0 ? 0 : i;
    }
    bool collides(int a, int b) const {
        if (a < 0 || b < 0 || a >= kMax || b >= kMax) return true;
        return (masks[a] >> b) & 1u;
    }
    bool collides(const std::string& a, const std::string& b) const { return collides(slotOf(a), slotOf(b)); }
    // Only Default is registered (most games): nothing to look up.
    bool onlyDefault() const {
        for (int i = 1; i < kMax; ++i) if (!names[i].empty()) return false;
        return true;
    }
    int count() const {
        int n = 0;
        for (const auto& s : names) if (!s.empty()) ++n;
        return n;
    }
    // A new group (colliding with everything). Returns its slot, or -1 when all 32 are used.
    // Registering one that already exists just gives its slot back.
    int add(const std::string& name) {
        if (int i = find(name); i >= 0) return i;
        for (int i = 1; i < kMax; ++i)
            if (names[i].empty()) {
                names[i] = name;
                setAll(i, true);
                return i;
            }
        return -1;
    }
    bool remove(const std::string& name) {
        const int i = find(name);
        if (i <= 0) return false;
        names[i].clear();
        setAll(i, true);
        return true;
    }
    void set(int a, int b, bool collide) {
        if (collide) { masks[a] |= 1u << b; masks[b] |= 1u << a; }
        else         { masks[a] &= ~(1u << b); masks[b] &= ~(1u << a); }
    }
    // The registered groups, in slot order.
    std::vector<int> slots() const {
        std::vector<int> out;
        for (int i = 0; i < kMax; ++i) if (!names[i].empty()) out.push_back(i);
        return out;
    }
    // Is it the table every new game starts with (only Default, colliding with itself)?
    bool isDefault() const { return onlyDefault() && collides(0, 0); }
    bool operator==(const CollisionGroups& o) const { return names == o.names && masks == o.masks; }
    bool operator!=(const CollisionGroups& o) const { return !(*this == o); }

    // Why a name can't be a group's ("" = it can).
    static std::string badName(const std::string& name) {
        if (name.empty()) return "a collision group needs a name";
        if (name.size() > kMaxNameLength) return "collision group names can be at most 100 characters";
        return {};
    }

private:
    void setAll(int i, bool collide) {
        for (int b = 0; b < kMax; ++b) set(i, b, collide);
    }
};
