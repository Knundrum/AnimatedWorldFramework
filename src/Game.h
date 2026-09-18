#pragma once

#include "RE/Bethesda/Actor.h"
#include "RE/Bethesda/PipboyManager.h"
#include "RE/Bethesda/TESForms.h"
#include "RE/Bethesda/TESObjectREFRs.h"

#include <string>

namespace AW::Game
{
	[[nodiscard]] bool Initialize();

	[[nodiscard]] bool PlayAction(RE::Actor* a_actor, RE::BGSAction* a_action, RE::TESObjectREFR* a_target);

	[[nodiscard]] bool CanApplyMaterialSwap() noexcept;
	void ApplyMaterialSwap(RE::NiAVObject* a_object, const RE::BGSMaterialSwap* a_swap);

	[[nodiscard]] bool CanTestActivationBlocked() noexcept;
	[[nodiscard]] bool IsActivationBlocked(RE::TESObjectREFR* a_ref);

	[[nodiscard]] bool CanTestWornKeyword() noexcept;
	[[nodiscard]] bool WornHasKeyword(RE::TESObjectREFR* a_ref, RE::BGSKeyword* a_keyword);

	[[nodiscard]] bool CanReopenPipboy() noexcept;
	void PlayPipboyOpenAnim(RE::PipboyManager* a_manager, const RE::BSFixedString& a_menuName);

	struct ClipInfo
	{
		float currentTime{ 0.0f };
		float duration{ 0.0f };
		std::string name{};

		bool durationKnown{ false };
	};

	[[nodiscard]] bool CanReadClipInfo() noexcept;
	[[nodiscard]] bool ReadCurrentClip(RE::Actor* a_actor, ClipInfo& a_out);
}
