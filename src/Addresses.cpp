#include "Addresses.h"

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

		// The id actually used on this runtime, for logging.
		[[nodiscard]] std::uint64_t IDForRuntime(const REL::ID& a_id) noexcept
		{
			return a_id.id();
		}

		// Strips every id except the running family's.
		//
		// IDDatabase::resolve() on OG tries ae_id() as a runtime pattern FIRST
		// and only falls back to the OG legacy table if that misses.  So a real
		// AE id sitting in the AE slot can pattern-match somewhere inside the OG
		// executable and win, and the hook is then written into an unrelated
		// function with no error reported.  Handing resolve() an ID that carries
		// only the current family's number removes that whole failure mode: on
		// OG the empty AE slot cannot match, so the legacy table always decides.
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

		return REL::Module::get().base() + *result.rva;
	}

	std::optional<std::uintptr_t> ResolveSite(Site a_site)
	{
		const auto& site = GetSite(a_site);

		const auto owner = ResolveFunction(site.name, site.owner);
		if (!owner) {
			return std::nullopt;
		}

		// Preferred: let CommonLibF4RD find the call itself.  Survives a shifted
		// function body or inserted instructions, and fails loudly rather than
		// guessing when the call is gone or ambiguous.
		if (HasIDForRuntime(site.callsiteTarget)) {
			const auto calls = REL::resolve_callsites(
				IsolateForRuntime(site.owner),
				IsolateForRuntime(site.callsiteTarget),
				REL::AutoCallsiteBranch::kCall);

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

		return *owner + static_cast<std::uintptr_t>(offset);
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
