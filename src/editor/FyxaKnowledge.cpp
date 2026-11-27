#include "FyxaKnowledge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {
using json = nlohmann::json;
constexpr std::size_t kMaxManifestBytes = 128 * 1024;
constexpr std::size_t kMaxSourceBytes = 512 * 1024;
constexpr std::size_t kMaxChunksPerSource = 512;
constexpr std::size_t kChunkChars = 1200;

std::vector<std::string> terms(const std::string& text) {
    static const std::set<std::string> stop = {
        "a","an","and","are","as","at","be","but","by","for","from","how","i","in","is","it",
        "me","my","of","on","or","our","please","the","this","to","us","we","what","when","where",
        "which","with","you","your"
    };
    std::vector<std::string> out;
    std::string word;
    auto flush = [&] {
        if (word.size() > 1 && !stop.count(word)) out.push_back(word);
        word.clear();
    };
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) word.push_back((char)std::tolower(ch));
        else flush();
    }
    flush();
    return out;
}

bool safeRelative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

bool readBounded(const std::filesystem::path& path, std::string& out) {
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(st) || std::filesystem::is_symlink(st)) return false;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxSourceBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return out.size() == size && out.size() <= kMaxSourceBytes;
}

struct Chunk { std::string sourceId, path, text; std::size_t index = 0; };

std::vector<std::string> splitChunks(const std::string& content) {
    std::vector<std::string> chunks;
    std::string current;
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        if (!current.empty() && current.size() + line.size() + 1 > kChunkChars) {
            chunks.push_back(std::move(current));
            current.clear();
        }
        if (!current.empty()) current.push_back('\n');
        current += line;
        if (chunks.size() >= kMaxChunksPerSource) break;
    }
    if (!current.empty() && chunks.size() < kMaxChunksPerSource) chunks.push_back(std::move(current));
    return chunks;
}
}

FyxaKnowledge::FyxaKnowledge(std::filesystem::path repositoryRoot, std::filesystem::path manifestPath)
    : m_root(std::move(repositoryRoot)), m_manifest(std::move(manifestPath)) {}

json FyxaKnowledge::search(const std::string& query, std::size_t limit, std::string& error) const {
    error.clear();
    if (query.size() < 2 || query.size() > 512) { error = "Query must contain 2 to 512 characters."; return json::array(); }
    if (limit == 0 || limit > 12) { error = "Result limit must be between 1 and 12."; return json::array(); }
    std::string manifestText;
    if (!readBounded(m_manifest, manifestText)) { error = "Knowledge source manifest is unavailable or exceeds its size limit."; return json::array(); }
    const json manifest = json::parse(manifestText, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object() || manifest.value("schema_version", 0) != 1 ||
        manifest.value("purpose", std::string()) != "reference_only" ||
        !manifest.contains("sources") || !manifest["sources"].is_array()) {
        error = "Knowledge manifest is invalid; retrieval is disabled.";
        return json::array();
    }
    const auto queryTerms = terms(query);
    if (queryTerms.empty()) { error = "Query contains no searchable terms."; return json::array(); }

    std::vector<Chunk> chunks;
    std::set<std::string> sourceIds, sourcePaths;
    for (const auto& source : manifest["sources"]) {
        if (!source.is_object() || source.value("authority", std::string()) != "first_party_repository" ||
            source.value("index_policy", std::string()) != "reference_only") continue;
        const std::string id = source.value("source_id", std::string());
        const std::string pathText = source.value("path", std::string());
        const std::filesystem::path relative(pathText);
        if (id.empty() || pathText.empty() || !safeRelative(relative) ||
            !sourceIds.insert(id).second || !sourcePaths.insert(pathText).second) continue;
        std::string content;
        if (!readBounded(m_root / relative, content)) continue;
        const auto sourceChunks = splitChunks(content);
        for (std::size_t i = 0; i < sourceChunks.size(); ++i)
            chunks.push_back({id, pathText, sourceChunks[i], i});
    }

    struct Ranked { json value; double score = 0; };
    std::vector<Ranked> ranked;
    for (const auto& chunk : chunks) {
        const auto docTerms = terms(chunk.text);
        if (docTerms.empty()) continue;
        std::unordered_map<std::string, int> freq;
        for (const auto& term : docTerms) ++freq[term];
        double score = 0.0;
        for (const auto& term : queryTerms) {
            const auto found = freq.find(term);
            if (found != freq.end()) score += 2.0 + std::min(found->second, 3) * 0.35;
        }
        if (score <= 0) continue;
        std::string snippet = chunk.text;
        if (snippet.size() > 640) snippet.resize(640);
        ranked.push_back({{{"source_id", chunk.sourceId}, {"path", chunk.path},
                          {"chunk", chunk.index}, {"score", score}, {"snippet", snippet}}, score});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.score > b.score; });
    json result = json::array();
    for (const auto& item : ranked) {
        if (result.size() >= limit) break;
        result.push_back(item.value);
    }
    return result;
}
