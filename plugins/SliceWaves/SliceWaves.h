#pragma once
#include <SliceEffect.h>

class SliceWaves : public SliceEffect
{
public:
	SliceWaves();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	// Rebuilds the slices' geometry, frames and time offsets from the preset and the selection.
	void BuildSlices();
	void UploadSlices( const std::vector< float >& vertices, const std::vector< float >& edges );
	void ReleaseSlices();

	ffglex::FFGLShader waveShader;
	GLuint vao          = 0;
	GLuint vbo          = 0;
	GLuint edgesTexture = 0;// one texel per edge: start x, y, end x, y
	GLsizei vertexCount = 0;

	std::shared_ptr< ffglqs::ParamBool > sync;
	std::shared_ptr< ffglqs::ParamOption > beats;
	std::shared_ptr< ffglqs::ParamRange > speed;
	std::shared_ptr< ffglqs::ParamOption > offset;
	float builtOffset = -1.0f, builtOriginX = -1.0f, builtOriginY = -1.0f;// what the offsets were built for

	double cycles      = 0.0;// Animation cycles elapsed, free-running or derived from the bar count.
	double bars        = 0.0;// Bars seen since the plugin started, counted on each barPhase wrap.
	float lastBarPhase = 0.0f;
};
