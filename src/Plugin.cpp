#include "Plugin.h"

#include "Addresses.h"
#include "Diagnostics.h"
#include "Game.h"
#include "Hooks.h"

namespace
{
	[[nodiscard]] constexpr std::uint32_t PackVersion(
		std::uint32_t a_major,
		std::uint32_t a_minor,
		std::uint32_t a_build,
		std::uint32_t a_sub = 0) noexcept
	{
		return ((a_major & 0xFF) << 24) |
		       ((a_minor & 0xFF) << 16) |
		       ((a_build & 0xFFF) << 4) |
		       (a_sub & 0xF);
	}

	[[nodiscard]] constexpr F4SE::PluginVersionData MakePluginVersionData() noexcept
	{
		F4SE::PluginVersionData data{};

		data.pluginVersion = PackVersion(
			static_cast<std::uint32_t>(Version::MAJOR),
			static_cast<std::uint32_t>(Version::MINOR),
			static_cast<std::uint32_t>(Version::PATCH));

		for (std::size_t i = 0; i < Version::PROJECT.size() && i < std::size(data.name) - 1; ++i) {
			data.name[i] = Version::PROJECT[i];
		}

		// Addresses come from CommonLibF4RD's runtime database, not from a
		// fixed executable layout, so a new patch number on its own is not a
		// reason for F4SE to reject the plugin.
		data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;

		// Class layouts are a separate concern from address resolution.  These
		// are advertised so the plugin can load on NG/AE and print its address
		// report; anything that actually depends on an unverified layout
		// (see Game::CanReadClipInfo) disables itself at runtime.
		data.structureIndependence =
			F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
			F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;

		return data;
	}

	[[nodiscard]] bool InitializeLogger()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			return false;
		}

		*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
		log->set_level(spdlog::level::trace);
#else
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
#endif

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v"s);
		return true;
	}
}

extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = MakePluginVersionData();

bool AW::Plugin::Initialize(const F4SE::LoadInterface* a_f4se)
{
	if (!InitializeLogger()) {
		return false;
	}

	if (a_f4se->IsEditor()) {
		logger::critical("loaded in editor");
		return false;
	}

	F4SE::Init(a_f4se);

	logger::info(
		"{} v{} loaded on runtime {}",
		Version::PROJECT,
		Version::NAME,
		a_f4se->RuntimeVersion().string());

	// Deliberately no executable-version whitelist.  A runtime this plugin has
	// never seen is allowed to reach the runtime database and try; whether it
	// can work is decided by whether the required addresses resolve.
	Addresses::LogCapabilityReport();

	if (!Game::Initialize()) {
		logger::error("required engine addresses are unavailable on this runtime - staying inactive");
		return false;
	}

	F4SE::AllocTrampoline(384);

	// Must happen before any hook is installed, or it decodes our trampolines
	// instead of the game's original branches.
	if (Diagnostics::CallsiteDumpRequested()) {
		Diagnostics::DumpCallsiteTargets();
	}

	if (!Hooks::Install()) {
		logger::error("no hooks were installed - staying inactive");
		return false;
	}

	logger::info("AnimatedWorld ready");
	return true;
}
