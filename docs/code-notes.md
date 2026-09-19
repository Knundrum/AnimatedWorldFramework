# Code Notes

These notes were moved out of the C++ source files. Decorative section dividers were omitted.

## Addresses

`src/Addresses.h` contains every address used by the plugin.

`REL::ID` values are ordered `(OG, NG, AE)`:

- `REL::ID{ ae }` is one portable AE ID, bridged to OG/NG when possible.
- `REL::ID{ og, ae }` uses the AE ID for NG.
- `REL::ID{ og, ng, ae }` specifies all three runtimes explicitly.

OG and AE use independent numeric ID spaces. There is no OG-to-AE bridge. `IDDatabase::resolve()` consults `ae_id()` and nothing else when the game is AE. Putting an OG number in the AE slot can resolve an unrelated AE function, causing a hook to land in the wrong location. Unknown runtime IDs therefore remain `UNKNOWN_ID`, and the affected feature is not installed.

To add AE/NG support, replace an unknown ID with the verified ID and provide the matching interior offset for hook sites. The startup capability report will pick it up. A `callsiteTarget` can also identify the called function so CommonLibF4RD can discover the offset through `REL::AUTO_CALLSITE`.

`UNKNOWN_ID` means an ID has not been determined for a runtime family. `UNKNOWN_OFFSET` means an interior hook offset has not been determined.

Direct function metadata:

- `PlayAction`: `Actor::PerformAction(BGSAction*, TESObjectREFR*)`.
- `ApplyMaterialSwap`: `ApplySwap(NiAVObject*, BGSMaterialSwap const*, float, float, void*)`. ID `2189271` has the matching 123-byte implementation shape on NG at RVA `0x201670` and AE at RVA `0x256060`, matching OG RVA `0x531B0`.
- `IsActivationBlocked`: `TESObjectREFR::IsActivationBlocked(TESObjectREFR*)`. Its native binding is enabled only on exact runtimes `1.10.163.0`, `1.10.984.0`, and `1.11.240.0`.
- `WornHasKeyword`: `TESObjectREFR::WornHasKeyword(TESObjectREFR*, BGSKeyword*)`. This exists in the old in-tree CommonLib fork but not CommonLibF4RD. Its native binding is enabled only on exact runtimes `1.10.163.0`, `1.10.984.0`, and `1.11.240.0`. The AE ID came from commonlib_NonVR PR #88 on 2026-09-17, which identifies AE `2200995` at RVA `0x507940` on 1.11.240 using Ghidra and Address Library evidence. This is static proof only; no live runtime result was claimed and the pre-merge runtime probe was not run. Verify the RVA against a real AE session before relying on it.
- `PlayPipboyOpenAnim`: `PipboyManager::PlayPipboyOpenAnim(PipboyManager*, const BSFixedString&)`. This exists in the old in-tree CommonLib fork but not CommonLibF4RD.

For hook sites, `owner` is the function containing the replaced call and offsets are relative to that function. Runtime slots are explicit; an unverified family remains `UNKNOWN_ID` or `UNKNOWN_OFFSET`.

`ProcessLists::RunActorUpdates` is different from the owner of its hook. Its ID must not be placed in the owner slot.

Each `HookSite` contains:

- The function containing the call.
- One interior offset per runtime family.
- An optional called-function ID used by `REL::resolve_callsites`; fixed offsets are the fallback.
- The branch kind. Five sites use E8 calls; the Pip-Boy light site uses an E9 tail-call jump. A jump is not matched as a call, so the branch kind must be correct.
- Whether the site is required for the plugin to be useful.

Hook-site facts:

