#include "litert.h"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static const char* TRANSLATE_SYSTEM =
    "You are a translator. Respond with ONLY the translated text, "
    "no explanations, no alternatives.";
static const char* CHAT_SYSTEM = "You are a helpful assistant.";

static std::string default_dll_path() {
#ifdef _WIN32
    constexpr const char* DLL = "litert-lm.dll";
    // 1. Env var override
    if (const char* env = getenv("LITERT_LM_DLL")) return env;
    // 2. Beside the executable
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    fs::path beside = fs::path(exe).parent_path() / DLL;
    if (fs::exists(beside)) return beside.string();
    // 3. Beside the executable in a lib/ subfolder
    fs::path lib_dir = fs::path(exe).parent_path() / "lib" / DLL;
    if (fs::exists(lib_dir)) return lib_dir.string();
    // 4. Fall back to DLL name only (rely on PATH / DLL search order)
    return DLL;
#else
    if (const char* env = getenv("LITERT_LM_DLL")) return env;
    return "liblitert-lm.so";
#endif
}

static void print_help(const char* prog) {
    std::cout
        << "Usage: " << prog << " <model.litertlm> [options]\n\n"
        << "Options:\n"
        << "  --gpu          Use GPU backend (default)\n"
        << "  --cpu          Use CPU backend\n"
        << "  --no-think     Disable thinking mode (Qwen3 models)\n"
        << "  --dll <path>   Path to litert-lm.dll\n"
        << "  -h, --help     Show this help\n\n"
        << "Chat commands:\n"
        << "  /translate <lang> <text>   Fast translation (no explanations)\n"
        << "  /cpu  /gpu                 Switch backend (reloads model)\n"
        << "  /reset                     Clear conversation history\n"
        << "  /quit                      Exit\n";
}

static long long ms_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::string model_path;
    std::string backend  = "gpu";
    std::string dll_path;
    bool        no_think = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")         { print_help(argv[0]); return 0; }
        else if (a == "--cpu")                  backend  = "cpu";
        else if (a == "--gpu")                  backend  = "gpu";
        else if (a == "--no-think")             no_think = true;
        else if (a == "--dll" && i + 1 < argc)  dll_path = argv[++i];
        else if (a[0] != '-' && model_path.empty()) model_path = a;
    }

    if (model_path.empty()) {
        std::cerr << "Error: model path required\n";
        print_help(argv[0]);
        return 1;
    }
    if (dll_path.empty()) dll_path = default_dll_path();

    litert::Config cfg{ model_path, backend };
    litert::Engine engine;

    auto load = [&]() -> bool {
        std::cout << "Loading model on " << cfg.backend << "...\n";
        long long t0 = ms_now();
        if (!engine.init(cfg, dll_path)) {
            std::cerr << "Error: " << engine.last_error() << "\n";
            return false;
        }
        std::cout << "Ready! (" << (ms_now() - t0) << " ms)\n"
                  << "Commands: /translate <lang> <text>  /cpu  /gpu  /reset  /quit\n\n";
        return true;
    };

    if (!load()) return 1;

    auto conv = engine.conversation(CHAT_SYSTEM);
    std::string backend_name = cfg.backend;

    std::string line;
    while (std::cout << "[" << backend_name << "] You: "
           && std::getline(std::cin, line)) {
        // Strip \r and any leading BOM/control chars from Windows pipe
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Strip UTF-8 BOM if PowerShell injected it on the first line
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        if (line.empty()) continue;

        // ── /quit ──────────────────────────────────────────────────────────
        if (line == "/quit") {
            std::cout << "Bye!\n";
            break;

        // ── /reset ─────────────────────────────────────────────────────────
        } else if (line == "/reset") {
            conv = engine.conversation(CHAT_SYSTEM);
            std::cout << "  [conversation reset]\n\n";

        // ── /cpu or /gpu ───────────────────────────────────────────────────
        } else if (line == "/cpu" || line == "/gpu") {
            std::string want = line.substr(1);
            if (want == backend_name) {
                std::cout << "  [already on " << backend_name << "]\n\n";
                continue;
            }
            conv.reset();
            // Destroy engine, recreate with new backend.
            engine.~Engine();
            new (&engine) litert::Engine();
            cfg.backend = want;
            if (!load()) return 1;
            backend_name = want;
            conv = engine.conversation(CHAT_SYSTEM);

        // ── /translate <lang> <text> ───────────────────────────────────────
        } else if (line.rfind("/translate ", 0) == 0) {
            std::string rest = line.substr(11);
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) {
                std::cout << "  Usage: /translate <lang> <text>\n\n";
                continue;
            }
            std::string lang = rest.substr(0, sp);
            std::string text = rest.substr(sp + 1);

            auto t_conv = engine.conversation(TRANSLATE_SYSTEM);
            std::string raw;
            int tok = 0;  // all streamed chunks (including thinking tokens for tok/s)
            long long t0 = ms_now();
            std::string prompt = std::string(no_think ? "/no_think " : "") + "Translate to " + lang + ": " + text;
            t_conv->send_stream(prompt,
                [&raw, &tok](std::string_view chunk, bool /*is_final*/) {
                    if (!chunk.empty()) { raw += chunk; ++tok; }
                });
            long long elapsed = ms_now() - t0;

            // Strip <think>...</think> for display; tok/s uses full token count
            std::string result = raw;
            {
                size_t p = 0;
                while ((p = result.find("<think>", p)) != std::string::npos) {
                    size_t e = result.find("</think>", p);
                    result.erase(p, e == std::string::npos ? std::string::npos : e - p + 8);
                }
                size_t trim = result.find_first_not_of(" \t\n\r");
                if (trim != std::string::npos) result = result.substr(trim);
            }
            float tps = elapsed > 0 ? tok * 1000.0f / elapsed : 0.0f;

            std::cout << "  " << lang << ": " << result << "\n"
                      << "  [" << elapsed << " ms | " << tok << " tok | "
                      << std::fixed << std::setprecision(1) << tps << " tok/s]\n\n";

        // ── regular chat ───────────────────────────────────────────────────
        } else {
            std::cout << "Gemma: " << std::flush;
            long long t0 = ms_now();
            int tok = 0;
            std::string chat_input = std::string(no_think ? "/no_think " : "") + line;
            conv->send_stream(chat_input, [&tok](std::string_view chunk, bool /*is_final*/) {
                if (!chunk.empty()) ++tok;
                std::cout << chunk << std::flush;
            });
            long long elapsed = ms_now() - t0;
            float tps = elapsed > 0 ? tok * 1000.0f / elapsed : 0.0f;
            std::cout << "\n  [" << elapsed << " ms | " << tok << " tok | "
                      << std::fixed << std::setprecision(1) << tps << " tok/s]\n\n";
        }
    }

    return 0;
}
