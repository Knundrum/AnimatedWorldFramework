#include "Hooks.h"

#include "Addresses.h"
#include "Config.h"
#include "Diagnostics.h"
#include "EntryHooks.h"
#include "Game.h"

#include "RE/Bethesda/BSInputDeviceManager.h"
#include "RE/Bethesda/FormFactory.h"
#include "RE/Bethesda/PipboyManager.h"
#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESBoundObjects.h"
#include "RE/Bethesda/TESForms.h"
#include "RE/Bethesda/UI.h"

namespace AW::Hooks
{
	namespace
	{
		using namespace std::chrono_literals;

		constexpr auto ESP_NAME = "Animated World - Base.esp"sv;

		constexpr std::uint32_t FORMID_IDLE_STOP_FIX = 0x34D3A;
		constexpr std::uint32_t FORMID_ACTION_ACTIVATE = 0x12196;
		constexpr std::uint32_t FORMID_ACTION_ITEM_ADDED = 0x2B4D9;
		constexpr std::uint32_t FORMID_ACTION_EQUIP_ANIM = 0x18481;
		constexpr std::uint32_t FORMID_ACTION_FLASHLIGHT = 0x14F3E;
		constexpr std::uint32_t FORMID_GLOBAL_PIPBOY_EQUIP = 0x399BC;

		constexpr auto ANIMATION_SETTLE_DELAY = 100ms;
		constexpr auto GROUND_PICKUP_DELAY = 100ms;
		constexpr auto MATERIAL_SWAP_DELAY = 200ms;
		constexpr auto DYNAMIC_IDLE_TAIL = 200ms;

		constexpr auto CLIP_UNREADABLE_FALLBACK = 700ms;

		constexpr auto IDLE_STOP_TIMEOUT = 2s;

		constexpr auto ANIMATION_PENDING_TIMEOUT = 3s;

		constexpr auto DYNAMIC_IDLE_CLIP = "DynamicIdle"sv;
		constexpr auto EVENT_IDLE_STOP = "IdleStop"sv;
		constexpr auto EVENT_REEVALUATE = "ReevaluateGraphState"sv;
		constexpr auto MENU_PIPBOY = "PipboyMenu"sv;
		constexpr auto PIPBOY_INVENTORY_ANIM = "PipboyInv"sv;

		using Clock = std::chrono::steady_clock;

		RE::BGSKeyword* g_idleStopFixKeyword{ nullptr };
		RE::BGSAction* g_actionActivate{ nullptr };
		RE::BGSAction* g_actionItemAdded{ nullptr };
		RE::BGSAction* g_actionEquipAnim{ nullptr };
		RE::BGSAction* g_actionFlashlight{ nullptr };
		RE::TESGlobal* g_globalPipboyEquipAnims{ nullptr };

		RE::TESObjectREFR* g_playerTarget{ nullptr };
		RE::TESObjectREFR* g_npcTarget{ nullptr };

		bool g_formsReady{ false };

		bool g_reopenPipboy{ false };

		bool g_idleStopFixArmed{ false };
		Clock::time_point g_idleStopFixExpiry{};

		bool g_animationPending{ false };
		Clock::time_point g_animationDeadline{};
		Clock::time_point g_animationExpiry{};

		bool g_animationSoon{ false };
		Clock::time_point g_animationReady{};

		bool g_matSwapPending{ false };
		Clock::time_point g_matSwapDeadline{};

		bool g_itemFromGround{ false };

		RE::BGSMaterialSwap* g_pendingSwap{ nullptr };
		RE::TESBoundObject* g_pendingSwapItem{ nullptr };

		bool g_graphEventHooked{ false };

		bool g_trace{ false };

		std::string g_lastTracedClip;

