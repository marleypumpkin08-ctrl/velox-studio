#version 450

layout(push_constant) uniform DrawState {
    vec2 center;
    vec2 halfExtent;
    vec4 color;
    uint mode;
} state;

layout(set = 0, binding = 0) uniform sampler2D layerImage;

layout(location = 0) in vec2 localPosition;
layout(location = 0) out vec4 outputColor;

void main()
{
    if (state.mode == 4 || state.mode == 5) {
        const vec4 source = texture(layerImage, localPosition);
        const float alpha = source.a * state.color.a;
        if (state.mode == 5) {
            outputColor = vec4(source.rgb * alpha, alpha);
        } else {
            outputColor = vec4(source.rgb, alpha);
        }
    } else if (state.mode == 0) {
        outputColor = state.color;
    } else if (state.mode == 1) {
        const float cell = mod(floor(gl_FragCoord.x / 18.0)
                             + floor(gl_FragCoord.y / 18.0), 2.0);
        const vec3 light = vec3(0.91, 0.92, 0.94);
        const vec3 shade = vec3(0.79, 0.81, 0.84);
        outputColor = vec4(mix(light, shade, cell), 1.0);
    } else if (state.mode == 2 || state.mode == 6) {
        const float radius = length((localPosition - 0.5) * 2.0);
        const float alpha = (1.0 - smoothstep(0.87, 1.0, radius)) * state.color.a;
        outputColor = state.mode == 6
            ? vec4(state.color.rgb * alpha, alpha)
            : vec4(state.color.rgb, alpha);
    } else {
        const vec2 edge = min(localPosition, 1.0 - localPosition);
        const float distanceToEdge = min(edge.x, edge.y);
        const float alpha = (1.0 - smoothstep(0.025, 0.065, distanceToEdge))
            * state.color.a;
        const float dash = mod(floor(gl_FragCoord.x / 8.0)
                             + floor(gl_FragCoord.y / 8.0), 2.0);
        outputColor = vec4(state.color.rgb, alpha * mix(0.45, 1.0, dash));
    }
}
