# AnimatedWorld (CommonLibF4RD)

Port of AnimatedWorld from the in-tree CommonLibF4 fork to
[CommonLibF4RD](https://github.com/Zzyxz/CommonLibF4RD).

Behaviour on Fallout 4 1.10.163 (OG) is unchanged apart from the bug fixes listed
below. On NG and AE the plugin loads, prints an address report, and installs only
the hooks whose addresses are known — see **Finishing NG/AE support**.

## Building

> **Visual Studio 2022 only.** CommonLibF4RD needs the **v143** toolset.
> Visual Studio 2019 ships v142 and cannot build it — you get
> `MSB8020: The build tools for v143 ... cannot be found`.
> If VS2019 offers to **"Retarget solution"**, say no: that downgrades the
> generated project to v142, and CMake overwrites it on the next configure
> anyway.

The simplest route, which picks VS2022 for you even if VS2019 is also installed:

```
git clone https://github.com/Zzyxz/CommonLibF4RD.git external/CommonLibF4RD
build.bat            :: Release
build.bat Debug      :: Debug
```

Or by hand, from a **VS2022** x64 Native Tools prompt:

```
cmake --preset vs2022-windows-vcpkg
cmake --build --preset vs2022-release
```

To work in the IDE, open `build\vs2022\AnimatedWorld.sln` **in Visual Studio
2022**, not 2019. A `ninja-windows-vcpkg` preset is also available for faster
iteration; it needs a VS2022 developer prompt so that `cl.exe` is v143.

`VCPKG_ROOT` must be set. To use a CommonLibF4RD checkout that lives somewhere
else, configure with `-DCOMMONLIBF4RD_PATH=<path>`.

`CMakeSettings.json` is kept for the "Open Folder" workflow — but Visual Studio
prefers it over `CMakePresets.json`, and it builds with whichever Visual Studio
opened the folder. Open the folder in VS2022, or delete that file and use the
presets.

### Do not put `/W4 /WX` in `CMAKE_CXX_FLAGS`

Those are global, so they also apply to CommonLibF4RD, which is added with
`add_subdirectory`. Its sources do not build warning-clean at `/W4` — its
CMakeLists suppresses a list that does not include C4100, and
`Actor.h` has an unreferenced `a_mount` parameter. The library then fails to
produce `CommonLibF4.lib` and this project dies much later with the misleading

```
LINK : fatal error LNK1181: cannot open input file 'CommonLibF4\Release\CommonLibF4.lib'
```

The presets therefore pass only `/EHsc /MP`; `/W4 /WX` and the suppression list
live in `target_compile_options` on the AnimatedWorld target. `CMakeLists.txt`
additionally silences C4100 on the `CommonLibF4` target so a stray global flag
cannot resurrect this.

End users additionally need the CommonLibF4RD Runtime Database at
`Data/F4SE/Plugins/f4rd-runtime.bin`. It ships separately from the library.

## Finishing NG/AE support

CommonLibF4RD makes *offsets* runtime-independent, not *ids*: OG and AE use
separate numeric id spaces and the only automatic bridge runs AE → OG. An OG id
left in the AE slot does not fail — it resolves to whatever unrelated function
owns that number on AE — so unknown ids are left as `UNKNOWN_ID` and the feature
that needs them is simply not installed.

Everything to fill in is in **`src/Addresses.h`**. Nothing else needs editing.

| What | OG id | NG id | AE id | OG offset | NG/AE offset |
| --- | --- | --- | --- | --- | --- |
| `PlayAction` | 1451490 | — | **TODO** | — | — |
| `ApplyMaterialSwap` | 708895 | — | **TODO** | — | — |
| `IsActivationBlocked` | 407609 | — | **TODO** | — | — |
| `WornHasKeyword` | 900857 | — | **TODO** | — | — |
| `PlayPipboyOpenAnim` | 663900 | — | **TODO** | — | — |
| `RunActorUpdates` | 556439 | — | **TODO** | 0xF0 | **TODO** |
| `AddAcquiredEvent` | 1401485 | — | **TODO** | 0x2D6 | **TODO** |
| `ActivateRef` | 785533 | — | **TODO** | 0x38A | **TODO** |
| `HandlePlayerItem` | 78185 | — | 2200949 | 0xA40 | **TODO** |
| `UseObject` | 988029 | — | 2231392 | 0x15A | **TODO** |
| `SetInputDeviceLightState` | 520007 | — | 2233201 | 0x5B | **TODO** |

The three AE ids that are already filled in were read out of CommonLibF4RD's own
headers, which carry `REL::ID(og, ae)` pairs. Everything the plugin reaches
*through* CommonLibF4RD (`PlayerCharacter::GetSingleton`, `UI`, `PipboyManager`,
the form factories, …) is already dual-keyed by the library and needs nothing.

Two things to prefer over a hardcoded AE offset:

- **`callsiteTarget`.** Each `HookSite` has one. Set it to the id of the function
  being *called* at that site and CommonLibF4RD locates the call itself via
  `REL::AUTO_CALLSITE`, surviving a shifted function body. It is used in
  preference to the fixed offsets on every runtime, including OG.
- **A `.trace` file.** Drop an empty `AnimatedWorld.trace` next to the DLL and
  CommonLibF4RD writes every id it was asked for, with the resolved RVA. The
  plugin also logs its own report at startup either way.

### Structure layouts are a separate problem

Runtime-aware addresses do not make class layouts portable. `Game::ReadCurrentClip`
walks Havok structures by raw byte offset (`AnimGraphLayout` in `src/Game.cpp`),
verified only on OG. On NG/AE it is marked unverified and disabled, and the item
animation falls back to a fixed delay rather than dereferencing guesses. Once the
offsets are confirmed on a real NG/AE binary, drop the `Unverified()` call.

## What changed in the port

**Plugin entry point.** `F4SEPlugin_Query` and the `RUNTIME_1_10_162` version
check are gone, replaced by a `constinit F4SEPlugin_Version` declaring
`kAddressIndependence_Signatures`. Per CommonLibF4RD's guidance an unknown patch
number is no longer a reason to refuse to load — whether the plugin can work is
decided by whether its addresses resolve. `AllocTrampoline` moved from `Query`
into `Load`.

**Addresses.** All eleven ids and six interior offsets moved out of the middle of
`hooks.cpp` into `src/Addresses.h`. Nothing uses `REL::Relocation`'s constructor
for an optional address, because that constructor calls `stl::report_and_fail`
and kills the process; resolution goes through `IDDatabase::resolve`, which
reports failure instead.

**APIs the RD library does not have.** The old in-tree CommonLibF4 was a fork
carrying `TESObjectREFR::WornHasKeyword`, `PipboyManager::PlayPipboyOpenAnim` and
a full `BSAnimationGraphManager` definition. Upstream CommonLibF4RD has none of
them, so they are reimplemented locally in `src/Game.{h,cpp}` and the library is
left unforked. `BSAnimationGraphEvent::tag` is `animEvent` upstream (same offset,
same size).

**Dropped.** `src/detourxs/` — nothing referenced it, and `LDE64x64.lib` was never
linked, so it was dead weight. The unused `playerRef` global and the declared-but-
never-defined `InstallVTableHook()` are gone too.

### Bug fixes

*Crash safety*

- `HookedAddEvent`'s null guard called the original and then **fell through and
  used the null pointers anyway** — a missing `return`.
- The per-frame update called `PlayAction` with `AW_ItemAddedAction` without
  checking it resolved; with the esp missing or not yet loaded that is a null
  action pointer.
- `RE::UI::GetSingleton()` and `RE::TESDataHandler::GetSingleton()` were
  dereferenced unchecked.
- `GetClipInfo` walked the active-generator array until it found a null with no
  bound, and null-checked only some of the pointers on the way down. Every step
  is now checked and the walk is bounded.
- The player vtable patch now verifies it read a non-null original before
  writing, and the `try`/`catch` around a call that cannot throw is gone.

*Behaviour*

- **Stale material swap.** `g_currentSwap` was set on the pickup path and never
  cleared, so a book's swap could be applied to an unrelated item that arrived
  later through a different path. The swap is now remembered together with the
  item it came from and is only applied if the animation target still holds that
  item — which also makes it independent of the order the two hooks fire in.
- **Flashlight hook fired with no player 3D**, including during load. It now
  requires a fully loaded player.
- **`IdleStopFix` latched forever** if the `IdleStop` event never arrived, so an
  unrelated later animation consumed it. It expires after 2s.
- **A pending animation polled forever** if its clip never became `DynamicIdle`.
  It now gives up after 3s.
- **Pip-Boy could be left closed.** The equip animation tears the Pip-Boy down and
  relies on `PlayPipboyOpenAnim` to restore it; if that address is unavailable the
  animation is now skipped rather than played with no way back.

*Unchanged on purpose*

- The dummy `TESObjectREFR`s are still created through the form factory and never
  registered or freed. That is how the mod addresses arbitrary items with one
  `BGSAction`, and changing it is a redesign, not a fix.
- The vtable patch is never reverted. Harmless for a game process.
