cmake_minimum_required(VERSION 3.21)

# macOS 12 Monterey on Intel is the target of this port.  These must be set
# before project() so that every dependency built here uses them too.
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest macOS the app runs on")
  if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target architecture")
  endif()
endif()

project(CompressCompare
        VERSION 0.3.0
        DESCRIPTION "See where social media sites recompress your images and videos"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # consumed by clang-tidy and cppcheck

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CC_BUILD_TESTS "Build the command-line unit tests" ON)
option(CC_BUILD_APP   "Build the GUI application (needs OpenGL + GLFW)" ON)
option(CC_WITH_CURL   "Use libcurl to fetch URLs when available (WinHTTP is used on Windows otherwise)" ON)
option(CC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (Power of 10 rule 10)" ON)
option(CC_ENABLE_CLANG_TIDY "Run clang-tidy on every source file during the build" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

# ---------------------------------------------------------------------------
# Built-in configuration: config/config.yaml is embedded into the executable
# at build time (see cmake/EmbedFile.cmake).  Editing the YAML triggers a
# rebuild of cc_core, no reconfigure needed.
# ---------------------------------------------------------------------------
set(CC_CONFIG_YAML "${CMAKE_CURRENT_SOURCE_DIR}/config/config.yaml")
set(CC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CC_BUILTIN_CONFIG_H "${CC_GENERATED_DIR}/cc_builtin_config.h")
file(MAKE_DIRECTORY "${CC_GENERATED_DIR}")
add_custom_command(
  OUTPUT "${CC_BUILTIN_CONFIG_H}"
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${CC_CONFIG_YAML}
          -DOUTPUT=${CC_BUILTIN_CONFIG_H}
          -DSYMBOL=kBuiltinConfigYaml
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake"
  DEPENDS "${CC_CONFIG_YAML}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake"
  COMMENT "Embedding config/config.yaml as the built-in configuration"
  VERBATIM)

# ---------------------------------------------------------------------------
# Core library: everything that does not need a window (used by the app and
# the tests).  Kept dependency-light so it builds on a bare toolchain.
# ---------------------------------------------------------------------------
add_library(cc_core STATIC
  src/core/accel.cpp
  src/core/colormap.cpp
  src/core/config.cpp
  src/core/cv_ops.cpp
  src/core/ffmpeg.cpp
  src/core/fuzzy.cpp
  src/core/image.cpp
  src/core/jpeg_info.cpp
  src/core/metric.cpp
  src/core/metrics.cpp
  src/core/net.cpp
  src/core/process.cpp
  src/core/sites.cpp
  src/core/threading.cpp
  src/core/video_cv.cpp
  src/core/view_mode.cpp
  src/util/contract.cpp
  "${CC_BUILTIN_CONFIG_H}"
)
target_include_directories(cc_core PUBLIC src)
target_include_directories(cc_core SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_include_directories(cc_core PRIVATE "${CC_GENERATED_DIR}")
target_compile_features(cc_core PUBLIC cxx_std_17)
cc_apply_warnings(cc_core)
target_link_libraries(cc_core PUBLIC ${CC_YAML_TARGET} ${CC_OPENCV_LIBS})
if(APPLE)
  # ImageIO decodes HEIC/HEIF (and AVIF on macOS 13+) that OpenCV cannot
  target_sources(cc_core PRIVATE src/core/imageio_apple.cpp)
  target_link_libraries(cc_core PUBLIC "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
endif()

# feature macros are always defined (0 or 1) so that -Wundef can stay on
target_compile_definitions(cc_core PUBLIC
  CC_HAVE_CURL=$<BOOL:${CC_HAVE_CURL}>)
if(CC_HAVE_CURL)
  target_link_libraries(cc_core PUBLIC CURL::libcurl)
  target_compile_definitions(cc_core PUBLIC CC_HAVE_WINHTTP=0)
elseif(WIN32)
  target_link_libraries(cc_core PUBLIC winhttp)
  target_compile_definitions(cc_core PUBLIC CC_HAVE_WINHTTP=1)
else()
  target_compile_definitions(cc_core PUBLIC CC_HAVE_WINHTTP=0)
endif()
if(WIN32)
  target_compile_definitions(cc_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
find_package(Threads REQUIRED)
target_link_libraries(cc_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
if(CC_BUILD_APP)
  set(CC_APP_ICON "")
  if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/CompressCompare.icns")
    set(CC_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/CompressCompare.icns")
  endif()
  add_executable(compresscompare WIN32 MACOSX_BUNDLE
    ${CC_APP_ICON}
    "${CC_CONFIG_YAML}"
    src/main.cpp
    src/app/app.cpp
    src/app/ui_slots.cpp
    src/app/ui_compare.cpp
    src/app/ui_panels.cpp
    src/app/fuzzy_combo.cpp
    src/gpu/gl.cpp
    src/gpu/renderer.cpp
    src/platform/dialogs.cpp
  )
  target_link_libraries(compresscompare PRIVATE cc_core ${CC_IMGUI_TARGET} ${CC_GLFW_TARGET} OpenGL::GL)
  target_include_directories(compresscompare PRIVATE src)
  # the renderer loads its own OpenGL 3.3 entry points (src/gpu/gl.h); GLFW must not pull in system GL headers
  target_compile_definitions(compresscompare PRIVATE GLFW_INCLUDE_NONE)
  cc_apply_warnings(compresscompare)
  if(WIN32)
    # WIN32 above gives a GUI subsystem (no console window pops up); we keep a
    # normal `main()` by asking the linker for the console-style entry point.
    if(MSVC)
      target_link_options(compresscompare PRIVATE "/ENTRY:mainCRTStartup")
    else()
      # MinGW: -mwindows hides the console; its CRT still calls main().
      target_link_options(compresscompare PRIVATE "-mwindows")
    endif()
    target_link_libraries(compresscompare PRIVATE comdlg32 ole32 shell32)
  endif()
  if(APPLE)
    target_link_libraries(compresscompare PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    # OpenGL is deprecated (not removed) on macOS; the app uses a 3.3 core context
    target_compile_definitions(compresscompare PRIVATE GL_SILENCE_DEPRECATION)
    set_target_properties(compresscompare PROPERTIES
      OUTPUT_NAME "CompressCompare"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "CompressCompare"
      MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.compresscompare"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_ICON_FILE "CompressCompare.icns")
    # the icon goes into Contents/Resources; the settings file is created per user
    # (~/Library/Application Support/CompressCompare/config.yaml, Settings > edit config.yaml)
    if(CC_APP_ICON)
      set_source_files_properties(${CC_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
  endif()
  set_source_files_properties("${CC_CONFIG_YAML}" PROPERTIES HEADER_FILE_ONLY ON)

  install(TARGETS compresscompare RUNTIME DESTINATION bin BUNDLE DESTINATION .)
  if(WIN32)
    # ship the vcpkg DLLs (curl, jpeg, webp, glfw, imgui, yaml-cpp, zlib) with the exe
    install(FILES $<TARGET_RUNTIME_DLLS:compresscompare> DESTINATION bin)
  endif()
  if(NOT APPLE)
    # the editable copy of the configuration lives next to the executable
    install(FILES "${CC_CONFIG_YAML}" DESTINATION bin)
  endif()
  install(FILES README.md THIRD_PARTY_NOTICES.md LICENSE DESTINATION .)
  install(DIRECTORY docs/ DESTINATION docs)
endif()

# ---------------------------------------------------------------------------
# Tests (no window needed)
# ---------------------------------------------------------------------------
if(CC_BUILD_TESTS)
  enable_testing()
  foreach(t config fuzzy metrics jpeg_info ffprobe sites opencl video)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE cc_core)
    cc_apply_warnings(test_${t})
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
  target_compile_definitions(test_video PRIVATE CC_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")
  # the same GPU-vs-CPU comparison on an OpenCL CPU device (Apple's, or PoCL on Linux CI)
  add_test(NAME opencl_cpu_device COMMAND test_opencl)
  set_tests_properties(opencl_cpu_device PROPERTIES ENVIRONMENT "OPENCV_OPENCL_DEVICE=:CPU:")
endif()

# Benchmark of the CPU and OpenCL paths (tools/cc_bench.cpp)
add_executable(cc_bench tools/cc_bench.cpp)
target_link_libraries(cc_bench PRIVATE cc_core)
cc_apply_warnings(cc_bench)

# ---------------------------------------------------------------------------
# Static analysis and documentation (Power of 10 rule 10)
# ---------------------------------------------------------------------------
set(CC_ANALYSED_SOURCES
  src/core src/util src/app src/gpu src/platform src/main.cpp)

if(CC_ENABLE_CLANG_TIDY)
  find_program(CC_CLANG_TIDY NAMES clang-tidy clang-tidy-18 clang-tidy-17)
  if(CC_CLANG_TIDY)
    set_target_properties(cc_core PROPERTIES CXX_CLANG_TIDY "${CC_CLANG_TIDY}")
    if(TARGET compresscompare)
      set_target_properties(compresscompare PROPERTIES CXX_CLANG_TIDY "${CC_CLANG_TIDY}")
    endif()
    message(STATUS "clang-tidy: ${CC_CLANG_TIDY} runs during the build")
  else()
    message(WARNING "CC_ENABLE_CLANG_TIDY is ON but clang-tidy was not found")
  endif()
endif()

find_program(CC_CPPCHECK NAMES cppcheck)
if(CC_CPPCHECK)
  add_custom_target(cppcheck
    COMMAND "${CC_CPPCHECK}"
            --enable=warning,style,performance,portability
            --std=c++17 --inline-suppr --quiet --error-exitcode=1
            --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression
            --suppress=*:*/generated/*
            # explicit bounded loops are preferred over algorithms (Power of 10 rule 2)
            --suppress=useStlAlgorithm
            # `return r.fail(...)` always returns false by design (the message is the point)
            --suppress=knownConditionTrueFalse:src/core/config.cpp
            -I "${CMAKE_CURRENT_SOURCE_DIR}/src" -I "${CC_GENERATED_DIR}"
            -D CC_HAVE_CURL=1 -D CC_HAVE_WINHTTP=0
            ${CC_ANALYSED_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running cppcheck over the project sources"
    VERBATIM)
endif()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating the API reference with Doxygen into build/doxygen"
    VERBATIM)
endif()
