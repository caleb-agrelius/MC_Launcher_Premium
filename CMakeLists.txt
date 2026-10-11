cmake_minimum_required(VERSION 3.14)
project(MC_Launcher_Premium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Configuring MC_Launcher_Premium - prefer vcpkg-managed dependencies. If packages are missing, run vcpkg install for glfw3 glad imgui nlohmann-json curl miniz")

find_package(glfw3 REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(miniz CONFIG QUIET)

include(FetchContent)
FetchContent_Declare(
  imgui_backend_source
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui_backend_source)

add_executable(MC_Launcher_Premium
  src/main.cpp
  src/Model/Modpack.cpp
  src/Network/HttpClient.cpp
  src/Storage/Installer.cpp
  ${imgui_backend_source_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_backend_source_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(MC_Launcher_Premium PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${imgui_backend_source_SOURCE_DIR}/backends
)

target_link_libraries(MC_Launcher_Premium PRIVATE
  imgui::imgui
  glfw
  glad::glad
  nlohmann_json::nlohmann_json
  CURL::libcurl
)

if (miniz_FOUND)
  target_link_libraries(MC_Launcher_Premium PRIVATE miniz::miniz)
else()
  message(STATUS "miniz not found via find_package; Installer may fail. Consider: vcpkg install miniz")
endif()

if (WIN32)
  target_compile_definitions(MC_Launcher_Premium PRIVATE "_CRT_SECURE_NO_WARNINGS" "GLFW_INCLUDE_NONE")
endif()

message(STATUS "Configured MC_Launcher_Premium project (vcpkg mode)")
