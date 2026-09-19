#pragma once

namespace AW::Addresses
{
	inline constexpr std::uint64_t UNKNOWN_ID = REL::ID::INVALID_ID;

	inline constexpr std::ptrdiff_t UNKNOWN_OFFSET = (std::numeric_limits<std::ptrdiff_t>::min)();

	inline constexpr REL::ID PlayAction{ 1057231, 2231177, 2231177 };

	inline constexpr REL::ID ApplyMaterialSwap{ 708895, 2189271, 2189271 };

	inline constexpr REL::ID IsActivationBlocked{ 407609, 2201146, 2201146 };

	inline constexpr REL::ID WornHasKeyword{ 900857, 2200995, 2200995 };

	inline constexpr REL::ID PlayPipboyOpenAnim{ 663900, 2225444, 2225444 };

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

		REL::ID owner;

		std::ptrdiff_t ogOffset;
		std::ptrdiff_t ngOffset;
		std::ptrdiff_t aeOffset;

		REL::ID callsiteTarget{};

		REL::AutoCallsiteBranch branch{ REL::AutoCallsiteBranch::kCall };

		bool required{ false };
	};

	inline constexpr std::array<HookSite, static_cast<std::size_t>(Site::kTotal)> kHookSites{ {
		HookSite{
			.name = "RunActorUpdates"sv,
			.owner = REL::ID{ 556439, 2227608, 2227608 },
			.ogOffset = 0xF0,
			.ngOffset = 0xF0,
			.aeOffset = 0xF0,
			.callsiteTarget = REL::ID{ 1318162, 2228929, 2228929 },
			.required = true },

		HookSite{
			.name = "AddAcquiredEvent"sv,
			.owner = REL::ID{ 1401485, 2221653, 2221653 },
			.ogOffset = 0x2D6,
			.ngOffset = 0x354,
			.aeOffset = 0x354,
			.callsiteTarget = REL::ID{ 876119, 2232930, 2232930 } },

		HookSite{
			.name = "ActivateRef"sv,
			.owner = REL::ID{ 785533, 2233039, 2233039 },
			.ogOffset = 0x38A,
			.ngOffset = 0x31D,
			.aeOffset = 0x31D,
			.callsiteTarget = REL::ID{ 753531, 2201147, 2201147 } },

		HookSite{
			.name = "HandlePlayerItem"sv,
			.owner = REL::ID{ 78185, 2200949, 2200949 },
			.ogOffset = 0xA40,
			.ngOffset = 0xA4A,
			.aeOffset = 0xA4A,
			.callsiteTarget = REL::ID{ 357079, 2194003, 2194003 } },

		HookSite{
			.name = "UseObject"sv,
			.owner = REL::ID{ 988029, 2231392, 2231392 },
			.ogOffset = 0x15A,
			.ngOffset = UNKNOWN_OFFSET,
			.aeOffset = UNKNOWN_OFFSET,
			.callsiteTarget = REL::ID{ 301794, 2231408, 2231408 } },

		HookSite{
			.name = "SetInputDeviceLightState"sv,
			.owner = REL::ID{ 520007, 2233201, 2233201 },
			.ogOffset = 0x5B,
			.ngOffset = 0x5B,
			.aeOffset = 0x5B,
			.callsiteTarget = REL::ID{ 157452, 2268396, 2268396 },
			.branch = REL::AutoCallsiteBranch::kJump },
	} };

	[[nodiscard]] constexpr const HookSite& GetSite(Site a_site) noexcept
	{
		return kHookSites[static_cast<std::size_t>(a_site)];
	}

	[[nodiscard]] bool HasIDForRuntime(const REL::ID& a_id) noexcept;

	[[nodiscard]] bool IsVerifiedRuntime() noexcept;

	[[nodiscard]] bool IsVerifiedUseObjectRuntime() noexcept;

	[[nodiscard]] std::optional<std::uintptr_t> ResolveFunction(
		std::string_view a_name,
		const REL::ID& a_id);

	[[nodiscard]] std::optional<std::uintptr_t> ResolveUseObjectEntry();

	[[nodiscard]] std::optional<std::uintptr_t> ResolveSite(Site a_site);

	[[nodiscard]] bool ValidateSite(Site a_site, std::uintptr_t a_address);

	void LogCapabilityReport();

	[[nodiscard]] std::string_view RuntimeFamilyName() noexcept;
}
