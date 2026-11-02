#pragma once
#include <map>
#include <set>
#include <TahuaPlugin.h>
#include "SliceLoader.h"

// An effect that reads an Advanced Output preset and lets the user pick its screens, and the slice
// families of each screen, with checkboxes. Derived plugins add the parameters in this order, with their own
// in between as they like: AddSlicesFileParam(), ..., AddSelectionParams().
class SliceEffect : public ffglqs::Effect
{
protected:
	static const int MAX_SCREENS  = 32;
	static const int MAX_FAMILIES = 64;

	struct SelectionUpdate
	{
		bool presetLoaded = false;// a new file was read: preset changed
		bool changed      = false;// the selected slices or their offsets changed (always true when presetLoaded)
	};

	void AddSlicesFileParam();
	// With withOffsets set, each family checkbox is followed by a slider that sets that family's
	// offset on the picked screen; FamilyOffset() reads it.
	void AddSelectionParams( bool withOffsets = false );
	// Call from Update(): follows the file parameter and the checkboxes.
	SelectionUpdate UpdateSelection();
	// Call from Clean().
	void ClearPreset();

	bool IsSelected( const PresetScreen& screen ) const;
	bool IsSelected( const PresetScreen& screen, const PresetSlice& slice ) const;
	float FamilyOffset( const PresetScreen& screen, const PresetSlice& slice ) const;

	// An option with MAX_SCREENS entries, so that the quickstart accepts any index the host
	// sends, shown as the screen names of the loaded preset by ShowScreenNames().
	unsigned int AddScreenOption( const std::string& name, const std::string& displayName );
	void ShowScreenNames( unsigned int index );

	Preset preset;

private:
	void AddCheckbox( const std::string& name, const std::string& group );
	// Updates exclusions from the checkboxes showing names; returns whether any was clicked.
	bool ReadCheckboxes( unsigned int first, const std::vector< std::string >& names, std::set< std::string >& exclusions );
	// Same for the family checkboxes, which show the families of familyScreen.
	bool ReadFamilyCheckboxes();
	bool IsFamilyOn( const std::string& screen, const std::string& family ) const;
	// Updates offsets from the family sliders; returns whether any was moved.
	bool ReadFamilyOffsets();
	float OffsetOf( const std::string& screen, const std::string& family ) const;
	// Checkbox i is at first + i * stride.
	void ShowCheckboxes( unsigned int first, unsigned int stride, int poolSize, const std::vector< std::string >& names, const std::vector< bool >& checked );
	void ShowFamilyOffsets();
	void WriteExclusions( unsigned int index, ffglqs::ParamText& param, const std::set< std::string >& exclusions );
	unsigned int FamilyStride() const { return familyOffsetsText ? 2 : 1; }

	std::shared_ptr< ffglqs::ParamText > slicesFile;
	std::shared_ptr< ffglqs::ParamText > excludedScreens;
	std::shared_ptr< ffglqs::ParamText > excludedFamilies;
	unsigned int excludedScreensIndex  = 0;
	unsigned int excludedFamiliesIndex = 0;
	unsigned int firstScreenBox        = 0;
	unsigned int familyScreenIndex     = 0;
	unsigned int firstFamilyBox        = 0;// each followed by its offset slider, with withOffsets set
	std::shared_ptr< ffglqs::ParamText > familyOffsetsText;// null without withOffsets
	unsigned int familyOffsetsIndex = 0;

	std::string loadedPath;
	std::set< std::string > screenExclusions;
	// "screen>family" is off on that screen. A bare "family", saved before families were picked
	// per screen, is off on every screen.
	std::set< std::string > familyExclusions;
	std::map< std::string, float > familyOffsets;// by "screen>family"; 0 when absent
	std::vector< std::string > shownScreens; // name shown by each visible screen checkbox
	std::vector< std::string > shownFamilies;// name shown by each visible family checkbox
	int familyScreen = -1;                   // index of the screen whose families they are
};
