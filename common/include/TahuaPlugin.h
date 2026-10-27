#pragma once
#include <FFGLSDK.h>

namespace tahua
{
// Plugin identity (id, display name, version) is defined once, by tahua_add_plugin()
// in the plugin's CMakeLists.txt, and reaches the code through these macros.
inline ffglqs::PluginInfo MakePluginInfo( const char* description )
{
	ffglqs::PluginInfo info( TAHUA_PLUGIN_ID, TAHUA_PLUGIN_NAME );
	info.description  = description;
	info.about        = "Tahua plugins";
	info.majorVersion = TAHUA_PLUGIN_VERSION_MAJOR;
	info.minorVersion = TAHUA_PLUGIN_VERSION_MINOR;
	return info;
}

}//End namespace tahua
