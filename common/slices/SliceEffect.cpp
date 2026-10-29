#include "SliceEffect.h"
#include <sstream>
using namespace ffglqs;

namespace
{
// The quickstart has text parameters but no file parameter: same storage, other type.
class ParamFile : public ParamText
{
public:
	ParamFile( std::string name ) :
		ParamText( std::move( name ) )
	{
		type = FF_TYPE_FILE;
	}
};

const char SEPARATOR = '|';

std::set< std::string > Split( const std::string& text )
{
	std::set< std::string > names;
	std::stringstream stream( text );
	std::string name;
	while( std::getline( stream, name, SEPARATOR ) )
	{
		if( !name.empty() )
			names.insert( name );
	}
	return names;
}

std::string Join( const std::set< std::string >& names )
{
	std::string text;
	for( const std::string& name : names )
		text += ( text.empty() ? "" : std::string( 1, SEPARATOR ) ) + name;
	return text;
}

void Log( const std::string& message )
{
	FFGLLog::LogToHost( ( std::string( TAHUA_PLUGIN_NAME ) + ": " + message ).c_str() );
}

}//End anonymous namespace

void SliceEffect::AddSlicesFileParam()
{
	slicesFile = std::make_shared< ParamFile >( "Slices" );
	SetFileParamInfo( (unsigned int)params.size(), "Slices", { "xml" }, "" );
	params.push_back( slicesFile );
}

void SliceEffect::AddSelectionParams()
{
	// What is unchecked, saved by name so it survives a reordered or edited preset.
	// Hidden: the checkboxes below are the UI. Names become shader uniforms, hence no spaces.
	excludedScreensIndex = (unsigned int)params.size();
	excludedScreens      = ParamText::create( "ExcludedScreens" );
	AddParam( excludedScreens );
	SetParamVisibility( excludedScreensIndex, false, false );
	excludedFamiliesIndex = (unsigned int)params.size();
	excludedFamilies      = ParamText::create( "ExcludedFamilies" );
	AddParam( excludedFamilies );
	SetParamVisibility( excludedFamiliesIndex, false, false );

	// FFGL parameters cannot be added after construction: a pool of checkboxes is declared up
	// front, then renamed and shown for the screens and slice families of the loaded preset.
	firstScreenBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_SCREENS; ++i )
		AddCheckbox( "Screen" + std::to_string( i + 1 ), "Screens" );
	firstFamilyBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_FAMILIES; ++i )
		AddCheckbox( "Family" + std::to_string( i + 1 ), "Slices" );
}

void SliceEffect::AddCheckbox( const std::string& name, const std::string& group )
{
	unsigned int index = (unsigned int)params.size();
	AddParam( ParamBool::Create( name, true ) );
	SetParamGroup( index, group );
	SetParamVisibility( index, false, false );
}

SliceEffect::SelectionUpdate SliceEffect::UpdateSelection()
{
	// The host changes the file or restores the saved exclusions (composition load, undo):
	// the checkboxes follow. Otherwise a checkbox that differs from the exclusions was clicked.
	bool reload   = slicesFile->text != loadedPath;
	bool restored = excludedScreens->text != Join( screenExclusions ) || excludedFamilies->text != Join( familyExclusions );
	if( reload )
	{
		loadedPath = slicesFile->text;
		preset     = Preset();
		std::string error;
		if( !loadedPath.empty() && !LoadPreset( loadedPath, preset, error ) )
			Log( error );
		if( preset.screens.size() > MAX_SCREENS )
			Log( "more screens than checkboxes, the last ones are always on" );
	}
	if( restored )
	{
		screenExclusions = Split( excludedScreens->text );
		familyExclusions = Split( excludedFamilies->text );
	}
	bool toggled = false;
	if( !reload && !restored )
	{
		toggled |= ReadCheckboxes( firstScreenBox, shownScreens, screenExclusions );
		toggled |= ReadCheckboxes( firstFamilyBox, shownFamilies, familyExclusions );
	}
	if( !reload && !restored && !toggled )
		return {};

	shownScreens.clear();
	std::set< std::string > families;
	for( const PresetScreen& screen : preset.screens )
	{
		if( shownScreens.size() < MAX_SCREENS )
			shownScreens.push_back( screen.name );
		if( screenExclusions.count( screen.name ) == 0 )
			for( const PresetSlice& slice : screen.slices )
				families.insert( slice.family );
	}
	if( reload && families.size() > MAX_FAMILIES )
		Log( "more slice families than checkboxes, the last ones are always on" );
	shownFamilies.assign( families.begin(), families.end() );
	shownFamilies.resize( std::min( shownFamilies.size(), (size_t)MAX_FAMILIES ) );

	ShowCheckboxes( firstScreenBox, MAX_SCREENS, shownScreens, screenExclusions );
	ShowCheckboxes( firstFamilyBox, MAX_FAMILIES, shownFamilies, familyExclusions );
	WriteExclusions( excludedScreensIndex, *excludedScreens, screenExclusions );
	WriteExclusions( excludedFamiliesIndex, *excludedFamilies, familyExclusions );
	return { reload, true };
}

void SliceEffect::ClearPreset()
{
	loadedPath.clear();
	preset = Preset();
}

bool SliceEffect::IsSelected( const PresetScreen& screen ) const
{
	return screenExclusions.count( screen.name ) == 0;
}

bool SliceEffect::IsSelected( const PresetSlice& slice ) const
{
	return familyExclusions.count( slice.family ) == 0;
}

bool SliceEffect::ReadCheckboxes( unsigned int first, const std::vector< std::string >& names, std::set< std::string >& exclusions )
{
	bool changed = false;
	for( size_t i = 0; i < names.size(); ++i )
	{
		bool checked = params[ first + i ]->GetValue() > 0.5f;
		if( checked == ( exclusions.count( names[ i ] ) == 0 ) )
			continue;
		if( checked )
			exclusions.erase( names[ i ] );
		else
			exclusions.insert( names[ i ] );
		changed = true;
	}
	return changed;
}

void SliceEffect::ShowCheckboxes( unsigned int first, int poolSize, const std::vector< std::string >& names, const std::set< std::string >& exclusions )
{
	for( int i = 0; i < poolSize; ++i )
	{
		unsigned int index = first + i;
		bool shown         = i < (int)names.size();
		SetParamVisibility( index, shown, true );
		if( !shown )
			continue;
		SetParamDisplayName( index, names[ i ], true );
		float value = exclusions.count( names[ i ] ) == 0 ? 1.0f : 0.0f;
		if( params[ index ]->GetValue() != value )
		{
			params[ index ]->SetValue( value );
			RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
		}
	}
}

void SliceEffect::WriteExclusions( unsigned int index, ParamText& param, const std::set< std::string >& exclusions )
{
	std::string text = Join( exclusions );
	if( param.text == text )
		return;
	param.text = text;
	RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
}
