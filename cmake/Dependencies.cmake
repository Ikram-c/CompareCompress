# Dependency resolution and compiler settings.
#
# OpenCV comes from deps/opencv (scripts/build-deps.sh) or any installed
# OpenCV 4.6+.  The small libraries are looked up with find_package() and
# pkg-config and otherwise fetched with FetchContent (Dear ImGui always; GLFW
# and yaml-cpp when missing), so a bare macOS with the Xcode Command Line
# Tools and CMake is enough.
#
# Every third-party include directory is marked SYSTEM so that the strict
# warning set below only judges this project's own sources.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Warnings (Power of 10 rule 10: all warnings on, warnings are errors).
# ---------------------------------------------------------------------------
function(cc_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus /external:anglebrackets /external:W0)
    if(CC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wformat=2 -Wundef
      -Wdouble-promotion -Wnull-dereference -Wnon-virtual-dtor -Wold-style-cast -Wimplicit-fallthrough)
    if(CC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Marks a target's interface include directories as system headers for its
# consumers (silences third-party warnings under the strict flags).
function(cc_mark_system target)
  if(TARGET ${target})
    get_target_property(_aliased ${target} ALIASED_TARGET)
    if(_aliased)
      set(target ${_aliased})
    endif()
    get_target_property(_imported ${target} IMPORTED)
    if(NOT _imported AND NOT CMAKE_VERSION VERSION_LESS 3.25)
      set_target_properties(${target} PROPERTIES SYSTEM ON)
    endif()
  endif()
endfunction()

# ---- yaml-cpp (required) --------------------------------------------------
find_package(yaml-cpp CONFIG QUIET)
if(TARGET yaml-cpp::yaml-cpp)
  set(CC_YAML_TARGET yaml-cpp::yaml-cpp)
  message(STATUS "yaml-cpp: found via find_package (${yaml-cpp_VERSION})")
elseif(TARGET yaml-cpp)
  set(CC_YAML_TARGET yaml-cpp)
  message(STATUS "yaml-cpp: found via find_package (legacy target name)")
else()
  message(STATUS "yaml-cpp: not found; fetching 0.8.0 from GitHub and building it")
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
  set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(yaml-cpp)
  set(CC_YAML_TARGET yaml-cpp)
  cc_mark_system(yaml-cpp)
endif()

# ---- OpenCV (required: core, imgproc, imgcodecs, videoio) --------------------
# scripts/build-deps.sh builds a static OpenCV into deps/opencv; a system or
# package-manager OpenCV 4.6+ works as well (pass -DOpenCV_DIR=...).
if(NOT OpenCV_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv")
  file(GLOB _cc_opencv_cfg "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv/lib/cmake/opencv4/OpenCVConfig.cmake"
                          "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv/lib64/cmake/opencv4/OpenCVConfig.cmake")
  if(_cc_opencv_cfg)
    list(GET _cc_opencv_cfg 0 _cc_opencv_first)
    get_filename_component(OpenCV_DIR "${_cc_opencv_first}" DIRECTORY)
  endif()
endif()
find_package(OpenCV 4.6 REQUIRED COMPONENTS core imgproc imgcodecs videoio)
message(STATUS "OpenCV ${OpenCV_VERSION}: ${OpenCV_DIR}")
set(CC_OPENCV_LIBS opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)

# ---- libcurl (optional; WinHTTP fallback on Windows) ----------------------
set(CC_HAVE_CURL OFF)
if(CC_WITH_CURL)
  find_package(CURL QUIET)
  if(TARGET CURL::libcurl)
    set(CC_HAVE_CURL ON)
    message(STATUS "libcurl: found -> URL fetching via curl")
  elseif(WIN32)
    message(STATUS "libcurl: not found -> URL fetching via WinHTTP")
  else()
    message(STATUS "libcurl: not found -> URL fetching disabled")
  endif()
endif()

if(NOT CC_BUILD_APP)
  return()
endif()

# ---- OpenGL ---------------------------------------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---- GLFW -----------------------------------------------------------------
find_package(glfw3 CONFIG QUIET)
if(TARGET glfw)
  set(CC_GLFW_TARGET glfw)
  message(STATUS "GLFW: found via find_package(glfw3)")
else()
  if(NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
    endif()
  endif()
  if(GLFW_FOUND)
    set(CC_GLFW_TARGET PkgConfig::GLFW)
    message(STATUS "GLFW: found via pkg-config")
  else()
    message(STATUS "GLFW: not found; fetching 3.4 from GitHub and building it")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG        3.4
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)
    set(CC_GLFW_TARGET glfw)
    cc_mark_system(glfw)
  endif()
endif()

# ---- Dear ImGui -----------------------------------------------------------
find_package(imgui CONFIG QUIET)
if(TARGET imgui::imgui)
  # vcpkg port with features glfw-binding + opengl3-binding: the backends are
  # compiled into the library and their headers installed alongside imgui.h.
  set(CC_IMGUI_TARGET imgui::imgui)
  message(STATUS "Dear ImGui: found via find_package(imgui) (vcpkg)")
else()
  message(STATUS "Dear ImGui: not found; fetching v1.92.9b from GitHub")
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(imgui)
  add_library(cc_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(cc_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(cc_imgui PUBLIC ${CC_GLFW_TARGET} OpenGL::GL)
  target_compile_definitions(cc_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  set(CC_IMGUI_TARGET cc_imgui)
endif()
