// Packed material pictures (src/renderer/OrmPack.h): occlusion, roughness and metalness
// in the red, green and blue of one picture, and the MaterialVariant's OrmMap property.
//
//   OrmPackTests
#include "renderer/OrmPack.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
// A grey picture, every pixel `value` (or made by `f`).
std::vector<unsigned char> grey(int w, int h, unsigned char value) {
    std::vector<unsigned char> px((size_t)w * h * 4, 255);
    for (size_t i = 0; i < (size_t)w * h; ++i) px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = value;
    return px;
}
} // namespace

int main() {
    using OrmPack::Source;
    std::vector<unsigned char> out;
    int w = 0, h = 0;

    // All three, the same size: each lands in its own channel.
    {
        auto o = grey(4, 4, 200), r = grey(4, 4, 90), m = grey(4, 4, 10);
        o[0] = 7;   // one odd pixel, to see nothing moves
        check(OrmPack::pack({o.data(), 4, 4}, {r.data(), 4, 4}, {m.data(), 4, 4}, out, w, h), "three pictures pack into one");
        check(w == 4 && h == 4 && out.size() == 64, "...the same size as they were");
        check(out[0] == 7 && out[4] == 200, "occlusion is in red");
        check(out[1] == 90 && out[5] == 90, "roughness is in green");
        check(out[2] == 10 && out[6] == 10, "metalness is in blue");
    }
    // Only roughness and metalness (what a Roblox material has): occlusion stays white,
    // so nothing gets darker than before.
    {
        auto r = grey(2, 2, 128), m = grey(2, 2, 255);
        check(OrmPack::pack({}, {r.data(), 2, 2}, {m.data(), 2, 2}, out, w, h), "roughness + metalness pack without occlusion");
        check(out[0] == 255 && out[1] == 128 && out[2] == 255, "...occlusion is white (full sky light)");
    }
    // Only one of them.
    {
        auto r = grey(2, 2, 40);
        check(OrmPack::pack({}, {r.data(), 2, 2}, {}, out, w, h) && out[1] == 40 && out[0] == 255 && out[2] == 255,
              "roughness alone: the other two are white");
    }
    // Different sizes: the result is as big as the biggest, the small one is stretched.
    {
        auto r = grey(8, 8, 60);
        std::vector<unsigned char> m((size_t)2 * 2 * 4, 255);
        const unsigned char vals[4] = {0, 255, 255, 0};   // a 2x2 checker
        for (int i = 0; i < 4; ++i) m[(size_t)i * 4] = vals[i];
        check(OrmPack::pack({}, {r.data(), 8, 8}, {m.data(), 2, 2}, out, w, h) && w == 8 && h == 8, "a small and a big picture: the big size wins");
        bool greenRight = true;
        for (size_t i = 0; i < 64; ++i) greenRight = greenRight && out[i * 4 + 1] == 60;
        check(greenRight, "...the big one is copied as it is");
        const auto blue = [&](int x, int y) { return (int)out[((size_t)y * 8 + x) * 4 + 2]; };
        check(blue(1, 1) < 80 && blue(5, 1) > 175 && blue(1, 5) > 175 && blue(5, 5) < 80,
              "...the small one is stretched over it (dark and light quarters in the right places)");
        check(blue(3, 1) > blue(1, 1) && blue(3, 1) < blue(5, 1), "...smoothly in between");
    }
    // None at all.
    check(!OrmPack::pack({}, {}, {}, out, w, h) && out.empty() && w == 0, "no pictures: nothing is made");

    // --- The OrmMap property ---
    {
        MaterialVariantProps v;
        check(v.ormMap.empty(), "a new material has no OrmMap");
        v.base = Material::Metal;
        v.colorMap = "rust.png";
        v.ormMap = "rust_orm.png";
        const nlohmann::json j = Serializer::variantToJson(v);
        check(j.value("ormMap", std::string()) == "rust_orm.png", "the OrmMap is saved");
        MaterialVariantProps back;
        Serializer::variantFromJson(j, back);
        check(back.ormMap == "rust_orm.png" && back.colorMap == "rust.png", "...and comes back when loaded");
        MaterialVariantProps old;
        Serializer::variantFromJson(nlohmann::json{{"base", "Wood"}, {"roughnessMap", "r.png"}}, old);
        check(old.ormMap.empty() && old.roughnessMap == "r.png", "an older save without one still loads");
        check(!Serializer::variantToJson(old).contains("ormMap"), "...and doesn't grow an empty OrmMap");
        MaterialVariantProps longOne;
        longOne.ormMap.assign(900, 'x');
        clampVariant(longOne);
        check(longOne.ormMap.size() <= 500, "a silly-long OrmMap is cut like the other pictures");
    }

    std::cout << (failures ? "FAILED\n" : "All packed-picture tests passed\n");
    return failures ? 1 : 0;
}
