#pragma once
#include <map>
#include <ChaseLevels.h>
#include <SliceEffect.h>

class Stage3D : public SliceEffect
{
public:
	Stage3D();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

	// Where a screen stands: its centre, its rotation in degrees and its height, in scene units.
	struct Placement
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
		float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
		float size = 1.0f;
		bool operator==( const Placement& other ) const;
	};

private:
	// The vertices of one screen, in this order: its backdrop (6 vertices), the front faces of its
	// selected slices, then their sides and back faces (drawn only when extruded).
	struct ScreenDraw
	{
		int screen;// index in preset.screens
		GLint first;
		GLint sidesFirst;
		GLint end;
		GLint edgesFirst = 0, edgesEnd = 0;// its slices' edges, in the edge buffer
	};
	// One slice's vertices, for drawing see-through slices one by one: its front faces, its sides,
	// then its back faces up to backEnd. centre is where it is sorted from, on its screen; low and
	// high bound its front face (x, y), on its screen.
	struct SliceDraw
	{
		int draw;// index in draws
		GLint frontFirst, frontEnd;
		GLint sidesFirst, backFirst, backEnd;
		float centre[ 3 ];
		float low[ 2 ], high[ 2 ];
	};

	void UpdatePlacement( bool reload );
	void UpdateModeVisibility();
	void ApplyMovePreset();
	void ApplyLightPreset();
	void ApplyMaterialPreset();
	// The point light this frame: its parameters, with its animation applied.
	struct LightState
	{
		float x, y, z, intensity;
		float hue, saturation, brightness;
	};
	LightState AnimateLight();
	void SetAndShow( unsigned int index, float value );// and tells the host
	Placement PlacementOf( int screen ) const;
	Placement ReadPlacementParams() const;
	void ShowPlacementParams( const Placement& placement );
	void UploadScreens();
	void ReleaseScreens();

	unsigned int modeIndex       = 0;
	unsigned int thresholdIndex  = 0;
	unsigned int curveIndex      = 0;// Curve, Elasticity, Speed
	unsigned int movePresetIndex = 0;// followed by Move X, Y, Z, Rotate X, Y, Z, Scale
	unsigned int colorIndex      = 0;
	unsigned int lightPresetIndex = 0;// followed by the Light parameters, in LIGHT_PRESETS' order
	unsigned int materialPresetIndex = 0;// just before materialIndex
	unsigned int materialIndex   = 0;// Base Color (4), Emissive, Emissive Color (4), Bounce, Spread, Specular, Glossiness, Fresnel, Metallic, Reflection, Iridescence
	unsigned int editScreenIndex = 0;
	unsigned int firstPlacement  = 0;// X, Y, Z, Rot X, Rot Y, Rot Z, Size
	unsigned int placementsIndex = 0;
	std::shared_ptr< ffglqs::ParamText > placementsText;

	std::map< std::string, Placement > placements;// by screen name; the others are laid out in a row
	std::string readPlacements;                   // placementsText as last read or written
	int editedScreen = -1;
	Placement shownPlacement;// what the placement parameters show for editedScreen
	int bars           = 0;// played so far, for light animations longer than a bar
	float lastBarPhase = 0.0f;

	ChaseLevels levels;
	ffglex::FFGLShader screenShader;
	ffglex::FFGLShader edgeShader;
	ffglex::FFGLFBO scene;// with a depth buffer, which the host's target lacks; copied to it
	std::vector< ScreenDraw > draws;
	std::vector< SliceDraw > sliceDraws;
	ffglex::FFGLShader spillShader;
	ffglex::FFGLFBO spill;// a mipmapped copy of the layer: its glow, blurred, lights the slices
	GLuint vao     = 0;
	GLuint vbo     = 0;
	GLuint edgeVao = 0;// the edges' bands, FLOATS_PER_EDGE floats per vertex
	GLuint edgeVbo = 0;
	int sliceCount = 0;
};
