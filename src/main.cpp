// ═══════════════════════════════════════════════════════════════════════
//  main.cpp — SkyZone Firmware Editor + Disassembler
//  ImGui + SDL2 + OpenGL3
// ═══════════════════════════════════════════════════════════════════════
#include "firmware.h"
#include "firmware_a.h"
#include "disasm.h"
#include "annotations.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <SDL_opengl.h>
#endif

// ── Forward declarations from ui_editor.cpp / ui_disasm.cpp ──────────
void DrawEditorUI(FirmwareBBoard& bb, FirmwareABoard& ab, Firmware040B& b040,
                  Disasm8051& dis8051, DisasmThumb& disThumb,
                  AnnotationDB& db8051, AnnotationDB& dbArm);
void ResetBOriginals();
const std::vector<uint8_t>& GetBBOriginal();
const std::vector<uint8_t>& GetABOriginal();
void ResetAOriginals();
void ResetDisasmState();
bool IsDisasmRunning();

// ── Globals ──────────────────────────────────────────────────────────
static FirmwareBBoard  g_bb;
static FirmwareABoard  g_ab;
static Firmware040B    g_b040;
static Disasm8051      g_dis8051;
static DisasmThumb     g_disThumb;
static AnnotationDB    g_db8051;
static AnnotationDB    g_dbArm;
static std::string     g_status = "No firmware loaded";
static char            g_last_dir[512] = ".";

// ── Native file dialog (Win32) ───────────────────────────────────────
#ifdef _WIN32
static std::string OpenFileDialog(const char* filter = "Firmware Files\0*.bin;*.hex;*.fw;*.*\0All Files\0*.*\0") {
    char path[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = g_last_dir;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        std::string s(path);
        auto pos = s.find_last_of("\\/");
        if (pos != std::string::npos)
            strncpy(g_last_dir, s.substr(0, pos).c_str(), sizeof(g_last_dir) - 1);
        return s;
    }
    return "";
}

static std::string SaveFileDialog(const char* filter = "Firmware Files\0*.bin;*.hex;*.fw;*.*\0All Files\0*.*\0") {
    char path[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = g_last_dir;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn)) return std::string(path);
    return "";
}
#else
static std::string OpenFileDialog(const char* = nullptr) { return ""; }
static std::string SaveFileDialog(const char* = nullptr) { return ""; }
#endif

// ── Dark theme ───────────────────────────────────────────────────────
static void ApplyDarkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 4.0f;
    s.FrameRounding    = 3.0f;
    s.GrabRounding     = 3.0f;
    s.TabRounding      = 3.0f;
    s.ScrollbarRounding= 3.0f;
    s.FramePadding     = ImVec2(8, 4);
    s.ItemSpacing      = ImVec2(8, 5);
    s.WindowPadding    = ImVec2(10, 10);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = {0.10f, 0.10f, 0.12f, 1.0f};
    c[ImGuiCol_ChildBg]         = {0.10f, 0.10f, 0.12f, 1.0f};
    c[ImGuiCol_PopupBg]         = {0.12f, 0.12f, 0.15f, 0.95f};
    c[ImGuiCol_Border]          = {0.25f, 0.25f, 0.30f, 0.50f};
    c[ImGuiCol_FrameBg]         = {0.16f, 0.16f, 0.20f, 1.0f};
    c[ImGuiCol_FrameBgHovered]  = {0.22f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_FrameBgActive]   = {0.28f, 0.28f, 0.35f, 1.0f};
    c[ImGuiCol_TitleBg]         = {0.08f, 0.08f, 0.10f, 1.0f};
    c[ImGuiCol_TitleBgActive]   = {0.12f, 0.14f, 0.20f, 1.0f};
    c[ImGuiCol_MenuBarBg]       = {0.12f, 0.12f, 0.15f, 1.0f};
    c[ImGuiCol_ScrollbarBg]     = {0.08f, 0.08f, 0.10f, 1.0f};
    c[ImGuiCol_ScrollbarGrab]   = {0.30f, 0.30f, 0.35f, 1.0f};
    c[ImGuiCol_CheckMark]       = {0.40f, 0.75f, 1.00f, 1.0f};
    c[ImGuiCol_SliderGrab]      = {0.40f, 0.75f, 1.00f, 1.0f};
    c[ImGuiCol_Button]          = {0.20f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_ButtonHovered]   = {0.28f, 0.32f, 0.42f, 1.0f};
    c[ImGuiCol_ButtonActive]    = {0.35f, 0.42f, 0.55f, 1.0f};
    c[ImGuiCol_Header]          = {0.18f, 0.20f, 0.28f, 1.0f};
    c[ImGuiCol_HeaderHovered]   = {0.26f, 0.30f, 0.42f, 1.0f};
    c[ImGuiCol_HeaderActive]    = {0.30f, 0.38f, 0.52f, 1.0f};
    c[ImGuiCol_Tab]             = {0.14f, 0.16f, 0.22f, 1.0f};
    c[ImGuiCol_TabSelected]     = {0.22f, 0.28f, 0.40f, 1.0f};
    c[ImGuiCol_TabHovered]      = {0.28f, 0.35f, 0.50f, 1.0f};
    c[ImGuiCol_TableHeaderBg]   = {0.14f, 0.16f, 0.22f, 1.0f};
    c[ImGuiCol_TableBorderStrong]={0.25f, 0.25f, 0.30f, 1.0f};
    c[ImGuiCol_TableBorderLight]= {0.20f, 0.20f, 0.25f, 1.0f};
    c[ImGuiCol_TableRowBg]      = {0.00f, 0.00f, 0.00f, 0.00f};
    c[ImGuiCol_TableRowBgAlt]   = {1.00f, 1.00f, 1.00f, 0.03f};
    c[ImGuiCol_TextDisabled]    = {0.50f, 0.50f, 0.55f, 1.0f};
    c[ImGuiCol_Text]            = {0.90f, 0.90f, 0.92f, 1.0f};
    c[ImGuiCol_Separator]       = {0.25f, 0.25f, 0.30f, 0.50f};
}

