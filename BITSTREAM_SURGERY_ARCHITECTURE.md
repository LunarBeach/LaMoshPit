# LaMoshPit-Edge: Bitstream-Surgery Architecture via Forked x264

**Branch:** `marksawatzky264`
**Status:** x264 fork active, sanity-check build passing, LaMoshPit-side integration in progress
**Last updated:** 2026 — during initial architecture sessions

This document captures *why* LaMoshPit-Edge exists as a separate branch from the
stable `main`, *why* we chose to fork x264 rather than other approaches, and *how*
the resulting pipeline is structured. It is intended as an onboarding reference
for anyone (including us, coming back later) who needs to understand the
bitstream-level editing path without re-deriving all the decisions.

---

## 1. The core problem

LaMoshPit's MB Editor exposes ~31 per-macroblock knobs so the user can sculpt
datamosh effects at a fine granularity (QP delta, motion vector drift, ghost
blend, block flatten, posterize, sharpen, and so on). For most of these, the
existing pixel-domain pipeline works reliably — we manipulate the pixels before
they reach the encoder, and the encoder faithfully encodes what it's given.

But a specific subset of knobs is fundamentally different. These are the knobs
that ask the encoder to **make a specific decision** in the compressed bitstream
itself, not a pixel decision:

- **CBP Zero** — force `coded_block_pattern = 0` so no residual data is emitted
- **Force Skip** — force the MB to a skip type
- **Force Intra Mode** — override the intra prediction mode choice
- **Force MB Type** — override the MB partition-type choice (I_16x16, P_8x8, etc.)
- **Direct MVD Injection** — write arbitrary motion-vector-difference values
- **DCT Coefficient Scale** — force zeroing/scaling of residual coefficients

For these, the current pixel-domain approximation is ineffective because **x264
overrides the user's intent**. The encoder's rate-distortion optimizer, psycho-
visual filters, adaptive-quantization heuristics, coefficient deadzones, and
rate controller all actively fight to "correct" the user's unusual input. Even
at fixed-QP (CQP) mode with every intelligence disabled, some overrides persist
because they are baked into the encoding algorithms themselves.

The only way to guarantee these decisions survive into the output bitstream is
to make them at the encoder level — where x264's own code emits the bits.

---

## 2. Options considered

Before committing to a course of action, we investigated five distinct paths.

### Option A — Stay with pixel-domain approximations + stronger Global Encode recipes

Keep the current approach but surface more encoder-suppression controls
(deadzone-inter, deadzone-intra, qcomp, ipratio, pbratio, qblur) so the user
can neuter x264 more thoroughly.

**Verdict:** Useful and already implemented (see
`GlobalEncodeParams.qcompEnabled`, etc.). But it still doesn't *guarantee*
intent. The encoder always has the final say. Adopted as a complementary layer,
not a replacement.

### Option B — Use the `aizvorski/h264bitstream` library to read-modify-write the compressed NAL units

Parse slice data into the library's `macroblock_t` struct, modify the fields,
write back. No encoder involved; bit-level surgery on the already-compressed
stream.

**Verdict: REJECTED.** Investigation revealed the library is essentially a
spec-translation template with fatal gaps:
- The primitives `bs_read_te`, `bs_read_me`, `bs_read_ce` (CAVLC) and
  `bs_read_ae` (CABAC) are declared in `h264_slice_data.h` but **never
  defined anywhere in the library**. The library compiles only because it's a
  static archive; any actual call would fail at link time.
- Every slice-data function uses `macroblock_t* mb;` — an uninitialized local
  pointer that is immediately dereferenced. The library has no macroblock
  storage infrastructure at all.
- The CMake file explicitly excludes `h264_slice_data.c`/`.h` from the build
  with the comment `"These do not participate in build -- for now..."`.
- Checked the `ChristianFeldmann/h264bitstream` fork as a possible escape
  hatch — it's older than upstream, doesn't contain slice_data code at all,
  and hasn't been touched since 2017. A dead end.

To actually use this library we would have to write CAVLC primitives (~200-400
lines), implement CABAC (~2,000 lines), add macroblock storage to
`h264_stream_t`, fix the uninitialized-pointer bugs throughout — essentially,
build a new H.264 syntax codec from scratch using the library as an outline.

### Option C — Fork x264 and add per-MB override hooks to the encoder

Vendor x264's source into our tree, add new fields to its public API for
per-MB overrides (following the pattern of its existing `quant_offsets` and
`mb_info` arrays), wire them through to the hot-path encoder functions, and
short-circuit the relevant RD decisions when the user has set an override.

**Verdict: CHOSEN.** Reasons:

