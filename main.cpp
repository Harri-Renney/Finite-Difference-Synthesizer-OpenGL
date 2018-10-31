#include <iostream>
#include <fstream>
#include <sstream>

#include <cstdio>
#include <windows.h>

#include <glad\glad.h> 
#include <GLFW\glfw3.h>

#include <SFML/Audio.hpp>
#include <vector>

///////////
//DEFINES//
///////////

#define NUM_OF_TIMESTEPS	2		//Number of textures which hold simulation model time steps.
#define MAGNIFIER			10		//The factor which the model is scaled by when rendering the texture to screen.
#define MICROSECS_IN_SEC	1000000	//Microseconds in second - Might be used to play recorded samples everysecond.

//Index into vertices to indentify texture Quad//
#define QUAD0				0		//The first simulation model grid - Alternatively switches between timestep n & n-1.
#define QUAD1				1		//The second simulation model grid - Alteratively switches between timestep n-1 & n.
#define QUAD2				2		//The audio buffer - Single fragment strip acting as a buffer for recording samples from listener point. 

////////////////////
//GLOBAL VARIABLES//
////////////////////

//Audio Buffers//
int sampleCounter = 0;						//Counter used to track when seconds worth of samples collected.
sf::Int16 realTimeAudioBuffer[54100];		//Records each second of generated samples to play in "real-time".
std::vector<sf::Int16> playbackAudioBuffer;	//Records all samples generate to play at end of program.

//Clock Variables//
clock_t realTimeClock;

//SFML Audio Objects//
sf::SoundBuffer engineSoundBuffer;
sf::Sound soundEngine;

//Simulation Model Variables//
int domainSize[2] = { 80, 80 };				//Number of simulation points - The number of cartisian cells in one quad. Used to produce models of both timesteps.
int ceiling = 2;							//The audio row and isloation row located at top of texture, comprising the "ceiling".
float excitationPosition[2] = { 0.7,0.5 };	//Contains coordinates of the excitation point - Currently supports one point.
int listenerPosition[2] = { 5,5 };			//Contains coordinates of the audio sampling point - Currently supports one point.
int buffer_size = 128;						//Size of the audio buffer - The number samples recorded before audio buffer is read.

//User Defined Settings//
int sampleRate = 44100;													//Rate at which simulation is advanced, and audio sample collected.
int duration = 10;														//Duration of simulation.
float maxExcitation = 1.0;												//Amplitude of spike from excitation.
int excitationFrequency = 1000;											//Frequency of spikes. Number of zeros before excitation spike.
int excitationDuration = sampleRate / excitationFrequency;				//How often strike/excitation.

/*
* state0: draw quad0 [left]
* state1: read audio from quad1 [right] cos quad0 might not be ready yet
* state2: draw quad1 [right]
* state3: read audio from quad0 [left] cos quad1 might not be ready yet
*/
int state;


/////////////////////
//Uniform Locations//
/////////////////////
GLuint stateLocation;
GLuint excitationPositionLocation;
GLuint excitationMagnitudeLocation;
GLuint wrCoordLocation;	

////////////////////
//HELPER FUNCTIONS//
////////////////////

//OpenGL function to load specific text files into a OpenGL shader program//
bool loadShaderProgram(const char* vertexShaderPath, const char* fragmentShaderPath, GLuint& shaderProgram);

//On mouse click callback - Handles setting new excitation point//
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

