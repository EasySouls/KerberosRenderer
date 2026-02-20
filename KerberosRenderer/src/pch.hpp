#pragma once

// Standard Library
#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <optional>

// Windows
#define NOMINMAX
#include <Windows.h>

// Third-party
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Core.hpp"