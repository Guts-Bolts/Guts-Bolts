#include "Luau.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Luau {

namespace {

enum class T { Name, Number, String, Interp, Comment, Symbol, Space, End };

struct Tok {
    T           type;
    std::string text;
    bool        newline = false;   // Space / Comment containing a line break
};

bool isNameStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool isNameChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// [[ ... ]] / [==[ ... ]==]: returns the length, or 0 if it isn't one.
size_t longBracket(const std::string& s, size_t i) {
    if (s[i] != '[') return 0;
    size_t j = i + 1;
    int level = 0;
    while (j < s.size() && s[j] == '=') { ++level; ++j; }
    if (j >= s.size() || s[j] != '[') return 0;
    std::string close = "]" + std::string(level, '=') + "]";
    size_t e = s.find(close, j + 1);
    return (e == std::string::npos ? s.size() : e + close.size()) - i;
}

std::vector<Tok> lex(const std::string& s) {
    static const char* multi[] = {"...", "..=", "//=", "..", "//", "==", "~=", "<=", ">=", "::", "+=", "-=",
                                  "*=", "/=", "%=", "^=", "->"};
    std::vector<Tok> out;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace((unsigned char)c)) {
            size_t j = i;
            while (j < s.size() && std::isspace((unsigned char)s[j])) ++j;
            std::string t = s.substr(i, j - i);
            out.push_back({T::Space, t, t.find('\n') != std::string::npos});
            i = j;
        } else if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            size_t len = (i + 2 < s.size()) ? longBracket(s, i + 2) : 0;
            size_t j = len ? i + 2 + len : s.find('\n', i);
            if (j == std::string::npos) j = s.size();
            std::string t = s.substr(i, j - i);
            out.push_back({T::Comment, t, t.find('\n') != std::string::npos});
            i = j;
        } else if (c == '"' || c == '\'' || c == '`') {
            size_t j = i + 1;
            int brace = 0;
            while (j < s.size()) {
                if (s[j] == '\\') { j += 2; continue; }
                if (c == '`' && s[j] == '{') ++brace;
                if (c == '`' && s[j] == '}') --brace;
                if (s[j] == c && brace <= 0) { ++j; break; }
                if (s[j] == '\n' && c != '`') break;
                ++j;
            }
            out.push_back({c == '`' ? T::Interp : T::String, s.substr(i, j - i)});
            i = j;
        } else if (c == '[' && longBracket(s, i)) {
            size_t len = longBracket(s, i);
            out.push_back({T::String, s.substr(i, len)});
            i += len;
        } else if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            size_t j = i + 1;
            while (j < s.size() && (isNameChar(s[j]) || s[j] == '.' ||
                                    ((s[j] == '+' || s[j] == '-') && (s[j - 1] == 'e' || s[j - 1] == 'E' ||
                                                                      s[j - 1] == 'p' || s[j - 1] == 'P'))))
                ++j;
            std::string num = s.substr(i, j - i);
            num.erase(std::remove(num.begin(), num.end(), '_'), num.end());   // Luau allows 1_000_000
            out.push_back({T::Number, num});
            i = j;
        } else if (isNameStart(c)) {
            size_t j = i;
            while (j < s.size() && isNameChar(s[j])) ++j;
            out.push_back({T::Name, s.substr(i, j - i)});
            i = j;
        } else {
            std::string sym(1, c);
            for (const char* m : multi) if (s.compare(i, std::strlen(m), m) == 0) { sym = m; break; }
            out.push_back({T::Symbol, sym});
            i += sym.size();
        }
    }
    out.push_back({T::End, ""});
    return out;
}

bool isSym(const Tok& t, const char* s) { return t.type == T::Symbol && t.text == s; }
bool isName(const Tok& t, const char* s) { return t.type == T::Name && t.text == s; }

struct Rewriter {
    std::vector<Tok> t;
    std::vector<std::string>* notes;
    std::set<std::string> said;

    void note(const std::string& n) { if (notes && said.insert(n).second) notes->push_back(n); }

    size_t next(size_t i) const {                // next meaningful token
        ++i;
        while (i < t.size() && (t[i].type == T::Space || t[i].type == T::Comment)) ++i;
        return std::min(i, t.size() - 1);
    }
    long prev(long i) const {
        --i;
        while (i >= 0 && (t[i].type == T::Space || t[i].type == T::Comment)) --i;
        return i;
    }

