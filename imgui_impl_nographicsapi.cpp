#include "imgui_impl_nographicsapi.h"


namespace {
    struct ImGuiRoot {
        ImDrawVert* vertices;
        gpu::uint32 texture_id;
        gpu::uint32 padding;
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
        gpu::TextureHeap texture_heap;
        gpu::TextureDescriptorHeap* texture_descriptor_heap;
        gpu::SamplerDescriptorHeap* sampler_descriptor_heap;
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

    void ImGui_ImplNoGraphicsAPI_UpdateTexture(ImTextureData* tex, gpu::CommandBuffer* commands, gpu::GpuCpuRange<byte>& frame_heap) {
        if (tex->Status == ImTextureStatus_OK) {
            return;
        }
        ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();

        if (tex->Status == ImTextureStatus_WantCreate) {
            gpu::TextureDesc desc{
                .extent = { .x = static_cast<gpu::uint32>(tex->Width), .y = static_cast<gpu::uint32>(tex->Height), .z = 1 },
                    .format = gpu::Format::rgba8_unorm,
                    .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination,
            };
            gpu::Texture* texture = gpu::create_texture(bd->init_info.device, desc, bd->texture_heap, 0);

            gpu::initialize_texture(commands, texture);
            gpu::write_texture_descriptor(bd->texture_descriptor_heap, 0, texture, gpu::TextureDescriptorType::sampled);
            gpu::write_sampler_descriptor(bd->sampler_descriptor_heap, 0, {
                .min_filter = gpu::Filter::nearest,
                .mag_filter = gpu::Filter::nearest,
                .address_u = gpu::AddressMode::clamp_to_edge,
                .address_v = gpu::AddressMode::clamp_to_edge,
            });
            tex->SetTexID(1);
            tex->BackendUserData = texture;
        }

        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
            gpu::Texture* texture = static_cast<gpu::Texture*>(tex->BackendUserData);

            const gpu::uint32 upload_x = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.x;
            const gpu::uint32 upload_y = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.y;
            const gpu::uint32 upload_w = (tex->Status == ImTextureStatus_WantCreate) ? tex->Width : tex->UpdateRect.w;
            const gpu::uint32 upload_h = (tex->Status == ImTextureStatus_WantCreate) ? tex->Height : tex->UpdateRect.h;

            for (int y = 0; y < upload_h; y++) {
                memcpy(frame_heap.cpu + y*upload_w*tex->BytesPerPixel, tex->GetPixelsAt(upload_x, upload_y + y), upload_w*tex->BytesPerPixel);
            }

            gpu::TextureCopyDesc copy {
                .offset = { .x=upload_x, .y=upload_y},
                .extent = {.x = upload_w, .y = upload_h }
            };
            gpu::copy_memory_to_texture(commands, gpu::gpu_range(frame_heap), texture, copy);
            tex->SetStatus(ImTextureStatus_OK);
        }

