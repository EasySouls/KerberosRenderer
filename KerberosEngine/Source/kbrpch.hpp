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

#ifdef KBR_PLATFORM_WINDOWS
#include <Windows.h>
#endif

// Third-party
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Profiling/Instrumentor.hpp"