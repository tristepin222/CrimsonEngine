#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

struct ReflectedField {
    std::string type;
    std::string name;
    int arraySize = 1;
};

struct ReflectedStruct {
    std::string name;
    std::string qualifiedName;
    std::string headerName;
    std::vector<ReflectedField> fields;
};

struct ReflectedEnum {
    std::string name;
    std::string qualifiedName;
    std::string headerName;
    std::vector<std::string> options;
};

struct ReflectedComponent {
    std::string name;
    std::string qualifiedName;
    std::string headerName;
    std::string category = "General";
    std::string displayName = "";
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
    if (type == "CinemachineMode" || type == "LightType" || type == "Engine::LightType" || type == "CloudPreset" || type == "WeatherPreset" || type == "WeatherType" || type.find("Mode") != std::string::npos || type.find("Enum") != std::string::npos || type.find("Preset") != std::string::npos) return "Engine::FieldType::Enum";
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

void parseHeader(const fs::path& filePath, const fs::path& inputDir,
                 std::vector<ReflectedComponent>& outComponents,
                 std::vector<ReflectedStruct>& outStructs,
                 std::vector<ReflectedSystem>& outSystems,
                 std::vector<ReflectedEnum>& outEnums) {
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    bool inComponent = false;
    bool inStruct = false;
    bool inEnum = false;
    int braceDepth = 0;
    ReflectedComponent currentComp;
    ReflectedStruct currentStruct;
    ReflectedEnum currentEnum;
    bool nextLineIsReflected = false;
    bool nextLineIsReflectedStruct = false;
    bool nextLineIsReflectedEnum = false;
    bool nextLineIsReflectedField = false;
    bool inEngineNamespace = false;
    std::string pendingCategory = "";
    std::string pendingDisplayName = "";

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.find("namespace Engine") != std::string::npos) {
            inEngineNamespace = true;
        }

        // Check if this line is an enum reflection marker: // [ReflectEnum]
        if (!inComponent && !inStruct && !inEnum && (trimmed.find("ReflectEnum") != std::string::npos || trimmed.find("@reflect_enum") != std::string::npos)) {
            nextLineIsReflectedEnum = true;
            continue;
        }

        // Check if this line is a struct reflection marker: // [ReflectStruct]
        if (!inComponent && !inStruct && !inEnum && trimmed.find("ReflectStruct") != std::string::npos) {
            nextLineIsReflectedStruct = true;
            continue;
        }

        // Check if this line is a class reflection marker: // [ReflectClass] or // [ReflectClass("Category/Name")] or @reflect
        if (!inComponent && !inStruct && !inEnum && (trimmed.find("@reflect") != std::string::npos || trimmed.find("ReflectClass") != std::string::npos)) {
            nextLineIsReflected = true;
            pendingCategory = "";
            pendingDisplayName = "";

            size_t q1 = trimmed.find('"');
            if (q1 != std::string::npos) {
                size_t q2 = trimmed.find('"', q1 + 1);
                if (q2 != std::string::npos) {
                    std::string menuPath = trimmed.substr(q1 + 1, q2 - q1 - 1);
                    size_t slashPos = menuPath.find('/');
                    if (slashPos != std::string::npos) {
                        pendingCategory = menuPath.substr(0, slashPos);
                        pendingDisplayName = menuPath.substr(slashPos + 1);
                    } else {
                        pendingCategory = menuPath;
                    }
                }
            }
            continue;
        }

