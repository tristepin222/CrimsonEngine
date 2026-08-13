#include "core/PluginManager.hpp"
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

PluginManager::PluginManager(Registry& reg, SystemManager& sys, VulkanRenderer& rend, EditorModeState& mode)
    : registry(reg), systemManager(sys), renderer(rend), editorMode(mode) {}

PluginManager::~PluginManager() {
    unloadPlugins();
}

void PluginManager::scanDirectory(const std::string& dir, bool isScript) {
    if (!std::filesystem::exists(dir)) return;

    std::cout << "[PluginManager] Scanning: " << dir << std::endl;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".dll") continue;

        std::string pathStr = entry.path().string();
        std::cout << "[PluginManager] Found library: " << pathStr << std::endl;

#ifdef _WIN32
        HMODULE handle = LoadLibraryA(pathStr.c_str());
        if (!handle) {
            DWORD err = GetLastError();
            std::cerr << "[PluginManager] Failed to load DLL: " << pathStr << " (Error " << err << ")" << std::endl;
            continue;
        }
        auto initFunc     = (PluginInitFunc)GetProcAddress(handle, "initPlugin");
        auto shutdownFunc = (PluginShutdownFunc)GetProcAddress(handle, "shutdownPlugin");
        if (!initFunc) {
            std::cerr << "[PluginManager] 'initPlugin' not found in: " << pathStr << std::endl;
            FreeLibrary(handle);
            continue;
        }
#else
        HMODULE handle = dlopen(pathStr.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            std::cerr << "[PluginManager] Failed to open: " << pathStr << " (" << dlerror() << ")" << std::endl;
            continue;
        }
        auto initFunc     = (PluginInitFunc)dlsym(handle, "initPlugin");
        auto shutdownFunc = (PluginShutdownFunc)dlsym(handle, "shutdownPlugin");
        if (!initFunc) {
            std::cerr << "[PluginManager] 'initPlugin' not found in: " << pathStr << std::endl;
            dlclose(handle);
            continue;
        }
#endif

        PluginContext context;
        context.registry      = &registry;
        context.systemManager = &systemManager;
        context.renderer      = &renderer;
        context.editorMode    = &editorMode;
        context.imguiContext  = ImGui::GetCurrentContext();

        std::cout << "[PluginManager] Initializing: " << pathStr << std::endl;
        try {
            initFunc(&context);
        } catch (const std::exception& ex) {
            std::cerr << "[PluginManager] Exception in initFunc for " << pathStr << ": " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[PluginManager] Unknown exception in initFunc for " << pathStr << std::endl;
        }

        LoadedPlugin plugin;
        plugin.path         = pathStr;
        plugin.handle       = handle;
        plugin.shutdownFunc = shutdownFunc;
        plugin.isScript     = isScript;
        loadedPlugins.push_back(plugin);

        std::cout << "[PluginManager] Loaded: " << pathStr << std::endl;
    }
}

void PluginManager::setExeDirectory(const std::string& dir) {
    exeDir = dir;
}

void PluginManager::loadPlugins() {
    // Engine plugins live next to the executable, not in the project directory.
    // exeDir is set to the directory of editor.exe / game_runtime.exe.
    std::string pluginsDir = exeDir.empty() ? "./plugins" : (exeDir + "/plugins");
    if (!std::filesystem::exists(pluginsDir)) {
        std::cout << "[PluginManager] No plugins/ folder found at: " << pluginsDir << std::endl;
        return;
    }
    scanDirectory(pluginsDir, false);
}

void PluginManager::loadScripts(const std::string& projectPath) {
    std::string binDir = projectPath + "/bin";
    std::string scriptsDir = projectPath + "/scripts";

    if (std::filesystem::exists(binDir)) {
        std::cout << "[PluginManager] Loading user scripts from bin: " << binDir << std::endl;
        scanDirectory(binDir, true);
    } else if (std::filesystem::exists(scriptsDir)) {
        std::cout << "[PluginManager] Loading user scripts from scripts folder: " << scriptsDir << std::endl;
        scanDirectory(scriptsDir, true);
    } else {
        std::cout << "[PluginManager] No scripts/ or bin/ folder found at: " << projectPath << std::endl;
    }
}

void PluginManager::unloadPlugins() {
    if (loadedPlugins.empty()) return;

    std::cout << "[PluginManager] Unloading all active plugins..." << std::endl;

    for (auto& plugin : loadedPlugins) {
        if (plugin.shutdownFunc) {
            PluginContext context;
            context.registry      = &registry;
            context.systemManager = &systemManager;
            context.renderer      = &renderer;
            context.editorMode    = &editorMode;
            context.imguiContext  = ImGui::GetCurrentContext();
            plugin.shutdownFunc(&context);
        }
#ifdef _WIN32
        FreeLibrary(plugin.handle);
#else
        dlclose(plugin.handle);
#endif
    }
    loadedPlugins.clear();
    std::cout << "[PluginManager] All plugins unloaded." << std::endl;
}

void PluginManager::unloadScripts() {
    if (loadedPlugins.empty()) return;

    std::cout << "[PluginManager] Unloading active user script plugins..." << std::endl;

    for (auto it = loadedPlugins.begin(); it != loadedPlugins.end(); ) {
        if (it->isScript) {
            if (it->shutdownFunc) {
                PluginContext context;
                context.registry      = &registry;
                context.systemManager = &systemManager;
                context.renderer      = &renderer;
                context.editorMode    = &editorMode;
                context.imguiContext  = ImGui::GetCurrentContext();
                it->shutdownFunc(&context);
            }
#ifdef _WIN32
            FreeLibrary(it->handle);
#else
            dlclose(it->handle);
#endif
            it = loadedPlugins.erase(it);
        } else {
            ++it;
        }
    }
    std::cout << "[PluginManager] Script plugins unloaded." << std::endl;
}

