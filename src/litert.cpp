#include "litert.h"
#include "litert_lm.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>

// ── DLL loader ────────────────────────────────────────────────────────────────

#define BIND(sym) \
    sym = reinterpret_cast<decltype(sym)>(LIB_SYM(handle, "litert_lm_" #sym)); \
    if (!sym) { return false; }

bool LiteRtLmLib::load(const std::string& dll_path) {
    handle = LIB_LOAD(dll_path.c_str());
    if (!handle) return false;

    BIND(set_min_log_level)
    BIND(engine_settings_create)
    BIND(engine_settings_delete)
    BIND(engine_settings_set_max_num_tokens)
    BIND(engine_create)
    BIND(engine_delete)
    BIND(session_config_create)
    BIND(session_config_delete)
    BIND(session_config_set_sampler_params)
    BIND(conversation_config_create)
    BIND(conversation_config_delete)
    BIND(conversation_config_set_session_config)
    BIND(conversation_config_set_system_message)
    BIND(conversation_create)
    BIND(conversation_delete)
    BIND(conversation_send_message)
    BIND(conversation_send_message_stream)
    BIND(conversation_cancel_process)
    BIND(json_response_delete)
    BIND(json_response_get_string)

    return true;
}

#undef BIND

// ── JSON helpers ──────────────────────────────────────────────────────────────

// Minimal JSON string escaping.
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

static std::string make_user_msg(const std::string& text) {
    return R"({"role":"user","content":[{"type":"text","text":")"
           + json_escape(text) + R"("}]})";
}

// Extract text from {"role":"assistant","content":[{"type":"text","text":"..."}]}
static std::string extract_text(const std::string& json) {
    // Find last occurrence of "text":"  (the content text, not the type field)
    // Walk through all "text":" occurrences and pick the last one.
    std::string needle = "\"text\":\"";
    size_t pos = std::string::npos;
    size_t search = 0;
    while (true) {
        size_t found = json.find(needle, search);
        if (found == std::string::npos) break;
        pos = found;
        search = found + 1;
    }
    if (pos == std::string::npos) return json;

    size_t start = pos + needle.size();
    std::string result;
    for (size_t i = start; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char next = json[i + 1];
            if      (next == '"')  { result += '"';  ++i; }
            else if (next == '\\') { result += '\\'; ++i; }
            else if (next == 'n')  { result += '\n'; ++i; }
            else if (next == 'r')  { result += '\r'; ++i; }
            else if (next == 't')  { result += '\t'; ++i; }
            else                   { result += json[i]; }
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i];
        }
    }
    return result;
}

// ── Conversation ──────────────────────────────────────────────────────────────

namespace litert {

Conversation::Conversation(LiteRtLmLib& lib, void* engine_ptr,
                           const std::string& system_message)
    : lib_(lib) {
    void* sess_cfg = lib_.session_config_create();
    if (!sess_cfg) { error_ = "session_config_create failed"; return; }

    LiteRtLmSamplerParams params{};
    params.type        = 2;    // top_p
    params.top_k       = 1;
    params.top_p       = 0.95f;
    params.temperature = 1.0f;
    params.seed        = 0;
    lib_.session_config_set_sampler_params(sess_cfg, &params);

    void* conv_cfg = lib_.conversation_config_create();
    if (!conv_cfg) {
        lib_.session_config_delete(sess_cfg);
        error_ = "conversation_config_create failed";
        return;
    }

    lib_.conversation_config_set_session_config(conv_cfg, sess_cfg);
    lib_.session_config_delete(sess_cfg);

    if (!system_message.empty())
        lib_.conversation_config_set_system_message(conv_cfg, system_message.c_str());

    conv_ptr_ = lib_.conversation_create(engine_ptr, conv_cfg);
    lib_.conversation_config_delete(conv_cfg);

    if (!conv_ptr_) error_ = "conversation_create failed";
}

Conversation::~Conversation() {
    if (conv_ptr_) { lib_.conversation_delete(conv_ptr_); conv_ptr_ = nullptr; }
}

std::string Conversation::send(const std::string& text) {
    if (!conv_ptr_) return "";

    std::string msg_json = make_user_msg(text);
    void* resp = lib_.conversation_send_message(
        conv_ptr_, msg_json.c_str(), "{}", nullptr);
    if (!resp) { error_ = "send_message returned null"; return ""; }

    std::string result;
    const char* raw = lib_.json_response_get_string(resp);
    if (raw) result = extract_text(std::string(raw));
    lib_.json_response_delete(resp);
    return result;
}

// ── Streaming ─────────────────────────────────────────────────────────────────

// The DLL calls the callback from a background thread and returns immediately,
// so we block here until is_final fires.
struct StreamCtx {
    std::function<void(std::string_view, bool)> cb;
    std::mutex              mtx;
    std::condition_variable cv;
    bool                    done = false;
};

static void stream_bridge(void* data, const char* chunk,
                          bool is_final, const char* /*error_msg*/) {
    auto* ctx = static_cast<StreamCtx*>(data);
    ctx->cb(chunk ? extract_text(chunk) : "", is_final);
    if (is_final) {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        ctx->done = true;
        ctx->cv.notify_one();
    }
}

bool Conversation::send_stream(const std::string& text,
    std::function<void(std::string_view, bool)> on_chunk)
{
    if (!conv_ptr_) return false;
    StreamCtx ctx;
    ctx.cb = std::move(on_chunk);
    std::string msg_json = make_user_msg(text);
    int rc = lib_.conversation_send_message_stream(
        conv_ptr_, msg_json.c_str(), "{}", nullptr, stream_bridge, &ctx);
    if (rc != 0) { error_ = "send_message_stream error: " + std::to_string(rc); return false; }
    std::unique_lock<std::mutex> lk(ctx.mtx);
    ctx.cv.wait(lk, [&ctx] { return ctx.done; });
    return true;
}

void Conversation::cancel() {
    if (conv_ptr_) lib_.conversation_cancel_process(conv_ptr_);
}

// ── Engine ────────────────────────────────────────────────────────────────────

Engine::~Engine() {
    if (engine_ptr_) { lib_.engine_delete(engine_ptr_); engine_ptr_ = nullptr; }
}

bool Engine::init(const Config& cfg, const std::string& dll_path) {
    cfg_ = cfg;

    if (!lib_.load(dll_path)) {
        error_ = "Failed to load DLL: " + dll_path;
        return false;
    }

    lib_.set_min_log_level(4); // ERROR only

    const char* backend_str = (cfg.backend == "cpu") ? "cpu" : "gpu";
    void* settings = lib_.engine_settings_create(
        cfg.model_path.c_str(), backend_str, nullptr, nullptr);
    if (!settings) { error_ = "engine_settings_create failed"; return false; }

    lib_.engine_settings_set_max_num_tokens(settings, cfg.max_tokens);

    engine_ptr_ = lib_.engine_create(settings);
    lib_.engine_settings_delete(settings);

    if (!engine_ptr_) { error_ = "engine_create failed"; return false; }
    return true;
}

std::unique_ptr<Conversation> Engine::conversation(const std::string& system_msg) {
    return std::make_unique<Conversation>(lib_, engine_ptr_, system_msg);
}

} // namespace litert
