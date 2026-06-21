# CLAUDE.md

Guidance for AI assistants (and humans) working in this repository.

## What this is

An **EndStone C++ plugin** (`endstone_protectionstones.so`/`.dll`) that adds
ProtectionStones-style land protection to **Minecraft Bedrock**. EndStone is a
plugin loader for Bedrock Dedicated Server; the C++ API is **header-only** and
pulled via CMake `FetchContent` at the pinned tag **v0.11.4**.

Read `README.md` first for the user-facing feature set and command list.

## Build & toolchain (important)

- **Must build with Clang + libc++.** EndStone targets the BDS ABI (Release,
  `_ITERATOR_DEBUG_LEVEL == 0`) and its headers use `std::floorf`, which GCC's
  libstdc++ does not provide. Building with GCC/libstdc++ fails inside EndStone
  headers — this is expected, not a bug in our code.
- Dependencies are statically linked into a shared object, so
  `CMAKE_POSITION_INDEPENDENT_CODE` is ON (otherwise fmt fails to link).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build build
```

Output: `build/endstone_protectionstones.so` (exports `init_endstone_plugin`).
There is no test suite yet; "does it compile and link" is the current gate.

## Architecture

| File | Responsibility |
| --- | --- |
| `include/protectionstones/claim.h` | Plain-data `Claim`, `ClaimFlags`, size↔block mapping. **No EndStone include** — keep it dependency-free and cheap. |
| `claim_manager.{h,cpp}` | Owns all claims + the **chunk-based spatial index**; JSON load/save; overlap test. |
| `lang.{h,cpp}` | JSON message catalogue (Thai default). `Lang::get(key, args...)` uses `fmt::vformat` with positional `{}`. |
| `protection_listener.{h,cpp}` | **Event hot path.** All cancel decisions live here. Event types are forward-declared in the header; full headers included only in the `.cpp`. |
| `ui_manager.{h,cpp}` | Bedrock `ActionForm`/`ModalForm`/`MessageForm` menus. |
| `plugin.{h,cpp}` | Main `Plugin`, `/ps` command dispatch, permission decisions, particle border, `ENDSTONE_PLUGIN(...)` entry point. |

### Key invariants / conventions

- **XUID is the identity key** for owners and members; display names are a cache
  refreshed on `PlayerJoinEvent`. Never key membership by name.
- **Permission decisions have one home:** `ProtectionStonesPlugin::canManage /
  canBuild / canInteract`. The listener and UI call these — don't re-implement
  the logic inline.
- **Claims never overlap.** Creation is rejected via `ClaimManager::overlaps`
  (AABB on X/Z). Therefore `getClaimAt` can return the first match.
- **Hot path = no allocations on the miss case.** `getClaimAt` hashes to one
  chunk and returns a pointer or `nullptr`; handlers early-return when null.
- **Forms capture claim *ids*, never `Claim*`.** A claim may be deleted before a
  form callback fires; re-fetch with `getClaim(id)` and bail if null.
- **`ItemStack`/`Identifier` gotcha:** `endstone::Identifier` stores a
  `string_view` into the string you pass. Construct item stacks from **string
  literals** (e.g. `blockTypeForSize()` returns `const char*`), not temporary
  `std::string`s, to avoid dangling.
- **Persist after mutations.** Anything that changes a claim calls
  `claims_->save()` (also saved on `onDisable`). Saves are atomic (temp + rename).
- All user-facing text goes through `Lang` — no hardcoded strings in handlers.

### Spatial index shape

```
std::unordered_map<dimension_name,
    std::unordered_map<packed_chunk_key /* (cx<<32)|cz */,
        std::vector<claim_id>>>
```

A claim is registered in every chunk its AABB overlaps; removal un-registers the
same set. After `load()`, the index is rebuilt from scratch via `indexClaim`.

## Verifying EndStone API before coding

Do **not** guess EndStone signatures. The real headers are fetched to
`build/_deps/endstone-src/include/endstone/` after a configure — grep there to
confirm event names, method signatures, the `ENDSTONE_PLUGIN` macro, form/particle
APIs, etc. Or clone `github.com/EndstoneMC/endstone` at tag `v0.11.4`.

Events currently registered (see `onEnable`): `BlockPlace`, `BlockBreak`,
`BlockExplode`, `ActorExplode`, `BlockFromTo`, `BlockPistonExtend/Retract`,
`ActorDamage`, `PlayerInteract`, `PlayerJoin`.

## Known gaps / TODO

- Full lava-flow protection is stubbed behind the `lava` flag (reserved, off).
- No block-level fire-spread event is exposed by v0.11.4; fire protection covers
  only what is catchable (liquid/fire flow onto the centre, explosions).
- Piston protection scans up to 12 blocks ahead for a centre block rather than
  reading the exact moved-block list (not exposed by the event).
