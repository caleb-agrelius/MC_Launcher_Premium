#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Finds Java installations on this PC, so Play and server setup can use one that matches
// the Minecraft version (like Prism Launcher) instead of always downloading Mojang's.

struct JavaInstall {
	std::filesystem::path home;   // the folder holding bin/
	std::filesystem::path java;   // bin/java.exe (console)
	std::filesystem::path javaw;  // bin/javaw.exe (no console window), or java when there is none
	std::string version;          // e.g. "17.0.8", "1.8.0_382"
	int major = 0;                // e.g. 17, 8
	std::string vendor;           // e.g. "Eclipse Adoptium"; may be empty
	bool mojang = false;          // a Mojang runtime (ours or the official launcher's)
};

// "1.8.0_382" -> 8, "17.0.8" -> 17, "21" -> 21, "" or junk -> 0.
int JavaMajorVersion(const std::string& version);

// Reads one Java home: its release file (JAVA_VERSION, IMPLEMENTOR, OS_ARCH), else the
// version in its folder name (jdk-17.0.2, jre1.8.0_381, zulu21...). False when it isn't a
// usable 64-bit Java (no bin/java.exe, an x86 build, or no version).
bool ReadJavaInstall(const std::filesystem::path& home, JavaInstall& out);

// Java homes inside each folder: the folder itself, its children, and their children (so
// "Program Files/Java" finds "jdk-17/" and a Minecraft runtime folder finds
// "java-runtime-gamma/windows-x64/java-runtime-gamma/").
std::vector<JavaInstall> ScanJavaFolders(const std::vector<std::filesystem::path>& folders);

// Every Java found on this PC: JAVA_HOME, PATH, the registry (Oracle, Adoptium, Microsoft,
// Zulu, BellSoft, Corretto), common install folders, IntelliJ's ~/.jdks, the Minecraft
// launcher's and Prism's runtimes, and Mojang runtimes this launcher downloaded under
// gameRoot/runtime/. Each home appears once, newest version first.
std::vector<JavaInstall> FindJavaInstalls(const std::filesystem::path& gameRoot);

// The best install for a Minecraft version that needs Java `requiredMajor`: the newest one
// with exactly that major version (Minecraft and loaders often break on newer majors),
// preferring a Mojang runtime when the versions tie. nullptr when none matches.
const JavaInstall* PickJava(const std::vector<JavaInstall>& installs, int requiredMajor);

// "Java 17.0.8 (Eclipse Adoptium)", for menus and progress text.
std::string JavaLabel(const JavaInstall& java);
