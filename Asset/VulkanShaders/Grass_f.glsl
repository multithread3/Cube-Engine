layout(set = 0, binding = 1) uniform sampler2D DiffuseMap;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 v_texcoord;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec3 v_tangent;



layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_position;
layout(location = 2) out vec4 out_normal;
layout(location = 3) out vec4 out_mix;

void main() 
{
	vec4 col = texture(DiffuseMap, v_texcoord);
	if(col.a <0.5)
	{
		discard;
	}
	col.rgb = pow(col.rgb, vec3(2.2));
	out_albedo = col;
	out_position = vec4(0, 0, 0, 1.0);
	out_normal = vec4(v_normal, 1);
	out_mix = vec4(1, 0, 0, 1.0);
}
