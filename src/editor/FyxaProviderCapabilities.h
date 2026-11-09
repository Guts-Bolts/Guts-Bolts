#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace FyxaProviderCapabilities {

struct OllamaModel {
    bool known = false;
    bool completion = false;
    bool tools = false;
    bool vision = false;
    std::string error;
};

OllamaModel parseOllamaShow(const nlohmann::json& response);

}
