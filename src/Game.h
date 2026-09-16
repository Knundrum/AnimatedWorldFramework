#pragma once

// ---------------------------------------------------------------------------
// Thin wrappers over engine functions, plus the two APIs the old in-tree
// CommonLibF4 fork carried that upstream CommonLibF4RD does not
// (TESObjectREFR::WornHasKeyword and PipboyManager::PlayPipboyOpenAnim).
//
// Everything here is optional: if an address could not be resolved on the
// running runtime the wrapper is inert and the matching Can*() query says so,
// rather than the process dying inside REL::Relocation's constructor.
// ---------------------------------------------------------------------------

#include "RE/Bethesda/Actor.h"
#include "RE/Bethesda/PipboyManager.h"
#include "RE/Bethesda/TESForms.h"
#include "RE/Bethesda/TESObjectREFRs.h"

#include <string>

namespace AW::Game
{
	// Resolves every engine address once.  Returns false when something the
	// plugin cannot work without is missing.
	[[nodiscard]] bool Initialize();

	// bool PlayAction(Actor*, BGSAction*, TESObjectREFR*, void*, uint32_t)
	[[nodiscard]] bool PlayAction(RE::Actor* a_actor, RE::BGSAction* a_action, RE::TESObjectREFR* a_target);

	[[nodiscard]] bool CanApplyMaterialSwap() noexcept;
	void ApplyMaterialSwap(RE::NiAVObject* a_object, const RE::BGSMaterialSwap* a_swap);

	[[nodiscard]] bool CanTestActivationBlocked() noexcept;
	[[nodiscard]] bool IsActivationBlocked(RE::TESObjectREFR* a_ref);

	[[nodiscard]] bool CanTestWornKeyword() noexcept;
	[[nodiscard]] bool WornHasKeyword(RE::TESObjectREFR* a_ref, RE::BGSKeyword* a_keyword);

	[[nodiscard]] bool CanReopenPipboy() noexcept;
	void PlayPipboyOpenAnim(RE::PipboyManager* a_manager, const RE::BSFixedString& a_menuName);

	// -----------------------------------------------------------------------
	// Animation clip timing.
	//
	// This walks raw Havok structures by byte offset.  Runtime-aware addresses
	// do NOT make class layouts portable - that is the "structureIndependence"
	// caveat in CommonLibF4RD's docs - so the offsets are only trusted on the
	// runtime families they were verified against.  Where they are not
	// verified, ReadCurrentClip() reports failure and the caller falls back to
	// a fixed delay instead of dereferencing guesses.
	// -----------------------------------------------------------------------

	struct ClipInfo
	{
		float currentTime{ 0.0f };
		float duration{ 0.0f };
		std::string name{};

		// An active generator can be identified by name before its animation
		// binding is reachable.  When this is false, `name` is trustworthy but
		// `duration` is not, and the caller must not compute a remaining time
		// from it.
		bool durationKnown{ false };
	};

	[[nodiscard]] bool CanReadClipInfo() noexcept;
	[[nodiscard]] bool ReadCurrentClip(RE::Actor* a_actor, ClipInfo& a_out);
}
