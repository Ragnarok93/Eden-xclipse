# SPDX-License-Identifier: GPL-3.0-or-later
# Explicitly opt-in: normal builds retain their existing compiler options.
set(EDEN_PGO_MODE "OFF" CACHE STRING "Android PGO: OFF, GENERATE, USE")
set_property(CACHE EDEN_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(EDEN_PGO_PROFILE "" CACHE FILEPATH "Merged LLVM instrumentation profile")
set(EDEN_PGO_BUILD_ID "local" CACHE STRING "Identity of the instrumented source/toolchain")

if(NOT EDEN_PGO_MODE MATCHES "^(OFF|GENERATE|USE)$")
    message(FATAL_ERROR "EDEN_PGO_MODE must be OFF, GENERATE, or USE")
endif()
if(EDEN_PGO_MODE STREQUAL "OFF")
    return()
endif()
if(NOT ANDROID OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Eden Android PGO requires Clang and arm64-v8a")
endif()
if(NOT EDEN_PGO_BUILD_ID MATCHES "^[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "EDEN_PGO_BUILD_ID contains unsupported characters")
endif()
if(EDEN_PGO_MODE STREQUAL "USE" AND NOT EXISTS "${EDEN_PGO_PROFILE}")
    message(FATAL_ERROR "PGO USE requires an existing EDEN_PGO_PROFILE")
endif()

# The test harness is excluded from OFF and USE builds; GENERATE only.
if(EDEN_PGO_MODE STREQUAL "GENERATE")
    target_sources(yuzu-android PRIVATE
        "${CMAKE_SOURCE_DIR}/src/android/app/src/main/jni/pgo_workloads.cpp")
endif()

# Apply PGO only to the translation units represented in this corpus. Do not
# classify untrained renderer submission, guest CPU, or kernel code as cold.
set(pgo_trained_sources
    "(video_core/textures/decoders|video_core/texture_cache/decode_bc|video_core/vulkan_common/vulkan_(device|device_profile|instance|wrapper)|bc_decoder/bc_decoder|common/dynamic_library|shader_recompiler/backend/spirv/.*|shader_recompiler/frontend/ir/(basic_block|ir_emitter|instruction|value)|shader_recompiler/ir_opt/(dead_code_elimination_pass|identity_removal_pass|verification_pass))\\.cpp$")
foreach(pgo_target video_core shader_recompiler common bc_decoder)
    if(NOT TARGET ${pgo_target})
        message(FATAL_ERROR "Missing PGO target: ${pgo_target}")
    endif()
    get_target_property(pgo_sources ${pgo_target} SOURCES)
    get_target_property(pgo_source_dir ${pgo_target} SOURCE_DIR)
    foreach(pgo_source IN LISTS pgo_sources)
        get_filename_component(pgo_absolute "${pgo_source}" ABSOLUTE BASE_DIR "${pgo_source_dir}")
        if(NOT pgo_absolute MATCHES "${pgo_trained_sources}")
            continue()
        endif()
        if(EDEN_PGO_MODE STREQUAL "GENERATE")
            set_property(SOURCE "${pgo_absolute}" TARGET_DIRECTORY ${pgo_target}
                APPEND PROPERTY COMPILE_OPTIONS -fprofile-instr-generate -fprofile-update=atomic)
        else()
            set_property(SOURCE "${pgo_absolute}" TARGET_DIRECTORY ${pgo_target}
                APPEND PROPERTY COMPILE_OPTIONS "-fprofile-instr-use=${EDEN_PGO_PROFILE}"
                -Wprofile-instr-unprofiled -Wprofile-instr-out-of-date
                -Werror=profile-instr-out-of-date)
        endif()
    endforeach()
endforeach()
target_compile_definitions(yuzu-android PRIVATE
    EDEN_PGO_BUILD_ID="${EDEN_PGO_BUILD_ID}"
    EDEN_PGO_COMPILER="${CMAKE_CXX_COMPILER_VERSION}")
if(EDEN_PGO_MODE STREQUAL "GENERATE")
    target_compile_definitions(yuzu-android PRIVATE EDEN_PGO_GENERATE=1)
    target_link_options(yuzu-android PRIVATE -fprofile-instr-generate)
endif()
