#version 450

/*
This code is distributed under the 2-Clause BSD License

Copyright 2017 Victor Zappi
*/

/* simple vertex shader to build a single flat quadrant where to place the portion of the FDTD texture that will be rendered to screen */


in vec4 pos_and_texc;

out vec2 tex_c;


void main () {
	gl_Position = vec4(pos_and_texc.rg, 0.0, 1.0); // set position
	tex_c       = vec2(pos_and_texc.ba);                 // pass texture coord along to fragment shader
}