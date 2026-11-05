#pragma once

// Single place that includes volk + Vulkan Memory Allocator with matching settings.

#if defined(_WIN32)
#include <windows.h>
#endif

// volk loads Vulkan function pointers at runtime, so VMA must fetch them dynamically too.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <volk.h>
#include <vk_mem_alloc.h>
