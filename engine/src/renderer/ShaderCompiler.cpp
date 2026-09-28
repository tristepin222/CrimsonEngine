#include "renderer/ShaderCompiler.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <array>
#include <chrono>

#if defined(_WIN32)
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace Engine {

    std::string ShaderCompiler::s_glslcPath = "";
    bool ShaderCompiler::s_isInitialized = false;
    bool ShaderCompiler::s_isAvailable = false;
    std::string ShaderCompiler::s_cacheDirectory = ".cache/shaders";
    std::string ShaderCompiler::s_projectDirectory = "";
    std::string ShaderCompiler::s_engineDirectory = "";
    std::vector<std::string> ShaderCompiler::s_includeDirectories{};
    std::unordered_map<std::string, uint64_t> ShaderCompiler::s_fileTimestamps{};

    void ShaderCompiler::initialize(const std::string& projectDir, const std::string& engineDir) {
        if (!projectDir.empty()) {
            s_projectDirectory = projectDir;
        }
        if (!engineDir.empty()) {
            s_engineDirectory = engineDir;
        }

        // Setup cache directory
        if (!s_projectDirectory.empty()) {
            s_cacheDirectory = (std::filesystem::path(s_projectDirectory) / ".cache" / "shaders").string();
        } else {
            s_cacheDirectory = ".cache/shaders";
        }

        std::error_code ec;
        std::filesystem::create_directories(s_cacheDirectory, ec);

        s_isAvailable = findGlslc();
        s_isInitialized = true;

        if (s_isAvailable) {
            std::cout << "[ShaderCompiler] Detected glslc compiler at: " << s_glslcPath << std::endl;
        } else {
            std::cerr << "[ShaderCompiler] WARNING: glslc not found. Runtime shader compilation will be unavailable." << std::endl;
        }
    }

    const std::string& ShaderCompiler::getGlslcPath() {
        if (!s_isInitialized) initialize();
        return s_glslcPath;
    }

    bool ShaderCompiler::isAvailable() {
        if (!s_isInitialized) initialize();
        return s_isAvailable;
    }

    bool ShaderCompiler::findGlslc() {
        // 1. Check VULKAN_SDK environment variable
        const char* vulkanSdk = std::getenv("VULKAN_SDK");
        if (vulkanSdk && *vulkanSdk) {
            std::filesystem::path binGlslc = std::filesystem::path(vulkanSdk) / "bin" / "glslc.exe";
            if (std::filesystem::exists(binGlslc)) {
                s_glslcPath = binGlslc.string();
                return true;
            }
            std::filesystem::path unixGlslc = std::filesystem::path(vulkanSdk) / "bin" / "glslc";
            if (std::filesystem::exists(unixGlslc)) {
                s_glslcPath = unixGlslc.string();
                return true;
            }
        }

        // 1b. Check standard Vulkan SDK installation directory on Windows
        if (std::filesystem::exists("C:\\VulkanSDK")) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator("C:\\VulkanSDK", ec)) {
                if (entry.is_directory()) {
                    std::filesystem::path glslcPath = entry.path() / "bin" / "glslc.exe";
                    if (std::filesystem::exists(glslcPath)) {
                        s_glslcPath = glslcPath.string();
                        return true;
                    }
                }
            }
        }

        // 2. Check system PATH via "where glslc" (Windows) or "which glslc" (Linux/macOS)
#if defined(_WIN32)
        FILE* pipe = POPEN("where glslc 2>NUL", "r");
#else
        FILE* pipe = POPEN("which glslc 2>/dev/null", "r");
