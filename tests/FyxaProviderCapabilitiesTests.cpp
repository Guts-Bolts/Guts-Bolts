#include "../src/editor/FyxaProviderCapabilities.h"

#include <cstdlib>
#include <iostream>

using nlohmann::json;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED at line " << __LINE__ << ": " #condition << "\n"; \
    return EXIT_FAILURE; \
} } while (false)

int main() {
    const auto capable = FyxaProviderCapabilities::parseOllamaShow({
        {"capabilities", json::array({"completion", "tools", "vision", "thinking"})}
    });
    CHECK(capable.known);
    CHECK(capable.completion);
    CHECK(capable.tools);
    CHECK(capable.vision);
    CHECK(capable.error.empty());

    const auto chatOnly = FyxaProviderCapabilities::parseOllamaShow({
        {"capabilities", json::array({"completion", "vision"})}
    });
    CHECK(chatOnly.known);
    CHECK(chatOnly.completion);
    CHECK(!chatOnly.tools);
    CHECK(chatOnly.vision);

    const auto missing = FyxaProviderCapabilities::parseOllamaShow(json::object());
    CHECK(!missing.known);
    CHECK(!missing.error.empty());

    const auto malformed = FyxaProviderCapabilities::parseOllamaShow({
        {"capabilities", json::array({"completion", 7})}
    });
    CHECK(!malformed.known);
    CHECK(!malformed.error.empty());

    const auto noCompletion = FyxaProviderCapabilities::parseOllamaShow({
        {"capabilities", json::array({"tools"})}
    });
    CHECK(noCompletion.known);
    CHECK(!noCompletion.completion);
    CHECK(noCompletion.tools);
    CHECK(!noCompletion.error.empty());

    std::cout << "Fyxa provider capability tests passed\n";
    return EXIT_SUCCESS;
}
