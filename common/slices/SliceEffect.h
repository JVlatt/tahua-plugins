#pragma once
#include <set>
#include <TahuaPlugin.h>
#include "SliceLoader.h"

// An effect that reads an Advanced Output preset and lets the user pick its screens and slice
// families with checkboxes. Derived plugins add the parameters in this order, with their own
// in between as they like: AddSlicesFileParam(), ..., AddSelectionParams().
class SliceEffect : public ffglqs::Effect
{
protected:
	static const int MAX_SCREENS  = 32;
	static const int MAX_FAMILIES = 64;

	struct SelectionUpdate
	{
		bool presetLoaded = false;// a new file was read: preset changed
		bool changed      = false;// the selected slices changed (always true when presetLoaded)
	};

	void AddSlicesFileParam();
	void AddSelectionParams();
	// Call from Update(): follows the file parameter and the checkboxes.
	SelectionUpdate UpdateSelection();
	// Call from Clean().
	void ClearPreset();

	bool IsSelected( const PresetScreen& screen ) const;
	bool IsSelected( const PresetSlice& slice ) const;

	Preset preset;

private:
	void AddCheckbox( const std::string& name, const std::string& group );
	// Updates exclusions from the checkboxes showing names; returns whether any was clicked.
	bool ReadCheckboxes( unsigned int first, const std::vector< std::string >& names, std::set< std::string >& exclusions );
	void ShowCheckboxes( unsigned int first, int poolSize, const std::vector< std::string >& names, const std::set< std::string >& exclusions );
	void WriteExclusions( unsigned int index, ffglqs::ParamText& param, const std::set< std::string >& exclusions );

	std::shared_ptr< ffglqs::ParamText > slicesFile;
	std::shared_ptr< ffglqs::ParamText > excludedScreens;
	std::shared_ptr< ffglqs::ParamText > excludedFamilies;
	unsigned int excludedScreensIndex  = 0;
	unsigned int excludedFamiliesIndex = 0;
	unsigned int firstScreenBox        = 0;
	unsigned int firstFamilyBox        = 0;

	std::string loadedPath;
	std::set< std::string > screenExclusions;
	std::set< std::string > familyExclusions;
	std::vector< std::string > shownScreens; // name shown by each visible screen checkbox
	std::vector< std::string > shownFamilies;// name shown by each visible family checkbox
};
