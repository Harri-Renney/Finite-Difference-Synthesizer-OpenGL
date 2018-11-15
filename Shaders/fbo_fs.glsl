#version 410

/* fragment shader: FDTD solver running on all the fragments of the texture [grid points]. Also saves audio. Result is rendered to the taxture, via FBO */

//Texture coodinates of current and neighbouring fragments//
in vec2 tex_c;
in vec2 tex_l;
in vec2 tex_u;
in vec2 tex_r;
in vec2 tex_d;

out vec4 frag_color;

//States that define which quads to update//
const int state0 = 0; // draw right quad 
const int state1 = 1; // read audio from left quad [cos right might not be ready yet]
const int state2 = 2; // draw left quad 
const int state3 = 3; // read audio from right quad [cos left might not be ready yet]

//Uniforms//
uniform sampler2D inOutTexture;
uniform int state;
uniform vec2 excitationPosition;
uniform float excitationMagnitude;
uniform vec2 wrCoord;   			//Write pixel x coordinate and RGBA index.
uniform vec2 listenerFragCoord[4];	//Position of listener point in both model quads.
uniform vec2 deltaCoord;			//Width + height of each fragment.


//Material Parameters - Modify to simulate different materials and types of boundaries//
uniform float dampFactor; 		//Damping factor, the higher the quicker the damping. Typically way below 1.
uniform float propFactor;  		//Propagation factor, Combines spatial scale and speed in the medium. must be <= 0.5
uniform float boundaryGain;  	//0 means fully clamped boundary [wall], 1 means completly free boundary.


//Calculates new value of air pressure for current fragment//
vec4 computeFDTD()
{
	vec4 frag_color  = texture(inOutTexture, tex_c);
	vec4 p       = frag_color.rrrr; //Current pressure point - Input into vector to allow parallel neighbour computation.
	float p_prev = frag_color.g; 	//Previous pressure point
	
	//Neighbours [pl_n, pr_n, pu_n, pd_n] need current pressure and if boundary.
	vec4 p_neigh;
	vec4 b_neigh;	//Checks all points if boundary. If they are, times by 0 and therefore preasure value not taken into account - Pretty weird.
	
	//Left fragment//
	vec4 frag_l = texture(inOutTexture, tex_l);
	p_neigh.r   = frag_l.r;
	b_neigh.r   = frag_l.b;	
	
	//Up fragment//
	vec4 frag_u = texture(inOutTexture, tex_u);
	p_neigh.g   = frag_u.r;
	b_neigh.g   = frag_u.b;
	
	//Right fragment//
	vec4 frag_r = texture(inOutTexture, tex_r);
	p_neigh.b   = frag_r.r;
	b_neigh.b   = frag_r.b;

	//Down fragment//
	vec4 frag_d = texture(inOutTexture, tex_d);
	p_neigh.a   = frag_d.r;
	b_neigh.a   = frag_d.b;
	
	//Parallel computation of pLRUD//
	//Not sure why the last part in equation. The addition, what does it do??//
	vec4 pLRUD = p_neigh*b_neigh + p*(1-b_neigh)*boundaryGain;
	
	// assemble equation
	float p_next = 2*p.r + (dampFactor-1) * p_prev;
	p_next += (pLRUD.x+pLRUD.y+pLRUD.z+pLRUD.w - 4*p.r) * propFactor;
	p_next /= dampFactor+1;
	
	//Old excitation point method//
	//Add excitation if this is excitation point [piece of cake]
	//int is_excitation = int(frag_color.a);
	//p_next += excitationMagnitude * is_excitation;
	
	//Low pass filter?//
	//p_next = (p_next+p_prev)/2.0;
	
	//Change excitation point//
	vec2 posDiff = vec2(tex_c.x - excitationPosition.x, tex_c.y - excitationPosition.y);
	vec2 absDiff = vec2(abs(posDiff.x), abs(posDiff.y));
	if(absDiff.x<deltaCoord.x/2 && absDiff.y<deltaCoord.y/2)
		p_next += excitationMagnitude;

		
	//          p_n+1    p_n  boundary? excitation?
	return vec4(p_next,  p.r, frag_color.b, frag_color.a);	//New pressure point, use current for previous pressure, same boundary, same excitation.
}


//Writes audio into audio quad from listener point//
vec4 saveAudio()
{
	//Audio write location//
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
	}	 
	return color;
}




void main() {

	if( (state == state0) || (state == state2) )
	{	
		//Calculate FDTD on fragment//
		frag_color = computeFDTD();
	}
	else
	{
		//Save audio sample to buffer//
		frag_color = saveAudio();
	}
};
