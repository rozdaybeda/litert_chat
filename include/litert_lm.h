#pragma once

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  using LibHandle = HMODULE;
  #define LIB_LOAD(p)     LoadLibraryA(p)
  #define LIB_SYM(h, s)  GetProcAddress((HMODULE)(h), s)
  #define LIB_FREE(h)     FreeLibrary((HMODULE)(h))
#else
  #include <dlfcn.h>
  using LibHandle = void*;
  #define LIB_LOAD(p)     dlopen(p, RTLD_NOW | RTLD_LOCAL)
  #define LIB_SYM(h, s)  dlsym(h, s)
  #define LIB_FREE(h)     dlclose(h)
#endif

#include <cstddef>
#include <string>

// ── C structs ─────────────────────────────────────────────────────────────────

struct LiteRtLmSamplerParams {
    int   type;        // 0=unspecified 1=top_k 2=top_p 3=greedy
    int   top_k;
    float top_p;
    float temperature;
    int   seed;
};

// ── Function pointer typedefs ─────────────────────────────────────────────────

using Fn_set_min_log_level                        = void(*)(int);

using Fn_engine_settings_create                   = void*(*)(const char*, const char*, const char*, const char*);
using Fn_engine_settings_delete                   = void(*)(void*);
using Fn_engine_settings_set_max_num_tokens       = void(*)(void*, int);

using Fn_engine_create                            = void*(*)(void*);
using Fn_engine_delete                            = void(*)(void*);

using Fn_session_config_create                    = void*(*)();
using Fn_session_config_delete                    = void(*)(void*);
using Fn_session_config_set_sampler_params        = void(*)(void*, const LiteRtLmSamplerParams*);

using Fn_conversation_config_create               = void*(*)();
using Fn_conversation_config_delete               = void(*)(void*);
using Fn_conversation_config_set_session_config   = void(*)(void*, void*);
using Fn_conversation_config_set_system_message   = void(*)(void*, const char*);

using Fn_conversation_create                      = void*(*)(void*, void*);
using Fn_conversation_delete                      = void(*)(void*);
using Fn_conversation_send_message                = void*(*)(void*, const char*, const char*, void*);

// chunk: incremental text; is_final: true on last call; error_msg: null on success
using StreamCallback = void(*)(void* user_data, const char* chunk, bool is_final, const char* error_msg);
using Fn_conversation_send_message_stream = int(*)(void*, const char*, const char*, void*, StreamCallback, void*);
using Fn_conversation_cancel_process      = void(*)(void*);

using Fn_json_response_delete                     = void(*)(void*);
using Fn_json_response_get_string                 = const char*(*)(void*);

// ── Dynamic loader ────────────────────────────────────────────────────────────

struct LiteRtLmLib {
    LibHandle handle = nullptr;

    Fn_set_min_log_level                      set_min_log_level = nullptr;
    Fn_engine_settings_create                 engine_settings_create = nullptr;
    Fn_engine_settings_delete                 engine_settings_delete = nullptr;
    Fn_engine_settings_set_max_num_tokens     engine_settings_set_max_num_tokens = nullptr;
    Fn_engine_create                          engine_create = nullptr;
    Fn_engine_delete                          engine_delete = nullptr;
    Fn_session_config_create                  session_config_create = nullptr;
    Fn_session_config_delete                  session_config_delete = nullptr;
    Fn_session_config_set_sampler_params      session_config_set_sampler_params = nullptr;
    Fn_conversation_config_create             conversation_config_create = nullptr;
    Fn_conversation_config_delete             conversation_config_delete = nullptr;
    Fn_conversation_config_set_session_config conversation_config_set_session_config = nullptr;
    Fn_conversation_config_set_system_message conversation_config_set_system_message = nullptr;
    Fn_conversation_create                    conversation_create = nullptr;
    Fn_conversation_delete                    conversation_delete = nullptr;
    Fn_conversation_send_message              conversation_send_message = nullptr;
    Fn_conversation_send_message_stream       conversation_send_message_stream = nullptr;
    Fn_conversation_cancel_process            conversation_cancel_process = nullptr;
    Fn_json_response_delete                   json_response_delete = nullptr;
    Fn_json_response_get_string               json_response_get_string = nullptr;

    bool load(const std::string& dll_path);

    ~LiteRtLmLib() {
        if (handle) { LIB_FREE(handle); handle = nullptr; }
    }

    LiteRtLmLib() = default;
    LiteRtLmLib(const LiteRtLmLib&) = delete;
    LiteRtLmLib& operator=(const LiteRtLmLib&) = delete;
};
