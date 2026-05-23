#include "hooks.h"
#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESForms.h"
#include "RE/Bethesda/TESDataHandler.h"
#include <chrono>

namespace HookLineAndSinker
{

		struct ObjectEquipParams
	{
		uint32_t a_stackID;
		uint32_t a_number;
	};

	//RE::TESObjectREFR* g_CrosshairRef = nullptr;
	//bool CloseEnoughVar = false;

	bool reOpenPipboy = false;
	bool IdleStopFix = false;
	bool G_HookInstalled = false;

	RE::BGSKeyword* AW_IdleStopFix = nullptr;
	RE::BGSAction* AW_ActionActivate = nullptr;
	RE::BGSAction* AW_ItemAddedAction = nullptr;
	RE::BGSAction* AW_EquipAnimAction = nullptr;
	RE::BGSAction* AW_FlashlightAction = nullptr;

	float g_clipTime = 0.0f;
	float g_clipDuration = 0.0f;
	std::string g_clipName = "";

	bool g_LightPending = false;
	std::chrono::steady_clock::time_point g_LightDeadline;

	bool g_ItemFromGround = false;

	RE::TESGlobal* AW_Enable_PipboyEquipAnims = nullptr;

	RE::BGSMaterialSwap* g_currentSwap = nullptr;

	static RE::TESObjectREFR* g_AnimationTarget = nullptr;
	static RE::TESObjectREFR* g_NPC_AnimationTarget = nullptr;

	bool g_AnimationPending = false;
	std::chrono::steady_clock::time_point g_AnimationDeadline;

	bool g_AnimationSoon = false;
	std::chrono::steady_clock::time_point g_AnimationReadyTimer;

	bool WaitForMatSwap = false;
	std::chrono::steady_clock::time_point MatSwapTimer;

	using ApplySwapSig = void(__fastcall*)(RE::NiAVObject*, RE::BGSMaterialSwap const*, float, float, void*);
	REL::Relocation<ApplySwapSig> ApplySwap{ REL::ID(708895) };

	using PlayActionSig = bool (*)(RE::Actor*, RE::BGSAction*, RE::TESObjectREFR*, void*, std::uint32_t);
	REL::Relocation<PlayActionSig> PlayAction { REL::ID(1451490) };

	using _IsActivationBlocked = bool(RE::TESObjectREFR* a_this);


	REL::Relocation<uintptr_t> ptr_RunActorUpdates{ REL::ID(556439), 0xF0 };  //0x17 0x21D //0xF0
	using FnRunActorUpdates = void(__fastcall*)(void* a_this);
	REL::Relocation<FnRunActorUpdates> RunActorUpdatesOrig;

	REL::Relocation<uintptr_t> ptr_AddEvent{ REL::ID(1401485), 0x2D6 };
	uintptr_t OriginalAddEvent;

	REL::Relocation<uintptr_t> ptr_ActivateRef{ REL::ID(785533), 0x38A };
	using FnActivateRef = bool(__fastcall*)(RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESBoundObject*, int, bool, bool, bool);
	REL::Relocation<FnActivateRef> OriginalActivateRef;

	REL::Relocation<uintptr_t> ptr_HandlePlayerItem{ REL::ID(78185), 0xA40 };  //A hook inside AddItem HUDInventoryChangeMessageEvent::HandlePlayerItemAdded(TESBoundObject *,ExtraDataList const &,uint)
	using FnHandlePlayerItem = void(__fastcall*)(RE::TESBoundObject*, RE::ExtraDataList*, uint32_t);
	REL::Relocation<FnHandlePlayerItem> OriginalHandlePlayerItem;


	REL::Relocation<uintptr_t> ptr_UseObject{ REL::ID(988029), 0x15A }; //The call inside the EquipItem function that handles the objects.
	using FnUseObject = bool(__fastcall*)(RE::ActorEquipManager*, RE::Actor*, RE::BGSObjectInstance*, ObjectEquipParams&);
	REL::Relocation<FnUseObject> OriginalUseObject;
	

	/*
	REL::Relocation<uintptr_t> ptr_TogglePipboyLight{ REL::ID(520007) };
	using FnTogglePipboyLight = void(__fastcall*)(RE::PlayerCharacter*);
	REL::Relocation<FnTogglePipboyLight> OriginalTogglePipboyLight;
	//uintptr_t OriginalTogglePipboyLight;
	*/
	