    // Skip a type expression starting at i; stops at a token in `stops` at depth 0
    // (or a line break at depth 0 when stopAtLine). Returns the first index after it.
    size_t skipType(size_t i, std::initializer_list<const char*> stops, bool stopAtLine) {
        int depth = 0;
        while (t[i].type != T::End) {
            const Tok& k = t[i];
            if (depth == 0) {
                if (k.type == T::Symbol)
                    for (const char* s : stops) if (k.text == s) return i;
                if (stopAtLine && k.newline) return i;
                if (k.type == T::Name && (k.text == "local" || k.text == "return" || k.text == "end")) return i;
            }
            if (isSym(k, "(") || isSym(k, "{") || isSym(k, "[") || isSym(k, "<")) ++depth;
            if (isSym(k, ")") || isSym(k, "}") || isSym(k, "]") || isSym(k, ">")) {
                if (depth == 0) return i;
                --depth;
            }
            ++i;
        }
        return i;
    }
    // Removes tokens but keeps their line breaks (so line numbers don't move).
    void blank(size_t a, size_t b) {
        for (size_t k = a; k < b && k < t.size(); ++k) {
            if (t[k].type == T::End) continue;
            t[k].text = std::string(std::count(t[k].text.begin(), t[k].text.end(), '\n'), '\n');
            t[k].type = t[k].newline ? T::Space : T::Comment;
        }
    }

    // Type annotations: local x: T, function params (a: T), return types, casts, type aliases.
    void stripTypes() {
        for (size_t i = 0; t[i].type != T::End; ++i) {
            Tok& k = t[i];
            // type / export type aliases at the start of a statement
            if ((isName(k, "type") || isName(k, "export")) && k.type == T::Name) {
                long p = prev((long)i);
                bool stmtStart = p < 0 || t[p].newline || isSym(t[p], ";") || t[p].type == T::Space ||
                                 isName(t[p], "end") || isName(t[p], "then") || isName(t[p], "do");
                size_t n = next(i);
                if (isName(k, "export") && isName(t[n], "type")) n = next(n);
                else if (isName(k, "export")) continue;
                if (stmtStart && t[n].type == T::Name && !isSym(t[n], "(") && (isSym(t[next(n)], "=") || isSym(t[next(n)], "<"))) {
                    size_t eq = next(n);
                    if (isSym(t[eq], "<")) eq = next(skipType(next(eq), {">"}, false));
                    size_t end = skipType(next(eq), {";"}, true);
                    blank(i, end);
                    note("type definitions were removed");
                    continue;
                }
            }
            // local a: T, b: U = ...
            if (isName(k, "local") && !isName(t[next(i)], "function")) {
                size_t j = next(i);
                while (t[j].type == T::Name) {
                    size_t c = next(j);
                    if (isSym(t[c], ":")) {
                        size_t e = skipType(next(c), {",", "="}, true);
                        blank(c, e);
                        note("type annotations were removed");
                        j = e;
                        while (t[j].type == T::Space || t[j].type == T::Comment) ++j;
                    } else {
                        j = c;
                    }
                    if (isSym(t[j], ",")) j = next(j); else break;
                }
            }
            // function name<T>(a: T, ...): R
            if (isName(k, "function")) {
                size_t j = next(i);
                while (t[j].type == T::Name || isSym(t[j], ".") || isSym(t[j], ":")) j = next(j);
                if (isSym(t[j], "<")) { size_t e = skipType(next(j), {">"}, false); blank(j, e + 1); j = next(e); }
                if (!isSym(t[j], "(")) continue;
                size_t p = next(j);
                while (!isSym(t[p], ")") && t[p].type != T::End) {
                    if (t[p].type == T::Name || isSym(t[p], "...")) {
                        size_t c = next(p);
                        if (isSym(t[c], ":")) {
                            size_t e = skipType(next(c), {",", ")"}, false);
                            blank(c, e);
                            note("type annotations were removed");
                            p = e;
                            while (t[p].type == T::Space || t[p].type == T::Comment) ++p;
                            if (isSym(t[p], ",")) p = next(p);
                            continue;
                        }
                    }
                    p = next(p);
                }
                size_t c = next(p);
                if (isSym(t[p], ")") && isSym(t[c], ":")) {
                    size_t e = skipType(next(c), {}, true);
                    blank(c, e);
                }
            }
            // ::label:: (plain Lua's goto labels) isn't a cast
            if (isSym(k, "::") && t[next(i)].type == T::Name && isSym(t[next(next(i))], "::")) {
                i = next(next(i));
                continue;
            }
            // expr :: Type  (a cast)
            if (isSym(k, "::")) {
                size_t e = skipType(next(i), {",", ")", "]", "}", "=", ";"}, true);
                // stop at binary operators too
                for (size_t q = next(i); q < e; q = next(q))
                    if (t[q].type == T::Symbol && std::string("+-*/%^..==~=<=>=andor").find(t[q].text) != std::string::npos &&
                        t[q].text != "." && t[q].text != "<" && t[q].text != ">") { e = q; break; }
                blank(i, e);
                note("type casts were removed");
            }
        }
    }

