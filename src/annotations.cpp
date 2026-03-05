// ═══════════════════════════════════════════════════════════════════════
//  annotations.cpp — PDB-style annotation database (JSON I/O)
// ═══════════════════════════════════════════════════════════════════════
#include "annotations.h"
#include "disasm.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>

// ── Mutators ────────────────────────────────────────────────────────
void AnnotationDB::RenameFunction(uint32_t addr, const std::string& name) {
    if (name.empty()) func_names.erase(addr);
    else func_names[addr] = name;
}
void AnnotationDB::SetFuncComment(uint32_t addr, const std::string& c) {
    if (c.empty()) func_comments.erase(addr);
    else func_comments[addr] = c;
}
void AnnotationDB::SetAddrComment(uint32_t addr, const std::string& c) {
    if (c.empty()) addr_comments.erase(addr);
    else addr_comments[addr] = c;
}
void AnnotationDB::SetLabel(uint32_t addr, const std::string& l) {
    if (l.empty()) labels.erase(addr);
    else labels[addr] = l;
}

std::string AnnotationDB::GetFuncName(uint32_t addr, const char* fallback) const {
    auto it = func_names.find(addr);
    if (it != func_names.end()) return it->second;
    if (fallback) return fallback;
    char buf[32]; snprintf(buf, sizeof(buf), "sub_%X", addr);
    return buf;
}

std::string AnnotationDB::GetAddrComment(uint32_t addr) const {
    auto it = addr_comments.find(addr);
    return it != addr_comments.end() ? it->second : "";
}

void AnnotationDB::ApplyToFunctions(std::vector<Function>& funcs) const {
    for (auto& f : funcs) {
        auto it = func_names.find(f.start);
        if (it != func_names.end()) f.name = it->second;
    }
}

void AnnotationDB::Merge(const AnnotationDB& other) {
    for (auto& [k, v] : other.func_names)   func_names[k]   = v;
    for (auto& [k, v] : other.func_comments) func_comments[k] = v;
    for (auto& [k, v] : other.addr_comments) addr_comments[k] = v;
    for (auto& [k, v] : other.labels)        labels[k]        = v;
}

void AnnotationDB::Clear() {
    func_names.clear(); func_comments.clear();
    addr_comments.clear(); labels.clear();
    target.clear(); version = 1;
}

bool AnnotationDB::Empty() const {
    return func_names.empty() && func_comments.empty() &&
           addr_comments.empty() && labels.empty();
}

// ═══════════════════════════════════════════════════════════════════════
//  Minimal JSON serialization (no external dependency)
// ═══════════════════════════════════════════════════════════════════════
static std::string EscJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static void WriteMap(std::ofstream& f, const char* key,
                     const std::unordered_map<uint32_t, std::string>& m, bool last = false) {
    f << "  \"" << key << "\": {\n";
    size_t n = 0;
    for (auto& [addr, val] : m) {
        char astr[16]; snprintf(astr, sizeof(astr), "0x%X", addr);
        f << "    \"" << astr << "\": \"" << EscJSON(val) << "\"";
        if (++n < m.size()) f << ",";
        f << "\n";
    }
    f << "  }" << (last ? "" : ",") << "\n";
}

bool AnnotationDB::SaveJSON(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "{\n";
    f << "  \"version\": " << version << ",\n";
    f << "  \"target\": \"" << EscJSON(target) << "\",\n";
    WriteMap(f, "func_names",    func_names);
    WriteMap(f, "func_comments", func_comments);
    WriteMap(f, "addr_comments", addr_comments);
    WriteMap(f, "labels",        labels, true);
    f << "}\n";
    return true;
}

// ── Simple JSON parser (handles our known schema) ────────────────────
static std::string UnescJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '\"': out += '\"'; ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 'r':  out += '\r'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string ExtractString(const std::string& line, size_t start) {
    auto q1 = line.find('\"', start);
    if (q1 == std::string::npos) return "";
    auto q2 = line.find('\"', q1 + 1);
    // Handle escaped quotes
    while (q2 != std::string::npos && q2 > 0 && line[q2 - 1] == '\\')
        q2 = line.find('\"', q2 + 1);
    if (q2 == std::string::npos) return "";
    return UnescJSON(line.substr(q1 + 1, q2 - q1 - 1));
}

bool AnnotationDB::LoadJSON(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    Clear();

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Parse target
    auto tpos = content.find("\"target\"");
    if (tpos != std::string::npos) target = ExtractString(content, tpos + 8);

    // Parse version
    auto vpos = content.find("\"version\"");
    if (vpos != std::string::npos) {
        auto colon = content.find(':', vpos);
        if (colon != std::string::npos) version = std::atoi(content.c_str() + colon + 1);
    }

    // Parse each map section
    auto ParseMap = [&](const char* key, std::unordered_map<uint32_t, std::string>& m) {
        auto kpos = content.find(std::string("\"") + key + "\"");
        if (kpos == std::string::npos) return;
        auto brace = content.find('{', kpos);
        if (brace == std::string::npos) return;
        auto end_brace = content.find('}', brace);
        if (end_brace == std::string::npos) return;

        std::string section = content.substr(brace + 1, end_brace - brace - 1);
        // Find all "0xABC": "value" pairs
        size_t pos = 0;
        while (pos < section.size()) {
            auto q1 = section.find('\"', pos);
            if (q1 == std::string::npos) break;
            auto q2 = section.find('\"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string addr_str = section.substr(q1 + 1, q2 - q1 - 1);
            uint32_t addr = (uint32_t)strtoul(addr_str.c_str(), nullptr, 0);

            std::string val = ExtractString(section, q2 + 1);
            if (!val.empty()) m[addr] = val;

            // Advance past value
            auto next_q = section.find('\"', q2 + 1);
            if (next_q == std::string::npos) break;
            next_q = section.find('\"', next_q + 1);
            if (next_q == std::string::npos) break;
            pos = next_q + 1;
        }
    };

    ParseMap("func_names",    func_names);
    ParseMap("func_comments", func_comments);
    ParseMap("addr_comments", addr_comments);
    ParseMap("labels",        labels);
    return true;
}
