#pragma once
#include <vector>
#include <FFGLSDK.h>

// The level of each slice (0 = off, 1 = on), kept on the GPU: each frame, a slice moves towards
// on when the average luminance of the input texture over its samples is above a threshold,
// and towards off otherwise, at the speed set by the fade durations.
class ChaseLevels
{
public:
	bool Init();
	void FreeGLResources();

	// samples: SAMPLES_PER_SLICE points per slice, x, y each. All slices start off.
	void Upload( const std::vector< float >& samples, int sliceCount );
	void Release();

	// How a follower moves after the level: as the level (linear), on a spring with no overshoot
	// (smooth) or with one (elastic, more with elasticity), or falling onto it and bouncing
	// (bounce, higher with elasticity). speed in Hz sets how fast.
	enum Curve
	{
		CURVE_LINEAR,
		CURVE_SMOOTH,
		CURVE_ELASTIC,
		CURVE_BOUNCE,
	};
	struct Settings
	{
		float threshold, fadeIn, fadeOut, deltaTime;
		int curve        = CURVE_LINEAR;
		float speed      = 2.0f;
		float elasticity = 0.0f;
	};
	// Leaves the viewport set to the 1 x sliceCount levels target: the caller restores its own.
	void Update( const FFGLTextureStruct& input, const Settings& settings, ffglex::FFGLScreenQuad& quad );

	// 1 x sliceCount, GL_RGBA32F: read with texelFetch( levels, ivec2( 0, slice ), 0 ): r is the
	// level, g the follower's position (which can go past 0 and 1), b its speed.
	GLuint Texture() const;

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLFBO levels[ 2 ];// this frame and the previous one
	int current           = 0;
	GLuint samplesTexture = 0;// SAMPLES_PER_SLICE x sliceCount: sample positions
	int sliceCount        = 0;
};
