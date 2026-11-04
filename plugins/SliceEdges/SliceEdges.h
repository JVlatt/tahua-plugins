#pragma once
#include <SliceEffect.h>

class SliceEdges : public SliceEffect
{
public:
	SliceEdges();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	void UploadSlices( const std::vector< float >& vertices, const std::vector< float >& edges );
	void ReleaseSlices();
	// Beats of Resolume's tempo since the plugin started.
	double CountBeats();
	// 1 on each pulse, falling towards 0 until the next one.
	float PulseEnvelope( double beats );

	ffglex::FFGLShader edgeShader;
	GLuint vao          = 0;
	GLuint vbo          = 0;
	GLuint edgesTexture = 0;// one texel per edge: start x, y, end x, y
	GLsizei vertexCount = 0;

	std::shared_ptr< ffglqs::ParamOption > pulseBeats;
	std::shared_ptr< ffglqs::ParamOption > trace;
	std::shared_ptr< ffglqs::ParamOption > traceBeats;
	double bars        = 0.0;// Bars seen since the plugin started, counted on each barPhase wrap.
	float lastBarPhase = 0.0f;
};
