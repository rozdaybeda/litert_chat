cmake_minimum_required(VERSION 3.21)
project(litert_chat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/litert.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Locate litert-lm DLLs ────────────────────────────────────────────────────
# litert-lm.dll is loaded dynamically at runtime (no link-time dep).
# We find it by asking Python where litert_lm is installed.
# Override with -DLITERT_PKG_DIR=<path> if needed.

if(NOT LITERT_PKG_DIR)
    execute_process(
        COMMAND python -c "import litert_lm, os; print(os.path.dirname(litert_lm.__file__))"
        OUTPUT_VARIABLE LITERT_PKG_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(LITERT_PKG_DIR AND EXISTS "${LITERT_PKG_DIR}")
    message(STATUS "litert_lm package: ${LITERT_PKG_DIR}")
    set(LITERT_DLLS
        "${LITERT_PKG_DIR}/litert-lm.dll"
        "${LITERT_PKG_DIR}/dxcompiler.dll"
        "${LITERT_PKG_DIR}/dxil.dll"
    )
    foreach(dll ${LITERT_DLLS})
        if(EXISTS "${dll}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying ${dll}"
            )
        endif()
    endforeach()
else()
    message(WARNING
        "litert_lm Python package not found. "
        "Install it with: pip install litert-lm-api  "
        "or set -DLITERT_PKG_DIR=<path> manually."
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup.py
    ${CMAKE_BINARY_DIR}/setup.py
    COPYONLY
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
