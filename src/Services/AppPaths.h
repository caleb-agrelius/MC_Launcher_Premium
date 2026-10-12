#pragma once

// File and folder names under the data folder (the one holding resources/index.json, which the
// app makes its working directory). The paths are relative, so they follow the app wherever it runs.
namespace AppPaths {

inline constexpr const char* kIndexFile = "resources/index.json";
inline constexpr const char* kSettingsFile = "settings.json";
inline constexpr const char* kLibraryFile = "library.json";
inline constexpr const char* kDownloadsFile = "downloads.json";
inline constexpr const char* kAccountFile = "account.dat";
inline constexpr const char* kLogFile = "app.log";
inline constexpr const char* kGameRoot = "minecraft";      // shared Minecraft versions, libraries, assets, Java
inline constexpr const char* kInstancesRoot = "instances"; // one game directory per pack
inline constexpr const char* kServersRoot = "servers";     // one dedicated server per pack
inline constexpr const char* kInstallsRoot = "installs";   // each pack's extracted files
inline constexpr const char* kDownloadsRoot = "downloads"; // downloaded pack archives
inline constexpr const char* kSharedFilesDir = "minecraft/shared-files";

}