1. **We become the encoder.** There is no "encoder override" if we *are* the
   encoder. Once our hook sets `i_cbp_luma = 0`, the rest of x264's existing
   code emits zero residual — we don't fight x264, we ride its existing logic.
2. **Effort is tractable.** ~60 lines of surgical additions to x264 for each
   new hook, following a repeatable pattern.
3. **Works on CABAC streams.** Encoder-side hooks operate on the high-level
   syntax values before entropy coding — the choice of CAVLC vs. CABAC is
   transparent. No restriction on source video format.
4. **Auto-skip is free.** When we zero CBP and the MV matches the predicted
   skip MV, x264 naturally converts the MB to P_SKIP/B_SKIP — the cleanest
   datamosh freeze effect, produced entirely by x264's existing code paths.
5. **No FFmpeg changes required** (see Option X2 below).
6. **No ongoing maintenance.** Since *every* video LaMoshPit imports is
   re-encoded by our fork during `standardizeVideo`, we don't need to stay
   current with upstream x264. Freezing on a specific commit costs nothing.

### Option D — Hybrid: FFmpeg decodes for parsing, our own CAVLC encoder writes modifications

Use FFmpeg's reliable H.264 decoder to extract per-MB info during a decode
pass, modify in memory, then write a new bitstream using a custom CAVLC
encoder we build from scratch.

