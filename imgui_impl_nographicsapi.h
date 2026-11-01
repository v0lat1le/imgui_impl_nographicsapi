#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h"
#include "NoGraphicsAPI/NoGraphicsAPI.hpp"


struct ImGui_ImplNoGraphicsAPI_RenderState {
    gpu::CommandBuffer* commands;
    gpu::PSO* pso;
};

struct ImGui_ImplNoGraphicsAPI_InitInfo {
    gpu::Device* device;
    gpu::Format image_format = gpu::Format::bgra8_srgb;
    int image_count = 2;
};

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Init(const ImGui_ImplNoGraphicsAPI_InitInfo& init_info);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_NewFrame();
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_UpdateTextures(gpu::CommandBuffer* commands, ImDrawData* draw_data);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_RenderDrawData(gpu::CommandBuffer* commands, ImDrawData* draw_data);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Shutdown();

#endif
