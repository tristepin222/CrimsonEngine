#pragma once

#include "core/EngineAPI.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine {

    /**
     * @enum ShaderStage
     * @brief Identifies the programmable shader stage.
     */
    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute,
        Unknown
    };

    /**
     * @struct ShaderCompileResult
     * @brief Stores the outcome, output paths, and compiler diagnostics of a shader compilation.
     */
    struct ENGINE_API ShaderCompileResult {
        bool success = false;
        std::string spvPath;
        std::string errorMessage;
        std::string warningMessage;
        bool wasCached = false;
        uint64_t sourceTimestamp = 0;
    };

    /**
     * @class ShaderCompiler
     * @brief Runtime and project shader compiler subsystem.
     * Invokes glslc (from Vulkan SDK or PATH) to compile .vert, .frag, and .comp files into SPIR-V.
     * Features intelligent timestamp caching and hot-reloading diagnostics.
     */
    class ENGINE_API ShaderCompiler {
    public:
        /**
         * @brief Initializes the compiler subsystem, detecting glslc from VULKAN_SDK or PATH.
         * @param projectDir Root directory of the active project (e.g. sandbox_game).
         * @param engineDir Root directory of the engine or executable.
         */
        static void initialize(const std::string& projectDir = "", const std::string& engineDir = "");

        /**
         * @brief Returns the absolute path to the detected glslc executable.
         */
        static const std::string& getGlslcPath();

        /**
         * @brief Checks if glslc is installed and callable.
         */
        static bool isAvailable();

        /**
         * @brief Infers the ShaderStage based on file extension (.vert, .frag, .comp).
         */
        static ShaderStage inferStageFromExtension(const std::string& filePath);

        /**
         * @brief Compiles a GLSL shader source file into SPIR-V (.spv).
         * Uses timestamp caching: if the output .spv is newer than the source, skips recompilation.
         * @param sourcePath Path to the source file (.vert, .frag, .comp).
         * @param outSpvPath Optional destination path. If empty, writes to the project cache directory.
         * @param forceRecompile If true, recompiles regardless of cached file timestamp.
         * @return ShaderCompileResult containing success status, output path, and compiler messages.
         */
        static ShaderCompileResult compileFile(
            const std::string& sourcePath,
            const std::string& outSpvPath = "",
            bool forceRecompile = false
        );

        /**
         * @brief Resolves a shader path. If given a .spv file that exists, returns it directly.
         * If given a source file (.vert, .frag, .comp), compiles it and returns the path to the resulting .spv.
         * @param path File path or virtual shader name.
         * @param forceRecompile If true, compiles file even if cached .spv exists.
         * @return Path to usable .spv binary, or empty string on failure.
         */
        static std::string resolveOrCompile(const std::string& path, bool forceRecompile = false);

        /**
         * @brief Scans a directory for shader files (.vert, .frag, .comp) and compiles any that are out of date.
         * @param directory Directory path to scan (e.g. "assets/shaders").
         * @return Number of shaders successfully compiled or verified up to date.
         */
        static int compileAllInDirectory(const std::string& directory);

        /**
         * @brief Adds an include directory for #include directives in GLSL shaders.
         */
        static void addIncludeDirectory(const std::string& dir);

        /**
         * @brief Sets the global cache directory for compiled SPIR-V bytecode.
         */
        static void setCacheDirectory(const std::string& dir);

        /**
         * @brief Gets the global cache directory.
         */
        static const std::string& getCacheDirectory();

        /**
         * @brief Checks if a source shader file has been modified on disk since its last compilation.
         */
        static bool isSourceModified(const std::string& sourcePath);

    private:
        static std::string s_glslcPath;
        static bool s_isInitialized;
        static bool s_isAvailable;
        static std::string s_cacheDirectory;
        static std::string s_projectDirectory;
        static std::string s_engineDirectory;
        static std::vector<std::string> s_includeDirectories;
        static std::unordered_map<std::string, uint64_t> s_fileTimestamps;

        static bool findGlslc();
        static uint64_t getFileLastWriteTime(const std::string& path);
    };

} // namespace Engine
