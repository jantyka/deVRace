##ifdef GL_ES
#version 300 es
precision highp float;
precision highp int;
##endif
##ifdef GL_CORE
#version 140
#extension GL_ARB_explicit_attrib_location:require
##endif

// Post effects: one triangle covering the target, texture coordinates 0..1 across it

out vec2 v_uv;

void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
