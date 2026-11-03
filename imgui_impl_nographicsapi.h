#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h"
#include "NoGraphicsAPI/NoGraphicsAPI.hpp"


struct ImGui_ImplNoGraphicsAPI_RenderState {
    gpu::CommandBuffer* commands;
    gpu::PSO* pso;
    gpu::uint32 sampler_slot;
};

struct ImGui_ImplNoGraphicsAPI_InitInfo {
    gpu::Device* device;
    gpu::Format image_format = gpu::Format::bgra8_srgb;
    
    gpu::uint32 linear_sampler_slot;
    gpu::uint32 nearest_sampler_slot;

    void* user_data;
    gpu::GpuCpuRange<gpu::byte> (*allocate_frame_memory)(void* user_data, gpu::uint64 size, gpu::uint64 alignment);
    gpu::Texture* (*create_texture)(void* user_data, const gpu::TextureDesc& desc);
    void          (*destroy_texture)(void* user_data, gpu::Texture* texture);
    gpu::uint32   (*allocate_texture_slot)(void* user_data);
    void          (*free_texture_slot)(void* user_data, gpu::uint32 slot);
};

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Init(const ImGui_ImplNoGraphicsAPI_InitInfo& init_info);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_NewFrame();
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_UpdateTextures(gpu::CommandBuffer* commands, ImDrawData* draw_data);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_RenderDrawData(gpu::CommandBuffer* commands, ImDrawData* draw_data);
IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Shutdown();

#endif
