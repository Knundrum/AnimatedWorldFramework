#include "Config.h"

#include <Windows.h>

#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace AW::Config
{
	namespace
	{
		std::filesystem::path g_path;
		bool g_debugLogging{ false };

		[[nodiscard]] std::string_view Trim(std::string_view a_value) noexcept
		{
			while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.front()))) {
				a_value.remove_prefix(1);
			}

			while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.back()))) {
				a_value.remove_suffix(1);
			}

			return a_value;
		}

		[[nodiscard]] std::string Lower(std::string_view a_value)
		{
			std::string result;
			result.reserve(a_value.size());
			for (const auto character : a_value) {
				result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
			}
			return result;
		}

		[[nodiscard]] bool ParseBool(std::string_view a_value) noexcept
		{
			const auto value = Lower(Trim(a_value));
			return value == "1" || value == "true" || value == "yes" || value == "on";
		}

		[[nodiscard]] std::optional<std::filesystem::path> ModulePath()
		{
			std::wstring buffer(260, L'\0');
			for (;;) {
				const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0) {
					return std::nullopt;
				}

				if (length < buffer.size() - 1) {
					return std::filesystem::path{ std::wstring(buffer.data(), length) };
				}

				if (buffer.size() >= 32768) {
					return std::nullopt;
				}

				buffer.resize(buffer.size() * 2);
			}
		}

		[[nodiscard]] std::filesystem::path ConfigPath()
		{
			if (const auto module = ModulePath()) {
				return module->parent_path() / "Data/F4SE/Plugins/AnimatedWorld.ini";
			}

			return {};
		}
	}

	bool Load()
	{
		g_path = ConfigPath();
		g_debugLogging = false;

		if (g_path.empty()) {
			return false;
		}

		std::ifstream stream(g_path);
		if (!stream) {
			return false;
		}

		std::string line;
		std::string section;
		while (std::getline(stream, line)) {
			const auto value = Trim(line);
			if (value.empty() || value.front() == ';' || value.front() == '#') {
				continue;
			}

			if (value.front() == '[' && value.back() == ']') {
				section = Lower(Trim(value.substr(1, value.size() - 2)));
				continue;
			}

			const auto separator = value.find('=');
			if (separator == std::string_view::npos) {
				continue;
			}

			const auto key = Lower(Trim(value.substr(0, separator)));
			if (section == "animatedworld" && key == "debuglogging") {
				g_debugLogging = ParseBool(value.substr(separator + 1));
			}
		}

		return true;
	}

	bool DebugLoggingEnabled() noexcept
	{
		return g_debugLogging;
	}

	const std::filesystem::path& Path() noexcept
	{
		return g_path;
	}
}
