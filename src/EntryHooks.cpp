#include "EntryHooks.h"

#include "Diagnostics.h"

#include <MinHook.h>

namespace AW::EntryHooks
{
	namespace
	{
		bool g_initialized{ false };

		[[nodiscard]] bool Initialize()
		{
			if (g_initialized) {
				return true;
			}

			const auto status = MH_Initialize();
			if (status != MH_OK) {
				logger::error("MinHook initialization failed: {}", MH_StatusToString(status));
				return false;
			}

			g_initialized = true;
			return true;
		}

		void LogFailure(const char* a_operation, MH_STATUS a_status)
		{
			logger::error("MinHook {} failed: {}", a_operation, MH_StatusToString(a_status));
		}
	}

	bool Install(std::uintptr_t a_target, void* a_detour, void** a_original)
	{
		if (a_original) {
			*a_original = nullptr;
		}

		if (!a_target || !a_detour || !a_original || !Initialize()) {
			return false;
		}

		auto* target = reinterpret_cast<LPVOID>(a_target);
		const auto createStatus = MH_CreateHook(target, a_detour, a_original);
		if (createStatus != MH_OK) {
			LogFailure("hook creation", createStatus);
			return false;
		}
		if (!*a_original) {
			logger::error("MinHook hook creation returned no original trampoline");
			const auto removeStatus = MH_RemoveHook(target);
			if (removeStatus != MH_OK) {
				LogFailure("partial hook cleanup", removeStatus);
			}
			return false;
		}

		const auto enableStatus = MH_EnableHook(target);
		if (enableStatus == MH_OK) {
			return true;
		}

		LogFailure("hook enable", enableStatus);
		const auto removeStatus = MH_RemoveHook(target);
		if (removeStatus != MH_OK) {
			LogFailure("partial hook cleanup", removeStatus);
		}
		*a_original = nullptr;
		return false;
	}
}
