cmake_minimum_required(VERSION 3.20)
file(STRINGS "./VERSION" RAW_VERSION)
string(STRIP "${RAW_VERSION}" VERSION)
message(STATUS "Building xs version ${VERSION}")
project(xs VERSION ${VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Feature options 
option(XS_ENABLE_VM         "Enable bytecode VM"          ON)
option(XS_ENABLE_JIT        "Enable JIT compiler"         ON)
option(XS_ENABLE_PLUGINS    "Enable plugin system"        ON)
option(XS_ENABLE_SANDBOX    "Enable sandbox"              ON)
option(XS_ENABLE_TRACER     "Enable tracer"               ON)
option(XS_ENABLE_LSP        "Enable language server"      ON)
option(XS_ENABLE_DAP        "Enable debug adapter"        ON)
option(XS_ENABLE_EFFECTS    "Enable effects system"       ON)
option(XS_ENABLE_TRANSPILER "Enable transpiler"           ON)
option(XS_ENABLE_FMT        "Enable code formatter"       ON)
option(XS_ENABLE_PKG        "Enable package manager"      ON)
option(XS_ENABLE_PROFILER   "Enable profiler"             ON)
option(XS_ENABLE_COVERAGE   "Enable coverage"             ON)
option(XS_ENABLE_DOC        "Enable doc generator"        ON)

# Platform detection 
if(WIN32)
  add_compile_definitions(_USE_MINGW_ANSI_STDIO=1 _WIN32)
endif()

# Common flags 
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Interface: includes + feature defines 
# Feature defines live here so every translation unit sees the same
# type layout (e.g. XS_CLOSURE under XSC_ENABLE_VM).
add_library(xs_defs INTERFACE)
target_include_directories(xs_defs INTERFACE src src/tls/bearssl)

if(XS_ENABLE_VM)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_VM)
endif()
if(XS_ENABLE_JIT)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_JIT)
endif()
if(XS_ENABLE_PLUGINS)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_PLUGINS)
endif()
if(XS_ENABLE_SANDBOX)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_SANDBOX)
endif()
if(XS_ENABLE_TRACER)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_TRACER)
endif()
if(XS_ENABLE_LSP)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_LSP)
endif()
if(XS_ENABLE_DAP)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_DAP)
endif()
if(XS_ENABLE_EFFECTS)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_EFFECTS)
endif()
if(XS_ENABLE_TRANSPILER)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_TRANSPILER)
endif()
if(XS_ENABLE_FMT)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_FMT)
endif()
if(XS_ENABLE_PKG)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_PKG)
endif()
if(XS_ENABLE_PROFILER)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_PROFILER)
endif()
if(XS_ENABLE_COVERAGE)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_COVERAGE)
endif()
if(XS_ENABLE_DOC)
  target_compile_definitions(xs_defs INTERFACE XSC_ENABLE_DOC)
endif()

# BearSSL per-file warning suppression
set(BEARSSL_NO_WARN -Wno-maybe-uninitialized -Wno-unknown-pragmas)

# OBJECT libraries 
# OBJECT libraries produce .o files without archiving, so the linker
# sees every symbol regardless of dependency order.  CMake 3.20+
# supports $<TARGET_OBJECTS:> in target_link_libraries.

add_library(xs_core OBJECT
  src/core/value.c
  src/core/ast.c
  src/core/lexer.c
  src/core/env.c
  src/core/parser.c
  src/core/xs_bigint.c
  src/core/gc.c
  src/core/gc_concurrent.c
  src/core/utf8.c
  src/core/strbuf.c
  src/core/limits.c
  src/core/regex.c
  src/core/msgpack.c
)
target_link_libraries(xs_core PRIVATE xs_defs)

add_library(xs_compiler OBJECT
  src/compiler/match_compiler.c
  src/types/inference.c
  src/types/checker.c
  src/types/types.c
  src/semantic/exhaust.c
  src/semantic/symtable.c
  src/semantic/resolve.c
  src/semantic/typecheck.c
  src/semantic/cache.c
  src/semantic/scoped_check.c
  src/semantic/purity.c
  src/semantic/sema.c
  src/plugins/plugin_parse.c
)
target_link_libraries(xs_compiler PRIVATE xs_defs)