#endif
        if (pipe) {
            std::array<char, 512> buffer;
            std::string result;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                result += buffer.data();
            }
            PCLOSE(pipe);

            // Strip trailing newlines / carriage returns
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
                result.pop_back();
            }

            if (!result.empty() && std::filesystem::exists(result)) {
                s_glslcPath = result;
                return true;
            }
        }

        // 3. Fallback to bare command name if it can be invoked directly
        s_glslcPath = "glslc";
        return false;
    }

    uint64_t ShaderCompiler::getFileLastWriteTime(const std::string& path) {
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(path, ec);
        if (ec) return 0;
        return static_cast<uint64_t>(ftime.time_since_epoch().count());
    }

    ShaderStage ShaderCompiler::inferStageFromExtension(const std::string& filePath) {
        std::filesystem::path p(filePath);
        std::string ext = p.extension().string();
        if (ext == ".vert") return ShaderStage::Vertex;
        if (ext == ".frag") return ShaderStage::Fragment;
        if (ext == ".comp") return ShaderStage::Compute;
        return ShaderStage::Unknown;
    }

    bool ShaderCompiler::isSourceModified(const std::string& sourcePath) {
        if (!std::filesystem::exists(sourcePath)) return false;
        uint64_t current = getFileLastWriteTime(sourcePath);
        auto it = s_fileTimestamps.find(sourcePath);
        if (it == s_fileTimestamps.end()) return true;
        return current > it->second;
    }

    ShaderCompileResult ShaderCompiler::compileFile(
        const std::string& sourcePath,
        const std::string& outSpvPath,
        bool forceRecompile
    ) {
        if (!s_isInitialized) initialize();

        ShaderCompileResult res{};
        if (!std::filesystem::exists(sourcePath)) {
            res.errorMessage = "Shader source file not found: " + sourcePath;
            res.success = false;
            return res;
        }

        std::filesystem::path srcP(sourcePath);
        std::string filename = srcP.filename().string();

        // Determine destination .spv path
        std::string targetSpv = outSpvPath;
        if (targetSpv.empty()) {
            std::filesystem::path cacheP(s_cacheDirectory);
            targetSpv = (cacheP / (filename + ".spv")).string();
        }

        res.spvPath = targetSpv;
        uint64_t srcTime = getFileLastWriteTime(sourcePath);
        res.sourceTimestamp = srcTime;

        // Check if cached .spv is already up to date
        if (!forceRecompile && std::filesystem::exists(targetSpv)) {
            uint64_t spvTime = getFileLastWriteTime(targetSpv);
            if (spvTime >= srcTime && spvTime != 0) {
                res.success = true;
                res.wasCached = true;
                s_fileTimestamps[sourcePath] = srcTime;
                return res;
            }
        }

        if (!s_isAvailable) {
            res.errorMessage = "glslc compiler is not available on this system.";
            res.success = false;
            return res;
        }

        // Ensure parent directory exists
        std::filesystem::path parentDir = std::filesystem::path(targetSpv).parent_path();
        if (!parentDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parentDir, ec);
        }

        // Build command line
        std::string cmd = "\"" + s_glslcPath + "\" \"" + sourcePath + "\" -o \"" + targetSpv + "\"";
        for (const auto& inc : s_includeDirectories) {
            cmd += " -I \"" + inc + "\"";
        }
        cmd += " 2>&1";

        FILE* pipe = POPEN(cmd.c_str(), "r");
        if (!pipe) {
            res.errorMessage = "Failed to spawn glslc process.";
            res.success = false;
            return res;
        }

        std::array<char, 256> buffer;
        std::string outputText;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            outputText += buffer.data();
        }
        int exitCode = PCLOSE(pipe);

        if (exitCode == 0 && std::filesystem::exists(targetSpv)) {
            res.success = true;
            res.wasCached = false;
            res.warningMessage = outputText;
            s_fileTimestamps[sourcePath] = srcTime;
            std::cout << "[ShaderCompiler] Successfully compiled: " << filename << " -> " << targetSpv << std::endl;
        } else {
            res.success = false;
            res.errorMessage = outputText;
            std::cerr << "[ShaderCompiler] Failed to compile " << sourcePath << " (exit code " << exitCode << "):\n"
                      << outputText << std::endl;
        }

        return res;
    }

    std::string ShaderCompiler::resolveOrCompile(const std::string& path, bool forceRecompile) {
        if (path.empty()) return "";

        // If it's already a .spv and exists directly, return it
        if (!forceRecompile && path.size() >= 4 && path.substr(path.size() - 4) == ".spv") {
            if (std::filesystem::exists(path)) return path;
        }

        // Check if it's a source shader (.vert, .frag, .comp, etc.)
        ShaderStage stage = inferStageFromExtension(path);
        if (stage != ShaderStage::Unknown) {
            // Find file directly or search common directories
            std::string resolvedSource = path;
            if (!std::filesystem::exists(resolvedSource)) {
                std::filesystem::path p(path);
                std::string fname = p.filename().string();

                // 1. Try stripping leading "sandbox_game/" if CWD is already inside sandbox_game
                std::string stripped = path;
                if (stripped.rfind("sandbox_game/", 0) == 0) {
                    stripped = stripped.substr(13);
                    if (std::filesystem::exists(stripped)) resolvedSource = stripped;
                }

                // 2. Try prepending "sandbox_game/" if running from repo root
                if (!std::filesystem::exists(resolvedSource)) {
                    std::filesystem::path sgPath = std::filesystem::path("sandbox_game") / path;
                    if (std::filesystem::exists(sgPath)) resolvedSource = sgPath.string();
                }

                // 3. Try assets/shaders/ directly
                if (!std::filesystem::exists(resolvedSource)) {
                    std::filesystem::path directShaders = std::filesystem::path("assets") / "shaders" / fname;
                    if (std::filesystem::exists(directShaders)) resolvedSource = directShaders.string();
                }

                // 4. Try project directory
                if (!std::filesystem::exists(resolvedSource) && !s_projectDirectory.empty()) {
                    std::filesystem::path pPath = std::filesystem::path(s_projectDirectory) / path;
                    if (std::filesystem::exists(pPath)) {
                        resolvedSource = pPath.string();
                    } else {
                        std::filesystem::path pShaders = std::filesystem::path(s_projectDirectory) / "assets" / "shaders" / fname;
                        if (std::filesystem::exists(pShaders)) {
                            resolvedSource = pShaders.string();
                        }
                    }
                }

                // 5. Try engine builtin_shaders
                if (!std::filesystem::exists(resolvedSource)) {
                    std::filesystem::path engineBuiltin = std::filesystem::path("engine/builtin_shaders") / fname;
                    if (std::filesystem::exists(engineBuiltin)) resolvedSource = engineBuiltin.string();
                }

                // 6. Try parent engine builtin_shaders (when running from sandbox_game)
                if (!std::filesystem::exists(resolvedSource)) {
                    std::filesystem::path parentBuiltin = std::filesystem::path("../engine/builtin_shaders") / fname;
                    if (std::filesystem::exists(parentBuiltin)) resolvedSource = parentBuiltin.string();
                }
            }

            if (std::filesystem::exists(resolvedSource)) {
                ShaderCompileResult res = compileFile(resolvedSource, "", forceRecompile);
                if (res.success) {
                    return res.spvPath;
                }
                // If compilation failed (e.g. glslc missing), check if a sibling .spv exists
                std::string siblingSpv = resolvedSource + ".spv";
                if (std::filesystem::exists(siblingSpv)) {
                    return siblingSpv;
                }
            }
        }

        // Check if an adjacent .spv exists directly
        std::string adjSpv = path + ".spv";
        if (std::filesystem::exists(adjSpv)) return adjSpv;

        // Return path as-is (will be handled by resolveShaderPath fallback)
        return path;
    }

    int ShaderCompiler::compileAllInDirectory(const std::string& directory) {
        if (!std::filesystem::exists(directory)) return 0;

        int compiledCount = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string path = entry.path().string();
            if (inferStageFromExtension(path) != ShaderStage::Unknown) {
                ShaderCompileResult res = compileFile(path);
                if (res.success) compiledCount++;
            }
        }
        return compiledCount;
    }

    void ShaderCompiler::addIncludeDirectory(const std::string& dir) {
        if (!dir.empty() && std::filesystem::exists(dir)) {
            s_includeDirectories.push_back(dir);
        }
    }

    void ShaderCompiler::setCacheDirectory(const std::string& dir) {
        s_cacheDirectory = dir;
        std::error_code ec;
        std::filesystem::create_directories(s_cacheDirectory, ec);
    }

    const std::string& ShaderCompiler::getCacheDirectory() {
        return s_cacheDirectory;
    }

} // namespace Engine
