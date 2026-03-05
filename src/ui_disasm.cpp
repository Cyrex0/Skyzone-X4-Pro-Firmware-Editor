// ═══════════════════════════════════════════════════════════════════════
//  ui_disasm.cpp — Disassembler / Decompiler / Annotation UI
//  IDA-style pseudocode + PDB-style function naming
//  Threaded disassembly with progress bar
// ═══════════════════════════════════════════════════════════════════════
#include "firmware.h"
#include "firmware_a.h"
#include "disasm.h"
#include "annotations.h"
#include "decompile.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>
#include <atomic>

// ── Colours ──────────────────────────────────────────────────────────
static const ImVec4 COL_HEADER    = {0.40f, 0.75f, 1.00f, 1.0f};
static const ImVec4 COL_DIM       = {0.50f, 0.50f, 0.55f, 1.0f};
static const ImVec4 COL_ADDR      = {0.55f, 0.55f, 0.65f, 1.0f};
static const ImVec4 COL_MNEMONIC  = {0.35f, 0.80f, 0.90f, 1.0f};
static const ImVec4 COL_OPERAND   = {0.90f, 0.90f, 0.80f, 1.0f};
static const ImVec4 COL_COMMENT   = {0.35f, 0.70f, 0.35f, 1.0f};
static const ImVec4 COL_LABEL     = {0.90f, 0.80f, 0.30f, 1.0f};
static const ImVec4 COL_CALL      = {0.90f, 0.55f, 0.20f, 1.0f};
static const ImVec4 COL_PSEUDO    = {0.85f, 0.85f, 0.75f, 1.0f};
static const ImVec4 COL_SECTION   = {0.90f, 0.75f, 0.30f, 1.0f};
static const ImVec4 COL_GREEN     = {0.30f, 0.85f, 0.45f, 1.0f};
static const ImVec4 COL_KEYWORD   = {0.55f, 0.55f, 0.95f, 1.0f};

// ── State ────────────────────────────────────────────────────────────
enum DisasmTarget { DTGT_B_8051 = 0, DTGT_A_ARM = 1 };
static int         g_target          = DTGT_B_8051;
static bool        g_disasm_done     = false;
static int         g_sel_func        = -1;
static bool        g_show_pseudo     = true;
static bool        g_show_xrefs      = true;
static char        g_func_filter[64] = "";

// Rename popup state
static bool        g_rename_open     = false;
static int         g_rename_func_idx = -1;
static char        g_rename_buf[128] = "";

// Comment popup state
static bool        g_comment_open    = false;
static uint32_t    g_comment_addr    = 0;
static char        g_comment_buf[256]= "";

// PDB file path
static char        g_pdb_path[512]   = "";

// Cached pseudocode
static std::vector<PseudoLine> g_pseudo_cache;
static int g_pseudo_cache_func = -2;

// ── Threading state ──────────────────────────────────────────────────
static std::atomic<float> g_disasm_progress{0.0f};
static std::atomic<bool>  g_disasm_running{false};
static std::thread        g_disasm_thread;
static bool               g_disasm_pending = false; // thread launched, waiting for completion
static int                g_disasm_thread_target = -1; // which target the thread is for

// ── Public API ───────────────────────────────────────────────────────
void ResetDisasmState() {
    // Wait for any running thread to finish
    if (g_disasm_thread.joinable()) {
        // Can't cancel it, just wait
        g_disasm_thread.join();
    }
    g_disasm_done     = false;
    g_disasm_pending  = false;
    g_disasm_running  = false;
    g_disasm_progress = 0.0f;
    g_sel_func        = -1;
    g_pseudo_cache_func = -2;
    g_pseudo_cache.clear();
    g_func_filter[0]  = 0;
}

bool IsDisasmRunning() {
    return g_disasm_running.load();
}

