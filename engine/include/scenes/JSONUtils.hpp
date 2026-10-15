#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

/**
 * @namespace JSONUtils
 * @brief Set of lightweight JSON parsing and formatting helper utilities.
 */
namespace JSONUtils {
    /**
     * @brief Generates indentation string according to spacing level.
     * @param level Indent level.
     * @return Indent spacing.
     */
    std::string indent(int level);
    /**
     * @brief Wraps a string in double quotes.
     * @param value Raw string.
     * @return Quoted string.
     */
    std::string quote(const std::string& value);
    /**
     * @brief Serializes a glm::vec3 to a JSON array.
     * @param v 3D vector.
     * @return JSON formatted array string.
     */
    std::string vec3ToJson(const glm::vec3& v);
    /**
     * @brief Serializes a glm::vec4 to a JSON array.
     * @param v 4D vector.
     * @return JSON formatted array string.
     */
    std::string vec4ToJson(const glm::vec4& v);
    
    /**
     * @brief Finds and extracts a string value associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @return Extracted string value.
     */
    std::string extractStringValue(const std::string& source, const std::string& key);
    /**
     * @brief Finds and extracts an array of floats associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @param values Destination buffer.
     * @param count Count of floats.
     * @return True if successful, false otherwise.
     */
    bool extractFloatArray(const std::string& source, const std::string& key, float* values, size_t count);
    /**
     * @brief Finds and extracts a single float value associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @param value Destination float reference.
     * @return True if successful, false otherwise.
     */
    bool extractFloatValue(const std::string& source, const std::string& key, float& value);
    /**
     * @brief Finds and extracts a boolean value associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @param value Destination bool reference.
     * @return True if successful, false otherwise.
     */
    bool extractBoolValue(const std::string& source, const std::string& key, bool& value);
    /**
     * @brief Extracts individual entity JSON blocks from a main scene array.
     * @param source Full JSON array text.
     * @return Vector of individual entity JSON strings.
     */
    std::vector<std::string> extractEntityObjects(const std::string& source);
    /**
     * @brief Finds and extracts an array of integers associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @param values Destination vector.
     * @return True if successful, false otherwise.
     */
    bool extractIntVector(const std::string& source, const std::string& key, std::vector<int>& values);
    /**
     * @brief Finds and extracts an array of quoted strings associated with a JSON key.
     * @param source JSON text source.
     * @param key Search key.
     * @param values Destination vector of strings.
     * @return True if the key was found and at least one string was parsed.
     */
    bool extractStringVector(const std::string& source, const std::string& key, std::vector<std::string>& values);

    /**
     * @brief Extracts a sub-object JSON string (enclosed in { ... }) for a given key.
     * @param source Raw JSON text.
     * @param key Property key.
     * @return Extracted JSON object string including surrounding braces, or empty string if not found.
     */
    std::string extractSubObject(const std::string& source, const std::string& key);
}

