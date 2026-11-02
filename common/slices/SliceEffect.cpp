#include "SliceEffect.h"
#include <algorithm>
#include <locale>
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

// A family on one screen only.
std::string ScreenFamily( const std::string& screen, const std::string& family )
{
	return screen + '>' + family;
}

bool HasFamily( const PresetScreen& screen, const std::string& family )
{
	return std::any_of( screen.slices.begin(), screen.slices.end(), [ & ]( const PresetSlice& slice ) { return slice.family == family; } );
}

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

// Saved offsets: "screen>family=offset|...", in the C++ classic locale so that a host running
// in French does not turn 0.5 into 0,5.
std::map< std::string, float > ParseOffsets( const std::string& text )
{
	std::map< std::string, float > offsets;
	for( const std::string& entry : Split( text ) )
	{
		size_t equals = entry.rfind( '=' );
		if( equals == std::string::npos || equals == 0 )
			continue;
		std::istringstream value( entry.substr( equals + 1 ) );
		value.imbue( std::locale::classic() );
		float offset;
		if( value >> offset )
			offsets[ entry.substr( 0, equals ) ] = offset;
	}
	return offsets;
}

std::string JoinOffsets( const std::map< std::string, float >& offsets )
{
	std::ostringstream text;
	text.imbue( std::locale::classic() );
	for( const auto& entry : offsets )
	{
		if( text.tellp() > 0 )
			text << SEPARATOR;
		text << entry.first << '=' << entry.second;
	}
	return text.str();
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

void SliceEffect::AddSelectionParams( bool withOffsets )
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
	if( withOffsets )
	{
		familyOffsetsIndex = (unsigned int)params.size();
		familyOffsetsText  = ParamText::create( "FamilyOffsets" );
		AddParam( familyOffsetsText );
		SetParamVisibility( familyOffsetsIndex, false, false );
	}

	// FFGL parameters cannot be added after construction: a pool of checkboxes is declared up
	// front, then renamed and shown for the screens and slice families of the loaded preset.
	firstScreenBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_SCREENS; ++i )
		AddCheckbox( "Screen" + std::to_string( i + 1 ), "Screens" );
	// The family checkboxes show the families of the screen picked here.
	familyScreenIndex = AddScreenOption( "FamilyScreen", "Screen" );
	SetParamGroup( familyScreenIndex, "Slices" );
	firstFamilyBox = (unsigned int)params.size();
	for( int i = 0; i < MAX_FAMILIES; ++i )
	{
		AddCheckbox( "Family" + std::to_string( i + 1 ), "Slices" );
		if( !withOffsets )
			continue;
		unsigned int index = (unsigned int)params.size();
		AddParam( ParamRange::Create( "Family" + std::to_string( i + 1 ) + "Offset", 0.0f, { -1.0f, 1.0f } ) );
		SetParamGroup( index, "Slices" );
		SetParamVisibility( index, false, false );
	}
}

void SliceEffect::AddCheckbox( const std::string& name, const std::string& group )
{
	unsigned int index = (unsigned int)params.size();
	AddParam( ParamBool::Create( name, true ) );
	SetParamGroup( index, group );
	SetParamVisibility( index, false, false );
}

unsigned int SliceEffect::AddScreenOption( const std::string& name, const std::string& displayName )
{
	unsigned int index = (unsigned int)params.size();
	std::vector< ParamOption::Option > screens;
	for( int i = 0; i < MAX_SCREENS; ++i )
		screens.push_back( { "Screen" + std::to_string( i + 1 ), (float)i } );
	AddParam( ParamOption::Create( name, screens ) );
	SetParamDisplayName( index, displayName, false );
	SetParamElements( index, { "-" }, { 0.0f }, false );
	return index;
}

void SliceEffect::ShowScreenNames( unsigned int index )
{
	std::vector< std::string > names;
	std::vector< float > values;
	for( size_t i = 0; i < preset.screens.size() && i < MAX_SCREENS; ++i )
	{
		names.push_back( preset.screens[ i ].name );
		values.push_back( (float)i );
	}
	if( names.empty() )
	{
		names.push_back( "-" );
		values.push_back( 0.0f );
	}
	SetParamElements( index, names, values, true );
	if( params[ index ]->GetValue() >= (float)names.size() )
	{
		params[ index ]->SetValue( 0.0f );
		RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
	}
}