**Verdict: REJECTED in favor of Option C.** Writing a CAVLC encoder is
non-trivial (CAVLC's coefficient coding is intricate), and the result gives us
bit-exact control of the output but *no* RD-integrated encoding for
unmodified MBs — we'd have to re-emit those as well. Option C gives us the
same guarantees with much less new code and keeps x264's RD-optimized
encoding for everything we *don't* override.

### Option E — "Advanced mode" import with raw Annex B stream + severe GOP constraints

Offer users a toggle at import time that encodes into raw `.264` (Annex B)
format instead of MP4, with enforced CAVLC-only, P-only, no MBAFF, single
slice per frame, etc. This would have shrunk the custom parser scope if we
had gone with Option B.

**Verdict: OBSOLETE.** Made sense only under Option B. Under Option C, no
source-format constraints are required.

---

## 3. Path C — detailed architecture

### 3.1 Encoder hook pattern

The pattern for adding a new per-MB override to x264 is consistent across all
MB edits we want to enforce. Five touchpoints:

1. **`x264-fork/x264.h`** — add field to `x264_image_properties_t` (public API).
2. **`x264-fork/common/frame.h`** — mirror field in internal `x264_frame_t`.
3. **`x264-fork/common/frame.c`** — copy pointer from user input to internal
   frame at frame-queue time; free via callback at frame lifetime end.
4. **`x264-fork/encoder/encoder.c`** — hand off the pointer from `fenc` to
   `fdec` at encode start; free after last thread completes.
5. **`x264-fork/encoder/[macroblock.c | analyse.c | cabac.c | cavlc.c]`** —
   the hook itself: read the per-MB override value at `h->fdec->foo[mb_xy]`
   and short-circuit the relevant decision.

The pattern follows x264's existing `mb_info` field exactly, which is
battle-tested by x264's own lookahead infrastructure. Memory ownership follows
the caller-allocates + callee-frees-via-callback convention x264 uses for all
its per-MB input arrays.

### 3.2 LaMoshPit-Edge render architecture (Option X2)

The original question: how does the user's per-MB override data flow from
the LaMoshPit UI down to libx264?

**Option X1:** Patch FFmpeg too — add a new AVFrame side-data type and modify
`libavcodec/libx264.c` to pass it through. *Rejected* because it forks a
second huge project with its own maintenance cost.

**Option X2 (chosen):** Build a parallel render path in `FrameTransformer`
that calls libx264's C API *directly*, bypassing FFmpeg's encoder wrapper.
The existing pixel-domain render path continues to use FFmpeg unchanged.

**Flow for a bitstream-surgery render:**

```
LaMoshPit MB Editor
    │ user sets bsCbpZero > 0 (or any bitstream-surgery knob)
    ▼
FrameTransformer::startTransform() detects bitstream edits present
    │
    ▼
runBitstreamEdit()        [new render path]
    │
    ├─► libavformat::av_read_frame  (demux input MP4 — unchanged)
    ├─► libavcodec::avcodec_send_packet  (decode — unchanged)
    │
    ├─► Build x264_picture_t
    │   └─► pic.prop.cbp_override = [per-MB uint8_t array, 1 = force CBP=0]
    │   └─► pic.prop.cbp_override_free = &av_free  (or similar)
    │
    ├─► libx264::x264_encoder_encode(pic)  [OUR forked libx264]
    │   └─► x264 internally hits our hook in macroblock_encode()
    │   └─► Zeros i_cbp_luma / i_cbp_chroma for flagged MBs
    │   └─► CAVLC/CABAC writers emit no residual for those MBs
    │   └─► Output NAL packet
    │
    ├─► libavformat::av_write_frame  (mux output MP4 — unchanged)
    ▼
output.mp4 with guaranteed bitstream-level MB edits
```

The existing `runTransform()` path (pixel-domain render via FFmpeg's libx264
wrapper) remains untouched. The two paths coexist; only the `FrameTransformer`
dispatches to one or the other based on whether any frame has bitstream-surgery
knob values active.

---

## 4. Current state (commits on `marksawatzky264`)

Commits since `main`:

```
be0fbd52  Add vcpkg overlay port for x264-lamoshpit (sanity check PASSED)
abbf49f2  x264: CBP Zero override hook in macroblock encoder (the actual effect)
ec7d9c30  x264: hand off cbp_override fenc->fdec + free after last thread
af3c893f  x264: propagate cbp_override from input pic to internal frame
ef4f1a00  x264: mirror cbp_override in internal x264_frame_t struct
6194bb95  x264: add cbp_override field to public API (x264_image_properties_t)
4014379c  Vendor x264 source — baseline from videolan at commit 31e19f92
cfd77b86  (main branch is here — pristine working LaMoshPit, untouched)
```

### 4.1 Sanity check (passed)

```
$ ./vcpkg/vcpkg.exe install x264-lamoshpit \
    --overlay-ports=./vcpkg-overlay-ports \
    --triplet=x64-windows
```

Produces `vcpkg/installed/x64-windows/lib/libx264.lib` (Release) and the Debug
counterpart in ~2.1 minutes. The installed `x264.h` contains our
`cbp_override` field. Post-build validation passes.

### 4.2 Implemented hooks

| Knob              | x264 hook location                                       | Status | Notes                                                       |
| ----------------- | -------------------------------------------------------- | :----: | ----------------------------------------------------------- |
| CBP Zero          | `encoder/macroblock.c` `macroblock_encode_internal`      |   ✅   | Zeros i_cbp_luma/chroma; residual emission fully suppressed |
| Force Skip        | `encoder/analyse.c` top of `x264_macroblock_analyse`     |   ✅   | P→P_SKIP, B→B_SKIP, I-slice ignored                         |
| Force MB Type     | `encoder/analyse.c` after I-slice RD selection           |   ✅   | I-slice only for now; P-slice is future work                |
| Force Intra Mode  | `encoder/analyse.c` after I-slice type override          |   ✅   | I_16x16 mode only; per-block I_4x4/I_8x8 modes are future   |
| MVD Injection     | `encoder/analyse.c` top of `mb_analyse_inter_p16x16`     |   ✅   | 16x16 partition only; sub-partitions are future             |
| DCT Scale         | `encoder/macroblock.c` before CBP commit                 |   ✅   | Scales post-quant coefficients uniformly                    |

---

## 5. Status: all six hooks active

All six bitstream-surgery hooks are implemented and built into libx264.lib.
The full public API is in `vcpkg/installed/x64-windows/include/x264.h` — look
for the `// LaMoshPit-Edge extension` comment blocks in `x264_image_properties_t`.

| Public API field          | Knob                   | Type/semantics                                        |
| ------------------------- | ---------------------- | ----------------------------------------------------- |
| `cbp_override`            | CBP Zero               | uint8_t array, non-zero = force CBP=0                 |
| `mb_skip_override`        | Force Skip             | uint8_t array, non-zero = force skip (P/B only)       |
| `mb_type_override`        | Force MB Type          | uint8_t array, 1=I16x16, 2=I4x4, 3=I8x8, 4=P_L0, 5=P_8x8 |
| `intra_mode_override`     | Force Intra Mode       | int8_t array, -1=none, 0-3=V/H/DC/Plane               |
| `mvd_x_override` + `mvd_y_override` + `mvd_active_override` | MVD Injection | int16_t arrays (q-pel) + uint8_t enable flag |
| `dct_scale_override`      | DCT Scale              | uint8_t array, 255=none, 0-200=percent scale          |

### Known limitations (future work markers)

These are intentional simplifications, not bugs. They are documented in code
comments at each hook site and may be expanded later if the corresponding MB
Editor knobs grow in scope.

- **Force MB Type on P-slices**: Only I-slice path is currently implemented.
  Forcing specific P-slice types requires deeper integration with x264's
  inter-vs-intra RD decision path.
- **Force Intra Mode for I_4x4 / I_8x8**: Currently overrides only I_16x16
  prediction mode. Per-block (16 blocks × 4x4, or 4 blocks × 8x8) override
  would need arrays indexed differently.
- **Force Intra Mode for chroma**: Luma only. Chroma prediction mode
  override is a separate field we haven't added.
- **MVD Injection sub-partitions**: Covers 16x16 partition only. 16x8, 8x16,
  and 8x8 sub-partition MV override is future work.
- **DCT Scale selective**: Scales all coefficients uniformly. Per-block
  (e.g., only AC, only DC, only specific 8x8) is future work.

---

## 6. Build system

### 6.1 vcpkg overlay port

Location: `vcpkg-overlay-ports/x264-lamoshpit/`

Files:
- `vcpkg.json` — package manifest (inherits from vcpkg's stock x264 port)
- `portfile.cmake` — uses `file(COPY)` from our local `x264-fork/` instead of
  `vcpkg_from_gitlab`; then runs the same configure+make steps as stock x264
- `*.patch` — copied from vcpkg's stock x264 port (MSVC-compat fixes for
  configure, parallel-install, UWP, clang-cl support)

### 6.2 Build command

```powershell
./vcpkg/vcpkg.exe install x264-lamoshpit `
    --overlay-ports=./vcpkg-overlay-ports `
    --triplet=x64-windows
```

First build: ~2.1 minutes (provisions MSYS2, NASM, autoconf, automake, etc.).
Subsequent builds: ~20-60 seconds thanks to vcpkg's binary cache.

### 6.3 Rebuilding after modifying x264 source (IMPORTANT)

vcpkg computes its binary-cache ABI hash from the **portfile contents and package
metadata** — NOT from the source tree contents. When you add new encoder hooks
to `x264-fork/` without touching the portfile, vcpkg sees no change and silently
restores the previous cached build. Your modifications won't be in the installed
library.

**Protocol for adding a new hook:**

1. Make your `x264-fork/` source changes, commit them.
2. **Bump `port-version` in `vcpkg-overlay-ports/x264-lamoshpit/vcpkg.json`**
   (e.g. from `2` to `3`). Commit that separately with a message like
   `"vcpkg overlay: bump port-version to 3 (FooBar hook added)"`.
3. Remove the cached build and reinstall:
   ```powershell
   ./vcpkg/vcpkg.exe remove x264-lamoshpit --overlay-ports=./vcpkg-overlay-ports --triplet=x64-windows
   ./vcpkg/vcpkg.exe install x264-lamoshpit --overlay-ports=./vcpkg-overlay-ports --triplet=x64-windows
   ```
4. Verify your new field appears in `vcpkg/installed/x64-windows/include/x264.h`.

**Current port-version history:**
- `port-version: 1` → CBP Zero hook only
- `port-version: 2` → CBP Zero + Force Skip hooks
- `port-version: 3` → All 6 hooks active (CBP Zero, Force Skip, Force MB Type, Force Intra Mode, MVD Injection, DCT Scale)

### 6.4 x264 baseline choice

We vendored x264 at commit `31e19f92f00c7003fa115047ce50978bc98c3a0d`
(`"ppc: Fix compilation on unknown OS"`) specifically because it's the commit
vcpkg's stable x264 port targets. Using a different commit would require
updating the MSVC-compat patches.

**Do not casually upgrade the x264 baseline.** Our 5 hook commits rebase
cleanly against changes to unrelated files but may conflict with upstream
changes to `encoder/macroblock.c` or `x264.h`. Only upgrade when (a) we want
a specific upstream fix, and (b) we've verified the vcpkg patches still apply.

---

## 7. Licensing

- **Main LaMoshPit (`main` branch):** GPL-2.0-or-later (was Apache 2.0, relicensed
  during this work so our fork can legally link with GPL-2 x264).
- **LaMoshPit-Edge (`marksawatzky264` branch):** GPL-2.0-or-later (inherits).
- **x264:** GPL-2.0-only. Our fork is a derivative work under GPL-2. The
  `GPL-2.0-or-later` choice for LaMoshPit is specifically to make this link
  legal — `or-later` can operate as GPL-2.0 when combined with GPL-2-only code.

Apache 2.0 is *not* GPL-2 compatible because Apache's patent-retaliation and
attribution clauses conflict with GPL-2's "no additional restrictions" rule.
This is why the relicense was required before we could proceed with Path C.

---

## 8. Known caveats

### 8.1 Struct layout ABI

Our `cbp_override` fields are inserted in the middle of
`x264_image_properties_t`, between `mb_info_free` and the output fields
(`f_ssim`, `f_psnr_avg`, etc.). This shifts the offsets of the output fields.

**Who is affected:** Any code compiled against vanilla x264's `x264.h` but
linking against our modified `libx264.lib`. Reading `.f_ssim` would pull from
the wrong struct offset.

**Why it doesn't bite us now:** Our LaMoshPit-Edge code always uses our
modified header *and* our modified library together. FFmpeg's internal use of
x264 was baked in at FFmpeg build time and doesn't re-read our header.

**Future cleanup:** Move `cbp_override` fields to the *end* of
`x264_image_properties_t` (after `f_crf_avg`). This makes the extension
ABI-compatible with vanilla x264. Low priority; do before external distribution.

### 8.2 main-branch rebuilds

If someone checks out `main` and rebuilds LaMoshPit, they'll see `cbp_override`
in the vcpkg-installed `x264.h`. The extra fields are harmless since no
main-branch code references them. But if they run `vcpkg install --recurse`
it could trigger an FFmpeg rebuild against our modified x264, which *is*
ABI-breaking for callers that follow (see 8.1). Mitigation: the main branch
never asks for `x264-lamoshpit`; only `marksawatzky264` uses the overlay port.

### 8.3 Upstream x264 rebases

If we ever need to pull in upstream x264 changes (security fix, bug fix), we
need to:
1. Fetch upstream at a new commit
2. Test that vcpkg's patches still apply (update them if not)
3. Rebase our 5 hook commits (more if we've added more hooks)
4. Re-run the vcpkg build

This is manual work — no automation yet.

---

## 9. Related files

| File/path                                              | Role                                                      |
| ------------------------------------------------------ | --------------------------------------------------------- |
| `x264-fork/`                                           | Our modified x264 source tree                             |
| `x264-fork/x264.h`                                     | Public API — where per-MB override fields go              |
| `x264-fork/common/frame.h`                             | Internal frame struct — mirror fields                     |
| `x264-fork/common/frame.c`                             | Frame input copy + free callback invocation               |
| `x264-fork/encoder/encoder.c`                          | fenc→fdec handoff + post-encode free                      |
| `x264-fork/encoder/macroblock.c`                       | CBP Zero hook (and future residual-related hooks)         |
| `x264-fork/encoder/analyse.c`                          | Future: Force Skip / MB Type / Intra Mode / MVD hooks     |
| `vcpkg-overlay-ports/x264-lamoshpit/`                  | vcpkg build recipe for our modified x264                  |
| `core/model/MBEditData.h`                              | `FrameMBParams` — the `bsCbpZero`, `bsMvdX`, etc. fields  |
| `core/transform/FrameTransformer.cpp::runTransform`    | Existing pixel-domain render path (unchanged)             |
| `core/transform/FrameTransformer.cpp::runBitstreamEdit`| New render path using libx264 directly (to be written)   |
| `gui/widgets/MacroblockWidget.cpp`                     | UI for the MB edit knobs (no changes needed for Path C)   |

---

## 10. FAQ

**Q: Why a fork at all? Why not just patch vcpkg's existing x264 port?**
A: Modifying vcpkg's files directly would be overwritten by any `git pull` or
vcpkg update. An overlay port is the idiomatic vcpkg way to add a derivative.

**Q: Why name it `marksawatzky264` instead of `edge` or `bitstream`?**
A: User preference. It's a pun on Mark's name + "264". Rename is trivial
(`git branch -m`) if a more descriptive name is wanted later.

**Q: Why commit to `or-later` rather than `GPL-3`?**
A: Because x264 is `GPL-2.0-only`, and GPL-3 is not compatible with GPL-2-only
(GPL-3 adds restrictions that GPL-2-only cannot accept). `or-later` lets our
code operate under GPL-2 terms when combined with x264, while also being
compatible with GPL-3 if ever combined with GPL-3-only code later.

**Q: If we add more MB hooks, does every one need its own x264 rebuild?**
A: Yes. vcpkg will detect the source changed (via hash) and rebuild. On a warm
binary cache, this is ~20-60 seconds. On a cold cache, ~2 minutes.

**Q: Can the `main` branch benefit from any of this work?**
A: Not directly — bitstream surgery requires our fork. But the ABI concerns
(§8.1) mean we should NOT accidentally let main's build link against our
modified x264. Keep the overlay port scoped to the edge branch only.

**Q: What about other encoders — x265, SVT-AV1, etc.?**
A: Out of scope. LaMoshPit is specifically an H.264/x264 tool. Other codecs
have different syntax, different libraries, and would require separate forks.
