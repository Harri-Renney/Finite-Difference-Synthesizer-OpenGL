#version 440

/*
This code is distributed under the 2-Clause BSD License

Copyright 2017 Victor Zappi
*/

/* fragment shader: FDTD solver running on all the fragments of the texture [grid points]. Also saves audio. Result is rendered to the taxture, via FBO */

in vec2 tex_c;
in vec2 tex_l;
in vec2 tex_u;
in vec2 tex_r;
in vec2 tex_d;

out vec4 frag_color;

//----------------------------------------------------------------------------------------------------------
// quads to draw
const int state0 = 0; // draw right quad 
const int state1 = 1; // read audio from left quad [cos right might not be ready yet]
const int state2 = 2; // draw left quad 
const int state3 = 3; // read audio from right quad [cos left might not be ready yet]

//----------------------------------------------------------------------------------------------------------
uniform sampler2D inOutTexture;
uniform int state;
uniform vec2 excitationPosition;
uniform float excitationMagnitude;
uniform vec2 wrCoord;   // [x coord of audioWrite pixel, RBGA index]
uniform vec2 listenerFragCoord[4];
uniform vec2 deltaCoord;	//Width + height of each fragment.

//----------------------------------------------------------------------------------------------------------
// can modify this simulate different materials and types of boundaries
//----------------------------------------------------------------------------------------------------------
uniform float dampFactor; 		// damping factor, the higher the quicker the damping. generally way below 1
uniform float propFactor;  	// propagation factor, that combines spatial scale and speed in the medium. must be <= 0.5
uniform float boundaryGain;  	// 0 means clamped boundary [wall], 1 means free boundary 
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------



// FDTD
vec4 computeFDTD() {
	// current point needs current pressure, previous pressure, type [checked later on]
	vec4 frag_color  = texture(inOutTexture, tex_c);
	vec4 p       = frag_color.rrrr; // p_n, this is turned into a vector cos used in parallel neighbor computation
	float p_prev = frag_color.g; // p_n-1
	
	// neighbours [pl_n, pr_n, pu_n, pd_n] need current pressure and type [only b...is boundary?]
	vec4 p_neigh;
	vec4 b_neigh;	//Checks all points if boundary. If they are, times by 0 and therefore preasure value not taken into account - Pretty weird.
	
	// left
	vec4 frag_l = texture(inOutTexture, tex_l);
	p_neigh.r   = frag_l.r;
	b_neigh.r   = frag_l.b;	
	
	// up
	vec4 frag_u = texture(inOutTexture, tex_u);
	p_neigh.g   = frag_u.r;
	b_neigh.g   = frag_u.b;
	
	// right
	vec4 frag_r = texture(inOutTexture, tex_r);
	p_neigh.b   = frag_r.r;
	b_neigh.b   = frag_r.b;

	// down
	vec4 frag_d = texture(inOutTexture, tex_d);
	p_neigh.a   = frag_d.r;
	b_neigh.a   = frag_d.b;
	
	// parallel computation of pLRUD
	vec4 pLRUD = p_neigh*b_neigh + p*(1-b_neigh)*boundaryGain; // gamma to simulate clamped or free edge
	
	// assemble equation
	float p_next = 2*p.r + (dampFactor-1) * p_prev;
	p_next += (pLRUD.x+pLRUD.y+pLRUD.z+pLRUD.w - 4*p.r) * propFactor;
	p_next /= dampFactor+1;
			
	// add excitation if this is excitation point [piece of cake]
	//int is_excitation = int(frag_color.a);
	//p_next += excitationMagnitude * is_excitation;
	
	//Change excitation point//
	//if((excitationPosition.x > (tex_c.x-0.01)) && (excitationPosition.x < (tex_c.x+0.01)) && (excitationPosition.y > (tex_c.y-0.01)) && (excitationPosition.y < (tex_c.y+0.01)))
	//if(excitationPosition == tex_c)
	//if((abs(excitationPosition.x - tex_c.x) < deltaCoord.x) && (abs(excitationPosition.y - tex_c.y) < deltaCoord.y))
	vec2 posDiff = vec2(tex_c.x - excitationPosition.x, tex_c.y - excitationPosition.y);
	vec2 absDiff = vec2(abs(posDiff.x), abs(posDiff.y));
	if(absDiff.x<deltaCoord.x/2 && absDiff.y<deltaCoord.y/2)
		p_next += excitationMagnitude;

		
	//          p_n+1    p_n  boundary? excitation?
	return vec4(p_next,  p.r, frag_color.b, frag_color.a); // pack and return (: [we maintain channels B and A intact]
}


// writes audio into audio quad from listener, in the correct pixel
vec4 saveAudio() {
	// write location
	float writeXCoord  = wrCoord[0]; // this helps chose what pixel to write audio to
	float writeChannel = wrCoord[1]; // this determines what pixel's channel to write to
		 
	// first of all copy all the 4 values stored in previous step 
	vec4 color = texture(inOutTexture, tex_c);
	
	// then see if this fragment is the one supposed to save the new audio sample...	
	float diffx = tex_c.x-writeXCoord;
	
	// is this the next available fragment?
	if( (diffx  < deltaCoord.x) && (diffx >= 0) )
	//if(abs(tex_c.x-writeXCoord) < deltaCoord.x)
	{
		// sample chosen grid point
		int readState = 1-(state/2); // index of the state we are reading from
		vec2 audioCoord = listenerFragCoord[readState]; 
		vec4 audioFrag = texture(inOutTexture, audioCoord); // get the audio info from the listener
		
		// silence boundaries, using b
		float audio = audioFrag.r * audioFrag.b;
		
		// put in the correct channel, according to the audio write command sent from cpu	
		color[int(writeChannel)] = audio;
		//color[int(writeChannel)] = audioFrag.r; //VIC
		//color[3] = 1;
		//color = vec4(1.0, 0.5, 0.25, 0.125);
	}	 
	return color;
}




void main() {

	if( (state == state0) || (state == state2) ) {	
		// regular FDTD calculation
		frag_color = computeFDTD();
		//frag_color = vec4(0.1,0.1,0.1,0.1);
	}
	else {
		// save audio routine
		frag_color = saveAudio();
		//frag_color = vec4(1.0,1.0,1.0,1.0);
	}
};
