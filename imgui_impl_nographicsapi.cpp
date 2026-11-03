#include "imgui_impl_nographicsapi.h"


namespace {
    struct ImGuiRoot {
        ImDrawVert* vertices;
        gpu::uint32 texture_slot;
        gpu::uint32 sampler_slot;
        ImVec2 scale;
        ImVec2 translate;
    };

    struct ImGui_ImplNoGraphicsAPI_Data {
        ImGui_ImplNoGraphicsAPI_InitInfo init_info;
        gpu::PSO* pso;
    };

    ImGui_ImplNoGraphicsAPI_Data* ImGui_ImplNoGraphicsAPI_GetBackendData() {
        return ImGui::GetCurrentContext() ? static_cast<ImGui_ImplNoGraphicsAPI_Data*>(ImGui::GetIO().BackendRendererUserData) : nullptr;
    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState(const ImDrawList*, const ImDrawCmd*) {
    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerLinear(const ImDrawList*, const ImDrawCmd*) {
        ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
        ImGui_ImplNoGraphicsAPI_RenderState* render_state = static_cast<ImGui_ImplNoGraphicsAPI_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
        render_state->sampler_slot = bd->init_info.linear_sampler_slot;
    }

    void ImGui_ImplNoGraphicsAPI_DrawCallback_SetSamplerNearest(const ImDrawList*, const ImDrawCmd*) {
        ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
        ImGui_ImplNoGraphicsAPI_RenderState* render_state = static_cast<ImGui_ImplNoGraphicsAPI_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
        render_state->sampler_slot = bd->init_info.nearest_sampler_slot;
    }

    void ImGui_ImplNoGraphicsAPI_UpdateTexture(ImTextureData* tex, gpu::CommandBuffer* commands) {
        if (tex->Status == ImTextureStatus_OK) {
            return;
        }
        ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();

        if (tex->Status == ImTextureStatus_WantCreate) {
            gpu::TextureDesc desc{
                .extent = { .x = static_cast<gpu::uint32>(tex->Width), .y = static_cast<gpu::uint32>(tex->Height), .z = 1 },
                    .format = tex->Format == ImTextureFormat_RGBA32 ? gpu::Format::rgba8_unorm : gpu::Format::r8_unorm,
                    .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination,
            };
            ImGui_ImplNoGraphicsAPI_Texture backend_texture = bd->init_info.create_texture(bd->init_info.user_data, desc);
            gpu::initialize_texture(commands, backend_texture.texture);

            tex->SetTexID(backend_texture.slot+1);
            tex->BackendUserData = backend_texture.texture;
        }

        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
            gpu::Texture* texture = static_cast<gpu::Texture*>(tex->BackendUserData);

            const gpu::uint32 upload_x = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.x;
            const gpu::uint32 upload_y = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.y;
            const gpu::uint32 upload_w = (tex->Status == ImTextureStatus_WantCreate) ? tex->Width : tex->UpdateRect.w;
            const gpu::uint32 upload_h = (tex->Status == ImTextureStatus_WantCreate) ? tex->Height : tex->UpdateRect.h;

            gpu::uint64 texture_size = upload_h*upload_w*tex->BytesPerPixel;
            gpu::GpuCpuRange<byte> staging_memory = bd->init_info.allocate_frame_memory(bd->init_info.user_data, texture_size, 4);
            for (gpu::uint32 y = 0; y < upload_h; y++) {
                memcpy(staging_memory.cpu + y*upload_w*tex->BytesPerPixel, tex->GetPixelsAt(upload_x, upload_y + y), upload_w*tex->BytesPerPixel);
            }

            gpu::TextureCopyDesc copy {
                .offset = { .x=upload_x, .y=upload_y },
                .extent = { .x=upload_w, .y=upload_h }
            };
            gpu::copy_memory_to_texture(commands, gpu::gpu_range(staging_memory), texture, copy);
            tex->SetStatus(ImTextureStatus_OK);
        }

        if (tex->Status == ImTextureStatus_WantDestroy) {
            bd->init_info.destroy_texture(bd->init_info.user_data, {
                static_cast<gpu::Texture*>(tex->BackendUserData),
                static_cast<gpu::uint32>(tex->GetTexID()-1)
            });
            tex->SetTexID(ImTextureID_Invalid);
            tex->BackendUserData = nullptr;
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    template<typename T>
    gpu::GpuCpuRange<T> convert_range(gpu::GpuCpuRange<gpu::byte> heap) {
        IM_ASSERT(heap.size % sizeof(T) == 0);
        return { reinterpret_cast<T*>(heap.cpu), reinterpret_cast<T*>(heap.gpu), heap.size };
    }

    gpu::uint64 align_up(gpu::uint64 value, gpu::uint64 alignment) {
        gpu::uint64 align_delta = value & (alignment - 1);
        if (align_delta > 0) {
            value += alignment - align_delta;
        }
        return value;
    }

    template<typename T>
    gpu::GpuCpuRange<T> allocate(ImGui_ImplNoGraphicsAPI_Data* bd, gpu::uint64 count=1, gpu::uint64 alignment=1, gpu::uint64 size_alignment=1) {
        gpu::uint64 size = align_up(sizeof(T)*count, size_alignment);
        return convert_range<T>(bd->init_info.allocate_frame_memory(bd->init_info.user_data, size, alignment));
    }

    template<typename T>
    T min(T lhs, T rhs) {
        return lhs < rhs ? lhs : rhs;
    }

    template<typename T>
    T max(T lhs, T rhs) {
        return lhs > rhs ? lhs : rhs;
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

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_UpdateTextures(gpu::CommandBuffer* commands, ImDrawData* draw_data) {
    if (draw_data->Textures == nullptr) {
        return;
    }
    for (ImTextureData* tex: *draw_data->Textures) {
        ImGui_ImplNoGraphicsAPI_UpdateTexture(tex, commands);
    }
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_RenderDrawData(gpu::CommandBuffer* commands, ImDrawData* draw_data) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    gpu::uint32 fb_width = static_cast<gpu::uint32>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x + 0.5f);
    gpu::uint32 fb_height = static_cast<gpu::uint32>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y + 0.5f);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }

    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplNoGraphicsAPI_RenderState render_state { .commands = commands, .pso = bd->pso, .sampler_slot = bd->init_info.linear_sampler_slot };
    platform_io.Renderer_RenderState = &render_state;  // can be used by callbacks

    gpu::bind_pso(commands, render_state.pso);
    gpu::set_depth_stencil(commands, {});
    gpu::set_viewport(commands, {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(fb_width),
        .height = static_cast<float>(fb_height)
    });

    for (const ImDrawList* draw_list: draw_data->CmdLists) {
        gpu::GpuCpuRange<ImDrawVert> vertices = allocate<ImDrawVert>(bd, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        gpu::GpuCpuRange<ImDrawIdx> indices = allocate<ImDrawIdx>(bd, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx), 4, 4);
        memcpy(vertices.cpu, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(indices.cpu, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState) {
                    gpu::bind_pso(commands, render_state.pso);
                    gpu::set_depth_stencil(commands, {});
                    gpu::set_viewport(commands, {
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(fb_width),
                        .height = static_cast<float>(fb_height)
                    });
                } else {
                    pcmd->UserCallback(draw_list, pcmd);
                }
                continue;
            }

            // Project scissor/clipping rectangles into framebuffer space, clamp to viewport
            gpu::int32 clip_min_x = max<gpu::int32>(0, (pcmd->ClipRect.x-draw_data->DisplayPos.x)*draw_data->FramebufferScale.x);
            gpu::int32 clip_min_y = max<gpu::int32>(0, (pcmd->ClipRect.y-draw_data->DisplayPos.y)*draw_data->FramebufferScale.y);
            gpu::int32 clip_max_x = min<gpu::int32>(fb_width, (pcmd->ClipRect.z-draw_data->DisplayPos.x)*draw_data->FramebufferScale.x);
            gpu::int32 clip_max_y = min<gpu::int32>(fb_height, (pcmd->ClipRect.w-draw_data->DisplayPos.y)*draw_data->FramebufferScale.y);
            if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y) {
                continue;
            }

            gpu::set_scissor(commands, {
                .x = clip_min_x,
                .y = clip_min_y,
                .width = static_cast<gpu::uint32>(clip_max_x - clip_min_x),
                .height = static_cast<gpu::uint32>(clip_max_y - clip_min_y)
            });

            gpu::GpuCpuRange<ImGuiRoot> root = allocate<ImGuiRoot>(bd, 1, 16);
            root.cpu->vertices = vertices.gpu;
            root.cpu->scale = { 2.0f / draw_data->DisplaySize.x, 2.0f / draw_data->DisplaySize.y };
            root.cpu->translate = { -1.0f - draw_data->DisplayPos.x * root.cpu->scale.x, -1.0f - draw_data->DisplayPos.y * root.cpu->scale.y };
            root.cpu->texture_slot = pcmd->GetTexID()-1;
            root.cpu->sampler_slot = render_state.sampler_slot;
            gpu::draw_indexed(
                commands,
                root.gpu,
                gpu::gpu_range(indices),
                sizeof(ImDrawIdx) == 2 ? gpu::IndexType::uint16 : gpu::IndexType::uint32,
                pcmd->ElemCount,
                1,
                pcmd->IdxOffset,
                pcmd->VtxOffset);
        }
    }
    platform_io.Renderer_RenderState = nullptr;

    gpu::set_scissor(commands, {
        .x = 0,
        .y = 0,
        .width = fb_width,
        .height = fb_height
    });
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Shutdown() {
    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    gpu::destroy_pso(bd->pso);
    for (ImTextureData* tex: platform_io.Textures) {
        bd->init_info.destroy_texture(bd->init_info.user_data, {
            static_cast<gpu::Texture*>(tex->BackendUserData),
            static_cast<gpu::uint32>(tex->GetTexID()-1)
        });
    }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}
