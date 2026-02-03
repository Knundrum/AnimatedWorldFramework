#include "hooks.h"
#include "detourxs/detourxs.h"
#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESForms.h"
#include "RE/Bethesda/TESDataHandler.h"



namespace HookLineAndSinker
{

		struct ObjectEquipParams
	{
		uint32_t a_stackID;
		uint32_t a_number;
	};

	RE::TESObjectREFR* g_CrosshairRef = nullptr;
	bool IgnoreIdleStop = false;
	bool CloseEnoughVar = false;

	typedef bool(ActivatePickRefSig)(RE::PlayerCharacter* a_manager);
	typedef bool (*IdleStopSig)(void* a_this, RE::BSFixedString const& a_eventName, float& a_outValue);
	typedef bool (*CloseEnoughSig)(RE::PlayerCharacter* a_manager, RE::TESObjectREFR* a_target);
	typedef bool (*EquipItemSig)(RE::ActorEquipManager* a_manager, RE::Actor* a_actor, RE::BGSObjectInstanceT<RE::TESBoundObject> a_obj, ObjectEquipParams& a_params);

	REL::Relocation<ActivatePickRefSig> OriginalActivatePickRef;
	REL::Relocation<IdleStopSig> OriginalIdleStop;
	REL::Relocation<CloseEnoughSig> OriginalCloseEnough;
	REL::Relocation<EquipItemSig> OriginalEquipItem;

	DetourXS hookActivatePickRef;
	DetourXS hookIdleStop;
	DetourXS hookCloseEnough;
	DetourXS hookEquipItem;

	bool HookedIdleStop(void* a_manager, RE::BSFixedString const& a_eventName, float& a_outValue)
	{
		//auto player = RE::PlayerCharacter::GetSingleton();
		logger::warn("Hooked IdleStop");

		//if (g_CrosshairRef && a_actor == player) {
		if (IgnoreIdleStop == true && a_eventName == "IdleStop") {
			logger::warn("Should have stopped the idlestop");
			a_outValue = 999999.0f;  // Set a massive time-to-stop
			return true;             // Pretend we found the data successfully
		} else {
			return OriginalIdleStop(a_manager,a_eventName,a_outValue);
		}

	}

	void HookedActivatePickRef(RE::PlayerCharacter* a_manager)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto dataHandler = RE::TESDataHandler::GetSingleton();

		auto action = dataHandler->LookupForm<RE::BGSAction>(0x12196, "Animated World - Base.esp");

		if (player && g_CrosshairRef && action && CloseEnoughVar) {
			const char* name = g_CrosshairRef->GetDisplayFullName();

			using func_t = bool (*)(RE::Actor*, RE::BGSAction*, RE::TESObjectREFR*, void*, std::uint32_t);

			// 3. Use the REL::ID you found
			REL::Relocation<func_t> func{ REL::ID(1451490) };

			// 4. Call the function
			// Arguments: Performer, Action, Target Object, VM (null), CallbackID (0)
			bool success = func(player, action, g_CrosshairRef, nullptr, 0);
			if (success) {
				logger::warn("Anim should have been played on {}", name);
				IgnoreIdleStop = true;
			}
		}

		OriginalActivatePickRef(a_manager);
	}

	bool HookedCloseEnough(RE::PlayerCharacter* a_manager, RE::TESObjectREFR* a_target)
	{
		//logger::warn("Is Close Enough");
		bool result = OriginalCloseEnough(a_manager, a_target);
		if (result) {
			CloseEnoughVar = true;
			g_CrosshairRef = a_target;
		} else {
			CloseEnoughVar = false;
			g_CrosshairRef = nullptr;
		}
		return result;
	}

	bool HookedEquipItem(RE::ActorEquipManager* a_manager, RE::Actor* a_actor, RE::BGSObjectInstanceT<RE::TESBoundObject> a_obj, ObjectEquipParams& a_params)
	{
	}



	void RegisterHook()
	{
		/*
		REL::Relocation<IdleStopSig> IdleStopLoc{ REL::ID(1115403) };

		if (hookIdleStop.Create(reinterpret_cast<LPVOID>(IdleStopLoc.address()), &HookedIdleStop)) {
			// We cast the trampoline address back to our signature type
			OriginalIdleStop = reinterpret_cast<std::uintptr_t>(hookIdleStop.GetTrampoline());
			logger::warn("Successfully hooked IdleStop");
		} else {
			logger::warn("Failed to create IdleStop hook");
		}
		*/

		REL::Relocation<ActivatePickRefSig> ActivatePickRefLoc{ REL::ID(547089) };

		if (hookActivatePickRef.Create(reinterpret_cast<LPVOID>(ActivatePickRefLoc.address()), &HookedActivatePickRef)) {
			// We cast the trampoline address back to our signature type
			OriginalActivatePickRef = reinterpret_cast<std::uintptr_t>(hookActivatePickRef.GetTrampoline());
			logger::warn("Successfully hooked ActivatePickRef");
		} else {
			logger::warn("Failed to create ActivatePickRef hook");
		}

		REL::Relocation<CloseEnoughSig> CloseEnoughLoc{ REL::ID(666830) };

		if (hookCloseEnough.Create(reinterpret_cast<LPVOID>(CloseEnoughLoc.address()), &HookedCloseEnough)) {
			// We cast the trampoline address back to our signature type
			OriginalCloseEnough = reinterpret_cast<std::uintptr_t>(hookCloseEnough.GetTrampoline());
			logger::warn("Successfully hooked CloseEnough");
		} else {
			logger::warn("Failed to create CloseEnough hook");
		}

		REL::Relocation<EquipItemSig> EquipItemLoc{ REL::ID(1474878) };

		if (hookEquipItem.Create(reinterpret_cast<LPVOID>(EquipItemLoc.address()), &HookedEquipItem)) {
			// We cast the trampoline address back to our signature type
			OriginalEquipItem = reinterpret_cast<std::uintptr_t>(hookEquipItem.GetTrampoline());
			logger::warn("Successfully hooked EquipItem");
		} else {
			logger::warn("Failed to create EquipItem hook");
		}
	}


}
