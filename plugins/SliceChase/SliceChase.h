#pragma once
#include <ChaseLevels.h>
#include <SliceEffect.h>

class SliceChase : public SliceEffect
{
public:
	SliceChase();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	void UploadSlices( const SliceGeometry& geometry );
	void ReleaseSlices();

	ChaseLevels levels;
	ffglex::FFGLShader sliceShader;
	GLuint vao          = 0;
	GLuint vbo          = 0;
	GLsizei vertexCount = 0;
	int sliceCount      = 0;
};
