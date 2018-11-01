#version 450

/*
This code is distributed under the 2-Clause BSD License

Copyright 2017 Victor Zappi
*/

/* simple fragment shader that assigns to each fragment a specific color [for screen rendering], according to the type of the related grid point */

in vec2 tex_c;

out vec4 frag_color;

//----------------------------------------------------------------------------------------------------------
uniform sampler2D inputTexture;
uniform vec2 listenerFragCoord;
uniform vec2 deltaCoord;
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
const vec4 color_white  = vec4(1);
const vec4 color_black  = vec4(0);
const vec4 color_red    = vec4(0.9, 0, 0, 1);
const vec4 color_green  = vec4(0, 1, 0, 1);
const vec4 color_blue   = vec4(0, 0, 1, 1);
const vec4 color_yellow = vec4(1, 1, 0, 1);
const vec4 color_pink 	= vec4(1, 0.0, 1, 1);
const vec4 color_purple = vec4(0.6, 0.0, 1, 1);

float maxVol = 20;
const float log_min = 10.0f;
//----------------------------------------------------------------------------------------------------------


vec4 get_color_from_pressure(float p) {
	if(p > maxVol)
		return color_red;
	else if (p<-maxVol)
		return color_purple;
		
	float slider = abs(p/maxVol);
	float log_scale = (log(slider + 1e-8) + log_min) / log_min;
	float clamp_scale = clamp(log_scale, 0, 1);

	if (p > 0)
		return mix(vec4(0), color_white, clamp_scale);
	return mix(vec4(0), color_blue, clamp_scale);
}

void main () {
	frag_color = texture(inputTexture, tex_c); // read current RGBA values
	
	// decode type
	float boundary = 1-frag_color.b;
	float excitation = frag_color.a;
	
	// color cells according to type
	
	// regular transmissive point, the most likely case
	if(boundary == 0) {
		float p = frag_color.r;
		frag_color = get_color_from_pressure(p);
	}
	// boundary
	else if(boundary == 1) { 
		frag_color = color_green;
	}
	
	// excitation
	if(excitation == 1) {
		float p = frag_color.r;
		vec4 color = get_color_from_pressure(p);
		
		frag_color = mix(color, color_yellow, 0.5); // a bit transparent, to see propagation
	}
	
	
	// listener
	vec2 posDiff = vec2(tex_c.x - listenerFragCoord.x, tex_c.y - listenerFragCoord.y);
	vec2 absDiff = vec2(abs(posDiff.x), abs(posDiff.y));
	if(absDiff.x<deltaCoord.x/2 && absDiff.y<deltaCoord.y/2)
		frag_color = mix(frag_color, color_pink, 0.5); // a bit transparent, to see what's underneath
}
