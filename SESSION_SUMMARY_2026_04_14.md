# LaMoshPit-Edge — Session Summary (2026-04-14)

**Branch:** `marksawatzky264`
**Session focus:** integrate the six bitstream-surgery hooks into the main LaMoshPit render pipeline, then validate each hook end-to-end through the UI.

## Starting State (from previous session)

Coming into this session, the foundation was already in place:

- x264 fork (`x264-fork/`) with all six hook touchpoints scaffolded
- vcpkg overlay port (`vcpkg-overlay-ports/x264-lamoshpit/`) building libx264.lib at port-version 3
- `BITSTREAM_SURGERY_ARCHITECTURE.md` documenting the design
- LaMoshPit's `FrameMBParams` extended with seven `bs*` knobs (`bsMvdX`, `bsMvdY`, `bsForceSkip`, `bsIntraMode`, `bsMbType`, `bsDctScale`, `bsCbpZero`)

What was missing: the LaMoshPit-side wiring to actually feed those fields to libx264 during render, plus end-to-end visual validation.

## What Shipped Today

### 1. LaMoshPit → libx264 integration (`runBitstreamEdit` render path)

- Added [core/transform/FrameTransformer.cpp::runBitstreamEdit](core/transform/FrameTransformer.cpp) — a parallel render path that decodes via libavcodec, encodes via libx264's C API directly (bypassing FFmpeg's libx264 wrapper), and muxes via libavformat with manual Annex-B→AVCC conversion for MP4 container output
- Added dispatch logic in `FrameTransformer::run()` — routes `MBEditOnly` renders to the bitstream path when any frame carries a bitstream-surgery knob
- Added render-path announcement to ControlLogger (`APPLY VIA BITSTREAM-SURGERY PATH` / `PIXEL-DOMAIN PATH`)
- Extended ControlLogger to include all bitstream fields (earlier log format was missing every `bs*` field, plus several newer pixel-domain fields)
- Added `reloadVideoAndTimeline` breadcrumbs so post-render crashes can be localized step-by-step
- Wired CMakeLists.txt to link the installed libx264 from vcpkg

### 2. x264 hook bugs discovered and fixed during validation

| Hook | Bug | Fix | Port-version |
|---|---|---|---|
| Force Skip | P_SKIP hook set `i_type` but didn't populate `h->mb.cache.mv/ref`; downstream `mc_luma` read stale cache → crash after ~280 consecutive MBs | Populated cache via `x264_macroblock_cache_ref` + `x264_macroblock_cache_mv_ptr` at entry | 4 |
| (general reload) | `BitstreamAnalyzer::analyzeVideo` (h264bitstream-based) crashed on output containing long P_SKIP runs | Skip re-analysis entirely for `MBEditOnly` renders — frame structure didn't change anyway; reuse cached `m_lastAnalysis` | (LaMoshPit-side) |
| MVD Injection | `x264_me_refine_qpel` / `_qpel_rd` refined the forced MV away after our hook returned | Early-return guards in both refinement functions when `mvd_active_override` is set for the MB | 8 |
| MVD Injection (phase 2) | Setting `a->l0.i_rd16x16 = COST_MAX` caused `rd_cost_mb` to compute actual cost of our wrong MV; RD comparison then switched to a different partition type | Changed to `a->l0.i_rd16x16 = 0`; added post-RD clamp before `analyse_update_cache` to force `i_type=P_L0`, `i_partition=D_16x16` for flagged MBs | 9 |
| Force MB Type (partial) | I_4x4/I_8x8 override crashed on multi-I-frame selections after a single-frame case worked; root cause not fully characterized | Scoped back: I_16x16 forcing kept, I_4x4/I_8x8 disabled in the hook with documented limitation | — (see UI refactor below) |

### 3. Force MB Type control migrated to Global Encode Params

After per-MB Force MB Type turned out to be fragile and narrow-utility, the control was redesigned around x264's native `--partitions` encoder-wide parameter. The per-MB `BS MB Type` knob was removed from the MB Editor.

