#include "ChaseLevels.h"
#include <ffglex/FFGLScopedFBOBinding.h>
#include "SliceLoader.h"
using namespace ffglex;

namespace
{
// Drawn into a 1 x sliceCount target: row i averages the input over slice i's samples,
// compares it to the threshold and moves the slice's level towards the result. Textures are
// premultiplied, so the luminance of rgb already includes alpha.
const char vertexShader[] = R"(
#version 410 core
layout( location = 0 ) in vec4 vPosition;

void main()
{
	gl_Position = vPosition;
}
)";

const char fragmentShader[] = R"(
#version 410 core
uniform sampler2D inputTexture;
uniform sampler2D samples;
uniform sampler2D previousLevels;
uniform vec2 maxUV;
uniform float threshold;
uniform float fadeIn;
uniform float fadeOut;
uniform float deltaTime;
uniform int curve;// ChaseLevels::Curve
uniform float speed;
uniform float elasticity;
out vec4 fragColor;

void main()
{
	int slice = int( gl_FragCoord.y );
	int count = textureSize( samples, 0 ).x;
	float sum = 0.0;
	for( int i = 0; i < count; ++i )
	{
		vec2 uv = texelFetch( samples, ivec2( i, slice ), 0 ).xy;
		sum += dot( texture( inputTexture, uv * maxUV ).rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	}
	float target   = sum / float( count ) > threshold ? 1.0 : 0.0;
	vec4 previous  = texelFetch( previousLevels, ivec2( 0, slice ), 0 );
	float duration = target > previous.r ? fadeIn : fadeOut;
	float maxStep  = duration > 0.0 ? deltaTime / duration : 1.0;
	float level    = previous.r + clamp( target - previous.r, -maxStep, maxStep );

	// The follower, pulled towards the level, in small enough steps to stay stable.
	float position = level, velocity = 0.0;
	if( curve != 0 )
	{
		position    = previous.g;
		velocity    = previous.b;
		float omega = 6.2831853 * speed;
		// Smooth is critically damped; Elastic less and less damped as elasticity goes up.
		float damping = 2.0 * omega * ( curve == 2 ? 1.0 - 0.95 * elasticity : 1.0 );
		// Bounce: falls the whole way onto the level in half a period, and leaves it again with
		// part of its speed.
		float gravity     = 2.0 * omega * omega / 9.8696;// 2 / ( period / 2 )^2: a fall of 1
		float restitution = 0.2 + 0.7 * elasticity;
		int steps         = clamp( int( ceil( deltaTime / 0.002 ) ), 1, 100 );
		float h           = deltaTime / float( steps );
		for( int i = 0; i < steps; ++i )
		{
			if( curve == 3 )
			{
				float side = sign( level - position );
				velocity += side * gravity * h;
				position += velocity * h;
				if( side != 0.0 && sign( level - position ) != side )
				{
					position = level;
					velocity = abs( velocity ) * restitution < 0.05 ? 0.0 : -velocity * restitution;
				}
			}
			else
			{
				velocity += ( omega * omega * ( level - position ) - damping * velocity ) * h;
				position += velocity * h;
			}
		}
	}
	fragColor = vec4( level, position, velocity, 0.0 );
}
)";

}//End anonymous namespace

bool ChaseLevels::Init()
{
	return shader.Compile( vertexShader, fragmentShader );
}

void ChaseLevels::FreeGLResources()
{
	Release();
	shader.FreeGLResources();
}

void ChaseLevels::Upload( const std::vector< float >& samples, int count )
{
	sliceCount = count;

	// 32-bit levels: a long fade moves them by tiny steps each frame. They start off, at rest.
	for( FFGLFBO& fbo : levels )
	{
		fbo.Initialise( 1, sliceCount, GL_RGBA32F );
		ScopedFBOBinding fboBinding( fbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	glGenTextures( 1, &samplesTexture );
	Scoped2DTextureBinding textureBinding( samplesTexture );
	// texelFetch only, but the texture must still be complete: no mipmaps.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RG32F, SAMPLES_PER_SLICE, sliceCount, 0, GL_RG, GL_FLOAT, samples.data() );
}

void ChaseLevels::Release()
{
	for( FFGLFBO& fbo : levels )
		fbo.Release();
	glDeleteTextures( 1, &samplesTexture );
	samplesTexture = 0;
	sliceCount     = 0;
}

void ChaseLevels::Update( const FFGLTextureStruct& input, const Settings& settings, FFGLScreenQuad& quad )
{
	// New level of each slice, from the previous frame's levels. The two level targets swap
	// roles every frame.
	FFGLFBO& previousLevels = levels[ current ];
	current                 = 1 - current;

	ScopedFBOBinding fboBinding( levels[ current ].GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, 1, sliceCount );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	// ScopedSamplerActivation returns to unit 0, not to the previous unit: with three units,
	// scoped bindings would unbind the wrong one and leave a texture bound for the host.
	const GLuint textures[] = { input.Handle, samplesTexture, previousLevels.GetTextureInfo().Handle };
	for( GLuint unit = 0; unit < 3; ++unit )
	{
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, textures[ unit ] );
	}

	FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
	shader.Set( "inputTexture", 0 );
	shader.Set( "samples", 1 );
	shader.Set( "previousLevels", 2 );
	shader.Set( "maxUV", maxCoords.s, maxCoords.t );
	shader.Set( "threshold", settings.threshold );
	shader.Set( "fadeIn", settings.fadeIn );
	shader.Set( "fadeOut", settings.fadeOut );
	shader.Set( "deltaTime", settings.deltaTime );
	shader.Set( "curve", settings.curve );
	shader.Set( "speed", settings.speed );
	shader.Set( "elasticity", settings.elasticity );
	quad.Draw();

	for( GLuint unit = 3; unit-- > 0; )
	{
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
}

GLuint ChaseLevels::Texture() const
{
	return levels[ current ].GetTextureInfo().Handle;
}