    // x += y  ->  x = x + (y)
    void compoundAssign() {
        for (size_t i = 0; t[i].type != T::End; ++i) {
            Tok& k = t[i];
            static const char* ops[] = {"+=", "-=", "*=", "/=", "//=", "%=", "^=", "..="};
            std::string op;
            if (k.type == T::Symbol) for (const char* o : ops) if (k.text == o) op = std::string(o, std::strlen(o) - 1);
            if (op.empty()) continue;
            // The target: Name (.Name | [expr])*
            long a = prev((long)i);
            long start = a;
            while (a >= 0) {
                if (isSym(t[a], "]")) {
                    int d = 0;
                    long b = a;
                    for (; b >= 0; --b) { if (isSym(t[b], "]")) ++d; if (isSym(t[b], "[")) { if (--d == 0) break; } }
                    a = prev(b);
                    start = b;
                    if (a >= 0 && t[a].type == T::Name) { start = a; long pa = prev(a); if (pa >= 0 && isSym(t[pa], ".")) { a = prev(pa); continue; } break; }
                    continue;
                }
                if (t[a].type == T::Name) {
                    start = a;
                    long pa = prev(a);
                    if (pa >= 0 && isSym(t[pa], ".")) { a = prev(pa); continue; }
                }
                break;
            }
            if (start < 0) continue;
            std::string target;
            for (long q = start; q < (long)i; ++q) target += t[q].text;
            while (!target.empty() && std::isspace((unsigned char)target.back())) target.pop_back();
            // The value: up to the end of the statement (line end at depth 0, or ';').
            size_t e = i + 1;
            int depth = 0;
            for (; t[e].type != T::End; ++e) {
                if (isSym(t[e], "(") || isSym(t[e], "{") || isSym(t[e], "[")) ++depth;
                if (isSym(t[e], ")") || isSym(t[e], "}") || isSym(t[e], "]")) --depth;
                if (depth <= 0 && (t[e].newline || isSym(t[e], ";") || t[e].type == T::Comment)) break;
                if (depth <= 0 && t[e].type == T::Name && (t[e].text == "end" || t[e].text == "else" || t[e].text == "elseif")) break;
                // A name right after a finished value starts the next statement: n += 1 print(n)
                if (depth <= 0 && t[e].type == T::Name && t[e].text != "and" && t[e].text != "or") {
                    const long p = prev((long)e);
                    if (p > (long)i) {
                        const Tok& b = t[p];
                        const bool done = b.type == T::Number || b.type == T::String || b.type == T::Interp ||
                                          isSym(b, ")") || isSym(b, "]") || isSym(b, "}") ||
                                          (b.type == T::Name && b.text != "and" && b.text != "or" && b.text != "not" &&
                                           b.text != "function" && b.text != "return" && b.text != "local");
                        if (done) break;
                    }
                }
            }
            // Keep trailing spaces outside the parentheses.
            size_t valEnd = e;
            while (valEnd > i + 1 && t[valEnd - 1].type == T::Space) --valEnd;
            std::string value;
            for (size_t q = i + 1; q < valEnd; ++q) value += t[q].text;
            std::string trimmed = value;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            k.text = "= " + target + " " + op + " (" + trimmed + ")";
            for (size_t q = i + 1; q < valEnd; ++q) t[q].text.clear();
            note("compound assignments (+= ...) were rewritten");
        }
    }

