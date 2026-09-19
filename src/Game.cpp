#include "Game.h"

#include "Addresses.h"

#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Havok/hkArray.h"
#include "RE/VTABLE_IDs.h"

namespace AW::Game
{
	namespace
	{
		using PlayActionFn = bool (*)(RE::Actor*, RE::BGSAction*, RE::TESObjectREFR*);
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
				logger::debug("bound {} at {:#x}", a_name, *address);
			} else {
				logger::debug("binding unavailable: {}", a_name);
			}
		}

		struct AnimGraphLayout
		{
			std::ptrdiff_t managerVariableCache;
			std::ptrdiff_t cacheGraphToCacheFor;
			std::ptrdiff_t graphBehaviorGraph;
			std::ptrdiff_t behaviorActiveNodes;
			std::ptrdiff_t clipUserData;
			std::ptrdiff_t clipName;
			std::ptrdiff_t clipLocalTime;
			std::ptrdiff_t clipAnimationControl;
			std::ptrdiff_t controlBinding;
			std::ptrdiff_t bindingAnimation;
			std::ptrdiff_t animationDuration;
			bool verified;
		};

		constexpr AnimGraphLayout kLayoutOG{
			.managerVariableCache = 0x88,
			.cacheGraphToCacheFor = 0x38,
			.graphBehaviorGraph = 0x378,
			.behaviorActiveNodes = 0xE0,
			.clipUserData = 0x30,
			.clipName = 0x38,
			.clipLocalTime = 0x140,
			.clipAnimationControl = 0xD0,
			.controlBinding = 0x38,
			.bindingAnimation = 0x18,
			.animationDuration = 0x14,
			.verified = true
		};

		constexpr AnimGraphLayout kLayoutNG = kLayoutOG;
		constexpr AnimGraphLayout kLayoutAE = kLayoutOG;
		constexpr AnimGraphLayout kLayoutUnverified{
			.managerVariableCache = 0,
			.cacheGraphToCacheFor = 0,
			.graphBehaviorGraph = 0,
			.behaviorActiveNodes = 0,
			.clipUserData = 0,
			.clipName = 0,
			.clipLocalTime = 0,
			.clipAnimationControl = 0,
			.controlBinding = 0,
			.bindingAnimation = 0,
			.animationDuration = 0,
			.verified = false
		};

		[[nodiscard]] const AnimGraphLayout& CurrentLayout() noexcept
		{
			if (!Addresses::IsVerifiedRuntime()) {
				return kLayoutUnverified;
			}

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

		constexpr std::size_t MAX_ACTIVE_NODES = 512;

		struct ActiveNodeInfo
		{
			std::uint8_t unknown[0x58];
			const void* nodeClone;
		};
		static_assert(offsetof(ActiveNodeInfo, nodeClone) == 0x58);

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
		if (Addresses::IsVerifiedRuntime()) {
			Bind(g_isActivationBlocked, "IsActivationBlocked"sv, Addresses::IsActivationBlocked);
			Bind(g_wornHasKeyword, "WornHasKeyword"sv, Addresses::WornHasKeyword);
		} else {
			static bool reported = false;
			if (!reported) {
				reported = true;
				logger::warn(
					"native TESObjectREFR helper bindings skipped on unverified runtime {}",
					REL::Module::get().version().string());
			}
		}
		Bind(g_playPipboyOpenAnim, "PlayPipboyOpenAnim"sv, Addresses::PlayPipboyOpenAnim);
		logger::debug(
			"game bindings playAction={} materialSwap={} activationBlocked={} wornKeyword={} pipboyAnim={}",
			g_playAction != nullptr,
			g_applySwap != nullptr,
			g_isActivationBlocked != nullptr,
			g_wornHasKeyword != nullptr,
			g_playPipboyOpenAnim != nullptr);

		if (!g_playAction) {
			logger::error("PlayAction is unavailable - AnimatedWorld cannot do anything without it");
			return false;
		}

		return true;
	}

	bool PlayAction(RE::Actor* a_actor, RE::BGSAction* a_action, RE::TESObjectREFR* a_target)
	{
		if (!g_playAction || !a_actor || !a_action || !a_target) {
			logger::debug(
				"PlayAction skipped actor={} action={} target={} bound={}",
				a_actor != nullptr,
				a_action != nullptr,
				a_target != nullptr,
				g_playAction != nullptr);
			return false;
		}

		logger::debug("PlayAction actor={} action={} target={}",
			a_actor->formID,
			a_action->formID,
			a_target->formID);
		return g_playAction(a_actor, a_action, a_target);
	}

	bool CanApplyMaterialSwap() noexcept
	{
		return g_applySwap != nullptr;
	}

	void ApplyMaterialSwap(RE::NiAVObject* a_object, const RE::BGSMaterialSwap* a_swap)
	{
		if (!g_applySwap || !a_object || !a_swap) {
			logger::debug(
				"ApplyMaterialSwap skipped object={} swap={} bound={}",
				a_object != nullptr,
				a_swap != nullptr,
				g_applySwap != nullptr);
			return;
		}

		logger::debug("ApplyMaterialSwap object={:#x} swap={:#x}",
			reinterpret_cast<std::uintptr_t>(a_object),
			reinterpret_cast<std::uintptr_t>(a_swap));
		g_applySwap(a_object, a_swap, 1.0f, 1.0f, nullptr);
	}

	bool CanTestActivationBlocked() noexcept
	{
		return g_isActivationBlocked != nullptr;
	}

	bool IsActivationBlocked(RE::TESObjectREFR* a_ref)
	{
		if (!g_isActivationBlocked || !a_ref) {
			logger::debug(
				"IsActivationBlocked skipped ref={} bound={}",
				a_ref != nullptr,
				g_isActivationBlocked != nullptr);
			return false;
		}

		const auto blocked = g_isActivationBlocked(a_ref);
		logger::debug("IsActivationBlocked ref={} result={}", a_ref->formID, blocked);
		return blocked;
	}

	bool CanTestWornKeyword() noexcept
	{
		return g_wornHasKeyword != nullptr;
	}

	bool WornHasKeyword(RE::TESObjectREFR* a_ref, RE::BGSKeyword* a_keyword)
	{
		if (!g_wornHasKeyword || !a_ref || !a_keyword) {
			logger::debug(
				"WornHasKeyword skipped ref={} keyword={} bound={}",
				a_ref != nullptr,
				a_keyword != nullptr,
				g_wornHasKeyword != nullptr);
			return false;
		}

		const auto result = g_wornHasKeyword(a_ref, a_keyword);
		logger::debug("WornHasKeyword ref={} keyword={} result={}", a_ref->formID, a_keyword->formID, result);
		return result;
	}

	bool CanReopenPipboy() noexcept
	{
		return g_playPipboyOpenAnim != nullptr;
	}

	void PlayPipboyOpenAnim(RE::PipboyManager* a_manager, const RE::BSFixedString& a_menuName)
	{
		if (!g_playPipboyOpenAnim || !a_manager) {
			logger::debug(
				"PlayPipboyOpenAnim skipped manager={} bound={}",
				a_manager != nullptr,
				g_playPipboyOpenAnim != nullptr);
			return;
		}

		logger::debug("PlayPipboyOpenAnim menu={}", a_menuName.c_str());
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

		static const auto clipGeneratorVTable =
			Addresses::ResolveFunction("hkbClipGenerator vtable"sv, RE::VTABLE::hkbClipGenerator.front());
		if (!clipGeneratorVTable) {
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

		using NodeArray = RE::hkArray<ActiveNodeInfo*>;
		const auto* activeNodes = ReadAt<const NodeArray*>(behaviorGraph, layout.behaviorActiveNodes);
		if (!activeNodes || activeNodes->_size <= 0 || !activeNodes->_data) {
			return false;
		}

		bool sawLiveGenerator = false;
		std::string lastName;
		float lastTime = 0.0f;

		for (std::int32_t i = 0;
			i < activeNodes->_size && i < static_cast<std::int32_t>(MAX_ACTIVE_NODES);
			++i) {
			const auto* nodeInfo = activeNodes->_data[i];
			if (!nodeInfo || !nodeInfo->nodeClone) {
				continue;
			}

			const auto* clip = nodeInfo->nodeClone;
			if (ReadAt<const void*>(clip, 0) != reinterpret_cast<const void*>(*clipGeneratorVTable)) {
				continue;
			}

			if (ReadAt<std::uint64_t>(clip, layout.clipUserData) == 0) {
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