- `RunActorUpdates` is the per-frame driver for all timed behavior. Owner `556439` is unnamed in CommonLibF4RD; its callee has signature `void(void*)`. The site name describes the callee rather than the owner. Its callee is `Main::OnIdle_UpdatePlayer`, OG ID `1318162`, AE ID `2228929`.
- `AddAcquiredEvent` is the player item-acquired event. Its OG callee is ID `876119`, RVA `0xe9f220`, and is unnamed in CommonLibF4RD.
- `ActivateRef` has owner `785533`. Its verified callsite is `+0x38A` on OG and `+0x31D` on NG/AE. The callee is OG ID `753531`, named `TESObjectREFR::ActivateRef` with AE ID `2201147`. Automatic resolution is active for this site; the fixed OG offset remains the fallback. If activation animations break, the callsite target can be removed and the log will show whether automatic resolution or fallback was used.
- `HandlePlayerItem` is inside `TESObjectREFR::AddInventoryItem`, OG ID `78185`, NG/AE ID `2200949`, at `+0xA40` on OG and `+0xA4A` on NG/AE. Its callee is OG ID `357079`, NG/AE ID `2194003`, with NG RVA `0x2f30d0` and AE RVA `0x3477c0`.
- On OG, `EquipObject` calls `UseObject` at `+0x15A`. On NG/AE, `EquipObject +0xE0` calls `GetDesiredEquipSlot`; the real `UseObject` entry is not a direct callsite. Exact verified NG `1.10.984.0` and AE `1.11.240.0` use the MinHook entry path, while every other runtime remains fail-closed. The entry resolver checks the resolved RVA against NG `0xc61490` and AE `0xce7460` before MinHook is called. `UseObject` is OG ID `301794`, RVA `0xe1d100`, and NG/AE ID `2231408`. It is unnamed in CommonLibF4RD. Static RE proof does not constitute live runtime acceptance.
- `SetInputDeviceLightState` is inside `PlayerCharacter::TogglePipBoyLight`, OG ID `520007`, AE ID `2233201`. The callsite dump identifies an E9 tail-call jump. Its OG callee is ID `157452`, RVA `0x1b2c080`, and is unnamed in CommonLibF4RD.

Address resolution returns `std::nullopt` instead of calling `stl::report_and_fail` when an optional ID cannot be resolved. Optional `REL::Relocation` constructors are avoided because they terminate the process on failure.

Entry hooks use MinHook revision `c3fcafdc10146beb5919319d0683e44e3c30d537`. Initialization, creation, enablement, and partial cleanup errors fail closed; no shutdown path is needed during the plugin process lifetime.

The four-argument ABI has these local proofs. The NG `Fallout4.984.exe.unpacked.exe` image uses base `0x140000000`; at `0x140c61490` (RVA `0xc61490`), the 315-byte function saves `R8` to `RSI`, reads the fourth argument through `[RSI]` and `[RSI+8]`, uses `RDX` as the object-instance argument, and returns its boolean result through `AL`. The AE `Fallout4.240.exe.unpacked.exe` image has the same 315-byte function shape at `0x140ce7460` (RVA `0xce7460`): `mov [rsp+8], rbx`, `mov [rsp+0x10], rsi`, `push rdi`, `mov rsi, r8`, `mov rbx, rdx`, and the same `[RSI]`/`[RSI+8]` reads. The reference `commonlib_NonVR/include/RE/O/ObjectEquipParams.h` proves the parameter layout with `stackID` at `0x00`, `number` at `0x04`, slot pointers at `0x08` and `0x10`, flags at `0x18` through `0x1D`, and `static_assert(sizeof(ObjectEquipParams) == 0x20)`.

## Game Wrappers

`src/Game.h` contains thin engine wrappers and the two APIs carried by the old in-tree CommonLib fork but absent from CommonLibF4RD: `TESObjectREFR::WornHasKeyword` and `PipboyManager::PlayPipboyOpenAnim`.

All wrappers are optional. If an address cannot be resolved on the running runtime, the wrapper is inert and its matching `Can*()` query reports that state instead of allowing `REL::Relocation` construction to terminate the process.

The `PlayAction` wrapper calls `Actor::PerformAction(BGSAction*, TESObjectREFR*)`; the actor is the implicit `this` argument.

Clip timing walks raw Havok structures by byte offset. Runtime-aware addresses do not make class layouts portable; CommonLibF4RD's `structureIndependence` caveat still applies. The active-node member is an `hkArray<hkbNodeInfo*>*`; each entry is followed to its node clone, filtered by the resolved `hkbClipGenerator` vtable, and then read through the verified `hkbNode` fields. Offsets are trusted only on exact verified runtimes `1.10.163.0`, `1.10.984.0`, and `1.11.240.0`. On other versions, `ReadCurrentClip()` fails and the caller uses a fixed delay instead of dereferencing guesses.

An active generator may be identified by name before its animation binding is reachable. In that state `name` is trustworthy but `duration` is not, so callers must not compute remaining time from it.

The animation graph offsets were verified against OG 1.10.163, NG 1.10.984, and AE 1.11.240. The manager/cache/graph chain was confirmed by runtime constructors, and the Havok member offsets match the SDK layout and corresponding NG/AE constructors.

The active-generator array should never contain anywhere near 512 entries. The limit prevents a corrupt or unexpected array from walking off the heap.

The original clip walk assigned a clip name for every live generator and only stopped once the full animation-binding chain resolved. Requiring the entire chain lost the `DynamicIdle` match that drives the item-added animation. The current behavior keeps the looser name matching without using a stale duration.

Zero user data means a generator is not currently driving a clip. A live generator whose duration cannot be read still provides a usable name for deciding what is playing.

## Hooks

