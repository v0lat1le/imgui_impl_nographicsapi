#include <NoGraphicsAPI/shader.slang>

struct ImDrawVert {
    float2 pos;
    float2 uv;
    uint col;
}

struct ImGuiRoot {
	ImDrawVert* vertices;
    float2 scale;
    float2 translate;
	uint texture_id;
};

struct VertexOutput {
    float4 color: COLOR0;
    float4 position: SV_Position;
    float2 uv: TEXCOORD0;
};

float4 unpack_color(uint packed) {
	float r = (packed & 0xFF) / 255.0f;
    float g = ((packed >> 8) & 0xFF) / 255.0f;
    float b = ((packed >> 16) & 0xFF) / 255.0f;
    float a = ((packed >> 24) & 0xFF) / 255.0f;
	return float4(r,g,b,a);
}

GPU_ROOT(ImGuiRoot, root);

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VulkanVertexID) {
    VertexOutput output;
    ImDrawVert vertex = root.vertices[vertex_id];
	output.color = unpack_color(vertex.col);
	output.position = float4(vertex.pos*root.scale+root.translate, 0, 1);
	output.uv = vertex.uv;
    return output;
}

struct FragmentInput {
    float4 color: COLOR0;
    float2 uv: TEXCOORD0;
};

[shader("fragment")]
float4 fragmentMain(FragmentInput input): SV_Target0 {
    Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(root.texture_id);
    SamplerState sampler = gpu_sampler(root.texture_id);
	return input.color*texture.Sample(sampler, input.uv);
}