int main()
{
	///////////////////////////////
	//Set model static parameters//
	///////////////////////////////
	float propagationFactor;
	std::cout << "Input a propogation factor for membrane material - Valid range [0.0-0.5]: ";
	std::cin >> propagationFactor;

	float dampingFactor;
	std::cout << "Input a damping factor for membrane material - Valid range [0.0-1.0] but expected very low value: ";
	std::cin >> dampingFactor;

	float boundaryGain;
	std::cout << "Input boundary gain. If it is clamped, and therefore reflects - 1 for fully clamped, 0 for free: ";
	std::cin >> boundaryGain;

	bool isSingleExcitation;	//Indicates if interactions with mouse cause a single or continouse excitation.
	std::cout << "Single or continous excitation - 0 for continous, 1 for single: ";
	std::cin >> isSingleExcitation;

	//////////////////////////
	//Initialize GLFW window//
	//////////////////////////
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	//Create GLFW window//
	GLFWwindow* window = glfwCreateWindow(domainSize[0] * MAGNIFIER, domainSize[1] * MAGNIFIER, "LearnOpenGL", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);

	//Initialize GLAD for loading OpenGL function pointers etc - Alternative to GLEW//
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
	glPointSize(MAGNIFIER); //Set magnifier to 10 - Factor increase number pixels in fragment.
	std::cout << "OpenGL " << glGetString(GL_VERSION) << " Supported" << std::endl;

	////////////////////////
	//Load Shader Programs//
	////////////////////////

	const char* vertex_fbo_shader_path = { "Shaders/fbo_vs.glsl" }; // vertex shader of solver program
	const char* fragment_fbo_shader_path = { "Shaders/fbo_fs.glsl" }; // fragment shader of solver program
	const char* vertex_render_shader_path = { "Shaders/render_vs.glsl" }; // vertex shader of render program
	const char* fragment_render_shader_path = { "Shaders/render_fs.glsl" }; // fragment shader of render program

	GLuint fboShaderProgram = 0;
	if (!loadShaderProgram(vertex_fbo_shader_path, fragment_fbo_shader_path, fboShaderProgram))
		std::cout << "Failed to create fbo shader." << std::endl;

	GLuint renderShaderProgram = 0;
	if (!loadShaderProgram(vertex_render_shader_path, fragment_render_shader_path, renderShaderProgram))
		std::cout << "Failed to create render shader." << std::endl;

	////////////////////////////////////////////
	//Structure texture with FDTD audio layout//
	////////////////////////////////////////////

	//Specify infromation numbers for texture//
	int numOfAttributesPerVertex = 12;						//The number of pieces of information each vertex contains.
	int numOfVerticesPerQuad = 4;							//Number of vertices that make up each texture quad.

	//Calculate texture size to fit FDTD structure//
	int textureWidth = domainSize[0] * NUM_OF_TIMESTEPS;	//The texture needs to contain the two timestep quads.
	int textureHeight = domainSize[1] + ceiling;			//The texture needs to contain the quad and then the isolation and audio row.

	//Calculate delta texture coordinates - The representation shaders use from width and height//
	float deltaX = 1.0 / (float)textureWidth;
	float deltaY = 1.0 / (float)textureHeight;

	//Calculate delta to compute vertex y position leaving space for isolation + audio row.
	float deltaV = 2.0 / (float)textureHeight;				//Unsure about this?

	float attributes[] = {
		// quad0 [left quadrant]
		// 4 vertices
		// pos N+1/-1				tex C coord N				tex L coord N							tex U coord N							tex R coord N							tex D coord N
		-1, -1,						0.5, 0,					0.5f - deltaX, 0,						0.5f, 0 + deltaY,						0.5f + deltaX, 0,						0.5f, 0 - deltaY,						// bottom left
		-1, 1 - ceiling * deltaV,	0.5f, 1 - ceiling * deltaY,	0.5f - deltaX, 1 - ceiling * deltaY,	0.5f, 1 + deltaY - ceiling * deltaY,	0.5f + deltaX, 1 - ceiling * deltaY,	0.5f, 1 - deltaY - ceiling * deltaY,	// top left [leaving space for clng]
		0, -1,						1.0f, 0,					1 - deltaX,    0,						1,    0 + deltaY,						1 + deltaX,    0,						1, 	  0 - deltaY,						// bottom right
		0, 1 - ceiling * deltaV,	1.0f, 1 - ceiling * deltaY,	1 - deltaX,    1 - ceiling * deltaY,	1,    1 + deltaY - ceiling * deltaY,	1 + deltaX,    1 - ceiling * deltaY,	1, 	  1 - deltaY - ceiling * deltaY,	// top right [leaving space for clng]

		// quad1 [right quadrant]
		// 4 vertices
		// pos N+1/-1				tex C coord N				tex L coord N							tex U coord N							tex R coord N							tex D coord N
		0, -1,						0, 0,						0 - deltaX,	0,							0, 0 + deltaY,							0 + deltaX,	0,							0, 0 - deltaY,							// bottom left
		0, 1 - ceiling * deltaV,	0,    1 - ceiling * deltaY,	0 - deltaX, 1 - ceiling * deltaY,		0,    1 + deltaY - ceiling * deltaY,	0 + deltaX,    1 - ceiling * deltaY,	0,    1 - deltaY - ceiling * deltaY,	// top left [leaving space for clng]
		1, -1,						0.5f, 0,					0.5f - deltaX, 0,						0.5f, 0 + deltaY,						0.5f + deltaX, 0,						0.5f, 0 - deltaY,						// bottom right
		1, 1 - ceiling * deltaV,	0.5f, 1 - ceiling * deltaY,	0.5f - deltaX, 1 - ceiling * deltaY,	0.5f, 1 + deltaY - ceiling * deltaY,	0.5f + deltaX, 1 - ceiling * deltaY,	0.5f, 1 - deltaY - ceiling * deltaY,	// top right [leaving space for clng]

		// quad2 [ audio quadrant]
		// 4 vertices
		// pos [no concept of time step]		tex C coords are the only ones required...
		-1, 1 - deltaV,			0,    1 - deltaY,	0,    0,			0,    0,			0,    0,			0,    0,	// bottom left [1 pixel below top]
		-1, 1,	    			0,    1,			0,    0,			0,    0,			0,    0,			0,    0,	// top left
		1, 1 - deltaV,			1,    1 - deltaY,	0,    0,			0,    0,			0,    0,			0,    0,	// bottom right [1 pixel below top]
		1, 1,					1,    1,			0,    0,			0,    0,			0,    0,			0,    0,	// top right
	};

	/////////////////
	//Quad Vertices//
	/////////////////
	int vertices[NUM_OF_TIMESTEPS + 1][2];		//Vertices + attributes stored here - For timestep and audio quads.

	//quad0 is composed of vertices 0 to 3.
	vertices[0][0] = 0;						//Index of first vertex.
	vertices[0][1] = numOfVerticesPerQuad;	//Number of vertices.

	//quad1 is composed of vertices 4 to 7.
	vertices[1][0] = 4;
	vertices[1][1] = numOfVerticesPerQuad;

	//quad2 is composed of vertices 8 to 11.
	vertices[2][0] = 8;
	vertices[2][1] = numOfVerticesPerQuad;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Create VBO + VAO objects - The VBO is the data passed to the GPU and VAO interprets how to read the VBO content//
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	unsigned int vbo;
	glGenBuffers(1, &vbo);
	//glBindBuffer(GL_ARRAY_BUFFER, vbo);
	//glBufferData(GL_ARRAY_BUFFER, sizeof(attributes), attributes, GL_STATIC_DRAW);

	unsigned int vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(attributes), attributes, GL_STATIC_DRAW);

	///////////////////////////////////////////////
	//Describe attributes shaders access from VBO//
	///////////////////////////////////////////////
	int numOfElementsPerAttribute = 4;	//Each attribute has 2 sets of coodinates. Therefore 4(vec4) elements form vertices data structure.

	GLint pos_and_texc_loc = glGetAttribLocation(fboShaderProgram, "pos_and_texc");
	glEnableVertexAttribArray(pos_and_texc_loc);
	glVertexAttribPointer(pos_and_texc_loc, numOfElementsPerAttribute, GL_FLOAT, GL_FALSE, numOfAttributesPerVertex * sizeof(GLfloat), (void*)(0 * numOfElementsPerAttribute * sizeof(GLfloat)));

	GLint texl_and_texu_loc = glGetAttribLocation(fboShaderProgram, "texl_and_texu");
	glEnableVertexAttribArray(texl_and_texu_loc);
	glVertexAttribPointer(texl_and_texu_loc, numOfElementsPerAttribute, GL_FLOAT, GL_FALSE, numOfAttributesPerVertex * sizeof(GLfloat), (void*)(1 * numOfElementsPerAttribute * sizeof(GLfloat)));

	GLint texr_and_texd_loc = glGetAttribLocation(fboShaderProgram, "texr_and_texd");
	glEnableVertexAttribArray(texr_and_texd_loc);
	glVertexAttribPointer(texr_and_texd_loc, numOfElementsPerAttribute, GL_FLOAT, GL_FALSE, numOfAttributesPerVertex * sizeof(GLfloat), (void*)(2 * numOfElementsPerAttribute * sizeof(GLfloat)));

	//Need to set attribute for render shader?//
	pos_and_texc_loc = glGetAttribLocation(renderShaderProgram, "pos_and_texc");
	glEnableVertexAttribArray(pos_and_texc_loc);
	glVertexAttribPointer(pos_and_texc_loc, numOfElementsPerAttribute, GL_FLOAT, GL_FALSE, numOfAttributesPerVertex * sizeof(GLfloat), (void*)(0 * numOfElementsPerAttribute * sizeof(GLfloat)));

	/////////////////////
	//Initalize Texture//
	/////////////////////

	//Initalize flattened multidimensional float array that will contain all fragments//
	uint8_t numChannels = 4;	//4 channels per pixel - RGBA.
	float* texturePixels = new float[textureWidth*textureHeight*numChannels];
	memset(texturePixels, 0, sizeof(float) * textureWidth * textureHeight * numChannels);

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Define the domain - The area of the texture which includes information. Normal points, boundaries, excitation points//
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	float** pointType[3];	//Why need three types? Normal point, excitation point, ???

	//Allocate memory for x axis//
	pointType[0] = new float*[domainSize[0]];	//Allocate memory for float pointer of size domainSize[0].
	pointType[1] = new float*[domainSize[0]];
	for (int i = 0; i != domainSize[0]; ++i)
	{
		//Allocate memory for y axis//
		pointType[0][i] = new float[domainSize[1]];
		pointType[1][i] = new float[domainSize[1]];

		//Initalize point types//
		for (int j = 0; j != domainSize[1]; ++j)
		{
			pointType[0][i][j] = 1;			//Regular point - Transmission value 1.
			pointType[1][i][j] = 0;			//No excitation.

			//Place excitation point specified//
			if ((i == excitationPosition[0]) && (j == excitationPosition[1]))	//Could support multiple by being excitation grid with 1 in positions.
				pointType[1][i][j] = 1;		//Excitation
		}
	}

	//Add columns of boundary points on left and right//
	//I think tutorial uses domainSize axis wrong way around. And only works as they are both equal in this program setup being {80, 80}//
	//Also why is there -1 size from certain sides?
	for (int i = 0; i != domainSize[0]; ++i)
	{
		pointType[0][i][0] = 0;		//Transmission value 0.
		pointType[0][i][domainSize[1] - 1] = 0;
	}

	for (int i = 0; i != domainSize[1] - 1; ++i)
	{
		pointType[0][0][i] = 0;
		pointType[0][domainSize[0] - 1][i] = 0;
	}

	////////////////////////////////////////////////////////////////////////
	//Define domain in texture - Copy domain into point type channels R, A//
	////////////////////////////////////////////////////////////////////////
	for (int i = 0; i != textureWidth; ++i)
	{
		for (int j = 0; j != textureHeight - ceiling; ++j)
		{
			//Quad0//
			if (i < domainSize[0])
			{
				texturePixels[(j*textureWidth + i) * 4 + 2] = pointType[0][i][j];
				texturePixels[(j*textureWidth + i) * 4 + 3] = pointType[1][i][j];
			}
			//Quad1//
			if (i >= domainSize[0])
			{
				texturePixels[(j*textureWidth + i) * 4 + 2] = pointType[0][i - domainSize[0]][j];
				texturePixels[(j*textureWidth + i) * 4 + 3] = pointType[1][i - domainSize[0]][j];
			}
		}
	}

	for (int i = 0; i != sizeof(texturePixels) * 4; ++i)
	{
		std::cout << texturePixels[i] << " ";
	}

	//Clean up//
	for (int i = 0; i < domainSize[0]; i++) {
		delete[] pointType[0][i];
		delete[] pointType[1][i];
	}
	delete[] pointType[0];
	delete[] pointType[1];

	///////////////////////////////////////////
	//Create texture using texture pixel data//
	///////////////////////////////////////////
	unsigned int texture;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_FLOAT, texturePixels);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glBindTexture(GL_TEXTURE_2D, 0);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmap(GL_TEXTURE_2D);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Create Framebuffer object - Memory we write the texture to on memory. This is done instead of using the default rendering framebuffer provided for window//
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	unsigned int fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);	//Should be able to use GL_DRAW_FRAMEBUFFER as only written to.
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	//Check fbo complete//
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
		std::cout << "VIKTOR!";

	///////////////////////////////////////////////////////////////////////////////
	//Create Pixel buffer object - Apparently to read from texture, Audio buffer?//
	///////////////////////////////////////////////////////////////////////////////
	//I think because we bind this buffer here. When using glReadPixels() later. It reads pixels from framebuffer into the pbo as state still remains//
	unsigned int pbo;
	glGenBuffers(1, &pbo);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float)*buffer_size * 4, NULL, GL_STREAM_READ);	//If we are using pbo to read do we use this??

	/////////////////////////////////////////////////////////////////////////////
	//Calculate useful(?) values - These will be used as uniforms in the shader//
	/////////////////////////////////////////////////////////////////////////////

	//Re-assign here for clarity//
	float deltaCoordX = deltaX;
	float deltaCoordY = deltaY;

	//Compute texture coordinates of the listener point which shader can recognize//
	float listenerFragCoord[2][2];

	//Quad0 reads audio from Quad1//
	listenerFragCoord[0][0] = (float)(listenerPosition[0] + 0.5 + domainSize[0]) / (float)textureWidth;
	listenerFragCoord[0][1] = (float)(listenerPosition[1] + 0.5) / (float)textureHeight;
	//Quad1 reads audio from Quad0//
	listenerFragCoord[1][0] = (float)(listenerPosition[0] + 0.5) / (float)textureWidth;
	listenerFragCoord[1][1] = (float)(listenerPosition[1] + 0.5) / (float)textureHeight;
	//Apparently +0.5 needed to match fragment we want to sample. Would like to know why.

	//////////////////////////////////
	//Setup Uniform data for shaders//
	//////////////////////////////////

	//////////////////////////////////
	//Update the FBO shader uniforms//
	glUseProgram(fboShaderProgram);

	///////////////////
	//Static Uniforms//

	GLuint propagationFactorLocation = glGetUniformLocation(fboShaderProgram, "propFactor");
	glUniform1f(propagationFactorLocation, propagationFactor);
	GLuint dampingFactorLocation = glGetUniformLocation(fboShaderProgram, "dampFactor");
	glUniform1f(dampingFactorLocation, dampingFactor);
	GLuint boundaryClampedLocation = glGetUniformLocation(fboShaderProgram, "boundaryGain");
	glUniform1f(boundaryClampedLocation, boundaryGain);

	//Width of each fragment - Used for working out excitation fragment + audio fragment//
	GLuint deltaCoordLocation = glGetUniformLocation(fboShaderProgram, "deltaCoord");
	glUniform2f(deltaCoordLocation, deltaCoordX, deltaCoordY);

	//Listener fragment coordinates as uniforms - For both quads//
	GLuint listenerFragCoordLocation[2];
	char name[22];
	for (int i = 0; i != NUM_OF_TIMESTEPS; ++i)
	{
		//Form name of shader uniforms, to get their locations, to update values.//
		sprintf(name, "listenerFragCoord[%d]", i);
		listenerFragCoordLocation[i] = glGetUniformLocation(fboShaderProgram, name);
		glUniform2f(listenerFragCoordLocation[i], listenerFragCoord[i][0], listenerFragCoord[i][1]);
	}

	////////////////////
	//Dynamic Uniforms//

	//Dynamic Uniform Variables//
	float excitationMagnitude = 0;							//Starts with no excitation.
	int currentQuad = QUAD0;								//Is this the current quad of focus for audio sample? // we start to draw from quad 0, left
	float wrCoord[2] = { 0, 0 };							//Coordinates of current audio buffer recording point - Contains x coordinate of fragment and index of next available RGBA channel.

	//The current state of FDTD processing in the shader//
	stateLocation = glGetUniformLocation(fboShaderProgram, "state");

	//Value of excitation point - Active or not. This could be done differently? Just need an identified excitation point.//
	excitationMagnitudeLocation = glGetUniformLocation(fboShaderProgram, "excitationMagnitude");
	glUniform1f(excitationMagnitudeLocation, excitationMagnitude);
	excitationPositionLocation = glGetUniformLocation(fboShaderProgram, "excitationPosition");
	glUniform2f(excitationPositionLocation, excitationPosition[0], excitationPosition[1]);

	//Fragment coordinate of the audioWrite pixel - Increments over X axis with audio samples?//
	wrCoordLocation = glGetUniformLocation(fboShaderProgram, "wrCoord");

	//Set inOutTexture uniform to the texture number zero created previously//
	//Not directly updating it. Simply initializing it by mapping FBO to first texture created - At index 0//
	glUniform1i(glGetUniformLocation(fboShaderProgram, "inOutTexture"), 0);

	/////////////////////////////////////
	//Update the render shader uniforms//
	glUseProgram(renderShaderProgram);

	///////////////////
	//Static Uniforms//
	GLuint deltaCoordLocationRender = glGetUniformLocation(renderShaderProgram, "deltaCoord");
	glUniform2f(deltaCoordLocationRender, deltaCoordX, deltaCoordY);

	//Listener fragment coordinates as uniforms - Only need one quad for rendering//
	//Texture accessed only, as there is no FBO
	GLuint listenerFragCoordLocationRender = glGetUniformLocation(renderShaderProgram, "listenerFragCoord");
	glUniform2f(listenerFragCoordLocationRender, listenerFragCoord[0][0], listenerFragCoord[0][1]);

	//Set inputTexture as same texture at index 0, just for reading from//
	glUniform1i(glGetUniformLocation(renderShaderProgram, "inputTexture"), 0);

	glUseProgram(0);	//Finished with this shader program for now.


	////////////////////
	//Simulation Cycle//
	////////////////////

	//Total number samples collected over set duration//
	int totalSampleNum = sampleRate * duration;

	//Compute number of filled audio buffers needed for specified duration//
	int bufferNum = totalSampleNum / buffer_size;

	//Initalize variables//
	state = -1;
	int sampleCnt = 0;	//Think it is sample counter, used to show when next excitation should occour???

	//Allocate enough memort for global audio buffer to fit all the required samples in at the samplerate and duration//
	//audioBuffer = new float[bufferNum*buffer_size];

	//Cycle filling audio buffer until desired durations worth collected//
	for (int i = 0; i != bufferNum; ++i)
	{
		//Record time before rendering.
		clock_t beginRealTimeClock = clock();

		//Switch to FBO shader for Quad texture//
		glUseProgram(fboShaderProgram);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);			//Render to our framebuffer!
		glViewport(0, 0, textureWidth, textureHeight);	//Full viewport - Give access to all texture

		//Cycle simulation - Advance until single audio buffer filled//
		for (int n = 0; n != buffer_size; ++n)
		{
			//////////////////////
			//Advance Simulation//

			//Pass next excitation Value//
			glUniform1f(excitationMagnitudeLocation, excitationMagnitude);
			glUniform2f(excitationPositionLocation, excitationPosition[0], excitationPosition[1]);

			//Simulation step - Advance state to focus on next quad, then execute shader on it//
			state = currentQuad * 2;
			glUniform1i(stateLocation, state);
			glDrawArrays(GL_TRIANGLE_STRIP, vertices[currentQuad][0], vertices[currentQuad][1]);	//Draw quad0 or quad1.

			//Audio step - Read audio sample from previous quad, defined by current state//
			glUniform2fv(wrCoordLocation, 1, wrCoord);		//Fragment and channel.
			glUniform1i(stateLocation, state + 1);			//Use next state, which will be to read audio from correct quad in shader.
			glDrawArrays(GL_TRIANGLE_STRIP, vertices[QUAD2][0], vertices[QUAD2][1]);	//Draw quad2, the audio quad. Appending audio from quad0 or quad1.

			//Prepare next simulation cycle//
			currentQuad = 1 - currentQuad;
			wrCoord[1] = int(wrCoord[1] + 1) % 4;	//Increment audio channel by 1?
			if (wrCoord[1] == 0)
				wrCoord[0] += deltaCoordX;

			//Next excitation//
			if (++sampleCnt == excitationDuration)
			{
				excitationMagnitude = maxExcitation;
				sampleCnt = 0;
				if(isSingleExcitation)
					maxExcitation = 0;
			}
			else
				excitationMagnitude = 0;

			//Re-sync all parallel GPU threads - Also done implictly when buffers swapped//
			//Basically glDrawArray calls make asynchronous GPU computations - Calling this makes CPU wait for all GPU threads to complete before continue//
			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
		}

		//Reset audio buffer write coordinates to beginning of buffer and first channel//
		wrCoord[0] = 0;
		wrCoord[1] = 0;

		//Retrieve audio samples from texture - Uses pbo as last buffer bind//
		glReadPixels(0, textureHeight - 1, buffer_size / 4, 1, GL_RGBA, GL_FLOAT, 0);	//Quad2 is single audio row on top of texture with 4 samples in each row.
		float* sampleBuffer = (float*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);	//Apparently to have physical copy of smaples on CPU.

		//Append audio texture samples to audioBuffer//
		//memcpy(audioBuffer + i*(buffer_size), sampleBuffer, buffer_size * sizeof(float));
		for (int i = 0; i != buffer_size; ++i)
		{
			//Should be signed?//
			sf::Int16 sample = sampleBuffer[i] * 32767;
			//xPos = (((xPos - 0.0)*(1.0 - 0.5)) / (1.0 - 0.0)) + 0.5;
			//sf::Int16 sample = (((sampleBuffer[i] - 0.0)*(32767 + 32768)) / (1.0 - 0.0)) - 32768;
			playbackAudioBuffer.push_back(sample);
			//realTimeAudioBuffer.push_back(sample);
			realTimeAudioBuffer[sampleCounter++] = sample;
		}

		clock_t endRealTimeClock = clock();
		clock_t differenceRealTimeClock = endRealTimeClock - beginRealTimeClock;
		if (differenceRealTimeClock < MICROSECS_IN_SEC)
			//Sleep(MICROSECS_IN_SEC - renderFrame);

		//Real Time audio - Plays second of samples when accumulated//
		//sampleCounter += buffer_size;
		if (sampleCounter > sampleRate)
		{
			engineSoundBuffer.loadFromSamples(&realTimeAudioBuffer[0], sampleCounter, 1, 44100);
			soundEngine.setBuffer(engineSoundBuffer);
			soundEngine.play();
			sampleCounter = 0;
		}

		//Switch to render shader - Only happens once every full audio buffer filled//
		glUseProgram(renderShaderProgram);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);	//Disable FBO to render to screen.
		glBindVertexArray(vao);
		glViewport(0, 0, textureWidth*MAGNIFIER, textureHeight*MAGNIFIER);

		//Render to screen//
		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		//glBindTexture(GL_TEXTURE_2D, texture);
		glDrawArrays(GL_TRIANGLE_STRIP, vertices[QUAD0][0], vertices[QUAD0][1]);	//Just pass quad0 fro  texture to render.
		glfwSwapBuffers(window);
		glfwPollEvents();

		//Poll escape key state - If presses, exit program//
		if (GLFW_PRESS == glfwGetKey(window, GLFW_KEY_ESCAPE))
			break;
	}

	//Playback audio - Plays the whole program accumulated audio//
	engineSoundBuffer.loadFromSamples(&playbackAudioBuffer[0], bufferNum*buffer_size, 1, 44100);
	soundEngine.setBuffer(engineSoundBuffer);
	soundEngine.play();

	//////////////////
	//End of program//
	//////////////////
	std::cout << "End of program." << std::endl;
	char c;
	std::cin >> c;

	return 0;
}