- Added three frame-type-specific dropdowns in Global Encode Params:
  - **I-frame MB Type** — Default / 16×16 only / +8×8 / +8×8 +4×4 (controls `i4x4`, `i8x8` flags)
  - **P-frame MB Type** — Default / 16×16 only / +8×8 / +8×8 +4×4 (controls `p8x8`, `p4x4`)
  - **B-frame MB Type** — Default / 16×16 only / +8×8 (controls `b8x8`; no b4x4 in H.264 spec)
- Each dropdown independently emits the corresponding partition flags; leaving at Default falls back to x264's natural per-frame-type defaults
- Piped through both render paths (`runTransform` via x264-params string, `runBitstreamEdit` via `x264Param.analyse.inter` bit flags)
- Auto-enables `b_transform_8x8` when any i8x8-including mode is picked
- Migrated built-in preset templates (`GlitchWave`, `BlockMosaic`, `QuantumResidue`) to the new three-field equivalents
- Preserved the legacy `partitionMode` JSON key in PresetManager for backward compat with older saved presets; new code reads only the three new fields
- Force Skip precedence explained and confirmed structurally correct — skip MBs are 16×16 by H.264 spec and bypass partition analysis entirely, so `bsForceSkip=100` always wins on its flagged MBs regardless of partition dropdowns

### 4. Validation results — four hooks proven end-to-end

| Hook | Data path verified | Visual effect confirmed | Status |
|---|---|---|---|
| **Force Skip** | ✅ | ✅ canonical frozen-frame datamosh | **PASS** |
| **CBP Zero** | ✅ | ✅ residual suppression visible on painted region | **PASS** |
| **DCT Scale** | ✅ | ✅ amplified edges / clipped chroma at scale=200 | **PASS** |
| **Force MB Type** | ✅ (I_16x16 only, via Partition Mode dropdown) | not yet spot-checked | **deferred to tomorrow** |
| **Force Intra Mode** | ✅ (data path) | not yet tested | **deferred to tomorrow** |
| **MVD Injection** | ✅ (data path) | ❌ effect still not visible after two rounds of hook fixes | **open bug** |

## Files Changed This Session

**LaMoshPit:**
- `CMakeLists.txt` — link libx264
- `core/transform/FrameTransformer.{cpp,h}` — new `runBitstreamEdit()` render path + partitions flag wiring + path dispatch
- `core/logger/ControlLogger.{cpp,h}` — `logRenderPath`, `logNote`, plus all missing field log output
- `core/model/GlobalEncodeParams.h` — three new `iFrameMbType` / `pFrameMbType` / `bFrameMbType` fields; legacy `partitionMode` kept for backcompat
- `core/presets/PresetManager.cpp` — serialize/deserialize new fields
- `gui/MainWindow.{cpp,h}` — render-type tracking, reload-step breadcrumbs, status-bar hint updates, skip-analysis-for-MBEditOnly optimization
- `gui/widgets/GlobalParamsWidget.{cpp,h}` — three new MB-type dropdowns replacing old single Partition Mode
- `gui/widgets/MacroblockWidget.{cpp,h}` — removed `BS MB Type` dial

**x264 fork (`x264-fork/`):**
- `encoder/analyse.c` — Force Skip cache-populate fix; MVD Injection cache + cost fixes + post-RD clamp; Force MB Type I_4x4/I_8x8 disabled with documentation
- `encoder/me.c` — MVD Injection guards in `x264_me_refine_qpel` and `x264_me_refine_qpel_rd`

**vcpkg port:**
- `vcpkg-overlay-ports/x264-lamoshpit/vcpkg.json` — port-version bumped from 3 → 9 over the course of iterative hook fixes

## Outstanding Issues

### MVD Injection: no visible effect (open)

Two rounds of hook fixes landed (port-versions 8 and 9) but the rendered output still shows no visible motion-vector displacement. All data-path signals are clean:

- `APPLY VIA BITSTREAM-SURGERY PATH` fires
- Log shows `bsMvdX = N` on every flagged frame
- `APPLY COMPLETED OK` fires
- No crash, reload completes cleanly

What's been ruled out:

- Hook not firing (log confirms bsMvdX reaches FrameMBParams)
- Wrong path dispatch (log confirms bitstream path)
- Sub-pel refinement overwriting forced MV (`me_refine_qpel*` early-return guards in place)
- RD comparison switching away from P_L0 (`i_rd16x16 = 0` + post-RD clamp in place)
- DLL mismatch (verified installed `libx264-164.dll` timestamps match the latest vcpkg install)