        if (nextLineIsReflectedEnum) {
            nextLineIsReflectedEnum = false;
            if (trimmed.find("enum") != std::string::npos) {
                std::regex enumRegex("enum\\s+(?:class\\s+|struct\\s+)?(?:ENGINE_API\\s+)?(\\w+)");
                std::smatch match;
                if (std::regex_search(trimmed, match, enumRegex)) {
                    inEnum = true;
                    braceDepth = 0;
                    currentEnum = ReflectedEnum();
                    currentEnum.name = match[1].str();
                    currentEnum.qualifiedName = inEngineNamespace ? ("Engine::" + currentEnum.name) : currentEnum.name;
                    currentEnum.headerName = getIncludePath(filePath, inputDir);
                }
            }
            for (char c : trimmed) {
                if (c == '{') ++braceDepth;
                else if (c == '}') --braceDepth;
            }
            size_t openBrace = trimmed.find('{');
            if (openBrace != std::string::npos && inEnum) {
                std::string afterBrace = trimmed.substr(openBrace + 1);
                size_t closeBrace = afterBrace.find('}');
                if (closeBrace != std::string::npos) {
                    afterBrace = afterBrace.substr(0, closeBrace);
                    inEnum = false;
                }
                std::stringstream ss(afterBrace);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t cPos = item.find("//");
                    if (cPos != std::string::npos) item = item.substr(0, cPos);
                    size_t eqPos = item.find('=');
                    if (eqPos != std::string::npos) item = item.substr(0, eqPos);
                    item = trim(item);
                    if (!item.empty() && item != "{" && item != "}") {
                        currentEnum.options.push_back(item);
                    }
                }
                if (!inEnum) {
                    outEnums.push_back(currentEnum);
                }
            }
            continue;
        }

        if (inEnum) {
            std::string codeOnly = trimmed;
            size_t cPos = codeOnly.find("//");
            if (cPos != std::string::npos) codeOnly = codeOnly.substr(0, cPos);
            codeOnly = trim(codeOnly);

            for (char c : codeOnly) {
                if (c == '{') ++braceDepth;
                else if (c == '}') --braceDepth;
            }

            if (braceDepth <= 0 && codeOnly.find('}') != std::string::npos) {
                size_t closePos = codeOnly.find('}');
                std::string beforeClose = codeOnly.substr(0, closePos);
                if (!beforeClose.empty()) {
                    std::stringstream ss(beforeClose);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        size_t eqPos = item.find('=');
                        if (eqPos != std::string::npos) item = item.substr(0, eqPos);
                        item = trim(item);
                        if (!item.empty() && item != "{" && item != "}") {
                            currentEnum.options.push_back(item);
                        }
                    }
                }
                inEnum = false;
                outEnums.push_back(currentEnum);
                continue;
            }

            if (!codeOnly.empty() && codeOnly != "{") {
                std::stringstream ss(codeOnly);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t eqPos = item.find('=');
                    if (eqPos != std::string::npos) item = item.substr(0, eqPos);
                    item = trim(item);
                    if (!item.empty() && item != "{" && item != "}") {
                        currentEnum.options.push_back(item);
                    }
                }
            }
            continue;
        }

        if (nextLineIsReflectedStruct) {
            nextLineIsReflectedStruct = false;
            if (trimmed.find("struct") != std::string::npos || trimmed.find("class") != std::string::npos) {
                std::regex structRegex("(?:struct|class)\\s+(?:ENGINE_API\\s+)?(\\w+)");
                std::smatch match;
                if (std::regex_search(trimmed, match, structRegex)) {
                    inStruct = true;
                    braceDepth = 0;
                    currentStruct = ReflectedStruct();
                    currentStruct.name = match[1].str();
                    currentStruct.qualifiedName = inEngineNamespace ? ("Engine::" + currentStruct.name) : currentStruct.name;
                    currentStruct.headerName = getIncludePath(filePath, inputDir);
                }
            }
            // Count braces on the declaration line if any
            for (char c : trimmed) {
                if (c == '{') ++braceDepth;
                else if (c == '}') --braceDepth;
            }
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
                    braceDepth = 0;
                    currentComp = ReflectedComponent();
                    currentComp.name = match[1].str();
                    currentComp.qualifiedName = inEngineNamespace ? ("Engine::" + currentComp.name) : currentComp.name;

                    if (!pendingCategory.empty()) {
                        currentComp.category = pendingCategory;
                    }
                    if (!pendingDisplayName.empty()) {
                        currentComp.displayName = pendingDisplayName;
                    } else {
                        // Auto-generate clean display name from class name
                        std::string cName = currentComp.name;
                        if (cName.size() > 9 && cName.rfind("Component") == cName.size() - 9) {
                            cName = cName.substr(0, cName.size() - 9);
                        }
                        std::string formatted;
                        for (size_t i = 0; i < cName.size(); ++i) {
                            if (i > 0 && std::isupper(cName[i]) && !std::isupper(cName[i-1])) {
                                formatted += " ";
                            }
                            formatted += cName[i];
                        }
                        currentComp.displayName = formatted;
                    }

                    currentComp.headerName = getIncludePath(filePath, inputDir);
                }
            }
            for (char c : trimmed) {
                if (c == '{') ++braceDepth;
                else if (c == '}') --braceDepth;
            }
            continue;
        }

        if (inComponent || inStruct) {
            // Count braces on this line (excluding comments)
            std::string codeOnly = trimmed;
            size_t cPos = codeOnly.find("//");
            if (cPos != std::string::npos) codeOnly = codeOnly.substr(0, cPos);

            int openCount = 0;
            int closeCount = 0;
            for (char c : codeOnly) {
                if (c == '{') { ++braceDepth; ++openCount; }
                else if (c == '}') { --braceDepth; ++closeCount; }
            }

            // Check for end of struct/class (brace depth reaches 0 and line contains closing brace + semicolon)
            if (braceDepth <= 0 && closeCount > 0 && codeOnly.find(';') != std::string::npos) {
                if (inComponent) {
                    outComponents.push_back(currentComp);
                    inComponent = false;
                } else if (inStruct) {
                    outStructs.push_back(currentStruct);
                    inStruct = false;
                }
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

                // Skip if it looks like a function declaration (contains parentheses)
                if (trimmed.find('(') != std::string::npos) {
                    continue;
                }

                // Split type and name
                size_t lastSpace = trimmed.find_last_of(" \t");
                if (lastSpace != std::string::npos) {
                    ReflectedField field;
                    field.type = trim(trimmed.substr(0, lastSpace));
                    std::string fName = trim(trimmed.substr(lastSpace + 1));

                    // Check for array syntax e.g. layers[2]
                    size_t b1 = fName.find('[');
                    size_t b2 = fName.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos && b2 > b1) {
                        std::string sizeStr = fName.substr(b1 + 1, b2 - b1 - 1);
                        try {
                            field.arraySize = std::stoi(sizeStr);
                        } catch (...) {
                            field.arraySize = 1;
                        }
                        field.name = trim(fName.substr(0, b1));
                    } else {
                        field.name = fName;
                        field.arraySize = 1;
                    }

                    if (inComponent) {
                        currentComp.fields.push_back(field);
                    } else if (inStruct) {
                        currentStruct.fields.push_back(field);
                    }
                }
            }
        }
    }
}

