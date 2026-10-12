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
find_package(WebP CONFIG REQUIRED)
find_package(Stb REQUIRED)

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
  src/Platform.cpp
  src/Game/GameFiles.cpp
  src/Game/GameOptions.cpp
  src/Game/Launcher.cpp
  src/Game/Loaders.cpp
  src/Game/PackInstance.cpp
  src/Model/Modpack.cpp
  src/Network/AtLauncher.cpp
  src/Network/CurseForge.cpp
  src/Network/Ftb.cpp
  src/Network/HttpClient.cpp
  src/Network/MicrosoftAuth.cpp
  src/Network/Modrinth.cpp
  src/Network/PackSources.cpp
  src/Network/Technic.cpp
  src/Storage/Account.cpp
  src/Storage/InstallPlan.cpp
  src/Storage/Installer.cpp
  src/Storage/Library.cpp
  src/Storage/Settings.cpp
  src/UI/ImageCache.cpp
  src/UI/SettingsPage.cpp
  src/UI/Theme.cpp
  src/UI/Toasts.cpp
  src/UI/Widgets.cpp
  src/Util/Hash.cpp
  src/Util/Paths.cpp
  src/Util/Process.cpp
  src/Util/Sha1.cpp
  ${imgui_backend_source_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_backend_source_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(MC_Launcher_Premium PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${imgui_backend_source_SOURCE_DIR}/backends
  ${Stb_INCLUDE_DIR}
)

target_link_libraries(MC_Launcher_Premium PRIVATE
  imgui::imgui
  glfw
  glad::glad
  nlohmann_json::nlohmann_json
  CURL::libcurl
  WebP::webp
)

if (miniz_FOUND)
  target_link_libraries(MC_Launcher_Premium PRIVATE miniz::miniz)
else()
  message(STATUS "miniz not found via find_package; Installer may fail. Consider: vcpkg install miniz")
endif()

if (WIN32)
  target_compile_definitions(MC_Launcher_Premium PRIVATE "_CRT_SECURE_NO_WARNINGS" "GLFW_INCLUDE_NONE")
  target_link_libraries(MC_Launcher_Premium PRIVATE bcrypt shell32 ws2_32)
endif()

# Visual Studio: make F5 launch the app (not ALL_BUILD), running from the repo root.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MC_Launcher_Premium)
set_target_properties(MC_Launcher_Premium PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Configured MC_Launcher_Premium project (vcpkg mode)")
