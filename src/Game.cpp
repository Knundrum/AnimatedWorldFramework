#include "Game.h"

#include "Addresses.h"

#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Havok/hkArray.h"

namespace AW::Game
{
	namespace
	{
		using PlayActionFn = bool (*)(RE::Actor*, RE::BGSAction*, RE::TESObjectREFR*, void*, std::uint32_t);
		using ApplySwapFn = void(__fastcall*)(RE::NiAVObject*, const RE::BGSMaterialSwap*, float, float, void*);
		using IsActivationBlockedFn = bool (*)(RE::TESObjectREFR*);
		using WornHasKeywordFn = bool (*)(RE::TESObjectREFR*, RE::BGSKeyword*);
		using PlayPipboyOpenAnimFn = void (*)(RE::PipboyManager*, const RE::BSFixedString&);

		PlayActionFn g_playAction{ nullptr };
		ApplySwapFn g_applySwap{ nullptr };
		IsActivationBlockedFn g_isActivationBlocked{ nullptr };
		WornHasKeywordFn g_wornHasKeyword{ nullptr };
		PlayPipboyOpenAnimFn g_playPipboyOpenAnim{ nullptr };

		template <class F>
		void Bind(F& a_out, std::string_view a_name, const REL::ID& a_id)
		{
			if (const auto address = Addresses::ResolveFunction(a_name, a_id)) {
				a_out = reinterpret_cast<F>(*address);
			}
		}

		// -------------------------------------------------------------------
		// Havok / BSAnimationGraph byte offsets.
		//
		// Verified against 1.10.163 (OG).  The same numbers are used as a
		// starting point for NG/AE but are flagged unverified, which disables
		// the clip walk on those runtimes until someone confirms them.
		// -------------------------------------------------------------------
		struct AnimGraphLayout
		{
			std::ptrdiff_t managerVariableCache;  // BSAnimationGraphManager -> variableCache
			std::ptrdiff_t cacheGraphToCacheFor;  // BSAnimationGraphVariableCache -> graphToCacheFor
			std::ptrdiff_t graphBehaviorGraph;    // BShkbAnimationGraph -> hkbBehaviorGraph
			std::ptrdiff_t behaviorActiveNodes;   // hkbBehaviorGraph -> active node array
			std::ptrdiff_t clipUserData;          // hkbClipGenerator -> user data (non-zero == live)
			std::ptrdiff_t clipName;              // hkbClipGenerator -> name
			std::ptrdiff_t clipLocalTime;         // hkbClipGenerator -> local time
			std::ptrdiff_t clipAnimationControl;  // hkbClipGenerator -> hkaDefaultAnimationControl
			std::ptrdiff_t controlBinding;        // hkaAnimationControl -> hkaAnimationBinding
			std::ptrdiff_t bindingAnimation;      // hkaAnimationBinding -> hkaAnimation
			std::ptrdiff_t animationDuration;     // hkaAnimation -> duration
			bool verified;
		};

		constexpr AnimGraphLayout kLayoutOG{
			.managerVariableCache = 0x88,
			.cacheGraphToCacheFor = 0x38,
			.graphBehaviorGraph = 0x378,
			.behaviorActiveNodes = 0xE0,
			.clipUserData = 0x08,
			.clipName = 0x38,
			.clipLocalTime = 0x140,
			.clipAnimationControl = 0xD0,
			.controlBinding = 0x38,
			.bindingAnimation = 0x18,
			.animationDuration = 0x14,
			.verified = true
		};

		[[nodiscard]] constexpr AnimGraphLayout Unverified(AnimGraphLayout a_layout) noexcept
		{
			a_layout.verified = false;
			return a_layout;
		}

		// Same numbers, not yet confirmed on these runtimes.  Once they have
		// been checked against a real NG/AE binary, drop the Unverified() call.
		constexpr AnimGraphLayout kLayoutNG = Unverified(kLayoutOG);
		constexpr AnimGraphLayout kLayoutAE = Unverified(kLayoutOG);

		[[nodiscard]] const AnimGraphLayout& CurrentLayout() noexcept
		{
			switch (REL::runtime_family(REL::Module::get().version())) {
			case REL::RuntimeFamily::kOG:
				return kLayoutOG;
			case REL::RuntimeFamily::kNG:
				return kLayoutNG;
			case REL::RuntimeFamily::kAE:
			default:
				return kLayoutAE;
			}
		}

		// A behaviour graph should never hold anywhere near this many active
		// generators; the bound stops a corrupt or unexpected array from
		// running the walk off the end of the heap.
		constexpr std::size_t MAX_ACTIVE_GENERATORS = 512;

