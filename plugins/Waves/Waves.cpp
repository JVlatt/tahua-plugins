#include "Waves.h"
#include <cmath>
#include <WaveShapes.h>
using namespace ffglqs;

static PluginInstance pluginInstance = Source::CreatePlugin< Waves >( tahua::MakePluginInfo() );

// Each parameter added in the constructor becomes a uniform of the same name.
// Also available: i_uv, resolution, time, deltaTime, frame, bpm, phase.
// Output is a white mask carried by the alpha channel (premultiplied).
static const char fragmentShader[] = R"(
uniform float progress; // Position in the current cycle, 0 -> 1, set by Update().
uniform float noiseTime;// Continuous time for the Noise shape, set by Update().

)";

static const char fragmentMain[] = R"(
void main()
{
	float v = WaveShape( i_uv, resolution.x / resolution.y );
	if( Invert )
		v = 1.0 - v;
	fragColor = vec4( v, v, v, v );
}
)";

Waves::Waves()
{
	SetFragmentShader( std::string( fragmentShader ) + tahua::waveShapes + fragmentMain );
	AddParam( ParamOption::Create( "Shape", { { "Rings", 0.0f }, { "Ring", 1.0f }, { "Noise", 2.0f }, { "Pulse", 3.0f }, { "Lateral", 4.0f }, { "Flat", 5.0f }, { "Line", 6.0f }, { "Radar", 7.0f }, { "Lines", 8.0f } } ) );
	AddParam( sync = ParamBool::Create( "Sync", false ) );
	AddParam( beats = ParamOption::Create( "Beats", { { "1/4", 0.25f }, { "1/2", 0.5f }, { "1", 1.0f }, { "2", 2.0f }, { "4", 4.0f }, { "8", 8.0f }, { "16", 16.0f } }, 2 ) );
	AddParam( speed = ParamRange::Create( "Speed", 0.5f, { 0.0f, 4.0f } ) );
	AddParam( ParamRange::Create( "Scale", 1.0f, { 0.1f, 4.0f } ) );
	AddParam( ParamRange::Create( "Thickness", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Softness", 0.0f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Echoes", 8.0f, { 0.0f, 12.0f } ) );
	AddParam( ParamRange::Create( "Trail", 0.4f, { 0.0f, 0.9f } ) );
	AddParam( ParamRange::Create( "Spread", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Attack", 0.5f, { 0.0f, 1.0f } ) );
	AddParam( ParamRange::Create( "Direction", 270.0f, { 0.0f, 360.0f } ) );
	AddParam( ParamBool::Create( "Mirror", false ) );
	AddParam( Param::Create( "CenterX", 0.5f ) );
	AddParam( Param::Create( "CenterY", 0.5f ) );
	AddParam( ParamBool::Create( "Invert", false ) );
	// Added in 1.1: after the 1.0 parameters, so that saved compositions keep their values.
	AddParam( ParamOption::Create( "Sides", { { "Circle", 0.0f }, { "Triangle", 3.0f }, { "Square", 4.0f }, { "Pentagon", 5.0f }, { "Hexagon", 6.0f }, { "Octagon", 8.0f } } ) );
}

void Waves::Update()
{
	// barPhase runs 0 -> 1 over each bar of 4 beats: a big drop means a new bar.
	if( barPhase + 0.5f < lastBarPhase )
		bars += 1.0;
	lastBarPhase = barPhase;

	if( sync->GetValue() )
		cycles = ( bars + barPhase ) * 4.0 / beats->GetRealValue();
	else
		cycles += deltaTime * speed->GetValue();

	shader.Set( "progress", (float)( cycles - std::floor( cycles ) ) );
	shader.Set( "noiseTime", (float)std::fmod( cycles * 0.25, 1000.0 ) );
}
