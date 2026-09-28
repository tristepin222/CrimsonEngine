#include "scenes/JSONUtils.hpp"
#include <sstream>

/**
 * @namespace JSONUtils
 * @brief Helper utilities for hand-parsing and formatting simple JSON files.
 */
namespace JSONUtils {

    /**
     * @brief Returns a string of spaces representing indentation level.
     * @param level Indentation multiplier level.
     * @return Space character string.
     */
    std::string indent(int level) {
        return std::string(static_cast<size_t>(level) * 2, ' ');
    }

    /**
     * @brief Quotes a string value.
     * @param value Raw string.
     * @return Quoted string.
     */
    std::string quote(const std::string& value) {
        return "\"" + value + "\"";
    }

    /**
     * @brief Converts glm::vec3 value to a JSON array representation.
     * @param v Input vector.
     * @return JSON array representation.
     */
    std::string vec3ToJson(const glm::vec3& v) {
        std::ostringstream out;
        out << "[" << v.x << ", " << v.y << ", " << v.z << "]";
        return out.str();
    }

    /**
     * @brief Converts glm::vec4 value to a JSON array representation.
     * @param v Input vector.
     * @return JSON array representation.
     */
    std::string vec4ToJson(const glm::vec4& v) {
        std::ostringstream out;
        out << "[" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << "]";
        return out.str();
    }

