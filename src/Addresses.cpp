#include "Addresses.h"

#include "Config.h"

namespace AW::Addresses
{
	namespace
	{
		[[nodiscard]] REL::RuntimeFamily CurrentFamily() noexcept
		{
			return REL::runtime_family(REL::Module::get().version());
		}

		[[nodiscard]] std::ptrdiff_t OffsetForRuntime(const HookSite& a_site) noexcept
		{
			switch (CurrentFamily()) {
			case REL::RuntimeFamily::kOG:
				return a_site.ogOffset;
			case REL::RuntimeFamily::kNG:
				return a_site.ngOffset;
			case REL::RuntimeFamily::kAE:
			default:
				return a_site.aeOffset;
			}
		}

		[[nodiscard]] std::uint64_t IDForRuntime(const REL::ID& a_id) noexcept
		{
			return a_id.id();
		}

		[[nodiscard]] REL::ID IsolateForRuntime(const REL::ID& a_id) noexcept
		{
			switch (CurrentFamily()) {
			case REL::RuntimeFamily::kOG:
				return REL::ID{ a_id.og_id(), REL::ID::INVALID_ID };
			case REL::RuntimeFamily::kNG:
				return REL::ID{ REL::ID::INVALID_ID, a_id.ng_id(), REL::ID::INVALID_ID };
			case REL::RuntimeFamily::kAE:
			default:
				return REL::ID{ a_id.ae_id() };
			}
		}
	}

	std::string_view RuntimeFamilyName() noexcept
	{
		switch (CurrentFamily()) {
		case REL::RuntimeFamily::kOG:
			return "OG"sv;
		case REL::RuntimeFamily::kNG:
			return "NG"sv;
		case REL::RuntimeFamily::kAE:
		default:
			return "AE"sv;
		}
	}

	bool HasIDForRuntime(const REL::ID& a_id) noexcept
	{
		return IDForRuntime(a_id) != REL::ID::INVALID_ID;
	}

	bool IsVerifiedRuntime() noexcept
	{
		const auto version = REL::Module::get().version();
		return version == REL::Version{ 1, 10, 163, 0 } ||
		       version == REL::Version{ 1, 10, 984, 0 } ||
		       version == REL::Version{ 1, 11, 240, 0 };
	}

	bool IsVerifiedUseObjectRuntime() noexcept
	{
		const auto version = REL::Module::get().version();
		const auto family = CurrentFamily();
		return (family == REL::RuntimeFamily::kNG && version == REL::Version{ 1, 10, 984, 0 }) ||
		       (family == REL::RuntimeFamily::kAE && version == REL::Version{ 1, 11, 240, 0 });
	}

	std::optional<std::uintptr_t> ResolveFunction(std::string_view a_name, const REL::ID& a_id)
	{
		if (!HasIDForRuntime(a_id)) {
			logger::warn(
				"{}: no {} id supplied - feature unavailable on this runtime",
				a_name,
				RuntimeFamilyName());
			return std::nullopt;
		}

		const auto result = REL::IDDatabase::get().resolve(IsolateForRuntime(a_id));
		if (!result) {
			logger::error(
				"{}: id {} could not be resolved ({})",
				a_name,
				IDForRuntime(a_id),
				REL::id_resolve_status_text(result.status));
			return std::nullopt;
		}

		if (Config::DebugLoggingEnabled()) {
			logger::debug(
				"resolved {} id {} to rva {:#x} address {:#x}",
				a_name,
				IDForRuntime(a_id),
				*result.rva,
				REL::Module::get().base() + *result.rva);
		}

		return REL::Module::get().base() + *result.rva;
	}

	std::optional<std::uintptr_t> ResolveUseObjectEntry()
	{
		const auto version = REL::Module::get().version();
		const auto family = CurrentFamily();
		std::optional<std::uintptr_t> expectedRva;
		if (family == REL::RuntimeFamily::kNG && version == REL::Version{ 1, 10, 984, 0 }) {
			expectedRva = 0xC61490;
		} else if (family == REL::RuntimeFamily::kAE && version == REL::Version{ 1, 11, 240, 0 }) {
			expectedRva = 0xCE7460;
		} else {
			logger::error("UseObject entry rejected on unverified runtime {}", version.string());
			return std::nullopt;
		}

		const auto& site = GetSite(Site::kUseObject);
		const auto address = ResolveFunction(site.name, site.callsiteTarget);
		if (!address) {
			return std::nullopt;
		}

		const auto base = REL::Module::get().base();
		if (*address < base) {
			logger::error("UseObject entry resolved below module base on runtime {}", version.string());
			return std::nullopt;
		}

		const auto actualRva = *address - base;
		if (actualRva != *expectedRva) {
			logger::error(
				"UseObject entry RVA mismatch on {}: expected {:#x}, found {:#x}",
				version.string(),
				*expectedRva,
				actualRva);
			return std::nullopt;
		}

		return address;
	}

