#include "QuickMoshWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

// =============================================================================
// Preset table — displayed name + one-line description.
// The index here must match the switch in MainWindow::onQuickMosh.
// =============================================================================
struct QuickPresetMeta {
    const char* name;
    const char* desc;
};

static const QuickPresetMeta kMeta[] = {
    {
        "P-Frame River",
        "Pure datamosh baseline \xe2\x80\x94 all frames become P-frames with no I-frame "
        "resets. The encoder\xe2\x80\x99s own temporal prediction chain does the work: "
        "no MB manipulation, maximum temporal smear from the first cut onward."
    },
    {
        "D\xc3\xa9j\xc3\xa0 Vu",
        "Deep temporal echo \xe2\x80\x94 every frame is ghost-blended toward content from "
        "3 frames back with a diffuse sample radius. Creates a layered double-exposure "
        "haze that cascades across the entire clip."
    },
    {
        "Block Cathedral",
        "Macroblock mosaic \xe2\x80\x94 16" "\xc3\x97" "16 partitions only, deblocking disabled, "
        "block-flatten stamps every frame. The encoder produces massive inter-block "
        "boundaries, turning the image into a stained-glass grid of prediction blocks."
    },
    {
        "Warp Smear",
        "MV liquify cascade \xe2\x80\x94 a strong diagonal drift seeds frame 1 at 3\xc3\x97 "
        "amplification; the corrupted prediction chain smears motion across the whole "
        "clip in a viscous fluid-warp style."
    },
    {
        "VHS Ghost",
        "Analog degradation \xe2\x80\x94 chroma planes drift independently from luma while a "
        "ghost blend echoes the previous frame. Cascades a warm, tape-worn colour "
        "bleed throughout the clip."
    },
    {
        "Corrupted Signal",
        "Digital disintegration \xe2\x80\x94 heavy noise injection, reference scatter, and "
        "forced high QP stamp every frame. Detail dissolves into a posterised, "
        "pixellated broadcast-error aesthetic."
    },
    {
        "Temporal Collapse",
        "B-frame saturation \xe2\x80\x94 8 B-frames with deep reference (16 frames) and "
        "temporal direct mode. Cascaded ghost blend stacks predictions across time, "
        "producing a complex multi-layered temporal bleed."
    },
    {
        "Welcome to LA - 10",
        "Classic datamosh pulse \xe2\x80\x94 seeds mosh bursts every ~45 frames. Subjects "
        "stay mostly recognisable between pulses; the effect ebbs back to near-normal "
        "before the next burst hits. Lowest intensity: interesting but controlled."
    },
    {
        "Welcome to LA - 20",
        "Stronger mosh pulse \xe2\x80\x94 bursts every ~35 frames with deeper ghost blend. "
        "The cascade lasts longer before fading, giving each pulse more screen time. "
        "Subjects still readable but noticeably dissolving at each hit."
    },
    {
        "Welcome to LA - 30",
        "Heavy pulse datamosh \xe2\x80\x94 bursts every ~25 frames with MV drift added. "
        "Motion vectors exaggerate movement; the encoder repeats macroblocks across "
        "neighbours. Subjects become abstract during bursts, recover between them."
    },
    {
        "Welcome to LA - 40",
        "Intense pulse cascade \xe2\x80\x94 bursts every ~18 frames, deeper reference, "
        "3\xc3\x97 MV amplification. The video barely recovers between pulses. Block "
        "echoes repeat across wide areas; subjects recognisable only at ebb moments."
    },
    {
        "Welcome to LA - NOT TO 50 YOU PSYCHO!!!!",
        "Maximum destruction \xe2\x80\x94 bursts every 10 frames, ghostBlend=88, refDepth=4, "
        "4\xc3\x97 MV amplification, QP corruption, noise injection, 16" "\xc3\x97" "16 block "
        "forcing. Video becomes a cascading macroblock avalanche. You were warned."
    },
    {
        "Block Wars",
        "Hard block freeze \xe2\x80\x94 strong horizontal MV displacement from 2 frames back "
        "seeds every ~30 frames; cascadeDecay=0 locks wrong pixels in place for 22 frames "
        "with no fade. 16" "\xc3\x97" "16 only, no deblock. Blocks smash through intermittently, "
        "freeze in wrong positions, then new motion punches through them."
    },
    {
        "Blockaderade",
        "Diagonal smash + internal scatter \xe2\x80\x94 seeds every ~25 frames from a 3-frame-back "
        "reference with refScatter=20 fragmenting each 16" "\xc3\x97" "16 block internally. Hard "
        "freeze, zero decay. Blocks appear as shattered pieces of distant frames randomly "
        "landing and locking until new motion breaks the deadlock."
    },
    {
        "COVID BLOCK DOWN",
        "Maximum block chaos \xe2\x80\x94 seeds every ~15 frames, 5\xc3\x97 MV amplification "
        "from 4 frames back, heavy internal scatter, QP=51 on seed. Encoder meRange=4 "
        "forces near-zero search accuracy so the encoder itself makes wrong predictions. "
        "Barely any recovery between freezes. You cannot leave the house."
    },
    {
        "Stepping on LEGGO",
        "Continuous horizontal block trail \xe2\x80\x94 every single frame is displaced from "
        "2 frames back at 3\xc3\x97 amplification. No cascade, no seeds: the block smear "
        "runs the entire clip uninterrupted. Blocks slide rightward like lego tiles on "
        "ice; subject motion leaves hard rectangular trails in its wake."
    },
    {
        "Ablockalypse Now",
        "One massive displacement seed every ~45 frames fires a 40-frame cascade that "
        "barely fades (decay=8). Once triggered the block trail runs almost to the next "
        "seed before easing off. Deep reference (3 frames back), strong horizontal "
        "smear. The video is a sustained lego-block river with brief moments of clarity."
    },
    {
        "Road to Prediction",
        "Rhythmic block stepping \xe2\x80\x94 seeds every 5 frames each fire a 4-frame "
        "hard-freeze cascade (decay=0). Blocks displace once, lock for 4 frames, "
        "displace again, lock again. The result is a discrete, jerky stepping motion "
        "where blocks advance in sudden jolts then hold their wrong position. No fade."
    },
    {
        "Why can't I do anything right?",
        "Diagonal block trails stamped on every frame at 5\xc3\x97 amplification \xe2\x80\x94 "
        "blocks are sourced from far off-axis reference positions producing extreme "
        "long-distance smears in both X and Y simultaneously. High QP forces coarse "
        "quantisation. Nothing lines up. Everything trails. Everything is wrong."
    },
    {
        "SHATTERED EGO",
        "Blast-radius block zones \xe2\x80\x94 each seed radiates 5 MBs outward via "
        "spillRadius, corrupting a large coordinated block group as one unit. "
        "The zone smears diagonally then hard-freezes for 17 frames with zero decay. "
        "Seeds every ~20 frames. Big areas of macroblocks move together then lock."
    },
    {
        "LEGGO MY EGO",
        "Deep compound corruption \xe2\x80\x94 every frame is displaced from content 5 frames "
        "back that has itself been corrupted 4 more times. Diagonal 4\xc3\x97 drift "
        "accumulates through the temporal prediction chain. High QP on every frame. "
        "The encoder references a past that no longer exists. Maximum block trail."
    },
    {
        "What is real?",
        "Rapid scatter \xe2\x80\x94 seeds every 4 frames, 3-frame hard freeze, wide meRange=16 "
        "so the encoder finds clean predictions in static areas and emits skip MBs there. "
        "Only motion areas accumulate wrong predictions. Small scattered blocks appear and "
        "disappear around moving subjects before the next seed jolts them again."
    },
    {
        "Mandella Effect",
        "3-frame seed interval, 2-frame freeze \xe2\x80\x94 blocks misremember their position on "
        "every cycle. The very short window means effects never fully lock; they flutter "
        "constantly around motion like a stutter in collective memory. Each slightly-wrong "
        "frame becomes the next reference, compounding subtly over time."
    },
    {
        "12 years Sober",
        "Upward block drift \xe2\x80\x94 seeds every 10 frames push blocks upward from a 2-frame-"
        "back reference at 3\xc3\x97 amplification, cascading hard for 8 frames. Vertical "
        "subject motion accumulates upward smear trails while horizontal-only areas skip "
        "cleanly. Long enough cascade to feel sustained before the next hit."
    },
    {
        "Halifax Explosion",
        "Violent block burst \xe2\x80\x94 seeds every 8 frames at 4\xc3\x97 amplification, 55px "
        "horizontal + 35px upward displacement; cascade holds only 5 frames then clears. "
        "Short. Explosive. Each event is a sudden block impact that disperses quickly, "
        "leaving the frame almost normal before the next detonation."
    },
    {
        "Rainbow Road",
        "Hue-shifted block trails \xe2\x80\x94 every frame stamped with horizontal block drift "
        "plus colorTwistU=+80 / colorTwistV=-80 on all motion-area blocks. Affected "
        "macroblocks shift toward magenta-red. Static areas skip cleanly and keep correct "
        "colour. No cascade: the hue corruption runs continuously through the whole clip."
    },
    {
        "Ego Death",
        "Cyan dissolution \xe2\x80\x94 seeds every 6 frames, 5-frame cascade, diagonal drift, "
        "colorTwistU=-100 / colorTwistV=+100 (strong cyan-green on seeds) plus "
        "chromaDriftX=50 which carries colour fringing through every cascade frame. "
        "Motion blocks take on a sickly cyan cast, smear, freeze, then slowly release."
    },
    {
        "K Hole",
        "Dissociative purple \xe2\x80\x94 every frame stamped from 3 frames back; "
        "colorTwistU=+70 / colorTwistV=+70 pushes motion blocks toward deep magenta. "
        "ChromaOffset=+20 lifts saturation and chromaDriftY=+30 vertically offsets "
        "colour planes from luma. Motion areas bloom into purple block clusters."
    },
    {
        "Hyaluronic Acid",
        "Chromatic fringing \xe2\x80\x94 seeds every 5 frames, 4-frame cascade; chromaDriftX=60 "
        "slides colour planes 60px horizontally relative to luma, leaving vivid colour "
        "halos at every block edge. ColorTwistU=-50 / colorTwistV=+80 warms the hue. "
        "Block smear trails leave rainbow-fringed residue around subject motion."
    },
    {
        "You belong in Prison",
        "Maximum chrominance punishment \xe2\x80\x94 every frame stamped with colorTwistU=+127 / "
        "colorTwistV=-127 (maximum possible hue distortion) plus chromaOffset=-60. "
        "Block smear trails run the entire clip in brutalised colour. No cascade needed. "
        "There is no parole. There is no early release. You did this."
    },
    {
        "Ohgee",
        "The balanced OG \xe2\x80\x94 seeds every 5 frames, 6-frame cascade; diagonal 3\xc3\x97 "
        "drift from 2 frames back, chromaDriftX=45 carries colour fringing through "
        "every cascade frame, colorTwistU=+60 / colorTwistV=-70 on seeds. Wide meRange "
        "keeps artifacts scattered at motion. All elements working together."
    },
    {
        "The Deep",
        "Maximum temporal lookback \xe2\x80\x94 every frame displaced from 7 frames back "
        "at 2\xc3\x97 amplification with wide meRange=24 concentrating prediction errors "
        "at motion. A slow-moving ocean of ancient blocks that barely resembles the present."
    },
    {
        "Undertow",
        "Sinusoidal horizontal pull \xe2\x80\x94 block drift oscillates across 3 full "
        "sine cycles through the clip. Seeds every 5 frames, 4-frame hard freeze. "
        "Blocks surge left, ease, surge right in a rhythmic tidal current."
    },
    {
        "Riptide",
        "Rapid alternating horizontal force \xe2\x80\x94 seeds every 3 frames flip "
        "direction each time: left-right-left at 3\xc3\x97 amplification. 2-frame "
        "hard freeze. A chaotic crosscurrent of blocks battling across the frame."
    },
    {
        "Kelp Forest",
        "Sinusoidal vertical sway \xe2\x80\x94 block drift oscillates vertically across "
        "4 sine cycles. Seeds every 7 frames, 5-frame hard freeze. Blocks drift "
        "up then down in slow undulating waves like underwater vegetation."
    },
    {
        "Tsunami",
        "Catastrophic single-direction flood \xe2\x80\x94 massive horizontal seeds every "
        "80 frames spawn 60-frame cascades at 5\xc3\x97 amplification. Each event "
        "is a wall of blocks that sweeps the entire frame before slowly subsiding."
    },
    {
        "Whirlpool",
        "Circular block rotation \xe2\x80\x94 drift traces 4 full circular paths "
        "via sin/cos at 60px amplitude. Seeds every 5 frames, 4-frame hard "
        "freeze. Blocks spiral in coordinated clockwise loops across the clip."
    },
    {
        "Bioluminescence",
        "Glowing block drift \xe2\x80\x94 every frame stamped with slow diagonal drift "
        "from 5 frames back, chromaDriftX=25 colour-plane offset, and "
        "colorTwistU=-70 / colorTwistV=+60. Motion blocks bloom in cyan-green."
    },
    {
        "The Abyss",
        "Extreme temporal depth \xe2\x80\x94 every frame draws from 7 frames back at "
        "2\xc3\x97 amplification. meRange=24 concentrates wrong predictions at "
        "motion only. A barely-moving wall of temporally ancient blocks."
    },
    {
        "Coral Reef",
        "Small circular scatter \xe2\x80\x94 drift traces 2 circular paths at 30px "
        "amplitude, seeds every 3 frames, 2-frame freeze. meRange=18 scatters "
        "effects naturally at motion. A diverse colony of small scattered blocks."
    },
    {
        "Monsoon",
        "Heavy vertical downpour \xe2\x80\x94 every frame displaced downward at "
        "3\xc3\x97 amplification from 2 frames back. mvDriftY=30 with minor "
        "horizontal drift. Blocks cascade downward like driving tropical rain."
    },
    {
        "The Gyre",
        "Growing spiral \xe2\x80\x94 rotation radius expands from 12px to 70px across "
        "6 full rotations. Seeds every 5 frames, 4-frame hard freeze. "
        "Blocks begin tight circular loops then fan out into wide spiralling arcs."
    },
    {
        "Sea Foam",
        "Light constant churn \xe2\x80\x94 seeds every 2 frames, 1-frame freeze at "
        "2\xc3\x97 amplification. meRange=24 naturally concentrates small block "
        "groups at motion only. A perpetual light froth of displaced pixels."
    },
    {
        "Rogue Wave",
        "Catastrophic rare impact \xe2\x80\x94 enormous horizontal seeds every 90 frames "
        "spawn 60-frame cascades at 5\xc3\x97 amplification. Each event is a "
        "singular wall of blocks that colonises the frame for a prolonged burst."
    },
    {
        "Gulf Stream",
        "Warm steady current \xe2\x80\x94 every frame displaced horizontally from 3 "
        "frames back at 2\xc3\x97 amplification. colorTwistU=+30 warms motion "
        "blocks with an orange cast. A continuous warm ocean current throughout."
    },
    {
        "Vortex Current",
        "Fast circular multi-rotation \xe2\x80\x94 8 full rotations of block drift "
        "via sin/cos at 45px amplitude. Seeds every 4 frames, 3-frame freeze. "
        "Blocks spin in rapid tight circles through the whole clip."
    },
    {
        "Dead Calm",
        "Sustained deep freeze \xe2\x80\x94 rare seeds every 60 frames each spawn a "
        "55-frame cascade with slow decay. meRange=20 concentrates effects at "
        "motion. The ocean barely moves, then shifts very slowly and for a long time."
    },
    {
        "Sonar Ping",
        "Rhythmic outward burst \xe2\x80\x94 seeds every 6 frames fire short 2-frame "
        "cascades at 3\xc3\x97 diagonal displacement. Narrow meRange=10 creates "
        "dense block clusters at each ping before clearing for the next pulse."
    },
    {
        "Coriolis",
        "Single slow planetary rotation \xe2\x80\x94 drift traces one complete circle "
        "across the clip. Seeds every 7 frames, 5-frame hard freeze, 50px "
        "amplitude. Blocks circle once in a slow geophysical arc."
    },
    {
        "The Hadal Zone",
        "Crushing deep-sea vertical compression \xe2\x80\x94 every frame stamped "
        "downward at 3\xc3\x97 from 7 frames back, qpDelta=35 forcing coarse "
        "blocks. meRange=8 forces wrong predictions everywhere. Maximum depth."
    },
    {
        "Poseidon",
        "Divine block chaos \xe2\x80\x94 5 circular rotations at 70px amplitude from "
        "4 frames back; chromaDriftX=40, colorTwistU=+80 / colorTwistV=-90 on "
        "seeds. 3-frame hard freeze. Spinning blocks in brutalised colour."
    },
    {
        "Block Trail Echoes 1",
        "Lightest mosaic trail \xe2\x80\x94 seed collapses each MB to a flat uniform "
        "colour (blockFlatten=80) and forces QP=51 so the encoder cannot add "
        "correction residual. The cascade hard-freezes those flat blocks for "
        "7 frames. Static areas freeze at their correct colour; motion areas "
        "freeze flat blocks at the subject\xe2\x80\x99s old position."
    },
    {
        "Block Trail Echoes 2",
        "Light mosaic trail \xe2\x80\x94 blockFlatten=90 stamps each MB closer to a "
        "uniform colour before QP=51 locks it in. 10-frame hard-freeze cascade. "
        "Background blocks pixelate at correct colours; moving subject leaves "
        "a trail of frozen flat rectangles at its previous position."
    },
    {
        "Block Trail Echoes 3",
        "Medium mosaic trail \xe2\x80\x94 blockFlatten=100 turns every seed-frame MB "
        "into a perfectly flat uniform colour. QP=51 prevents any correction. "
        "14-frame hard freeze. meRange=24 maximises encoder freedom so static "
        "flat blocks skip cleanly; motion zones leave hard pixelated rectangles."
    },
    {
        "Block Trail Echoes 4",
        "Heavy mosaic trail \xe2\x80\x94 full blockFlatten=100, QP=51 on seed. "
        "18-frame hard-freeze cascade. Seeds every 22 frames give 4 frames of "
        "clean recovery between events. Subject motion leaves thick frozen "
        "mosaic rectangles; static background pixelates at correct colours."
    },
    {
        "Block Trail Echoes 5",
        "Maximum mosaic trail \xe2\x80\x94 full blockFlatten=100, QP=51 on seed, "
        "23-frame hard-freeze cascade. Seeds every 27 frames. The longest "
        "sustained frozen flat-block trail of any preset. Hard, sticky, "
        "perfectly square mosaic rectangles clustered at subject motion only."
    },
    {
        "Nebula Drift",
        "Motion-isolated smear \xe2\x80\x94 seeds every 4 frames, 8-frame cascade "
        "with gentle decay. meRange=22 lets the encoder skip static areas cleanly; "
        "only motion zones accumulate trailing block residue. Soft and diffuse."
    },
    {
        "Solar Flare",
        "Periodic motion-burst \xe2\x80\x94 seeds every 15 frames, 10-frame cascade "
        "with moderate decay. Strong horizontal displacement seeds the flare; "
        "static areas recover between events while motion zones stay smeared."
    },
    {
        "Asteroid Belt",
        "Constant light scatter \xe2\x80\x94 seeds every 2 frames, 2-frame cascade, "
        "meRange=24 for maximum static-area skip. Tiny diagonal displacement at "
        "1\xc3\x97 amplification. Only moving subjects collect any visible smear."
    },
    {
        "Pulsar",
        "Rhythmic horizontal pulse \xe2\x80\x94 seeds every 8 frames, 5-frame cascade "
        "with high decay. Clear recovery between each pulse. Static areas skip "
        "cleanly; motion zones receive a brief block smear then fully restore."
    },
    {
        "Black Hole",
        "Slow circular motion-isolation \xe2\x80\x94 2 full rotations at 25px via "
        "sin/cos, seeds every 6 frames, 6-frame cascade. meRange=22 keeps block "
        "smear confined to subjects; background stays mostly intact."
    },
    {
        "Cosmic Ray",
        "Fast diagonal trace \xe2\x80\x94 seeds every 3 frames, 3-frame cascade with "
        "heavy decay. Diagonal 30/20px displacement at 2\xc3\x97 amplification. "
        "Smear appears as brief streaks on moving subjects before clearing."
    },
    {
        "Supernova",
        "Rare large outburst \xe2\x80\x94 seeds every 40 frames, 15-frame cascade "
        "with slow decay. 40/30px diagonal smear at 2\xc3\x97 amplification erupts "
        "and fades entirely by the next seed. Motion areas bear the full impact."
    },
    {
        "Dark Matter",
        "Invisible influence \xe2\x80\x94 seeds every 5 frames, 4-frame cascade with "
        "very high decay. meRange=24, 1\xc3\x97 amplification, 8px drift. Almost "
        "nothing visible in static areas; only motion zones reveal subtle smear."
    },
    {
        "Quasar",
        "Oscillating horizontal isolation \xe2\x80\x94 horizontal drift follows 2 sine "
        "cycles across the clip. Seeds every 5 frames, 6-frame cascade with "
        "moderate decay. Smear direction reverses mid-clip on motion subjects only."
    },
    {
        "Event Horizon",
        "Circular motion smear \xe2\x80\x94 3 full rotations at 35px amplitude via "
        "sin/cos. Seeds every 5 frames, 5-frame cascade. meRange=22 confines "
        "circular block drift to moving subjects; static zones skip cleanly."
    },
    {
        "Comet Trail",
        "Diagonal burst and fade \xe2\x80\x94 seeds every 12 frames, 8-frame cascade "
        "with slow decay; 40px right / 15px up at 2\xc3\x97 amplification. A "
        "comet-like trail smears from motion areas and fades before the next seed."
    },
    {
        "Stellar Wind",
        "Gentle steady drift \xe2\x80\x94 seeds every 7 frames, 6-frame cascade with "
        "moderate decay. 22px horizontal at 1\xc3\x97 amplification. A soft "
        "rightward current that smears only motion subjects, leaving sky alone."
    },
    {
        "Aurora Borealis",
        "Vertical sine + chroma wash \xe2\x80\x94 vertical drift follows 2 sine cycles; "
        "chromaDriftX=15 and colorTwistU=-40 / colorTwistV=+50 on seed frames. "
        "Seeds every 8 frames, 5-frame cascade. Colour fringe clings to motion."
    },
    {
        "Magnetar",
        "Infrequent strong isolation burst \xe2\x80\x94 seeds every 20 frames, 6-frame "
        "cascade. 50px/35px diagonal at 2\xc3\x97. meRange=24 keeps the strong "
        "displacement entirely confined to motion zones; static areas skip freely."
    },
    {
        "Wormhole",
        "Rapid alternating-direction isolation \xe2\x80\x94 seeds every 4 frames flip "
        "between +35px and -35px horizontal with 3-frame cascade and high decay. "
        "Battling smear directions cancel in static areas but linger at motion."
    },
    {
        "Plasma Stream",
        "Constant low-level diagonal stream \xe2\x80\x94 seeds every 3 frames, 4-frame "
        "cascade with high decay, 25/15px diagonal at 1\xc3\x97 amplification. "
        "A continuous gentle smear visible only where subjects are moving."
    },
    {
        "Gravity Well",
        "Converging spiral \xe2\x80\x94 circular rotation with radius shrinking from "
        "40px to 5px across the clip. Seeds every 5 frames, 5-frame cascade. "
        "Block smear winds inward on motion subjects as the clip progresses."
    },
    {
        "Cosmic Web",
        "Fine circular scatter \xe2\x80\x94 5 rapid rotations at 15px amplitude, seeds "
        "every 4 frames, 3-frame cascade, meRange=24. Tiny circular block motion "
        "appears as a delicate web of smear visible only at moving edges."
    },
    {
        "Stardust",
        "Barely-there motion trace \xe2\x80\x94 seeds every 2 frames, 2-frame cascade, "
        "cascadeDecay=35, 10/8px diagonal at 1\xc3\x97. meRange=24 maximum. "
        "Imperceptible in static zones; faint block haze clings to motion only."
    },
    {
        "Galaxy Collision",
        "Two-front smear \xe2\x80\x94 even seeds smear left on a sine envelope, odd seeds "
        "smear right on the same envelope. Seeds every 4 frames, 5-frame cascade. "
        "Opposing block currents collide and cancel except at moving subjects."
    },
};

