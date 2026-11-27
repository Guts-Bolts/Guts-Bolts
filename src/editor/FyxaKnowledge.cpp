#include "FyxaKnowledge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <utility>

namespace {
using json = nlohmann::json;
std::mutex g_knowledgeMutex;
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
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

bool containedWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;
    auto rootIt = canonicalRoot.begin();
    auto candidateIt = canonicalCandidate.begin();
    for (; rootIt != canonicalRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == canonicalCandidate.end() || *rootIt != *candidateIt) return false;
    }
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
        if (line.size() > kChunkChars) {
            if (!current.empty()) { chunks.push_back(std::move(current)); current.clear(); }
            for (std::size_t offset = 0; offset < line.size() && chunks.size() < kMaxChunksPerSource; offset += kChunkChars)
                chunks.push_back(line.substr(offset, kChunkChars));
            if (chunks.size() >= kMaxChunksPerSource) break;
            continue;
        }
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
    const auto queryTerms = terms(query);
    if (queryTerms.empty()) { error = "Query contains no searchable terms."; return json::array(); }

    std::lock_guard lock(g_knowledgeMutex);
    if (!m_indexReady) {
        m_indexReady = true;
        std::string manifestText;
        if (!readBounded(m_manifest, manifestText)) m_indexError = "Knowledge source manifest is unavailable or exceeds its size limit.";
        else {
            const json manifest = json::parse(manifestText, nullptr, false);
            if (manifest.is_discarded() || !manifest.is_object() || !manifest.contains("schema_version") ||
                !manifest["schema_version"].is_number_integer() || manifest["schema_version"] != 1 ||
                !manifest.contains("purpose") || !manifest["purpose"].is_string() || manifest["purpose"] != "reference_only" ||
                !manifest.contains("sources") || !manifest["sources"].is_array()) {
                m_indexError = "Knowledge manifest is invalid; retrieval is disabled.";
            } else {
                std::set<std::string> sourceIds, sourcePaths;
                for (const auto& source : manifest["sources"]) {
                    if (!source.is_object() || !source.contains("authority") || !source["authority"].is_string() ||
                        source["authority"] != "first_party_repository" || !source.contains("index_policy") ||
                        !source["index_policy"].is_string() || source["index_policy"] != "reference_only") continue;
                    const std::string id = source.contains("source_id") && source["source_id"].is_string()
                        ? source["source_id"].get<std::string>() : std::string();
                    const std::string pathText = source.contains("path") && source["path"].is_string()
                        ? source["path"].get<std::string>() : std::string();
                    const std::filesystem::path relative(pathText);
                    if (id.empty() || pathText.empty() || !safeRelative(relative) || !containedWithin(m_root, m_root / relative) ||
                        !sourceIds.insert(id).second || !sourcePaths.insert(pathText).second) continue;
                    std::string content;
                    if (!readBounded(m_root / relative, content)) continue;
                    const auto sourceChunks = splitChunks(content);
                    for (std::size_t i = 0; i < sourceChunks.size(); ++i)
                        m_index.push_back({{"source_id", id}, {"path", pathText}, {"chunk", i}, {"text", sourceChunks[i]}});
                }
            }
        }
    }
    if (!m_indexError.empty()) { error = m_indexError; return json::array(); }
    struct Ranked { json value; double score = 0; };
    std::vector<Ranked> ranked;
    for (const auto& chunk : m_index) {
        const auto docTerms = terms(chunk.value("text", std::string()));
        if (docTerms.empty()) continue;
        std::unordered_map<std::string, int> freq;
        for (const auto& term : docTerms) ++freq[term];
        double score = 0.0;
        for (const auto& term : queryTerms) {
            const auto found = freq.find(term);
            if (found != freq.end()) score += 2.0 + std::min(found->second, 3) * 0.35;
        }
        if (score <= 0) continue;
        std::string snippet = chunk.value("text", std::string());
        if (snippet.size() > 640) snippet.resize(640);
        ranked.push_back({{{"source_id", chunk.value("source_id", std::string())}, {"path", chunk.value("path", std::string())},
                          {"chunk", chunk.value("chunk", 0)}, {"score", score}, {"snippet", snippet}}, score});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.score > b.score; });
    json result = json::array();
    for (const auto& item : ranked) {
        if (result.size() >= limit) break;
        result.push_back(item.value);
    }
    return result;
}
