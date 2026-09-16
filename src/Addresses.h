#pragma once

// ---------------------------------------------------------------------------
// Every address this plugin depends on lives in this file.
//
// CommonLibF4RD's REL::ID takes ids in the order (OG, NG, AE):
//
//     REL::ID{ ae }            one portable AE id, bridged to OG/NG if possible
//     REL::ID{ og, ae }        NG shares the AE id
//     REL::ID{ og, ng, ae }    all three explicit
//
// OG and AE use INDEPENDENT numeric id spaces.  There is no OG -> AE bridge:
// IDDatabase::resolve() consults ae_id() and nothing else when the game is AE.
// Putting an OG number in the AE slot therefore does not fail - it resolves to
// whatever unrelated function owns that number on AE, and the hook lands in the
// middle of it.  So an id that is not known for a runtime family is left as
// UNKNOWN_ID and the owning feature is simply not installed on that runtime.
//
// TO ADD AE/NG SUPPORT: replace an UNKNOWN_ID with the real id and, for hook
// sites, the matching interior offset.  Nothing else needs to change; the
// capability report at startup will pick it up.  Better still, give the site a
// `callsiteTarget` (the id of the function being called at that site) and
// CommonLibF4RD will discover the offset itself via REL::AUTO_CALLSITE.
// ---------------------------------------------------------------------------

namespace AW::Addresses
{
	// An id that has not been determined for a runtime family yet.
	inline constexpr std::uint64_t UNKNOWN_ID = REL::ID::INVALID_ID;

	// An interior hook offset that has not been determined yet.
	inline constexpr std::ptrdiff_t UNKNOWN_OFFSET = (std::numeric_limits<std::ptrdiff_t>::min)();

	// -----------------------------------------------------------------------
	// Functions the plugin calls directly.
	// -----------------------------------------------------------------------

	// bool PlayAction(Actor*, BGSAction*, TESObjectREFR*, void*, uint32_t)
	inline constexpr REL::ID PlayAction{ 1451490, UNKNOWN_ID };

	// void ApplySwap(NiAVObject*, BGSMaterialSwap const*, float, float, void*)
	inline constexpr REL::ID ApplyMaterialSwap{ 708895, UNKNOWN_ID };

	// bool TESObjectREFR::IsActivationBlocked(TESObjectREFR*)
	inline constexpr REL::ID IsActivationBlocked{ 407609, UNKNOWN_ID };

	// bool TESObjectREFR::WornHasKeyword(TESObjectREFR*, BGSKeyword*)
	// Present in the old in-tree CommonLibF4 fork, absent from CommonLibF4RD.
	inline constexpr REL::ID WornHasKeyword{ 900857, UNKNOWN_ID };

	// void PipboyManager::PlayPipboyOpenAnim(PipboyManager*, const BSFixedString&)
	// Present in the old in-tree CommonLibF4 fork, absent from CommonLibF4RD.
	inline constexpr REL::ID PlayPipboyOpenAnim{ 663900, UNKNOWN_ID };

	// -----------------------------------------------------------------------
	// Hook sites.
	//
	// `owner` is the function that CONTAINS the call being replaced; the
	// offsets are relative to the start of that function.  AE ids marked below
	// were recovered from CommonLibF4RD's own headers, which carry (OG, AE)
	// pairs; the interior offsets for those runtimes are still unknown, so the
	// sites stay disabled until either the offset or a callsite target is
	// supplied.
	// -----------------------------------------------------------------------

	enum class Site : std::size_t
	{
		kRunActorUpdates,
		kAddAcquiredEvent,
		kActivateRef,
		kHandlePlayerItem,
		kUseObject,
		kSetInputDeviceLightState,

		kTotal
	};

	struct HookSite
	{
		std::string_view name;

		// Function containing the call to replace.
		REL::ID owner;

		// Interior offsets, one per runtime family.
		std::ptrdiff_t ogOffset;
		std::ptrdiff_t ngOffset;
		std::ptrdiff_t aeOffset;

		// Optional: the id of the function called at this site.  When set and
		// resolvable, REL::resolve_callsites finds the offset itself and the
		// fixed offsets above are only a fallback.  This is the update-resilient
		// form recommended by CommonLibF4RD and is worth filling in even for OG.
		REL::ID callsiteTarget{};

		// True when this site must exist for the plugin to be worth loading.
		bool required{ false };
	};

	inline constexpr std::array<HookSite, static_cast<std::size_t>(Site::kTotal)> kHookSites{ {
		// Per-frame actor update driver.  Everything time-based hangs off this
		// one, so without it the plugin does nothing at all.
		HookSite{
			.name = "RunActorUpdates"sv,
			.owner = REL::ID{ 556439, UNKNOWN_ID },
			.ogOffset = 0xF0,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET,
			.required = true },

		// PlayerCharacter item-acquired event.
		HookSite{
			.name = "AddAcquiredEvent"sv,
			.owner = REL::ID{ 1401485, UNKNOWN_ID },
			.ogOffset = 0x2D6,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET },

		// TESObjectREFR activation.
		HookSite{
			.name = "ActivateRef"sv,
			.owner = REL::ID{ 785533, UNKNOWN_ID },
			.ogOffset = 0x38A,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET },

		// Player pickup path.  AE id 2200949 read from CommonLibF4RD headers.
		HookSite{
			.name = "HandlePlayerItem"sv,
			.owner = REL::ID{ 78185, 2200949 },
			.ogOffset = 0xA40,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET },

		// ActorEquipManager use/equip.  AE id 2231392 read from CommonLibF4RD.
		HookSite{
			.name = "UseObject"sv,
			.owner = REL::ID{ 988029, 2231392 },
			.ogOffset = 0x15A,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET },

		// Pip-Boy light toggle.  AE id 2233201 read from CommonLibF4RD.
		HookSite{
			.name = "SetInputDeviceLightState"sv,
			.owner = REL::ID{ 520007, 2233201 },
			.ogOffset = 0x5B,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET },
	} };

	[[nodiscard]] constexpr const HookSite& GetSite(Site a_site) noexcept
	{
		return kHookSites[static_cast<std::size_t>(a_site)];
	}

	// -----------------------------------------------------------------------
	// Resolution.  None of these ever call stl::report_and_fail: an id that
	// cannot be resolved yields std::nullopt and the caller declines the
	// feature.  REL::Relocation's constructor is deliberately not used for
	// anything optional, because it terminates the process on failure.
	// -----------------------------------------------------------------------

	// True when a_id carries an id for the family the game is actually running.
	[[nodiscard]] bool HasIDForRuntime(const REL::ID& a_id) noexcept;

	// Absolute address of a function, or nullopt with a logged reason.
	[[nodiscard]] std::optional<std::uintptr_t> ResolveFunction(
		std::string_view a_name,
		const REL::ID& a_id);

	// Absolute address of a hook site, or nullopt with a logged reason.
	[[nodiscard]] std::optional<std::uintptr_t> ResolveSite(Site a_site);

	// One block in the log naming every id, whether it resolved, and why not.
	void LogCapabilityReport();

	[[nodiscard]] std::string_view RuntimeFamilyName() noexcept;
}
