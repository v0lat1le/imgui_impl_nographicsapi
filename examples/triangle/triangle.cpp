#ifndef NDEBUG
#define RGFW_DEBUG
#endif

#include "imgui.h"
#define RGFW_IMPLEMENTATION
#include "RGFW.h"
#define RGFW_IMGUI_IMPLEMENTATION
#include "imgui_impl_rgfw.h"
#include "imgui_impl_nographicsapi.h"

#include "NoGraphicsAPI/NoGraphicsAPI.hpp"

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

void convert_imgui_style_to_linear_HACK() {
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        ImVec4& col = style.Colors[i];
        if (col.w != 0.0f) {
            col.x = srgb_to_linear(col.x*col.w)/col.w;
            col.y = srgb_to_linear(col.y*col.w)/col.w;
            col.z = srgb_to_linear(col.z*col.w)/col.w;
        }
    }
}

#if defined(__APPLE__)
    id get_metal_layer(RGFW_window* window) {
        void* view = RGFW_window_getView_OSX(window);
        return ((id(*)(id, SEL))objc_msgSend)((id)view, sel_getUid("layer"));
    }
    void* init_surface(RGFW_window* window) {
        RGFW_window_setLayer_OSX(window, RGFW_getLayer_OSX());
        return get_metal_layer(window);
    }

    void update_surface(RGFW_window* window) {
        id layer = get_metal_layer(window);
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(layer, sel_registerName("setContentsScale:"), RGFW_window_getMonitor(window)->pixelRatio);

        i32 pixel_width, pixel_height;
        RGFW_window_getSizeInPixels(window, &pixel_width, &pixel_height);
        ((void (*)(id, SEL, CGSize))objc_msgSend)(layer, sel_registerName("setDrawableSize:"), CGSizeMake(pixel_width, pixel_height));
    }
#else
    void* init_surface(RGFW_window* window) {
        return RGFW_window_getHWND(window);
    }

    void update_surface(RGFW_window* /*window*/) {}
#endif

