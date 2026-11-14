#pragma once
#include <string>
#include <vector>

#include "Animation.h"

// Animations from words: "wave twice, then bow" becomes keyframes (an Animation.h clip).
// It runs on your computer and needs no AI service: it knows a list of motions (wave, bow, jump,
// walk, clap, dance...) and words that change them:
//   - which side:   left, right, both
//   - how many:     once, twice, 3 times, three times
//   - how fast:     slowly, quickly (and "very")
//   - how big:      small, gentle, big, huge
//   - looping:      loop, forever, over and over (a walk or a dance can play on repeat)
// Steps go one after another: split them with "then", commas or full stops.
//
// Poses are made for the classic body (Torso, Head, Left Arm...). On an R15 body (UpperTorso,
// LeftUpperArm...) they're moved to the matching parts. Studio's Animation Editor ("Make from
// words..."), and the AI tool make_animation use this. Fyxa can also send its own keyframes
// to make_animation for a motion that isn't on the list.
namespace AnimText {

struct Step {
    std::string text;     // the words it came from
    std::string motion;   // "wave"
    int         times = 1;
    float       start = 0.0f, end = 0.0f;   // seconds
};

struct Result {
    Anim::Clip               clip;
    std::string              name;        // "Wave, Bow"
    std::vector<Step>        steps;
    std::vector<std::string> unknown;     // bits of the text no motion matched
    std::vector<std::string> missing;     // parts the motions wanted that the rig doesn't have
    std::string              error;       // set when nothing could be made
};

// `rigParts`: the names of the rig's parts (to pick classic or R15 names). Empty = classic.
Result make(const std::string& text, const std::vector<std::string>& rigParts = {});

// Every motion it knows, for help text: {"wave", "say hi with one arm"}...
struct Motion { const char* name; const char* about; };
const std::vector<Motion>& motions();

// Fit a clip made for the classic body to this rig (R15 names), dropping parts it doesn't have.
// The names that weren't there go in `missing`.
void fitToRig(Anim::Clip& clip, const std::vector<std::string>& rigParts, std::vector<std::string>* missing = nullptr);

} // namespace AnimText
