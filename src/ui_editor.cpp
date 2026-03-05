// ═══════════════════════════════════════════════════════════════════════
//  ui_editor.cpp — Firmware editor ImGui panels
//  Unified B Board tab auto-detects X4Pro vs 040 Pro
//  v2.0 — Latency dashboard, improved presets, user-friendly tooltips
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
#include <cmath>

// ── Forward declarations ─────────────────────────────────────────────
// -- Patch diff accessors (called from main.cpp save handler) --
static std::vector<uint8_t> s_bb_orig, s_ab_orig;

const std::vector<uint8_t>& GetBBOriginal() { return s_bb_orig; }
const std::vector<uint8_t>& GetABOriginal() { return s_ab_orig; }

void DrawEditorUI(FirmwareBBoard& bb, FirmwareABoard& ab, Firmware040B& b040,
                  Disasm8051& dis8051, DisasmThumb& disThumb,
                  AnnotationDB& db8051, AnnotationDB& dbArm);
void DrawDisasmUI(FirmwareBBoard& bb, FirmwareABoard& ab, Firmware040B& b040,
                  Disasm8051& dis8051, DisasmThumb& disThumb,
                  AnnotationDB& db8051, AnnotationDB& dbArm);

// ── Helpers ──────────────────────────────────────────────────────────
static const ImVec4 COL_HEADER  = {0.40f, 0.75f, 1.00f, 1.0f};
static const ImVec4 COL_CHANGED = {1.00f, 0.55f, 0.20f, 1.0f};
static const ImVec4 COL_DIM     = {0.50f, 0.50f, 0.55f, 1.0f};
static const ImVec4 COL_GREEN   = {0.30f, 0.85f, 0.45f, 1.0f};
static const ImVec4 COL_SECTION = {0.90f, 0.75f, 0.30f, 1.0f};
static const ImVec4 COL_RED     = {1.00f, 0.35f, 0.30f, 1.0f};
static const ImVec4 COL_YELLOW  = {1.00f, 0.85f, 0.20f, 1.0f};
static const ImVec4 COL_CYAN    = {0.30f, 0.90f, 0.90f, 1.0f};