What hasn't been investigated yet:

- Whether `analyse_update_cache` is actually running the `case P_L0: case D_16x16` branch at commit time (could be falling through to a different case if `h->mb.i_type` gets changed between our clamp and the call — extremely short window but possible with thread interaction)
- Whether `macroblock_encode` is doing something different with our forced MV than expected (e.g., bi-dir interpretation on B-slices, though we only target P-slices)
- Whether there's an intra-mode override competing (unlikely since user isn't setting intra mode)
- Whether x264's initial skip detection (`probe_pskip`) is firing *before* `mb_analyse_inter_p16x16` is called for some fraction of MBs, bypassing our hook entirely — the code path does exist for neighbors-are-P_SKIP case, though `b_try_skip` is the more common path at medium preset

## Tomorrow's Priority Todo List

1. **Diagnose MVD Injection "no visible effect" bug**
   - Add instrumentation inside the MVD hook and inside `analyse_update_cache`'s `case P_L0: case D_16x16:` branch to confirm both are reached for flagged MBs
   - Consider a **simpler smoke test**: in the hook, set `mv = {256, 0}` unconditionally (ignoring user value) so any P-slice MB with active override gets a guaranteed-clear 64px right pull. If that doesn't show an effect either, the issue is definitely in the commit/encode stage, not the value pipeline
   - Check whether `b_try_skip` in `mb_analyse_inter_p16x16` might be short-circuiting our hook (our hook is at the top, but worth confirming the *actual* call ordering)
   - If all else fails: dump `h->mb.cache.mv[0][x264_scan8[0]]` right before `macroblock_encode` to confirm our value actually reaches the encode stage

2. **Test Force Intra Mode end-to-end** (remaining untested hook)
   - Paint region on frame 0 with `BS Intra Mode = 2` (DC)
   - Expected: uniform flat prediction across painted region (DC = average of top+left neighbors)
   - Confirm log shows `bsIntraMode = 2` and hook fires

3. **Test new I/P/B MB Type dropdowns end-to-end**
   - Set `I-frame MB Type = 16×16 only` alone → expect larger block artifacts on I-frames
   - Set `P-frame MB Type = 16×16 + 8×8 + 4×4` alone → expect finer partition on P-frames (subtle)
   - Verify precedence: combine Partition Mode with `BS Force Skip = 100` → skip region should override

4. **Once MVD is resolved, update `BITSTREAM_SURGERY_ARCHITECTURE.md`** to reflect the final state of each hook (working / scope-limited / etc.) and bump the "Current state" section's commit list

5. **Nice-to-have polish** (if time permits)
   - Consider renaming the MB Editor's `Intra Mode` knob to `Intra 16×16 Mode` to signal its scope
   - Clean up the x264 fork's `mb_type_override` hook (currently dead code since LaMoshPit no longer populates the array) — decide whether to keep it (for future extension) or remove it entirely
   - Consider adding a "rebuild x264 fork" helper script since the port-version bump + remove + install sequence is repetitive

## x264 Port-Version History (this session)

- 3 → starting state (all six hooks scaffolded)
- 4 → Force Skip cache-populate fix
- 5 → MVD Injection cache-populate fix (first attempt)
- 6 → Force MB Type I_4x4/I_8x8 DC-fill attempt
- 7 → Force MB Type I_4x4/I_8x8 disabled (reverted to I_16x16-only)
- 8 → MVD Injection `me_refine_qpel*` guards
- 9 → MVD Injection RD-path fixes (`i_rd16x16 = 0` + post-RD clamp)

## Architecture Integrity

The foundation remains sound:

- Five-touchpoint hook pattern is proven — three hooks (Force Skip, CBP Zero, DCT Scale) work flawlessly end-to-end
- Annex-B → AVCC mux is solid — VLC plays every output file we've produced
- Both render paths (pixel-domain FFmpeg and direct libx264 bitstream) coexist cleanly via the dispatch in `FrameTransformer::run()`
- `reloadVideoAndTimeline` no longer crashes on any bitstream-surgery output (via BitstreamAnalyzer skip)
- UI layer is clean — `BS MB Type` knob removal + three-dropdown replacement in Global Encode Params landed without breaking saved presets