SliceEffect::SelectionUpdate SliceEffect::UpdateSelection()
{
	// The host changes the file or restores the saved exclusions (composition load, undo):
	// the checkboxes follow. Otherwise a checkbox that differs from the exclusions was clicked.
	bool reload   = slicesFile->text != loadedPath;
	bool restored = excludedScreens->text != Join( screenExclusions ) || excludedFamilies->text != Join( familyExclusions ) ||
	                ( familyOffsetsText && familyOffsetsText->text != JoinOffsets( familyOffsets ) );
	if( reload )
	{
		loadedPath = slicesFile->text;
		preset     = Preset();
		std::string error;
		if( !loadedPath.empty() && !LoadPreset( loadedPath, preset, error ) )
			Log( error );
		if( preset.screens.size() > MAX_SCREENS )
			Log( "more screens than checkboxes, the last ones are always on" );
		for( const PresetScreen& screen : preset.screens )
		{
			std::set< std::string > families;
			for( const PresetSlice& slice : screen.slices )
				families.insert( slice.family );
			if( families.size() > MAX_FAMILIES )
				Log( screen.name + ": more slice families than checkboxes, the last ones are always on" );
		}
		ShowScreenNames( familyScreenIndex );
	}
	if( restored )
	{
		screenExclusions = Split( excludedScreens->text );
		familyExclusions = Split( excludedFamilies->text );
		if( familyOffsetsText )
			familyOffsets = ParseOffsets( familyOffsetsText->text );
	}
	// Picking another screen only shows other checkboxes: the family checkboxes still show the
	// previous screen's families, and must not be read as clicked.
	int count    = (int)std::min( preset.screens.size(), (size_t)MAX_SCREENS );
	int picked   = count > 0 ? std::clamp( (int)params[ familyScreenIndex ]->GetValue(), 0, count - 1 ) : -1;
	bool toggled = false;
	if( !reload && !restored )
	{
		toggled |= ReadCheckboxes( firstScreenBox, shownScreens, screenExclusions );
		if( picked == familyScreen )
		{
			toggled |= ReadFamilyCheckboxes();
			toggled |= ReadFamilyOffsets();
		}
	}
	if( !reload && !restored && !toggled && picked == familyScreen )
		return {};

	shownScreens.clear();
	std::vector< bool > screensOn;
	for( const PresetScreen& screen : preset.screens )
	{
		if( shownScreens.size() == MAX_SCREENS )
			break;
		shownScreens.push_back( screen.name );
		screensOn.push_back( IsSelected( screen ) );
	}
	familyScreen = picked;
	shownFamilies.clear();
	std::vector< bool > familiesOn;
	if( picked >= 0 )
	{
		const PresetScreen& screen = preset.screens[ picked ];
		std::set< std::string > families;
		for( const PresetSlice& slice : screen.slices )
			families.insert( slice.family );
		shownFamilies.assign( families.begin(), families.end() );
		shownFamilies.resize( std::min( shownFamilies.size(), (size_t)MAX_FAMILIES ) );
		for( const std::string& family : shownFamilies )
			familiesOn.push_back( IsFamilyOn( screen.name, family ) );
	}

	ShowCheckboxes( firstScreenBox, 1, MAX_SCREENS, shownScreens, screensOn );
	ShowCheckboxes( firstFamilyBox, FamilyStride(), MAX_FAMILIES, shownFamilies, familiesOn );
	ShowFamilyOffsets();
	WriteExclusions( excludedScreensIndex, *excludedScreens, screenExclusions );
	WriteExclusions( excludedFamiliesIndex, *excludedFamilies, familyExclusions );
	if( familyOffsetsText && familyOffsetsText->text != JoinOffsets( familyOffsets ) )
	{
		familyOffsetsText->text = JoinOffsets( familyOffsets );
		RaiseParamEvent( familyOffsetsIndex, FF_EVENT_FLAG_VALUE );
	}
	return { reload, reload || restored || toggled };
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

bool SliceEffect::IsSelected( const PresetScreen& screen, const PresetSlice& slice ) const
{
	return IsFamilyOn( screen.name, slice.family );
}

bool SliceEffect::IsFamilyOn( const std::string& screen, const std::string& family ) const
{
	return familyExclusions.count( family ) == 0 && familyExclusions.count( ScreenFamily( screen, family ) ) == 0;
}

float SliceEffect::FamilyOffset( const PresetScreen& screen, const PresetSlice& slice ) const
{
	return OffsetOf( screen.name, slice.family );
}

float SliceEffect::OffsetOf( const std::string& screen, const std::string& family ) const
{
	auto saved = familyOffsets.find( ScreenFamily( screen, family ) );
	return saved != familyOffsets.end() ? saved->second : 0.0f;
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

bool SliceEffect::ReadFamilyCheckboxes()
{
	if( familyScreen < 0 )
		return false;
	const std::string& screen = preset.screens[ familyScreen ].name;
	bool changed              = false;
	for( size_t i = 0; i < shownFamilies.size(); ++i )
	{
		const std::string& family = shownFamilies[ i ];
		bool checked              = params[ firstFamilyBox + i * FamilyStride() ]->GetValue() > 0.5f;
		if( checked == IsFamilyOn( screen, family ) )
			continue;
		changed = true;
		if( !checked )
		{
			familyExclusions.insert( ScreenFamily( screen, family ) );
			continue;
		}
		familyExclusions.erase( ScreenFamily( screen, family ) );
		// Off on every screen until now: it stays off on the other screens.
		if( familyExclusions.erase( family ) == 0 )
			continue;
		for( const PresetScreen& other : preset.screens )
			if( other.name != screen && HasFamily( other, family ) )
				familyExclusions.insert( ScreenFamily( other.name, family ) );
	}
	return changed;
}

bool SliceEffect::ReadFamilyOffsets()
{
	if( !familyOffsetsText || familyScreen < 0 )
		return false;
	const std::string& screen = preset.screens[ familyScreen ].name;
	bool changed              = false;
	for( size_t i = 0; i < shownFamilies.size(); ++i )
	{
		float offset = params[ firstFamilyBox + i * 2 + 1 ]->GetValue();
		if( offset == OffsetOf( screen, shownFamilies[ i ] ) )
			continue;
		if( offset == 0.0f )
			familyOffsets.erase( ScreenFamily( screen, shownFamilies[ i ] ) );
		else
			familyOffsets[ ScreenFamily( screen, shownFamilies[ i ] ) ] = offset;
		changed = true;
	}
	return changed;
}

void SliceEffect::ShowCheckboxes( unsigned int first, unsigned int stride, int poolSize, const std::vector< std::string >& names, const std::vector< bool >& checked )
{
	for( int i = 0; i < poolSize; ++i )
	{
		unsigned int index = first + i * stride;
		bool shown         = i < (int)names.size();
		SetParamVisibility( index, shown, true );
		if( !shown )
			continue;
		SetParamDisplayName( index, names[ i ], true );
		float value = checked[ i ] ? 1.0f : 0.0f;
		if( params[ index ]->GetValue() != value )
		{
			params[ index ]->SetValue( value );
			RaiseParamEvent( index, FF_EVENT_FLAG_VALUE );
		}
	}
}

void SliceEffect::ShowFamilyOffsets()
{
	if( !familyOffsetsText )
		return;
	for( int i = 0; i < MAX_FAMILIES; ++i )
	{
		unsigned int index = firstFamilyBox + i * 2 + 1;
		bool shown         = i < (int)shownFamilies.size();
		SetParamVisibility( index, shown, true );
		if( !shown )
			continue;
		SetParamDisplayName( index, shownFamilies[ i ] + " Z", true );
		float value = OffsetOf( preset.screens[ familyScreen ].name, shownFamilies[ i ] );
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
