#pragma once
#include <string>

// The language you read and write in, as a short code ("en", "ru", "es"...), for
// translating the in-game chat. It's the computer's (or phone's) language.
namespace Lang {

// Your language's two-letter code, or "" if it can't be told (then chat isn't translated).
std::string code();
// A language's name in English ("ru" -> "Russian"), or "" for codes it doesn't know.
const char* name(const std::string& code);
// True for the languages chat can be translated between (the same list as the server's).
bool translatable(const std::string& code);

} // namespace Lang