add_library(xs_runtime OBJECT
  # Interpreter
  src/runtime/interp.c
  src/runtime/interp_ast.c
  src/runtime/interp_enum.c
  src/runtime/interp_nodemap.c
  src/runtime/interp_typecheck.c
  src/runtime/interp_derive.c
  # Builtins
  src/runtime/builtins.c
  src/runtime/builtins_math.c
  src/runtime/builtins_time.c
  src/runtime/builtins_string.c
  src/runtime/builtins_path.c
  src/runtime/builtins_base64.c
  src/runtime/builtins_hash.c
  src/runtime/builtins_uuid.c
  src/runtime/builtins_random.c
  src/runtime/builtins_json.c
  src/runtime/builtins_csv.c
  src/runtime/builtins_url.c
  src/runtime/builtins_toml.c
  src/runtime/builtins_log.c
  src/runtime/builtins_fmt.c
  src/runtime/builtins_test.c
  src/runtime/builtins_re.c
  src/runtime/builtins_tracing.c
  src/runtime/builtins_collections.c
  src/runtime/builtins_process.c
  src/runtime/builtins_io.c
  src/runtime/builtins_os.c
  src/runtime/builtins_async.c
  src/runtime/builtins_net.c
  src/runtime/builtins_crypto.c
  src/runtime/builtins_thread.c
  src/runtime/builtins_buf.c
  src/runtime/builtins_encode.c
  src/runtime/builtins_db.c
  src/runtime/builtins_cli.c
  src/runtime/builtins_ffi.c
  src/runtime/builtins_reflect.c
  src/runtime/builtins_gc.c
  src/runtime/builtins_http.c
  src/runtime/builtins_fs.c
  # Runtime infrastructure
  src/runtime/error.c
  src/runtime/stdlib.c
  src/runtime/scheduler.c
  src/runtime/event_loop.c
  src/runtime/concurrent.c
  src/runtime/triggers.c
  # Async
  src/runtime/async.c
  # Diagnostics
  src/diagnostic/diagnostic.c
  src/diagnostic/render.c
  src/diagnostic/colorize.c
  src/diagnostic/explain.c
  # Embed
  src/xs_embed.c
  # Lint
  src/lint/lint.c
  # Tools
  src/fmt/fmt.c
  src/doc/docgen.c
  src/pkg/pkg.c
  src/pkg/pkg_http.c
  src/pkg/self.c
  src/profiler/profiler.c
  src/coverage/coverage.c
  src/optimizer/optimizer.c
  src/optimizer/ssa.c
  src/optimizer/inline_cache.c
  src/ir/ir.c
  src/repl/repl.c
  # DB
  src/db/xsdb.c
  # Net
  src/net/http_server.c
  src/net/http_tls.c
  src/net/sock_compat.c
  # TLS
  src/tls/xs_tls.c
  src/tls/xs_ca_bundle.c
)
target_link_libraries(xs_runtime PRIVATE xs_defs)

# Conditional feature sources 
if(XS_ENABLE_VM)
  file(GLOB _vm_srcs src/vm/*.c)
  target_sources(xs_runtime PRIVATE ${_vm_srcs})
endif()

if(XS_ENABLE_JIT)
  file(GLOB _jit_srcs src/jit/*.c)
  target_sources(xs_runtime PRIVATE ${_jit_srcs})
endif()

if(XS_ENABLE_PLUGINS)
  # plugin_parse.c is in xs_compiler; exclude from runtime glob
  file(GLOB _plugin_srcs src/plugins/*.c)
  list(REMOVE_ITEM _plugin_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/plugin_parse.c)
  target_sources(xs_runtime PRIVATE ${_plugin_srcs})
endif()

if(XS_ENABLE_TRACER)
  file(GLOB _tracer_srcs src/tracer/*.c)
  target_sources(xs_runtime PRIVATE ${_tracer_srcs})
endif()

if(XS_ENABLE_LSP)
  file(GLOB _lsp_srcs src/lsp/*.c)
  target_sources(xs_runtime PRIVATE ${_lsp_srcs})
endif()

if(XS_ENABLE_DAP)
  file(GLOB _dap_srcs src/dap/*.c)
  target_sources(xs_runtime PRIVATE ${_dap_srcs})
endif()

if(XS_ENABLE_EFFECTS)
  file(GLOB _effects_srcs src/effects/*.c)
  target_sources(xs_runtime PRIVATE ${_effects_srcs})
endif()

if(XS_ENABLE_TRANSPILER)
  file(GLOB _transpiler_srcs src/transpiler/*.c)
  target_sources(xs_runtime PRIVATE ${_transpiler_srcs})
endif()

# BearSSL sources 
file(GLOB BEARSSL_SRCS
  src/tls/bearssl/*.c
  src/tls/bearssl/**/*.c
)
target_sources(xs_runtime PRIVATE ${BEARSSL_SRCS})

foreach(_bsrc ${BEARSSL_SRCS})
  set_source_files_properties(${_bsrc} PROPERTIES
    COMPILE_OPTIONS "${BEARSSL_NO_WARN}"
  )
endforeach()

# Executable 
# Link all OBJECT libraries + main.c into a single binary.
# $<TARGET_OBJECTS:> flattens .o files directly onto the link line,
# avoiding static-library ordering issues.
add_executable(xs
  src/main.c
  $<TARGET_OBJECTS:xs_core>
  $<TARGET_OBJECTS:xs_compiler>
  $<TARGET_OBJECTS:xs_runtime>
)
target_compile_definitions(xs PRIVATE XS_VERSION="${VERSION}")
target_link_libraries(xs PRIVATE xs_defs)

# Platform link libraries
if(WIN32)
  target_link_libraries(xs PRIVATE ws2_32 psapi)
  if(MINGW)
    target_link_options(xs PRIVATE -static)
  endif()
elseif(UNIX AND NOT APPLE)
  target_link_libraries(xs PRIVATE dl pthread m)
elseif(APPLE)
  target_link_libraries(xs PRIVATE pthread m)
endif()

# Build type
# Debug -> ASan + UBSan
# Release -> -O3, LTO, NDEBUG
set(CMAKE_C_FLAGS_DEBUG
  "-g -O0 -fsanitize=address -fsanitize=undefined -DDEBUG"
)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG
  "-fsanitize=address -fsanitize=undefined"
)
set(CMAKE_C_FLAGS_RELEASE
  "-O3 -DNDEBUG -flto"
)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -s")
if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")
endif()
