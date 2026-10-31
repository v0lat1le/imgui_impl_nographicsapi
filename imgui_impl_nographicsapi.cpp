#include "imgui_impl_nographicsapi.h"


namespace {
    struct ImGuiRoot {
        ImDrawVert* vertices;
        ImVec2 scale;
        ImVec2 translate;
    };

    struct ImGui_ImplNoGraphicsAPI_FrameData {
        gpu::GpuCpuRange<byte> heap;
    };

    struct ImGui_ImplNoGraphicsAPI_Data {
        ImGui_ImplNoGraphicsAPI_InitInfo init_info;
        gpu::PSO* pso;
        gpu::GpuHeap heap;
        ImVector<ImGui_ImplNoGraphicsAPI_FrameData> frame_data;
        int frame_index = 0;
    };

    ImGui_ImplNoGraphicsAPI_Data* ImGui_ImplNoGraphicsAPI_GetBackendData() {
        return ImGui::GetCurrentContext() ? static_cast<ImGui_ImplNoGraphicsAPI_Data*>(ImGui::GetIO().BackendRendererUserData) : nullptr;
    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState(const ImDrawList* parent_list, const ImDrawCmd* cmd) {

    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerLinear(const ImDrawList* parent_list, const ImDrawCmd* cmd) {

    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerNearest(const ImDrawList* parent_list, const ImDrawCmd* cmd) {

    }

    void ImGui_ImplNoGraphicsAPI_UpdateTexture(ImTextureData* tex) {

    }

    template<typename T>
    gpu::GpuCpuRange<T> allocate(gpu::GpuCpuRange<gpu::byte>& heap, gpu::uint64 count=1) {
        gpu::uint64 size = sizeof(T)*count;
        IM_ASSERT(size <= heap.size);
        gpu::GpuCpuRange<T> result {
            .cpu = reinterpret_cast<T*>(heap.cpu),
            .gpu = reinterpret_cast<T*>(heap.gpu),
            .size = size
        };
        heap.cpu += size;
        heap.gpu += size;
        heap.size -= size;
        return result;
    }

    byte shader_code[] = {
#include "imgui_impl_nographicsapi.inl"
    };
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Init(const ImGui_ImplNoGraphicsAPI_InitInfo& init_info) {
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(init_info.device != nullptr);

    ImGui_ImplNoGraphicsAPI_Data* bd = IM_NEW(ImGui_ImplNoGraphicsAPI_Data)();
    bd->init_info = init_info;
    const gpu::uint64 frame_heap_size = 128u*1024;
    bd->heap = gpu::create_gpu_heap(init_info.device, frame_heap_size*init_info.image_count);
    bd->frame_data.resize(init_info.image_count);
    gpu::GpuCpuRange<byte> frame_heap = bd->heap.range;
    for (ImGui_ImplNoGraphicsAPI_FrameData& frame_data: bd->frame_data) {
        frame_data.heap = allocate<byte>(frame_heap, frame_heap_size);
    }
    bd->pso = gpu::create_graphics_pso(init_info.device, {
        .vertex = {.code={shader_code, sizeof(shader_code)}, .entry_point="vertexMain"},
        .fragment = {.code={shader_code, sizeof(shader_code)}, .entry_point="fragmentMain"},
        .color_targets = {{
            .format = init_info.image_format,
            .blend = {
                .enabled = true,
                .color = {
                    .source = gpu::BlendFactor::source_alpha,
                    .destination = gpu::BlendFactor::one_minus_source_alpha,
                    .operation = gpu::BlendOp::add
                },
                .alpha = {
                    .source = gpu::BlendFactor::one,
                    .destination = gpu::BlendFactor::one_minus_source_alpha,
                    .operation = gpu::BlendOp::add
                },
            },
            .write_mask = 0x0F,  // rgba, vulkan-style
        }, } 
    });
    io.BackendRendererUserData = bd;
    io.BackendRendererName = "imgui_impl_nographicsapi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.DrawCallback_ResetRenderState = ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState;
    platform_io.DrawCallback_SetSamplerLinear = ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerLinear;
    platform_io.DrawCallback_SetSamplerNearest = ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerNearest;
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_NewFrame() {
    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplNoGraphicsAPI_Init()?");
    IM_UNUSED(bd);
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_RenderDrawData(gpu::CommandBuffer* commands, ImDrawData* draw_data) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    gpu::uint32 fb_width = static_cast<gpu::uint32>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x + 0.5f);
    gpu::uint32 fb_height = static_cast<gpu::uint32>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y + 0.5f);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }

    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    ImGui_ImplNoGraphicsAPI_FrameData& frame_data = bd->frame_data[bd->frame_index];
    gpu::GpuCpuRange<gpu::byte> frame_heap = frame_data.heap;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex: *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) {
                ImGui_ImplNoGraphicsAPI_UpdateTexture(tex);
            }
        }
    }

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplNoGraphicsAPI_RenderState render_state { .commands = commands, .pso = bd->pso };
    platform_io.Renderer_RenderState = &render_state;

    gpu::set_depth_stencil(commands, {});
    gpu::bind_pso(render_state.commands, render_state.pso);
    gpu::set_viewport(commands, {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(fb_width),
        .height = static_cast<float>(fb_height)
    });

    gpu::GpuCpuRange<ImGuiRoot> root = allocate<ImGuiRoot>(frame_heap);
    gpu::GpuCpuRange<ImDrawVert> vertices = allocate<ImDrawVert>(frame_heap, draw_data->TotalVtxCount);
    gpu::GpuCpuRange<ImDrawIdx> indices = allocate<ImDrawIdx>(frame_heap, draw_data->TotalIdxCount);
    root.cpu->vertices = vertices.gpu;
    root.cpu->scale = { 2.0f / draw_data->DisplaySize.x, 2.0f / draw_data->DisplaySize.y };
    root.cpu->translate = { -1.0f - draw_data->DisplayPos.x * root.cpu->scale.x, -1.0f - draw_data->DisplayPos.y * root.cpu->scale.y };

    // Render command lists
    // (Because we merge all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    for (const ImDrawList* draw_list: draw_data->CmdLists) {
        memcpy(vertices.cpu+global_vtx_offset, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(indices.cpu+global_idx_offset, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                if (pcmd->UserCallback == ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState) {
                    //ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
                    //last_image_view = VK_NULL_HANDLE;
                } else {
                    pcmd->UserCallback(draw_list, pcmd);
                }
                continue;
            }

            // Project scissor/clipping rectangles into framebuffer space
            gpu::int32 clip_min_x = static_cast<gpu::int32>((pcmd->ClipRect.x - draw_data->DisplayPos.x)*draw_data->FramebufferScale.x);
            gpu::int32 clip_min_y = static_cast<gpu::int32>((pcmd->ClipRect.y - draw_data->DisplayPos.y)*draw_data->FramebufferScale.y);
            gpu::int32 clip_max_x = static_cast<gpu::int32>((pcmd->ClipRect.z - draw_data->DisplayPos.x)*draw_data->FramebufferScale.x);
            gpu::int32 clip_max_y = static_cast<gpu::int32>((pcmd->ClipRect.w - draw_data->DisplayPos.y)*draw_data->FramebufferScale.y);

            // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
            if (clip_min_x < 0) { clip_min_x = 0; }
            if (clip_min_y < 0) { clip_min_y = 0; }
            if (clip_max_x > static_cast<gpu::int32>(fb_width)) { clip_max_x = fb_width; }
            if (clip_max_y > static_cast<gpu::int32>(fb_height)) { clip_max_y = fb_height; }
            if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y) {
                continue;
            }

            gpu::set_scissor(commands, {
                .x = clip_min_x,
                .y = clip_min_y,
                .width = static_cast<gpu::uint32>(clip_max_x - clip_min_x),
                .height = static_cast<gpu::uint32>(clip_max_y - clip_min_y)
            });

            // Bind DescriptorSets for image view (font or user texture) and samplers
            //VkDescriptorSet image_view = (VkDescriptorSet)pcmd->GetTexID();
            //if (image_view != last_image_view)
            //    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bd->PipelineLayout, 0, 1, &image_view, 0, nullptr);
            //last_image_view = image_view;

            gpu::draw_indexed(
                commands,
                root.gpu,
                gpu::gpu_range(indices),
                sizeof(ImDrawIdx) == 2 ? gpu::IndexType::uint16 : gpu::IndexType::uint32,
                pcmd->ElemCount,
                1,
                pcmd->IdxOffset + global_idx_offset,
                pcmd->VtxOffset + global_vtx_offset);
        }
        global_vtx_offset += draw_list->VtxBuffer.Size;
        global_idx_offset += draw_list->IdxBuffer.Size;
    }
    platform_io.Renderer_RenderState = nullptr;

    gpu::set_scissor(commands, {
        .x = 0,
        .y = 0,
        .width = fb_width,
        .height = fb_height
    });
    bd->frame_index = (bd->frame_index+1) % bd->init_info.image_count;
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Shutdown() {
    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    gpu::destroy_pso(bd->pso);
    gpu::destroy_gpu_heap(bd->heap);

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}