		using FnRunActorUpdates = void(__fastcall*)(void*);
		using FnAddAcquiredEvent = void(__fastcall*)(RE::PlayerCharacter*, RE::TESBoundObject*, RE::TESForm*, RE::TESObjectREFR*, std::int32_t);
		using FnActivateRef = bool(__fastcall*)(RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESBoundObject*, int, bool, bool, bool);
		using FnHandlePlayerItem = void(__fastcall*)(RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t);
		using FnUseObject = bool(__fastcall*)(
			RE::ActorEquipManager*,
			RE::Actor*,
			const RE::BGSObjectInstance*,
			void*);
		using FnSetInputDeviceLightState = void(__fastcall*)(RE::BSInputDeviceManager*, std::uint32_t, bool);
		using FnProcessGraphEvent = RE::BSEventNotifyControl(__fastcall*)(
			RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
			const RE::BSAnimationGraphEvent&,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>*);

		FnRunActorUpdates g_origRunActorUpdates{ nullptr };
		FnAddAcquiredEvent g_origAddAcquiredEvent{ nullptr };
		FnActivateRef g_origActivateRef{ nullptr };
		FnHandlePlayerItem g_origHandlePlayerItem{ nullptr };
		FnUseObject g_origUseObject{ nullptr };
		FnSetInputDeviceLightState g_origSetInputDeviceLightState{ nullptr };
		FnProcessGraphEvent g_origProcessGraphEvent{ nullptr };

		[[nodiscard]] RE::TESObjectREFR* CreateDummyReference()
		{
			auto* factory =
				RE::ConcreteFormFactory<RE::TESObjectREFR, RE::ENUM_FORM_ID::kREFR>::GetFormFactory();
			return factory ? factory->Create() : nullptr;
		}

		bool EnsureFormsResolved()
		{
			if (g_formsReady) {
				return true;
			}

			logger::debug("resolving plugin forms");
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				logger::debug("plugin forms unavailable: TESDataHandler is null");
				return false;
			}

			if (!g_playerTarget) {
				g_playerTarget = CreateDummyReference();
			}
			if (!g_npcTarget) {
				g_npcTarget = CreateDummyReference();
			}

			if (!g_idleStopFixKeyword) {
				g_idleStopFixKeyword = dataHandler->LookupForm<RE::BGSKeyword>(FORMID_IDLE_STOP_FIX, ESP_NAME);
			}
			if (!g_actionActivate) {
				g_actionActivate = dataHandler->LookupForm<RE::BGSAction>(FORMID_ACTION_ACTIVATE, ESP_NAME);
			}
			if (!g_actionItemAdded) {
				g_actionItemAdded = dataHandler->LookupForm<RE::BGSAction>(FORMID_ACTION_ITEM_ADDED, ESP_NAME);
			}
			if (!g_actionEquipAnim) {
				g_actionEquipAnim = dataHandler->LookupForm<RE::BGSAction>(FORMID_ACTION_EQUIP_ANIM, ESP_NAME);
			}
			if (!g_actionFlashlight) {
				g_actionFlashlight = dataHandler->LookupForm<RE::BGSAction>(FORMID_ACTION_FLASHLIGHT, ESP_NAME);
			}
			if (!g_globalPipboyEquipAnims) {
				g_globalPipboyEquipAnims = dataHandler->LookupForm<RE::TESGlobal>(FORMID_GLOBAL_PIPBOY_EQUIP, ESP_NAME);
			}

			g_formsReady =
				g_playerTarget && g_npcTarget &&
				g_actionActivate && g_actionItemAdded && g_actionEquipAnim && g_actionFlashlight;

