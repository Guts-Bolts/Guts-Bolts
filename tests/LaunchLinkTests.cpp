#include "player/LaunchLink.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
}

int main() {
    LaunchLink::Link link;
    check(LaunchLink::parse("gutsandbolts://play/123?guest=boy", link) &&
          link.game == "123" && link.guest == "boy" && link.lesson.empty() && !link.edit,
          "existing Play links keep their behavior");
    check(LaunchLink::parse("gutsandbolts://edit/123", link) &&
          link.game == "123" && link.edit && link.lesson.empty(),
          "existing Edit links keep their behavior");
    check(LaunchLink::parse("gutsandbolts://learn/first-build", link) &&
          link.lesson == "first-build" && link.game.empty() && !link.edit,
          "valid lesson link is parsed");
    check(LaunchLink::parse("gutsandbolts://learn/first-build%2d", link) &&
          link.lesson == "first-build-", "percent-encoded safe slug is decoded");
    check(!LaunchLink::parse("gutsandbolts://learn/../outside", link),
          "path traversal is rejected");
    check(!LaunchLink::parse("gutsandbolts://learn/First-Build", link),
          "non-canonical lesson IDs are rejected");
    check(!LaunchLink::parse("gutsandbolts://learn/first-build%2f..", link),
          "encoded separators are rejected");
    check(!LaunchLink::parse("gutsandbolts://learn/", link),
          "empty lesson ID is rejected");
    std::cout << (failures ? "Launch link tests FAILED\n" : "Launch link tests passed\n");
    return failures ? 1 : 0;
}
