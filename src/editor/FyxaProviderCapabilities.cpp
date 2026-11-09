#include "FyxaProviderCapabilities.h"

namespace FyxaProviderCapabilities {

OllamaModel parseOllamaShow(const nlohmann::json& response) {
    OllamaModel result;
    if (!response.is_object() || !response.contains("capabilities") || !response["capabilities"].is_array()) {
        result.error = "Ollama /api/show did not return a valid capabilities array.";
        return result;
    }
    for (const auto& capability : response["capabilities"]) {
        if (!capability.is_string()) {
            result.error = "Ollama /api/show returned a malformed capability entry.";
            return result;
        }
        const std::string name = capability.get<std::string>();
        if (name == "completion") result.completion = true;
        else if (name == "tools") result.tools = true;
        else if (name == "vision") result.vision = true;
    }
    result.known = true;
    if (!result.completion) result.error = "The selected Ollama model does not advertise completion capability.";
    return result;
}

}
