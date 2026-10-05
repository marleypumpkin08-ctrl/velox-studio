#version 450

layout(push_constant) uniform DrawState {
    vec2 center;
    vec2 halfExtent;
    vec4 color;
    uint mode;
} state;

layout(location = 0) out vec2 localPosition;

void main()
{
    const vec2 vertices[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );
    const vec2 point = vertices[gl_VertexIndex];
    localPosition = point;
    gl_Position = vec4(
        state.center + (point * 2.0 - 1.0) * state.halfExtent,
        0.0,
        1.0
    );
}
