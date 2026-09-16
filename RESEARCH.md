# No Man's Sky — 6DOF Flight Overhaul: Research Notes

**Goal:** Replace NMS's arcade flight model with Star Citizen-style 6DOF movement —
lateral/vertical strafe, decoupled flight, real momentum — on **both Windows and Linux**,
installable as easily as any other mod.

**Status:** Phases 0–3 built. Native strafe mod (`winmm.dll`) built and verified
offline against the real game binary; awaiting first in-game run. See
[README.md](README.md) and [section 13](#13-native-mod-v010).

**Rebuilt for the 2026-09-09 update** (Steam buildid 24039799 → 25208023). See
[section 12](#12-the-2026-09-09-update).

**Date:** 2026-09-09

---

## 1. Verdict

Feasible. The three "weird things" about NMS flight are data-tunable today. True lateral
thrust does not exist in the ship engine model and must be built — but every primitive
needed to build it is present, reachable, and already reverse-engineered.

| Complaint | Fixable how | Confidence |
|---|---|---|
| Always drifting forward, can't stop | `MinSpeed` / `MinSpeedForce` → 0 | Verified |
| Mouse turn rolls the ship | `RollAmount` → 0 | Verified |
| Too arcade-y / no momentum | `DirectionBrake`, `DirectionBrakeMin`, PID gains | Verified |
| **No lateral movement** | **Must be implemented — see §5, §6** | Verified absent |

---

## 2. Target environment

| | |
|---|---|
| Install | a Steam library, found through `libraryfolders.vdf` (see `common.py`) |
| **Game build** | **170671** (from `GAMEDATA/FullLog.txt` telemetry) |
| Platform | Linux via Steam Proton — runs the **Windows x86-64 binary** |
| Renderer | **Vulkan** (`vulkan-1.dll`) — *not* D3D11 |
| Physics | **Havok** (`hknpBody`, `hkTransformf`, `hkQTransform`) |
| Image base | `0x140000000`, `.text` at RVA `0x1000`, size `0x31670c0` |
| `GAMEDATA/MODS/` | Does not exist yet |

**Key consequence:** on Linux, NMS is still the Windows binary under Proton. There is no
native Linux build. So a single Windows DLL covers *both* platforms — one artifact, one
codebase, no cross-platform abstraction layer.

Local toolchain gaps: no `dotnet` installed; `wine` present.

---

## 3. Data layer (MBIN)

Flight tuning lives in `GCSPACESHIPGLOBALS.GLOBAL.MBIN`, inside
`GAMEDATA/PCBANKS/NMSARC.globals.pak`.

### 3.1 `GcPlayerSpaceshipEngineData` — the flight model (31 fields, all float)

```
BalanceTimeMax        BalanceTimeMin        BoostFalloff
BoostingTurnDamp      BoostMaxSpeed         BoostThrustForce
DirectionBrake        DirectionBrakeMin     Falloff
FollowDerivativeGain  FollowDerivativeLimit FollowIntegralDecay
FollowIntegralGain    FollowIntegralLimit   FollowProportionalGain
FollowProportionalLimit  LowSpeedTurnDamper MaxSpeed
MinSpeed              MinSpeedForce         OverspeedBrake
ReverseBrake          RollAmount            RollAutoTime
RollForce             ThrustForce           TurnBrakeMax
TurnBrakeMin          TurnStrength
```

Notable:

- **`ThrustForce` is the ONLY translational force.** One axis. This is the definitive
  proof that lateral thrust has no latent data parameter.
- **`DirectionBrake` / `DirectionBrakeMin`** bleed off any velocity not aligned with the
  nose. This is NMS's permanently-welded-on flight assist. Lowering it is what produces
  real momentum and drift.
- **`Follow*Gain` / `Follow*Limit`** are a genuine **PID controller** steering the ship
  toward the aim vector. Retuning this is where "weight" comes from.
- **`RollAmount`** ~~is the yaw→roll auto-bank coupling~~ — **wrong, see §13**. It is the
  strength of MANUAL roll input; zeroing it disabled Q/E roll entirely (v0.1 bug). The
  yaw→roll auto-bank is the `RudderToRoll*` group, and it is subtle in vanilla
  (multipliers 0.05–0.16), a slight bank into the turn rather than a visible roll.
- **`MinSpeed`** defaults to **20** in atmosphere, **1** in space — the "always moving
  forward" complaint.

### 3.2 Structure

`GcPlayerSpaceshipControlData` (size `0x218`) holds four engine profiles:
`AtmosCombatEngine` (0x0), `CombatEngine` (0x74), `PlanetEngine` (0xE8), `SpaceEngine` (0x15C),
plus `AngularFactor` (0x1D0), `MaxTorque` (0x1F0), and the `ShipPlanetBrake*` block.

`GcSpaceshipGlobals` (~700 fields, `0x1E40`) contains **six** such blocks —
`Control`, `ControlCorvette`, `ControlHeavy`, `ControlHeavyHover`, `ControlHover`,
`ControlLight` — i.e. **24 independently tunable engine profiles**.

### 3.3 Things that will fight a 6DOF model

These must be neutralized or they will actively undo lateral movement:

- `DirectionBrake`, `DirectionBrakeMin`, `DirectionBrakeVerticalMultiplier`
- `LinearDamping` (0x1688), `AngularDamping` (0x1254)
- `AutoLevel{MaxAngle,MinAngle,MaxPitchAngle,MinPitchAngle,PitchCorrectMargin,WaterTorque}`
- `PitchCorrect*` (18 fields), `NearGroundPitchCorrect*`
- `RudderToRoll*` (10 fields), `TurnRudderStrength`
- `ApplyHeightAlign` (0x1E93), `ApplyHeightForce` (0x1E94)
- `GroundHeight*` (~25 fields), `GravityDrop*`

### 3.4 Undocumented, worth probing

- **`AltControls`** (bool, 0x1E92) — bare boolean, no documented meaning. Vehicle
  counterpart `GcVehicleGlobals.VehicleAltControlScheme` has paired smoothing tunings.
- `LateralDriftRange` (0x1674), `LateralDriftRollAmount` (0x1678) — unclear whether these
  move the rigid body or only the visual model.

### 3.5 Mod install format changed

Since **5.50 Worlds Part II**, the game loads **loose folders** from `GAMEDATA/MODS/<name>/`.
No `.pak` packing. No `DISABLEMODS.TXT` (ignored now). Load order lives in
`BINARIES/SETTINGS/GCMODSETTINGS.MXML`.

---

## 4. Code layer (binary recon)

### 4.1 RTTI is intact — 7,366 class type descriptors

The shipped binary retains full MSVC RTTI, and lambda type descriptors embed their
enclosing function signatures. Effectively a partial symbol table.

Classes confirmed present with RTTI (→ vtable discoverable at runtime by name):

| Class | RTTI |
|---|---|
| `cGcSpaceshipComponent` | yes |
| `cTkPhysicsComponent` | yes |
| `cGcApplication` | yes |
| `cTkInputManager` | yes (+ `cTkInputManagerOpenVR`, `cTkInputManagerSteam`) |
| `cGcPlayerController` | yes |
| `cGcCameraManager` | yes |
| `cGcSpaceshipControl` | **no** — static helper namespace, not polymorphic |

This matters for maintainability: classes can be located **by name at runtime** rather than
by hardcoded offsets.

### 4.2 Verified function addresses (build 170671)

NMS.py's byte-pattern signatures were scanned against this exact binary.
**All five matched uniquely — exactly one hit each.**

| Function | Address |
|---|---|
| `cGcSpaceshipComponent::UpdateControlled(this, float dt)` | `0x14160bc40` |
| `cGcSpaceshipComponent::Update(this, float dt)` | `0x1415ffdb0` |
| `cGcSpaceshipComponent::GetVelocity(this, &out)` | `0x14160b800` |
| `cTkRigidBody::SetLinearVelocity(this, &vec3, bool)` | `0x142a81860` |
| `cTkRigidBody::SetAngularVelocity(this, &vec3, bool)` | `0x142a80f60` |

`UpdateControlled` disassembles cleanly: 941 instructions to first `ret`, 41 unique call
targets. **This is the function we take over.**

Because these resolve by pattern rather than fixed offset, the mod should survive game
patches.

### 4.3 The control chain

```
cGcSpaceshipComponent
  ├─ mpController   → cGcPlayerController*   (0x5F20)
  ├─ mbControllerActive : bool               (0x5F28)
  ├─ mpPhysics      → cTkPhysicsComponent*   (0x60A8)
  │                     └─ mRigidBody : cTkRigidBody (0x50)
  │                          ├─ SetLinearVelocity()
  │                          └─ SetAngularVelocity()
  ├─ mfHeight : float                        (0x656C)
  └─ meLandState : uint32                    (0x7040)
```

**The ship is a `cTkRigidBody` driven by velocity setters** — not by Havok impulses. We
write the velocity vector directly each frame. Simpler than impulse-based control.

### 4.4 Capabilities and gaps

**Available:**
- Per-frame ship physics hook with timestep, replaceable wholesale
- Linear + angular velocity **writes**
- Linear velocity **read**
- Orientation **read** as `cTkMatrix34` basis (`right`/`up`/`at`/`pos`) via
  `Engine::GetNodeAbsoluteTransMatrix` on the ship's `TkHandle`
- ~700 live-writable tuning globals

**Absent (verified):**
- No quaternion type anywhere — orientation is matrix-only *(fine; a basis matrix is what
  a thrust model wants)*
- No angular-velocity **getter** — we integrate and track it ourselves
- **No ship input axis reads of any kind.** `cGcPlayerController` is an empty stub. No
  input manager binding, no stick/mouse-delta accessor
- No camera control hooks
- No `cTkRigidBody` field offsets (mass, inertia tensor, transform) — two setters only

---

## 5. The latent strafe actions

**The game defines lateral input actions that appear unused.**

`GcInputActions.InputActionEnum` (304 entries) includes:

```
144  ShipStrafe            (analogue, 2D)
145  ShipStrafeHorizontal  (analogue, 1D)
146  ShipStrafeVertical    (analogue, 1D)
169  ShipUp     170 ShipDown     171 ShipLeft     172 ShipRight   (digital)
```

They sit in the analogue-axis block **directly beside the known-live** `ShipSteer` (150),
`ShipTurn` (151), `ShipPitch` (152). All seven confirmed present as strings in build 170671.

**Provenance:** introduced in commit `2f87d96d`, "Batch 3 of Beyond structs" (2019-08-15) —
the **Beyond / VR update**, alongside `PlayerSnapTurnLeft/Right`, `HMD_Recenter`. They have
survived every patch since.

**Why they may be reachable:** `GcInputActionInfoMap` is a fixed 304-entry array indexed by
the enum, shipped as an MBIN. Each entry carries:

```csharp
[Flags] enum InputActionInfoFlagsEnum : uint {
    None=0x0, AvailableOnConsole=0x1, HideInControlsPage=0x2,
    HideInControlRebindingPage=0x4, HideInMenusMenu=0x8,
    OnlyVR=0x10, OnlyNonVR=0x20, IgnoreInSteam=0x40,
}
```

### ✅ CORRECTED: they are not hidden, just unbound

The `OnlyVR` / `HideInControlRebindingPage` hypothesis was **wrong**. Verified against the
shipped `metadata/input/gcinputactioninfo.mbin` from build 170671:

```
[144] ShipStrafe            Analogue=true  LocTag=CTRL_ANLG_SHIP_STRAFE
                            ExternalId=ship_strafe   Flags=AvailableOnConsole
[145] ShipStrafeHorizontal  Analogue=true  LocTag=CTRL_ANLG_SHIP_STRAFEHORIZONTAL
                            ExternalId=ship_strafeh  Flags=AvailableOnConsole
[146] ShipStrafeVertical    Analogue=true  LocTag=CTRL_ANLG_SHIP_STRAFEVERTICAL
                            ExternalId=ship_strafev  Flags=AvailableOnConsole
[150] ShipSteer  (KNOWN LIVE)                        Flags=AvailableOnConsole
[152] ShipPitch  (KNOWN LIVE)                        Flags=AvailableOnConsole
```

**Their flags are identical to the known-live steering axes.** No `OnlyVR`, no hiding. They
also carry full localisation tags and **Steam Input external IDs**, so the game registers
them with the Steam Input API.

They are simply **unbound**. Verified across all nine shipped profiles — keyboard, mouse,
Xbox pad, DS4, PS5, Switch joycon, gestures, iOS touch, and every VR profile: **zero
bindings** for any of the seven actions. Even VR doesn't bind them, which undercuts the
"they came from VR and VR still uses them" theory.

So the probe is simpler than expected: just add `GcInputBinding` rows. No flag clearing.

### 🔑 They are fully localised — strongest evidence yet

Extracted from the shipped English/French/German/Japanese tables in
`language/nms_loc5_*.mbin` (build 25208023):

| LocTag | English | French | German | Japanese |
|---|---|---|---|---|
| `CTRL_ANLG_SHIP_STRAFE` | **Thrust** | — | — | — |
| `CTRL_ANLG_SHIP_STRAFEHORIZONTAL` | **Horizontal Thrust** | Poussée horizontale | Horizontaler Schub | 水平加速 |
| `CTRL_ANLG_SHIP_STRAFEVERTICAL` | **Vertical Thrust** | Poussée verticale | Vertikaler Schub | 上昇 |
| `CTRL_ANLG_SHIP_PITCH` *(live)* | Pitch | Tangage | Neigung | ピッチ |
| `CTRL_ANLG_SHIP_TURN` *(live)* | Turn | — | — | — |

They sit in the **same shipped table** as the known-live Pitch/Turn/Steer, with
translations into every supported language. **Nobody pays translators to localise dead
enum entries.** These were built as a user-facing feature.

Counter-evidence, for balance:

- No shipped binding profile binds them (9 of 9 checked, VR included).
- The **Steam Input** manifests in `GAMEDATA/INPUT/*.vdf` expose `ship_steer`,
  `ship_freelook`, `ship_galmap`, `ship_target*` — but **not** `ship_strafe*`, despite
  those `ExternalId`s existing in the action table.

**Leading hypothesis:** these belong to the **VR hand-controller** scheme.
`GcSpaceshipGlobals.HandControllerValueMultiplier` is a `Vector3f` — three translation
axes of hand offset — and "Horizontal Thrust" / "Vertical Thrust" describe exactly what
moving your hand sideways or up would do. If VR feeds thrust straight from hand offset
rather than through the action system, binding a key would do nothing even though the
labels are real.

That cannot be settled statically. Hence the controlled probe.

### ⚠️ The unresolved question

**An enum existing guarantees a binding will serialize — not that anything consumes it.**

I attempted to settle this statically by scanning for the enum values passed as call
arguments. **The test was invalid**: `SpaceshipThrust` (a definitely-live action) scored
0 hits while `ShipStrafe` scored 468, because `0x90`/`0x98` are common struct sizes and
offsets. A method that misses a known-live action cannot be trusted on the others.

Most likely explanation: ship control reads a **pre-populated input state struct** filled
once per frame by the input manager, so action enums never appear as immediates near the
flight code.

**This must be settled empirically — bind them and see if the ship moves.**

**STATUS: probe v2 built and installed** (`strafe_test.py`), now a *controlled*
experiment. The first run was inconclusive because A/D were tested — those are vanilla
`Ship_RollLeft`/`Ship_RollRight` and were never rebound.

v2 adds a **control binding**: `Ship_Scan` → `KeyZ`. If Z fires the scanner, the modified
binding file is provably live and added bindings provably work — which makes a null result
on the strafe keys real evidence rather than an unexplained nothing.

Test bindings: arrows → `ShipLeft`/`ShipRight`/`ShipUp`/`ShipDown`, Q/R →
`ShipStrafeHorizontal`/`ShipStrafeVertical`.

⚠️ Caveat on interpreting a negative: 145/146 are *analogue* actions bound to *digital*
keys, so the input layer may drop them before the flight code ever sees them. `--pad` binds
them to a real analogue axis (`RightStickX`/`Y`) and is the stronger test.

### Related negative results

- **VR exposes no extra movement axes.** No VR-specific ship globals struct exists. VR uses
  `HandController*` fields inside the normal ship globals, consumed as throttle +
  pitch/yaw/roll reorientation. Note `DirectionBrakeVRBoost` — VR gets *more* velocity
  snapping, not less.
- **No reusable lateral code from other vehicles.** The Minotaur has real strafe
  (`MechStrafeEnabled`, `MechJetpackStrafeStrength`) but shares **no struct** with the ship
  path — reference implementation, not reusable code. Exocraft/Nautilon/Nomad/jetpack/
  freighter/living ships: none.
- Zero hits tree-wide for `Sideways`, `VerticalThrust`, `SixDof`, `Newtonian`, `Decoupled`.

---

## 6. Loader architecture

### 6.1 Why not Python

NMS.py + pyMHF is excellent reverse-engineering work and its **signatures are directly
reusable**, but it is wrong as a *delivery* mechanism:

- **No gamepad support at all**
- **Single keys only** — no `Ctrl+K`, no mouse deltas, no analog axes
- Global OS-level keyboard hooks firing on a **listener thread**, not the game thread
- It is a **cross-process injector** (`OpenProcess` + `VirtualAllocEx` + `CreateRemoteThread`),
  which fights Proton's pressure-vessel container. Windows-only by design; Linux is tracked
  but unsolved (NMS.py issue #45)
- Requires end users to install Python + pip packages — fails the "easy install" bar

**Every Proton-friendly loader is a passive proxy DLL loaded by the game's own loader.
Every one needing `CreateRemoteThread` or a separate launcher exe is friction.**

### 6.2 Choosing the proxy DLL

From `NMS.exe`'s real import table:

| Proxy | Loaded? | Exports needed | Verdict |
|---|---|---|---|
| **`winmm.dll`** | **static import** | `timeBeginPeriod`, `timeEndPeriod` | **Chosen.** Earliest, smallest surface, least contended |
| `dbghelp.dll` | **static import** | `MiniDumpWriteDump` | Equally early; what OptiScaler uses on NMS. Collides with OptiScaler / DLSS Enabler |
| `xinput9_1_0.dll` | **static import** | `XInputGetState`, `XInputSetState` | **Strong for a flight mod** — proxying it puts you directly on the gamepad stream with zero hooking |
| `version.dll` | transitive static import | version APIs | Good fallback; contended by Nexus mods |
| `winhttp.dll` | static import | — | Works; BepInEx convention; larger surface |
| `dinput8.dll` | runtime `LoadLibrary` by **GLFW**, late + conditional | — | **Avoid** |
| `dxgi` / `d3d11` / `d3d12` | never loaded | — | **Will not load — game is Vulkan** |
| `vulkan-1.dll` | static import, shipped locally | — | **Do not use.** Wine's builtin `vulkan-1` bridges to `winevulkan` and is required |
| `libSceFios2.dll` | not imported in this build | — | Dead vector (ReNMS used it in 4.13) |

**Windows side:** the exe directory (search-order position 7) wins for all candidates.
Neither hazard applies — `winmm`/`version`/`dbghelp`/`xinput*` are **not** in `KnownDLLs`,
and none resolve through an API set.

### 6.3 The `=n` vs `=n,b` trap

Verified in Wine's `load_builtin()`:

```c
if (pe_mapping->image.wine_builtin) {
    if (loadorder == LO_NATIVE) return STATUS_DLL_NOT_FOUND;
    loadorder = LO_BUILTIN_NATIVE;
}
```

Overrides match on **module basename with no path distinction**. So `winmm=n` also applies
to the System32 `winmm.dll` — which *is* a Wine builtin — so that load **fails**, and the
proxy can no longer chain-load the original.

> **Always ship `=n,b`, never `=n`.**

Grammar: separators are `,` `;` `:` `+` and whitespace; only the first letter of each token
matters, so `native,builtin` is equivalent to `n,b`. A token with neither letter means
**disabled**.

Verified in a Proton prefix: `winmm`, `version`, `dbghelp`, `xinput9_1_0`, `dinput8`, `vulkan-1`
all carry the `Wine builtin DLL` signature, so all need an override. (`dxgi`/`d3d11` do
*not* — they're DXVK, and Proton already sets them native. That's why "just drop dxgi.dll
in" works for D3D games and misleads people.)

Proton appends its own overrides **after** yours and Wine's `add_load_order()` is
last-wins — but no Proton default collides with these names for AppID 275850.

### 6.4 Install UX

**Windows:** drop `Binaries/winmm.dll`. Done.

**Linux:** same file, plus one Steam launch option:

```
WINEDLLOVERRIDES="winmm=n,b" %command%
```

Optional zero-launch-option alternative — a re-runnable installer writes into
`steamapps/compatdata/275850/pfx/user.reg` an `AppDefaults\NMS.exe\DllOverrides` entry
setting `winmm` to `native,builtin`. Proton itself writes `AppDefaults` entries there, so
the mechanism is live. A Proton major-version prefix upgrade may rewrite `user.reg`, so keep
the installer re-runnable.

### 6.5 Future-proofing: Wine 11.6 makes the override unnecessary

Wine 11.5/11.6 (commits `a31ec8da9572`, `8cf7a53ee7d9`, Wine bug 59658) added a heuristic:
a DLL with **no version resource**, or whose `CompanyName` is not Microsoft, is preferred
natively **without any override**.

Verified: your `Proton - Experimental` (`experimental-11.0-20260903c`, wine-11.0) does
**not** have it yet — its `ntdll.so` lacks the new trace strings. So the override is
required today.

> **Build the DLL with no version resource** (or a non-Microsoft `CompanyName`). Once
> Proton rebases onto Wine 11.6+, Linux install becomes drop-in with zero configuration.

### 6.6 Hook engine

| Library | Status | Verdict |
|---|---|---|
| **safetyhook** | Active, v0.7.0 (2026-06), BSL-1.0 | **Best Wine story by construction.** The *only* library with an automatic **14-byte absolute-jmp fallback**. Uses just `VirtualAlloc/Query/Protect` + VEH — no `SuspendThread`, no toolhelp. Requires Zydis 4.1, C++23 |
| **MinHook** | **Revived** — v1.3.4 (2025-03, first release since 2017), maintained by m417z; BSD-2 | **Most proven under Proton** (ReShade, Special K, CET, pyMHF). Tiny C dep, MinGW+CMake friendly. **No 14-byte fallback** — safe anyway, see below |
| PolyHook2 | Active, no tagged releases | Works (UE4SS ships it), but the **only library with a documented Wine bug**. Needs C++20 + Zydis + AsmJit |
| Detours | Main alive, **last tag 2018** | Mechanically sound, independently verified under Wine. No CMake (NMAKE/VS only) |
| funchook | **ARCHIVED 2025-09-28** | GPLv2+linking-exception. Doesn't freeze threads at all. Avoid |
| subhook | **Upstream repo is 404 — gone** | **Never use.** No proximity search at all: plain `VirtualAlloc(NULL,...)`, which under Wine lands ~`0x7ffffe...` — guaranteed out of range |

**Either safetyhook or MinHook. Both are correct choices.**

#### Why MinHook is safe despite having no fallback

MinHook searches **+/-1 GB** (`MAX_MEMORY_RANGE = 0x40000000`; the "+/-512MB" comment in the
source is stale) and fails with `MH_ERROR_MEMORY_ALLOC` if nothing is found. Under Wine that
never happens: measured on Wine 11.16, the **entire 2 GB below the exe base is `MEM_FREE`**
(`0x7ff00000` bytes), and a MinHook-style downward scan finds a slot on the **first try** at
`0x13fff0000`. The exe is always at its preferred `0x140000000` — **Wine has no ASLR**, and
EXEs are never relocated.

#### Correction: Wine bug 44893 is not a live hazard

Earlier notes overstated this. Bug 44893 (SKSE64 "no free space before image") is
**effectively fixed** upstream — Wine adopted `MEM_TOP_DOWN` for relocatable DLLs plus a
server-side address pool. The +/-2 GB proximity search now always succeeds.

⚠️ **But bug [48641](https://bugs.winehq.org/show_bug.cgi?id=48641): wine-staging's
`ntdll-ForceBottomUpAlloc` patchset *re-broke* it.** GE-Proton and other staging-based forks
can carry allocator patches that change address-space layout. **Test on GE-Proton separately.**

#### The real allocation hazard is `VirtualAlloc(NULL, ...)`

Measured under Wine: 12 consecutive bottom-up 1 MB allocations returned
`0x7ffffe7c0000, 0x7ffffe8c0000, ...` — because `map_view()` tries the preloader's reserved
areas first. On Windows a naive `VirtualAlloc(NULL)` code cave often *happens* to land within
+/-2 GB of the exe; **under Wine it never will.** Any engine doing an explicit proximity
search is fine; subhook, which doesn't, is not.

#### PolyHook2's Wine bug (for reference)

`VirtualAlloc2` ignored `MEM_ADDRESS_REQUIREMENTS` on old Wine — `HighestEndingAddress`
before Wine 8.9, `LowestStartingAddress` before 8.11. Fixed by commits `5b6e82f0f9c2` /
`11cd51139d4b`. **Proton 8.0 and older affected; Proton 9.0+ not.** PolyHook guards it and
degrades to INPLACE/CODE_CAVE regardless.

#### Implementation rules

- **Keep trampolines in `VirtualAlloc`'d memory, not in-image code caves.** Wine bug
  [54290](https://bugs.winehq.org/show_bug.cgi?id=54290): `RtlAddFunctionTable` is ignored
  for a range inside a loaded PE image with empty `.pdata`, so SEH won't unwind through an
  in-image trampoline.
- **`VirtualQuery` over-reports `MEM_FREE` on x64** (the "pretend it's allocated" fallback is
  `#ifdef __i386__` only). Addresses where glibc/ld.so/GPU drivers live report free, and the
  subsequent `VirtualAlloc` fails with `ERROR_INVALID_ADDRESS`. **Retry loops are required**;
  single-shot "query said free, so alloc must work" logic is wrong.
- **Detect and log Wine:**
  `GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")` (also
  `wine_get_build_id`, `wine_get_host_version`).
- Debug aids: `WINEDEBUG=+loaddll` (module bases), `WINEDEBUG=+virtual` (view protections),
  `/proc/<pid>/maps` (host mappings `VirtualQuery` hides). ⚠️ **Do not use `WINEDEBUG=+relay`
  alongside inline hooks** — bugs 50108 / 39938 show they interact badly.

### 6.7 Verified: no obstacles to inline hooking

- **No anti-cheat.** No Denuvo, no EAC, no BattlEye.
- **No Control Flow Guard.** `DllCharacteristics = 0x8160` — the `GUARD_CF` bit `0x4000` is
  clear and `GuardCFCheckFunctionPointer` is 0. Inline *and* indirect-call hooking
  unobstructed.
- **CET / shadow stack / IBT is a non-risk.** Wine has **zero** CET support: no
  `ARCH_SHSTK_ENABLE`, no `IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT` on any builtin (measured
  `0x0160` on both Wine 11.16 and Proton Experimental 11.0), and Linux has **no user-space
  IBT at all**. No `#CP` faults from jumping into a trampoline.
- **`VirtualProtect` is faithful.** `PAGE_EXECUTE_READWRITE` works on private *and* PE image
  pages with correct COW semantics. One divergence: after writing to an image page Wine keeps
  reporting `PAGE_EXECUTE_WRITECOPY` from `VirtualQuery` where Windows reports
  `PAGE_EXECUTE_READWRITE`. **Harmless** for protect→patch→restore (the returned `oldProtect`
  is correct); only breaks code that *asserts* on the queried value afterward.
- **Thread freezing works.** `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)`, `SuspendThread`
  and `GetThreadContext` all work and give an **accurate RIP** — a thread in `Sleep` reports
  RIP inside the ntdll PE syscall stub, not a host `.so` address.
- **VEH fully works** — including `EXCEPTION_CONTINUE_EXECUTION`, guard pages, and
  Windows-compatible `int3` semantics. That's safetyhook's whole thread-safety mechanism.
- **SEH/unwind through trampolines works** — `RtlAddFunctionTable` is genuinely consulted by
  `RtlLookupFunctionEntry` (subject to bug 54290 above).
- **`FlushInstructionCache` is an explicit no-op** on x86-64 under Wine. Keep calling it;
  x86-64 icache is coherent for same-core SMC anyway.
- **W^X only matters on hardened kernels** (PaX/grsecurity `MPROTECT`, SELinux `execmem`
  denial). Not a concern on a normal gaming distro.
- **Steam overlay won't collide.** Under Proton the overlay is drawn Linux-side by
  `gameoverlayrenderer.so`, **not** by `GameOverlayRenderer64.dll` hooking `Present`.
- **ESYNC/FSYNC/NTSYNC are irrelevant** to hooking — they only change wait-object
  implementation.

⚠️ **SteamStub**: the exe has a `.bind` section (RVA `0x07253000`) — code may be decrypted
at/after the original entry point. **Install hooks from a worker thread after the first
frame, never from `DllMain`**, and pattern-scan only after that.

⚠️ **Hook `NMS.exe`'s own code, not Wine builtins.** Builtins *are* real PE files so the
mechanics work, but the *shape* differs: Wine's `DECLSPEC_HOTPATCH` prologue isn't on every
export and depends on how your Wine was built (bug 49828); most `kernel32!X` exports are
thunks into kernelbase, so a hook that catches everything on Windows may catch nothing under
Wine; some exports are forwarders that silently land in ntdll. Rule of thumb: **hook the
lowest layer you can reach.** Also note `d3d11`/`dxgi` under Proton are **DXVK**, not Wine
builtins — byte signatures derived from Windows d3d11 fail 100% of the time.

### 6.8 Input

The game already uses raw input: `NMS.exe` contains `RegisterRawInputDevices` /
`GetRawInputData` (GLFW registers raw mouse for disabled-cursor mode) plus `XInputGetState`.

| API | Verdict |
|---|---|
| **`GetAsyncKeyState`** | **Recommended for keys.** Reads desktop-wide shared memory under Wine. May hit the wineserver — poll only the few keys you need, once per frame, and cache. Do not sweep 256 VKs |
| **`RegisterRawInputDevices` from the DLL** | **Never.** Registration is per-process per-usage-page, GLFW has already registered mouse, and you would **clobber the game's mouse look** |
| **Raw mouse deltas** | **Subclass the game window** (`SetWindowLongPtrW` / `GWLP_WNDPROC`) and observe the `WM_INPUT` messages GLFW already receives. Standard ImGui-overlay approach; identical under Wine |
| **XInput** | **Best gamepad route: proxy `xinput9_1_0.dll`** and read/modify `XInputGetState` results. Zero hooking, and it's exactly the data flight code consumes |
| `GetKeyState` | Queue-local — don't use from a mod thread |
| DirectInput8 | Avoid — late load, COM forwarding burden |
| `SetWindowsHookEx` | Works but unnecessary |

### 6.9 A simplification worth noting

`GcSpaceshipGlobals` is **live-writable memory**. The DLL can neutralize the stock flight
assist at runtime — `RollAmount`, `DirectionBrake`, `AutoLevel*`, `PitchCorrect*`,
`RudderToRoll*`, the PID gains — with no MBIN mod at all.

**No MBINCompiler, no `GAMEDATA/MODS` folder, no re-packing after game updates.
The entire mod becomes a single DLL.**

*(Caveat: if the latent-action route in section 5 works, that half does need an MBIN edit for
the binding table. Data mod for bindings + DLL for physics.)*

---

## 7. Prior art — the NMS native mod scene

There is a real scene, and one project is a near-perfect template.

**Actively maintained**

- **[Mrsuss60/NoMansSky_MODs](https://github.com/Mrsuss60/NoMansSky_MODs)** — pushed
  2026-09-08. Ships **both a `dbghelp.dll` and a `version.dll` proxy** loader that
  `LoadLibrary`s every `*.mods` file beside it. Hand-rolled hooking: IDA-style pattern scan,
  RIP-relative reference resolution, `AllocateNearAddress` walk, manual `E9` codecaves.
  Contains `CTPCamera`, `NoFOVLimit`, `NoMansTime`. **Excellent template.**
- **[OptiScaler](https://github.com/optiscaler/OptiScaler)** — its NMS wiki page says rename
  to `dbghelp.dll` "for early hooking", with `Vulkan=true` because NMS is Vulkan.
  **Known-working under Proton.**
- **[pyMHF](https://github.com/monkeyman192/pyMHF) / [NMS.py](https://github.com/monkeyman192/NMS.py)**
  — pushed 2026-09-09. **The only current, maintained offset/signature database for NMS**
  (`tools/data.json`, `nmspy/data/types.py`). Use as RE source, not as runtime.
- **DLSS Enabler** — hard-codes `NMS.exe`; one of few titles with Vulkan spoofing enabled
  under Linux by default.

**Unmaintained but instructive**

- **[ReNMS](https://github.com/sonny-tel/renms)** (2024-02) — the most serious C++ SDK.
  `libSceFios2.dll` proxy (~1000 forwarded exports), **PolyHook2** (`NatDetour` + `IatHook`),
  resumes the suspended main thread after installing hooks so they land before game code.
  Locked to NMS 4.13, and its proxy vector is dead in the current build.
- **[NoMansSky.Api](https://github.com/gurrenm3/NoMansSky.Api)** (2023-06) — C# on
  **[Reloaded-II](https://github.com/Reloaded-Project/Reloaded-II)**, the only framework here
  with an *official Linux/Proton guide*.
- **NMSExtender** (2016) — external injector, hooked OpenGL. Pre-Vulkan, historical only.

**Collision note:** `dbghelp.dll` is already claimed on NMS by OptiScaler and DLSS Enabler;
`version.dll` by several Nexus mods. `winmm.dll` is the least contended early-load name.
Good citizenship: build the same binary under several names (Ultimate-ASI-Loader style) and
document the conflict.

---

## 8. Target flight model — the Star Citizen spec

Full spec with a 31-item prioritized checklist:
[`flight-model-spec.html`](flight-model-spec.html) in this repo.

Primary source: John Pritchett (CIG Senior Physics Programmer), *IFCS and Flight Physics,
Star Citizen 3.2* — 21 pages, extracted in full. The spec is built on that rather than
community paraphrase.

### 8.1 The insight that shapes our implementation

**IFCS is not the flight model.** The simulation is *always* Newtonian 6DOF. IFCS is a
control layer on top. Coupled vs decoupled does not change the physics — it changes
**what quantity the stick commands**:

| Mode | Linear axes command | Throttle | Drift nulling |
|---|---|---|---|
| **Coupled** | **velocity** | active | on |
| **Decoupled** | **acceleration** | disabled | off |

**Rotational control is identical in both modes.** This is why SC decoupled is *not*
Elite's FA-off — FA-off also removes rotational damping.

**This maps directly onto what NMS gives us.** `cTkRigidBody::SetLinearVelocity` is a
*velocity* command — exactly the coupled-mode primitive, for free. Decoupled mode is then:
integrate commanded acceleration into our own velocity state, and write the result through
the same setter. One code path, one branch.

### 8.2 Mouse feel — the second-order/first-order distinction

The "floaty vs direct" argument reduces to **one integrator**:

- **VJoy mode** integrates mouse deltas into a persistent virtual stick that **does not
  self-centre**. The mouse therefore controls rotational *acceleration* — two integrations
  away from attitude. Second-order plant.
- **Relative mode** removes that integrator. First-order plant.

NMS's `UseOldMouseFlight` user setting hints at a similar split. Whichever we build, this
is the single biggest lever on perceived feel, and it should be a user toggle.

### 8.3 Quantitative targets

Pulled live from the SC wiki API across 20 hulls, the RSI Ship Matrix, Coriolis, and
Frontier measurement threads.

| Quantity | Star Citizen | Elite Dangerous |
|---|---|---|
| Retro thrust vs main (fighters) | **20–35%** | — |
| Boost effect on rotation rates | flat **x1.2** | — |
| Boost capacitor / refill | 20 s / ~27 s | — |
| Boost ramp-up by hull size | **0.4 s → 2.0 s** | — |
| Yaw/pitch ratio | **0.76–0.91** | median **0.40** |

**Thruster asymmetry is the point.** Lateral and retro thrust being a fraction of main
thrust is what makes strafe feel like a real maneuvering system rather than omnidirectional
sliding.

### 8.4 Corrections to common understanding

- Decouple is bound to **`C`**, not CapsLock (CapsLock hasn't appeared in a shipped profile
  since 2.x).
- The bare mouse wheel is no longer the speed limiter — it's `LAlt`+wheel in 4.x.
- `v_target_match_vel` was **removed entirely** between 3.23.1 and 4.8.1.
- **Elite's FA-off rate uplift is widely overstated.** Max angular rate is fixed; FA-off
  doesn't raise it. It bypasses the *speed-dependent penalty*, so uplift is ~0% at
  blue-zone centre, but +128% median at zero throttle and +137% at full.

### 8.5 Independent corroboration of the MBIN ceiling

This track independently pulled the MBINCompiler struct definitions and reached the same
conclusion as section 3: `GcPlayerSpaceshipEngineData` has **no lateral or vertical thrust
field, no per-axis thrust, no mass or inertia tensor**, and a `MinSpeed` floor preventing
rest.

> **The Core tier of the checklist is not reachable by MBIN edits.** A data-only mod reaches
> a partial *Important* tier — drift, accel/brake asymmetry, rotational weight, and a genuine
> SCM/NAV split by re-tuning `CombatEngine` vs `SpaceEngine` (which lines up neatly with the
> existing engine profiles). "No compromises" requires the native code mod.

### 8.6 Checklist shape

31 items: **Core 11 / Important 12 / Polish 8**, ordered by contribution to feel.

---

## 9. Open questions

1. **Do `ShipStrafeHorizontal`/`ShipStrafeVertical` still drive anything?** (§5) — decides
   whether we get input plumbing for free. **Test first.**
2. **Is the ship's `cTkRigidBody` dynamic or kinematic?** We have velocity setters, so we
   can drive it either way — but this determines whether collisions and momentum behave
   naturally or need manual handling.
3. **What does `AltControls` (0x1E92) do?** Cheap to toggle.
4. **Do `LateralDriftRange`/`LateralDriftRollAmount` move the body or just the model?**
5. ~~Does inline hooking work reliably under Proton?~~ **ANSWERED — yes.** No anti-cheat,
   no CFG, no CET anywhere in Wine, faithful `VirtualProtect`, working thread-freeze and
   unwind. Use safetyhook or MinHook; keep trampolines out of in-image code caves; retry
   failed allocations. Verify separately on GE-Proton. See sections 6.6-6.7.
6. **Camera behaviour during strafe.** No lateral camera-offset field exists in
   `GcCameraGlobals`; the follow-cam may not track a strafing ship well.

---

## 10. Plan of attack

**Phase 0 — Data-only proof (hours, no code)**
- Set up MBINCompiler (`MBINCompiler-linux` native binary ships in releases).
- Zero `RollAmount`; zero `MinSpeed`/`MinSpeedForce`; drop `DirectionBrake` hard.
- Fly it. This alone addresses 3 of the 4 original complaints and calibrates how much of
  the "arcade" feeling was the missing strafe vs. the permanent flight assist.

**Phase 1 — The cheap strafe test (~1 hour)**
- Clear `OnlyVR` / `HideInControlRebindingPage` on `GcInputActionInfoMap` entries 144–146,
  169–172.
- Add `GcInputBinding` rows for `ShipStrafeHorizontal`/`ShipStrafeVertical` in the
  `ShipControls` set.
- Bind, push key, observe. **Decides the shape of everything downstream.**

**Phase 2 — Loader skeleton**
- `winmm.dll` proxy; resolve `timeBeginPeriod`/`timeEndPeriod` at runtime via
  `LoadLibraryW` + `GetProcAddress`. Build with **no version resource** (see 6.5).
- Verify it loads on Windows *and* under Proton with `WINEDLLOVERRIDES="winmm=n,b"`.
- Worker thread; wait for first frame (SteamStub + GLFW init) before scanning. Never
  hook from `DllMain`.
- Pattern-scan for the five functions; log addresses to confirm resolution.

**Phase 3 — Take over flight**
- Hook `cGcSpaceshipComponent::UpdateControlled`.
- Neutralize assist globals at runtime.
- RawInput for keyboard/mouse/gamepad on the game thread.
- Implement the 6DOF model; drive via `SetLinearVelocity`/`SetAngularVelocity`.

**Phase 4 — Feel and polish**
- Coupled/decoupled toggle (see 8.1 — one branch, not two code paths).
- Thruster asymmetry (retro/lateral at 20-35% of main), speed limiter, boost
  (x1.2 rotation, 20 s capacitor, size-scaled ramp).
- Mouse mode toggle: second-order (VJoy-style) vs first-order (relative). See 8.2.
- Work the 31-item checklist in section 8 by tier.
- In-game tuning overlay (Vulkan - note the renderer constraint).

---

## 11. Tooling

| Tool | Notes |
|---|---|
| **MBINCompiler** | ⚠️ **Must be v6.45.0-pre1**, NOT the current v7.0x — see below. Native `MBINCompiler-linux` binary, needs .NET 8 runtime |
| **HGPAKtool** | ⚠️ **Required.** The paks are **not PSARC** — since 5.50 they're Hello Games' own `HGPAK` format. [monkeyman192/HGPAKtool](https://github.com/monkeyman192/HGPAKtool) 1.1.3 ships a native Linux binary. Use `--platform windows` (ZSTD) |
| **.NET 8 runtime** | Installed user-local at `~/.dotnet` via `dotnet-install.sh` — no sudo, no system packages |
| **AMUMSS** | Windows GUI wrapper — community says it needs a Windows VM. **Not needed**; MBINCompiler + HGPAKtool are enough on Linux |
| **NMS.py / pyMHF** | Latest release `170671.6` — matches this build exactly. **Used as a signature source, not as a runtime** |
| **libMBIN** | Authoritative struct definitions (`libMBIN/Source/NMS/`) |
| capstone 5.0.7 | Installed in a throwaway venv for disassembly |
| **safetyhook** or **MinHook** | Hook engine. safetyhook = 14-byte fallback + VEH thread safety; MinHook = tiny C dep, easiest MinGW cross-compile, most Proton mileage (see 6.6) |
| **mingw-w64** | ⚠️ **NOT INSTALLED.** Machine has clang 22.1.8 + lld-link + cmake + ninja, but no `x86_64-w64-mingw32-gcc` and no MSVC SDK. Need `pacman -S mingw-w64-gcc` to cross-compile the PE DLL. ReNMS's `mingw64.cmake` is a working reference |


### ⚠️ MBINCompiler version is not cosmetic

v7.00.0-pre1 and v7.01.0-pre1 (both released 2026-09-09) are **ahead of game build 170671**.
They warn `File not recognized` and then emit **silently misaligned data** — visible as
denormal floats like `5.40480818E-42`. Writing that back would corrupt the file.

The discriminator is the template GUID:

| | GcSpaceshipGlobals GUID |
|---|---|
| Game build 170671 | `0x6f4439bd7fb55340` |
| libMBIN 7.x expects | `0xB1A40934F959BDB4` |

v6.40 through v6.45 all decompile cleanly. **v6.45.0-pre1** is the newest clean one.

**Round-trip verification** (do this after every game update): recompile an *unmodified*
decompile and diff against the extracted original. Result on 170671 — all **8,194 data
bytes identical**, with only 6 header bytes differing (MBINCompiler's version stamp
`6.45.0.1` at `0x18`, plus a format flag bit at `0x0A`). The file's own template GUID is
preserved, not overwritten with libMBIN's.


---

## 12. The 2026-09-09 update

A game update landed mid-project. What it did and did not break is a useful record of
how much maintenance this mod actually needs.

| | Before | After |
|---|---|---|
| Steam buildid | 24039799 | **25208023** |
| `NMS.exe` | 85,404,232 bytes (2026-08-08) | **88,481,352** bytes (2026-09-09) |
| `.text` size | `0x31670c0` | `0x33f5cc0` |
| Correct MBINCompiler | v6.45.0-pre1 | **v7.00.0-pre1** |
| `GcSpaceshipGlobals` data | 8,194 bytes / 697 fields | **8,372 bytes / 723 fields** |
| `GcPlayerSpaceshipEngineData` | 29 fields | **29 fields (unchanged)** |

### What this confirmed

- **The struct layout does change even when flight behaviour doesn't.** The old mod
  would have been misread. Any game update requires a rebuild, full stop.
- **v7.0x was released *for* this update.** Before it, v7.0x mismatched and v6.45 was
  correct; after, exactly the reverse. The prober re-ran and flipped automatically.
- **All 200 tuning keys still exist** — no field renames, no missing-key warnings.
- **No new lateral thrust.** `GcPlayerSpaceshipEngineData` is still 29 fields with one
  translational force. The +26 top-level fields are unrelated to flight.
- **All 7 strafe input actions survive** in the new binary. The strafe probe binds by
  action *name*, so it needed no changes.

### Phase 3 impact: signatures partially invalidated

Re-scanning NMS.py's byte patterns against the new `NMS.exe`:

| Function | Result |
|---|---|
| `cGcSpaceshipComponent::UpdateControlled` | ✅ unique @ `0x141744530` |
| `cGcSpaceshipComponent::Update` | ✅ unique @ `0x14178b9f0` |
| `cGcSpaceshipComponent::GetVelocity` | ✅ unique @ `0x1417440f0` |
| `cTkRigidBody::SetLinearVelocity` | ❌ **not found** |
| `cTkRigidBody::SetAngularVelocity` | ❌ **not found** |

Three of five survived at new addresses — which is the pattern-scanning architecture
working exactly as intended, since none of those addresses were hardcoded. The two
velocity setters changed and need re-deriving before Phase 3.

NMS.py is still published as `170671.6` and has **not** been updated for this build, so
either wait for it or derive the two signatures directly.

### Tooling response

Added `setup_tools.py` (probe + install the matching MBINCompiler), `common.py` (game
discovery, build detection, round-trip verification), and `update.sh` (one-shot
post-update rebuild). `build_mod.py` now:

1. **fails closed** if the toolchain was validated for a different build;
2. **verifies a lossless round-trip** against the real game file before every build;
3. **warns if any tuning key is missing**, which is how a future struct rename surfaces.

Extracted vanilla files are wiped whenever the build changes, so a stale cache cannot
silently feed the next build.


---

## 13. Native mod v0.1.0

### 13.1 The strafe probe verdict

Probe v2 bound `Ship_Scan` to Z as a control alongside the strafe actions. **Z fired
the scanner; every strafe binding did nothing.** The binding file demonstrably loaded,
so this is real evidence: flatscreen flight code does not consume `ShipStrafe*` or
`ShipUp/Down/Left/Right`, despite their shipped translations. Strafe must be supplied
natively.

Also confirmed: the **A+D pulse jump is not a binding.** The only `ChordAD` in any
profile is on-foot `PlayerMoveX`. In the ship, holding *both roll actions* fires pulse
(Steam Input labels roll "Roll Left / Pulse Drive"). Moving roll keys moves the chord.

`GCUSERSETTINGSDATA.MXML` has an empty `CustomBindingsPC`, so shipped defaults (and
therefore our binding mod) are authoritative.

### 13.2 The 2026-09-10 hotfix shifted struct offsets

Steam buildid **25233815**. MBINCompiler **v7.02.0-pre1** (auto-selected). More
importantly, offsets NMS.py documents were stale:

| | NMS.py docs | Build 25233815 |
|---|---|---|
| ship → `mpPhysics` | `0x60A8` | **`0x6248`** |
| physics → `mRigidBody` | `+0x50` | **`+0x60`** |

This is why the DLL **parses offsets out of the matched machine code** rather than
trusting any documented value. From `GetVelocity`'s signature:
`48 8B 89 <disp32>` (physics) and `48 83 C1 <imm8>` (rigid body).

NMS.py `master` had already updated `SetLinearVelocity` for the new build
(`0F 84 ? ? ? ?` → `74 ?`), though its PyPI release had not.

### 13.3 Orientation — first attempt was WRONG (v0.1.0), corrected in v0.2.0

**v0.1.0 bug, found in the first in-game run.** The log showed
`ship orientation unavailable (node 0)` on every frame: the hook ran, but read zero
from `ship+0x2A4`. The `mov ecx,[rsi+0x2A4]` callsite came from a linear disassembly
sweep that ran past function boundaries (these functions are split into chunks in the
exception table). `+0x2A4` belongs to a different class. Nothing in the flight path
reads a scene node matrix at all.

**What the flight code actually does.** `UpdateControlled` (called with the ship pointer
from `0x141739579`) gets orientation from the **rigid body**, at four sites:

```
mov  rcx,[rsi+0x6248]   ; physics
lea  rdx,[rbp+..]       ; out buffer
add  rcx,0x60           ; rigid body
call 0x1402968c0        ; cTkRigidBody::GetTransform -> rax
movups xmm,[rax]        ; row 0
movups xmm,[rax+0x10]   ; row 1
movups xmm,[rax+0x20]   ; row 2      (+0x30 + +0x40 = position)
```

The rows are world-space axis directions: the code immediately dots a vector with
row 1 to extract the component along that axis. `GetTransform` begins
`mov rsi,[rcx+0x290]` (physics state) and writes default rows if that is null, so the
DLL checks the state pointer itself rather than trusting the output.

v0.2.0 locates `GetTransform` via that callsite (unique; resolving its `E8` gives
exactly `0x1402968c0`), falls back to a unique 16-byte function signature, reads the
`0x290` state offset out of the function body, and cross-checks the physics/rigid-body
offsets encoded at the callsite against those from `GetVelocity`.

Also identified: `0x140213830` = `lea rax,[rcx+0x60]; ret` (physics → rigid body), and
`0x142cb1e70`, called right after `SetLinearVelocity` with the same zero vector — very
likely `SetAngularVelocity`.

### 13.3b The detour through `GetNodeAbsoluteTransMatrix` (kept for the record)

It copies **one** 16-byte row — the translation at `[matrices + idx*64 + 0x30]`. Position
only, no orientation. But it revealed the storage: 64-byte node matrices, rows
right/up/at/pos at `+0x00/+0x10/+0x20/+0x30`, with a lazily-recomputed absolute array
(dirty bit `0x20`).

`cTkEngineUtils::GetMatricesFromNode(handle, abs*, rel*)` copies all four rows of both
(via `movaps`, so buffers must be 16-byte aligned). Its **second argument is absolute**.

~~The ship's node handle is at `+0x2A4`~~ — **wrong**, see above.

### 13.4 The ship is force-driven

`UpdateControlled` calls `GetVelocity` but **never** `SetLinearVelocity`; none of the
setter's 81 callers are in the ship update path. So a post-hook that reads velocity,
adds thruster Δv along the ship's right/up axes, and writes it back is not clobbered
within that call. The setter's third argument is `false` at 53 of 81 callsites.

### 13.5 Verification done without running the game

| Check | Result |
|---|---|
| llvm-mingw 20260908 (user-local) builds the DLL | 31 KB, exports `timeBeginPeriod`/`timeEndPeriod`, imports only UCRT/KERNEL32/USER32 |
| MinHook v1.3.4 hook under Wine | hooked function `16 → 1016` |
| Proxy under Wine 11.17, `winmm=n,b` | loads **and** chain-loads the real winmm |
| Proxy with `winmm=n` | loads but resolves itself — the trap, reproduced |
| DLL in a fake `NMS.exe` host | config parsed; graceful signature failure |
| **Self-test: DLL's C scanner vs. real `NMS.exe`** mapped as an image | all signatures unique — but self-test only proves the pattern is *found*, not that the field *means* what I assumed. That gap is exactly the `+0x2A4` bug. |
| Registry install/uninstall on copies of the real `user.reg` | exact inverse in 4 layouts, idempotent, double-uninstall safe |
| Wine honours our exact `AppDefaults` text | MS-versioned DLL: builtin → **native** with entry → builtin after uninstall |

System Wine 11.17 loads the proxy even without an override (the 11.6 no-version-resource
heuristic). The game runs on **Proton Experimental 11.0**, which lacks it, so the
override is still required — hence the installer writes it.

### 13.6 What remains unverified

Only an in-game run can confirm: that the post-hook write survives the physics step,
the sign convention of the node's right/up rows, behaviour while landed or in pulse
drive, and feel. `Debug=1` logs per-second velocity decomposed into right/up/at, so
the first flight session answers all of these from `BetterFlight.log`.


---

## 14. 1.0.0 failed to launch on Windows (fixed in 1.0.1)

**Report:** *"The procedure entry point waveOutReset could not be located in the dynamic
link library …\steam_api64.dll"* — game would not start.

**Cause: an incomplete proxy.** 1.0.0's `winmm.dll` exported only `timeBeginPeriod` and
`timeEndPeriod` — everything `NMS.exe` imports. But once a DLL is loaded as `winmm.dll`,
*every* module in the process that imports winmm binds to it. Scanning the game folder
and the Steam client DLLs that load into the game:

| Module | winmm imports |
|---|---|
| `NMS.exe` | `timeBeginPeriod`, `timeEndPeriod` |
| `sl.dlss_g.dll` (DLSS frame generation) | `timeBeginPeriod`, `timeEndPeriod` |
| `GameOverlayRenderer64.dll` | `timeBeginPeriod` |
| `steamclient64.dll` | **17**: `waveOut*`, `waveIn*`, `mixer*`, timers |

The Linux Steam client's `steamclient64.dll` doesn't import `waveOutReset`; the Windows
player's newer client does. Patching single names would chase this forever.

**Why Linux never showed it:** Windows refuses to start a process when any imported name is
missing. Wine logs it and substitutes a stub that only faults if called.

**Fix:** forward the complete interface. Reference export tables were taken from real
Microsoft binaries (via Winbindex → Microsoft symbol server):

| Source | Exports |
|---|---|
| Windows 10 22H2 (10.0.19041.546) | 181 (ordinal 2 unnamed) |
| Windows 11 24H2 (10.0.26100.9278) | 181 — **ordinals identical to 10 22H2** |
| Windows 11 26H1 preview (10.0.28000) | smaller; timer functions forwarded to `api-ms-win-mm-time-l1-1-0` |
| Wine / Proton 11 builtin | 189 |

The proxy exports the union — **189** (188 named + unnamed ordinal 2) — at Windows 11 24H2
ordinals. Each export is `jmp [table+i*8]`; the table is filled from the system winmm during
`DLL_PROCESS_ATTACH` (importing modules initialize after us). Every slot starts at a lazy
resolver that preserves all argument registers, and names the system DLL lacks fall back to
a stub returning 0 — nothing can jump to address zero.

**Verified offline:** complete coverage of both Windows export tables and every game-folder
import at matching ordinals; a runtime test calling winmm through the proxy produces output
identical to the real DLL under Wine, via both the eager and the lazy path.

**Also fixed:** builds were not reproducible (the linker embeds two timestamps), so
re-packaging silently changed the DLL's hash and invalidated VirusTotal links. Both
timestamps are now zeroed after linking.

**Lesson:** Wine is more permissive than Windows about DLL imports. "Works under Proton"
does not imply "works on Windows" for anything touching the loader.

---

## 15. Corvettes flew themselves during EVA

**Report (1.1.0 era):** on EVA from a corvette, the jetpack/roll keys also strafed the
corvette. It flew off into space. Single-seat ships were unaffected.

**Cause.** `UpdateControlled` has three callers, all in `cGcSpaceshipComponent::Update`
(build 25320008: `+0x173ecb2`, `+0x173f031`, `+0x173f247`). The game's own test before
flying a ship for the player is:

```
lea rcx,[rsi+0x60C0]; call IsValid      ; controller handle: ptr && *ptr
test al,al; je ..
cmp byte [rsi+0x60C8],0; jne controlled ; controller active
cmp dword [rsi+0x6420],3; je controlled
mov rax,[rsi+0x28]; cmp dword [rax+0xD0],0xA ; ship class 10 = Corvette
jne uncontrolled
...                                     ; corvette: UpdateControlled anyway
```

Corvettes are updated as "controlled" with **nobody at the controls**, so they keep
simulating while the player walks around inside or goes on EVA. The hook ran after every
call and applied whatever keys were held.

**Fix.** `flight_tick` now stands down unless the controller handle is valid **and** the
controller-active byte is set, the same test the game uses for every non-corvette path.
While standing down it reads no keys, so jetpack keys don't toggle Z/F8 either. The
offsets come from two independent signatures (`SIG_PILOT_CHECK`, and `SIG_PILOT_BRANCH`,
whose call target must be `UpdateControlled`). If neither resolves, the mod keeps its old
behaviour and logs it. The selftest requires the pilot check. Simulator tests P/P′/P″
cover it.

**Unverified in game:** whether a piloted ship ever runs with the active byte clear (the
`+0x6420 == 3` path). If it does, the mod would stand down in that state. `Debug=1` logs
every transition as `no pilot at the controls` / `pilot back at the controls`.

---

## 16. Flight retune moved from a data mod into the DLL

**Why:** the 1.1.x retune shipped as a replacement `GCSPACESHIPGLOBALS.GLOBAL.MBIN`.
Loose mods replace whole files, so it conflicted with every mod touching that file
(reported with *Prepare To Sky (PTSd)*). The DLL now applies the same 176 changes in
memory, on top of whatever the game loaded.

**Reflection metadata in NMS.exe** (layouts as documented in NMS.py `tools/extract.py`):

| Record | Size | Fields used |
|---|---|---|
| `cTkMetaDataClass` | 0x28, in `.rdata` | +0x00 name, +0x18 member table, +0x20 count, +0x24 size |
| `cTkMetaDataMember` | 0x58 | +0x00 name, +0x0C type (0x03 struct, 0x0E float), +0x14 size, +0x1C offset, +0x20 class |

- `cGcSpaceshipGlobals`: 723 members, 0x1EC0 bytes. Its member table is in `.bss` and
  is filled in by static initialisers (`lea rax,[name]; mov [rec],rax; mov [rec+..],imm`).
  In game it is already filled when the DLL looks, about 40 ms after the flight hook goes in.
- `cGcPlayerSpaceshipControlData` (23 members: `SpaceEngine` 0x15C, `PlanetEngine` 0xE8,
  `CombatEngine` 0x74, `AtmosCombatEngine` 0x0) and `cGcPlayerSpaceshipEngineData`
  (29 members) have member tables stored in the file. Offline selftest covers them.
- Six control blocks carry the `ControlData` class: `Control`, `ControlLight`,
  `ControlHeavy`, `ControlHeavyHover`, `ControlCorvette`, `ControlHover`. The DLL finds
  them by class, so a new ship type's block would be picked up automatically.

**Loaded instance.** As in NMS.py `globals.py`: `.rdata` string
`/GcSpaceshipGlobals.global.mbin` ← one `.data` slot ← one load stub
`mov rdx,[rip+slot]; lea rcx,[rip+instance]; jmp loader` (build 25320008: stub
`+0x2786150`, instance `+0x5217F20` in `.bss`, loader `+0x2789B70`). Each of the 38
globals files has its own stub and its own loader; the loader takes two register
arguments and returns a bool.

**MBIN = memory layout.** The MBIN header is 0x20 bytes (its template hash and GUID
equal the class record's), then the struct. At metadata offsets all 176 tuned fields
match MBINCompiler's decode of both the vanilla and the 1.1.x modded file.

**Applying.** No loader hook. On every `UpdateControlled` call the DLL compares each
tuned value with the bits it last wrote. Anything else means the game loaded or changed
it, so the change is applied again on that value (set, or multiply). `FlightRetune=0` or
F8 writes the loaded values back. If `GAMEDATA/MODS/BetterFlight` still exists, the
retune is skipped so nothing is applied twice.

**Verified in game (build 25320008):** 176/176 fields retuned on the first flight frame.
Every loaded value equals vanilla and every written value equals the 1.1.x data mod.

**Not yet verified in game:** F8 / `FlightRetune=0` restore (simulator only), and
running alongside PTSd.

---

## Sources

- [MBINCompiler / libMBIN](https://github.com/monkeyman192/MBINCompiler)
- [NMS.py](https://github.com/monkeyman192/NMS.py) · [pyMHF](https://github.com/monkeyman192/pyMHF)
- [AMUMSS Lua script collection](https://github.com/MetaIdea/nms-amumss-lua-mod-script-collection)
- [Step Modifications — GCSpaceshipGlobals reference](https://stepmodifications.org/wiki/NoMansSky:Reference_Guides/Global_Files/GCSpaceshipGlobals)
- [NMS-MMM flight overhaul](https://github.com/Himeki/NMS-MMM)
- [Freedom of Flight](https://www.nexusmods.com/nomanssky/mods/2490)
