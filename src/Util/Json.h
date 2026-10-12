#pragma once

#include <nlohmann/json.hpp>

#include <string>

// object[key] when it's a string, else an empty string (missing keys, nulls and numbers
// included). API responses leave fields out or set them to null freely.
inline std::string StringOrEmpty(const nlohmann::json& object, const char* key)
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}
