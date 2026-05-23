#pragma once

extern RE::PlayerCharacter* playerRef;

namespace HookLineAndSinker
{

	void RegisterHook();
	void InstallVTableHook();
	void EnsureTargetExists();

}