void generateFieldsCode(std::ostream& out,
                        const std::vector<ReflectedField>& fields,
                        const std::string& parentQualifiedName,
                        const std::map<std::string, ReflectedStruct>& structMap,
                        const std::map<std::string, ReflectedEnum>& enumMap,
                        int indentLevel) {
    std::string ind(indentLevel * 4, ' ');
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];

        auto it = structMap.find(field.type);
        if (it != structMap.end()) {
            // Nested struct
            const ReflectedStruct& s = it->second;
            if (field.arraySize > 1) {
                for (int arrIdx = 0; arrIdx < field.arraySize; ++arrIdx) {
                    std::string elemName = field.name + "[" + std::to_string(arrIdx) + "]";
                    out << ind << "{ \"" << elemName << "\", Engine::FieldType::Struct, offsetof("
                        << parentQualifiedName << ", " << elemName << "), {}, {\n";
                    generateFieldsCode(out, s.fields, s.qualifiedName, structMap, enumMap, indentLevel + 1);
                    out << ind << "}, \"" << s.name << "\" }";
                    if (i < fields.size() - 1 || arrIdx < field.arraySize - 1) out << ",";
                    out << "\n";
                }
            } else {
                out << ind << "{ \"" << field.name << "\", Engine::FieldType::Struct, offsetof("
                    << parentQualifiedName << ", " << field.name << "), {}, {\n";
                generateFieldsCode(out, s.fields, s.qualifiedName, structMap, enumMap, indentLevel + 1);
                out << ind << "}, \"" << s.name << "\" }";
                if (i < fields.size() - 1) out << ",";
                out << "\n";
            }
        } else {
            // Check if field.type is an enum registered via [ReflectEnum]
            auto enumIt = enumMap.find(field.type);
            if (enumIt != enumMap.end()) {
                out << ind << "{ \"" << field.name << "\", Engine::FieldType::Enum, offsetof("
                    << parentQualifiedName << ", " << field.name << "), { ";
                for (size_t optIdx = 0; optIdx < enumIt->second.options.size(); ++optIdx) {
                    out << "\"" << enumIt->second.options[optIdx] << "\"";
                    if (optIdx + 1 < enumIt->second.options.size()) out << ", ";
                }
                out << " } }";
                if (i < fields.size() - 1) out << ",";
                out << "\n";
                continue;
            }

            // Primitive or built-in Enum field
            std::string enumStr = getFieldTypeEnum(field.type);
            if (!enumStr.empty()) {
                if (enumStr == "Engine::FieldType::Enum") {
                    if (field.type == "LightType" || field.type == "Engine::LightType") {
                        out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                            << parentQualifiedName << ", " << field.name << "), { \"Directional\", \"Point\", \"Spot\" } }";
                    } else if (field.type == "FogMode" || field.type == "VolumetricFogMode") {
                        out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                            << parentQualifiedName << ", " << field.name << "), { \"Atmospheric Haze\", \"Dense / Stylized Fog\" } }";
                    } else if (field.type == "CloudPreset" || field.type == "WeatherPreset") {
                        out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                            << parentQualifiedName << ", " << field.name << "), { \"Custom\", \"Clear Sky\", \"Fair Weather Cumulus\", \"Scattered Clouds\", \"Broken Sky\", \"Overcast\", \"Stormy\" } }";
                    } else if (field.type == "WeatherType") {
                        out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                            << parentQualifiedName << ", " << field.name << "), { \"Clear\", \"Rain\", \"Snow\", \"Storm\" } }";
                    } else {
                        out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                            << parentQualifiedName << ", " << field.name << "), { \"Third Person Follow\", \"First Person\", \"Fixed Look At\", \"2D Follow\" } }";
                    }
                } else {
                    out << ind << "{ \"" << field.name << "\", " << enumStr << ", offsetof("
                        << parentQualifiedName << ", " << field.name << ") }";
                }
                if (i < fields.size() - 1) out << ",";
                out << "\n";
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
    std::vector<ReflectedStruct> structs;
    std::vector<ReflectedSystem> systems;
    std::vector<ReflectedEnum> enums;

    for (const auto& inputDir : inputDirs) {
        try {
            if (fs::exists(inputDir)) {
                for (const auto& entry : fs::recursive_directory_iterator(inputDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".hpp" || ext == ".h") {
                            parseHeader(entry.path(), inputDir, components, structs, systems, enums);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning directory " << inputDir << ": " << e.what() << std::endl;
        }
    }

    // Build struct lookup map
    std::map<std::string, ReflectedStruct> structMap;
    for (const auto& s : structs) {
        structMap[s.name] = s;
        if (!s.qualifiedName.empty() && s.qualifiedName != s.name) {
            structMap[s.qualifiedName] = s;
        }
    }

    // Build enum lookup map
    std::map<std::string, ReflectedEnum> enumMap;
    for (const auto& e : enums) {
        enumMap[e.name] = e;
        if (!e.qualifiedName.empty() && e.qualifiedName != e.name) {
            enumMap[e.qualifiedName] = e;
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
    for (const auto& s : structs) {
        if (std::find(includedHeaders.begin(), includedHeaders.end(), s.headerName) == includedHeaders.end()) {
            out << "#include \"" << s.headerName << "\"\n";
            includedHeaders.push_back(s.headerName);
        }
    }
    for (const auto& e : enums) {
        if (std::find(includedHeaders.begin(), includedHeaders.end(), e.headerName) == includedHeaders.end()) {
            out << "#include \"" << e.headerName << "\"\n";
            includedHeaders.push_back(e.headerName);
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

        out << "    {\n";
        out << "        Engine::ComponentReflection refl;\n";
        out << "        refl.name = \"" << compName << "\";\n";
        out << "        refl.category = \"" << comp.category << "\";\n";
        out << "        refl.displayName = \"" << (comp.displayName.empty() ? compName : comp.displayName) << "\";\n";
        out << "        refl.fields = {\n";
        generateFieldsCode(out, comp.fields, comp.qualifiedName, structMap, enumMap, 3);
        out << "        };\n";
        out << "        refl.add = [](Registry& reg, Entity e) { reg.emplace<" << comp.qualifiedName << ">(e, " << comp.qualifiedName << "{}); };\n";
        out << "        refl.has = [](Registry& reg, Entity e) { return reg.has<" << comp.qualifiedName << ">(e); };\n";
        out << "        refl.remove = [](Registry& reg, Entity e) { reg.remove<" << comp.qualifiedName << ">(e); };\n";
        out << "        refl.get = [](Registry& reg, Entity e) { return static_cast<void*>(reg.get<" << comp.qualifiedName << ">(e)); };\n";
        out << "        refl.isEnabled = [](const Registry& reg, Entity e) { return reg.isComponentEnabled<" << comp.qualifiedName << ">(e); };\n";
        out << "        refl.setEnabled = [](Registry& reg, Entity e, bool enabled) { reg.setComponentEnabled<" << comp.qualifiedName << ">(e, enabled); };\n";
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
              << components.size() << " components, " << structs.size() << " structs, "
              << enums.size() << " enums, "
              << systems.size() << " systems)" << std::endl;

    return 0;
}
