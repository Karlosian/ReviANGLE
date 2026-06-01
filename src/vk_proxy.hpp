#pragma once
#include <windows.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>

// Vulkan proxy layer for OpenGL -> Vulkan translation
// Analogous to wgl_proxy.cpp for DirectX/ANGLE

namespace vkproxy {

// Vulkan rendering context (equivalent to HGLRC for DirectX backend)
struct VulkanContext {
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    
    // Presentation
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Frame tracking
    uint32_t currentFrame = 0;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    
    // Window info
    HDC hdc = nullptr;
    HWND hwnd = nullptr;
};

// Global Vulkan state (singleton per DLL instance)
class VulkanState {
public:
    static VulkanState& getInstance();
    
    bool initialize();
    void shutdown();
    
    VkInstance getVkInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkQueue getPresentQueue() const { return presentQueue; }
    
    std::unique_ptr<VulkanContext> createContext(HDC hdc, HWND hwnd);
    bool makeContextCurrent(VulkanContext* ctx);
    bool presentFrame(VulkanContext* ctx);
    
private:
    VulkanState() = default;
    ~VulkanState() = default;
    
    // Vulkan global objects
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = UINT32_MAX;
    uint32_t presentQueueFamily = UINT32_MAX;
    VkSurfaceKHR currentSurface = VK_NULL_HANDLE;
};

} // namespace vkproxy