The plugin content uses the `Animated World - Base.esp` forms for the idle-stop fix, activation, item-added, equip, flashlight, and Pip-Boy equip animations.

Timing values:

- Animation settle delay: `100ms`.
- Ground pickup delay: `100ms`.
- Material swap delay: `200ms`.
- Dynamic idle tail: `200ms`.
- Unreadable clip fallback: `700ms`.
- Idle-stop timeout: `2s`.
- Pending-animation timeout: `3s`.

The unreadable-clip fallback is used when the clip walk is unavailable because a runtime layout is unverified. The idle-stop fix expires so an unrelated later `IdleStop` cannot consume it. A pending animation eventually gives up instead of polling every frame for the rest of the session.

Dummy references are retargeted to the item being animated so one `BGSAction` can drive any object. Pending material swaps are stored with their item so a swap from an earlier pickup cannot be applied to an unrelated item through another path.

Debug logging is controlled by `Data/F4SE/Plugins/AnimatedWorld.ini`. Set `[AnimatedWorld] DebugLogging = true` to enable detailed address, binding, hook, event, and animation-state logs. It is disabled by default. The clip name is logged only when it changes, preventing the settling poll from flooding the log.

The animation graph event sink is patched into the player's vtable. It needs no address ID because the sink slot index is part of the interface and works on every runtime. Vtable slot 0 is the destructor and slot 1 is `ProcessEvent`.

An expired idle-stop fix must not survive into an unrelated animation. The ground-pickup and `DynamicIdle` conditions are not mutually exclusive; a playing `DynamicIdle` wins over the ground shortcut. The pickup or activation animation must finish before the item-added animation starts, or the new action can be swallowed by the still-running idle. If a clip is identified but not yet timeable, polling continues because the binding normally resolves within a frame or two. If the struct layout is unverified, the clip walk is disabled and a fixed delay is used.

The original acquired-event path rejected null pointers but continued executing. The guard now calls the original and returns before using those pointers.

Playing the equip animation tears down the Pip-Boy, so it is used only when the menu can be reopened. The original flashlight path fired unconditionally, including while the player had no 3D during loading or at the main menu.

The fourth `UseObject` parameter is private and not exposed by CommonLibF4RD. The hook keeps it as an opaque pointer to preserve the ABI without depending on the fork-specific `ObjectEquipParams` type. MinHook owns the entry relocation and supplies the original trampoline; CommonLibF4RD `write_branch<5>` is not used at this function entry.

The per-frame driver is required because all timed follow-ups depend on it. If it cannot be installed, the plugin remains inactive. The activation hook requires the blocked-reference test; otherwise it could animate references the game refuses to activate.

The repository template is `config/AnimatedWorld.ini`. A `COPY_BUILD=ON` CMake build copies it beside the plugin DLL automatically.

## Diagnostics

`AnimatedWorld.findcallsites` is an empty marker file next to the F4SE log:

`Documents\\My Games\\Fallout4\\F4SE\\AnimatedWorld.findcallsites`

Before hooks are installed, the diagnostic decodes each hook site's branch and reports the called function as a Runtime Database ID. That ID belongs in the matching `HookSite::callsiteTarget`; CommonLibF4RD can then find the callsite and fixed interior offsets stop mattering across runtimes.

Run the dump once on OG to learn callee IDs, then look up those callees rather than the hook owners. They are ordinary named functions and may already be dual-keyed in CommonLibF4RD headers.

The INI is loaded once during plugin startup. The callsite dump must run before `Hooks::Install()` or it will decode the plugin's trampolines instead of the game's original branches.

The diagnostic decoder handles the E8/E9 rel32 instruction replaced by `write_call` or `write_branch`. Reverse lookup searches the sorted RVA-to-ID container directly because calling `REL::IDDatabase::Offset2ID::operator()` for an unknown function start invokes `stl::report_and_fail` and would terminate the game.

## Plugin Loading

Addresses come from CommonLibF4RD's runtime database rather than a fixed executable layout, so a new patch number alone is not a reason for F4SE to reject the plugin. Class-layout independence is separate; the plugin advertises its supported layouts, and features depending on an unverified layout disable themselves at runtime.

There is deliberately no executable-version whitelist. Unknown runtimes reach the runtime database, and the required resolved addresses determine whether the plugin can work.

The callsite diagnostic runs before any hook is installed so it reads the game's original branches rather than the plugin's trampolines.

OG, NG, and AE all enter through the same F4SE load function. There is no `F4SEPlugin_Query` or `compatibleVersions` whitelist; `F4SEPlugin_Version` declares address and structure independence and CommonLibF4RD decides at runtime whether the required addresses exist.

`Hooks::Install()` reports false when no resolved hook can be installed.
