#pragma once

namespace AW::EntryHooks
{
	[[nodiscard]] bool Install(
		std::uintptr_t a_target,
		void* a_detour,
		void** a_original);
}