// ── Helpers ──────────────────────────────────────────────────────────
static bool IsCallMnemonic(const std::string& m) {
    return m == "LCALL" || m == "ACALL" || m == "BL" || m == "BLX";
}
static bool IsJumpMnemonic(const std::string& m) {
    return m == "LJMP" || m == "AJMP" || m == "SJMP" || m == "JMP" ||
           m == "JZ" || m == "JNZ" || m == "JC" || m == "JNC" ||
           m == "JB" || m == "JNB" || m == "JBC" || m == "CJNE" || m == "DJNZ" ||
           m == "B" || m == "BEQ" || m == "BNE" || m == "BCS" || m == "BCC" ||
           m == "BMI" || m == "BPL" || m == "BVS" || m == "BVC" ||
           m == "BHI" || m == "BLS" || m == "BGE" || m == "BLT" || m == "BGT" || m == "BLE";
}

// ═══════════════════════════════════════════════════════════════════════
//  Function list panel (left side)
// ═══════════════════════════════════════════════════════════════════════
static void DrawFunctionList(std::vector<Function>& funcs, AnnotationDB& db) {
    ImGui::TextColored(COL_HEADER, "Functions (%zu)", funcs.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ffilt", "Filter...", g_func_filter, sizeof(g_func_filter));

    if (ImGui::BeginChild("##flist", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        for (int i = 0; i < (int)funcs.size(); ++i) {
            auto& f = funcs[i];
            if (g_func_filter[0]) {
                std::string hay = f.name;
                std::string needle(g_func_filter);
                std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (hay.find(needle) == std::string::npos) continue;
            }

            bool sel = (g_sel_func == i);
            char label[256];
            snprintf(label, sizeof(label), "0x%04X  %s (%zu)",
                     f.start, f.name.c_str(), f.lines.size());

            bool renamed = db.func_names.count(f.start) > 0;
            if (renamed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CALL);

            if (ImGui::Selectable(label, sel)) {
                g_sel_func = i;
                g_pseudo_cache_func = -2;
            }

            if (renamed) ImGui::PopStyleColor();

            // Right-click context menu
            if (ImGui::BeginPopupContextItem(("##fctx" + std::to_string(i)).c_str())) {
                if (ImGui::MenuItem("Rename Function...")) {
                    g_rename_func_idx = i;
                    strncpy(g_rename_buf, f.name.c_str(), sizeof(g_rename_buf) - 1);
                    g_rename_buf[sizeof(g_rename_buf) - 1] = 0;
                    g_rename_open = true;
                }
                if (ImGui::MenuItem("Set Comment...")) {
                    g_comment_addr = f.start;
                    auto cit = db.func_comments.find(f.start);
                    if (cit != db.func_comments.end())
                        strncpy(g_comment_buf, cit->second.c_str(), sizeof(g_comment_buf) - 1);
                    else g_comment_buf[0] = 0;
                    g_comment_open = true;
                }
                if (renamed && ImGui::MenuItem("Reset Name")) {
                    db.func_names.erase(f.start);
                    char buf[32]; snprintf(buf, sizeof(buf), "sub_%04X", f.start);
                    f.name = buf;
                }
                ImGui::EndPopup();
            }

            // Comment tooltip
            auto cit = db.func_comments.find(f.start);
            if (cit != db.func_comments.end() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", cit->second.c_str());
            }
        }
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════════════
//  Assembly listing panel
// ═══════════════════════════════════════════════════════════════════════
static void DrawCodeListing(std::vector<Function>& funcs, AnnotationDB& db) {
    if (g_sel_func < 0 || g_sel_func >= (int)funcs.size()) {
        ImGui::TextColored(COL_DIM, "Select a function from the list.");
        return;
    }
    auto& f = funcs[g_sel_func];
    auto cit = db.func_comments.find(f.start);

    ImGui::TextColored(COL_HEADER, "%s", f.name.c_str());
    if (cit != db.func_comments.end()) {
        ImGui::SameLine();
        ImGui::TextColored(COL_COMMENT, "// %s", cit->second.c_str());
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##asm", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin((int)f.lines.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                auto& ln = f.lines[row];
                ImGui::PushID(row);

                // Label
                auto lit = db.labels.find(ln.address);
                if (lit != db.labels.end()) {
                    ImGui::TextColored(COL_LABEL, "%s:", lit->second.c_str());
                }

                // Address
                ImGui::TextColored(COL_ADDR, "%04X: ", ln.address);
                ImGui::SameLine();

                // Bytes
                char bytes[32] = "";
                int blen = 0;
                for (auto b : ln.raw)
                    blen += snprintf(bytes + blen, sizeof(bytes) - blen, "%02X ", b);
                ImGui::TextColored(COL_DIM, "%-12s", bytes);
                ImGui::SameLine();

                // Mnemonic
                if (IsCallMnemonic(ln.mnemonic))
                    ImGui::TextColored(COL_CALL, "%-8s", ln.mnemonic.c_str());
                else if (IsJumpMnemonic(ln.mnemonic))
                    ImGui::TextColored(COL_LABEL, "%-8s", ln.mnemonic.c_str());
                else
                    ImGui::TextColored(COL_MNEMONIC, "%-8s", ln.mnemonic.c_str());
                ImGui::SameLine();

                // Operands — resolve call targets
                std::string ops = ln.operands;
                if (IsCallMnemonic(ln.mnemonic) && !ln.operands.empty()) {
                    uint32_t tgt = (uint32_t)strtol(ln.operands.c_str(), nullptr, 16);
                    std::string n = db.GetFuncName(tgt);
                    if (!n.empty()) ops = n;
                }
                ImGui::TextColored(COL_OPERAND, "%s", ops.c_str());

                // Inline comment
                auto ait = db.addr_comments.find(ln.address);
                if (ait != db.addr_comments.end()) {
                    ImGui::SameLine();
                    ImGui::TextColored(COL_COMMENT, "  ; %s", ait->second.c_str());
                } else if (!ln.comment.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(COL_COMMENT, "  ; %s", ln.comment.c_str());
                }

                // Right-click for comments/labels
                if (ImGui::BeginPopupContextItem("##lctx")) {
                    if (ImGui::MenuItem("Set Comment...")) {
                        g_comment_addr = ln.address;
                        auto ait2 = db.addr_comments.find(ln.address);
                        if (ait2 != db.addr_comments.end())
                            strncpy(g_comment_buf, ait2->second.c_str(), sizeof(g_comment_buf) - 1);
                        else g_comment_buf[0] = 0;
                        g_comment_open = true;
                    }
                    if (ImGui::MenuItem("Set Label...")) {
                        g_comment_addr = ln.address;
                        auto lit2 = db.labels.find(ln.address);
                        if (lit2 != db.labels.end())
                            strncpy(g_comment_buf, lit2->second.c_str(), sizeof(g_comment_buf) - 1);
                        else {
                            char buf[32]; snprintf(buf, sizeof(buf), "loc_%04X", ln.address);
                            strncpy(g_comment_buf, buf, sizeof(g_comment_buf) - 1);
                        }
                        g_comment_buf[sizeof(g_comment_buf) - 1] = 0;
                        g_comment_open = true;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════════════
//  Pseudocode panel (IDA-style decompiler output)
// ═══════════════════════════════════════════════════════════════════════
static void DrawPseudocode(std::vector<Function>& funcs, std::vector<DisasmLine>& allLines,
                           AnnotationDB& db, bool is8051) {
    if (g_sel_func < 0 || g_sel_func >= (int)funcs.size()) {
        ImGui::TextColored(COL_DIM, "Select a function to see pseudocode.");
        return;
    }
    auto& f = funcs[g_sel_func];

    // Cache pseudocode (regenerate on function change)
    if (g_pseudo_cache_func != g_sel_func) {
        if (is8051) g_pseudo_cache = Decompile8051(f, allLines, db);
        else        g_pseudo_cache = DecompileThumb(f, allLines, db);
        g_pseudo_cache_func = g_sel_func;
    }

    auto cit = db.func_comments.find(f.start);
    if (cit != db.func_comments.end()) {
        ImGui::TextColored(COL_COMMENT, "/* %s */", cit->second.c_str());
    }

    // Function signature
    ImGui::TextColored(COL_KEYWORD, "void");
    ImGui::SameLine();
    ImGui::TextColored(COL_CALL, "%s", f.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(COL_PSEUDO, "()");
    ImGui::TextColored(COL_PSEUDO, "{");
    ImGui::Separator();

    if (ImGui::BeginChild("##pseudo", ImVec2(0, -2), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        for (auto& pl : g_pseudo_cache) {
            if (pl.is_label) {
                ImGui::TextColored(COL_LABEL, "%s", pl.text.c_str());
                continue;
            }

            // Indent
            if (pl.indent > 0) {
                char pad[64] = "";
                int n = std::min(pl.indent * 4, 60);
                memset(pad, ' ', n); pad[n] = 0;
                ImGui::TextUnformatted(pad);
                ImGui::SameLine(0, 0);
            }

            // Colourise keywords
            if (pl.text.find("if ") == 0) {
                ImGui::TextColored(COL_KEYWORD, "if");
                ImGui::SameLine(0, 0);
                ImGui::TextColored(COL_PSEUDO, "%s", pl.text.c_str() + 2);
            } else if (pl.text.find("goto ") == 0) {
                ImGui::TextColored(COL_KEYWORD, "goto");
                ImGui::SameLine(0, 0);
                ImGui::TextColored(COL_LABEL, "%s", pl.text.c_str() + 4);
            } else if (pl.text.find("return") == 0) {
                ImGui::TextColored(COL_KEYWORD, "return");
                if (pl.text.size() > 6) {
                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(COL_PSEUDO, "%s", pl.text.c_str() + 6);
                }
            } else if (pl.text.find("sub_") != std::string::npos ||
                       pl.text.find("func_") != std::string::npos) {
                ImGui::TextColored(COL_CALL, "%s", pl.text.c_str());
            } else {
                ImGui::TextColored(COL_PSEUDO, "%s", pl.text.c_str());
            }

            // Comment
            if (!pl.comment.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(COL_COMMENT, "// %s", pl.comment.c_str());
            }
        }
    }
    ImGui::EndChild();

    ImGui::TextColored(COL_PSEUDO, "}");
}

// ═══════════════════════════════════════════════════════════════════════
//  XRef panel
// ═══════════════════════════════════════════════════════════════════════
static void DrawXRefs(std::vector<Function>& funcs, std::vector<XRef>& xrefs) {
    if (g_sel_func < 0 || g_sel_func >= (int)funcs.size()) return;
    auto& f = funcs[g_sel_func];

    ImGui::TextColored(COL_HEADER, "XRefs to %s", f.name.c_str());
    ImGui::Separator();

    int count = 0;
    for (auto& xr : xrefs) {
        if (xr.to >= f.start && xr.to <= f.end) {
            const char* type = xr.type == XRefType::CALL ? "CALL" : "JUMP";
            ImGui::TextColored(COL_ADDR, "  0x%04X", xr.from);
            ImGui::SameLine();
            ImGui::TextColored(COL_DIM, "->");
            ImGui::SameLine();
            ImGui::TextColored(COL_ADDR, "0x%04X", xr.to);
            ImGui::SameLine();
            ImGui::TextColored(xr.type == XRefType::CALL ? COL_CALL : COL_LABEL, "  [%s]", type);
            ++count;
        }
    }
    if (count == 0) ImGui::TextColored(COL_DIM, "  (none)");
}

// ═══════════════════════════════════════════════════════════════════════
//  Annotation DB toolbar (PDB-style save/load/share)
// ═══════════════════════════════════════════════════════════════════════
static void DrawAnnotationToolbar(AnnotationDB& db, const char* target_label) {
    ImGui::TextColored(COL_SECTION, "Annotations (%s)", target_label);
    ImGui::SameLine();

    size_t total = db.func_names.size() + db.func_comments.size() +
                   db.addr_comments.size() + db.labels.size();
    if (total > 0) {
        ImGui::TextColored(COL_GREEN, "[%zu entries]", total);
        ImGui::SameLine();
    }

    if (ImGui::SmallButton("Save PDB...")) {
        std::string def = std::string(target_label) + "_annotations.json";
        strncpy(g_pdb_path, def.c_str(), sizeof(g_pdb_path) - 1);
        ImGui::OpenPopup("##pdbsave");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Load PDB...")) {
        ImGui::OpenPopup("##pdbload");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Merge PDB...")) {
        ImGui::OpenPopup("##pdbmerge");
    }
    ImGui::SameLine();
    if (total > 0 && ImGui::SmallButton("Clear All")) {
        db.Clear();
        g_pseudo_cache_func = -2;
    }

    // Save popup
    if (ImGui::BeginPopup("##pdbsave")) {
        ImGui::Text("Save annotations to JSON file:");
        ImGui::SetNextItemWidth(350);
        ImGui::InputText("##path", g_pdb_path, sizeof(g_pdb_path));
        if (ImGui::Button("Save")) {
            db.SaveJSON(g_pdb_path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Load popup
    if (ImGui::BeginPopup("##pdbload")) {
        ImGui::Text("Load annotations from JSON file:");
        ImGui::SetNextItemWidth(350);
        ImGui::InputText("##path", g_pdb_path, sizeof(g_pdb_path));
        if (ImGui::Button("Load")) {
            db.LoadJSON(g_pdb_path);
            g_pseudo_cache_func = -2;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Merge popup
    if (ImGui::BeginPopup("##pdbmerge")) {
        ImGui::Text("Merge annotations from another JSON file:");
        ImGui::TextColored(COL_DIM, "(Existing entries kept, new ones added)");
        ImGui::SetNextItemWidth(350);
        ImGui::InputText("##path", g_pdb_path, sizeof(g_pdb_path));
        if (ImGui::Button("Merge")) {
            AnnotationDB other;
            other.LoadJSON(g_pdb_path);
            db.Merge(other);
            g_pseudo_cache_func = -2;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Rename / Comment popup modals
// ═══════════════════════════════════════════════════════════════════════
static void DrawPopups(std::vector<Function>& funcs, AnnotationDB& db) {
    if (g_rename_open) {
        ImGui::OpenPopup("Rename Function");
        g_rename_open = false;
    }
    if (ImGui::BeginPopupModal("Rename Function", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter new name for function at 0x%04X:",
                    g_rename_func_idx >= 0 ? funcs[g_rename_func_idx].start : 0);
        ImGui::SetNextItemWidth(300);
        bool enter = ImGui::InputText("##rn", g_rename_buf, sizeof(g_rename_buf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("OK")) {
            if (g_rename_func_idx >= 0 && g_rename_buf[0]) {
                auto& f = funcs[g_rename_func_idx];
                db.RenameFunction(f.start, g_rename_buf);
                f.name = g_rename_buf;
                g_pseudo_cache_func = -2;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_comment_open) {
        ImGui::OpenPopup("Set Comment");
        g_comment_open = false;
    }
    if (ImGui::BeginPopupModal("Set Comment", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Comment for address 0x%04X:", g_comment_addr);
        ImGui::SetNextItemWidth(350);
        bool enter = ImGui::InputText("##cm", g_comment_buf, sizeof(g_comment_buf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("OK")) {
            if (g_comment_buf[0])
                db.SetAddrComment(g_comment_addr, g_comment_buf);
            else
                db.addr_comments.erase(g_comment_addr);
            g_pseudo_cache_func = -2;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Statistics panel
// ═══════════════════════════════════════════════════════════════════════
static void DrawStats(std::vector<Function>& funcs, std::vector<DisasmLine>& lines,
                      std::vector<XRef>& xrefs) {
    ImGui::TextColored(COL_HEADER, "Disassembly Statistics");
    ImGui::Separator();
    ImGui::Text("Total instructions: %zu", lines.size());
    ImGui::Text("Functions found:    %zu", funcs.size());
    ImGui::Text("Cross-references:   %zu", xrefs.size());

    if (!funcs.empty()) {
        size_t total_inst = 0, largest = 0;
        for (auto& f : funcs) {
            total_inst += f.lines.size();
            if (f.lines.size() > largest) largest = f.lines.size();
        }
        ImGui::Text("Avg func size:      %zu instructions", total_inst / funcs.size());
        ImGui::Text("Largest function:   %zu instructions", largest);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Progress bar overlay — shown while disassembler thread runs
// ═══════════════════════════════════════════════════════════════════════
static void DrawProgressBar() {
    float prog = g_disasm_progress.load();

    ImGui::Spacing();
    ImGui::Spacing();

    // Centered progress area
    float avail = ImGui::GetContentRegionAvail().x;
    float bar_w = std::min(500.0f, avail - 40.0f);
    float indent = (avail - bar_w) * 0.5f;
    if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    // Phase label
    const char* phase = "Disassembling...";
    if (prog >= 0.92f)      phase = "Annotating strings...";
    else if (prog >= 0.82f) phase = "Detecting functions...";

    ImGui::TextColored(COL_HEADER, "%s", phase);

    if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::ProgressBar(prog, ImVec2(bar_w, 24));

    // Percentage text
    if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::TextColored(COL_DIM, "%.0f%% complete", prog * 100.0f);

    ImGui::Spacing();
    if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::TextColored(COL_DIM, "Please wait, UI will resume when finished.");
}

// ═══════════════════════════════════════════════════════════════════════
//  Main Disassembler UI — called from DrawEditorUI
// ═══════════════════════════════════════════════════════════════════════
void DrawDisasmUI(FirmwareBBoard& bb, FirmwareABoard& ab, Firmware040B& b040,
                  Disasm8051& dis8051, DisasmThumb& disThumb,
                  AnnotationDB& db8051, AnnotationDB& dbArm)
{
    // ── Check for completed background disassembly ───────────────────
    if (g_disasm_pending && !g_disasm_running.load()) {
        if (g_disasm_thread.joinable()) g_disasm_thread.join();

        // Apply annotations now that data is ready (main thread only)
        if (g_disasm_thread_target == DTGT_B_8051) {
            db8051.ApplyToFunctions(dis8051.Functions());
        } else {
            dbArm.ApplyToFunctions(disThumb.Functions());
        }
        g_disasm_done    = true;
        g_disasm_pending = false;
        g_sel_func       = -1;
        g_pseudo_cache_func = -2;
    }

    // ── Target selector + action bar ─────────────────────────────────
    ImGui::TextColored(COL_HEADER, "Target:");
    ImGui::SameLine();
    // Disable target switching while thread is running
    if (g_disasm_running.load()) ImGui::BeginDisabled();
    ImGui::RadioButton("B Board (8051)", &g_target, DTGT_B_8051);
    ImGui::SameLine();
    ImGui::RadioButton("A Board (ARM)",  &g_target, DTGT_A_ARM);
    if (g_disasm_running.load()) ImGui::EndDisabled();
    ImGui::SameLine();

    bool can_disasm = false;
    if (g_target == DTGT_B_8051)
        can_disasm = bb.IsLoaded() || b040.IsLoaded();
    else
        can_disasm = ab.IsLoaded();

    // ── Disassemble button — launches worker thread ──────────────────
    if (can_disasm && !g_disasm_running.load()) {
        if (ImGui::Button("Disassemble")) {
            // Clean up any previous thread
            if (g_disasm_thread.joinable()) g_disasm_thread.join();

            g_disasm_done       = false;
            g_disasm_pending    = true;
            g_disasm_progress   = 0.0f;
            g_disasm_running    = true;
            g_disasm_thread_target = g_target;

            if (g_target == DTGT_B_8051) {
                db8051.target = "8051";
                // Capture pointer to data (firmware globals, lifetime safe)
                const std::vector<uint8_t>* pdata =
                    b040.IsLoaded() ? &b040.Decoded() : &bb.Data();
                g_disasm_thread = std::thread([&dis8051, pdata]() {
                    dis8051.SetProgressCallback([](float p) {
                        g_disasm_progress.store(p);
                    });
                    dis8051.Disassemble(*pdata);
                    dis8051.SetProgressCallback(nullptr);
                    g_disasm_running.store(false);
                });
            } else {
                dbArm.target = "arm";
                const std::vector<uint8_t>* pdata = &ab.Decoded();
                g_disasm_thread = std::thread([&disThumb, pdata]() {
                    disThumb.SetProgressCallback([](float p) {
                        g_disasm_progress.store(p);
                    });
                    disThumb.Disassemble(*pdata);
                    disThumb.SetProgressCallback(nullptr);
                    g_disasm_running.store(false);
                });
            }
        }
    } else if (g_disasm_running.load()) {
        ImGui::BeginDisabled();
        ImGui::Button("Disassembling...");
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Disassemble");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(COL_DIM, "(Load firmware first)");
    }

    // View toggles (only when not running)
    if (!g_disasm_running.load()) {
        ImGui::SameLine();
        ImGui::Checkbox("Pseudocode", &g_show_pseudo);
        ImGui::SameLine();
        ImGui::Checkbox("XRefs", &g_show_xrefs);
    }

    // ── Progress bar while disassembling ─────────────────────────────
    if (g_disasm_running.load()) {
        DrawProgressBar();
        return; // Don't draw the rest of the UI while working
    }

    // ── Get current disasm state (safe — thread not running) ─────────
    bool is8051 = (g_target == DTGT_B_8051);
    auto& funcs  = is8051 ? dis8051.Functions()  : disThumb.Functions();
    auto& lines  = is8051 ? dis8051.Lines()      : disThumb.Lines();
    auto& xrefs  = is8051 ? dis8051.XRefs()      : disThumb.XRefs();
    auto& db     = is8051 ? db8051               : dbArm;
    const char* tgt_label = is8051 ? "8051" : "ARM";

    if (!g_disasm_done || funcs.empty()) {
        ImGui::Separator();
        ImGui::TextColored(COL_DIM, "No disassembly data. Click 'Disassemble' to begin.");
        return;
    }

    // Annotation toolbar
    DrawAnnotationToolbar(db, tgt_label);
    ImGui::Separator();

    // ── Main layout: functions | assembly | pseudocode ───────────────
    float avail_w = ImGui::GetContentRegionAvail().x;
    float func_w  = 250.0f;
    float pseudo_w = g_show_pseudo ? avail_w * 0.35f : 0;
    float asm_w   = avail_w - func_w - pseudo_w - 20;
    float panel_h = ImGui::GetContentRegionAvail().y - (g_show_xrefs ? 140 : 10);

    // Functions panel
    ImGui::BeginGroup();
    ImGui::BeginChild("##func_panel", ImVec2(func_w, panel_h));
    DrawFunctionList(funcs, db);
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine();

    // Assembly panel
    ImGui::BeginGroup();
    ImGui::BeginChild("##asm_panel", ImVec2(asm_w, panel_h), ImGuiChildFlags_Border);
    DrawCodeListing(funcs, db);
    ImGui::EndChild();
    ImGui::EndGroup();

    // Pseudocode panel
    if (g_show_pseudo) {
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::BeginChild("##pseudo_panel", ImVec2(pseudo_w, panel_h), ImGuiChildFlags_Border);
        ImGui::TextColored(COL_SECTION, "Pseudocode");
        ImGui::Separator();
        DrawPseudocode(funcs, lines, db, is8051);
        ImGui::EndChild();
        ImGui::EndGroup();
    }

    // XRefs at bottom
    if (g_show_xrefs) {
        ImGui::BeginChild("##xref_panel", ImVec2(0, 130), ImGuiChildFlags_Border);
        if (ImGui::BeginTabBar("##xstats")) {
            if (ImGui::BeginTabItem("XRefs")) {
                DrawXRefs(funcs, xrefs);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Statistics")) {
                DrawStats(funcs, lines, xrefs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    // Draw modal popups
    DrawPopups(funcs, db);
}
