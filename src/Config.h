#pragma once

#include <filesystem>

namespace AW::Config
{
	[[nodiscard]] bool Load();
	[[nodiscard]] bool DebugLoggingEnabled() noexcept;
	[[nodiscard]] const std::filesystem::path& Path() noexcept;
}