// ── Main menu bar ────────────────────────────────────────────────────
// -- Generate patch log alongside saved firmware --
static void WritePatchLog(const std::string& fw_path,
                          const std::vector<uint8_t>& original,
                          const std::vector<uint8_t>& current,
                          const char* board_name)
{
    if (original.size() != current.size() || original.empty()) return;

    // Build diff list
    struct Diff { uint32_t off; uint8_t oldv, newv; };
    std::vector<Diff> diffs;
    for (size_t i = 0; i < original.size(); ++i)
        if (original[i] != current[i])
            diffs.push_back({(uint32_t)i, original[i], current[i]});
    if (diffs.empty()) return;

    // Write .txt companion file
    std::string log_path = fw_path;
    auto dot = log_path.rfind('.');
    if (dot != std::string::npos) log_path = log_path.substr(0, dot);
    log_path += "_patch_log.txt";

    std::ofstream f(log_path);
    if (!f) return;
    f << "SkyZone Firmware Editor - Patch Log\n";
    f << "Board: " << board_name << "\n";
    f << "File:  " << fw_path << "\n";
    f << "Changes: " << diffs.size() << " byte(s)\n";
    f << "----------------------------------------\n";
    char buf[80];
    for (auto& d : diffs) {
        snprintf(buf, sizeof(buf), "  0x%05X:  0x%02X -> 0x%02X", d.off, d.oldv, d.newv);
        f << buf << "\n";
    }
    f << "----------------------------------------\n";
}
static void DrawMenuBar() {
    bool disasm_busy = IsDisasmRunning();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // ── Open ─────────────────────────────────────────────────
            // Disable file open/save while disassembler thread is running
            if (disasm_busy) ImGui::BeginDisabled();

            if (ImGui::MenuItem("Open B Board...", "Ctrl+B")) {
                auto p = OpenFileDialog();
                if (!p.empty()) {
                    // *** RESET everything before loading new firmware ***
                    // Clear both firmware objects so old data doesn't linger
                    g_bb   = FirmwareBBoard();
                    g_b040 = Firmware040B();

                    // Reset disassembler state (waits for thread if running)
                    ResetDisasmState();
                    g_dis8051.Clear();
                    g_db8051.Clear();

                    // Reset UI originals cache
                    ResetBOriginals();

                    // Auto-detect: try 040 encoded format first, then raw X4Pro
                    bool loaded = false;
                    if (g_b040.Load(p)) {
                        loaded = true;
                        g_status = "B Board loaded (040 Pro): " + g_b040.Filename() +
                                   " -- " + std::to_string(g_b040.Delays().size()) + " delays, " +
                                   std::to_string(g_b040.InitRegs().size()) + " regs";
                    }
                    if (!loaded) {
                        // 040 load failed — reset it and try raw X4Pro
                        g_b040 = Firmware040B();
                        if (g_bb.Load(p)) {
                            loaded = true;
                            g_status = "B Board loaded (X4Pro): " + g_bb.Filename() +
                                       " -- " + std::to_string(g_bb.Delays().size()) + " delays, " +
                                       std::to_string(g_bb.InitRegs().size()) + " regs";
                        }
                    }
                    if (!loaded) {
                        g_status = "Failed to load B firmware!";
                    }
                }
            }
            if (ImGui::MenuItem("Open A Board...", "Ctrl+A")) {
                auto p = OpenFileDialog();
                if (!p.empty()) {
                    // Reset A board state
                    g_ab = FirmwareABoard();
                    ResetDisasmState();
                    g_disThumb.Clear();
                    g_dbArm.Clear();
                    ResetAOriginals();

                    if (g_ab.Load(p)) {
                        g_status = "A Board loaded: " + g_ab.Filename() +
                                   " -- " + std::to_string(g_ab.TimingSites().size()) + " timing sites, " +
                                   std::to_string(g_ab.PanelInits().size()) + " panel inits";
                    } else {
                        g_status = "Failed to load A firmware!";
                    }
                }
            }

            ImGui::Separator();

            // ── Save ─────────────────────────────────────────────────
            bool b_loaded = g_bb.IsLoaded() || g_b040.IsLoaded();
            if (ImGui::MenuItem("Save B Board...", nullptr, false, b_loaded)) {
                auto p = SaveFileDialog();
                if (!p.empty()) {
                    bool ok = false;
                    if (g_b040.IsLoaded()) ok = g_b040.Save(p);
                    else if (g_bb.IsLoaded()) ok = g_bb.Save(p);
                    if (ok) {
                        const char* bname = g_b040.IsLoaded() ? "040 B Board" : "X4Pro B Board";
                        WritePatchLog(p, GetBBOriginal(),
                                      g_b040.IsLoaded() ? g_b040.Raw() : (const std::vector<uint8_t>&)g_bb.Data(),
                                      bname);
                        g_status = "B Board saved to " + p;
                    }
                    else g_status = "Failed to save!";
                }
            }
            if (ImGui::MenuItem("Save A Board...", nullptr, false, g_ab.IsLoaded())) {
                auto p = SaveFileDialog();
                if (!p.empty()) {
                    if (g_ab.Save(p)) {
                        WritePatchLog(p, GetABOriginal(), g_ab.Raw(), "A Board");
                        g_status = "A Board saved to " + p;
                    }
                    else g_status = "Failed to save!";
                }
            }

            if (disasm_busy) ImGui::EndDisabled();

            ImGui::Separator();

            // ── Annotations (PDB) ───────────────────────────────────
            if (ImGui::MenuItem("Load Annotations...", "Ctrl+L")) {
                auto p = OpenFileDialog("Annotation Files\0*.json\0All Files\0*.*\0");
                if (!p.empty()) {
                    AnnotationDB tmp;
                    if (tmp.LoadJSON(p)) {
                        if (tmp.target == "arm") {
                            g_dbArm = tmp;
                            g_dbArm.ApplyToFunctions(g_disThumb.Functions());
                        } else {
                            g_db8051 = tmp;
                            g_db8051.ApplyToFunctions(g_dis8051.Functions());
                        }
                        g_status = "Loaded annotations from " + p;
                    } else g_status = "Failed to load annotations!";
                }
            }
            if (ImGui::MenuItem("Save 8051 Annotations...", nullptr, false, !g_db8051.Empty())) {
                auto p = SaveFileDialog("JSON Files\0*.json\0All Files\0*.*\0");
                if (!p.empty()) {
                    g_db8051.target = "8051";
                    g_db8051.SaveJSON(p);
                    g_status = "Saved 8051 annotations to " + p;
                }
            }
            if (ImGui::MenuItem("Save ARM Annotations...", nullptr, false, !g_dbArm.Empty())) {
                auto p = SaveFileDialog("JSON Files\0*.json\0All Files\0*.*\0");
                if (!p.empty()) {
                    g_dbArm.target = "arm";
                    g_dbArm.SaveJSON(p);
                    g_status = "Saved ARM annotations to " + p;
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                SDL_Event e; e.type = SDL_QUIT;
                SDL_PushEvent(&e);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About"))
                ImGui::OpenPopup("About##popup");
            ImGui::EndMenu();
        }
        // About popup
        if (ImGui::BeginPopupModal("About##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored({0.40f,0.75f,1.0f,1.0f}, "SkyZone Firmware Editor");
            ImGui::Separator();
            ImGui::Text("Supports SKY04X Pro and SKY04O Pro (040) firmwares.");
            ImGui::Spacing();
            ImGui::Text("B Board: TW8836 8051 video processor");
            ImGui::Text("  - Delay timing, init registers, image quality");
            ImGui::Text("  - Comb filter, CTI, scaler, contrast/brightness");
            ImGui::Spacing();
            ImGui::Text("A Board: MK22FN256 ARM Cortex-M4");
            ImGui::Text("  - Frame rate (100/120/144 fps)");
            ImGui::Text("  - Panel OLED init, LT9211, IT6802 databases");
            ImGui::Text("  - Embedded string viewer, hex editor");
            ImGui::Spacing();
            ImGui::Text("Built with ImGui + SDL2 + OpenGL3");
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Status in menu bar
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(g_status.c_str()).x - 20);
        ImGui::TextColored({0.50f, 0.50f, 0.55f, 1.0f}, "%s", g_status.c_str());

        ImGui::EndMainMenuBar();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Entry point
// ═══════════════════════════════════════════════════════════════════════
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#else
int main(int, char**) {
#endif
    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "SkyZone Firmware Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // VSync

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "skyzone_editor_imgui.ini";

    // Theme
    ApplyDarkTheme();

    // Platform + renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    const char* glsl_version = "#version 330";
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ── Main loop ────────────────────────────────────────────────────
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // New frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Full-window dockspace
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();

        DrawMenuBar();
        DrawEditorUI(g_bb, g_ab, g_b040, g_dis8051, g_disThumb, g_db8051, g_dbArm);

        ImGui::End();

        // Render
        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Make sure disasm thread is cleaned up before exit
    ResetDisasmState();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}