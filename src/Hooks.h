#pragma once

namespace AW::Hooks
{
	// Installs every hook whose addresses resolved on this runtime.  Returns
	// false when nothing could be installed.
	[[nodiscard]] bool Install();
}