        if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames >= (int)bd->init_info.image_count) {
            //  TODO: Destroy Texture
        }
    }

    template<typename T>
    gpu::GpuCpuRange<T> allocate(gpu::GpuCpuRange<gpu::byte>& heap, gpu::uint64 count=1, gpu::uint64 alignment=1) {
        gpu::uint64 size = sizeof(T)*count;
        size_t align_delta = reinterpret_cast<gpu::uint64>(heap.gpu) & (alignment - 1);
        if (align_delta > 0) {
            align_delta = alignment - align_delta;
        }
        if (size+align_delta > heap.size) {
            return { nullptr, nullptr, 0 };
        }
        gpu::GpuCpuRange<T> result {
            .cpu = reinterpret_cast<T*>(heap.cpu+align_delta),
            .gpu = reinterpret_cast<T*>(heap.gpu+align_delta),
            .size = size
        };
        heap.cpu += size+align_delta;
        heap.gpu += size+align_delta;
        heap.size -= size+align_delta;
        return result;
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
    const gpu::uint64 frame_heap_size = 256*1024;
    bd->heap = gpu::create_gpu_heap(init_info.device, frame_heap_size*init_info.image_count);
    bd->texture_heap = gpu::create_texture_heap(init_info.device, 1024*1024);
    bd->texture_descriptor_heap = gpu::create_texture_descriptor_heap(init_info.device, 4);
    bd->sampler_descriptor_heap = gpu::create_sampler_descriptor_heap(init_info.device, 4);
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

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_UpdateTextures(gpu::CommandBuffer* commands, ImDrawData* draw_data) {
    if (draw_data->Textures == nullptr) {
        return;
    }

    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    ImGui_ImplNoGraphicsAPI_FrameData& frame_data = bd->frame_data[bd->frame_index];
    gpu::GpuCpuRange<gpu::byte> frame_heap = frame_data.heap;

    for (ImTextureData* tex: *draw_data->Textures) {
        ImGui_ImplNoGraphicsAPI_UpdateTexture(tex, commands, frame_heap);
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
    ImGui_ImplNoGraphicsAPI_FrameData& frame_data = bd->frame_data[bd->frame_index];
    gpu::GpuCpuRange<gpu::byte> frame_heap = frame_data.heap;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplNoGraphicsAPI_RenderState render_state { .commands = commands, .pso = bd->pso };
    platform_io.Renderer_RenderState = &render_state;  // can be used by callbacks

    gpu::bind_pso(commands, bd->pso);
    gpu::set_depth_stencil(commands, {});
    gpu::set_viewport(commands, {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(fb_width),
        .height = static_cast<float>(fb_height)
    });
    gpu::set_texture_descriptor_heap(commands, bd->texture_descriptor_heap);
    gpu::set_sampler_descriptor_heap(commands, bd->sampler_descriptor_heap);

    for (const ImDrawList* draw_list: draw_data->CmdLists) {
        gpu::GpuCpuRange<ImDrawVert> vertices = allocate<ImDrawVert>(frame_heap, draw_list->VtxBuffer.Size, 4);
        gpu::GpuCpuRange<ImDrawIdx> indices = allocate<ImDrawIdx>(frame_heap, draw_list->IdxBuffer.Size, 4);
        memcpy(vertices.cpu, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(indices.cpu, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImGui_ImplNoGraphicsAPI_DrawCallback_ResetRenderState) {
                    gpu::bind_pso(commands, bd->pso);
                    gpu::set_depth_stencil(commands, {});
                    gpu::set_viewport(commands, {
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(fb_width),
                        .height = static_cast<float>(fb_height)
                    });
                    gpu::set_texture_descriptor_heap(commands, bd->texture_descriptor_heap);
                    gpu::set_sampler_descriptor_heap(commands, bd->sampler_descriptor_heap);
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

            gpu::GpuCpuRange<ImGuiRoot> root = allocate<ImGuiRoot>(frame_heap, 1, 16);
            root.cpu->vertices = vertices.gpu;
            root.cpu->scale = { 2.0f / draw_data->DisplaySize.x, 2.0f / draw_data->DisplaySize.y };
            root.cpu->translate = { -1.0f - draw_data->DisplayPos.x * root.cpu->scale.x, -1.0f - draw_data->DisplayPos.y * root.cpu->scale.y };
            root.cpu->texture_id = pcmd->GetTexID()-1;
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
    bd->frame_index = (bd->frame_index+1) % bd->init_info.image_count;
}

IMGUI_IMPL_API void ImGui_ImplNoGraphicsAPI_Shutdown() {
    ImGui_ImplNoGraphicsAPI_Data* bd = ImGui_ImplNoGraphicsAPI_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    gpu::destroy_pso(bd->pso);
    gpu::destroy_gpu_heap(bd->heap);
    gpu::destroy_texture_heap(bd->texture_heap);
    gpu::destroy_texture_descriptor_heap(bd->texture_descriptor_heap);
    gpu::destroy_sampler_descriptor_heap(bd->sampler_descriptor_heap);

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}