    // continue -> goto __continue_N, with the label at the end of the loop body.
    void continues() {
        struct Block { int kind; size_t doTok; int label; bool used; };   // kind: 0 other, 1 loop, 2 repeat, 3 function
        std::vector<Block> stack;
        int labels = 0;
        std::vector<std::pair<size_t, std::string>> insertAfter, insertBefore;
        for (size_t i = 0; t[i].type != T::End; ++i) {
            Tok& k = t[i];
            if (k.type != T::Name) continue;
            // a.end / a:end are fields, not keywords
            long p = prev((long)i);
            if (p >= 0 && (isSym(t[p], ".") || isSym(t[p], ":"))) continue;
            if (k.text == "while" || k.text == "for") stack.push_back({1, 0, 0, false});
            else if (k.text == "do") {
                if (!stack.empty() && stack.back().kind == 1 && stack.back().doTok == 0) stack.back().doTok = i;
                else stack.push_back({0, 0, 0, false});
            } else if (k.text == "repeat") stack.push_back({2, i, 0, false});
            else if (k.text == "function") stack.push_back({3, 0, 0, false});
            else if (k.text == "if") stack.push_back({0, 0, 0, false});
            else if (k.text == "end" || k.text == "until") {
                if (stack.empty()) continue;
                Block b = stack.back();
                stack.pop_back();
                if (b.used && b.kind == 1) {
                    insertAfter.push_back({b.doTok, " do"});
                    insertBefore.push_back({i, "end ::__continue_" + std::to_string(b.label) + ":: "});
                } else if (b.used && b.kind == 2) {
                    insertBefore.push_back({i, "::__continue_" + std::to_string(b.label) + ":: "});
                }
            } else if (k.text == "continue") {
                size_t n = next(i);
                if (isSym(t[n], "=") || isSym(t[n], "(") || isSym(t[n], ".") || isSym(t[n], "[") || isSym(t[n], ":")) continue;
                // `continue` used as a value (a variable called continue): not a statement.
                if (t[n].type == T::Symbol && !isSym(t[n], ";")) continue;
                if (isName(t[n], "then") || isName(t[n], "do") || isName(t[n], "and") || isName(t[n], "or")) continue;
                if (p >= 0 && t[p].type == T::Symbol && !isSym(t[p], ";") && !isSym(t[p], ")") && !isSym(t[p], "]") &&
                    !isSym(t[p], "}") && !isSym(t[p], "::")) continue;
                if (p >= 0 && t[p].type == T::Name &&
                    (t[p].text == "if" || t[p].text == "elseif" || t[p].text == "while" || t[p].text == "until" ||
                     t[p].text == "return" || t[p].text == "local" || t[p].text == "not" || t[p].text == "and" ||
                     t[p].text == "or" || t[p].text == "in")) continue;
                for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                    if (it->kind == 3) break;
                    if (it->kind == 1 || it->kind == 2) {
                        if (!it->used) { it->used = true; it->label = ++labels; }
                        k.text = "goto __continue_" + std::to_string(it->label);
                        note("'continue' was rewritten");
                        break;
                    }
                }
            }
        }
        for (auto& [idx, text] : insertAfter) t[idx].text += text;
        for (auto& [idx, text] : insertBefore) t[idx].text = text + t[idx].text;
    }

    // `Hello {name}!` -> ("Hello " .. tostring(name) .. "!")
    void interpolation() {
        for (Tok& k : t) {
            if (k.type != T::Interp) continue;
            std::string body = k.text.substr(1, k.text.size() >= 2 ? k.text.size() - 2 : 0);
            std::vector<std::string> parts;
            std::string lit;
            size_t lines = 0;   // its line breaks go after it instead (so line numbers stay put)
            for (size_t i = 0; i < body.size(); ++i) {
                if (body[i] == '\\' && i + 1 < body.size()) { lit += body[i]; lit += body[i + 1]; ++i; continue; }
                if (body[i] == '{') {
                    int d = 1;
                    size_t j = i + 1;
                    for (; j < body.size() && d; ++j) { if (body[j] == '{') ++d; if (body[j] == '}') --d; }
                    if (!lit.empty()) { parts.push_back("\"" + lit + "\""); lit.clear(); }
                    std::string expr = toLua(body.substr(i + 1, j - i - 2));
                    lines += (size_t)std::count(expr.begin(), expr.end(), '\n');
                    std::replace(expr.begin(), expr.end(), '\n', ' ');
                    parts.push_back("tostring(" + expr + ")");
                    i = j - 1;
                    continue;
                }
                if (body[i] == '"') { lit += "\\\""; continue; }
                if (body[i] == '\n') { lit += "\\n"; ++lines; continue; }
                lit += body[i];
            }
            if (!lit.empty() || parts.empty()) parts.push_back("\"" + lit + "\"");
            std::string out = "(";
            for (size_t i = 0; i < parts.size(); ++i) out += (i ? " .. " : "") + parts[i];
            k.text = out + ")" + std::string(lines, '\n');
            k.type = T::String;
            note("string interpolation was rewritten");
        }
    }

    std::string join() const {
        std::string out;
        for (const Tok& k : t) out += k.text;
        return out;
    }
};

} // namespace

std::string toLua(const std::string& source, std::vector<std::string>* notes) {
    Rewriter r{lex(source), notes, {}};
    r.interpolation();
    r.stripTypes();
    r.compoundAssign();
    r.continues();
    return r.join();
}

int load(lua_State* L, const std::string& source, const char* chunkName, const char* mode) {
    int r = luaL_loadbufferx(L, source.data(), source.size(), chunkName, mode);
    if (r != LUA_ERRSYNTAX) return r;
    const std::string lua = toLua(source);
    if (lua == source) return r;               // nothing Luau in it: the real error
    lua_pop(L, 1);
    return luaL_loadbufferx(L, lua.data(), lua.size(), chunkName, mode);
}

} // namespace Luau
