#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

struct ReflectedField {
    std::string type;
    std::string name;
};

struct ReflectedComponent {
    std::string name;
    std::string qualifiedName;
    std::string headerName;
    std::vector<ReflectedField> fields;
};

struct ReflectedSystem {
    std::string name;
    std::string headerName;
};

// Trim whitespace from string
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string getFieldTypeEnum(const std::string& type) {
    if (type == "float" || type == "double") return "Engine::FieldType::Float";
    if (type == "int" || type == "int32_t" || type == "uint32_t" || type == "size_t") return "Engine::FieldType::Int";

    if (type == "bool") return "Engine::FieldType::Bool";
    if (type == "glm::vec2" || type == "vec2") return "Engine::FieldType::Vec2";
    if (type == "glm::vec3" || type == "vec3" || type == "RotationField") return "Engine::FieldType::Vec3";
    if (type == "glm::vec4" || type == "vec4") return "Engine::FieldType::Vec4";
    if (type == "std::string" || type == "string") return "Engine::FieldType::String";
    if (type == "RigidBodyType") return "Engine::FieldType::RigidBodyType";
    if (type == "Entity") return "Engine::FieldType::Entity";
    if (type == "CinemachineMode" || type.find("Mode") != std::string::npos || type.find("Enum") != std::string::npos) return "Engine::FieldType::Enum";
    return "";
}


// Calculate the correct relative include path for both user scripts and engine public headers
std::string getIncludePath(const fs::path& filePath, const fs::path& inputDir) {
    std::string pathStr = filePath.generic_string();
    size_t ecsPos = pathStr.find("ecs/components");
    if (ecsPos != std::string::npos) {
        return pathStr.substr(ecsPos);
    }
    try {
        fs::path rel = fs::relative(filePath, inputDir);
        return rel.generic_string();
    } catch (...) {
        return filePath.filename().string();
    }
}

