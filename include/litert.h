#pragma once

#include "litert_lm.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace litert {

struct Config {
    std::string model_path;
    std::string backend    = "gpu";   // "cpu" or "gpu"
    int         max_tokens = 4096;
};

// ── Conversation ──────────────────────────────────────────────────────────────

class Conversation {
public:
    explicit Conversation(LiteRtLmLib& lib, void* engine_ptr,
                          const std::string& system_message = "");
    ~Conversation();

    Conversation(const Conversation&) = delete;
    Conversation& operator=(const Conversation&) = delete;

    // Blocking: send a user message, return the full assistant reply.
    std::string send(const std::string& text);

    // Streaming: invoke on_chunk for each incremental text piece.
    // Returns false on error.
    bool send_stream(const std::string& text,
                     std::function<void(std::string_view chunk, bool is_final)> on_chunk);

    void cancel();

    const std::string& last_error() const { return error_; }

private:
    LiteRtLmLib& lib_;
    void*        conv_ptr_ = nullptr;
    std::string  error_;
};

// ── Engine ────────────────────────────────────────────────────────────────────

class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(const Config& cfg, const std::string& dll_path);

    // Creates a fresh one-shot conversation (closed automatically on destruction).
    std::unique_ptr<Conversation> conversation(const std::string& system_msg = "");

    bool               ready()      const { return engine_ptr_ != nullptr; }
    const std::string& last_error() const { return error_; }
    const Config&      config()     const { return cfg_; }

private:
    LiteRtLmLib lib_;
    void*       engine_ptr_ = nullptr;
    Config      cfg_;
    std::string error_;
};

} // namespace litert
