// Profile backgrounds: a picture behind your avatar on your profile. Any decal that passed its
// staff check works. worker/backgrounds.js has the same rules.
//   profile.background {decal}   ('' takes it off)
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
// (Like Server.cpp's: "" when there's no staff check waiting or failed.)
std::string reviewOf(const json& meta) { return meta.is_object() ? meta.value("review", std::string()) : std::string(); }
} // namespace

json GbServer::backgroundOf(const User& u) const {
    if (u.background.empty()) return nullptr;
    auto it = m_assets.find(u.background);
    if (it == m_assets.end() || it->second.kind != "decal" || !reviewOf(it->second.meta).empty()) return nullptr;
    return {{"id", it->second.id}, {"num", it->second.num}, {"name", it->second.name}};
}

json GbServer::backgroundOp(User& me, const json& args) {
    if (me.userId == 0) return fail("Sign up to pick a profile background.");
    std::string want = args.contains("decal") && args["decal"].is_string() ? Online::cleanText(args["decal"].get<std::string>(), 80)
                     : args.contains("decal") && args["decal"].is_number_integer() ? std::to_string(args["decal"].get<long long>()) : std::string();
    if (want.rfind("gb:", 0) == 0) want = want.substr(3);
    if (want.empty()) {
        me.background.clear();
        saveUsers();
        json r = okay(); r["background"] = nullptr; return r;
    }
    auto it = findAsset(want);
    if (it == m_assets.end() || it->second.kind != "decal") return fail("That isn't a decal. Pick a decal from the Library (or upload your own).");
    if (!reviewOf(it->second.meta).empty()) return fail("That decal is still waiting for its staff check.");
    me.background = it->second.id;
    saveUsers();
    json r = okay(); r["background"] = backgroundOf(me); return r;
}
