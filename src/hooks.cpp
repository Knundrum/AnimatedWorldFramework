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
	bool CloseEnoughVar = false;

	typedef bool(ActivatePickRefSig)(RE::PlayerCharacter* a_manager);
	typedef bool (*CloseEnoughSig)(RE::PlayerCharacter* a_manager, RE::TESObjectREFR* a_target);

	REL::Relocation<ActivatePickRefSig> OriginalActivatePickRef;
	REL::Relocation<CloseEnoughSig> OriginalCloseEnough;

	DetourXS hookActivatePickRef;
	DetourXS hookCloseEnough;

	void HookedActivatePickRef(RE::PlayerCharacter* a_manager)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto dataHandler = RE::TESDataHandler::GetSingleton();

		auto action = dataHandler->LookupForm<RE::BGSAction>(0x12196, "Animated World - Base.esp");

		if (player && g_CrosshairRef && action && CloseEnoughVar) {
			const char* name = g_CrosshairRef->GetDisplayFullName();

			using func_t = bool (*)(RE::Actor*, RE::BGSAction*, RE::TESObjectREFR*, void*, std::uint32_t);

			REL::Relocation<func_t> func{ REL::ID(1451490) };

			bool success = func(player, action, g_CrosshairRef, nullptr, 0);
			if (success) {
				logger::warn("Anim should have been played on {}", name);
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



	void RegisterHook()
	{

		REL::Relocation<ActivatePickRefSig> ActivatePickRefLoc{ REL::ID(547089) };

		if (hookActivatePickRef.Create(reinterpret_cast<LPVOID>(ActivatePickRefLoc.address()), &HookedActivatePickRef)) {
			OriginalActivatePickRef = reinterpret_cast<std::uintptr_t>(hookActivatePickRef.GetTrampoline());
			logger::warn("Successfully hooked ActivatePickRef");
		} else {
			logger::warn("Failed to create ActivatePickRef hook");
		}

		REL::Relocation<CloseEnoughSig> CloseEnoughLoc{ REL::ID(666830) };

		if (hookCloseEnough.Create(reinterpret_cast<LPVOID>(CloseEnoughLoc.address()), &HookedCloseEnough)) {
			OriginalCloseEnough = reinterpret_cast<std::uintptr_t>(hookCloseEnough.GetTrampoline());
			logger::warn("Successfully hooked CloseEnough");
		} else {
			logger::warn("Failed to create CloseEnough hook");
		}

	}


}