	REL::Relocation<uintptr_t> ptr_SetInputDeviceLightState{ REL::ID(520007), 0x5B };  //157452
	using FnSetInputDeviceLightState = void(__fastcall*)(RE::BSInputDeviceManager*, std::uint32_t, bool);
	REL::Relocation<FnSetInputDeviceLightState> OriginalSetInputDeviceLightState;
	
	
	using OriginalProcessEvent_Fn = RE::BSEventNotifyControl (RE::BSTEventSink<RE::BSAnimationGraphEvent>::*)(const RE::BSAnimationGraphEvent&, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
	static OriginalProcessEvent_Fn OriginalPlayerProcessEvent = nullptr;


	void GetClipInfo(RE::Actor* actor, float& currentTime, float& duration, std::string& clipName)
	{
		if (actor->currentProcess) {
			RE::BSAnimationGraphManager* graphManager = actor->currentProcess->middleHigh->animationGraphManager.get();
			if (graphManager) {
				RE::BShkbAnimationGraph* graph = graphManager->variableCache.graphToCacheFor.get();
				if (graph) {
					void* hkGraph = *(void**)((uintptr_t)graph + 0x378);
					typedef RE::hkArray<void**, struct hkContainerHeapAllocator> nodeArray;
					nodeArray* activeNodes = *(nodeArray**)((uintptr_t)hkGraph + 0xE0);
					if (activeNodes && activeNodes->_size > 0) {
						void** generatorArray = *activeNodes->_data;
						while (*generatorArray) {
							uintptr_t clip = (uintptr_t)(*generatorArray);
							if (*(uint32_t*)(clip + 0x8)) {
								currentTime = *(float*)(clip + 0x140);
								clipName = std::string(*(const char**)(clip + 0x38));
								if (currentTime) {
									uintptr_t animCtrl = *(uintptr_t*)(clip + 0xD0);
									if (animCtrl) {
										uintptr_t animBinding = *(uintptr_t*)(animCtrl + 0x38);
										uintptr_t anim = *(uintptr_t*)(animBinding + 0x18);
										duration = *(float*)(anim + 0x14);
										return;
									}
								}
							}
							generatorArray = (void**)((uintptr_t)generatorArray + 0x8);
						}
					}
				}
			}
		}
	}


	void EnsureTargetExists()
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();

		if (!g_AnimationTarget) {
			auto factory = RE::IFormFactory::GetFormFactories()[static_cast<std::size_t>(RE::ENUM_FORM_ID::kREFR)];
			g_AnimationTarget = static_cast<RE::TESObjectREFR*>(factory->DoCreate());
		}
		if (!g_NPC_AnimationTarget) {
			auto factory2 = RE::IFormFactory::GetFormFactories()[static_cast<std::size_t>(RE::ENUM_FORM_ID::kREFR)];
			g_NPC_AnimationTarget = static_cast<RE::TESObjectREFR*>(factory2->DoCreate());
		}
		if (!AW_IdleStopFix) {
			AW_IdleStopFix = dataHandler->LookupForm<RE::BGSKeyword>(0x34D3A, "Animated World - Base.esp");
		}
		if (!AW_ActionActivate) {
			AW_ActionActivate = dataHandler->LookupForm<RE::BGSAction>(0x12196, "Animated World - Base.esp");  //AW_ActionActivate
		}
		if (!AW_ItemAddedAction) {
			AW_ItemAddedAction = dataHandler->LookupForm<RE::BGSAction>(0x2B4D9, "Animated World - Base.esp");  //AW_ItemAddedAction
		}
		if (!AW_EquipAnimAction) {
			AW_EquipAnimAction = dataHandler->LookupForm<RE::BGSAction>(0x18481, "Animated World - Base.esp");  //AW_EquipAnimAction
		}
		if (!AW_FlashlightAction) {
			AW_FlashlightAction = dataHandler->LookupForm<RE::BGSAction>(0x14F3E, "Animated World - Base.esp");  //AW_FlashlightAction
		}
		if (!AW_Enable_PipboyEquipAnims) {
			AW_Enable_PipboyEquipAnims = dataHandler->LookupForm<RE::TESGlobal>(0x399BC, "Animated World - Base.esp");
		}
		
		//logger::warn("Ready to Animate!");
	}

