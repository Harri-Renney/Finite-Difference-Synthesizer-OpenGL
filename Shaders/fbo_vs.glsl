#version 410

/*
This code is distributed under the 2-Clause BSD License

Copyright 2017 Victor Zappi
*/

/* simple vertex shader to build the 2 flat quadrants where to place the FDTD texture and the third one that will contain the audio */

in vec4 pos_and_texc;
in vec4 texl_and_texu;
in vec4 texr_and_texd;

out vec2 tex_c;
out vec2 tex_l;
out vec2 tex_u;
out vec2 tex_r;
out vec2 tex_d;

void main () {	
	gl_Position = vec4 (pos_and_texc.rg, 0.0, 1.0); // set vertex position
	// unpack and pass texture coords to fragment shader
	tex_c = pos_and_texc.ba;
	tex_l = texl_and_texu.rg;
	tex_u = texl_and_texu.ba;
	tex_r = texr_and_texd.rg;
	tex_d = texr_and_texd.ba;
}
