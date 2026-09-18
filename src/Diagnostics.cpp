#include "Diagnostics.h"

#include "Addresses.h"

#include <algorithm>
#include <filesystem>

namespace AW::Diagnostics
{
	namespace
	{
		constexpr auto CALLSITE_MARKER_NAME = "AnimatedWorld.findcallsites";

		constexpr std::uint8_t OPCODE_CALL_REL32 = 0xE8;
		constexpr std::uint8_t OPCODE_JMP_REL32 = 0xE9;
		constexpr std::size_t REL32_INSTRUCTION_SIZE = 5;

		struct Branch
		{
			std::uintptr_t target{ 0 };
			const char* kind{ "" };
			bool valid{ false };
		};

		[[nodiscard]] Branch DecodeBranch(std::uintptr_t a_site)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_site);

			const char* kind = nullptr;
			switch (bytes[0]) {
			case OPCODE_CALL_REL32:
				kind = "call";
				break;
			case OPCODE_JMP_REL32:
				kind = "jmp";
				break;
			default:
				return {};
			}

			std::int32_t displacement{ 0 };
			std::memcpy(std::addressof(displacement), bytes + 1, sizeof(displacement));

			Branch branch;
			branch.target = a_site + REL32_INSTRUCTION_SIZE + static_cast<std::intptr_t>(displacement);
			branch.kind = kind;
			branch.valid = true;
			return branch;
		}

		class ReverseLookup
		{
		public:
			ReverseLookup() :
				_table{}
			{}

			[[nodiscard]] bool empty() const noexcept { return _table.size() == 0; }

			[[nodiscard]] std::optional<std::uint64_t> operator()(std::size_t a_rva) const
			{
				const auto it = std::lower_bound(
					_table.begin(),
					_table.end(),
					a_rva,
					[](const auto& a_entry, std::size_t a_value) { return a_entry.offset < a_value; });

				if (it == _table.end() || it->offset != a_rva) {
					return std::nullopt;
				}

				return it->id;
			}

		private:
			REL::IDDatabase::Offset2ID _table;
		};

		[[nodiscard]] bool MarkerExists(const char* a_name)
		{
			auto directory = logger::log_directory();
			if (!directory) {
				return false;
			}

			*directory /= a_name;

			std::error_code error;
			return std::filesystem::exists(*directory, error) && !error;
		}
	}

	bool CallsiteDumpRequested()
	{
		return MarkerExists(CALLSITE_MARKER_NAME);
	}

	void DumpCallsiteTargets()
	{
		const auto base = REL::Module::get().base();

		logger::info("--- callsite target dump ({}) ---", Addresses::RuntimeFamilyName());
		logger::info(
			"Fill these ids into the matching HookSite's callsiteTarget in Addresses.h,");
		logger::info(
			"then the interior offsets stop being needed on any runtime.");

		ReverseLookup toID;
		if (toID.empty()) {
			logger::warn(
				"the id<-callsite table is empty on this runtime; callee RVAs are still");
			logger::warn(
				"reported below and can be looked up in a disassembler by hand");
		}

		for (std::size_t i = 0; i < static_cast<std::size_t>(Addresses::Site::kTotal); ++i) {
			const auto siteEnum = static_cast<Addresses::Site>(i);
			const auto& site = Addresses::GetSite(siteEnum);

			const auto address = Addresses::ResolveSite(siteEnum);
			if (!address) {
				logger::info("  {:<26} site address unavailable - nothing to decode", site.name);
				continue;
			}

			const auto branch = DecodeBranch(*address);
			if (!branch.valid) {
				logger::info(
					"  {:<26} site rva {:#x}: not a rel32 call/jmp (first byte {:#04x})",
					site.name,
					*address - base,
					*reinterpret_cast<const std::uint8_t*>(*address));
				continue;
			}

			const auto targetRva = branch.target - base;
			const auto id = toID(targetRva);

			if (id) {
				logger::info(
					"  {:<26} {} -> rva {:#x} = id {}",
					site.name,
					branch.kind,
					targetRva,
					*id);
			} else {
				logger::info(
					"  {:<26} {} -> rva {:#x} (no id for this rva; it may not be a function start)",
					site.name,
					branch.kind,
					targetRva);
			}
		}

		logger::info("--- end callsite target dump ---");
	}
}