bool loadShaderProgram(const char* vertexShaderPath, const char* fragmentShaderPath, GLuint& shaderProgram)
{
	//Load files source code//
	std::string vertexSource;
	std::string fragmentSource;
	std::ifstream vShaderFile;
	std::ifstream fShaderFile;

	//Open files to ifstream//
	vShaderFile.open(vertexShaderPath);
	fShaderFile.open(fragmentShaderPath);

	//Read file's buffer content into stream//
	std::stringstream vShaderStream, fShaderStream;
	vShaderStream << vShaderFile.rdbuf();
	fShaderStream << fShaderFile.rdbuf();

	//Close files//
	vShaderFile.close();
	fShaderFile.close();

	//Convert stream to string//
	vertexSource = vShaderStream.str();
	fragmentSource = fShaderStream.str();

	//Set source code in char* for opengl c use//
	const char* vShaderCode = vertexSource.c_str();
	const char* fShaderCode = fragmentSource.c_str();

	//Compile vertex shader from source//
	GLuint vertexShader;
	vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vShaderCode, NULL);
	glCompileShader(vertexShader);

	//Compile fragment shader from source//
	GLuint fragmentShader;
	fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fShaderCode, NULL);
	glCompileShader(fragmentShader);

	//Create and link shaders into shader program//
	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	//Clean up shaders//
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	//Return status of new shader//
	int status;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
		return false;
	return true;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	//Because the coordinates from top left of screen taken, if the domain exceeds window size, then glfw returned coordinates will not calcualte correctly//
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		double xPos, yPos;
		glfwGetCursorPos(window, &xPos, &yPos);
		xPos = xPos / (domainSize[0] * MAGNIFIER);
		xPos = (((xPos - 0.0)*(1.0 - 0.5)) / (1.0 - 0.0)) + 0.5;
		//NewValue = (((OldValue - OldMin) * (NewMax - NewMin)) / (OldMax - OldMin)) + NewMin
		//xPos += 0.5;
		yPos = yPos / (domainSize[1] * MAGNIFIER);
		yPos = 1.0 - yPos;
		excitationPosition[0] = xPos;
		excitationPosition[1] = yPos;
		maxExcitation = 1.0;
	}
}