int main() {
    RGFW_init("imgui-nographicsapi", 0);
    RGFW_window* window = RGFW_createWindow("imgui-nographicsapi", 0, 0, 800, 600, RGFW_windowCenter);

    gpu::Device* device = gpu::create_device({ .window = init_surface(window), .swapchain_format = gpu::Format::bgra8_srgb }).device;

    gpu::TimelinePoint latest_completion{ .semaphore = gpu::create_timeline_semaphore(device) };
    gpu::CommandPool* command_pools[] = { gpu::create_command_pool(device), gpu::create_command_pool(device) };
    gpu::GpuHeap heap = gpu::create_gpu_heap(device, 32*1024*1024);

    gpu::TextureHeap texture_heap = gpu::create_texture_heap(device, 4*1024*1024);
    gpu::TextureDescriptorHeap* texture_descriptor_heap = gpu::create_texture_descriptor_heap(device, 4);
    gpu::SamplerDescriptorHeap* sampler_descriptor_heap = gpu::create_sampler_descriptor_heap(device, 2);

    gpu::write_sampler_descriptor(sampler_descriptor_heap, 0, {
        .min_filter = gpu::Filter::nearest,
        .mag_filter = gpu::Filter::nearest,
        .address_u = gpu::AddressMode::clamp_to_edge,
        .address_v = gpu::AddressMode::clamp_to_edge,
    });
    gpu::write_sampler_descriptor(sampler_descriptor_heap, 1, {
        .min_filter = gpu::Filter::linear,
        .mag_filter = gpu::Filter::linear,
        .address_u = gpu::AddressMode::clamp_to_edge,
        .address_v = gpu::AddressMode::clamp_to_edge,
    });

    struct BackendUserData {
        gpu::Device* device;
        gpu::CommandBuffer* commands;
        gpu::GpuCpuRange<byte> frame_heap;
        gpu::TextureHeap texture_heap;
        gpu::uint64 texture_heap_offset = 0;
        gpu::TextureDescriptorHeap* texture_descriptor_heap;
        gpu::uint32 texture_slot = 0;
    };
    BackendUserData user_data {.device = device, .texture_heap = texture_heap, .texture_descriptor_heap = texture_descriptor_heap };

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;

    ImGui::StyleColorsDark();
    convert_imgui_style_to_linear_HACK();  // TODO: do this in the shader?

    ImGui_ImplRgfw_InitForOther(window, true);
    ImGui_ImplNoGraphicsAPI_Init({
        .device = device,
        .nearest_sampler_slot = 0,
        .linear_sampler_slot = 1,
        .user_data = &user_data,
        .allocate_frame_memory = [](void* user_data, gpu::uint64 size, gpu::uint64 alignment) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            gpu::uint64 align_offset = reinterpret_cast<gpu::uint64>(backend_data->frame_heap.gpu) & (alignment - 1);
            if (align_offset > 0) {
                align_offset = alignment - align_offset;
            }
            IM_ASSERT(backend_data->frame_heap.size >= size + align_offset);
            gpu::GpuCpuRange<byte> result = {
                .cpu = backend_data->frame_heap.cpu + align_offset,
                .gpu = backend_data->frame_heap.gpu + align_offset,
                .size = size,
            };
            backend_data->frame_heap.cpu += size + align_offset;
            backend_data->frame_heap.gpu += size + align_offset;
            backend_data->frame_heap.size -= size + align_offset;
            return result;
        },
        .create_texture = [](void* user_data, const gpu::TextureDesc& desc) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            const gpu::SizeAlign texture_size = gpu::get_texture_size_align(backend_data->device, desc);
            gpu::Texture* texture = gpu::create_texture(backend_data->device, desc, backend_data->texture_heap, backend_data->texture_heap_offset);
            backend_data->texture_heap_offset = (backend_data->texture_heap_offset + texture_size.align - 1) & ~(texture_size.align - 1);
            backend_data->texture_heap_offset += texture_size.size;
            const gpu::uint32 texture_slot = backend_data->texture_slot++;
            gpu::write_texture_descriptor(backend_data->texture_descriptor_heap, texture_slot, texture, gpu::TextureDescriptorType::sampled);
            gpu::initialize_texture(backend_data->commands, texture);  // can be done in update_texture
            return ImGui_ImplNoGraphicsAPI_Texture{ .slot = texture_slot, .texture_data = texture };
        },
        .update_texture = [](void* user_data, const ImGui_ImplNoGraphicsAPI_Texture& texture, const gpu::TextureCopyDesc& copy_desc, gpu::Span<gpu::byte> data) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            ImGui_ImplNoGraphicsAPI_UpdateTexture(backend_data->commands, static_cast<gpu::Texture*>(texture.texture_data), copy_desc, data);
        },
        .destroy_texture = [](void* /*user_data*/, const ImGui_ImplNoGraphicsAPI_Texture& texture) {
            gpu::destroy_texture(static_cast<gpu::Texture*>(texture.texture_data));
        }
    });

    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while (RGFW_window_shouldClose(window) == RGFW_FALSE) {
        RGFW_pollEvents();
        update_surface(window);
        ImGui_ImplRgfw_NewFrame();
        ImGui_ImplNoGraphicsAPI_NewFrame();
        ImGui::NewFrame();

        if (show_demo_window) {
            ImGui::ShowDemoWindow(&show_demo_window);
        }
        {
            static float f = 0.0f;
            static int counter = 0;
            ImGui::Begin("Hello, world!");
                ImGui::Text("This is some useful text.");
                ImGui::Checkbox("Demo Window", &show_demo_window);
                ImGui::Checkbox("Another Window", &show_another_window);

                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
                ImGui::ColorEdit3("clear color", &clear_color.x);

                if (ImGui::Button("Button")) {
                    counter++;
                }
                ImGui::SameLine();
                ImGui::Text("counter = %d", counter);

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        if (show_another_window) {
            ImGui::Begin("Another Window", &show_another_window);
                ImGui::Text("Hello from another window!");
                if (ImGui::Button("Close Me")) {
                    show_another_window = false;
                }
            ImGui::End();
        }

        ImGui::Render();

        if (latest_completion.value >= 2) {
            gpu::wait_timeline({ .semaphore = latest_completion.semaphore, .value = latest_completion.value - 1 });
        }
        gpu::uint64 frame_index = latest_completion.value % 2;
        gpu::CommandPool* command_pool = command_pools[frame_index];
        gpu::reset_command_pool(command_pool);
        gpu::CommandBuffer* commands = gpu::begin_commands(command_pool);
        const gpu::SwapchainFrame frame = gpu::acquire(commands);
        if (!frame.render_view) {
            gpu::end_commands(commands);
            continue;
        }

        user_data.commands = commands;
        user_data.frame_heap = {
            .cpu = heap.range.cpu + frame_index*heap.range.size/2,
            .gpu = heap.range.gpu + frame_index*heap.range.size/2,
            .size = heap.range.size/2,
        };
        ImGui_ImplNoGraphicsAPI_UpdateTextures(ImGui::GetDrawData());

        gpu::set_texture_descriptor_heap(commands, texture_descriptor_heap);
        gpu::set_sampler_descriptor_heap(commands, sampler_descriptor_heap);
        gpu::begin_render_pass(commands, {
            .colors = { {.render_view = frame.render_view, .load = gpu::LoadOp::clear } },
        });
        ImGui_ImplNoGraphicsAPI_RenderDrawData(commands, ImGui::GetDrawData());

        gpu::end_render_pass(commands);
        gpu::end_commands(commands);
        latest_completion.value++;
        gpu::submit_and_present(device, { .commands = {commands}, .completion = latest_completion });
    }

    gpu::wait_idle(device);

    ImGui_ImplNoGraphicsAPI_Shutdown();
    ImGui_ImplRgfw_Shutdown();
    ImGui::DestroyContext();

    gpu::destroy_gpu_heap(heap);
    gpu::destroy_texture_heap(texture_heap);
    gpu::destroy_texture_descriptor_heap(texture_descriptor_heap);
    gpu::destroy_sampler_descriptor_heap(sampler_descriptor_heap);
    gpu::destroy_command_pool(command_pools[1]);
    gpu::destroy_command_pool(command_pools[0]);
    gpu::destroy_timeline_semaphore(latest_completion.semaphore);
    gpu::destroy_device(device);
    RGFW_window_close(window);
    return 0;
}