static constexpr int kPresetCount = (int)(sizeof(kMeta) / sizeof(kMeta[0]));

// =============================================================================

QuickMoshWidget::QuickMoshWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // Header label
    auto* header = new QLabel("Quick Mosh", this);
    header->setStyleSheet(
        "font: bold 10pt 'Consolas'; color:#ff00ff; "
        "border-bottom: 1px solid #440044; padding-bottom:2px;");
    root->addWidget(header);

    // Combo + Mosh Now row
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    auto* comboLabel = new QLabel("Effect:", this);
    comboLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    topRow->addWidget(comboLabel);

    m_combo = new QComboBox(this);
    for (int i = 0; i < kPresetCount; ++i)
        m_combo->addItem(kMeta[i].name);
    m_combo->setStyleSheet(
        "QComboBox { background:#1a1a1a; color:#ff88ff; border:1px solid #663366; "
        "font:bold 9pt 'Consolas'; padding:2px 6px; min-width:160px; }"
        "QComboBox::drop-down { border:none; width:20px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#ff88ff; "
        "selection-background-color:#441144; font:9pt 'Consolas'; }");
    topRow->addWidget(m_combo, 1);

    m_btnMosh = new QPushButton("Mosh Now!", this);
    m_btnMosh->setFixedHeight(32);
    m_btnMosh->setMinimumWidth(110);
    m_btnMosh->setEnabled(false);   // enabled once a video is loaded
    m_btnMosh->setStyleSheet(
        "QPushButton { background:#330033; color:#ff00ff; "
        "border:2px solid #ff00ff; border-radius:4px; font:bold 11pt 'Consolas'; }"
        "QPushButton:hover { background:#550055; color:#ffffff; border-color:#ffffff; }"
        "QPushButton:pressed { background:#ff00ff; color:#000000; }"
        "QPushButton:disabled { background:#1a1a1a; color:#443344; border-color:#332233; }");
    topRow->addWidget(m_btnMosh);
    root->addLayout(topRow);

    // Description label — auto-updates when combo changes
    m_desc = new QLabel(kMeta[0].desc, this);
    m_desc->setWordWrap(true);
    m_desc->setStyleSheet("color:#888; font:8pt 'Consolas'; padding:2px 0;");
    m_desc->setMinimumHeight(32);
    root->addWidget(m_desc);

    // Wire signals
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < kPresetCount)
            m_desc->setText(kMeta[idx].desc);
    });

    connect(m_btnMosh, &QPushButton::clicked, this, [this]() {
        emit moshRequested(m_combo->currentIndex());
    });
}

void QuickMoshWidget::setMoshEnabled(bool enabled)
{
    m_btnMosh->setEnabled(enabled);
}

#include "QuickMoshWidget.moc"