    /**
     * @brief Extract string values corresponding to target key.
     * @param source Raw JSON source.
     * @param key Target JSON property key.
     * @return Extracted value.
     */
    std::string extractStringValue(const std::string& source, const std::string& key) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) {
            return {};
        }

        size_t colonPos = source.find(':', keyPos);
        size_t firstQuote = source.find('"', colonPos + 1);
        size_t secondQuote = source.find('"', firstQuote + 1);
        if (colonPos == std::string::npos || firstQuote == std::string::npos || secondQuote == std::string::npos) {
            return {};
        }

        return source.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    }

    /**
     * @brief Extract float array values corresponding to target key.
     * @param source Raw JSON source.
     * @param key Target JSON property key.
     * @param values Destination buffer.
     * @param count Maximum array values to read.
     * @return True if successful, false otherwise.
     */
    bool extractFloatArray(const std::string& source, const std::string& key, float* values, size_t count) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) {
            return false;
        }

        size_t open = source.find('[', keyPos);
        size_t close = source.find(']', open);
        if (open == std::string::npos || close == std::string::npos) {
            return false;
        }

        std::string payload = source.substr(open + 1, close - open - 1);
        for (char& c : payload) {
            if (c == ',') {
                c = ' ';
            }
        }

        std::istringstream stream(payload);
        for (size_t i = 0; i < count; ++i) {
            if (!(stream >> values[i])) {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Extract single float value corresponding to target key.
     * @param source Raw JSON source.
     * @param key Target JSON property key.
     * @param value Destination variable reference.
     * @return True if successful, false otherwise.
     */
    bool extractFloatValue(const std::string& source, const std::string& key, float& value) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) {
            return false;
        }

        size_t colonPos = source.find(':', keyPos);
        if (colonPos == std::string::npos) {
            return false;
        }

        std::string payload = source.substr(colonPos + 1);
        std::istringstream stream(payload);
        return static_cast<bool>(stream >> value);
    }

    /**
     * @brief Extract boolean value corresponding to target key.
     * @param source Raw JSON source.
     * @param key Target JSON property key.
     * @param value Destination variable reference.
     * @return True if successful, false otherwise.
     */
    bool extractBoolValue(const std::string& source, const std::string& key, bool& value) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) {
            return false;
        }

        size_t colonPos = source.find(':', keyPos);
        if (colonPos == std::string::npos) {
            return false;
        }

        size_t truePos = source.find("true", colonPos);
        size_t falsePos = source.find("false", colonPos);
        size_t commaPos = source.find(',', colonPos);
        size_t bracePos = source.find('}', colonPos);
        size_t endPos = std::min(commaPos, bracePos);

        if (truePos != std::string::npos && (endPos == std::string::npos || truePos < endPos)) {
            value = true;
            return true;
        } else if (falsePos != std::string::npos && (endPos == std::string::npos || falsePos < endPos)) {
            value = false;
            return true;
        }
        return false;
    }

    /**
     * @brief Extracts individual entity JSON objects.
     * @param source Raw JSON source.
     * @return Vector of entity JSON strings.
     */
    std::vector<std::string> extractEntityObjects(const std::string& source) {
        std::vector<std::string> objects;
        size_t entitiesPos = source.find("\"entities\"");
        if (entitiesPos == std::string::npos) {
            return objects;
        }

        size_t arrayOpen = source.find('[', entitiesPos);
        if (arrayOpen == std::string::npos) {
            return objects;
        }

        int arrayDepth = 0;
        size_t arrayClose = std::string::npos;
        for (size_t i = arrayOpen; i < source.size(); ++i) {
            if (source[i] == '[') {
                ++arrayDepth;
            } else if (source[i] == ']') {
                --arrayDepth;
                if (arrayDepth == 0) {
                    arrayClose = i;
                    break;
                }
            }
        }

        if (arrayClose == std::string::npos) {
            return objects;
        }

        size_t pos = arrayOpen + 1;
        while (pos < arrayClose) {
            size_t objectStart = source.find('{', pos);
            if (objectStart == std::string::npos || objectStart > arrayClose) {
                break;
            }

            int depth = 0;
            size_t objectEnd = objectStart;
            for (; objectEnd < source.size(); ++objectEnd) {
                if (source[objectEnd] == '{') {
                    ++depth;
                } else if (source[objectEnd] == '}') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }

            if (depth == 0 && objectEnd < source.size()) {
                objects.push_back(source.substr(objectStart, objectEnd - objectStart + 1));
            }

            pos = objectEnd + 1;
        }

        return objects;
    }

    /**
     * @brief Finds and extracts an array of integers associated with a JSON key.
     */
    bool extractIntVector(const std::string& source, const std::string& key, std::vector<int>& values) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) {
            return false;
        }

        size_t open = source.find('[', keyPos);
        size_t close = source.find(']', open);
        if (open == std::string::npos || close == std::string::npos) {
            return false;
        }

        std::string payload = source.substr(open + 1, close - open - 1);
        for (char& c : payload) {
            if (c == ',') {
                c = ' ';
            }
        }

        std::istringstream stream(payload);
        int val;
        values.clear();
        while (stream >> val) {
            values.push_back(val);
        }

        return true;
    }

    /**
     * @brief Finds and extracts an array of quoted string values associated with a JSON key.
     *        Handles both compact (["a","b"]) and pretty-printed arrays.
     */
    bool extractStringVector(const std::string& source, const std::string& key, std::vector<std::string>& values) {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = source.find(token);
        if (keyPos == std::string::npos) return false;

        size_t open = source.find('[', keyPos);
        if (open == std::string::npos) return false;

        // Find matching close bracket (handles nested arrays)
        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = open; i < source.size(); ++i) {
            if (source[i] == '[') ++depth;
            else if (source[i] == ']') {
                --depth;
                if (depth == 0) { close = i; break; }
            }
        }
        if (close == std::string::npos) return false;

        std::string payload = source.substr(open + 1, close - open - 1);

        // Extract all quoted strings from the payload
        values.clear();
        size_t pos = 0;
        while (pos < payload.size()) {
            size_t q1 = payload.find('"', pos);
            if (q1 == std::string::npos) break;
            size_t q2 = payload.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            values.push_back(payload.substr(q1 + 1, q2 - q1 - 1));
            pos = q2 + 1;
        }
        return !values.empty();
    }

    std::string extractSubObject(const std::string& source, const std::string& key) {
        const std::string token = "\"" + key + "\"";
        size_t pos = 0;
        while ((pos = source.find(token, pos)) != std::string::npos) {
            // Verify token is followed by optional whitespace and ':'
            size_t afterKey = pos + token.length();
            while (afterKey < source.size() && (source[afterKey] == ' ' || source[afterKey] == '\t' || source[afterKey] == '\r' || source[afterKey] == '\n')) {
                ++afterKey;
            }

            if (afterKey < source.size() && source[afterKey] == ':') {
                size_t open = source.find('{', afterKey);
                if (open != std::string::npos) {
                    bool validObj = true;
                    for (size_t k = afterKey + 1; k < open; ++k) {
                        if (source[k] != ' ' && source[k] != '\t' && source[k] != '\r' && source[k] != '\n') {
                            validObj = false;
                            break;
                        }
                    }

                    if (validObj) {
                        int depth = 0;
                        bool inQuotes = false;
                        size_t close = std::string::npos;
                        for (size_t i = open; i < source.size(); ++i) {
                            char c = source[i];
                            if (c == '"' && (i == 0 || source[i - 1] != '\\')) {
                                inQuotes = !inQuotes;
                            } else if (!inQuotes) {
                                if (c == '{') {
                                    ++depth;
                                } else if (c == '}') {
                                    --depth;
                                    if (depth == 0) {
                                        close = i;
                                        break;
                                    }
                                }
                            }
                        }
                        if (close != std::string::npos) {
                            return source.substr(open, close - open + 1);
                        }
                    }
                }
            }
            pos += token.length();
        }
        return {};
    }
}



