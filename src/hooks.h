#pragma once

extern RE::PlayerCharacter* playerRef;

namespace HookLineAndSinker
{
	bool HookedActorCheckValidTarget(RE::AIProcess**, RE::TESObjectREFR*);

	void RegisterHook();
}