void parseHeader(const fs::path& filePath, const fs::path& inputDir, std::vector<ReflectedComponent>& outComponents, std::vector<ReflectedSystem>& outSystems) {
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    bool inComponent = false;
    ReflectedComponent currentComp;
    bool nextLineIsReflected = false;
    bool nextLineIsReflectedField = false;
    bool inEngineNamespace = false;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.find("namespace Engine") != std::string::npos) {
            inEngineNamespace = true;
        }

        // Check if this line is a class reflection marker
        if ((trimmed.find("@reflect") != std::string::npos || trimmed.find("ReflectClass") != std::string::npos) && !inComponent) {
            nextLineIsReflected = true;
            continue;
        }

        if (nextLineIsReflected) {
            nextLineIsReflected = false;

            // Check if it's a system class (inherits from System)
            if (trimmed.find("class") != std::string::npos && trimmed.find(": public System") != std::string::npos) {
                std::regex sysRegex("class\\s+(?:ENGINE_API\\s+)?(\\w+)");
                std::smatch match;
                if (std::regex_search(trimmed, match, sysRegex)) {
                    ReflectedSystem sys;
                    sys.name = match[1].str();
                    sys.headerName = getIncludePath(filePath, inputDir);
                    outSystems.push_back(sys);
                }
                continue;
            }

            // Check if it's a component struct/class
            if (trimmed.find("struct") != std::string::npos || trimmed.find("class") != std::string::npos) {
                std::regex compRegex("(?:struct|class)\\s+(?:ENGINE_API\\s+)?(\\w+)");
                std::smatch match;
                if (std::regex_search(trimmed, match, compRegex)) {
                    inComponent = true;
                    currentComp = ReflectedComponent();
                    currentComp.name = match[1].str();
                    if (inEngineNamespace) {
                        currentComp.qualifiedName = "Engine::" + currentComp.name;
                    } else {
                        currentComp.qualifiedName = currentComp.name;
                    }

                    currentComp.headerName = getIncludePath(filePath, inputDir);
                }
                continue;
            }
        }

        if (inComponent) {
            // Check for end of struct/class
            if (trimmed.rfind("};", 0) == 0 || trimmed == "};") {
                outComponents.push_back(currentComp);
                inComponent = false;
                nextLineIsReflectedField = false;
                continue;
            }

            // Check if this line is a field reflection marker on its own line
            if (trimmed.find("ReflectField") != std::string::npos && trimmed.find(';') == std::string::npos && trimmed.find('=') == std::string::npos) {
                nextLineIsReflectedField = true;
                continue;
            }

            // Check if the field is annotated
            bool isReflectedField = nextLineIsReflectedField || (trimmed.find("@reflect") != std::string::npos) || (trimmed.find("ReflectField") != std::string::npos);

            if (isReflectedField) {
                nextLineIsReflectedField = false;
                // Strip comments
                size_t commentPos = trimmed.find("//");
                if (commentPos != std::string::npos) {
                    trimmed = trimmed.substr(0, commentPos);
                }
                trimmed = trim(trimmed);


                // Strip trailing semicolon
                if (!trimmed.empty() && trimmed.back() == ';') {
                    trimmed.pop_back();
                }
                trimmed = trim(trimmed);

                // Split at '=' or '{'
                size_t eqPos = trimmed.find('=');
                if (eqPos == std::string::npos) {
                    eqPos = trimmed.find('{');
                }
                if (eqPos != std::string::npos) {
                    trimmed = trimmed.substr(0, eqPos);
                }
                trimmed = trim(trimmed);

                // Split type and name
                size_t lastSpace = trimmed.find_last_of(" \t");
                if (lastSpace != std::string::npos) {
                    ReflectedField field;
                    field.type = trim(trimmed.substr(0, lastSpace));
                    field.name = trim(trimmed.substr(lastSpace + 1));
                    currentComp.fields.push_back(field);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_dir_1> [<input_dir_2> ...] <output_file>" << std::endl;
        return 1;
    }

    std::string outputFile = argv[argc - 1];
    std::vector<std::string> inputDirs;
    for (int i = 1; i < argc - 1; ++i) {
        inputDirs.push_back(argv[i]);
    }

    std::vector<ReflectedComponent> components;
    std::vector<ReflectedSystem> systems;

    for (const auto& inputDir : inputDirs) {
        try {
            if (fs::exists(inputDir)) {
                for (const auto& entry : fs::recursive_directory_iterator(inputDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".hpp" || ext == ".h") {
                            parseHeader(entry.path(), inputDir, components, systems);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning directory " << inputDir << ": " << e.what() << std::endl;
        }
    }

    // Open output file
    std::ofstream out(outputFile);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << outputFile << std::endl;
        return 1;
    }

    // Generate output header
    out << "// =========================================================================\n";
    out << "//  GENERATED REFLECTION FILE — DO NOT EDIT MANUALLY\n";
    out << "//  This file is automatically generated by reflection_generator\n";
    out << "// =========================================================================\n\n";

    out << "#include \"ScriptAPI.hpp\"\n";
    
    // Generate includes for each parsed script header
    std::vector<std::string> includedHeaders;
    for (const auto& comp : components) {
        if (std::find(includedHeaders.begin(), includedHeaders.end(), comp.headerName) == includedHeaders.end()) {
            out << "#include \"" << comp.headerName << "\"\n";
            includedHeaders.push_back(comp.headerName);
        }
    }
    for (const auto& sys : systems) {
        if (std::find(includedHeaders.begin(), includedHeaders.end(), sys.headerName) == includedHeaders.end()) {
            out << "#include \"" << sys.headerName << "\"\n";
            includedHeaders.push_back(sys.headerName);
        }
    }
    out << "\n";

    // Generate DLL Entry Points
    out << "#ifdef _WIN32\n";
    out << "    #define PLUGIN_API extern \"C\" __declspec(dllexport)\n";
    out << "#else\n";
    out << "    #define PLUGIN_API extern \"C\"\n";
    out << "#endif\n\n";

    std::string funcName = systems.empty() ? "registerEngineReflection" : "registerScriptReflection";

    // Generate component reflection registration function
    out << "PLUGIN_API void " << funcName << "() {\n";
    for (const auto& comp : components) {
        std::string compName = comp.name;
        if (compName.size() > 9 && compName.rfind("Component") == compName.size() - 9) {
            compName = compName.substr(0, compName.size() - 9);
        }
        std::string category = "General";
        std::string displayName = compName;

        if (compName == "SpriteRenderer") {
            category = "Rendering & Lights";
            displayName = "Sprite Renderer";
        } else if (compName == "Mesh" || compName == "Material" || compName == "LightComponent") {
            category = "Rendering & Lights";
            if (compName == "LightComponent") displayName = "Light";
        } else if (compName == "Animator" || compName == "AnimationController") {
            category = "Animation";
            if (compName == "AnimationController") displayName = "Animation Controller";
        }

        out << "    {\n";
        out << "        Engine::ComponentReflection refl;\n";
        out << "        refl.name = \"" << compName << "\";\n";
        out << "        refl.category = \"" << category << "\";\n";
        out << "        refl.displayName = \"" << displayName << "\";\n";
        out << "        refl.fields = {\n";
        for (size_t i = 0; i < comp.fields.size(); ++i) {
            const auto& field = comp.fields[i];
            std::string enumStr = getFieldTypeEnum(field.type);
            if (!enumStr.empty()) {
                if (enumStr == "Engine::FieldType::Enum") {
                    out << "            { \"" << field.name << "\", " << enumStr << ", offsetof(" << comp.qualifiedName << ", " << field.name << "), { \"Third Person Follow\", \"First Person\", \"Fixed Look At\", \"2D Follow\" } }";
                } else {
                    out << "            { \"" << field.name << "\", " << enumStr << ", offsetof(" << comp.qualifiedName << ", " << field.name << ") }";
                }
                if (i < comp.fields.size() - 1) out << ",";
                out << "\n";
            }
        }
        out << "        };\n";
        out << "        refl.add = [](Registry& reg, Entity e) { reg.emplace<" << comp.qualifiedName << ">(e, " << comp.qualifiedName << "{}); };\n";
        out << "        refl.has = [](Registry& reg, Entity e) { return reg.has<" << comp.qualifiedName << ">(e); };\n";
        out << "        refl.remove = [](Registry& reg, Entity e) { reg.remove<" << comp.qualifiedName << ">(e); };\n";
        out << "        refl.get = [](Registry& reg, Entity e) { return static_cast<void*>(reg.get<" << comp.qualifiedName << ">(e)); };\n";
        out << "        Engine::ComponentReflectionRegistry::getInstance().registerComponent(refl);\n";
        out << "    }\n";
    }
    out << "}\n\n";

    out << "static std::vector<std::shared_ptr<System>> s_pluginSystems;\n\n";

    out << "PLUGIN_API void initPlugin(PluginContext* context) {\n";
    out << "    if (context && context->imguiContext) ImGui::SetCurrentContext(context->imguiContext);\n";
    out << "    " << funcName << "();\n\n";
    out << "    s_pluginSystems.clear();\n\n";

    // Register reflected systems
    for (const auto& sys : systems) {
        out << "    // Register " << sys.name << "\n";
        out << "    {\n";
        out << "        auto sysPtr = std::make_shared<" << sys.name << ">(*context->registry, *context->renderer, *context->editorMode);\n";
        out << "        s_pluginSystems.push_back(sysPtr);\n";
        out << "        context->systemManager->addSystem(sysPtr);\n";
        out << "    }\n\n";
    }

    out << "}\n\n";

    out << "PLUGIN_API void shutdownPlugin(PluginContext* context) {\n";
    out << "    if (context && context->systemManager) {\n";
    out << "        for (auto& sysPtr : s_pluginSystems) {\n";
    out << "            context->systemManager->removeSystem(sysPtr);\n";
    out << "        }\n";
    out << "    }\n";
    out << "    s_pluginSystems.clear();\n";
    out << "}\n";

    std::cout << "[Reflection Generator] Successfully generated: " << outputFile << " (" 
              << components.size() << " components, " << systems.size() << " systems)" << std::endl;

    return 0;
}