			static bool reported = false;
			if (g_formsReady && !reported) {
				reported = true;
				logger::info(
					"forms resolved: activate={} itemAdded={} equip={} flashlight={} keyword={} pipboyGlobal={}",
					g_actionActivate != nullptr,
					g_actionItemAdded != nullptr,
					g_actionEquipAnim != nullptr,
					g_actionFlashlight != nullptr,
					g_idleStopFixKeyword != nullptr,
					g_globalPipboyEquipAnims != nullptr);
			}
			logger::debug(
				"form resolution ready={} playerTarget={} npcTarget={} activate={} itemAdded={} equip={} flashlight={} keyword={} pipboyGlobal={}",
				g_formsReady,
				g_playerTarget != nullptr,
				g_npcTarget != nullptr,
				g_actionActivate != nullptr,
				g_actionItemAdded != nullptr,
				g_actionEquipAnim != nullptr,
				g_actionFlashlight != nullptr,
				g_idleStopFixKeyword != nullptr,
				g_globalPipboyEquipAnims != nullptr);

			return g_formsReady;
		}

		void ArmIdleStopFix()
		{
			g_idleStopFixArmed = true;
			g_idleStopFixExpiry = Clock::now() + IDLE_STOP_TIMEOUT;
			logger::debug("idle-stop fix armed");
		}

		void ArmAnimation()
		{
			if (g_trace) {
				logger::info("[aw] animation armed (waiting for the activate animation to finish)");
			}

			const auto now = Clock::now();
			g_animationDeadline = now + ANIMATION_SETTLE_DELAY;
			g_animationExpiry = now + ANIMATION_PENDING_TIMEOUT;
			g_animationPending = true;
		}

		[[nodiscard]] RE::BGSMaterialSwap* SwapFormFor(RE::TESBoundObject* a_item)
		{
			if (!a_item) {
				return nullptr;
			}

			switch (a_item->formType.get()) {
			case RE::ENUM_FORM_ID::kBOOK:
				return static_cast<RE::TESObjectBOOK*>(a_item)->swapForm;
			case RE::ENUM_FORM_ID::kMISC:
				return static_cast<RE::TESObjectMISC*>(a_item)->swapForm;
			default:
				return nullptr;
			}
		}

		void ClearPendingSwap()
		{
			g_pendingSwap = nullptr;
			g_pendingSwapItem = nullptr;
		}

