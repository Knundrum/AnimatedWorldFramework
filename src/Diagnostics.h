#pragma once

namespace AW::Diagnostics
{
	[[nodiscard]] bool CallsiteDumpRequested();

	[[nodiscard]] bool HookTraceEnabled();

	void DumpCallsiteTargets();
}
