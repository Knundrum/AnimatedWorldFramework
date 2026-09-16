#pragma once

// ---------------------------------------------------------------------------
// Development helper for porting the plugin to another runtime family.
//
// Enable it by creating an empty marker file next to the F4SE log:
//
//     Documents\My Games\Fallout4\F4SE\AnimatedWorld.findcallsites
//
// On startup, BEFORE any hook is installed, this decodes the branch sitting at
// each hook site and reports which function it calls, as a Runtime Database id.
// That id is what a HookSite's `callsiteTarget` wants: once it is filled in,
// CommonLibF4RD finds the callsite itself with REL::AUTO_CALLSITE and the
// hardcoded interior offsets stop mattering on every runtime.
//
// Run it once on OG to learn the callee ids, then look those callees up rather
// than the hook owners - they are ordinary named functions, so several are
// likely already dual-keyed inside CommonLibF4RD's own headers.
// ---------------------------------------------------------------------------

namespace AW::Diagnostics
{
	[[nodiscard]] bool CallsiteDumpRequested();

	// Verbose per-step logging of the animation state machine.  Enable with an
	// empty marker file next to the F4SE log:
	//
	//     Documents\My Games\Fallout4\F4SE\AnimatedWorld.tracehooks
	[[nodiscard]] bool HookTraceEnabled();

	// Must run before Hooks::Install(), or it will decode our own trampolines.
	void DumpCallsiteTargets();
}
