#pragma once

	namespace AW::Diagnostics
{
	[[nodiscard]] bool CallsiteDumpRequested();

	void DumpCallsiteTargets();
}
