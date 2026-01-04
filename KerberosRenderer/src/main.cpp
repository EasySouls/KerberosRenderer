#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

constexpr uint32_t width = 800;
constexpr uint32_t height = 600;

class HelloTriangleApplication
{
private:
    GLFWwindow* m_Window = nullptr;

public:
    void run() 
    {
        InitWindow();
        InitVulkan();
        MainLoop();
        Cleanup();
    }

private:
    void InitWindow() 
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_Window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
    }

    void InitVulkan() 
    {

    }

    void MainLoop() const 
    {
        while (!glfwWindowShouldClose(m_Window)) {
            glfwPollEvents();
        }
    }

    void Cleanup() const 
    {
        glfwDestroyWindow(m_Window);

        glfwTerminate();
    }
};

int main() {
	try {
		HelloTriangleApplication app;
		app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}