		[[nodiscard]] bool PipboyMenuOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->GetMenuOpen(RE::BSFixedString{ MENU_PIPBOY });
		}

		RE::BSEventNotifyControl HookedProcessGraphEvent(
			RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
			const RE::BSAnimationGraphEvent& a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
		{
			if (!g_origProcessGraphEvent) {
				logger::debug("animation graph event hook has no original function");
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (a_this && player &&
				static_cast<RE::BSTEventSink<RE::BSAnimationGraphEvent>*>(player) == a_this) {
				const auto* tag = a_event.animEvent.c_str();
				const auto event = tag ? std::string_view{ tag } : std::string_view{};
				logger::debug(
					"animation graph event={} idleStopArmed={} reopenPipboy={}",
					event,
					g_idleStopFixArmed,
					g_reopenPipboy);

				if (event == EVENT_IDLE_STOP && g_idleStopFixArmed) {
					const bool holdsFixedItem =
						g_idleStopFixKeyword &&
						Game::CanTestWornKeyword() &&
						Game::WornHasKeyword(player, g_idleStopFixKeyword);

					player->UpdateAnimation(holdsFixedItem ? 2.0f : 0.0f);
					g_idleStopFixArmed = false;
				} else if (event == EVENT_REEVALUATE && g_reopenPipboy) {
					g_reopenPipboy = false;

					auto* pipboy = RE::PipboyManager::GetSingleton();
					if (pipboy && PipboyMenuOpen()) {
						Game::PlayPipboyOpenAnim(pipboy, RE::BSFixedString{ PIPBOY_INVENTORY_ANIM });
					}
				}
			}

			return g_origProcessGraphEvent(a_this, a_event, a_source);
		}

		void InstallGraphEventHook()
		{
			if (g_graphEventHooked) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetFullyLoaded3D()) {
				logger::debug("animation graph hook waiting for loaded player 3D");
				return;
			}

			using Sink = RE::BSTEventSink<RE::BSAnimationGraphEvent>;
			auto* sink = static_cast<Sink*>(player);
			if (!sink) {
				return;
			}

			auto* vtable = *reinterpret_cast<std::uintptr_t**>(sink);
			if (!vtable) {
				return;
			}

			constexpr std::size_t PROCESS_EVENT_SLOT = 1;

			g_origProcessGraphEvent =
				reinterpret_cast<FnProcessGraphEvent>(vtable[PROCESS_EVENT_SLOT]);
			if (!g_origProcessGraphEvent) {
				logger::debug("animation graph hook has no original vtable entry");
				return;
			}

			REL::safe_write(
				reinterpret_cast<std::uintptr_t>(std::addressof(vtable[PROCESS_EVENT_SLOT])),
				reinterpret_cast<std::uintptr_t>(&HookedProcessGraphEvent));

			g_graphEventHooked = true;
			logger::info("player animation graph event sink hooked");
		}

		void __fastcall HookedRunActorUpdates(void* a_this)
		{
			g_origRunActorUpdates(a_this);

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetFullyLoaded3D()) {
				return;
			}

			if (!player->currentProcess || !player->currentProcess->middleHigh) {
				return;
			}

			InstallGraphEventHook();

			if (!EnsureFormsResolved()) {
				return;
			}

			const auto now = Clock::now();

			if (g_idleStopFixArmed && now >= g_idleStopFixExpiry) {
				g_idleStopFixArmed = false;
			}

			if (g_animationPending && now >= g_animationExpiry) {
				if (g_trace) {
					logger::info("[aw] pending animation timed out without a usable clip");
				}
				g_animationPending = false;
				g_itemFromGround = false;
				ClearPendingSwap();
			}

			if (g_animationPending && !g_matSwapPending && now >= g_animationDeadline) {
				Game::ClipInfo clip;
				const bool haveClip = Game::ReadCurrentClip(player, clip);

				if (g_trace && clip.name != g_lastTracedClip) {
					g_lastTracedClip = clip.name;
					logger::info(
						"[aw] clip: have={} name='{}' t={:.3f} dur={:.3f} durKnown={}",
						haveClip,
						clip.name,
						clip.currentTime,
						clip.duration,
						clip.durationKnown);
				}

				bool arm = false;
				auto delay = GROUND_PICKUP_DELAY;
				const char* reason = "";

				if (g_itemFromGround) {
					arm = true;
					delay = GROUND_PICKUP_DELAY;
					reason = "itemFromGround";
				}

				if (haveClip && clip.name == DYNAMIC_IDLE_CLIP) {
					if (clip.durationKnown) {
						const auto remaining =
							std::chrono::milliseconds{
								static_cast<std::int64_t>((clip.duration - clip.currentTime) * 1000.0f)
							} +
							DYNAMIC_IDLE_TAIL;

						arm = true;
						delay = remaining.count() > 0 ? remaining : DYNAMIC_IDLE_TAIL;
						reason = "DynamicIdle";
					} else {
						arm = false;
						reason = "";
					}
				}

				if (!arm && !Game::CanReadClipInfo()) {
					arm = true;
					delay = CLIP_UNREADABLE_FALLBACK;
					reason = "clip walk unavailable";
				}

				if (arm) {
					g_animationPending = false;
					g_itemFromGround = false;
					g_animationSoon = true;
					g_animationReady = now + delay;
					if (g_trace) {
						logger::info("[aw] armed via {}, +{}ms", reason, delay.count());
					}
				}
			}

			if (g_animationSoon && !g_matSwapPending && now >= g_animationReady) {
				g_animationSoon = false;
				g_lastTracedClip.clear();

				if (g_actionItemAdded && g_playerTarget) {
					const bool played = Game::PlayAction(player, g_actionItemAdded, g_playerTarget);
					if (g_trace) {
						logger::info(
							"[aw] item-added action played={} target item={:08X}",
							played,
							g_playerTarget->data.objectReference ?
								g_playerTarget->data.objectReference->formID :
								0u);
					}

					const bool swapMatchesTarget =
						g_pendingSwap &&
						g_pendingSwapItem &&
						g_playerTarget->data.objectReference == g_pendingSwapItem;

					if (swapMatchesTarget && Game::CanApplyMaterialSwap()) {
						g_matSwapPending = true;
						g_matSwapDeadline = now + MATERIAL_SWAP_DELAY;
					} else {
						ClearPendingSwap();
					}
				}
			}

			if (g_matSwapPending && now >= g_matSwapDeadline) {
				g_matSwapPending = false;

				if (g_pendingSwap) {
					Game::ApplyMaterialSwap(player->Get3D(), g_pendingSwap);
				}

				ClearPendingSwap();
			}
		}

		bool __fastcall HookedActivateRef(
			RE::TESObjectREFR* a_target,
			RE::TESObjectREFR* a_activator,
			RE::TESBoundObject* a_item,
			int a_count,
			bool a_force,
			bool a_silent,
			bool a_other)
		{
			logger::debug(
				"ActivateRef target={} activator={} item={} count={} force={} silent={}",
				a_target ? a_target->formID : 0,
				a_activator ? a_activator->formID : 0,
				a_item ? a_item->formID : 0,
				a_count,
				a_force,
				a_silent);
			const auto callOriginal = [&] {
				return g_origActivateRef(a_target, a_activator, a_item, a_count, a_force, a_silent, a_other);
			};

			if (Game::IsActivationBlocked(a_target)) {
				logger::debug("ActivateRef skipped because activation is blocked");
				return callOriginal();
			}

			g_itemFromGround = false;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && a_target && EnsureFormsResolved() && g_actionActivate) {
				if (player->weaponState != RE::WEAPON_STATE::kSheathed) {
					ArmIdleStopFix();
				}

				if (Game::PlayAction(player, g_actionActivate, a_target)) {
					g_itemFromGround = true;
				}
			}
			logger::debug("ActivateRef animation armed fromGround={}", g_itemFromGround);

			return callOriginal();
		}

		void __fastcall HookedAddAcquiredEvent(
			RE::PlayerCharacter* a_player,
			RE::TESBoundObject* a_item,
			RE::TESForm* a_source,
			RE::TESObjectREFR* a_container,
			std::int32_t a_acquireType)
		{
			logger::debug(
				"AddAcquiredEvent player={} item={} source={} container={} acquireType={}",
				a_player ? a_player->formID : 0,
				a_item ? a_item->formID : 0,
				a_source ? a_source->formID : 0,
				a_container ? a_container->formID : 0,
				a_acquireType);
			const auto callOriginal = [&] {
				g_origAddAcquiredEvent(a_player, a_item, a_source, a_container, a_acquireType);
			};

			auto* player = RE::PlayerCharacter::GetSingleton();

			if (!player || !a_item || !EnsureFormsResolved() || !g_playerTarget || !g_actionActivate) {
				logger::debug("AddAcquiredEvent skipped because required state is unavailable");
				callOriginal();
				return;
			}

			g_playerTarget->data.objectReference = a_item;

			if (player->weaponState != RE::WEAPON_STATE::kSheathed) {
				ArmIdleStopFix();
			}

			const bool played = Game::PlayAction(player, g_actionActivate, g_playerTarget);
			ArmAnimation();

			if (g_trace) {
				logger::info(
					"[aw] AddAcquiredEvent item={:08X} activateAction played={} fromGround={}",
					a_item->formID,
					played,
					g_itemFromGround);
			}

			callOriginal();
		}

		void __fastcall HookedHandlePlayerItem(
			RE::TESBoundObject* a_item,
			RE::ExtraDataList* a_extra,
			std::uint32_t a_count)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			logger::debug(
				"HandlePlayerItem item={} count={} loaded3D={}",
				a_item ? a_item->formID : 0,
				a_count,
				player && player->GetFullyLoaded3D());

			if (g_trace) {
				logger::info(
					"[aw] HandlePlayerItem item={:08X} loaded3D={} formsReady={}",
					a_item ? a_item->formID : 0u,
					player && player->GetFullyLoaded3D(),
					g_formsReady);
			}

			if (player && player->GetFullyLoaded3D() && a_item &&
				EnsureFormsResolved() && g_playerTarget) {
				g_playerTarget->data.objectReference = a_item;

				g_pendingSwap = SwapFormFor(a_item);
				g_pendingSwapItem = g_pendingSwap ? a_item : nullptr;

				ArmAnimation();
				logger::debug(
					"HandlePlayerItem queued animation item={} swap={}",
					a_item->formID,
					g_pendingSwap != nullptr);
			}

			g_origHandlePlayerItem(a_item, a_extra, a_count);
		}

		bool __fastcall HookedUseObject(
			RE::ActorEquipManager* a_this,
			RE::Actor* a_actor,
			const RE::BGSObjectInstance* a_object,
			void* a_params)
		{
			logger::debug(
				"UseObject actor={} object={} params={}",
				a_actor ? a_actor->formID : 0,
				a_object && a_object->object ? a_object->object->formID : 0,
				a_params != nullptr);
			const auto callOriginal = [&] {
				return g_origUseObject(a_this, a_actor, a_object, a_params);
			};

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* baseForm = a_object ? static_cast<RE::TESBoundObject*>(a_object->object) : nullptr;

			if (!a_actor || !player || !baseForm || !a_actor->GetFullyLoaded3D()) {
				logger::debug("UseObject skipped because actor, object, player, or 3D is unavailable");
				return callOriginal();
			}

			const bool animatable =
				baseForm->formType == RE::ENUM_FORM_ID::kALCH ||
				baseForm->formType == RE::ENUM_FORM_ID::kARMO;

			if (!animatable || !EnsureFormsResolved() || !g_actionEquipAnim) {
				logger::debug(
					"UseObject skipped animatable={} formsReady={} actionReady={}",
					animatable,
					g_formsReady,
					g_actionEquipAnim != nullptr);
				return callOriginal();
			}

			if (a_actor == player) {
				if (g_playerTarget) {
					g_playerTarget->data.objectReference = baseForm;

					if (PipboyMenuOpen()) {
						const bool enabled =
							g_globalPipboyEquipAnims && g_globalPipboyEquipAnims->value > 0.0f;

						if (enabled && Game::CanReopenPipboy()) {
							static_cast<void>(Game::PlayAction(player, g_actionEquipAnim, g_playerTarget));
							g_reopenPipboy = true;
						}
					} else {
						static_cast<void>(Game::PlayAction(player, g_actionEquipAnim, g_playerTarget));
					}
				}
			} else if (baseForm->formType == RE::ENUM_FORM_ID::kALCH && g_npcTarget) {
				g_npcTarget->data.objectReference = baseForm;
				static_cast<void>(Game::PlayAction(a_actor, g_actionEquipAnim, g_npcTarget));
			}

			return callOriginal();
		}

		void __fastcall HookedSetInputDeviceLightState(
			RE::BSInputDeviceManager* a_manager,
			std::uint32_t a_state,
			bool a_on)
		{
			logger::debug("SetInputDeviceLightState state={} on={}", a_state, a_on);
			auto* player = RE::PlayerCharacter::GetSingleton();

			if (player && player->GetFullyLoaded3D() && EnsureFormsResolved() && g_actionFlashlight) {
				ArmIdleStopFix();
				static_cast<void>(Game::PlayAction(player, g_actionFlashlight, player));
			}

			g_origSetInputDeviceLightState(a_manager, a_state, a_on);
		}

		template <class F>
		[[nodiscard]] bool InstallCall(Addresses::Site a_site, F& a_original, F a_hook)
		{
			const auto address = Addresses::ResolveSite(a_site);
			if (!address) {
				return false;
			}
			if (!Addresses::ValidateSite(a_site, *address)) {
				return false;
			}

			auto& trampoline = F4SE::GetTrampoline();
			a_original = reinterpret_cast<F>(trampoline.write_call<5>(*address, a_hook));
			logger::info("installed {} hook", Addresses::GetSite(a_site).name);
			return true;
		}

		template <class F>
		[[nodiscard]] bool InstallBranch(Addresses::Site a_site, F& a_original, F a_hook)
		{
			const auto address = Addresses::ResolveSite(a_site);
			if (!address) {
				return false;
			}
			if (!Addresses::ValidateSite(a_site, *address)) {
				return false;
			}

			auto& trampoline = F4SE::GetTrampoline();
			a_original = reinterpret_cast<F>(trampoline.write_branch<5>(*address, a_hook));
			logger::info("installed {} hook", Addresses::GetSite(a_site).name);
			return true;
		}

		template <class F>
		[[nodiscard]] bool InstallEntry(Addresses::Site a_site, F& a_original, F a_hook)
		{
			if (a_site != Addresses::Site::kUseObject) {
				return false;
			}
			a_original = nullptr;

			const auto& site = Addresses::GetSite(a_site);
			const auto address = Addresses::ResolveUseObjectEntry();
			if (!address) {
				return false;
			}

			void* rawOriginal{ nullptr };
			if (!EntryHooks::Install(
				*address,
				reinterpret_cast<void*>(a_hook),
				&rawOriginal) ||
				!rawOriginal) {
				return false;
			}

			a_original = reinterpret_cast<F>(rawOriginal);
			logger::info(
				"installed {} entry hook runtime={} rva={:#x}",
				site.name,
				REL::Module::get().version().string(),
				*address - REL::Module::get().base());
			return true;
		}

	}

	bool Install()
	{
		g_trace = Config::DebugLoggingEnabled();
		if (g_trace) {
			logger::info("[aw] hook tracing enabled");
		}

		if (!InstallCall(Addresses::Site::kRunActorUpdates, g_origRunActorUpdates, &HookedRunActorUpdates)) {
			logger::error("RunActorUpdates hook unavailable - AnimatedWorld will stay inactive");
			return false;
		}

		static_cast<void>(InstallCall(
			Addresses::Site::kAddAcquiredEvent, g_origAddAcquiredEvent, &HookedAddAcquiredEvent));

		static_cast<void>(InstallCall(
			Addresses::Site::kHandlePlayerItem, g_origHandlePlayerItem, &HookedHandlePlayerItem));

		if (REL::runtime_family(REL::Module::get().version()) == REL::RuntimeFamily::kOG) {
			static_cast<void>(InstallCall(
				Addresses::Site::kUseObject, g_origUseObject, &HookedUseObject));
		} else if (Addresses::IsVerifiedUseObjectRuntime()) {
			if (!InstallEntry(Addresses::Site::kUseObject, g_origUseObject, &HookedUseObject)) {
				logger::warn("UseObject entry hook unavailable - feature remains disabled");
			}
		} else {
			logger::warn("UseObject hook skipped: runtime version is not verified");
		}

		if (Game::CanTestActivationBlocked()) {
			static_cast<void>(InstallCall(
				Addresses::Site::kActivateRef, g_origActivateRef, &HookedActivateRef));
		} else {
			logger::warn("ActivateRef hook skipped: IsActivationBlocked is unavailable");
		}

		static_cast<void>(InstallBranch(
			Addresses::Site::kSetInputDeviceLightState,
			g_origSetInputDeviceLightState,
			&HookedSetInputDeviceLightState));

		return true;
	}
}