	std::optional<std::uintptr_t> ResolveSite(Site a_site)
	{
		const auto& site = GetSite(a_site);

		const auto owner = ResolveFunction(site.name, site.owner);
		if (!owner) {
			return std::nullopt;
		}

		if (HasIDForRuntime(site.callsiteTarget)) {
			const auto calls = REL::resolve_callsites(
				IsolateForRuntime(site.owner),
				IsolateForRuntime(site.callsiteTarget),
				site.branch);

			if (calls && calls.rvas.size() == 1) {
				logger::info(
					"{}: callsite discovered automatically at +{:#x}",
					site.name,
					calls.offsets.empty() ? std::ptrdiff_t{ 0 } : calls.offsets.front());
				return REL::Module::get().base() + calls.rvas.front();
			}

			logger::warn(
				"{}: automatic callsite unusable ({}, {} match(es)) - falling back to the fixed offset",
				site.name,
				REL::id_resolve_status_text(calls.status),
				calls.rvas.size());
		}

		const auto offset = OffsetForRuntime(site);
		if (offset == UNKNOWN_OFFSET) {
			logger::warn(
				"{}: no {} interior offset supplied - hook not installed",
				site.name,
				RuntimeFamilyName());
			return std::nullopt;
		}

		const auto address = *owner + static_cast<std::uintptr_t>(offset);
		if (Config::DebugLoggingEnabled()) {
			logger::debug(
				"{} using fixed offset {:#x} at address {:#x}",
				site.name,
				offset,
				address);
		}

		return address;
	}

	bool ValidateSite(Site a_site, std::uintptr_t a_address)
	{
		const auto& site = GetSite(a_site);
		const auto expectedOpcode =
			site.branch == REL::AutoCallsiteBranch::kJump ? std::uint8_t{ 0xE9 } : std::uint8_t{ 0xE8 };
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_address);

		if (bytes[0] != expectedOpcode) {
			logger::error(
				"{}: refusing hook at {:#x}: expected {:#04x}, found {:#04x}",
				site.name,
				a_address,
				expectedOpcode,
				bytes[0]);
			return false;
		}

		if (!HasIDForRuntime(site.callsiteTarget)) {
			return true;
		}

		const auto target = REL::IDDatabase::get().resolve(IsolateForRuntime(site.callsiteTarget));
		if (!target) {
			logger::error(
				"{}: refusing hook because target id {} cannot be resolved ({})",
				site.name,
				IDForRuntime(site.callsiteTarget),
				REL::id_resolve_status_text(target.status));
			return false;
		}

		std::int32_t displacement{ 0 };
		std::memcpy(std::addressof(displacement), bytes + 1, sizeof(displacement));
		const auto actualTarget = a_address + 5 + static_cast<std::intptr_t>(displacement);
		const auto expectedTarget = REL::Module::get().base() + *target.rva;
		if (actualTarget != expectedTarget) {
			logger::error(
				"{}: refusing hook at {:#x}: target {:#x} does not match expected {:#x}",
				site.name,
				a_address,
				actualTarget,
				expectedTarget);
			return false;
		}

		return true;
	}

	void LogCapabilityReport()
	{
		const auto describe = [](std::string_view a_name, const REL::ID& a_id) {
			if (!HasIDForRuntime(a_id)) {
				logger::info("  {:<26} MISSING  (no {} id)", a_name, RuntimeFamilyName());
				return;
			}

			const auto result = REL::IDDatabase::get().resolve(IsolateForRuntime(a_id));
			if (result) {
				logger::info(
					"  {:<26} ok       id {} -> rva {:#x}",
					a_name,
					IDForRuntime(a_id),
					*result.rva);
			} else {
				logger::info(
					"  {:<26} FAILED   id {} ({})",
					a_name,
					IDForRuntime(a_id),
					REL::id_resolve_status_text(result.status));
			}
		};

		logger::info(
			"AnimatedWorld address report - runtime {} (family {})",
			REL::Module::get().version().string(),
			RuntimeFamilyName());

		logger::info(" functions:");
		describe("PlayAction"sv, PlayAction);
		describe("ApplyMaterialSwap"sv, ApplyMaterialSwap);
		describe("IsActivationBlocked"sv, IsActivationBlocked);
		describe("WornHasKeyword"sv, WornHasKeyword);
		describe("PlayPipboyOpenAnim"sv, PlayPipboyOpenAnim);

		logger::info(" hook sites:");
		for (const auto& site : kHookSites) {
			describe(site.name, site.owner);
			if (HasIDForRuntime(site.owner) && OffsetForRuntime(site) == UNKNOWN_OFFSET &&
				!HasIDForRuntime(site.callsiteTarget)) {
				logger::info(
					"  {:<26} ...but no {} interior offset or callsite target",
					site.name,
					RuntimeFamilyName());
			}
		}
	}
}