		template <class T>
		[[nodiscard]] T ReadAt(const void* a_base, std::ptrdiff_t a_offset) noexcept
		{
			return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(a_base) + a_offset);
		}
	}

	bool Initialize()
	{
		Bind(g_playAction, "PlayAction"sv, Addresses::PlayAction);
		Bind(g_applySwap, "ApplyMaterialSwap"sv, Addresses::ApplyMaterialSwap);
		Bind(g_isActivationBlocked, "IsActivationBlocked"sv, Addresses::IsActivationBlocked);
		Bind(g_wornHasKeyword, "WornHasKeyword"sv, Addresses::WornHasKeyword);
		Bind(g_playPipboyOpenAnim, "PlayPipboyOpenAnim"sv, Addresses::PlayPipboyOpenAnim);

		if (!g_playAction) {
			logger::error("PlayAction is unavailable - AnimatedWorld cannot do anything without it");
			return false;
		}

		return true;
	}

	bool PlayAction(RE::Actor* a_actor, RE::BGSAction* a_action, RE::TESObjectREFR* a_target)
	{
		if (!g_playAction || !a_actor || !a_action || !a_target) {
			return false;
		}

		return g_playAction(a_actor, a_action, a_target, nullptr, 0);
	}

	bool CanApplyMaterialSwap() noexcept
	{
		return g_applySwap != nullptr;
	}

	void ApplyMaterialSwap(RE::NiAVObject* a_object, const RE::BGSMaterialSwap* a_swap)
	{
		if (!g_applySwap || !a_object || !a_swap) {
			return;
		}

		g_applySwap(a_object, a_swap, 1.0f, 1.0f, nullptr);
	}

	bool CanTestActivationBlocked() noexcept
	{
		return g_isActivationBlocked != nullptr;
	}

	bool IsActivationBlocked(RE::TESObjectREFR* a_ref)
	{
		if (!g_isActivationBlocked || !a_ref) {
			return false;
		}

		return g_isActivationBlocked(a_ref);
	}

	bool CanTestWornKeyword() noexcept
	{
		return g_wornHasKeyword != nullptr;
	}

	bool WornHasKeyword(RE::TESObjectREFR* a_ref, RE::BGSKeyword* a_keyword)
	{
		if (!g_wornHasKeyword || !a_ref || !a_keyword) {
			return false;
		}

		return g_wornHasKeyword(a_ref, a_keyword);
	}

	bool CanReopenPipboy() noexcept
	{
		return g_playPipboyOpenAnim != nullptr;
	}

	void PlayPipboyOpenAnim(RE::PipboyManager* a_manager, const RE::BSFixedString& a_menuName)
	{
		if (!g_playPipboyOpenAnim || !a_manager) {
			return;
		}

		g_playPipboyOpenAnim(a_manager, a_menuName);
	}

	bool CanReadClipInfo() noexcept
	{
		return CurrentLayout().verified;
	}

	bool ReadCurrentClip(RE::Actor* a_actor, ClipInfo& a_out)
	{
		a_out = {};

		const auto& layout = CurrentLayout();
		if (!layout.verified || !a_actor) {
			return false;
		}

		const auto* process = a_actor->currentProcess;
		if (!process || !process->middleHigh) {
			return false;
		}

		const auto* graphManager = process->middleHigh->animationGraphManager.get();
		if (!graphManager) {
			return false;
		}

		const auto* graph = ReadAt<const void*>(graphManager, layout.managerVariableCache + layout.cacheGraphToCacheFor);
		if (!graph) {
			return false;
		}

		const auto* behaviorGraph = ReadAt<const void*>(graph, layout.graphBehaviorGraph);
		if (!behaviorGraph) {
			return false;
		}

		using NodeArray = RE::hkArray<void**>;
		const auto* activeNodes = ReadAt<const NodeArray*>(behaviorGraph, layout.behaviorActiveNodes);
		if (!activeNodes || activeNodes->_size <= 0 || !activeNodes->_data) {
			return false;
		}

		void** generator = *activeNodes->_data;
		if (!generator) {
			return false;
		}

		// The original walk assigned the clip name for EVERY live generator and
		// only bailed out early once the whole animation-binding chain resolved,
		// so a generator could be identified by name even when its duration was
		// not reachable.  Requiring the full chain here silently lost the
		// "DynamicIdle" match that drives the item-added animation, so the looser
		// behaviour is reproduced - minus the original's use of a stale duration.
		bool sawLiveGenerator = false;
		std::string lastName;
		float lastTime = 0.0f;

		for (std::size_t i = 0; i < MAX_ACTIVE_GENERATORS && *generator; ++i, ++generator) {
			const auto* clip = *generator;

			// Zero user data means the generator is not currently driving a clip.
			if (ReadAt<std::uint32_t>(clip, layout.clipUserData) == 0) {
				continue;
			}

			const auto localTime = ReadAt<float>(clip, layout.clipLocalTime);
			const auto* name = ReadAt<const char*>(clip, layout.clipName);

			sawLiveGenerator = true;
			lastTime = localTime;
			if (name) {
				lastName = name;
			}

			if (localTime == 0.0f) {
				continue;
			}

			const auto* control = ReadAt<const void*>(clip, layout.clipAnimationControl);
			if (!control) {
				continue;
			}

			const auto* binding = ReadAt<const void*>(control, layout.controlBinding);
			if (!binding) {
				continue;
			}

			const auto* animation = ReadAt<const void*>(binding, layout.bindingAnimation);
			if (!animation) {
				continue;
			}

			a_out.currentTime = localTime;
			a_out.duration = ReadAt<float>(animation, layout.animationDuration);
			a_out.name = lastName;
			a_out.durationKnown = true;
			return true;
		}

		// A live generator was found but its duration could not be read.  The
		// name is still usable for deciding what is playing.
		if (sawLiveGenerator && !lastName.empty()) {
			a_out.currentTime = lastTime;
			a_out.duration = 0.0f;
			a_out.name = lastName;
			a_out.durationKnown = false;
			return true;
		}

		return false;
	}
}
