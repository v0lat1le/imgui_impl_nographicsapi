// The texture map is adapted from Vulkan-Tools' vkcube sample. Copyright
// notices and the Apache-2.0 license are in NOTICE.md and LICENSE-Apache-2.0.txt.

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

#include "cube_shared.h"

#include <NoGraphicsAPIUtility/bump_allocator.hpp>
#include <NoGraphicsAPIUtility/math.hpp>
#include <NoGraphicsAPIUtility/texture_allocator.hpp>
#include <NoGraphicsAPIUtility/upload_queue.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace gpu;

namespace {

	constexpr uint32 width = 500;
	constexpr uint32 height = 500;
	constexpr uint32 texture_width = 256;
	constexpr uint32 texture_height = 256;
	constexpr size_t texture_byte_count = size_t(texture_width) * texture_height * 4;
	constexpr uint64 data_heap_size = 8 * 1024 * 1024;
	constexpr uint64 texture_heap_size = 256 * 1024 * 1024;
	constexpr float radians_per_frame = 4.0f * math::pi / 180.0f;

	constexpr CubeVertex cube_vertices[] = {
		{ .position = { .x = -1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },
		{ .position = { .x = -1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },
		{ .position = { .x = -1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = -1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },

		{ .position = { .x = -1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },
		{ .position = { .x = -1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },

		{ .position = { .x = -1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },
		{ .position = { .x = -1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },

		{ .position = { .x = -1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = -1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },
		{ .position = { .x = 1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },

		{ .position = { .x = 1.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = -1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },

		{ .position = { .x = -1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 1.0f } },
		{ .position = { .x = 1.0f, .y = -1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 1.0f } },
		{ .position = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 1.0f, .y = 0.0f } },
		{ .position = { .x = -1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, .uv = { .x = 0.0f, .y = 0.0f } },
	};

	constexpr uint16 cube_indices[] = {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4,
		8, 9, 10, 10, 11, 8,
		12, 13, 14, 14, 15, 12,
		16, 17, 18, 18, 19, 16,
		20, 21, 22, 22, 23, 20,
	};

	constexpr size_t cube_vertex_count = sizeof(cube_vertices) / sizeof(cube_vertices[0]);
	constexpr uint32 cube_index_count = uint32(sizeof(cube_indices) / sizeof(cube_indices[0]));

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
} // namespace

int main() {
    RGFW_init("imgui-nographicsapi", 0);
    RGFW_window* window = RGFW_createWindow("imgui-nographicsapi", 0, 0, 800, 600, RGFW_windowCenter);

    gpu::Device* device = gpu::create_device({ .window = init_surface(window), .swapchain_format = gpu::Format::bgra8_srgb }).device;
    if (!window || !device)
    {
        destroy_device(device);
        RGFW_window_close(window);
        return 1;
    }

	const DeviceCaps& caps = get_device_caps(device);
    printf("Using %s\n", caps.device_name);

	// Shaders
 //   const Span<byte> vertex_code = read_shader(NOGRAPHICSAPI_CUBE_VERTEX_SHADER_PATH);
 //   const Span<byte> fragment_code = read_shader(NOGRAPHICSAPI_CUBE_FRAGMENT_SHADER_PATH);
	//PSO* cube_pso = create_graphics_pso(device, {
 //       .vertex = {.code = {vertex_code.data, vertex_code.size}, .entry_point = "vertexMain"},
 //       .fragment = {.code = {fragment_code.data, fragment_code.size}, .entry_point = "fragmentMain"},
 //       .color_targets = {{.format = Format::bgra8_srgb}},
 //       .depth_format = Format::d32_float,
 //       .rasterization = { .cull = CullMode::clockwise },
 //   });
 //   free(fragment_code.data);
 //   free(vertex_code.data);
 //   if (!cube_pso)
 //   {
 //       destroy_device(device);
 //       RGFW_window_close(window);
 //       return 1;
 //   }

    // GPU resources
    GpuHeap data_heap = create_gpu_heap(device, data_heap_size);
    BumpAllocator data_allocator(data_heap.range);
    const GpuCpuRange<byte> frame_ranges[2]{data_allocator.allocate(2*1024*1024), data_allocator.allocate(2*1024*1024)};
    const GpuCpuRange<CubeVertex> vertex_allocation = data_allocator.allocate<CubeVertex>(cube_vertex_count);
    const GpuCpuRange<uint16> index_allocation = data_allocator.allocate<uint16>(cube_index_count);
    byte* texture_bytes = new byte[texture_byte_count];
    memcpy(vertex_allocation.cpu, cube_vertices, sizeof(cube_vertices));
	memcpy(index_allocation.cpu, cube_indices, sizeof(cube_indices));
    //read_binary_file(NOGRAPHICSAPI_CUBE_TEXTURE_PATH, Span<byte>(texture_bytes, texture_byte_count));

	UploadQueue uploads(device, texture_byte_count);
	TextureDescriptorHeap* texture_descriptor_heap = create_texture_descriptor_heap(device, 4);
	SamplerDescriptorHeap* sampler_descriptor_heap = create_sampler_descriptor_heap(device, 2);
	TextureHeap texture_heap = create_texture_heap(device, texture_heap_size);
    TextureAllocator texture_allocator(device, texture_heap, 16);

	TimelinePoint latest_completion{.semaphore = create_timeline_semaphore(device)};
	CommandPool* command_pools[] = {create_command_pool(device), create_command_pool(device)};
	CommandBuffer* texture_commands = begin_commands(command_pools[0]);

	// Textures
	PlacedTexture texture = texture_allocator.allocate({
		.extent = {.x = texture_width, .y = texture_height, .z = 1},
		.format = Format::rgba8_srgb,
		.usage = TextureUsage::sampled | TextureUsage::transfer_destination,
	});

	initialize_texture(texture_commands, texture.texture);
	write_texture_descriptor(texture_descriptor_heap, 0, texture.texture, TextureDescriptorType::sampled);
	write_sampler_descriptor(sampler_descriptor_heap, 0, {
		.min_filter = Filter::nearest,
		.mag_filter = Filter::nearest,
		.address_u = AddressMode::clamp_to_edge,
		.address_v = AddressMode::clamp_to_edge,
	});
    write_sampler_descriptor(sampler_descriptor_heap, 1, {
        .min_filter = Filter::linear,
        .mag_filter = Filter::linear,
        .address_u = AddressMode::clamp_to_edge,
        .address_v = AddressMode::clamp_to_edge,
    });
	end_commands(texture_commands);
	latest_completion.value++;
	submit(device, {.commands = {texture_commands}, .completion = latest_completion});
    uploads.upload_texture(texture.texture, {texture_bytes, texture_byte_count});
    delete[] texture_bytes;
    uploads.wait();
    //uploads.destroy();

	PlacedTexture depth{};
	RenderView* depth_render_view = nullptr;
	uint32x2 depth_extent{};
	uint64 frame_index = 0;
    const float4x4 view = math::look_at_rh({.x = 0.0f, .y = 3.0f, .z = 5.0f}, {.x = 0.0f, .y = 0.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f});

    struct BackendUserData {
        gpu::Device* device;
        gpu::TextureAllocator* texture_allocator;
        gpu::uint32 texture_slot = 1;
        gpu::TextureDescriptorHeap* texture_descriptor_heap;
        gpu::CommandBuffer* commands;
        gpu::BumpAllocator* frame_data;
    };
    BackendUserData user_data{ .device = device, .texture_allocator = &texture_allocator, .texture_descriptor_heap = texture_descriptor_heap };

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
            IM_ASSERT(backend_data->frame_data->alignment >= alignment);
            return backend_data->frame_data->allocate<gpu::byte>(size);
        },
        .create_texture = [](void* user_data, const gpu::TextureDesc& desc) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            PlacedTexture texture = backend_data->texture_allocator->allocate(desc);
            const gpu::uint32 texture_slot = backend_data->texture_slot++;
            gpu::write_texture_descriptor(backend_data->texture_descriptor_heap, texture_slot, texture.texture, gpu::TextureDescriptorType::sampled);
            gpu::initialize_texture(backend_data->commands, texture.texture); // TODO: separate commands?
            return ImGui_ImplNoGraphicsAPI_Texture{.slot = texture_slot, .texture_data = texture.texture };
        },
        .update_texture = [](void* user_data, const ImGui_ImplNoGraphicsAPI_Texture& texture, const gpu::TextureCopyDesc& copy_desc, gpu::Span<gpu::byte> data) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            ImGui_ImplNoGraphicsAPI_UpdateTexture(backend_data->commands, static_cast<gpu::Texture*>(texture.texture_data), copy_desc, data);
        },
        .destroy_texture = [](void* user_data, const ImGui_ImplNoGraphicsAPI_Texture& texture) {
            BackendUserData* backend_data = static_cast<BackendUserData*>(user_data);
            PlacedTexture tex;
            backend_data->texture_allocator->free(tex);
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

        if (latest_completion.value >= 2)
            wait_timeline({.semaphore = latest_completion.semaphore, .value = latest_completion.value - 1});
        BumpAllocator frame_data(frame_ranges[latest_completion.value % 2]);
        CommandPool* command_pool = command_pools[latest_completion.value % 2];
        reset_command_pool(command_pool);
        CommandBuffer* commands = begin_commands(command_pool);
        const SwapchainFrame frame = acquire(commands);
        if (!frame.render_view)
            continue;

        // Window resize?
        if (frame.extent.x != depth_extent.x || frame.extent.y != depth_extent.y)
		{
			if (depth.texture)
				wait_timeline(latest_completion);
			destroy_render_view(depth_render_view);
			texture_allocator.free(depth);
			depth = texture_allocator.allocate({
				.extent = {.x = frame.extent.x, .y = frame.extent.y, .z = 1},
				.format = Format::d32_float,
				.usage = TextureUsage::depth_stencil_attachment,
			});
			initialize_texture(commands, depth.texture);
			depth_render_view = create_render_view(depth.texture);
			depth_extent = frame.extent;
		}

        user_data.commands = commands;
        user_data.frame_data = &frame_data;
        ImGui_ImplNoGraphicsAPI_UpdateTextures(ImGui::GetDrawData());
        uploads.wait();

		// Render
        set_texture_descriptor_heap(commands, texture_descriptor_heap);
        set_sampler_descriptor_heap(commands, sampler_descriptor_heap);

		barrier(commands, 
			Stage::depth_stencil_tests, Access::depth_stencil_write, 
			Stage::depth_stencil_tests, Access::depth_stencil_write);

		begin_render_pass(commands, {
			.colors = { {
				.render_view = frame.render_view,
				.load = LoadOp::clear,
				.clear = { .x = 0.2f, .y = 0.2f, .z = 0.2f, .w = 0.2f },
			}},
			.depth = {
				.render_view = depth_render_view,
				.load = LoadOp::clear,
				.store = StoreOp::discard,
			},
		});

		set_depth_stencil(commands, {.depth_test = true, .depth_write = true});
		//bind_pso(commands, cube_pso);

        float4x4 projection = math::perspective_rh_zo(45.0f * math::pi / 180.0f,
                                                     float(frame.extent.x) / float(frame.extent.y), 0.1f, 100.0f);
        projection.rows[1].y = -projection.rows[1].y;
		const GpuCpuRange<CubeRootArguments> root = frame_data.allocate<CubeRootArguments>();
        *root.cpu = {
            .vertices = vertex_allocation.gpu,
            .transform = projection * view * math::rotation_y(radians_per_frame * float(frame_index++)),
        };

		//draw_indexed(commands, root.gpu, gpu_range(index_allocation), IndexType::uint16, cube_index_count);
        end_render_pass(commands);

        gpu::begin_render_pass(commands, {
            .colors = { {.render_view = frame.render_view, .load = gpu::LoadOp::clear } },
        });
        ImGui_ImplNoGraphicsAPI_RenderDrawData(commands, ImGui::GetDrawData());

		end_render_pass(commands);

        // Submit
        end_commands(commands);
        latest_completion.value++;
		submit_and_present(device, {.commands = {commands}, .completion = latest_completion});
	}

    wait_idle(device);

	// Cleanup
    ImGui_ImplNoGraphicsAPI_Shutdown();
    ImGui_ImplRgfw_Shutdown();
    ImGui::DestroyContext();
	destroy_command_pool(command_pools[1]);
	destroy_command_pool(command_pools[0]);
	destroy_timeline_semaphore(latest_completion.semaphore);
    //destroy_pso(cube_pso);
	destroy_render_view(depth_render_view);
	texture_allocator.free(depth);
	texture_allocator.free(texture);
    destroy_texture_heap(texture_heap);
	destroy_sampler_descriptor_heap(sampler_descriptor_heap);
	destroy_texture_descriptor_heap(texture_descriptor_heap);
    destroy_gpu_heap(data_heap);

	destroy_device(device);
    RGFW_window_close(window);
	return 0;
}