static void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Latency bar helper — draws a colored horizontal bar
static void LatencyBar(const char* label, float value_us, float max_us, const char* detail) {
    ImGui::Text("%-22s", label);
    ImGui::SameLine(200);

    float frac = (max_us > 0) ? std::min(value_us / max_us, 1.0f) : 0.0f;
    ImVec4 col;
    if (value_us < 100)       col = COL_GREEN;
    else if (value_us < 500)  col = COL_YELLOW;
    else                      col = COL_RED;

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
    char overlay[64];
    if (value_us >= 1000)
        snprintf(overlay, sizeof(overlay), "%.1f ms", value_us / 1000.0f);
    else
        snprintf(overlay, sizeof(overlay), "%.0f us", value_us);
    ImGui::ProgressBar(frac, ImVec2(200, 0), overlay);
    ImGui::PopStyleColor();

    if (detail && detail[0]) {
        ImGui::SameLine();
        ImGui::TextColored(COL_DIM, "%s", detail);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Unified B Board — works with X4Pro (raw) or 040 Pro (encoded)
// ═══════════════════════════════════════════════════════════════════════
enum BBoardVariant { BB_NONE = 0, BB_X4PRO, BB_040PRO };

struct BBoardView {
    BBoardVariant       variant = BB_NONE;
    FirmwareBBoard*     bb  = nullptr;
    Firmware040B*       b040 = nullptr;

    bool IsLoaded() const {
        return (variant == BB_X4PRO && bb && bb->IsLoaded()) ||
               (variant == BB_040PRO && b040 && b040->IsLoaded());
    }
    const char* VariantName() const {
        switch (variant) {
            case BB_X4PRO:  return "SKY04X Pro (X4Pro)";
            case BB_040PRO: return "SKY04O Pro (040)";
            default:        return "Unknown";
        }
    }
    const char* DisplaySpec() const {
        switch (variant) {
            case BB_X4PRO:  return "1920x1080 OLED | 52 deg FOV | 720P 100fps HDMI";
            case BB_040PRO: return "1280x720 OLED | 42 deg FOV | 60fps HDMI";
            default:        return "";
        }
    }
    std::string Filename() const {
        if (variant == BB_X4PRO && bb) return bb->Filename();
        if (variant == BB_040PRO && b040) return b040->Filename();
        return "";
    }
    uint32_t DataSize() const {
        if (variant == BB_X4PRO && bb) return (uint32_t)bb->Data().size();
        if (variant == BB_040PRO && b040) return (uint32_t)b040->Decoded().size();
        return 0;
    }
    uint32_t RawSize() const {
        if (variant == BB_X4PRO && bb) return (uint32_t)bb->Data().size();
        if (variant == BB_040PRO && b040) return (uint32_t)b040->Raw().size();
        return 0;
    }
    std::vector<DelayCall>& Delays() {
        return variant == BB_X4PRO ? bb->Delays() : b040->Delays();
    }
    std::vector<InitRegEntry>& InitRegs() {
        return variant == BB_X4PRO ? bb->InitRegs() : b040->InitRegs();
    }
    size_t SubTableCount() const {
        return variant == BB_X4PRO ? bb->SubTableCount() : b040->SubTableCount();
    }
    void SetDelay(DelayCall& d, uint16_t ms) {
        if (variant == BB_X4PRO) bb->SetDelay(d, ms);
        else b040->SetDelay(d, ms);
    }
    void SetInitReg(InitRegEntry& e, uint8_t val) {
        if (variant == BB_X4PRO) bb->SetInitReg(e, val);
        else b040->SetInitReg(e, val);
    }
    const std::vector<uint8_t>& HexData() const {
        if (variant == BB_X4PRO) return bb->Data();
        return b040->Decoded();
    }
    const std::vector<uint8_t>& SaveData() const {
        if (variant == BB_X4PRO) return bb->Data();
        return b040->Raw();
    }
    bool Save(const std::string& path) const {
        if (variant == BB_X4PRO) return bb->Save(path);
        if (variant == BB_040PRO) return b040->Save(path);
        return false;
    }
};

static BBoardView g_bview;
static bool g_bb_orig_dirty = true;
static bool g_ab_orig_dirty = true;

void ResetBOriginals() {
    g_bview.variant = BB_NONE;
    g_bb_orig_dirty = true;
}

void ResetAOriginals() {
    g_ab_orig_dirty = true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Latency estimation helpers
// ═══════════════════════════════════════════════════════════════════════
static const float LINE_TIME_US = 63.5f; // ~63.5μs per line at 15.7kHz (NTSC)

// Find a register value in the init regs, return -1 if not found
static int FindRegValue(std::vector<InitRegEntry>& regs, uint16_t reg_key) {
    for (auto& r : regs) {
        uint16_t rk = ((uint16_t)r.page << 8) | r.index;
        if (rk == reg_key) return r.value;
    }
    return -1;
}

struct LatencyStage {
    const char* name;
    float latency_us;
    const char* detail;
    ImVec4 color;
};

static std::vector<LatencyStage> CalcBBoardLatency(BBoardView& bv) {
    std::vector<LatencyStage> stages;
    if (!bv.IsLoaded()) return stages;
    auto& regs = bv.InitRegs();

    // 1. Decoder — comb filter
    int comb = FindRegValue(regs, 0x10C);
    float comb_lat = 0;
    const char* comb_detail = "2D comb (optimal)";
    if (comb >= 0) {
        if (comb & 0x10) { comb_lat = 16700; comb_detail = "3D COMB — 1 FRAME DELAY!"; }
        else { comb_lat = LINE_TIME_US; comb_detail = "2D comb (1 line)"; }
    }
    stages.push_back({"Comb Filter (0x10C)", comb_lat, comb_detail,
                       comb_lat > 1000 ? COL_RED : COL_GREEN});

    // 2. Decoder — sharpness
    int sharp = FindRegValue(regs, 0x112);
    float sharp_lat = (sharp > 0) ? LINE_TIME_US * 0.5f : 0;
    char sharp_buf[64];
    snprintf(sharp_buf, sizeof(sharp_buf), "value=0x%02X %s",
             sharp >= 0 ? sharp : 0, sharp > 0 ? "(active)" : "(off)");
    stages.push_back({"Dec Sharpness (0x112)", sharp_lat, sharp > 0 ? "active — adds FIR delay" : "off",
                       sharp > 0 ? COL_YELLOW : COL_GREEN});

    // 3. V Peaking
    int vpeak = FindRegValue(regs, 0x117);
    float vpeak_lat = (vpeak > 0) ? LINE_TIME_US : 0;
    stages.push_back({"V Peaking (0x117)", vpeak_lat, vpeak > 0 ? "active — 1 line buffer" : "off",
                       vpeak > 0 ? COL_YELLOW : COL_GREEN});

    // 4. H Filter
    int hfilt = FindRegValue(regs, 0x12C);
    float hfilt_lat = (hfilt > 0) ? LINE_TIME_US * 0.5f : 0;
    stages.push_back({"H Filter (0x12C)", hfilt_lat, hfilt > 0 ? "active" : "bypassed",
                       hfilt > 0 ? COL_YELLOW : COL_GREEN});

    // 5. CTI
    bool cti_active = false;
    for (uint16_t r = 0x120; r <= 0x128; ++r) {
        int v = FindRegValue(regs, r);
        if (v > 0) { cti_active = true; break; }
    }
    if (!cti_active) {
        int va = FindRegValue(regs, 0x12A);
        int vb = FindRegValue(regs, 0x12B);
        if ((va > 0) || (vb > 0)) cti_active = true;
    }
    float cti_lat = cti_active ? LINE_TIME_US : 0;
    stages.push_back({"CTI Engine", cti_lat,
                       cti_active ? "11 coefficients active — 1 line delay" : "all coefficients zero",
                       cti_active ? COL_YELLOW : COL_GREEN});

    // 6. Scaler line buffer
    int lbuf = FindRegValue(regs, 0x20B);
    float lbuf_lat = (lbuf > 0) ? lbuf * LINE_TIME_US : 0;
    char lbuf_buf[64];
    snprintf(lbuf_buf, sizeof(lbuf_buf), "%d lines buffered", lbuf >= 0 ? lbuf : 0);
    stages.push_back({"Line Buffer (0x20B)", lbuf_lat, lbuf_buf,
                       lbuf > 8 ? COL_RED : (lbuf > 2 ? COL_YELLOW : COL_GREEN)});

    // 7. Image adjust (contrast, brightness, sharpness)
    int iac = FindRegValue(regs, 0x284);
    int iab = FindRegValue(regs, 0x28A);
    int ias = FindRegValue(regs, 0x28B);
    float ia_lat = 0;
    std::string ia_detail;
    if (iac >= 0 && iac != 0x80) { ia_lat += 16; ia_detail += "contrast "; }
    if (iab >= 0 && iab != 0x80) { ia_lat += 16; ia_detail += "brightness "; }
    if (ias >= 0 && ias > 0)     { ia_lat += 32; ia_detail += "sharpness "; }
    if (iac < 0 && iab < 0 && ias < 0) ia_detail = "regs missing (040 Pro)";
    else if (ia_lat == 0) ia_detail = "all neutral/off";
    stages.push_back({"Image Adjust", ia_lat, ia_detail.c_str(),
                       ia_lat > 0 ? COL_YELLOW : COL_GREEN});

    // 8. Dither
    int dith = FindRegValue(regs, 0x2E4);
    float dith_lat = (dith > 0) ? 16.0f : 0;
    stages.push_back({"Dither (0x2E4)", dith_lat, dith > 0 ? "spatial dithering on" : "off",
                       dith > 0 ? COL_YELLOW : COL_GREEN});

    return stages;
}

// ═══════════════════════════════════════════════════════════════════════
//  B Board: Latency Dashboard
// ═══════════════════════════════════════════════════════════════════════
static void DrawBLatency(BBoardView& bv) {
    if (!bv.IsLoaded()) {
        ImGui::TextColored(COL_DIM, "Load B firmware to see latency analysis.");
        ImGui::Spacing();
        ImGui::TextWrapped("The Latency Dashboard shows a per-stage breakdown of the "
                           "TW8836 video processing pipeline delay, helping you identify "
                           "and minimize every source of latency.");
        return;
    }

    ImGui::TextColored(COL_HEADER, "B Board Latency Analysis — %s", bv.VariantName());
    ImGui::SameLine();
    HelpMarker("This dashboard estimates the processing latency added by each stage\n"
               "of the TW8836 video pipeline. Green = minimal, Yellow = moderate,\n"
               "Red = high latency. The line buffer (0x20B) and comb filter (0x10C)\n"
               "are typically the biggest contributors.\n\n"
               "NOTE: These are init-table values. Runtime firmware may change some\n"
               "registers (e.g., SC_LINEBUF_DLY is often set higher for CVBS mode).");
    ImGui::Separator();

    auto stages = CalcBBoardLatency(bv);
    float total = 0;
    for (auto& s : stages) total += s.latency_us;

    // ── Total latency summary ──
    ImGui::Spacing();
    ImVec4 total_col = total < 500 ? COL_GREEN : (total < 2000 ? COL_YELLOW : COL_RED);
    ImGui::TextColored(total_col, "Estimated Processing Latency: ");
    ImGui::SameLine();
    if (total >= 1000)
        ImGui::TextColored(total_col, "%.2f ms", total / 1000.0f);
    else
        ImGui::TextColored(total_col, "%.0f us", total);

    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "(B board pipeline only — add A board frame period for total)");
    ImGui::Spacing();

    // ── Quick action buttons ──
    ImGui::TextColored(COL_SECTION, "Quick Actions:");
    ImGui::SameLine();
    HelpMarker("These buttons apply targeted optimizations to specific pipeline stages.\n"
               "Use 'Image Quality' tab for full control over individual registers.");

    auto& regs = bv.InitRegs();
    auto low_lat = GetLowLatencyRegs();

    if (ImGui::Button("Minimize All Latency")) {
        int applied = 0, skipped = 0;
        for (auto& ll : low_lat) {
            bool found = false;
            for (auto& r : regs) {
                uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                if (rk == ll.reg) {
                    r.value = ll.low_lat;
                    bv.SetInitReg(r, ll.low_lat);
                    found = true; applied++; break;
                }
            }
            if (!found) skipped++;
        }
        if (skipped > 0) ImGui::OpenPopup("PresetResult");
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Stock")) {
        for (auto& r : regs) { r.value = r.original; bv.SetInitReg(r, r.original); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Line Buffer Only")) {
        for (auto& r : regs) {
            uint16_t rk = ((uint16_t)r.page << 8) | r.index;
            if (rk == 0x20B) { r.value = 0x02; bv.SetInitReg(r, 0x02); break; }
        }
    }
    ImGui::SameLine();
    HelpMarker("Sets SC_LINEBUF_DLY to 2 lines — biggest single latency reduction\n"
               "(saves ~0.9ms) with minimal quality impact.");

    // Preset result popup (shows skipped registers for 040 Pro)
    if (ImGui::BeginPopupModal("PresetResult", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(COL_YELLOW, "Low Latency Preset Applied");
        ImGui::Separator();

        int applied = 0, skipped = 0;
        for (auto& ll : low_lat) {
            bool found = false;
            for (auto& r : regs) {
                uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                if (rk == ll.reg) { found = true; applied++; break; }
            }
            if (!found) skipped++;
        }

        ImGui::Text("Registers modified: %d / %zu", applied, low_lat.size());
        if (skipped > 0) {
            ImGui::Spacing();
            ImGui::TextColored(COL_RED, "Skipped %d register(s) — not in %s init table:",
                               skipped, bv.variant == BB_040PRO ? "040 Pro" : "X4Pro");
            for (auto& ll : low_lat) {
                bool found = false;
                for (auto& r : regs) {
                    uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                    if (rk == ll.reg) { found = true; break; }
                }
                if (!found) {
                    auto& ri = GetRegisterInfo();
                    auto it = ri.find(ll.reg);
                    ImGui::BulletText("0x%03X  %s  —  %s", ll.reg,
                                      it != ri.end() ? it->second.name : "???", ll.desc);
                }
            }
            ImGui::Spacing();
            ImGui::TextColored(COL_DIM, "These registers are not in the firmware's init table.\n"
                                        "They use hardware defaults and cannot be modified here.");
        } else {
            ImGui::TextColored(COL_GREEN, "All registers applied successfully!");
        }

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Per-stage breakdown ──
    ImGui::TextColored(COL_SECTION, "Pipeline Stage Breakdown:");
    ImGui::Spacing();

    float max_stage = 2000; // scale for progress bars
    for (auto& s : stages) {
        if (s.latency_us > max_stage) max_stage = s.latency_us * 1.2f;
    }

    for (auto& s : stages) {
        ImGui::PushStyleColor(ImGuiCol_Text, s.color);
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        LatencyBar(s.name, s.latency_us, max_stage, s.detail);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Warnings ──
    ImGui::TextColored(COL_SECTION, "Warnings & Recommendations:");
    ImGui::Spacing();

    int comb = FindRegValue(regs, 0x10C);
    if (comb >= 0 && (comb & 0x10)) {
        ImGui::TextColored(COL_RED, "!! CRITICAL: 3D Comb filter enabled (DEC_CNTRL1 = 0x%02X)", comb);
        ImGui::TextWrapped("   3D comb filtering adds a FULL FRAME of delay (~16.7ms). "
                           "Change REG 0x10C bit4 to 0 (value 0xCC) for 2D mode.");
    }

    int lbuf = FindRegValue(regs, 0x20B);
    if (lbuf > 8) {
        ImGui::TextColored(COL_YELLOW, "Line buffer set to %d lines (0x%02X) — %.1f ms delay",
                           lbuf, lbuf, lbuf * LINE_TIME_US / 1000.0f);
        ImGui::TextWrapped("   Reduce to 2 (0x02) for minimum latency. Values below 2 may "
                           "cause scaler artifacts.");
    }

    if (bv.variant == BB_040PRO) {
        int iac = FindRegValue(regs, 0x284);
        int iab = FindRegValue(regs, 0x28A);
        int ias = FindRegValue(regs, 0x28B);
        if (iac < 0 || iab < 0 || ias < 0) {
            ImGui::TextColored(COL_CYAN, "040 Pro: Image Adjust registers (0x284, 0x28A, 0x28B) "
                                         "not in init table.");
            ImGui::TextWrapped("   These use hardware defaults. Cannot be modified through "
                               "this editor. This only accounts for ~64us of latency.");
        }
    }

    bool all_optimal = true;
    for (auto& s : stages) {
        if (s.latency_us > 100) { all_optimal = false; break; }
    }
    if (all_optimal) {
        ImGui::Spacing();
        ImGui::TextColored(COL_GREEN, "All stages are at or near minimum latency!");
    }

    // ── Total system latency estimate ──
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(COL_SECTION, "Estimated Total System Latency:");
    ImGui::Spacing();

    float b_total = total;
    ImGui::Text("B Board processing:  ");
    ImGui::SameLine(220);
    ImGui::TextColored(total_col, "%.2f ms", b_total / 1000.0f);

    ImGui::Text("A Board @ 100fps:    ");
    ImGui::SameLine(220);
    ImGui::Text("%.1f ms frame period", 10000.0f / 1000.0f);
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "= %.1f ms total", (b_total + 10000) / 1000.0f);

    ImGui::Text("A Board @ 120fps:    ");
    ImGui::SameLine(220);
    ImGui::Text("%.1f ms frame period", 8333.0f / 1000.0f);
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "= %.1f ms total", (b_total + 8333) / 1000.0f);

    ImGui::Text("A Board @ 144fps:    ");
    ImGui::SameLine(220);
    ImGui::Text("%.1f ms frame period", 6944.0f / 1000.0f);
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "= %.1f ms total", (b_total + 6944) / 1000.0f);
}

// ── B Board: Overview ────────────────────────────────────────────────
static void DrawBOverview(BBoardView& bv) {
    if (!bv.IsLoaded()) {
        ImGui::Spacing();
        ImGui::TextColored(COL_HEADER, "Welcome to the SkyZone Firmware Editor");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("This tool lets you modify timing and image processing parameters "
                           "in Skyzone FPV goggle firmware to reduce latency and customize "
                           "image quality.");
        ImGui::Spacing();

        ImGui::TextColored(COL_SECTION, "Getting Started:");
        ImGui::BulletText("File > Open B Board — load TW8836 video processor firmware");
        ImGui::BulletText("File > Open A Board — load MK22F ARM display controller firmware");
        ImGui::Spacing();

        ImGui::TextColored(COL_SECTION, "Supported Models:");
        ImGui::BulletText("SKY04X Pro (X4 Pro) — raw 8051 binary, 1920x1080 OLED");
        ImGui::BulletText("SKY04O Pro (040 Pro) — XOR-encoded, 1280x720 OLED");
        ImGui::Spacing();

        ImGui::TextColored(COL_SECTION, "Quick Guide:");
        ImGui::TextWrapped("For lowest latency, load both A and B board firmware files, "
                           "then:\n"
                           "  1. B Board > Latency tab > 'Minimize All Latency'\n"
                           "  2. A Board > Frame Timing > select 120fps or 144fps\n"
                           "  3. Save both files");
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, COL_HEADER);
    ImGui::Text("%s  —  B Board (TW8836)", bv.VariantName());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (bv.variant == BB_040PRO) {
        ImGui::TextColored(COL_GREEN, "Format: Skyzone encoded (XOR payload)");
        ImGui::Text("Raw file: %u bytes", bv.RawSize());
        ImGui::Text("Decoded payload: %u bytes", bv.DataSize());
    } else {
        ImGui::TextColored(COL_GREEN, "Format: Raw 8051 binary");
        ImGui::Text("Size: %u bytes (0x%X)", bv.DataSize(), bv.DataSize());
    }

    ImGui::Text("File: %s", bv.Filename().c_str());
    ImGui::Text("Delays found: %zu", bv.Delays().size());
    ImGui::Text("Init regs found: %zu", bv.InitRegs().size());
    ImGui::Text("Sub-tables: %zu", bv.SubTableCount());

    ImGui::Spacing();
    ImGui::TextColored(COL_SECTION, "Display: %s", bv.DisplaySpec());

    // Show latency summary badge
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    auto stages = CalcBBoardLatency(bv);
    float total = 0;
    for (auto& s : stages) total += s.latency_us;
    ImVec4 total_col = total < 500 ? COL_GREEN : (total < 2000 ? COL_YELLOW : COL_RED);
    ImGui::TextColored(COL_SECTION, "Processing Latency: ");
    ImGui::SameLine();
    ImGui::TextColored(total_col, "%.2f ms", total / 1000.0f);
    ImGui::SameLine();
    if (total < 500) ImGui::TextColored(COL_GREEN, "(optimized)");
    else if (total < 2000) ImGui::TextColored(COL_YELLOW, "(moderate — see Latency tab)");
    else ImGui::TextColored(COL_RED, "(high — see Latency tab to optimize)");

    // Show change count
    int changes = 0;
    for (auto& r : bv.InitRegs()) if (r.value != r.original) changes++;
    for (auto& d : bv.Delays()) if (d.value_ms != d.original_ms) changes++;
    if (changes > 0) {
        ImGui::Spacing();
        ImGui::TextColored(COL_CHANGED, "%d modification(s) pending — use File > Save to write", changes);
    }
}

// ── B Board: Delays ──────────────────────────────────────────────────
static void DrawBDelays(BBoardView& bv) {
    if (!bv.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load B firmware first."); return; }
    auto& delays = bv.Delays();

    ImGui::TextColored(COL_HEADER, "Delay Timing Editor");
    ImGui::SameLine();
    HelpMarker("These are software delay loops in the 8051 firmware.\n"
               "They pause execution during initialization and mode switches.\n\n"
               "IMPORTANT: These do NOT affect per-frame latency during normal\n"
               "video display. They only affect:\n"
               "  - Power-on to first image time\n"
               "  - Input switching speed (HDMI/analog)\n"
               "  - Signal loss recovery time\n\n"
               "Reducing too aggressively may cause:\n"
               "  - Failed PLL lock (screen tearing)\n"
               "  - Missed signal detection\n"
               "  - Color calibration errors\n\n"
               "Safe to halve most delays. Do NOT reduce AFE/PLL delays below 50%.");
    ImGui::Separator();

    // Summary
    float total_ms = 0;
    for (auto& d : delays) total_ms += d.value_ms;
    float orig_ms = 0;
    for (auto& d : delays) orig_ms += d.original_ms;
    ImGui::Text("Total delay time: %.0f ms (stock: %.0f ms)", total_ms, orig_ms);
    if (total_ms < orig_ms)
        ImGui::SameLine(), ImGui::TextColored(COL_GREEN, "  saved %.0f ms", orig_ms - total_ms);
    ImGui::Spacing();

    if (ImGui::BeginTable("##delays", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 30))) {
        ImGui::TableSetupColumn("Offset",       ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Bank",         ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Current (ms)", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Original",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Edit (ms)",    ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Description");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < delays.size(); ++i) {
            auto& d = delays[i];
            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableNextColumn(); ImGui::Text("0x%05X", d.file_offset);
            ImGui::TableNextColumn(); ImGui::TextColored(COL_DIM, "%s", d.bank.c_str());
            ImGui::TableNextColumn();
            bool changed = d.value_ms != d.original_ms;
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CHANGED);
            ImGui::Text("%.1f", d.value_ms);
            if (changed) ImGui::PopStyleColor();
            ImGui::TableNextColumn(); ImGui::TextColored(COL_DIM, "%.1f", d.original_ms);

            ImGui::TableNextColumn();
            float v = d.value_ms;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##ms", &v, 0.5f, 5.0f, "%.1f")) {
                if (v >= 0 && v <= 30000) { d.value_ms = v; bv.SetDelay(d, (uint16_t)v); }
            }

            ImGui::TableNextColumn();
            if (!d.desc.empty())
                ImGui::TextWrapped("%s", d.desc.c_str());
            else
                ImGui::TextColored(COL_DIM, "---");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ── B Board: Image Quality ───────────────────────────────────────────
static void DrawBImgQ(BBoardView& bv) {
    if (!bv.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load B firmware first."); return; }
    auto groups   = GetImgQGroups();
    auto low_lat  = GetLowLatencyRegs();
    auto reg_info = GetRegisterInfo();
    auto& regs    = bv.InitRegs();

    ImGui::TextColored(COL_HEADER, "Image Quality & Processing");
    ImGui::SameLine();
    HelpMarker("Each group controls a stage of the TW8836 video processing pipeline.\n"
               "For lowest latency, disable all processing (Low Latency preset).\n"
               "For best picture quality, keep stock values.\n\n"
               "The 'Balanced' preset keeps the comb filter (essential for analog\n"
               "video quality) but disables sharpening, CTI, and dithering.");
    ImGui::Separator();

    // Preset buttons with improved feedback
    static bool show_preset_popup = false;
    static int preset_applied = 0, preset_skipped = 0;
    static std::vector<std::pair<uint16_t, const char*>> skipped_list;

    if (ImGui::Button("Low Latency")) {
        preset_applied = 0; preset_skipped = 0;
        skipped_list.clear();
        for (auto& ll : low_lat) {
            bool found = false;
            for (auto& r : regs) {
                uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                if (rk == ll.reg) {
                    r.value = ll.low_lat;
                    bv.SetInitReg(r, ll.low_lat);
                    found = true; preset_applied++; break;
                }
            }
            if (!found) {
                preset_skipped++;
                skipped_list.push_back({ll.reg, ll.desc});
            }
        }
        if (preset_skipped > 0) show_preset_popup = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Disables ALL processing for minimum latency:");
        for (auto& ll : low_lat)
            ImGui::BulletText("0x%03X = 0x%02X — %s", ll.reg, ll.low_lat, ll.desc);
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button("Balanced")) {
        // Balanced: disable sharpness, CTI, dither but keep comb filter and line buffer at 4
        const struct { uint16_t reg; uint8_t val; } balanced[] = {
            {0x112, 0x00}, {0x117, 0x00}, {0x12C, 0x00},  // decoder processing off
            {0x120, 0x00}, {0x121, 0x00}, {0x122, 0x00}, {0x123, 0x00},
            {0x124, 0x00}, {0x125, 0x00}, {0x126, 0x00}, {0x127, 0x00},
            {0x128, 0x00},  // CTI off
            {0x20B, 0x04},  // line buffer = 4 (compromise)
            {0x284, 0x80}, {0x28A, 0x80}, {0x28B, 0x00},  // img adjust neutral
            {0x2E4, 0x00},  // dither off
        };
        for (auto& b : balanced) {
            for (auto& r : regs) {
                uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                if (rk == b.reg) { r.value = b.val; bv.SetInitReg(r, b.val); break; }
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Good balance of speed and quality:");
        ImGui::BulletText("Decoder sharpness/peaking/filter: OFF");
        ImGui::BulletText("CTI: OFF");
        ImGui::BulletText("Line buffer: 4 lines (vs 2 min, 16 stock)");
        ImGui::BulletText("Image adjust: neutral");
        ImGui::BulletText("Dither: OFF");
        ImGui::BulletText("Comb filter: KEPT (essential for analog)");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button("Stock Values")) {
        for (auto& ll : low_lat) {
            for (auto& r : regs) {
                uint16_t rk = ((uint16_t)r.page << 8) | r.index;
                if (rk == ll.reg) { r.value = r.original; bv.SetInitReg(r, r.original); }
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Restore factory default values for all\nimage quality registers.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset ALL")) {
        for (auto& r : regs) { r.value = r.original; bv.SetInitReg(r, r.original); }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Reset ALL registers (including non-ImgQ)\nto their original firmware values.");
        ImGui::EndTooltip();
    }

    // Skipped registers popup
    if (show_preset_popup) {
        ImGui::OpenPopup("SkippedRegs");
        show_preset_popup = false;
    }
    if (ImGui::BeginPopupModal("SkippedRegs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(COL_YELLOW, "Low Latency Preset Applied");
        ImGui::Separator();
        ImGui::Text("Modified: %d register(s)", preset_applied);
        ImGui::TextColored(COL_RED, "Skipped: %d register(s) — not in init table", preset_skipped);
        ImGui::Spacing();
        for (auto& [reg, desc] : skipped_list) {
            auto& ri = GetRegisterInfo();
            auto it = ri.find(reg);
            ImGui::BulletText("0x%03X  %s — %s", reg,
                              it != ri.end() ? it->second.name : "???", desc);
        }
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "The 040 Pro firmware has fewer init entries.\n"
                                    "Missing registers use hardware defaults.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // Grouped register display with latency hints
    if (ImGui::BeginChild("##imgq_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        for (auto& grp : groups) {
            bool any = false;
            for (auto rk : grp.regs) {
                uint8_t pg = (rk >> 8) & 0xFF, idx = rk & 0xFF;
                for (auto& r : regs) { if (r.page == pg && r.index == idx) { any = true; break; } }
                if (any) break;
            }
            if (!any) continue;

            ImGui::TextColored(COL_SECTION, "%s", grp.title.c_str());
            if (!grp.hint.empty()) { ImGui::SameLine(); HelpMarker(grp.hint.c_str()); }
            ImGui::Separator();

            if (ImGui::BeginTable(("##grp" + grp.title).c_str(), 6,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Page:Idx", ImGuiTableColumnFlags_WidthFixed, 72);
                ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthFixed, 160);
                ImGui::TableSetupColumn("Value",    ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Edit",     ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("Stock",    ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("Info",     ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (auto rk : grp.regs) {
                    uint8_t pg = (rk >> 8) & 0xFF, idx = rk & 0xFF;
                    InitRegEntry* found = nullptr;
                    for (auto& r : regs) { if (r.page == pg && r.index == idx) { found = &r; break; } }
                    if (!found) continue;

                    ImGui::TableNextRow();
                    ImGui::PushID(rk);

                    ImGui::TableNextColumn(); ImGui::Text("%X:%02X", pg, idx);
                    ImGui::TableNextColumn();
                    auto it = reg_info.find(rk);
                    ImGui::Text("%s", it != reg_info.end() ? it->second.name : "???");
                    // Tooltip with full register description
                    if (it != reg_info.end() && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("REG 0x%03X — %s", rk, it->second.name);
                        ImGui::TextColored(COL_DIM, "%s", it->second.desc);
                        ImGui::EndTooltip();
                    }

                    ImGui::TableNextColumn();
                    bool changed = found->value != found->original;
                    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CHANGED);
                    ImGui::Text("0x%02X", found->value);
                    if (changed) ImGui::PopStyleColor();

                    ImGui::TableNextColumn();
                    int val = found->value;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputInt("##v", &val, 1, 16)) {
                        val = std::clamp(val, 0, 255);
                        found->value = (uint8_t)val;
                        bv.SetInitReg(*found, (uint8_t)val);
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextColored(COL_DIM, "0x%02X", found->original);

                    // Info column — show latency-relevant notes
                    ImGui::TableNextColumn();
                    if (rk == 0x10C) {
                        if (found->value & 0x10)
                            ImGui::TextColored(COL_RED, "3D COMB!");
                        else
                            ImGui::TextColored(COL_GREEN, "2D (good)");
                    } else if (rk == 0x20B) {
                        ImGui::TextColored(found->value > 8 ? COL_YELLOW : COL_GREEN,
                                           "%d lines", found->value);
                    } else if (rk == 0x284 || rk == 0x28A) {
                        if (found->value == 0x80)
                            ImGui::TextColored(COL_GREEN, "neutral");
                        else
                            ImGui::TextColored(COL_DIM, "active");
                    } else if (rk == 0x112 || rk == 0x117 || rk == 0x28B) {
                        if (found->value == 0)
                            ImGui::TextColored(COL_GREEN, "off");
                        else
                            ImGui::TextColored(COL_DIM, "on");
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
}

// ── B Board: All Registers ───────────────────────────────────────────
static void DrawBRegs(BBoardView& bv) {
    if (!bv.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load B firmware first."); return; }
    auto& regs    = bv.InitRegs();
    auto reg_info = GetRegisterInfo();
    static char filter[64] = "";

    ImGui::TextColored(COL_HEADER, "All Init Register Entries (%zu)", regs.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##filter", "Filter by name or address...", filter, sizeof(filter));
    ImGui::SameLine();
    HelpMarker("Search by register name (e.g. 'SHARP') or address (e.g. '1:12').\n"
               "Orange values indicate modifications from stock.");
    ImGui::Separator();

    if (ImGui::BeginTable("##allregs", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 10))) {
        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Offset",   ImGuiTableColumnFlags_WidthFixed, 72);
        ImGui::TableSetupColumn("Page:Idx", ImGuiTableColumnFlags_WidthFixed, 72);
        ImGui::TableSetupColumn("Value",    ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Edit",     ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Name");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < regs.size(); ++i) {
            auto& r = regs[i];
            uint16_t rk = ((uint16_t)r.page << 8) | r.index;
            auto it = reg_info.find(rk);
            std::string name = it != reg_info.end() ? it->second.name : "";

            if (filter[0]) {
                char buf[32]; snprintf(buf, sizeof(buf), "%X:%02X", r.page, r.index);
                std::string haystack = std::string(buf) + " " + name;
                std::string needle(filter);
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (haystack.find(needle) == std::string::npos) continue;
            }

            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableNextColumn(); ImGui::Text("%zu", i);
            ImGui::TableNextColumn(); ImGui::Text("0x%05X", r.file_offset);
            ImGui::TableNextColumn(); ImGui::Text("%X:%02X", r.page, r.index);

            ImGui::TableNextColumn();
            bool changed = r.value != r.original;
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CHANGED);
            ImGui::Text("0x%02X", r.value);
            if (changed) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            int val = r.value;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##v", &val, 1, 16)) {
                val = std::clamp(val, 0, 255);
                r.value = (uint8_t)val;
                bv.SetInitReg(r, (uint8_t)val);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", name.c_str());
            if (it != reg_info.end() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(COL_DIM, "%s", it->second.desc);
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ── Shared: Hex View ─────────────────────────────────────────────────
static void DrawHexView(const std::vector<uint8_t>& data, const char* label, int cols = 16) {
    static int goto_addr = 0;
    ImGui::TextColored(COL_HEADER, "%s  —  %zu bytes", label, data.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Go to", &goto_addr, 16, 256);
    ImGui::Separator();

    if (data.empty()) return;

    float line_h = ImGui::GetTextLineHeightWithSpacing();
    int total_lines = ((int)data.size() + cols - 1) / cols;

    if (ImGui::BeginChild("##hex", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        if (goto_addr > 0 && goto_addr < (int)data.size()) {
            ImGui::SetScrollY((float)(goto_addr / cols) * line_h);
            goto_addr = 0;
        }

        ImGuiListClipper clipper;
        clipper.Begin(total_lines, line_h);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                int off = row * cols;
                ImGui::Text("%06X: ", off);
                ImGui::SameLine();
                char hex[256] = "", ascii[32] = "";
                int hlen = 0;
                for (int c = 0; c < cols && off + c < (int)data.size(); ++c) {
                    uint8_t b = data[off + c];
                    hlen += snprintf(hex + hlen, sizeof(hex) - hlen, "%02X ", b);
                    ascii[c] = (b >= 32 && b < 127) ? (char)b : '.';
                    ascii[c + 1] = 0;
                }
                ImGui::SameLine(); ImGui::TextUnformatted(hex);
                ImGui::SameLine(); ImGui::TextColored(COL_DIM, "%s", ascii);
            }
        }
    }
    ImGui::EndChild();
}

// ── Shared: Patch Output ─────────────────────────────────────────────
static void DrawPatchOutput(const std::vector<uint8_t>& original,
                            const std::vector<uint8_t>& current, const char* label) {
    ImGui::TextColored(COL_HEADER, "Patch Output  —  %s", label);
    ImGui::Separator();

    if (original.size() != current.size() || original.empty()) {
        ImGui::TextColored(COL_DIM, "No data to compare.");
        return;
    }

    struct Diff { uint32_t off; uint8_t oldv, newv; };
    std::vector<Diff> diffs;
    for (size_t i = 0; i < original.size(); ++i)
        if (original[i] != current[i])
            diffs.push_back({(uint32_t)i, original[i], current[i]});

    ImGui::Text("%zu byte(s) changed", diffs.size());
    if (diffs.empty()) { ImGui::TextColored(COL_GREEN, "Firmware is unmodified."); return; }

    ImGui::Spacing();
    if (ImGui::BeginTable("##patch", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Old",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("New",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (auto& d : diffs) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%05X", d.off);
            ImGui::TableNextColumn(); ImGui::TextColored(COL_DIM, "0x%02X", d.oldv);
            ImGui::TableNextColumn(); ImGui::TextColored(COL_CHANGED, "0x%02X", d.newv);
        }
        ImGui::EndTable();
    }
}

// ── B Board: Hex + Patch wrappers ────────────────────────────────────
static void DrawBHex(BBoardView& bv) {
    if (!bv.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load B firmware first."); return; }
    DrawHexView(bv.HexData(), bv.variant == BB_040PRO ? "040 B Decoded Payload" : "B Board Raw Hex");
}

// ═══════════════════════════════════════════════════════════════════════
//  A Board tabs
// ═══════════════════════════════════════════════════════════════════════
static void DrawAOverview(FirmwareABoard& ab) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_HEADER);
    ImGui::Text("%s  —  A Board (MK22FN256)", ab.Is040() ? "SKY04O Pro (040)" : "SKY04X Pro");
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!ab.IsLoaded()) {
        ImGui::TextColored(COL_DIM, "No A board firmware loaded.");
        ImGui::Spacing();
        ImGui::TextWrapped("The A Board controls the ARM Cortex-M4 display controller.\n"
                           "Load it to modify frame rate (100/120/144 fps) and panel init settings.");
        return;
    }
    ImGui::Text("File: %s", ab.Filename().c_str());
    ImGui::Text("Size: %u bytes", (uint32_t)ab.Raw().size());
    ImGui::Text("Payload: %u bytes", (uint32_t)ab.Decoded().size());
    ImGui::Text("SP Init: 0x%08X", ab.SpInit());
    ImGui::Text("Reset Vector: 0x%08X", ab.ResetVec());
    ImGui::Text("Code boundary: 0x%X", ab.CodeBoundary());
    ImGui::Text("Timing sites: %zu", ab.TimingSites().size());
    ImGui::Text("Panel init entries: %zu", ab.PanelInits().size());
    ImGui::Text("Strings: %zu", ab.Strings().size());
    if (!ab.BuildDate().empty())
        ImGui::TextColored(COL_GREEN, "Build: %s", ab.BuildDate().c_str());

    // Show current FPS status
    if (!ab.TimingSites().empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        auto& s = ab.TimingSites()[0];
        float fps = s.Fps();
        ImVec4 fps_col = fps >= 140 ? COL_GREEN : (fps >= 115 ? COL_YELLOW : COL_DIM);
        ImGui::TextColored(COL_SECTION, "Frame Rate: ");
        ImGui::SameLine();
        ImGui::TextColored(fps_col, "%.0f fps (%.1f ms frame period)",
                           fps, s.value_us / 1000.0f);
    }
}

static void DrawATiming(FirmwareABoard& ab) {
    if (!ab.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load A firmware first."); return; }
    auto& sites = ab.TimingSites();

    ImGui::TextColored(COL_HEADER, "Frame Timing — Display Refresh Rate");
    ImGui::SameLine();
    HelpMarker("Controls how often the OLED display refreshes.\n"
               "Higher FPS = lower worst-case latency.\n\n"
               "The frame period is the time between display refreshes.\n"
               "Latency reduction = old_period - new_period.\n\n"
               "Presets modify the FIRST TWO timing sites only,\n"
               "matching the known community latency patches.\n"
               "Sites 3-4 are fallback/init paths.");
    ImGui::Separator();

    // ── Latency impact explanation ──
    ImGui::Spacing();
    ImGui::TextColored(COL_SECTION, "Latency Impact:");
    ImGui::Spacing();

    if (ImGui::BeginTable("##fps_info", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                          ImVec2(500, 0))) {
        ImGui::TableSetupColumn("Setting",    ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Frame Time", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("FPS",        ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Latency Saved", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const struct { const char* name; int us; float fps; const char* saved; ImVec4 col; } rows[] = {
            {"Stock",     10000, 100.0f, "—  (baseline)",    COL_DIM},
            {"Fast",       8333, 120.0f, "-1.7 ms vs stock", COL_YELLOW},
            {"Ultra",      6944, 144.0f, "-3.1 ms vs stock", COL_GREEN},
        };
        for (auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", row.name);
            ImGui::TableNextColumn(); ImGui::Text("%.1f ms", row.us / 1000.0f);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", row.fps);
            ImGui::TableNextColumn(); ImGui::TextColored(row.col, "%s", row.saved);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "Note: This is worst-case scan-out latency. Actual improvement "
                                "depends on signal timing alignment.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Presets
    size_t preset_count = std::min<size_t>(2, sites.size());
    if (ImGui::Button("100 fps (stock)")) {
        for (size_t i = 0; i < preset_count; ++i)
            ab.SetFramePeriod(sites[i], FirmwareABoard::FRAME_PERIOD_STOCK);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("10,000 us frame period — factory default");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("120 fps")) {
        for (size_t i = 0; i < preset_count; ++i)
            ab.SetFramePeriod(sites[i], FirmwareABoard::FRAME_PERIOD_120);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("8,333 us — saves 1.7ms per frame");
        ImGui::Text("Safe for both X4Pro and 040 Pro panels");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("144 fps")) {
        for (size_t i = 0; i < preset_count; ++i)
            ab.SetFramePeriod(sites[i], FirmwareABoard::FRAME_PERIOD_144);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("6,944 us — saves 3.1ms per frame");
        ImGui::Text("Maximum speed — near panel refresh limits");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "(applies to first %zu site%s)",
                       preset_count, preset_count == 1 ? "" : "s");
    ImGui::Spacing();

    // Timing sites table
    if (ImGui::BeginTable("##timing", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Site",       ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Offset",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Register",   ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Value (us)", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("FPS",        ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Edit (us)",  ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < sites.size(); ++i) {
            auto& s = sites[i];
            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableNextColumn();
            if (i < preset_count)
                ImGui::TextColored(COL_GREEN, "%zu*", i + 1);
            else
                ImGui::TextColored(COL_DIM, "%zu", i + 1);

            ImGui::TableNextColumn(); ImGui::Text("0x%05X", s.payload_offset);
            ImGui::TableNextColumn(); ImGui::Text("R%d", s.arm_register);

            ImGui::TableNextColumn();
            bool changed = s.value_us != s.original_us;
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CHANGED);
            ImGui::Text("%u", s.value_us);
            if (changed) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            float fps = s.Fps();
            ImVec4 fps_col = fps >= 140 ? COL_GREEN : (fps >= 115 ? COL_YELLOW : COL_DIM);
            ImGui::TextColored(fps_col, "%.0f", fps);

            ImGui::TableNextColumn();
            int val = s.value_us;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##us", &val, 100, 1000)) {
                val = std::clamp(val, 1000, 30000);
                ab.SetFramePeriod(s, (uint16_t)val);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "* = modified by presets.  Sites 3-4 are fallback/init paths.");
}

static void DrawAPanel(FirmwareABoard& ab) {
    if (!ab.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load A firmware first."); return; }
    auto& inits = ab.PanelInits();

    ImGui::TextColored(COL_HEADER, "Panel Init Entries (%zu)", inits.size());
    ImGui::SameLine();
    HelpMarker("OLED panel initialization registers sent via I2C.\n"
               "These configure the display panel hardware.\n"
               "Modifying these can affect display quality and timing.\n"
               "Use the Reg Reference tab to look up register names.");
    ImGui::Separator();

    if (ImGui::BeginTable("##panel", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y - 10))) {
        ImGui::TableSetupColumn("Table",  ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Group",  ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Reg",    ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Value",  ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Edit",   ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Stock",  ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < inits.size(); ++i) {
            auto& e = inits[i];
            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableNextColumn(); ImGui::Text("%c", e.table_id);
            ImGui::TableNextColumn(); ImGui::Text("%u", e.group_idx);
            ImGui::TableNextColumn(); ImGui::Text("0x%02X", e.reg);
            ImGui::TableNextColumn();
            { auto& pr = GetPanelRegs(); auto pit = pr.find(e.reg);
              if (pit != pr.end()) {
                  ImGui::Text("%s", pit->second.name);
                  if (ImGui::IsItemHovered()) {
                      ImGui::BeginTooltip();
                      ImGui::TextColored(COL_DIM, "%s", pit->second.desc);
                      ImGui::EndTooltip();
                  }
              }
              else ImGui::TextColored(COL_DIM, "---"); }

            ImGui::TableNextColumn();
            bool changed = e.value != e.original;
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, COL_CHANGED);
            ImGui::Text("0x%02X", e.value);
            if (changed) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            int val = e.value;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##v", &val, 1, 16)) {
                val = std::clamp(val, 0, 255);
                ab.SetPanelValue(e, (uint8_t)val);
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(COL_DIM, "0x%02X", e.original);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void DrawAStrings(FirmwareABoard& ab) {
    if (!ab.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load A firmware first."); return; }
    auto& strs = ab.Strings();
    static char filter[64] = "";

    ImGui::TextColored(COL_HEADER, "Extracted Strings (%zu)", strs.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##sf", "Filter...", filter, sizeof(filter));
    ImGui::Separator();

    if (ImGui::BeginTable("##strings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY, ImVec2(0, ImGui::GetContentRegionAvail().y - 10))) {
        ImGui::TableSetupColumn("Offset",  ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("String");
        ImGui::TableHeadersRow();

        for (auto& s : strs) {
            if (filter[0]) {
                std::string hay = s.text;
                std::string needle(filter);
                std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (hay.find(needle) == std::string::npos) continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%05X", s.payload_offset);
            ImGui::TableNextColumn(); ImGui::TextColored(COL_DIM, "%s", s.section.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", s.text.c_str());
        }
        ImGui::EndTable();
    }
}

static void DrawAHex(FirmwareABoard& ab) {
    if (!ab.IsLoaded()) { ImGui::TextColored(COL_DIM, "Load A firmware first."); return; }
    DrawHexView(ab.Decoded(), "A Board Decoded Payload");
}

// -- A Board: Register Reference ---
static void DrawARegReference(FirmwareABoard& ab) {
    static char filter[64] = "";
    static int db_sel = 0;
    const char* db_names[] = { "Panel OLED", "LT9211 (MIPI-LVDS)", "IT6802 (HDMI Rx)", "MK22F (ARM MCU)" };

    ImGui::TextColored(COL_HEADER, "A Board Register Reference");
    ImGui::SameLine(); HelpMarker("Complete register databases for all A board chips.\n"
                                  "Use this as reference when examining panel init entries or disassembly.");
    ImGui::Separator();

    ImGui::Text("Chip: ");
    ImGui::SameLine();
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(db_names[i], db_sel == i)) db_sel = i;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##rfilt", "Filter...", filter, sizeof(filter));
    ImGui::Spacing();

    auto matchFilter = [](const char* name, const char* desc, const char* filt) -> bool {
        if (!filt[0]) return true;
        std::string hay = std::string(name) + " " + desc;
        std::string needle(filt);
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        return hay.find(needle) != std::string::npos;
    };

    if (ImGui::BeginTable("##regref", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 10))) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthFixed, 180);
        ImGui::TableSetupColumn("Description");
        ImGui::TableHeadersRow();

        if (db_sel == 0) {
            auto& db = GetPanelRegs();
            std::vector<uint8_t> keys;
            for (auto& kv : db) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            for (auto k : keys) {
                auto& r = db.at(k);
                if (!matchFilter(r.name, r.desc, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("0x%02X", k);
                ImGui::TableNextColumn(); ImGui::Text("%s", r.name);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.desc);
            }
        } else if (db_sel == 1) {
            auto& db = GetLT9211Regs();
            std::vector<uint32_t> keys;
            for (auto& kv : db) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            for (auto k : keys) {
                auto& r = db.at(k);
                if (!matchFilter(r.name, r.desc, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("0x%04X", k);
                ImGui::TableNextColumn(); ImGui::Text("%s", r.name);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.desc);
            }
        } else if (db_sel == 2) {
            auto& db = GetIT6802Regs();
            std::vector<uint8_t> keys;
            for (auto& kv : db) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            for (auto k : keys) {
                auto& r = db.at(k);
                if (!matchFilter(r.name, r.desc, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("0x%02X", k);
                ImGui::TableNextColumn(); ImGui::Text("%s", r.name);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.desc);
            }
        } else {
            auto& db = GetMK22FRegs();
            std::vector<uint32_t> keys;
            for (auto& kv : db) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            for (auto k : keys) {
                auto& r = db.at(k);
                if (!matchFilter(r.name, r.desc, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("0x%08X", k);
                ImGui::TableNextColumn(); ImGui::Text("%s", r.name);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.desc);
            }
        }
        ImGui::EndTable();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Main editor draw — called from main.cpp
// ═══════════════════════════════════════════════════════════════════════
void DrawEditorUI(FirmwareBBoard& bb, FirmwareABoard& ab, Firmware040B& b040,
                  Disasm8051& dis8051, DisasmThumb& disThumb,
                  AnnotationDB& db8051, AnnotationDB& dbArm)
{
    // Keep view pointers in sync
    g_bview.bb   = &bb;
    g_bview.b040 = &b040;
    if (bb.IsLoaded()   && g_bview.variant == BB_NONE) g_bview.variant = BB_X4PRO;
    if (b040.IsLoaded() && g_bview.variant == BB_NONE) g_bview.variant = BB_040PRO;

    if (g_bb_orig_dirty) { s_bb_orig.clear(); g_bb_orig_dirty = false; }
    if (g_ab_orig_dirty) { s_ab_orig.clear(); g_ab_orig_dirty = false; }
    if (g_bview.IsLoaded() && s_bb_orig.empty()) s_bb_orig = g_bview.SaveData();
    if (ab.IsLoaded() && s_ab_orig.empty())      s_ab_orig = ab.Raw();

    if (ImGui::BeginTabBar("##boards")) {
        // ── Unified B Board tab ─────────────────────────────────
        if (ImGui::BeginTabItem("B Board — TW8836")) {
            if (ImGui::BeginTabBar("##btabs")) {
                if (ImGui::BeginTabItem("Overview"))       { DrawBOverview(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Latency"))        { DrawBLatency(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Delays"))         { DrawBDelays(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Image Quality"))  { DrawBImgQ(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Registers"))      { DrawBRegs(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Hex View"))       { DrawBHex(g_bview); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Patch Output"))   {
                    DrawPatchOutput(s_bb_orig, g_bview.SaveData(),
                        g_bview.variant == BB_040PRO ? "040 B Board" : "B Board");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }

        // ── A Board tab ─────────────────────────────────────────
        if (ImGui::BeginTabItem("A Board — MK22F")) {
            if (ImGui::BeginTabBar("##atabs")) {
                if (ImGui::BeginTabItem("Overview"))       { DrawAOverview(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Frame Timing"))   { DrawATiming(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Panel Init"))     { DrawAPanel(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Strings"))        { DrawAStrings(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Reg Reference"))  { DrawARegReference(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Hex View"))       { DrawAHex(ab); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Patch Output"))   {
                    DrawPatchOutput(s_ab_orig, ab.Raw(), "A Board");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }

        // ── Disassembler / Decompiler tab ───────────────────────
        if (ImGui::BeginTabItem("Disassembler")) {
            DrawDisasmUI(bb, ab, b040, dis8051, disThumb, db8051, dbArm);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
