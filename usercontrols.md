# LaMoshPit — Complete User Controls Reference

> A plain-language + deep-technical guide to every control in the application.
> No prior knowledge of video compression is assumed.  By the end you will understand
> not just what each knob does, but *why* it works — all the way down to the bitstream.

---

## Table of Contents

1. [What Is Video Compression? A Crash Course](#1-what-is-video-compression-a-crash-course)
2. [How LaMoshPit Works: The Damage Pipeline](#2-how-lamoshpit-works-the-damage-pipeline)
3. [Application Workflow Overview](#3-application-workflow-overview)
4. [Timeline Panel](#4-timeline-panel)
5. [Frame Type Conversion Toolbar](#5-frame-type-conversion-toolbar)
6. [Macroblock Canvas (The Paint Area)](#6-macroblock-canvas-the-paint-area)
7. [Transient Envelope Sliders](#7-transient-envelope-sliders)
8. [Macroblock Parameter Knobs](#8-macroblock-parameter-knobs)
   - 8.1 [Quantization — QP Δ](#81-quantization--qp-δ)
   - 8.2 [Temporal Reference — Ref Depth](#82-temporal-reference--ref-depth)
   - 8.3 [Temporal Reference — Ghost Blend](#83-temporal-reference--ghost-blend)
   - 8.4 [Temporal Reference — MV Drift X / Y](#84-temporal-reference--mv-drift-x--y)
   - 8.5 [Temporal Reference — MV Amplify](#85-temporal-reference--mv-amplify)
   - 8.6 [Luma Corruption — Noise Level](#86-luma-corruption--noise-level)
   - 8.7 [Luma Corruption — Pixel Offset](#87-luma-corruption--pixel-offset)
   - 8.8 [Luma Corruption — Invert %](#88-luma-corruption--invert-)
   - 8.9 [Chroma Corruption — Chr Drift X / Y](#89-chroma-corruption--chr-drift-x--y)
   - 8.10 [Chroma Corruption — Chr Offset](#810-chroma-corruption--chr-offset)
   - 8.11 [Chroma Corruption — Color Twist U / V](#811-chroma-corruption--color-twist-u--v)
   - 8.12 [Spatial Influence — Spill Out (Blast Radius)](#812-spatial-influence--spill-out-blast-radius)
   - 8.13 [Spatial Influence — Sample In (Incoming Radius)](#813-spatial-influence--sample-in-incoming-radius)
   - 8.14 [Pixel Manipulation — Block Flatten %](#814-pixel-manipulation--block-flatten-)
   - 8.15 [Pixel Manipulation — Ref Scatter px](#815-pixel-manipulation--ref-scatter-px)
9. [Global Encode Parameters Panel](#9-global-encode-parameters-panel)
   - 9.1 [Kill I-Frames Checkbox](#91-kill-i-frames-checkbox)
   - 9.2 [Frame Structure — GOP Size](#92-frame-structure--gop-size)
   - 9.3 [Frame Structure — B-Frames](#93-frame-structure--b-frames)
   - 9.4 [Frame Structure — B-Adapt](#94-frame-structure--b-adapt)
   - 9.5 [Frame Structure — Ref Frames](#95-frame-structure--ref-frames)
   - 9.6 [Rate Control — QP Override](#96-rate-control--qp-override)
   - 9.7 [Rate Control — QP Min / Max](#97-rate-control--qp-min--max)
   - 9.8 [Motion Estimation — ME Method](#98-motion-estimation--me-method)
   - 9.9 [Motion Estimation — ME Range](#99-motion-estimation--me-range)
   - 9.10 [Motion Estimation — Subpel Refinement](#910-motion-estimation--subpel-refinement)
   - 9.11 [Partitions & DCT — Partition Mode](#911-partitions--dct--partition-mode)
   - 9.12 [Partitions & DCT — 8×8 DCT](#912-partitions--dct--88-dct)
   - 9.13 [B-Frame Prediction — Direct Mode](#913-b-frame-prediction--direct-mode)
   - 9.14 [B-Frame Prediction — Weighted B / Weighted P](#914-b-frame-prediction--weighted-b--weighted-p)
   - 9.15 [Quantization Flags — Trellis](#915-quantization-flags--trellis)
   - 9.16 [Quantization Flags — No Fast P-Skip](#916-quantization-flags--no-fast-p-skip)
   - 9.17 [Quantization Flags — No DCT Decimate](#917-quantization-flags--no-dct-decimate)
   - 9.18 [Quantization Flags — Disable CABAC](#918-quantization-flags--disable-cabac)
   - 9.19 [Deblocking — Disable Deblock](#919-deblocking--disable-deblock)
   - 9.20 [Deblocking — Alpha / Beta Offsets](#920-deblocking--alpha--beta-offsets)
   - 9.21 [Psychovisual — Psy RD](#921-psychovisual--psy-rd)
   - 9.22 [Psychovisual — Psy Trellis](#922-psychovisual--psy-trellis)
   - 9.23 [Psychovisual — AQ Mode](#923-psychovisual--aq-mode)
   - 9.24 [Psychovisual — AQ Strength](#924-psychovisual--aq-strength)
   - 9.25 [Psychovisual — Disable MB-Tree](#925-psychovisual--disable-mb-tree)
   - 9.26 [Lookahead](#926-lookahead)
   - 9.27 [Spatial Mask](#927-spatial-mask)
10. [Presets Reference](#10-presets-reference)
11. [Compounding Iterations](#11-compounding-iterations)
12. [Quick-Start Examples](#12-quick-start-examples)

---

## 1. What Is Video Compression? A Crash Course

### The Problem Video Compression Solves

An uncompressed 1080p video at 30 fps produces about **6 gigabytes per second** of raw data.
A two-hour movie would be 43 terabytes. Compression shrinks this by 100–1000× by exploiting
two facts: adjacent pixels in a frame look similar (spatial redundancy), and adjacent frames
in a video look similar (temporal redundancy).

### Frames: I, P, and B

H.264 (the compression format this application targets) divides every video into a sequence
of three kinds of frames:

| Frame type | Name | What it stores |
|------------|------|----------------|
| **I-frame** (Intra) | Keyframe | A complete picture — no reference to anything else. Self-contained. |
| **P-frame** (Predicted) | Predicted frame | Only the *difference* from a previous frame. Uses motion vectors to describe where blocks moved. |
| **B-frame** (Bi-directional) | B-frame | The difference from *both* a previous AND a future frame simultaneously. Maximum compression. |

A typical video looks like: `I P P P P B B P P P P B B I P P P ...`

The stretch between two I-frames is called a **Group of Pictures (GOP)**.

### Macroblocks (MBs)

Each frame is divided into a grid of **16×16 pixel blocks** called **macroblocks** (MBs).
For a 1920×1080 video that's 120 columns × 68 rows = 8160 MBs per frame.
The encoder makes a decision for each MB independently: can this block be predicted from
a nearby reference, or does it need to be encoded from scratch?

### Motion Vectors (MVs)

When encoding a P-frame, the encoder asks: "Where did this MB come from in the previous frame?"
It searches the previous frame for the best matching block and records the **motion vector** —
the X/Y pixel offset pointing to the source block. Only the *error* between the predicted block
and the actual block (the **residual**) is stored, heavily compressed.

If the prediction is perfect, the residual is zero — the encoder writes almost nothing.
If the prediction is wrong, a large residual is needed — the encoder writes a lot.

### The DCT and Quantization

The residual difference image is transformed into frequency components via the
**Discrete Cosine Transform (DCT)**, then **quantized** — meaning small values are rounded
to zero. The **quantization parameter (QP)** controls how aggressively to round:

- **Low QP** (e.g. 18): very little rounding → good quality, large file
- **High QP** (e.g. 51): aggressive rounding → blocky quality, small file

QP 51 is the *maximum* — it forces nearly every DCT coefficient to zero, meaning the encoder
can only describe the inter-prediction part and cannot add any correction.

### The Reference Buffer

The encoder maintains a ring buffer of recently encoded frames. Each frame is encoded, decoded
back, and stored as a **reconstructed reference**. Future frames predict from this buffer.
If the reconstructed reference was corrupted *before* encoding, every future prediction carries
that corruption forward.

### Why Datamoshing Works

Datamoshing is the art of exploiting temporal prediction chains:

1. **Delete or corrupt an I-frame** so the decoder never gets a clean reference.
2. The following P-frames predict from an incorrect reference → they describe motion on top
   of the *wrong* pixel content → smears, trails, ghosting, color bleeding propagate forward.
3. With no I-frame to reset things, corruption compounds indefinitely across the GOP.

LaMoshPit automates and sculpts this process at the MB level, pixel level, and encoder-decision
level simultaneously.

---

## 2. How LaMoshPit Works: The Damage Pipeline

When you click **Apply**, the application runs a full re-encode of your video file using FFmpeg's
libavcodec (the H.264 encoder is libx264). The pipeline for each frame is:

```
Decode frame from disk
        │
        ▼
Kill I-frames? → force AV_PICTURE_TYPE_P, clear AV_FRAME_FLAG_KEY
        │
        ▼
Apply per-MB pixel edits (FrameMBParams):
  • MV Drift → paste reference pixels at offset → encoder finds that offset → assigns those MVs
  • Ghost Blend → fade frame toward reference at same position → creates temporal echo
  • Luma Corruption → noise, DC offset, inversion blend on Y plane
  • Chroma Corruption → UV plane drift, DC offset, color twist
  • Block Flatten → average pixels within each MB → forces skip MBs or big residuals
  • Ref Scatter → randomize reference sample positions → shattered glass
        │
        ▼
Attach QP ROI side data (AVRegionOfInterest) → per-MB QP offset
        │
        ▼
Apply spatial mask QP (if captured)
        │
        ▼
Send to libx264 encoder (with GlobalEncodeParams x264-params string)
        │
        ▼
Write to temp file → atomically replace source file
        │
        ▼
Reload player and timeline
```

**Key insight:** Pixel edits run on the raw frame *before encoding*. The encoder then tries to
predict the (already-corrupted) frame from the (already-corrupted) reference. Since both sides
carry the damage, the encoder finds "perfect" prediction for the corrupted content — the residual
is near-zero, the corruption propagates forward with no correction pathway.

The reference buffer is filled **after** pixel edits, so every subsequent frame predicts from
corrupted reconstructed frames, not the originals.

---

## 3. Application Workflow Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  1. Open File  →  Timeline fills with frame-type badges         │
│  2. Select frames in timeline  →  MB Canvas shows those frames  │
│  3. Paint MBs on canvas  →  red selection overlay               │
│  4. Dial in MB parameter knobs  →  sets FrameMBParams           │
│  5. Set Transient Envelope  →  cascadeLen + cascadeDecay        │
│  6. (Optional) Set Global Encode Params  →  whole-encode flags  │
│  7. Click Apply  →  re-encode runs, file replaced in-place      │
│  8. Preview player updates automatically  →  see the damage     │
│  9. Repeat from step 2  →  damage compounds on the damaged file │
└─────────────────────────────────────────────────────────────────┘
```

Each "Apply" reads the already-damaged file and damages it further. There is no undo beyond
keeping a backup of your original file — keep one.

---

## 4. Timeline Panel

The timeline displays every frame in the video as a small badge showing its type:
`I` (green), `P` (blue), `B` (gray).

**Click** a single frame to jump the canvas to that frame.
**Click and drag** to select a range. Selected frames are highlighted.

The timeline does not directly apply effects — it controls which frame is visible in the
MB Canvas so you can paint the MB selection for that frame. Frame type badges update after
each Apply operation so you can see the structural impact of Kill I-Frames and GOP changes.

**When changes appear in the player:** Immediately after Apply completes — the player reloads
from the newly-written file.

---

## 5. Frame Type Conversion Toolbar

Buttons that rewrite specific frames in the video stream:

| Button | Effect | x264 hook |
|--------|--------|-----------|
| **Force I** | Selected frames become IDR (full keyframe) | `pict_type = AV_PICTURE_TYPE_I`, sets `AV_FRAME_FLAG_KEY` |
| **Force P** | Selected frames become P-frames | `pict_type = AV_PICTURE_TYPE_P`, clears `AV_FRAME_FLAG_KEY` |
| **Force B** | Selected frames become B-frames | `pict_type = AV_PICTURE_TYPE_B`, clears `AV_FRAME_FLAG_KEY` |
| **Delete** | Removes selected frames entirely | Frames are omitted from the output mux |
| **Duplicate** | Inserts a copy of selected frames | Frames are muxed twice |
| **Undo** | Reverts the last frame-type operation | Restores the pre-operation file |

**Important:** Forcing a frame to type P or B requests that the encoder *attempt* a predicted
frame. The encoder may override this if it detects that no valid reference exists. Kill I-Frames
(described below) is a stronger enforcement that runs at the decoder level before the encoder
sees the frame.

**Scope:** Affects only the selected frames in the timeline. No MB edits are involved.

**When visible in player:** The re-encode is fast (no pixel edits), typically complete in seconds.
Changes appear in the player immediately. Structural changes like deleting an I-frame become
visible as the player decodes forward into the next GOP.

---

## 6. Macroblock Canvas (The Paint Area)

### The Grid Overlay

The canvas displays the current frame at full resolution with a 16×16 pixel grid drawn over it.
Each cell in the grid = one macroblock. Painting a cell marks it as "selected" — the MB
parameter knobs will apply to exactly those MBs.

### Painting Controls

| Control | Interaction |
|---------|-------------|
| **Paint** | Left-click drag over MBs to select them (adds to selection) |
| **Erase** | Right-click drag to deselect MBs |
| **Brush Size** | SpinBox (`m_sbBrush`). Sets the paint brush radius in MBs. `1` = single MB; `4` = paints a 9×9 MB square centered on cursor. |
| **Clear Frame** | Button — clears all MB selection AND all knob values for the *current* frame only. (`FrameMBParams` entry removed from `m_edits[currentFrame]`) |
| **Clear All** | Button — clears every MB edit on every frame. The entire `MBEditMap` is reset to empty. |
| **← Prev / Next →** | Navigation buttons. Step through frames. Emits `frameNavigated(int)` to sync the timeline selection. |
| **Frame indicator** | Label showing current frame index and total frame count, e.g. `Frame 42 / 300`. |

### What the Selection Does

The set of painted MBs is stored in `FrameMBParams::selectedMBs` (a `QSet<int>` where index =
`row * mbCols + col`). This set is the *spatial mask* for all per-MB operations. No effect runs
unless at least one MB is selected.

The selection is also continuously emitted via `mbSelectionChanged(const QSet<int>&)` so the
Global Params panel can capture it as a spatial mask (see section 9.27).

**Scope:** Per-frame. Each frame has its own independent painted selection stored in `m_edits[frameIdx]`.

---

## 7. Transient Envelope Sliders

These two prominent sliders sit *above* the scrollable knob area and control how damage propagates
forward in time from the frame you painted.

### What "Cascade" Means

When you paint MBs and set knob values on frame N, you are normally only affecting frame N.
The transient envelope automatically generates "ghost" edit entries for frames N+1, N+2, ..., N+L
(where L = transient length) that echo the same corruption with a decaying intensity.
This simulates the way real analog glitches *ring* or *smear* forward over several frames before
dying out — rather than a single abrupt hit.

### Length Slider — `cascadeLen`

| | |
|---|---|
| **Parameter** | `FrameMBParams::cascadeLen` |
| **Range** | 0 – 60 frames |
| **Default** | 0 (no cascade — single frame only) |
| **Display** | Label reads `N frames` |
| **x264 mechanism** | For each cascade frame, a synthetic `FrameMBParams` is injected into `activeEdits`. Each synthetic entry carries a scaled `ghostBlend`, `refDepth=1`, and critically **`qpDelta=51`** to prevent the encoder from adding corrective residuals that would fight the smear. |

**In plain language:** "How many frames after the painted one should carry the effect?"

Setting Length=10 means your painted damage echoes across 10 subsequent frames. The encoder sees
slightly different content on each of those frames and cannot correct it (QP=51 forces residuals
to zero), so the artifact propagates visibly in the player as a temporal smear or streak.

**When visible in player:** Frames N+1 through N+L all show the cascading artifact. Play forward
from your painted frame after Apply and you'll see the effect ring out and fade.

**Scope:** Per painted frame. Each frame has its own cascade length. Overlapping cascades from
adjacent painted frames compound automatically.

### Decay Slider — `cascadeDecay`

| | |
|---|---|
| **Parameter** | `FrameMBParams::cascadeDecay` |
| **Range** | 0 – 100 |
| **Default** | 0 (no decay — every cascade frame gets full intensity) |
| **Display** | Shape description: "flat", "gentle", "medium", "steep", "very steep" |
| **Formula** | `retainPerFrame = 1.0 - decay/200` → range 0.5..1.0 per frame. Intensity of cascade frame K = `baseIntensity × retainPerFrame^K` |

**In plain language:** "How quickly should the effect fade away across those cascade frames?"

- **Decay=0 / flat:** Every cascade frame has the same full intensity. Creates a hard plateau of
  corruption that suddenly stops.
- **Decay=50 / medium:** Each cascade frame has ~75% the intensity of the previous one.
  Creates a smooth fade-out.
- **Decay=100 / very steep:** Each frame has ~50% the intensity of the previous. Effect halves
  every frame — rapid fade.

**Pairing with Length:** Long Length + low Decay = a wide, flat corruption zone (good for sustained
smears). Short Length + high Decay = a brief sharp hit that rings out quickly (good for momentary
glitch pulses).

---

## 8. Macroblock Parameter Knobs

Each knob group affects a different aspect of how the selected MBs are corrupted.
All edits are applied to the raw YUV pixel data *before* the encoder sees the frame.

The knobs are rendered as paired `QDial + QSpinBox` controls. The dial and spinbox are always
in sync. The spinbox lets you type exact values; the dial provides tactile control.

---

### 8.1 Quantization — QP Δ

| | |
|---|---|
| **Parameter** | `FrameMBParams::qpDelta` |
| **Range** | −51 to +51 |
| **Default** | 0 (no change) |
| **x264 mechanism** | `AVRegionOfInterest` side data on the `AVFrame` before encoding. Field: `roi.qoffset = AVRational{qpDelta, 51}`. libx264 reads this and shifts the quantizer step for those MBs. |
| **Code location** | `applyQPROI()` in `FrameTransformer.cpp` |

**What QP is:** The Quantization Parameter is the single most important variable in H.264 quality.
A lower QP means finer quantization (more DCT coefficients survive) → better quality.
A higher QP means coarser quantization (most coefficients are rounded to zero) → heavy blocking.

**What QP Δ does:** This is an *offset* from the encoder's global QP decision. `+51` means "use
the maximum possible quantization on these MBs." At +51, virtually all DCT residual coefficients
are forced to zero. The encoder can only output the inter-prediction result — it cannot add any
correction on top. This is essential for making datamosh smears stick: if the residual is zero,
the only information in the frame is the (corrupted) motion-predicted content.

**Creative use:**
- `+51` on selected MBs: those MBs carry zero corrective information → their content is entirely
  determined by what the encoder predicted from the corrupted reference. Maximum smear propagation.
- Negative values (e.g. −20): those MBs get higher quality than the rest of the frame.
  Useful if you want some areas to remain sharp while others glitch.
- `0`: no effect (default).

**Visible in player:** Immediately on the encoded frame. High positive values produce blocky,
posterized, or smeared areas. The frame containing the edit is the first to show the effect.

---

### 8.2 Temporal Reference — Ref Depth

| | |
|---|---|
| **Parameter** | `FrameMBParams::refDepth` |
| **Range** | 0 – 7 |
| **Default** | 0 (disabled — current frame only) |
| **x264 mechanism** | Selects which slot from the internal ring buffer (`refBuf[refDepth-1]`) is used as the pixel source for ghost blend and MV drift operations. `refBuf[0]` = 1 frame back, `refBuf[6]` = 7 frames back. |

**What the reference buffer is:** During the encode pass, after each frame's pixel edits are
applied, a copy is placed into a ring buffer. Slot 0 = the frame just encoded. Slot 6 = the frame
encoded 7 steps ago. This buffer stores *already-corrupted* reconstructed content.

**What Ref Depth does:** Selects how far back in time the pixel source for blend/drift operations
is drawn from. `refDepth=1` = blend toward what the *previous* frame looked like (after its own
edits). `refDepth=5` = blend toward what frame N-5 looked like.

**Creative use:**
- Low values (1–2): tight temporal echo, creates a one-or-two-frame ghost.
- High values (4–7): deep time echo. Objects from many frames back "bleed" into the current
  frame. Good for dissolve-like smears or ghosting of motion from long ago.
- `0`: disabled — ghost blend and MV drift have no effect even if their own values are non-zero.

**Scope:** Per frame, per selected MBs.

---

### 8.3 Temporal Reference — Ghost Blend

| | |
|---|---|
| **Parameter** | `FrameMBParams::ghostBlend` |
| **Range** | 0 – 100 % |
| **Default** | 0 (no blend) |
| **x264 mechanism** | Pixel-level linear blend: `output = (current * (100-blend) + reference * blend) / 100`. Applied to Y, U, V planes for selected MBs. The reference is `refBuf[refDepth-1]` at the *same spatial position* (no spatial shift). Code: `applyGhostBlend()`. |

**What it does:** Fades the current frame's pixels toward a past frame's pixels at the same
location. At 0% = no effect. At 100% = selected MBs show exactly what those pixels looked like
`refDepth` frames ago. At 50% = a 50/50 mix of now and then.

Because the reference frame may itself have been corrupted by earlier edits, the blend content
carries compounded damage.

**Creative use:**
- 30–50%: subtle ghost effect — a translucent double-image of past content.
- 100%: complete temporal substitution. The encoder sees identical content between predicted
  frames → emits "skip" MBs (zero motion vectors, zero residual) → those MBs become frozen
  patches of old content that don't update.
- Combine with Ref Depth 3–5: ghosting from several frames back creates a dissolving echo trail.

**When visible:** On the encoded frame. With sampleRadius=0, the ghost has sharp block boundaries
aligned to the 16×16 MB grid. Use Sample In (8.13) to soften these edges.

---

### 8.4 Temporal Reference — MV Drift X / Y

| | |
|---|---|
| **Parameters** | `FrameMBParams::mvDriftX`, `FrameMBParams::mvDriftY` |
| **Range** | −128 to +128 pixels each |
| **Default** | 0 (no drift) |
| **x264 mechanism** | Copies Y+UV pixels from `refBuf[refDepth-1]` at position `(mbOrigin + driftXY)` into the destination MB. The encoder then finds these pixels and assigns motion vectors pointing to the offset location. Code: `applyMVPixelDrift()`. UV planes use `driftX/2, driftY/2` (because UV is half-size in 4:2:0). |

**What motion vectors are:** Every inter-coded MB in H.264 has motion vectors — the encoder's
answer to "where did this block come from in the reference frame?" MVs are stored in the bitstream
and decoded. If you force a specific pixel content into a MB, the encoder finds the best match in
its search window and assigns the vector pointing to where that content came from.

**What MV Drift does:** Instead of using the reference frame at the *same* position (which would
give zero-motion vector), we paste content from a *shifted* position. The encoder sees this shifted
content, searches for where it came from, and assigns a motion vector of approximately `(driftX, driftY)`.

The result: those MBs appear to be moving in the encoded bitstream. Since the reference carries
corrupted content, the "moving" block may be trailing a smear or dragging visual artifacts across
the frame.

**Creative use:**
- Small values (±5–20): subtle directional drift — a slow crawl of the reference toward a corner.
- Large values (±50–128): dramatic spatial displacement — content jumps across the frame.
- Negative X: reference pixels pulled from the right, MV encoded as leftward.
- Combine with MV Amplify to multiply the effective shift without changing the UI value.
- Combine X and Y to create diagonal drift paths.

**Scope:** Per MB, per frame. Different MBs in the same frame can have different drift values
if you change knobs between painting steps (each painted set is saved independently).

---

### 8.5 Temporal Reference — MV Amplify

| | |
|---|---|
| **Parameter** | `FrameMBParams::mvAmplify` |
| **Range** | 1 – 16 |
| **Default** | 1 (no amplification) |
| **x264 mechanism** | Multiplier applied to `mvDriftX` and `mvDriftY` before the pixel copy. Effective drift = `mvDriftX * mvAmplify`. |

**In plain language:** A gain stage on top of the MV Drift controls. If MV Drift X = 10 and
MV Amplify = 4, the actual pixel shift is 40 pixels.

This allows you to set a moderate base drift value and then scale it up dramatically without
losing the ability to make fine adjustments to the base value with the drift knobs.

**Creative use:**
- `mvAmplify=8` with `mvDriftX=16`: effective drift of 128px — full-frame displacement.
- Varying amplify over multiple frames creates acceleration effects (drift path speeds up).

---

### 8.6 Luma Corruption — Noise Level

| | |
|---|---|
| **Parameter** | `FrameMBParams::noiseLevel` |
| **Range** | 0 – 255 |
| **Default** | 0 (no noise) |
| **x264 mechanism** | Per-pixel: `v += rand() % (2*noise+1) - noise`. Applied only to the Y (luma) plane. Code: `applyLumaCorruption()`, `noiseLevel > 0` branch. |

**What luma is:** In YUV color space (which H.264 uses internally), Y = brightness/luminance.
U and V = color difference (chroma). The luma plane is a grayscale representation of the frame.

**What noise does:** Adds random signed offsets to each pixel's brightness before the encoder
sees the frame. The encoder then has to encode these random differences as residuals, producing
high-frequency textural noise artifacts.

Because the noisy frame becomes the reference for subsequent frames, any future P-frames that
ghost-blend or MV-drift from this reference also carry the noise pattern forward.

**Creative use:**
- Low values (10–30): subtle grain — adds texture that looks like analog tape noise.
- High values (100–255): heavy static over selected MBs — creates a "television interference" look.
- Combined with Ghost Blend: the noise pattern from the past frame bleeds into the current one.

---

### 8.7 Luma Corruption — Pixel Offset

| | |
|---|---|
| **Parameter** | `FrameMBParams::pixelOffset` |
| **Range** | −128 to +127 |
| **Default** | 0 (no offset) |
| **x264 mechanism** | Per-pixel: `v = clamp(v + pixelOffset, 0, 255)`. Applied to Y plane. Code: `applyLumaCorruption()`, `pixelOffset != 0` branch. |

**What it does:** Shifts the brightness of every pixel in selected MBs by a constant amount.
Positive = brighter. Negative = darker.

**Creative use:**
- `+127`: blow out selected MBs to near-white.
- `-127`: crush them to near-black.
- Used with Ghost Blend: the reference frame's offset pixels blend into the current frame's
  version of those MBs, creating a brightness flicker or wipe effect over time.
- Combined with invert blend: the offset changes the "neutral" point around which inversion occurs.

---

### 8.8 Luma Corruption — Invert %

| | |
|---|---|
| **Parameter** | `FrameMBParams::invertLuma` |
| **Range** | 0 – 100 % |
| **Default** | 0 (no inversion) |
| **x264 mechanism** | Per-pixel: `v = (v * (100-invertPct) + (255-v) * invertPct) / 100`. Applied to Y plane. Code: `applyLumaCorruption()`, `invertPct > 0` branch. |

**What it does:** Blends the luma plane toward its photographic negative. `255 - Y` = the
inverted value. At 100%, selected MBs appear as a pure negative of the brightness — dark becomes
light and light becomes dark.

**Creative use:**
- 50%: creates a ghostly solarization where mid-tones collapse toward middle gray.
- 100%: hard invert — dramatic contrast reversal.
- Because Y=128 inverts to Y=127 (neutral gray), high invert values tend to wash out contrast.
- Used with cascade: inversion propagates forward in time, creating a rippling negative effect.

---

### 8.9 Chroma Corruption — Chr Drift X / Y

| | |
|---|---|
| **Parameters** | `FrameMBParams::chromaDriftX`, `FrameMBParams::chromaDriftY` |
| **Range** | −128 to +128 pixels each |
| **Default** | 0 |
| **x264 mechanism** | Copies U and V plane pixels (and only U/V — not Y) from `refBuf[refDepth-1]` at position `(mbOriginUV + driftXY/2)`. Y plane is unaffected. Code: `applyChromaCorruption()`. |

**What chroma planes are:** U (Cb) and V (Cr) encode color difference information. In 4:2:0
format (standard H.264), each U/V pixel covers a 2×2 region of Y pixels — so U/V have half the
width and height of Y. This means a 16px chroma drift covers 32 luma pixels of spatial shift.

**What Chroma Drift does:** Moves the color information independently from the brightness.
The luma (edges, detail) stays in place but the colors "slide" spatially by the drift amount.
This creates **chromatic aberration** — the classic lens or CRT mis-registration look where
different color channels are misaligned.

**Creative use:**
- `chromaDriftX = +20, chromaDriftY = 0`: horizontal color fringing — red/blue edges appear offset.
- Large values: full color dissociation. The hue at any pixel comes from a completely different
  location in time, while the brightness stays current.
- Combined with ghost blend on luma: ghostly monochrome shape with vibrant color offset from a
  different moment in time.

---

### 8.10 Chroma Corruption — Chr Offset

| | |
|---|---|
| **Parameter** | `FrameMBParams::chromaOffset` |
| **Range** | −128 to +127 |
| **Default** | 0 |
| **x264 mechanism** | Per-pixel DC offset applied to both U and V planes: `u = clamp(u + dcOffset, 0, 255)`. Code: `applyChromaCorruption()`, `dcOffset != 0` branch. |

**What it does:** Shifts the hue of selected MBs by adding a constant to both color difference
channels simultaneously. This shifts the overall color temperature: positive values push toward
green (U) and red (V) depending on existing content; negative pushes the opposite directions.

**Creative use:**
- Large positive values on a normally colored scene: turns skin tones green, skies cyan.
- Combine with Chroma Drift for double-layer color corruption (shifted position + shifted hue).
- Different offset values on different MBs in the same frame: creates a patchwork of color zones.

---

### 8.11 Chroma Corruption — Color Twist U / V

| | |
|---|---|
| **Parameters** | `FrameMBParams::colorTwistU`, `FrameMBParams::colorTwistV` |
| **Range** | −127 to +127 each |
| **Default** | 0 |
| **x264 mechanism** | Independent per-channel DC offsets: `u = clamp(u + colorTwistU, 0, 255)`, `v = clamp(v + colorTwistV, 0, 255)`. Unlike Chr Offset (which offsets both equally), these let you steer U and V independently. Code: `applyColorTwist()`. |

**The difference from Chr Offset:** Chr Offset moves U and V by the same amount. Color Twist
lets you drive them independently, giving you full hue rotation control. In YUV color space:
- U = blue-difference (high U = green, low U = blue-purple)
- V = red-difference (high V = orange-red, low V = cyan-green)

**Creative use:**
- `colorTwistU = +80, colorTwistV = -80`: shifts toward green-cyan.
- `colorTwistU = -127, colorTwistV = +127`: push toward heavy blue-red split.
- Fine-tune after Chr Offset to hit specific color targets.
- Different U/V twist on adjacent MBs: color gradient across the frame.

---

### 8.12 Spatial Influence — Spill Out (Blast Radius)

| | |
|---|---|
| **Parameter** | `FrameMBParams::spillRadius` |
| **Range** | 0 – 8 MBs |
| **Default** | 0 (exact painted selection only) |
| **x264 mechanism** | Before applying any per-MB edit, the selected MB set is expanded using `expandedMBs(selectedMBs, spillRadius, mbCols, mbRows)`. This adds all MBs within `spillRadius` steps (Manhattan-ish square bloom) to the effective selection. The knob values are applied to this expanded set. |

**In plain language:** Makes the effect "bleed out" beyond exactly what you painted. If you paint
a single MB and set Spill=3, the entire 7×7 MB region around that MB gets the effect applied.

**Creative use:**
- `spillRadius=1`: one-MB buffer zone around painted region. Useful for ensuring block boundaries
  don't create sharp edges at the selection perimeter.
- `spillRadius=4–8`: the effect "explodes" out from the painted region. Great for creating
  large corruption zones from minimal painting.
- Combined with noise: spill creates a noise "cloud" emanating from the painted center.
- Combined with ghost blend: creates a wide temporal echo zone with painted precision at center.

**Scope:** Expands the selection at apply-time only — the canvas still shows only the painted MBs.
The expansion is computed fresh on each apply.

---

### 8.13 Spatial Influence — Sample In (Incoming Radius)

| | |
|---|---|
| **Parameter** | `FrameMBParams::sampleRadius` |
| **Range** | 0 – 8 MBs |
| **Default** | 0 (point sample — exactly aligned pixels) |
| **x264 mechanism** | When `sampleRadius > 0`, ghost blend uses `applyGhostBlendBlurred()` instead of `applyGhostBlend()`. For each pixel, the reference value is the *spatial average* of all pixels within `sampleRadius * 16` pixels in the reference frame. This is a box blur of the reference before blending. |

**In plain language:** Controls how "wide" the temporal echo is when blending toward the reference
frame. With `sampleRadius=0`, you get a sharp ghost — an exact copy of the reference at the
aligned position. With `sampleRadius=3`, you get a blurry, diffuse smear — each pixel blends
toward the average of a 96×96 pixel neighborhood in the reference.

**The difference from Spill Out:** Spill Out is about which MBs are *affected* (outward blast
radius). Sample In is about where the *source pixels* are drawn from (incoming smoothing).

**Creative use:**
- `sampleRadius=1–2`: softens ghost edges. Instead of a crisp block overlay, the ghost feathers
  into the surrounding content.
- `sampleRadius=5–8`: very large incoming sampling window. The ghost becomes a diffuse glow or
  haze drawn from a large area of the reference frame. Good for atmospheric blur artifacts.
- Combined with high ghost blend: diffuse temporal smear — content from the past melts into
  the current frame like ink dropped in water.

---

### 8.14 Pixel Manipulation — Block Flatten %

| | |
|---|---|
| **Parameter** | `FrameMBParams::blockFlatten` |
| **Range** | 0 – 100 % |
| **Default** | 0 (no flattening) |
| **x264 mechanism** | For each selected MB, compute the spatial average Y value across the 16×16 block. Then blend each pixel toward that average: `v = v * (1 - flattenPct/100) + avg * (flattenPct/100)`. Applied to Y plane. Code: `applyBlockFlatten()`. |

**What it does:** Reduces the internal contrast within each selected MB by pulling all pixels
toward the block mean. At 100%, every pixel in the MB becomes the same value — a perfectly flat,
posterized block.

**Why it creates artifacts:** The encoder sees a nearly-uniform block in the current frame.
It tries to predict this from the reference (which may have texture). The discrepancy between
the flat block and the textured reference becomes a large residual, and depending on QP, that
residual may not be encoded accurately — resulting in strange flat patches surrounded by buzzing
or smearing prediction error.

At high values, the encoder often decides to encode the flat block as a "skip MB" (using the
inter-prediction directly) which locks those MBs to whatever the encoder predicted — which may
be completely wrong content.

**Creative use:**
- 30–60%: reduces texture on selected MBs → mosaic-like appearance.
- 100% + high QP delta: forces perfectly flat blocks that the encoder encodes with near-zero
  residual → creates uniform-color patches that "drift" with motion prediction artifacts.
- Good for stylized censorship/pixelation that also distorts over time.

---

### 8.15 Pixel Manipulation — Ref Scatter px

| | |
|---|---|
| **Parameter** | `FrameMBParams::refScatter` |
| **Range** | 0 – 32 pixels |
| **Default** | 0 (no scatter) |
| **x264 mechanism** | For each pixel in the selected MBs, the reference frame sample position is offset by a random amount: `±rand() % refScatter` in both X and Y. So instead of reading from `(refX, refY)` in the reference frame, each pixel independently reads from `(refX ± rand, refY ± rand)`. Code: `applyRefScatter()`. |

**What it does:** Shatters the temporal reference into fragments. Instead of a smooth ghost echo,
each pixel in the MB samples from a slightly different position in the reference — creating a
"broken glass" or pixelation effect on the ghosted content.

**Creative use:**
- Small scatter (2–5px): adds a textural crawl to the ghost, like a CRT with poor sync.
- Large scatter (20–32px): completely disintegrates the reference into a pixel storm.
- Combined with high Ghost Blend: the scattered reference fully replaces current content →
  a chaotic mosaic of pixels sampled from random nearby positions in a past frame.
- Combined with Noise: the already-noisy reference gets scatter-sampled, creating double disorder.

---

## 9. Global Encode Parameters Panel

These controls affect the x264 encoder's fundamental decision-making across the *entire* encode
pass — not specific MBs, but the algorithms the encoder uses on every single frame.

They are passed as an x264-params string via `av_opt_set(encCtx->priv_data, "x264-params", ...)`.
This reaches directly into libx264's parameter structure before `avcodec_open2` is called.

All controls default to `-1` (meaning "use encoder default"). You only need to set values you
want to override.

**Important:** The global params panel applies on every "Apply" operation. If you have MB edits
AND global params set, both run in the same encode pass.

---

### 9.1 Kill I-Frames Checkbox

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::killIFrames` |
| **Type** | Boolean checkbox |
| **Default** | Off |
| **x264 mechanism** | At the start of the frame loop, for every frame except frame 0: `if (frame->pict_type == AV_PICTURE_TYPE_I \|\| (frame->flags & AV_FRAME_FLAG_KEY)) { frame->pict_type = AV_PICTURE_TYPE_P; frame->flags &= ~AV_FRAME_FLAG_KEY; }`. This runs *before* encoding, on the decoded frame. |
| **UI style** | Orange/red text label — it's visually prominent to warn you this is destructive. |

**What I-frames are:** I-frames (keyframes) are the "checkpoints" of video. Every GOP starts with
one. When the decoder needs to start playing from a random point, it seeks to the nearest I-frame
and decodes forward from there. Without I-frames, the decoder cannot recover from corruption.

**What Kill I-Frames does:** Forces every I-frame (except the very first frame in the file, which
must remain an IDR for stream validity) to be encoded as a P-frame instead. The decoder, reading
the bitstream, never encounters a clean keyframe — it must maintain its (corrupted) state from
frame 0 forward, with no chance to reset.

**Why this enables extreme datamoshing:** Once every I-frame is killed, the video becomes a single
unbroken chain of inter-predicted frames from beginning to end. Any corruption introduced early
in the file propagates forward indefinitely. There is no I-frame to "reset" the decoder and clean
up the mess.

**Note:** The frame *count* does not change. The timing of the video is preserved. Only the
frame *type* changes — I becomes P. This means the file may be somewhat larger than expected
because I-frame content must now be predicted from the previous frame rather than self-encoded.

**When visible:** On every frame after the first, the consequences cascade. If you combine Kill
I-Frames with any MB edits or temporal blend on early frames, that damage is never washed away
by a clean keyframe. The visible impact grows progressively further into the video.

---

### 9.2 Frame Structure — GOP Size

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::gopSize` |
| **Range** | -1 (default=250), 0 (infinite), or exact value 1–9999 |
| **x264 parameter** | `keyint=N` via x264-params string |

**What GOP (Group of Pictures) size is:** The maximum number of frames between two I-frames.
At `gopSize=250` (the default), an I-frame occurs at least every 250 frames (~8 seconds at 30fps).

**What changing it does:**
- `gopSize=0` (infinite): sets `keyint=9999`. The encoder almost never inserts automatic I-frames.
  With Kill I-Frames also active, the chain of P-frames is truly endless.
- `gopSize=1`: every frame is forced to be an I-frame. No inter-prediction. No datamosh.
  Maximum file size, no corruption propagation.
- `gopSize=4–8`: very short GOPs — corruption is reset frequently. Creates small isolated glitch
  pockets rather than sustained smears.
- `gopSize=250–9999`: long GOPs — corruption can propagate for seconds before an I-frame cleans it.

---

### 9.3 Frame Structure — B-Frames

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::bFrames` |
| **Range** | -1 (default=3), 0–16 |
| **x264 parameter** | `bframes=N` |

B-frames are bidirectionally predicted — from both past AND future reference frames.

- `bFrames=0`: no B-frames. All inter-frames are P-frames. Simplest prediction structure.
  Good for maximum datamosh: P-frames form a clean forward-prediction chain.
- `bFrames=8–16`: many B-frames. Bidirectional prediction creates more complex artifacts —
  content from the *future* bleeds backward. Creates time-reversal-like glitches.
- Default (3): moderate B-frame count. Standard compression trade-off.

---

### 9.4 Frame Structure — B-Adapt

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::bAdapt` |
| **Range** | -1 (default), 0=off, 1=fast, 2=trellis |
| **x264 parameter** | `b-adapt=N` |

Controls whether the encoder *automatically* decides which frames become B-frames based on scene
content analysis.

- `bAdapt=0` (off): disable adaptive B-frame placement. Combined with `bFrames=0`, ensures
  a pure P-frame structure.
- `bAdapt=2` (trellis): maximum analysis. The encoder picks the optimal B-frame positions.
  More complex prediction graph = more interesting bidirectional corruption.

---

### 9.5 Frame Structure — Ref Frames

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::refFrames` |
| **Range** | -1 (default=3), 1–16 |
| **x264 parameter** | `ref=N` |

The number of previously encoded frames the encoder keeps available as references for motion
prediction. A P-frame can reference any of the last N frames.

- `refFrames=1`: only the immediately previous frame. Simple forward chain.
- `refFrames=16`: maximum. The encoder can reference any of the last 16 frames.
  Creates complex long-range prediction dependencies. Corruption in frame N can influence frames
  up to N+16 that reference it, even without any explicit cascade.
- More reference frames = larger reconstruction buffer = slower encode, but richer temporal
  dependency graph for artifacts to travel through.

---

### 9.6 Rate Control — QP Override

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::qpOverride` |
| **Range** | -1 (use CRF 18), 0–51 |
| **x264 parameter** | Switches to CQP mode: `qp=N` |

Normally the encoder uses **CRF 18** (constant rate factor) — a quality-based mode where QP is
chosen automatically per-frame to maintain consistent visual quality. Setting QP Override switches
to **CQP** (constant quantizer) — every frame at every MB uses exactly this QP value.

- `qpOverride=18`: same average as CRF 18, but no per-frame adaptation. Quality may vary.
- `qpOverride=36–51`: aggressive quantization across the whole video. High blocking, small file,
  but maximizes residual suppression — content is defined entirely by inter-prediction.
- `qpOverride=0`: theoretically lossless (QP=0), very large file. Useful for preserving pixel
  edits exactly when you want to study the raw effect.

**Note:** When QP Override is set, the per-MB QP Delta still works as an *additional* offset on
top of the override value.

---

### 9.7 Rate Control — QP Min / Max

| | |
|---|---|
| **Parameters** | `GlobalEncodeParams::qpMin`, `GlobalEncodeParams::qpMax` |
| **Range** | -1 (unchanged), 0–51 each |
| **x264 parameters** | `qpmin=N`, `qpmax=N` |

Floor and ceiling on the quantizer in CRF mode.

- `qpMax=51`: the encoder is allowed to use maximum quantization on frames it deems low-quality.
  Combined with datamosh conditions, the encoder will often choose high QP for corrupted P-frames
  (because the prediction is bad = large residual = encoder wants to compress more), which then
  suppresses the residual and lets the corruption propagate further.
- `qpMin=28`: prevents the encoder from using very low QP even on frames it thinks are high-quality.
  Keeps a floor of compression that limits the cleanness of I-frames.

---

### 9.8 Motion Estimation — ME Method

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::meMethod` |
| **Range** | -1 (default), 0=dia, 1=hex, 2=umh, 3=esa, 4=tesa |
| **x264 parameter** | `me=dia/hex/umh/esa/tesa` |

The motion estimation algorithm controls how the encoder *searches* for the best matching block
in the reference frame.

| Method | Description | Effect on Artifacts |
|--------|-------------|---------------------|
| `dia` (diamond) | Searches a tiny cross pattern. Very fast. | Frequently finds wrong matches → bad MVs → more artifacts |
| `hex` (hexagonal) | Medium search. x264 default. | Balanced |
| `umh` (uneven multi-hex) | Thorough multi-scale search | Finds better matches → fewer prediction errors |
| `esa` (exhaustive) | Checks every candidate | Near-perfect predictions → minimal artifacts |
| `tesa` (transformed esa) | Best possible | Maximum quality → minimum interesting artifacts |

**For datamoshing:** `dia` is your friend. A limited diamond search often settles for a wrong
match, producing motion vectors that point nowhere sensible. This causes the encoder to drag
the wrong content across the frame and emit a large residual that (with high QP) gets suppressed.

---

### 9.9 Motion Estimation — ME Range

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::meRange` |
| **Range** | -1 (default=16), 4–512 px |
| **x264 parameter** | `merange=N` |

The radius in pixels within which the encoder searches for a matching block.

- **Small range (4–8px):** The encoder can only find matches from nearby positions. Content that
  moved more than 8 pixels is declared "unpredictable" → large residual → gets compressed heavily
  (or if QP is high, produces blocky artifacts).
- **Large range (128–512px):** The encoder can reference content from anywhere in the frame.
  Better motion handling but also enables *longer-range* artifact propagation when those long-range
  matches point to corrupted content.

---

### 9.10 Motion Estimation — Subpel Refinement

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::subpelRef` |
| **Range** | -1 (default=7), 0–10 |
| **x264 parameter** | `subme=N` |

H.264 supports motion vectors with sub-pixel precision (half-pixel and quarter-pixel). The
`subme` parameter controls how many refinement passes are spent finding the optimal fractional
motion vector.

- `subpelRef=0` (integer-pixel only): all MVs are whole-pixel values. No interpolation filtering.
  Prediction blocks have sharp staircase-like block boundaries. Low-precision MVs → more residual
  energy → more opportunities for quantization artifacts.
- `subpelRef=10`: maximum fractional MV precision. Near-perfect sub-pixel matching. Minimal residual.
  This actually *reduces* corruption potential since better predictions leave less energy for
  artifacts.
- `subpelRef=1–3`: some refinement but still imprecise. A useful middle ground that creates
  slightly jittery, step-like motion artifacts without completely preventing inter-prediction.

---

### 9.11 Partitions & DCT — Partition Mode

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::partitionMode` |
| **Range** | -1 (default), 0=16×16 only, 1=p8×8, 2=all, 3=all+4×4 |
| **x264 parameter** | Controls `partitions=` flag set |

**What partitions are:** Within a 16×16 MB, H.264 can split the prediction into smaller sub-blocks:
16×16, 16×8, 8×16, 8×8, 8×4, 4×8, 4×4. Smaller partitions track motion with more spatial detail
but require more bits to encode the extra motion vectors.

| Mode | Effect |
|------|--------|
| `16×16 only` | Every MB uses a single MV. Very crude spatial predictions. Large block-mosaic artifacts. |
| `p8×8` | Sub-divide into 8×8 blocks. Better prediction, smaller blocks. |
| `all` | Enable all standard partition sizes. |
| `all+4×4` | Enable 4×4 sub-partitions too. Maximum spatial precision. Very fine-grained. |

**For datamosh:** `16×16 only` maximizes block-mosaic visual character — the 16-pixel grid is
visually prominent in the output. Combined with no deblocking, you get the classic "big pixel"
datamosh look.

---

### 9.12 Partitions & DCT — 8×8 DCT

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::use8x8DCT` |
| **Default** | true (enabled) |
| **x264 parameter** | `8x8dct=1/0` |

Normally H.264 uses 4×4 DCT blocks for residual coding. Enabling 8×8 DCT allows larger
transform blocks which capture lower-frequency content more efficiently.

- **Enabled (8×8 DCT):** Better at capturing large smooth areas with fewer coefficients.
  Corruption tends to look "smeared" at medium-to-low frequency.
- **Disabled (4×4 DCT only):** Higher frequency representation. Artifacts appear more like
  fine checkerboard or horizontal/vertical stripes rather than large blobs.

---

### 9.13 B-Frame Prediction — Direct Mode

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::directMode` |
| **Range** | -1 (default), 0=none, 1=temporal, 2=spatial, 3=auto |
| **x264 parameter** | `direct=none/temporal/spatial/auto` |

For B-frames, the "direct" mode controls how the encoder derives motion vectors for skip MBs
(MBs that don't store explicit MVs).

- `temporal`: skip MB motion vectors are derived from co-located MB motion in the reference.
  Creates smooth time-consistent motion artifacts.
- `spatial`: skip MVs derived from neighboring MBs in the current frame. Creates more localized,
  spatially coherent artifacts.
- `none`: no direct MV derivation. Forces explicit MVs everywhere. More bits, more control,
  less implicit propagation.

For **TemporalBleed** effects, `temporal` direct mode creates the longest-range MV inheritance
chains across B-frames.

---

### 9.14 B-Frame Prediction — Weighted B / Weighted P

| | |
|---|---|
| **Parameters** | `GlobalEncodeParams::weightedPredB` (bool), `GlobalEncodeParams::weightedPredP` (int) |
| **x264 parameters** | `weightb=1/0`, `weightp=0/1/2` |

Weighted prediction allows the encoder to apply a gain and offset to reference samples before
prediction — effectively letting the encoder say "this frame comes from the reference, but 30%
brighter." This helps compress scenes with fade-in/fade-out.

- Weighted B enabled: B-frames can apply weights when blending their forward and backward
  references. Enables the encoder to model frame blending and dissolves correctly.
- Weighted P=2 (smart): the encoder analyzes the scene and applies optimal per-reference weights.
  In corrupted contexts, the encoder sometimes finds "optimal" weights for corrupted content and
  assigns dramatic brightness/contrast weights that amplify the artifact appearance.

---

### 9.15 Quantization Flags — Trellis

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::trellis` |
| **Range** | -1 (default=1), 0=off, 1=final-MB only, 2=all MBs |
| **x264 parameter** | `trellis=N` |

**What trellis quantization is:** After computing DCT coefficients, the encoder must decide which
coefficients to keep and which to round to zero. Trellis quantization uses a rate-distortion
optimization (essentially dynamic programming) to pick the *best* set of nonzero coefficients
to retain — minimizing visual error per bit spent. It's expensive but produces cleaner residuals.

- `trellis=0` (off): no RD-optimal coefficient selection. Simpler quantization. Residuals are
  noisier and less efficient. Combined with datamosh edits, the residuals look more chaotic.
- `trellis=2` (all MBs): maximum optimization. Clean, efficient residuals that accurately capture
  the inter-prediction error. This fights datamosh by correcting the pixel errors more precisely.
- For maximum artifact creation: turn trellis off. The encoder spends its bit budget less wisely
  and leaves more uncorrected prediction errors in the output.

---

### 9.16 Quantization Flags — No Fast P-Skip

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::noFastPSkip` |
| **Default** | false (fast skip enabled) |
| **x264 parameter** | `no-fast-pskip=1` |

By default, x264 uses a fast early-exit when evaluating P-frame MB modes: if the residual looks
small enough, it skips further analysis and emits a "skip MB" immediately. This saves encoding
time but means the encoder might emit skips for MBs that have non-trivial residuals.

- **Enabled (no fast skip):** The encoder evaluates every MB's residual fully before deciding
  whether to skip. More CPU, but more intentional skip decisions.
- **Disabled (default):** The encoder may skip MBs prematurely, which in corrupted content
  can mean propagating obviously wrong prediction content without even attempting to correct it.

For datamosh: leaving fast P-skip *enabled* (no-fast-pskip=false) means the encoder will sometimes
skip MBs with bad content without correcting them — maximizing artifact propagation.

---

### 9.17 Quantization Flags — No DCT Decimate

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::noDctDecimate` |
| **Default** | false (decimation enabled) |
| **x264 parameter** | `no-dct-decimate=1` |

By default, after quantization, if a DCT block has very few nonzero coefficients, x264 zeroes
them all out ("decimates" them) to save entropy coding overhead. This is a lossy but compact
decision.

- **Enabled (no decimate):** All nonzero DCT coefficients survive even if there are only 1–2 of them.
  More complete residual information is preserved.
- **Disabled (default):** Sparse blocks are fully zeroed. Near-zero residuals disappear. The prediction
  content dominates — good for letting corrupted inter-prediction propagate unimpeded.

For datamosh: with decimation enabled (default), small correction residuals are thrown away
automatically, further locking in the inter-prediction artifact. With decimation disabled, fine
residual detail is preserved — useful if you want more texture in the corruption artifacts.

---

### 9.18 Quantization Flags — Disable CABAC

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::cabacDisable` |
| **Default** | false (CABAC enabled) |
| **x264 parameter** | `cabac=0` |

H.264 supports two entropy coding modes: **CABAC** (Context-Adaptive Binary Arithmetic Coding)
and **CAVLC** (Context-Adaptive Variable Length Coding). CABAC is ~10–15% more efficient but
requires more complex decoding. CAVLC is simpler but produces slightly larger files.

Disabling CABAC:
- Switches to CAVLC entropy coding.
- Slightly different residual bit patterns.
- Subtle effect on how the decoder reconstructs MBs.
- Mainly useful for compatibility testing or achieving specific bitstream characteristics.

---

### 9.19 Deblocking — Disable Deblock

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::noDeblock` |
| **Default** | false (deblocking enabled) |
| **x264 parameter** | `deblock=0` or `no-deblock=1` |

**What the deblocking filter is:** After decoding, H.264 applies an in-loop deblocking filter that
smooths the edges between MBs. This removes the "blocky" appearance that quantization causes. The
deblocking filter runs *inside the codec*, meaning it affects the reconstructed reference frame too.

**What disabling it does:** When deblocking is off, every MB boundary remains sharp. The 16×16
grid becomes visually apparent. Block edges between MBs with different prediction histories create
hard discontinuities.

- **Disabled:** Hard block boundaries. The classic "pixel art" datamosh look. Each corrupted MB
  is visually distinct from its neighbors.
- **Enabled (default):** Smooth transitions at MB boundaries. Corruption is blended between adjacent
  MBs, creating softer smears but less distinct block structure.

**When visible:** Immediately apparent on every frame. In the player, disabled deblocking creates
a visible 16-pixel grid, especially noticeable on smooth gradients and skin tones.

---

### 9.20 Deblocking — Alpha / Beta Offsets

| | |
|---|---|
| **Parameters** | `GlobalEncodeParams::deblockAlpha`, `GlobalEncodeParams::deblockBeta` |
| **Range** | −6 to +6 each |
| **Default** | 0 (encoder-chosen threshold) |
| **x264 parameter** | `deblock=alpha:beta` |

If deblocking is enabled, these fine-tune how aggressively the filter smooths MB boundaries.

- **Alpha:** Controls the threshold for detecting block edges. Higher alpha → filter activates
  on more boundaries → more smoothing.
- **Beta:** Controls a detail-preservation threshold. Higher beta → filter is less aggressive on
  fine details.
- Positive values: more smoothing. Corruption artifacts blend into surrounding content.
- Negative values: less smoothing. Block structure is more visible even with filter on.

---

### 9.21 Psychovisual — Psy RD

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::psyRD` |
| **Range** | -1.0 (default=1.0), 0.0–5.0 |
| **x264 parameter** | `psy-rd=N:T` (N=RD strength, T=trellis psy strength) |

**What psychovisual rate-distortion is:** Standard R-D optimization minimizes the mathematical
error (PSNR). But human perception is not uniform — we're more sensitive to some types of
distortion than others. Psychovisual optimization adds a penalty for "visual flatness" — the
encoder intentionally adds high-frequency texture to areas that would otherwise become smooth
blobs, because the visual system perceives smooth-where-texture-should-be as more unpleasant
than preserving perceptible texture at slightly higher error.

- `psyRD=0.0`: disable psychovisual RD optimization. The encoder minimizes mathematical error.
  Corrupted flat areas stay flat. Clean areas are more mathematically accurate.
- `psyRD=1.0` (default): balanced perceptual quality. Some texture injection.
- `psyRD=3.0–5.0`: aggressive psy optimization. The encoder injects high-frequency "energy"
  into smooth areas. Combined with datamosh, this creates a swarming, oscillating texture in
  corrupted regions — the encoder fights the corruption with injected grain.

**For datamosh:**
- Turn psy off (`psyRD=0`) for smooth, clean-smearing datamosh without textural noise.
- Turn psy up (`psyRD=3–5`) for a grainier, more agitated corruption that vibrates.

---

### 9.22 Psychovisual — Psy Trellis

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::psyTrellis` |
| **Range** | -1.0 (default=0.0), 0.0–1.0 |
| **x264 parameter** | Second value in `psy-rd=N:T` |

Same perceptual philosophy as Psy RD but applied during trellis quantization. Higher values
increase the encoder's tendency to preserve coefficient patterns that match the statistics of
natural textures, even if they increase the mathematical error.

- `psyTrellis=0`: no psychovisual trellis. Pure rate-distortion trellis.
- `psyTrellis=0.5–1.0`: high tendency to preserve visual texture in residuals. In corrupted
  content, this can create interesting residual noise patterns.

---

### 9.23 Psychovisual — AQ Mode

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::aqMode` |
| **Range** | -1 (default=1), 0=off, 1=variance, 2=auto-variance, 3=auto-variance+edge |
| **x264 parameter** | `aq-mode=N` |

**What Adaptive Quantization is:** By default, flat areas and complex areas get the same QP.
But our visual system is more sensitive to artifacts in flat areas (a small error on a smooth
gradient stands out) than in textured areas (errors are masked by the texture). AQ redistributes
bits: use lower QP (more bits) for flat areas, higher QP (fewer bits) for complex areas.

| Mode | Effect on Artifacts |
|------|---------------------|
| `off` | Uniform QP. Flat areas and textured areas encoded with equal quality. |
| `variance` | Flat areas get more bits; complex areas get fewer. |
| `auto-variance` | Automatically calibrates the variance metric. |
| `auto-variance+edge` | Extra bit budget for edges. Edge-aware quality distribution. |

**For datamosh:** `aqMode=0` (off) is useful for maximum-impact flat-area corruption: without AQ,
flat areas that have been corrupted by prediction errors don't get compensating extra residuals.
High `aqStrength` with mode 3 creates chroma-bleeding effects around edges where the encoder
concentrates its error correction budget.

---

### 9.24 Psychovisual — AQ Strength

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::aqStrength` |
| **Range** | -1.0 (default=1.0), 0.0–3.0 |
| **x264 parameter** | `aq-strength=N` |

The magnitude of the AQ redistribution. Higher values create more extreme QP variation between
flat and complex areas.

- `aqStrength=0.0`: AQ mode is selected but has no actual effect.
- `aqStrength=2.0–3.0`: dramatic QP variation. Flat corrupted areas become much higher QP
  (stronger compression, more blocking) while complex areas get heavily protected. Creates strong
  visual contrast between clean edges and corrupted flat zones.

---

### 9.25 Psychovisual — Disable MB-Tree

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::mbTreeDisable` |
| **Default** | false (MB-tree enabled) |
| **x264 parameter** | `mbtree=0` |

**What MB-tree is:** A lookahead analysis that propagates quality decisions backward in time.
Before encoding starts, x264 simulates the next `rcLookahead` frames and determines which MBs
in the *current* frame will be referenced heavily by future frames. MBs that will be referenced
a lot get lower QP (more bits) now so that future frames can predict from them accurately.
It's a form of temporal quality pre-allocation.

**For datamosh:** MB-tree is actively harmful to maximum corruption. If a corrupted MB is
identified as a frequently-referenced anchor, MB-tree gives it better quality — which means
the corruption in that MB gets corrected to some degree, weakening the propagation chain.

- **Disabled:** Every MB is treated equally regardless of how many future frames reference it.
  Corrupted MBs receive no compensating quality boost. The corruption propagates at full strength.
- **Enabled (default):** The encoder "invests" bits in important MBs. Good for normal video quality;
  counterproductive for datamosh.

---

### 9.26 Lookahead

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::rcLookahead` |
| **Range** | -1 (default=40), 0–250 |
| **x264 parameter** | `rc-lookahead=N` |

The number of frames x264 analyzes in advance before making encoding decisions (B-frame placement,
MB-tree analysis, rate control). This is the temporal window for MB-tree propagation.

- `rcLookahead=0`: disable lookahead entirely. The encoder makes decisions frame-by-frame with no
  future context. Simpler, faster, and removes the influence of future frames on current frame quality.
  Best for maximum immediate impact of datamosh edits.
- Large values (100–250): the encoder considers a long future window. It may make more conservative
  decisions to protect future frame quality, potentially weakening corruption propagation.

---

### 9.27 Spatial Mask

| | |
|---|---|
| **Parameter** | `GlobalEncodeParams::spatialMaskMBs`, `GlobalEncodeParams::spatialMaskQP` |
| **UI controls** | Capture Mask button, QP spinbox (0–51), mask label showing current capture count |

**What it is:** A bridge between the MB painter and the global encode pass.
When you click "Capture Mask", the application reads the *current* MB selection from the canvas
and stores it as a spatial mask. On every subsequent Apply, that mask is stamped as a QP ROI on
*every frame in the video* — not just the frame you painted it on.

**The Capture Mask workflow:**
1. Navigate to any frame in the canvas.
2. Paint the region of interest.
3. Click "Capture Mask" — the label updates to show how many MBs are captured.
4. Set the Mask QP value (0–51).
5. Click Apply Global Params.

**What it does to the encode:** `applyQPROI()` is called inside the `encodeAndDrain` lambda for
every encoded frame with the captured MB indices and `spatialMaskQP`. The captured region
receives a constant QP offset on every frame — globally, throughout the entire video.

**Mask QP:**
- `51`: Maximum suppression. The spatial mask region has its residuals completely zeroed across
  the entire video. Only inter-prediction content survives there. Every frame, those MBs are
  predicted from their (possibly corrupted) state in the reference, with zero correction. The
  spatial mask defines a perpetual corruption zone.
- `0`: No effect (the ROI offset is zero).
- `-30` to `-10`: Increase quality in the masked region globally. Useful for protecting a
  "safe zone" while the rest of the frame corrupts.

**Example:** Paint the center third of the frame, set Mask QP=51, Apply. The center region of
every frame in the video now carries no corrective residuals — only motion-predicted content.
Any corruption that enters that region propagates indefinitely, while the borders may still have
I-frame resets cleaning them up.

---

## 10. Presets Reference

Presets load a complete `GlobalEncodeParams` configuration with one click. Select from the dropdown
and values populate immediately. You can then fine-tune before clicking Apply.

---

### Default
No overrides — all encoder parameters at their standard x264 defaults. Use this to reset the
global params panel to neutral.

---

### Infinite GOP
```
gopSize=0 (keyint=9999), bFrames=0, bAdapt=0, refFrames=4, killIFrames=true
```
Pure P-frame chain. No B-frames. No automatic keyframes. Kill I-Frames active. Creates the
minimum-complexity forward prediction chain — the classic datamosh baseline. Any content
corruption you introduce in early frames propagates for the entire duration of the video.

**Best combined with:** Heavy ghost blend on an early frame + transient length 30–60.

---

### Max Datamosh
```
gopSize=0, bFrames=0, bAdapt=0, refFrames=8, killIFrames=true,
noDeblock=true, noFastPSkip=true, noDctDecimate=true,
psyRD=0, psyTrellis=0, aqMode=0, mbTreeDisable=true,
qpMax=51, subpelRef=0, trellis=0, meMethod=dia
```
Every flag set to maximize corruption propagation and suppress correction pathways.
- No deblocking → block grid visible everywhere.
- No trellis, no decimate → crude residuals.
- No psy, no AQ, no MB-tree → no perceptual compensation.
- QP max 51 → encoder free to quantize to nothing.
- Diamond ME, no subpel → worst possible motion estimates.
- Kill I-frames + infinite GOP → no reset.

**Effect:** Aggressive, chaotic datamosh. First apply may appear near-normal; subsequent applies
compound rapidly. By the 3rd–5th apply cycle, original content becomes unrecognizable.

---

### Smear Heavy
```
gopSize=0, killIFrames=true, bFrames=0, refFrames=16, noDeblock=true,
subpelRef=1, trellis=0, meMethod=dia, meRange=64, mbTreeDisable=true
```
Optimized for smooth temporal smearing rather than block mosaic artifacts.
- 16 reference frames: the encoder has a very long memory. Content from 16 frames back can
  directly influence the current frame.
- Diamond ME with wide range: searches widely but imprecisely, creating large sweeping
  smears rather than small glitchy blocks.
- No deblock: smear boundaries stay hard (which paradoxically makes smears look more distinct
  against non-smeared content).

**Effect:** Painterly, watercolor-like smears that move with the motion in the video.
Objects leave long temporal streaks.

---

### Glitch Wave
```
meMethod=dia, meRange=128, partitionMode=all, psyRD=3.0, trellis=2,
noFastPSkip=true, subpelRef=4
```
Diamond ME with massive search range + very high psy optimization.
- Searching 128px with a diamond pattern: the encoder often finds wrong matches at extreme
  distances, creating wild warping effects.
- All partitions enabled: small sub-block prediction creates complex internal MB structure.
- High psy RD: the encoder aggressively injects texture energy into prediction errors.

**Effect:** Wave-like distortions and undulating texture patterns. Video appears to "breathe"
or vibrate. Psychovisual injection creates a shimmering noise layer over corrupted regions.

---

### Block Mosaic
```
partitionMode=16x16_only, subpelRef=0, trellis=0, noDeblock=true,
use8x8DCT=false, meMethod=dia, meRange=8, gopSize=0
```
Everything optimized to produce large, visible block artifacts.
- 16×16 only partitions: no sub-division. Each MB is one prediction unit.
- No subpel, small ME range: integer-pixel, short-range search. Very crude predictions.
- No deblock: every block boundary is a hard edge.
- No 8×8 DCT: residuals use 4×4 blocks only, showing higher-frequency blocking patterns.

**Effect:** A "big pixel" aesthetic — the video looks like it's rendering at 1/16 resolution
with a pixel art filter. Combined with any corruption edits, the blocks carry corrupted content
as large distinct colored tiles.

---

### Chroma Fever
```
aqMode=3 (auto-var+edge), aqStrength=3.0, psyTrellis=0, psyRD=1.5,
noDeblock=true, refFrames=8
```
AQ with extreme strength at edge-aware mode combined with high psy RD.
- AQ mode 3 aggressively shifts bits toward edges. With strength 3.0, edge MBs receive
  dramatically lower QP, non-edge areas dramatically higher QP.
- Combined with psy RD 1.5: texture injection on top of the AQ redistribution.
- With datamosh corruption in non-edge areas, those areas (high QP) have their corruption
  fully propagated and amplified. Edge areas stay relatively sharp.

**Effect:** Chroma saturation bleed and color fringing. Appears as vivid color halos, especially
around moving objects. The color information from reference frames bleeds along motion-detected
edges.

---

### Quantum Residue
```
noDctDecimate=true, noFastPSkip=true, trellis=2, use8x8DCT=true,
partitionMode=all+4x4, subpelRef=10, psyRD=0.5
```
Maximum residual fidelity — opposite of "suppress residuals."
- No decimate: every tiny coefficient is preserved.
- Full trellis: optimal coefficient selection.
- All+4x4 partitions: maximum spatial resolution.
- Max subpel: near-perfect sub-pixel motion.

**Effect:** Rather than propagating corruption through residual suppression, this mode creates
artifacts through *over-precise* encoding of corrupted content. The micro-level texture of
pixel edits is encoded with full fidelity — creating a sharp, high-frequency noise field
around corruption zones. Looks like quantum measurement error: everything is precisely wrong.

---

### Temporal Bleed
```
gopSize=0, bFrames=8, bAdapt=2, refFrames=16, weightedPredB=true,
weightedPredP=2, directMode=temporal, noDeblock=false
```
Maximum bidirectional prediction complexity.
- 8 B-frames with adaptive placement: the encoder constantly weaves forward and backward
  references.
- 16 reference frames: extremely long temporal dependency graph.
- Temporal direct mode: skip MB motion vectors derived from future-to-past temporal interpolation.
- Weighted B + smart weighted P: the encoder applies brightness weights to references.

**Effect:** Content from *both* the future and the past bleeds into the present simultaneously.
Frame N may contain content from frame N+8 and frame N-16 at the same time. Creates a dissolving,
time-overlapping aesthetic where multiple moments in the video coexist in the same frame.

---

### Data Corrupt
```
gopSize=0, qpMin=36, qpMax=51, noDeblock=true, noFastPSkip=true,
noDctDecimate=true, mbTreeDisable=true, psyRD=0, aqMode=0, meMethod=dia
```
Rate control forced into a high-quantization band.
- qpMin=36: the encoder is never allowed to use QP below 36. Every frame is at least medium-low
  quality regardless of scene complexity.
- qpMax=51: the encoder is free to go all the way to maximum compression.
- Combined with diamond ME and no optimizations: the encoder works with coarse predictions and
  compresses the error heavily.

**Effect:** Generalized compression artifact look across the whole video. Not targeted to specific
datamosh patterns but creates a "corrupted data" aesthetic — the entire video has visible
compression artifacts even without MB edits. Apply with MB edits to create regions of heavy
targeted corruption within a globally degraded visual field.

---

## 11. Compounding Iterations

**This is the core creative power of LaMoshPit.**

Every "Apply" operation:
1. Reads the current file (`m_currentVideoPath`) — which may already be damaged from previous applies.
2. Decodes frame-by-frame from the damaged file.
3. Applies all current MB edits and global params.
4. Re-encodes to a temp file.
5. Atomically replaces the current file with the temp file.
6. Reloads the player and timeline.

Your MB edits are preserved across applies (`m_edits` is not cleared by `reload()`). This means:

**Iteration 1:** Original file → damaged file #1. Corruption starts.
**Iteration 2:** Damaged file #1 → damaged file #2. Corruption compounds. The encoder is now
  predicting corrupted content from other corrupted content.
**Iteration N:** Each pass deepens the damage geometrically. What was a subtle smear becomes a
  full-frame temporal dissolution. What was a small color shift becomes a complete color swap.

**The encoder is predicting its own past mistakes.** Each iteration, the ring buffer fills with
increasingly corrupted reconstructed frames, and the next encode predicts from those. The error
compounds exponentially.

**Practical workflow for maximum damage:**
1. Start with Max Datamosh or Infinite GOP preset.
2. Paint a large MB region on a P-frame early in the video.
3. Set Ghost Blend to 80%, Ref Depth 2, Transient Length 30, Decay 20%.
4. Apply → preview. See first generation of corruption.
5. Increase Ghost Blend to 95%, keep same selection. Apply again.
6. Remove or reduce Ghost Blend, add MV Drift. Apply again.
7. Repeat with different parameters each pass.

**Tip:** Keep a backup of your original file. Once you've applied several passes,
there's no undo pathway — the original content may be unrecoverable.

---

## 12. Quick-Start Examples

### Classic Datamosh Smear
1. Load a video with action/motion.
2. Select the preset: **Infinite GOP**. Click Apply. (This establishes the P-frame chain.)
3. Navigate to frame 5–10. Paint the entire canvas.
4. Set Ghost Blend = 90%, Ref Depth = 2, Transient Length = 20, Decay = 30%.
5. Click Apply. Preview — you should see temporal smearing starting around frame 5.
6. Navigate to frame 15. Paint a region of interest. Set MV Drift X = 30.
7. Click Apply again. The drift compounds with the existing smear.

### Color Bleed / Chroma Aberration
1. Load video. Select preset: **Chroma Fever**.
2. Navigate to a frame with strong motion. Paint MBs along a moving edge.
3. Set Chr Drift X = 30, Chr Drift Y = 10, Color Twist U = 60.
4. Set Transient Length = 10, Decay = 50%.
5. Apply. Preview — look for color fringing following the motion.

### Block Mosaic Freeze
1. Load video. Select preset: **Block Mosaic**.
2. Navigate to a frame 2–3 seconds in. Paint a central region.
3. Set Ghost Blend = 100%, Ref Depth = 1, QP Δ = +51.
4. Set Transient Length = 0 (single frame only).
5. Apply. The center MBs "freeze" — they're replaced entirely by their previous-frame content
   and can't be corrected (QP=51). The frozen patch persists until the next I-frame (which,
   with Block Mosaic's infinite GOP, never comes).

### Temporal Ghost Trail
1. Load video. Select preset: **Smear Heavy**.
2. Navigate to a frame where a subject is in motion.
3. Paint the subject's body region.
4. Set Ghost Blend = 70%, Ref Depth = 5, Sample In (sampleRadius) = 4.
5. Set Transient Length = 40, Decay = 15%.
6. Apply. The subject leaves a 40-frame diffuse ghost trail, blurred over a 4-MB neighborhood,
   fading gently over the following ~40 frames.

### Quantum Noise Field
1. Load video. Select preset: **Quantum Residue**.
2. Navigate to any frame. Paint a textured region (not a flat background).
3. Set Noise Level = 80, Ref Scatter = 15, Block Flatten = 40%.
4. Set QP Δ = -15 (give these MBs extra quality so the noise is faithfully encoded).
5. Apply. The selected region becomes a high-resolution noise field. Because Quantum Residue
   preserves all coefficients, the noise pattern is encoded precisely — it looks intentional.
6. Apply again without changing settings — the noise compounds into a different pattern each time
   as the noisy content becomes the new reference.

---

*Documentation reflects the codebase state as of April 2026.*
*All parameter names match fields in `core/model/MBEditData.h` and `core/model/GlobalEncodeParams.h`.*
*Implementation details reference `core/transform/FrameTransformer.cpp`.*
