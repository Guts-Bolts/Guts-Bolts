#include "../src/editor/StudioMcpAccess.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED at line " << __LINE__ << ": " #condition "\\n"; \
    return EXIT_FAILURE; \
} } while (false)

int main() {
    CHECK(StudioMcpAccess::appMay("get_engine_info", false));
    CHECK(StudioMcpAccess::appMay("read_script", false));
    CHECK(StudioMcpAccess::appMay("screenshot", false));

    // Write-capable tools remain denied unless the connected app has studio.write.
    CHECK(!StudioMcpAccess::appMay("set_property", false));
    CHECK(!StudioMcpAccess::appMay("delete_object", false));
    CHECK(StudioMcpAccess::appMay("set_property", true));
    CHECK(StudioMcpAccess::appMay("delete_object", true));

    // Sensitive or unregistered capabilities are denied even with studio.write.
    CHECK(!StudioMcpAccess::appMay("publish_game", true));
    CHECK(!StudioMcpAccess::appMay("compare_scenes", true));
    CHECK(!StudioMcpAccess::appMay("read_private_account_data", true));
    CHECK(!StudioMcpAccess::appMay("", true));

    std::cout << "Connected-app capability boundary tests passed\\n";
    return EXIT_SUCCESS;
}
