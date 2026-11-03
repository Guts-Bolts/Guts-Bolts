#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// RichText: Roblox's text markup for TextLabels, TextButtons and TextBoxes with RichText on.
//   "You are a <b>Cop</b>!"   "<font color=\"#ff0000\">Wanted</font>"   "<font size=\"30\">Big</font>"
// Tags: <b> <i> <u> <s> <font color="#rrggbb" / "rgb(r,g,b)" size="n" transparency="0..1">
// <uppercase> (<uc>) <br /> and <!-- comments -->; &lt; &gt; &quot; &apos; &amp; for the
// characters themselves. <i> shows as normal (there's no slanted font); unknown tags are
// left out.
namespace RichText {

struct Style {
    bool      bold = false, italic = false, underline = false, strike = false, upper = false;
    bool      hasColor = false;
    glm::vec3 color{1.0f};
    float     size = 0.0f;           // 0 = the label's own TextSize
    float     transparency = -1.0f;  // < 0 = the label's own TextTransparency
};

struct Run {
    std::string text;   // plain text ("\n" for <br />)
    Style       style;
};

std::vector<Run> parse(const std::string& markup);
// The text without the markup (what Roblox calls ContentText).
std::string strip(const std::string& markup);

} // namespace RichText
