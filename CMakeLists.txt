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

# Each OS has its own definitions of the Platform functions (src/Platform/Platform.h).
if (WIN32)
  set(PLATFORM_SOURCES src/Platform/Platform_win32.cpp)
else()
  set(PLATFORM_SOURCES src/Platform/Platform_posix.cpp)
endif()

add_executable(MC_Launcher_Premium
  src/main.cpp
  ${PLATFORM_SOURCES}
  src/App/AppRoot.cpp
  src/App/Application.cpp
  src/App/GlfwWindow.cpp
  src/App/ThemeController.cpp
  src/Core/TaskScope.cpp
  src/Core/UiQueue.cpp
  src/Core/WorkerPool.cpp
  src/Services/Account/AccountService.cpp
  src/Services/Discover/DiscoverService.cpp
  src/Services/Discover/OptionalModsService.cpp
  src/Services/Discover/PackDetailsService.cpp
  src/Services/Downloads/DownloadService.cpp
  src/Services/Library/CustomPackService.cpp
  src/Services/Library/PackContentService.cpp
  src/Services/Library/PackLibrary.cpp
  src/Services/Library/UninstallService.cpp
  src/Services/Play/PlayService.cpp
  src/Services/Servers/ServerService.cpp
  src/Services/Settings/JavaScanService.cpp
  src/Services/Settings/SettingsStore.cpp
  src/Services/Settings/SharedFilesService.cpp
  src/Services/Updates/UpdateService.cpp
  src/UI/Common/UiHelpers.cpp
  src/UI/Common/VersionPicker.cpp
  src/UI/Dialogs/DeleteServerDialog.cpp
  src/UI/Dialogs/DialogHost.cpp
  src/UI/Dialogs/LaunchSettingsDialog.cpp
  src/UI/Dialogs/NewPackDialogHost.cpp
  src/UI/Dialogs/OptionalModsDialog.cpp
  src/UI/Dialogs/StopGameDialog.cpp
  src/UI/Dialogs/UninstallDialog.cpp
  src/UI/Dialogs/UpdateDialog.cpp
  src/UI/Pages/Discover/DiscoverDetails.cpp
  src/UI/Pages/Discover/DiscoverFilterBar.cpp
  src/UI/Pages/Discover/DiscoverPage.cpp
  src/UI/Pages/Discover/DiscoverResultList.cpp
  src/UI/Pages/Discover/PackInfoTabs.cpp
  src/UI/Pages/Discover/PackInstallSection.cpp
  src/UI/Pages/Discover/PackPlayButton.cpp
  src/UI/Pages/Downloads/DownloadsPage.cpp
  src/UI/Pages/Library/LibraryPackCard.cpp
  src/UI/Pages/Library/LibraryPage.cpp
  src/UI/Pages/Manage/ChangelogTab.cpp
  src/UI/Pages/Manage/ContentBrowser.cpp
  src/UI/Pages/Manage/ContentModel.cpp
  src/UI/Pages/Manage/ContentTab.cpp
  src/UI/Pages/Manage/LogReader.cpp
  src/UI/Pages/Manage/LogsTab.cpp
  src/UI/Pages/Manage/ManageHeader.cpp
  src/UI/Pages/Manage/ManagePage.cpp
  src/UI/Pages/Manage/ManagePageHost.cpp
  src/UI/Pages/Manage/OverviewCards.cpp
  src/UI/Pages/Manage/OverviewTab.cpp
  src/UI/Pages/Manage/ScreenshotsTab.cpp
  src/UI/Pages/Servers/DockerStatusView.cpp
  src/UI/Pages/Servers/ServerConsole.cpp
  src/UI/Pages/Servers/ServerDetails.cpp
  src/UI/Pages/Servers/ServerDockerTab.cpp
  src/UI/Pages/Servers/ServerFilesCard.cpp
  src/UI/Pages/Servers/ServerList.cpp
  src/UI/Pages/Servers/ServerModsTab.cpp
  src/UI/Pages/Servers/ServerRunCard.cpp
  src/UI/Pages/Servers/ServerSettingsTab.cpp
  src/UI/Pages/Servers/ServersPage.cpp
  src/UI/Pages/Settings/AboutTab.cpp
  src/UI/Pages/Settings/AccountTab.cpp
  src/UI/Pages/Settings/AppearanceTab.cpp
  src/UI/Pages/Settings/DownloadsTab.cpp
  src/UI/Pages/Settings/GameOptionsTab.cpp
  src/UI/Pages/Settings/LaunchSettingsCards.cpp
  src/UI/Pages/Settings/MinecraftTab.cpp
  src/UI/Pages/Settings/PackLaunchSettings.cpp
  src/UI/Pages/Settings/SettingsLayout.cpp
  src/UI/Pages/Settings/SettingsPage.cpp
  src/UI/Pages/Settings/SettingsPageHost.cpp
  src/UI/Pages/Settings/ThemeEditor.cpp
  src/UI/Shell/AccountPanel.cpp
  src/UI/Shell/AppShell.cpp
  src/UI/Shell/Mascot.cpp
  src/UI/Shell/Shortcuts.cpp
  src/UI/Shell/Sidebar.cpp
  src/Game/GameFiles.cpp
  src/Game/GameOptions.cpp
  src/Game/InstanceContent.cpp
  src/Game/JarMods.cpp
  src/Game/JavaFinder.cpp
  src/Game/Launcher.cpp
  src/Game/Loaders.cpp
  src/Game/PackCreator.cpp
  src/Game/PackInstance.cpp
  src/Game/ServerSetup.cpp
  src/Model/Modpack.cpp
  src/Network/AtLauncher.cpp
  src/Network/CurseForge.cpp
  src/Network/Ftb.cpp
  src/Network/HttpClient.cpp
  src/Network/MicrosoftAuth.cpp
  src/Network/Modrinth.cpp
  src/Network/ModSearch.cpp
  src/Network/PackSources.cpp
  src/Network/Technic.cpp
  src/Storage/Account.cpp
  src/Storage/InstallPlan.cpp
  src/Storage/Installer.cpp
  src/Storage/Library.cpp
  src/Storage/Settings.cpp
  src/UI/MobModel.cpp
  src/UI/Mobs.cpp
  src/UI/ImageCache.cpp
  src/UI/LogView.cpp
  src/UI/Logo.cpp
  src/UI/NewPackDialog.cpp
  src/UI/Theme.cpp
  src/UI/Toasts.cpp
  src/UI/Widgets.cpp
  src/Util/Hash.cpp
  src/Util/Paths.cpp
  src/Util/Process.cpp
  src/Util/Sha1.cpp
  src/Util/SharedFiles.cpp
  src/Util/Text.cpp
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
  # The executable's icon (resources/logo.ico, written by logo_export).
  target_sources(MC_Launcher_Premium PRIVATE resources/app.rc)
  target_compile_definitions(MC_Launcher_Premium PRIVATE "_CRT_SECURE_NO_WARNINGS" "GLFW_INCLUDE_NONE")
  target_link_libraries(MC_Launcher_Premium PRIVATE bcrypt shell32 ws2_32)
endif()

# Visual Studio: make F5 launch the app (not ALL_BUILD), running from the repo root.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MC_Launcher_Premium)
set_target_properties(MC_Launcher_Premium PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Writes resources/logo.ico and resources/logo.png from src/UI/Logo.cpp. Run it from the repository root.
add_executable(logo_export tools/LogoExport.cpp src/UI/Logo.cpp)
target_include_directories(logo_export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Stb_INCLUDE_DIR})

message(STATUS "Configured MC_Launcher_Premium project (vcpkg mode)")

include(CTest)
if (BUILD_TESTING)
  add_executable(launcher_checks
    tests/LauncherChecks.cpp
    ${PLATFORM_SOURCES}
    src/Game/GameFiles.cpp
    src/Game/GameOptions.cpp
    src/Game/InstanceContent.cpp
    src/Game/JarMods.cpp
    src/Game/JavaFinder.cpp
    src/Game/Loaders.cpp
    src/Game/PackCreator.cpp
    src/Game/PackInstance.cpp
    src/Game/ServerSetup.cpp
    src/Model/Modpack.cpp
    src/Network/HttpClient.cpp
    src/Storage/Installer.cpp
    src/Storage/Library.cpp
    src/Storage/Settings.cpp
    src/UI/ImageCache.cpp
    src/UI/Logo.cpp
    src/UI/Theme.cpp
    src/UI/Widgets.cpp
    src/Util/Hash.cpp
    src/Util/Paths.cpp
    src/Util/Process.cpp
    src/Util/Sha1.cpp
    src/Util/SharedFiles.cpp
    src/Util/Text.cpp
  )
  get_target_property(launcher_includes MC_Launcher_Premium INCLUDE_DIRECTORIES)
  get_target_property(launcher_libraries MC_Launcher_Premium LINK_LIBRARIES)
  target_include_directories(launcher_checks PRIVATE ${launcher_includes})
  target_link_libraries(launcher_checks PRIVATE ${launcher_libraries})
  if (WIN32)
    target_compile_definitions(launcher_checks PRIVATE _CRT_SECURE_NO_WARNINGS GLFW_INCLUDE_NONE)
  endif()
  add_test(NAME launcher_checks COMMAND launcher_checks)
  set_tests_properties(launcher_checks PROPERTIES TIMEOUT 30)

  # Checks against the real pack APIs. Not a CTest test: they need the internet.
  add_executable(live_checks
    tests/LiveChecks.cpp
    ${PLATFORM_SOURCES}
    src/Game/GameFiles.cpp
    src/Game/GameOptions.cpp
    src/Game/InstanceContent.cpp
    src/Game/JarMods.cpp
    src/Game/JavaFinder.cpp
    src/Game/Loaders.cpp
    src/Game/PackCreator.cpp
    src/Game/PackInstance.cpp
    src/Game/ServerSetup.cpp
    src/Model/Modpack.cpp
    src/Network/AtLauncher.cpp
    src/Network/CurseForge.cpp
    src/Network/Ftb.cpp
    src/Network/HttpClient.cpp
    src/Network/Modrinth.cpp
    src/Network/ModSearch.cpp
    src/Network/PackSources.cpp
    src/Network/Technic.cpp
    src/Storage/InstallPlan.cpp
    src/Storage/Installer.cpp
    src/Util/Hash.cpp
    src/Util/Paths.cpp
    src/Util/Process.cpp
    src/Util/Sha1.cpp
    src/Util/SharedFiles.cpp
    src/Util/Text.cpp
  )
  target_include_directories(live_checks PRIVATE ${launcher_includes})
  target_link_libraries(live_checks PRIVATE ${launcher_libraries})
  if (WIN32)
    target_compile_definitions(live_checks PRIVATE _CRT_SECURE_NO_WARNINGS GLFW_INCLUDE_NONE)
  endif()
endif()
