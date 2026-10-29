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
	};

	void UpdateScreenList();
	void UpdatePlacement( bool reload );
	void UpdateModeVisibility();
	Placement PlacementOf( int screen ) const;
	Placement ReadPlacementParams() const;
	void ShowPlacementParams( const Placement& placement );
	void UploadScreens();
	void ReleaseScreens();

	unsigned int modeIndex       = 0;
	unsigned int thresholdIndex  = 0;
	unsigned int colorIndex      = 0;
	unsigned int editScreenIndex = 0;
	unsigned int firstPlacement  = 0;// X, Y, Z, Rot X, Rot Y, Rot Z, Size
	unsigned int placementsIndex = 0;
	std::shared_ptr< ffglqs::ParamText > placementsText;

	std::map< std::string, Placement > placements;// by screen name; the others are laid out in a row
	std::string readPlacements;                   // placementsText as last read or written
	int editedScreen = -1;
	Placement shownPlacement;// what the placement parameters show for editedScreen

	ChaseLevels levels;
	ffglex::FFGLShader screenShader;
	ffglex::FFGLFBO scene;// with a depth buffer, which the host's target lacks; copied to it
	std::vector< ScreenDraw > draws;
	GLuint vao     = 0;
	GLuint vbo     = 0;
	int sliceCount = 0;
};