	bool IsRefBlocked(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref)
			return false;

		static REL::Relocation<_IsActivationBlocked> func{ REL::ID(407609) };

		return func(a_ref);
	}


	RE::BSEventNotifyControl HookedPlayerProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this, const RE::BSAnimationGraphEvent& a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) //IdleStopFix approach originally made by LuBuCake, what a champ.
	{
	//logger::warn("AnimEvent: {}", a_event.tag.c_str()); //Nice way to find different things to hook.
		if (!OriginalPlayerProcessEvent) {
			return RE::BSEventNotifyControl::kContinue;
			logger::warn("returned kContinue, might CTD here");
		}

		if (a_event.tag == "IdleStop" && IdleStopFix == true) {
			auto player = RE::PlayerCharacter::GetSingleton();

			if (player->WornHasKeyword(AW_IdleStopFix)) {
				player->UpdateAnimation(2.0f);
				//logger::warn("Player has KW");
			} else {
				player->UpdateAnimation(0.0f);
			}
			IdleStopFix = false;

		} else if (a_event.tag == "ReevaluateGraphState" && reOpenPipboy == true) {
			auto pipboy = RE::PipboyManager::GetSingleton();
			
			reOpenPipboy = false;
			if (RE::UI::GetSingleton()->GetMenuOpen("PipboyMenu"))
				pipboy->PlayPipboyOpenAnim("PipboyInv");
		}


		return OriginalPlayerProcessEvent ? (a_this->*OriginalPlayerProcessEvent)(a_event, a_source) : RE::BSEventNotifyControl::kContinue;
	}



	bool __fastcall HookedActivateRef(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, RE::TESBoundObject* a_item, int a_count, bool a_force, bool a_silent, bool a_other)
	{
		auto player = RE::PlayerCharacter::GetSingleton();

		if (IsRefBlocked(a_target))
			return OriginalActivateRef(a_target, a_activator, a_item, a_count, a_force, a_silent, a_other);

		g_ItemFromGround = false;

		if (player && a_target && AW_ActionActivate) {
			if (player->weaponState != RE::WEAPON_STATE::kSheathed)
				IdleStopFix = true;

			bool success = PlayAction(player, AW_ActionActivate, a_target, nullptr, 0);
			if (success) {
				g_ItemFromGround = true;
			}
		}

		return OriginalActivateRef(a_target, a_activator, a_item, a_count, a_force, a_silent, a_other);
	}


	void __fastcall HookedAddEvent(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_item, RE::TESForm* a_source, RE::TESObjectREFR* a_container, int32_t a_acquireType)
	{
		typedef void (*FnUpdate)(RE::PlayerCharacter*, RE::TESBoundObject*, RE::TESForm*, RE::TESObjectREFR*, int32_t);
		FnUpdate fn = (FnUpdate)OriginalAddEvent;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto baseForm = static_cast<RE::TESBoundObject*>(a_item);
		if (!player || !baseForm)
			(*fn)(a_player, a_item, a_source, a_container, a_acquireType);

		EnsureTargetExists();
		g_AnimationTarget->data.objectReference = baseForm;

		if (player && g_AnimationTarget && AW_ActionActivate) {
			if (player->weaponState != RE::WEAPON_STATE::kSheathed)
				IdleStopFix = true;


			PlayAction(player, AW_ActionActivate, g_AnimationTarget, nullptr, 0);

			g_AnimationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
			g_AnimationPending = true;
		}

				if (fn)
			(*fn)(a_player, a_item, a_source, a_container, a_acquireType);
	}


	void InstallVTableHook()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player)
			return;

		uintptr_t playerAnimSinkPtr = reinterpret_cast<uintptr_t>(player) + 0x38;
		if (playerAnimSinkPtr == 0)
			return;

		std::uintptr_t* vtable = *reinterpret_cast<std::uintptr_t**>(playerAnimSinkPtr);
		if (!vtable)
			return;

		if (vtable[1] == reinterpret_cast<std::uintptr_t>(HookedPlayerProcessEvent)) {
			return;
		}

		OriginalPlayerProcessEvent = *(OriginalProcessEvent_Fn*)&vtable[1];

		REL::safe_write(
			reinterpret_cast<std::uintptr_t>(&vtable[1]),
			reinterpret_cast<std::uintptr_t>(HookedPlayerProcessEvent));

		//logger::warn("Hook installed at Player+0x38. NPC interference should be zero.");
	}


	void __fastcall HookedRunActorUpdate(void* a_this)
	{
		RunActorUpdatesOrig(a_this);

		auto player = RE::PlayerCharacter::GetSingleton();
		if (player && player->currentProcess && player->currentProcess->middleHigh) {
			if (!OriginalPlayerProcessEvent)
				InstallVTableHook();
			if (player->GetFullyLoaded3D()) {
				EnsureTargetExists();
				//logger::warn("{}", a_delta);

				//GetClipInfo(player, g_clipTime, g_clipDuration, g_clipName);
				//logger::warn("{}", g_clipName);

				if (g_AnimationPending && WaitForMatSwap == false) {
					if (std::chrono::steady_clock::now() >= g_AnimationDeadline) {
						GetClipInfo(player, g_clipTime, g_clipDuration, g_clipName);

						if (g_ItemFromGround == true) {
							g_AnimationPending = false;
							g_AnimationSoon = true;
							g_AnimationReadyTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);  //Wait for animation when you pick up a -> additem -> playanim
						}
						if (g_clipName == "DynamicIdle") {
							g_AnimationPending = false;
							g_AnimationSoon = true;
							int32_t OffsetCal = static_cast<int32_t>((g_clipDuration - g_clipTime) * 1000.0f) + 200;
							g_AnimationReadyTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(OffsetCal);
						}
					}
				}
				if (g_AnimationSoon && WaitForMatSwap == false) {
					if (std::chrono::steady_clock::now() >= g_AnimationReadyTimer) {
						PlayAction(player, AW_ItemAddedAction, g_AnimationTarget, nullptr, 0);

						if (g_currentSwap != nullptr) {
							WaitForMatSwap = true;
							MatSwapTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
						}
						g_AnimationSoon = false;
					}
				}
				if (WaitForMatSwap && g_currentSwap) {
					if (std::chrono::steady_clock::now() >= MatSwapTimer) {
						auto player3D = RE::PlayerCharacter::GetSingleton()->Get3D();

						//using ApplySwap_t = void(__fastcall*)(RE::NiAVObject*, RE::BGSMaterialSwap const*, float, float, void*);
						//REL::Relocation<ApplySwap_t> ApplySwap{ REL::ID(708895) };

						ApplySwap(player3D, g_currentSwap, 1.0f, 1.0f, nullptr);

						WaitForMatSwap = false;

						//const char* name = g_currentSwap->GetFormEditorID();
						//logger::warn("Apply {}", name);
					}
				}
				if (g_LightPending) {
					if (std::chrono::steady_clock::now() >= g_LightDeadline) {
						//OriginalTogglePipboyLight(playerRef);
						g_LightPending = false;
						logger::warn("Light nao");
					}
				}
			}
		}

		//return RunActorUpdatesOrig(a_this);
	}



	void __fastcall HookedHandlePlayerItem(RE::TESBoundObject* a_item, RE::ExtraDataList* a_extra, uint32_t a_count)
	{
		
		auto player = RE::PlayerCharacter::GetSingleton();
		if (player && player->GetFullyLoaded3D()) {
				//if ((a_item->formType == RE::ENUM_FORM_ID::kBOOK) || (a_item->formType == RE::ENUM_FORM_ID::kMISC)) {

				EnsureTargetExists();

				g_AnimationTarget->data.objectReference = a_item;
				
				if (a_item->formType == RE::ENUM_FORM_ID::kBOOK) {
					auto book = static_cast<RE::TESObjectBOOK*>(a_item);
					g_currentSwap = book->swapForm;
				} else if (a_item->formType == RE::ENUM_FORM_ID::kMISC) {
					auto misc = static_cast<RE::TESObjectMISC*>(a_item);
					g_currentSwap = misc->swapForm;
				} else {
					g_currentSwap = nullptr;
				}

				g_AnimationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
				g_AnimationPending = true;

				//const char* name = a_item->GetFullName();
				//logger::warn("Added Item: {} [ID: {:08X}]", name ? name : "Unknown", a_item->GetFormID());

		}
		
		OriginalHandlePlayerItem(a_item, a_extra, a_count);
	}


	bool __fastcall HookedUseObject(RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::BGSObjectInstance* a_obj, ObjectEquipParams& a_params)
	{
		if (!a_actor->GetFullyLoaded3D())
			return OriginalUseObject(a_mgr, a_actor, a_obj, a_params);

		auto player = RE::PlayerCharacter::GetSingleton();
		auto baseForm = static_cast<RE::TESBoundObject*>(a_obj->object);
		if (!baseForm)
			return OriginalUseObject(a_mgr, a_actor, a_obj, a_params);
		if (!player)
			return OriginalUseObject(a_mgr, a_actor, a_obj, a_params);

		if (a_obj->object->formType == RE::ENUM_FORM_ID::kALCH || a_obj->object->formType == RE::ENUM_FORM_ID::kARMO) {
			EnsureTargetExists();

			if (a_actor == player && player->GetFullyLoaded3D()) {
				if (g_AnimationTarget && baseForm) {
					g_AnimationTarget->data.objectReference = baseForm;

					if (RE::UI::GetSingleton()->GetMenuOpen("PipboyMenu")) {
						if (AW_Enable_PipboyEquipAnims->value > 0.0f) {
							PlayAction(player, AW_EquipAnimAction, g_AnimationTarget, nullptr, 0);
							reOpenPipboy = true;
						}
					} else {
						PlayAction(player, AW_EquipAnimAction, g_AnimationTarget, nullptr, 0);
					}
				}
			} else if (a_actor != player && a_actor->GetFullyLoaded3D() && a_obj->object->formType == RE::ENUM_FORM_ID::kALCH) {
				if (g_NPC_AnimationTarget && baseForm) {
					g_NPC_AnimationTarget->data.objectReference = baseForm;

					PlayAction(a_actor, AW_EquipAnimAction, g_NPC_AnimationTarget, nullptr, 0);
				}
			}
		}
		return OriginalUseObject(a_mgr, a_actor, a_obj, a_params);
	}


	void HookedSetInputDeviceLightState(RE::BSInputDeviceManager* a_manager, std::uint32_t a_state, bool a_on)
	{

		auto player = RE::PlayerCharacter::GetSingleton();
		auto dataHandler = RE::TESDataHandler::GetSingleton();

		if (player && dataHandler) {

			if (AW_FlashlightAction) {

				IdleStopFix = true;

				PlayAction(player, AW_FlashlightAction, player, nullptr, 0);
			}
		}

		return OriginalSetInputDeviceLightState(a_manager, a_state, a_on);
	}


	/*
	void __fastcall HookedTogglePipboyLight(RE::PlayerCharacter* a_player)
	{
		//typedef bool (*FnUpdate)(RE::PlayerCharacter*);
		//FnUpdate fn = (FnUpdate)OriginalTogglePipboyLight;
		
		if (g_LightPending == false) {
			g_LightPending = true;
			g_LightDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
		}
		logger::warn("Light pls");
		//return;
		OriginalTogglePipboyLight(a_player);
		//if (fn)
		//	(*fn)(a_player);
	}
	*/

	void RegisterHook()
	{

		
		F4SE::Trampoline& trampoline = F4SE::GetTrampoline();

		RunActorUpdatesOrig = trampoline.write_call<5>(ptr_RunActorUpdates.address(), &HookedRunActorUpdate);

		OriginalAddEvent = trampoline.write_call<5>(ptr_AddEvent.address(), &HookedAddEvent);

		OriginalUseObject = trampoline.write_call<5>(ptr_UseObject.address(), &HookedUseObject);

		OriginalHandlePlayerItem = trampoline.write_call<5>(ptr_HandlePlayerItem.address(), &HookedHandlePlayerItem);

		OriginalActivateRef = trampoline.write_call<5>(ptr_ActivateRef.address(), &HookedActivateRef);

		OriginalSetInputDeviceLightState = trampoline.write_branch<5>(ptr_SetInputDeviceLightState.address(), &HookedSetInputDeviceLightState);
		/*
		uintptr_t addr = ptr_TogglePipboyLight.address();
		REL::safe_fill(addr, 0x90, 7);
		OriginalTogglePipboyLight = trampoline.write_branch<5>(addr, &HookedTogglePipboyLight);
		*/
	}


}
