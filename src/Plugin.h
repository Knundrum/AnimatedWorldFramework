#pragma once

namespace F4SE
{
	class LoadInterface;
}

namespace AW::Plugin
{
	[[nodiscard]] bool Initialize(const F4SE::LoadInterface* a_f4se);
}
