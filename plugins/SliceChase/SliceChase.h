#pragma once
#include <set>
#include <TahuaPlugin.h>
#include "SliceLoader.h"

class SliceChase : public ffglqs::Effect
{
public:
	SliceChase();

	FFResult Init() override;
	void Update() override;
	FFResult Render( ProcessOpenGLStruct* inputTextures ) override;
	void Clean() override;

private:
	static const int MAX_SCREENS  = 32;
	static const int MAX_FAMILIES = 64;

	void AddCheckbox( const std::string& name, const std::string& group );
	// Updates exclusions from the checkboxes showing names; returns whether any was clicked.
	bool ReadCheckboxes( unsigned int first, const std::vector< std::string >& names, std::set< std::string >& exclusions );
	void ShowCheckboxes( unsigned int first, int poolSize, const std::vector< std::string >& names, const std::set< std::string >& exclusions );
	void WriteExclusions( unsigned int index, ffglqs::ParamText& param, const std::set< std::string >& exclusions );
	void UploadSlices( const SliceGeometry& geometry );
	void ReleaseSlices();

	std::shared_ptr< ffglqs::ParamText > slicesFile;
	std::shared_ptr< ffglqs::ParamText > excludedScreens;
	std::shared_ptr< ffglqs::ParamText > excludedFamilies;
	unsigned int excludedScreensIndex  = 0;
	unsigned int excludedFamiliesIndex = 0;
	unsigned int firstScreenBox        = 0;
	unsigned int firstFamilyBox        = 0;

	std::string loadedPath;
	Preset preset;
	std::set< std::string > screenExclusions;
	std::set< std::string > familyExclusions;
	std::vector< std::string > shownScreens; // name shown by each visible screen checkbox
	std::vector< std::string > shownFamilies;// name shown by each visible family checkbox

	ffglex::FFGLShader stateShader;
	ffglex::FFGLShader sliceShader;
	ffglex::FFGLFBO levels[ 2 ];// 1 x sliceCount: level of each slice, this frame and the previous one
	int currentLevels     = 0;
	GLuint samplesTexture = 0;  // SAMPLES_PER_SLICE x sliceCount: sample positions
	GLuint vao            = 0;
	GLuint vbo            = 0;
	GLsizei vertexCount   = 0;
	int sliceCount        = 0;
};
