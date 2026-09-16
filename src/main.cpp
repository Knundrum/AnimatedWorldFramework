#include "Plugin.h"

// OG, NG and AE all enter here.  There is no F4SEPlugin_Query and no
// compatibleVersions whitelist: F4SEPlugin_Version (see Plugin.cpp) declares
// address and structure independence instead, and CommonLibF4RD decides at
// runtime whether the addresses this plugin needs exist.
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	return AW::Plugin::Initialize(a_f4se);
}
