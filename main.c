#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
// Auto-close any previously running instance before opening the new window
// (prevents linker permission error or hanging window when pressing Run twice).
// Windows API calls are wrapped in their own namespace-free declaration block
// so they don't collide with raylib's Rectangle / CloseWindow / ShowCursor.
extern void* __stdcall CreateMutexA(void* lpMutexAttributes, int bInitialOwner, const char* lpName);
extern unsigned long __stdcall GetLastError(void);
extern void* __stdcall FindWindowA(const char* lpClassName, const char* lpWindowName);
extern int __stdcall PostMessageA(void* hWnd, unsigned int Msg, unsigned long long wParam, long long lParam);
extern unsigned long __stdcall GetWindowThreadProcessId(void* hWnd, unsigned long* lpdwProcessId);
extern void* __stdcall OpenProcess(unsigned long dwDesiredAccess, int bInheritHandle, unsigned long dwProcessId);
extern int __stdcall TerminateProcess(void* hProcess, unsigned int uExitCode);
extern int __stdcall CloseHandle(void* hObject);
extern void __stdcall Sleep(unsigned long dwMilliseconds);

static void ClosePreviousInstanceIfRunning(const char* windowTitle)
{
    void* prevWnd = FindWindowA(0, windowTitle);
    if (prevWnd) {
        unsigned long pid = 0;
        GetWindowThreadProcessId(prevWnd, &pid);
        PostMessageA(prevWnd, 0x0010 /* WM_CLOSE */, 0, 0);
        Sleep(80);
        if (pid != 0) {
            void* hProc = OpenProcess(0x0001 /* PROCESS_TERMINATE */, 0, pid);
            if (hProc) {
                TerminateProcess(hProc, 0);
                CloseHandle(hProc);
            }
        }
    }
}
#else
static void ClosePreviousInstanceIfRunning(const char* windowTitle) { (void)windowTitle; }
#endif

#define WINDOW_TITLE        "BallvBall"

// ======================= BALANCE CONSTANTS =======================
#define SCREEN_W            800
#define SCREEN_H            450

#define BASE_RADIUS         30
#define BASE_SPEED          4.35f
#define WALL_HOMING_BIAS    0.05f   // base 5% homing bias toward enemies on wall bounce
#define REFORGE_HOMING_BIAS (WALL_HOMING_BIAS * 1.08f) // +8% collision rate

#define BURST_BASE_DMG      7
#define BURST_DASH_DMG      17
#define BURST_COOLDOWN      4.5f
#define BURST_DURATION      0.7f
#define BURST_PREFIRE_TIME  0.5f    // fire aura appears 0.5s before dash
#define DASH_MULT           2.0f

// Harpoon: hook has 0 homing/aim bias (100% random angle + unpredictable wall bounces),
// hook visual & hitbox increased by +5% (2.25 * 1.05 = 2.3625x), pull speed reduced by 10% (1.32 * 0.9 = 1.188),
// deals 1 dmg immediately on hit then 1 dmg every 0.05s while pulling
#define HARPOON_SPEED       10.08f
#define HARPOON_PULL_STEP   (1.32f * 0.90f)
#define HARPOON_HEAD_SCALE  (2.25f * 1.05f)
#define HARPOON_HIT_BONUS   (14.0f * HARPOON_HEAD_SCALE)
#define HARPOON_PULL_TICK   0.05f
#define TRAIL_MAX           2000

#define TRIDENT_SPEED       14.0f
#define TRIDENT_DAMAGE      10
#define TRIDENT_COOLDOWN    3.0f
#define TRIDENT_LAND_TIME   1.0f
#define TRIDENT_RANGE       ((SCREEN_W / 4.0f) * 0.95f)

#define ICE_MAX             200
#define ICE_TRAIL_TIME      3.0f
#define ICE_HALF_L          18.0f

#define SHADOW_N            10

#define LINK_SLOW           0.5f
#define BOOST_TIME          0.25f
#define BOOST_MULT          2.5f

#define ELEC_CHARGE_TIME    4.5f
#define SHOCK_TICKS         5
#define SHOCK_TICK_TIME     0.4f
#define SHOCK_DMG           3

// Witch: 1 potion every 2s, non-heal max range = 1/4 window width, +10% miss scatter, 0.7s freeze
#define WITCH_INTERVAL      2.0f
#define POTION_SPEED        9.0f
#define WITCH_MAX_RANGE     (SCREEN_W / 4.0f)
#define ZONE_TIME           2.5f
#define WITCH_FREEZE_TIME   0.7f

// Bow: shoots white arrow (length = ball diameter = 60) every 5s, accuracy +5% at all distances
// (97% close -> 45% at max range = 3/4 screen width), deals 25 dmg, disappears on wall or enemy hit
#define BOW_COOLDOWN        5.0f
#define BOW_ARROW_SPEED     13.5f
#define BOW_ARROW_DMG       25
#define BOW_ARROW_LEN       (BASE_RADIUS * 2.0f)
#define BOW_ARROW_RANGE     (SCREEN_W * 0.75f)

// Reforge: main ball (90% size, 0 dmg) + companion ball (75% size, Lv1 = 5 dmg, +30% dmg per level)
#define REFORGE_MAIN_RAD    ((int)(BASE_RADIUS * 0.90f))
#define REFORGE_SUB_RAD     ((int)(BASE_RADIUS * 0.75f))
#define REFORGE_BASE_DMG    5.0f
#define REFORGE_LVL_MULT    1.30f

// Ninja: throws kunai sequentially; waits 0.5s then reloads over 2.5s (3.0s total);
// max flight range = 40% window width (320px), accuracy = 80% at max range, 3..8 kunai from 100->35 HP
#define NINJA_WAIT_TIME     0.5f
#define NINJA_RELOAD_WINDOW 2.5f
#define NINJA_THROW_GAP     0.12f
#define KUNAI_SPEED         13.0f
#define KUNAI_RANGE         (SCREEN_W * 0.40f)
#define KUNAI_DMG           3
#define KUNAI_MAX           96

// Cell: State 1 (65% size, 15 HP, 5 dmg) -> grows to State 2 (100% size) after 4.5s -> splits into 3 on death
#define CELL_HP             15
#define CELL_DMG            5
#define CELL_GROW_TIME      4.5f
#define CELL_SMALL_RAD      ((int)(BASE_RADIUS * 0.65f))

// Laser: 0 homing bias toward enemies (slight 10% wall-seeking bias), spawns axis-aligned wall turret on bounce,
// 5 dmg every 2.0s
#define TURRET_MAX          48
#define TURRET_INTERVAL     2.0f
#define TURRET_BEAM_TIME    0.22f
#define TURRET_LASER_DMG    5
#define LASER_WALL_BIAS     0.10f

// Cactus: 0 contact dmg, starts with 3 ammo (max 3), 2.0s per ammo recharge, 0.15s min delay for burst fire;
// throws 75%-radius cactus bomb (max range = 1/4 window width = 200px, 0 contact dmg, -10% speed = 8.55);
// explodes on enemy/wall/max-range into 8 red spikes (3 dmg each, disappear on wall hit)
#define CACTUS_MAX_AMMO     3
#define CACTUS_RELOAD_TIME  2.0f
#define CACTUS_BURST_DELAY  0.15f
#define CACTUS_BOMB_RAD     ((int)(BASE_RADIUS * 0.75f)) // 50% + 25% of a normal ball = 75% (22px)
#define CACTUS_BOMB_SPEED   8.55f                        // -10% bomb speed (9.5 * 0.9)
#define CACTUS_BOMB_RANGE   (SCREEN_W / 4.0f)
#define CACTUS_SPIKE_DMG    3
#define CACTUS_SPIKE_RANGE  (BASE_RADIUS * 2.0f)
#define CACTUS_CURVE_RANGE  (BASE_RADIUS * 3.0f)
#define CACTUS_BOMB_MAX     16
#define CACTUS_SPIKE_MAX    128

// Wind: 0 contact dmg, first tornado after 3.0s, then every 2.5s;
// tornado pierces and stuns ALL enemies along its flight path (12 dmg + 1.5s stun), disappears on wall hit
#define WIND_FIRST_DELAY    3.0f
#define WIND_INTERVAL       2.5f
#define TORNADO_SPEED       8.5f
#define TORNADO_RADIUS      (18.0f * 1.10f)            // +10% size (19.8px)
#define TORNADO_RANGE       (SCREEN_W * 0.40f)         // max flight distance = 40% window width (320px)
#define TORNADO_DMG         12
#define WIND_STUN_TIME      1.5f
#define TORNADO_MAX         24

// Chess: 0 normal contact dmg; every 3.0s (not counting 2.75s skill duration) turns arena into a subtle
// checkerboard for 2.75s, anchors briefly at start pos, and dashes with 25% chance each as:
// Queen (8 dirs), Rook (4 ortho dirs), Bishop (4 diag dirs), or Knight (8 L-shapes)
#define CHESS_COOLDOWN      3.0f
#define CHESS_DURATION      2.75f
#define CHESS_PAUSE_TIME    0.12f
#define CHESS_SKILL_DMG     15
#define CHESS_DIR_RANGE     (SCREEN_W / 2.0f)
#define CHESS_TRAIL_MAX     240

// Gun: white ball with a 6-chamber rotating cylinder in the center and a 6-barrel gatling barrel protruding outside;
// Phase 0 (Normal):   fires 1 bullet every 0.15s for 2.25s (hit rate drops with distance to 55% at max range);
// Phase 1 (Pre-wait): waits 0.5s after normal fire before starting rapid fire;
// Phase 2 (Rapid):    fires 1 bullet every 0.05s for 1.25s (barrel heats up from pale to deep yellow-orange);
// Phase 3 (Cooldown): overheated cooldown for 2.5s (does not shoot while barrel cools back to dark gray-black);
// Bullets have radius = 15% of normal ball (4.5px), max flight range = 45% window width (360px),
// and disappear on hitting a wall or enemy.
#define GUN_NORM_DURATION     2.25f
#define GUN_NORM_INTERVAL     0.15f
#define GUN_PRE_RAPID_WAIT    0.50f
#define GUN_RAPID_DURATION    1.25f
#define GUN_RAPID_INTERVAL    0.05f
#define GUN_COOLDOWN_DURATION 2.50f
#define GUN_BULLET_RAD        (BASE_RADIUS * 0.15f)
#define GUN_BULLET_SPEED      12.5f
#define GUN_BULLET_RANGE      (SCREEN_W * 0.45f)         // 45% window width (360px)
#define GUN_BULLET_DMG        1
#define GUN_NORM_MIN_HIT      55.0f                      // normal fire accuracy at max range
#define GUN_RAPID_MIN_HIT     40.0f                      // rapid fire accuracy at max range
#define GUN_BULLET_MAX        160

// Dice: white/black/red ball with a 3D die in the center, 0 contact dmg;
// every 2.0s rolls a die (1.0s prominent full-screen animation rotating 3 times then revealing 1..6 pips);
// on reveal: all enemies are anchored & invulnerable to external damage for 0.25s, red screen border flashes,
// and enemies take pip damage: 1->5, 2->7, 3->9, 4->12, 5->15, 6->18 HP
#define DICE_COOLDOWN       2.0f
#define DICE_ANIM_TIME      1.0f
#define DICE_LOCK_TIME      0.25f

// Gambler: pink ball with gold ring, gold necklace, and sly eyes; 5 base contact dmg, 5% initial homing bias;
// rolls 3 dice sequentially (1.5s prominent animation) + draws 1 card (2..10, J, Q, K, A, 0.75s showcase),
// then gains +5% homing bias and +5% movement speed after each cast and waits until colliding with an enemy:
// - Next offensive hit deals (sum of 3 dice) * 3 damage
// - Next incoming hit mitigation from drawn card:
//   * 2..10: reduces incoming damage by 10%..50% linearly
//   * J, Q, K: blocks 100% of incoming damage
//   * Ace: blocks 100% of incoming damage and deals (33% of enemy's contact damage + sum of 3 dice * 3) to the enemy
#define GAMBLER_BASE_DMG    5
#define GAMBLER_HOMING_STEP 0.05f
#define GAMBLER_SPEED_STEP  0.05f
#define GAMBLER_COOLDOWN    3.0f
#define GAMBLER_DICE_TIME   1.5f
#define GAMBLER_CARD_TIME   0.75f

// Psychic: wears a pink Tara-style mystic scarf with black folds and a scarred third eye, 0 contact dmg;
// throws 3 tarot cards simultaneously every 3.0s (slow speed, moderate accuracy, unlimited range, 10 dmg each,
// pierces teammates and disappears on wall or enemy hit);
// 7.0s after the first tarot throw, casts a Black Hole for 2.0s (diameter = 33% of window width)
// that pulls all enemies to its center (0.75s pull time from outer rim); during the 2.0s Black Hole,
// Psychic's card throw cooldown is reset and reduced to 2/3s (0.667s)
#define PSYCHIC_NORM_CD     3.0f
#define PSYCHIC_FAST_CD     (2.0f / 3.0f)
#define PSYCHIC_BH_DELAY    7.0f
#define PSYCHIC_BH_TIME     2.0f
#define PSYCHIC_BH_RADIUS   ((SCREEN_W * 0.33f) * 0.5f)
#define PSYCHIC_BH_PULL_T   0.75f
#define TAROT_SPEED         5.8f
#define TAROT_DMG           10
#define TAROT_MAX           64
#define BLACKHOLE_MAX       8

// Charge (Common): multi-shade blue ball, 10 base contact damage;
// every 3.0s grows +10% size and +30% contact damage; resets to base size & damage upon hitting an enemy
#define CHARGE_BASE_DMG     10
#define CHARGE_INTERVAL     3.0f
#define CHARGE_RAD_STEP     0.10f
#define CHARGE_DMG_STEP     0.30f
#define CHARGE_MAX_STACKS   8

// Shackless (Legendary): blue/purple/black multi-shade ball with 2 golden chains (Sylas style), 0 contact dmg;
// every 5.0s has 1/3 chance to cast one of 3 ultimates:
// 1. Leap Slam (0.50s jump delay toward remembered nearest enemy within 3x radius, slams 1.875x radius black fissure + brown earth rim for 20 HP,
//    holds Shackless & hit enemies in place for 0.35s then fades in 0.10s)
// 2. Final Spark Rainbow Laser (0.95s charge toward remembered farthest enemy, infinite range, width = 1.5x diameter [+25%], 25 HP)
// 3. Demacian Justice Sword (fast 0.35s drop, 100% locks onto target enemy within 1.4x radius, +20% visual size + 2 tilted translucent side swords,
//    golden ground crack, executes enemies at <= 20 HP, else deals 7 HP to target and 3 HP to nearby enemies)
#define UNSHACKLED_CD               5.0f
#define UNSHACKLED_LEAP_T           0.50f
#define UNSHACKLED_LEAP_RNG         (BASE_RADIUS * 3.0f)
#define UNSHACKLED_SLAM_RAD         (BASE_RADIUS * 1.5f * 1.25f) // +25% size (1.875x radius)
#define UNSHACKLED_SLAM_DMG         20
#define UNSHACKLED_SLAM_HOLD_T      0.35f
#define UNSHACKLED_SLAM_FADE_T      0.10f
#define UNSHACKLED_LUX_T            0.95f
#define UNSHACKLED_LUX_W            (BASE_RADIUS * 2.0f * 1.2f * 1.25f) // +25% width
#define UNSHACKLED_LUX_DMG          25
#define UNSHACKLED_SWORD_T          0.35f                        // faster sword fall speed
#define UNSHACKLED_SWORD_RNG        (BASE_RADIUS * 1.4f)
#define UNSHACKLED_EXEC_HP          20                           // executes enemies with <= 20 HP
#define UNSHACKLED_SWORD_TARGET_DMG 7                            // 7 dmg to primary target
#define UNSHACKLED_SWORD_AOE_DMG    3                            // 3 dmg to surrounding enemies
#define UNSHACKLED_MAX_STACKS       3                            // max 3 chain-lash stacks
#define UNSHACKLED_STACK_TIME       3.0f                         // 3.0s stack duration (resets on skill hit or chain lash)
#define UNSHACKLED_LASH_DMG         10                           // 10 HP per chain lash on contact
#define UNSHACKLED_LASH_DELAY       0.25f                        // 0.25s delay between chain lashes

// Judgement (Legendary): gold/white/gray multi-shade justice ball with angel wings & golden halo, 0 normal contact dmg;
// every 3.0s stops in place, remembers nearest enemy, charges for 0.75s (50% dmg reduction, outer gold & white aura + Kizaru 4-point star),
// and if the target enemy is still within 35% window width (280px) at the end of the 0.75s charge, Judgement is 100% guaranteed to ram & hit them
// (if the enemy escaped outside the 35% window-width radius during the 0.75s charge, the ram misses);
// Judgement is 100% immune to damage while ramming and for 0.50s after the ram finishes
#define JUDGEMENT_CD            3.0f
#define JUDGEMENT_CHARGE_T      0.75f
#define JUDGEMENT_SHRINK_T      0.30f
#define JUDGEMENT_POST_IMMUNE_T 0.50f
#define JUDGEMENT_MAX_GROW      0.25f
#define JUDGEMENT_RAM_SPEED     17.5f
#define JUDGEMENT_RAM_RANGE     (SCREEN_W * 0.35f)
#define JUDGEMENT_RAM_DMG       20

// Train (Uncommon): black/dark-blue/wood/iron ball, 0 contact dmg;
// lays railway tracks (width = ball diameter = 60px) while moving for 4.0s, waits 1.0s,
// then a train of up to 5 cars connected by short chains rushes along the tracks dealing 15 HP on hit (0.5s per-victim immunity),
// then waits 2.0s before repeating the 4.0s track-laying cycle
#define TRAIN_LAY_TIME      4.0f
#define TRAIN_PRE_WAIT      1.0f
#define TRAIN_POST_WAIT     2.0f
#define TRAIN_LOCO_SPEED    18.0f
#define TRAIN_TRACK_W       (BASE_RADIUS * 2.0f)
#define TRAIN_HIT_DMG       15
#define TRAIN_IMMUNE_TIME   0.5f
#define TRAIN_TRACK_MAX     320
#define TRAIN_MAX_CARS      5
#define TRAIN_CAR_SPACING   52.0f

// Web (Common): red ball with white spiderweb in the center, 0 contact dmg,
// 0 homing bias toward enemies (+10% wall-seeking bias);
// on odd wall hits (1st, 3rd, 5th...) remembers the wall point; on the next (even) wall hit
// spins a white web line between the remembered point and the current wall point;
// any enemy crossing a web line takes 1 HP damage (can trigger repeatedly each crossing)
#define SPIDER_WALL_BIAS    0.10f
#define SPIDER_WEB_DMG      1
#define SPIDER_WEB_MAX      64

// Range (Uncommon): yellow ball, 0 contact dmg;
// starts with an outer ring of radius = 1.1x ball radius (33px);
// every 2.0s the ring radius grows 10% faster each cycle with NO upper limit;
// any enemy inside the outer ring takes 2 HP every 0.5s
#define RANGE_INIT_MULT     1.10f
#define RANGE_GROW_INTERVAL 2.0f
#define RANGE_GROW_STEP     0.20f
#define RANGE_GROW_ACCEL    1.10f   // +10% faster ring growth per cycle
#define RANGE_TICK_TIME     0.5f
#define RANGE_TICK_DMG      2

// Knight (Rare - Medieval): carries sword & shield, wears red/blue/gold cape over gray/white armor, 0 direct contact dmg;
// on contact: reduces incoming damage by 5%..40%, and after a 0.10s delay sweeps a 30-degree cone slash (reach = 85% ball size)
// toward the target dealing 12 HP
#define KNIGHT_SLASH_DELAY  0.10f
#define KNIGHT_CONE_DEG     30.0f
#define KNIGHT_CONE_REACH   (BASE_RADIUS * 2.0f * 0.85f)
#define KNIGHT_SLASH_DMG    12

// Cavalry (Rare - Medieval): brown warhorse in iron armor carrying a long iron lance;
// normal contact thrusts lance with 0 delay (100% hit) for 12 HP;
// every 4.0s enters Charge state: accelerates up to 1.6x normal speed for up to 50% window width (400px),
// scaling lance damage from 10 to 25 HP based on current speed;
// if colliding with an enemy or wall during Charge, sparks fly and both are immobilized for 1.25s before starting the 4.0s cooldown
#define CAVALRY_BASE_DMG    12
#define CAVALRY_CD          4.0f
#define CAVALRY_MAX_MULT    1.60f
#define CAVALRY_MAX_DIST    (SCREEN_W * 0.50f)
#define CAVALRY_MIN_CHG_DMG 10
#define CAVALRY_MAX_CHG_DMG 25
#define CAVALRY_STUN_T      1.25f

// Executer (Common - Medieval): wears an iron executioner hood & carries a heavy long-handled axe;
// on contact: raises heavy axe and chops down for 10 HP (with red pain flash on victim), 2.25s cooldown (0 dmg while on cooldown);
// every 5.0s captures all enemies within 2.5x ball radius (immobilized/stunned & immune to other contact dmg) and charges for 3.0s,
// forming a rounded-corner red gradient trapezoid (short base = ball diameter [not drawn], length = 1.2x short base, bright red border,
// inner red darkening from Executer toward the outer long edge, Sion-Q style) and slams for 20 HP
#define EXECUTER_CHOP_DMG   10
#define EXECUTER_CHOP_CD    2.25f
#define EXECUTER_SKILL_CD   5.0f
#define EXECUTER_CHARGE_T   3.0f
#define EXECUTER_CAPTURE_R  (BASE_RADIUS * 2.5f)
#define EXECUTER_TRAP_SHORT (BASE_RADIUS * 2.0f * 0.45f)
#define EXECUTER_TRAP_LEN   (BASE_RADIUS * 2.0f * 1.2f * 1.60f)
#define EXECUTER_TRAP_LONG  (EXECUTER_TRAP_SHORT * 1.85f)
#define EXECUTER_SLAM_DMG   20

// Squire (Common - Medieval): sand yellow & skin tone body, bright red cross in the center,
// gray armor rim near the outer edge, carries a medieval sword;
// deals 10 HP on each collision and takes 15% reduced incoming damage
#define SQUIRE_BASE_DMG     10
#define SQUIRE_DMG_RED      0.15f

// King (Mythic - Medieval): wears a golden crown with triangular peaks, white center cross, and red velvet lining,
// carrying a blue triangular flag with gold stripes (Jarvan IV style);
// every 2.5s plants the flag at 30% window width from King (dealing 10 HP in 1.25x ball radius around the flag),
// then after 0.25s dashes rapidly to the flag (dealing 15 HP and 0.4s stun to all enemies along the path,
// while completely immune to all damage and crowd control during the dash)
#define KING_CD             2.50f
#define KING_FLAG_DIST      (SCREEN_W * 0.30f)
#define KING_FLAG_RAD       (BASE_RADIUS * 1.25f)
#define KING_FLAG_DMG       10
#define KING_DASH_DELAY     0.25f
#define KING_DASH_SPEED     22.0f
#define KING_DASH_DMG       15
#define KING_STUN_T         0.40f

// Catapult (Mythic - Medieval V2): bright wood brown & bright stone gray siege catapult;
// every 10.0s hurls 1 stone boulder in a high parabolic arc (rises into the sky and falls down,
// dealing 0 damage along the flight path and passing through all objects/walls),
// landing at the farthest enemy's position with a 75% hit rate at all distances,
// dealing 35 HP to all enemies standing within a 3.5x ball radius AoE
#define CATAPULT_CD         10.0f
#define CATAPULT_FLIGHT_T   1.15f
#define CATAPULT_AOE_RAD    (BASE_RADIUS * 3.5f)
#define CATAPULT_DMG        35
#define CATAPULT_HIT_PCT    75
#define CATAPULT_ARC_HEIGHT 165.0f

// Archer (Rare - Medieval V2): forest green & steel gray archer;
// every 7.5s shoots a fire-tipped arrow at the nearest enemy (max range = 55% window width, accuracy drops to 30% at max range):
// - Direct hit deals 10 HP and guarantees Burn (thiêu đốt)
// - Creates a Burning Ground Zone of radius 1.8x ball radius (any enemy touching the zone is afflicted with Burn regardless of duration)
// - Burn deals 3 HP every 0.75s for 3 ticks (total 9 HP)
#define ARCHER_CD           7.50f
#define ARCHER_ARROW_SPEED  13.0f
#define ARCHER_ARROW_LEN    42.0f
#define ARCHER_MAX_RANGE    (SCREEN_W * 0.55f)
#define ARCHER_MIN_HIT_PCT  30.0f
#define ARCHER_DIRECT_DMG   10
#define ARCHER_ZONE_RAD     (BASE_RADIUS * 1.8f * 0.90f)
#define ARCHER_ZONE_TIME    1.00f
#define ARCHER_BURN_TICK_T  0.75f
#define ARCHER_BURN_TICKS   3
#define ARCHER_BURN_DMG     3
#define BURN_ZONE_MAX       16

// Colors
#define COL_DEVIL           (Color){ 120, 0, 10, 255 }
#define COL_ICE             (Color){ 0, 40, 150, 255 }
#define COL_TRIDENT         (Color){ 0, 160, 60, 255 }
#define COL_ELECTRIC        (Color){ 255, 225, 40, 255 }
#define COL_WITCH           (Color){ 140, 60, 190, 255 }
#define COL_BOW             (Color){ 205, 105, 35, 255 }
#define COL_REFORGE         (Color){ 55, 58, 64, 255 }
#define COL_REFORGE_SUB     (Color){ 105, 62, 28, 255 }
#define COL_NINJA           (Color){ 25, 85, 145, 255 }
#define COL_CELL            (Color){ 135, 235, 130, 255 }
#define COL_LASER           (Color){ 250, 25, 35, 255 }
#define COL_CACTUS          (Color){ 32, 130, 52, 255 }
#define COL_WIND            (Color){ 155, 165, 175, 255 }
#define COL_CHESS           (Color){ 235, 235, 235, 255 }
#define COL_GUN             (Color){ 248, 248, 252, 255 }
#define COL_DICE            (Color){ 242, 238, 235, 255 }
#define COL_GAMBLER         (Color){ 245, 120, 180, 255 }
#define COL_PSYCHIC         (Color){ 95, 45, 140, 255 }
#define COL_CHARGE          (Color){ 20, 105, 225, 255 }
#define COL_UNSHACKLED      (Color){ 55, 35, 115, 255 }
#define COL_JUDGEMENT       (Color){ 245, 225, 135, 255 }
#define COL_TRAIN           (Color){ 35, 52, 72, 255 }
#define COL_SPIDER          (Color){ 215, 25, 30, 255 }
#define COL_RANGE           (Color){ 255, 215, 25, 255 }
#define COL_KNIGHT          (Color){ 185, 192, 205, 255 }
#define COL_CAVALRY         (Color){ 148, 96, 52, 255 }
#define COL_EXECUTER        (Color){ 78, 72, 84, 255 }
#define COL_KING            (Color){ 215, 175, 55, 255 }
#define COL_CATAPULT        (Color){ 188, 118, 52, 255 }
#define COL_ARCHER          (Color){ 46, 148, 66, 255 }
#define COL_SQUIRE          (Color){ 232, 202, 135, 255 }
#define COL_BLOOD           (Color){ 160, 0, 0, 255 }
#define COL_POISON          (Color){ 30, 160, 50, 255 }
#define COL_HEAL            (Color){ 150, 240, 150, 255 }
#define COL_PLUS            (Color){ 0, 150, 0, 255 }
#define COL_FREEZE          (Color){ 40, 110, 255, 255 }
#define BG_COLOR            (Color){ 212, 212, 215, 255 }

#define MAX_BALLS           32

// ======================= ENUMS & STRUCTS =======================
typedef enum { MENU_MODE, MENU_SANDBOX, MENU_BOT_MODE, MENU_CHALLENGER, MENU_SELECT, MENU_GACHA, MENU_INVENTORY, COUNTDOWN, PLAYING, GAME_OVER } GameState;
typedef enum { BOT_EASY = 0, BOT_MEDIUM, BOT_HARD, BOT_IMPOSSIBLE, BOT_DIFF_COUNT } BotDifficulty;
typedef enum { INV_TAB_INVENTORY = 0, INV_TAB_UPGRADE, INV_TAB_FUSION, INV_TAB_FORGE } InventoryTab;
typedef enum { RARITY_COMMON = 0, RARITY_UNCOMMON, RARITY_RARE, RARITY_MYTHIC, RARITY_LEGENDARY, RARITY_COUNT } BallRarity;
typedef enum {
    TYPE_BURST, TYPE_HARPOON, TYPE_VERITY, TYPE_DEVIL,
    TYPE_ICE, TYPE_TRIDENT, TYPE_ELECTRIC, TYPE_WITCH,
    TYPE_BOW, TYPE_REFORGE, TYPE_NINJA, TYPE_CELL, TYPE_LASER,
    TYPE_CACTUS, TYPE_WIND, TYPE_CHESS, TYPE_GUN,
    TYPE_DICE, TYPE_GAMBLER, TYPE_PSYCHIC,
    TYPE_CHARGE, TYPE_UNSHACKLED, TYPE_JUDGEMENT, TYPE_TRAIN,
    TYPE_SPIDER, TYPE_RANGE,
    TYPE_KNIGHT, TYPE_CAVALRY, TYPE_EXECUTER, TYPE_KING,
    TYPE_CATAPULT, TYPE_ARCHER, TYPE_SQUIRE,
    TYPE_COUNT,
    TYPE_REFORGE_SUB = 99
} BallType;
typedef enum { H_READY, H_SHOOTING, H_PULLING } HarpoonState;
typedef enum { T_READY, T_FLYING, T_LANDED, T_RETURNING } TridentState;
typedef enum { N_WAIT, N_RELOADING, N_THROWING } NinjaState;
typedef enum { CACTUS_HOLD, CACTUS_POKE, CACTUS_DUMP } CactusAIMode;
typedef enum { CHESS_NONE = 0, CHESS_QUEEN, CHESS_ROOK, CHESS_KNIGHT, CHESS_BISHOP } ChessPiece;
typedef enum { P_DAMAGE, P_SLOW, P_POISON, P_HEAL, P_FREEZE, P_COUNT } PotionKind;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    HarpoonState state;
    Vector2 trail[TRAIL_MAX];
    int trailCount;
    float trailCursor;
    int victimIdx;
    float pullDmgTimer;
    Vector2 anchorPos;
    bool firstShotDone;
} HarpoonData;

typedef struct {
    TridentState state;
    Vector2 pos;
    float aimAngle;
    float travelled;
    float timer;
    bool hit[MAX_BALLS];
} TridentData;

typedef struct {
    bool active;
    Vector2 pos;
    Vector2 vel;
    float angle;
    float travelled;
    float aimAngle;
} BowData;

typedef struct {
    NinjaState state;
    int currentKunai;
    int targetMaxKunai;
    float timer;
    float stepTime;
    float aimAngle;
} NinjaData;

typedef struct {
    int ammo;
    float reloadTimer;
    float fireDelayTimer;
    float aiDecisionTimer;
    int burstQueued;
    CactusAIMode aiMode;
} CactusData;

typedef struct {
    bool active;
    ChessPiece piece;
    Vector2 anchor;
    float cooldown;
    float elapsed;
    int curRay;
    int phase;
    float rayDist;
    Vector2 legTurnPos;
    bool hitThisRay[MAX_BALLS];
    Vector2 trail[CHESS_TRAIL_MAX];
    int trailCount;
} ChessData;

typedef struct {
    int phase;              // 0 = normal (2.25s), 1 = pre-rapid wait (0.50s), 2 = rapid fire (1.25s, heats up), 3 = overheat cooldown (2.50s, cools down)
    float phaseTimer;
    float shotTimer;
    float aimAngle;
    float barrelSpin;       // rotation angle for the 6-chamber cylinder
    float heat;             // 0.0 (normal gray-black) -> 1.0 (deep yellow-orange overheat)
} GunData;

typedef struct {
    bool rolling;
    float animTimer;        // 1.0s roll animation
    int rolledVal;          // 1..6 final result
    int displayFace;        // face currently shown during 3-spin animation
    float spinAngle;
} DiceData;

typedef struct {
    int phase;              // 0 = idle/waiting, 1 = rolling 3 dice (1.5s), 2 = drawing card (0.75s)
    float timer;
    int diceVals[3];        // 1..6 for each of the 3 dice
    int cardRank;           // 1 = Ace, 2..10 = number card, 11 = J, 12 = Q, 13 = K
    int nextHitDmg;         // (d1 + d2 + d3) * 3 applied on next offensive contact
    bool hasCardBuff;       // active defense buff from drawn card for next incoming contact
    bool waitingForHit;     // true after card reveal until Gambler collides with an enemy
    float bonusHoming;      // +5% homing bias gained each time Gambler finishes casting its skill
    float speedMult;        // starts at 1.0f, gains +5% speed each time Gambler finishes casting its skill
} GamblerData;

typedef struct {
    bool firstThrowDone;
    bool bhActive;
    float bhCooldown;       // 7.0s after first throw
    float bhTimer;          // 2.0s active black hole duration
    Vector2 bhPos;
} PsychicData;

typedef struct {
    int stacks;             // number of 3.0s growth stacks
    float timer;
    float exactRadius;      // exact compounding radius (multiplied by 1.10 each 3s)
    float exactDamage;      // exact compounding damage (multiplied by 1.30 each 3s)
} ChargeData;

typedef struct {
    int mode;               // 0 = cooldown, 1 = Leap Slam (0.50s), 2 = Lux Rainbow Laser (0.95s), 3 = Garen Sword (0.35s), 4 = Slam post-hold (0.35s)
    float timer;
    float cooldown;
    Vector2 startPos;
    Vector2 targetPos;      // remembered enemy position at cast start (or locked enemy pos for Mode 3)
    int targetEnemyIdx;     // locked target enemy index for 100% hit on Mode 3 sword drop
    float aimAngle;
    float fxTimer;          // lingering visual timer for fissure (0.45s = 0.35s hold + 0.10s fade) / rainbow beam / sword impact
    Vector2 fxPos;
    float fxAngle;
    int fxKind;             // 1 = fissure, 2 = rainbow beam, 3 = sword
    int passiveStacks;      // 0..3 Chain Lash stacks (+1 when a skill hits an enemy, resets timer to 3.0s)
    float passiveTimer;     // 3.0s stack duration timer (resets to 0 stacks if expired)
    float lashCooldown;     // 0.25s delay between Chain Lashes on collision
    float lashAnimTimer;    // visual whip animation timer when Chain Lash triggers
    float lashAngle;        // direction of the Chain Lash whip
} UnshackledData;

typedef struct {
    int state;              // 0 = cooldown (3.0s), 1 = charging (0.75s anchored + 50% dmg reduction), 2 = ramming (100% dmg immune)
    float timer;
    float shrinkTimer;      // 0.30s shrink-back timer after ram finishes
    float immuneTimer;      // 0.50s post-ram 100% damage immunity timer
    Vector2 anchorPos;
    Vector2 targetPos;
    Vector2 ramDir;
    int targetEnemyIdx;     // enemy targeted at charge start
    bool guaranteedHit;     // true if targetEnemyIdx remained within 35% window-width radius when 0.75s charge finished
    float travelled;
    bool hitEnemy[MAX_BALLS];
} JudgementData;

typedef struct {
    int phase;              // 0 = laying tracks (4.0s), 1 = pre-train wait (1.0s), 2 = train rushing (up to 5 cars), 3 = post-train wait (2.0s)
    float timer;
    Vector2 track[TRAIN_TRACK_MAX];
    int trackCount;
    float totalTrackLen;
    float headDist;         // distance travelled by lead locomotive along track
    int numCars;            // up to 5 cars (TRAIN_MAX_CARS)
    Vector2 carPos[TRAIN_MAX_CARS];
    float carAngle[TRAIN_MAX_CARS];
    bool carActive[TRAIN_MAX_CARS];
    float hitImmune[MAX_BALLS]; // 0.5s per-victim immunity to prevent multi-hit melting
} TrainData;

typedef struct {
    Vector2 p1;
    Vector2 p2;
    bool touching[MAX_BALLS];
    bool active;
} SpiderWebLine;

typedef struct {
    bool hasPendingAnchor;
    Vector2 pendingAnchor;
    SpiderWebLine webs[SPIDER_WEB_MAX];
    int webNext;
} SpiderData;

typedef struct {
    float baseRingRadius;   // initial ring radius = 1.1x ball radius (33px)
    float ringRadius;       // grows every 2.0s without limit
    float curStepMult;      // starts at RANGE_GROW_STEP and grows by +10% each cycle
    int stacks;
    float growTimer;        // 2.0s growth timer
    float tickTimer;        // 0.5s AoE damage tick timer
} RangeData;

typedef struct {
    bool windingUp;         // true during 0.10s pre-slash delay after contact
    float timer;            // counts up to KNIGHT_SLASH_DELAY (0.10s)
    float slashAngle;       // remembered angle toward target at contact
    float fxTimer;          // visual duration of the 30-degree cone slash arc
    float fxAngle;
} KnightData;

typedef struct {
    int state;              // 0 = normal (4.0s cooldown), 1 = charging (accelerates up to 1.6x for up to 50% window width), 2 = post-crash 1.25s immobilize
    float timer;            // 4.0s cooldown or 1.25s immobilize timer
    float travelled;        // distance covered during Charge
    float speedMult;        // 1.0f -> 1.6f during Charge
    float aimAngle;         // direction of the long iron lance
    float thrustTimer;      // visual lance thrust animation on contact
} CavalryData;

typedef struct {
    float chopCooldown;     // 2.25s cooldown on basic contact axe chop (0 dmg while > 0)
    float chopAnimTimer;    // heavy overhead axe chop animation timer
    float chopAngle;
    int skillState;         // 0 = 5.0s cooldown, 1 = 3.0s Sion-Q trapezoid charge
    float skillTimer;       // 5.0s cooldown or 3.0s charge progress
    float trapAngle;        // direction of the Sion-Q trapezoid
    float slamFxTimer;      // lingering visual flash after 3.0s slam completes
    Vector2 anchorPos;
    bool captured[MAX_BALLS];
} ExecuterData;

typedef struct {
    int state;              // 0 = 2.5s cooldown, 1 = flag planted (0.25s wait before dash), 2 = dashing to flag (cc & dmg immune)
    float timer;
    Vector2 flagPos;
    bool flagActive;
    bool hitOnDash[MAX_BALLS];
} KingData;

typedef struct {
    float cooldown;         // 10.0s cooldown between stone boulder launches
    bool boulderActive;     // true while stone boulder is flying along its parabolic sky arc
    float boulderTimer;     // 0.0 -> CATAPULT_FLIGHT_T (1.15s)
    Vector2 boulderStart;
    Vector2 boulderTarget;
    float armAnimTimer;     // catapult throwing arm swing animation timer
    float aimAngle;
} CatapultData;

typedef struct {
    float cooldown;         // 7.5s cooldown between fire-arrow shots
    bool arrowActive;       // true while fire-tipped arrow is in flight
    Vector2 arrowPos;
    Vector2 arrowVel;
    float arrowAngle;
    float arrowTravelled;
    float aimAngle;
} ArcherData;

typedef struct {
    Vector2 pos;
    float radius;           // 1.8x ball radius (54px)
    float age;
    int team;
    float dmgMult;
    bool active;
} BurnZone;

typedef struct {
    Vector2 pos;
    float angle;
    float age;
    bool active;
} IceSeg;

typedef struct {
    bool active;
    int team;
    bool isSubBall;
    int parentIdx;
    int subIdx;
    int reforgeLevel;
    int cellStage;

    Vector2 pos;
    Vector2 vel;
    Vector2 normalVel;
    int radius;
    int hp;
    int maxHp;
    Color color;
    BallType type;

    int damage;
    float dmgMult;          // attacker damage multiplier (Upgrade +2.5%/lv, Hard Bot +15%, Impossible Bot +25%)
    float homingBonus;      // extra homing bias for Hard (+5%) / Impossible (+10%) Bot contact-dmg balls
    float dodgeChance;      // 5% chance to take 0 damage (Impossible Bot)
    float dmgRedChance;     // 10% chance to reduce incoming damage by 33% (Impossible Bot)
    float skillTimer;
    float durationTimer;

    bool isDashing;
    Vector2 shadowHistory[SHADOW_N];

    // Debounce state so each wall impact triggers at most 1 turret (Laser) or 1 anchor (Web)
    bool touchingWall;
    float wallBounceLockTimer;

    int verityForm;
    HarpoonData harpoon;

    // Freeze state
    bool frozen;
    float frozenTimer;
    float freezeDmgTimer;
    float freezeImmune;
    Vector2 freezePos;

    // Electric shock state
    bool shocked;
    int shockTicks;
    float shockTick;
    Vector2 shockPos;

    // Wind stun state
    bool windStunned;
    float windStunTimer;
    Vector2 windStunPos;

    // Dice lock state (anchored + invulnerable to external damage for 0.25s)
    bool diceLocked;
    float diceLockTimer;
    Vector2 diceLockPos;

    // Shackless earthquake slam hold / Cavalry crash stun / King dash stun
    bool slamLocked;
    float slamLockTimer;
    Vector2 slamLockPos;

    // Executer 3.0s Sion-Q capture state (anchored, stunned, 0 contact dmg dealt & immune to other contact dmg)
    bool execLocked;
    float execLockTimer;
    Vector2 execLockPos;

    // Red pain flash overlay when hit by Executer's heavy axe
    float painFlashTimer;

    // Per-victim hit cooldown for Chess to prevent multi-hit melting
    float chessHitImmune[MAX_BALLS];

    // Electric charge
    bool charged;
    float chargeTimer;

    // Witch debuffs
    float slowYTimer;
    float slowPTimer;
    int poisonTicks;
    float poisonTick;
    float poisonDmgMult;

    // Archer burn debuff (3 HP every 0.75s for 3 ticks = 9 HP total)
    int burnTicks;
    float burnTickTimer;
    float burnDmgMult;

    float shockDmgMult;
    float freezeDmgMult;

    // Short separation speed boost
    float boostTimer;

    // Devil drain link
    int drainTicks;
    float drainTick;
    int drainAmt;
    int drainTargetIdx;
    Vector2 linkOffset;

    TridentData trident;
    BowData bow;
    NinjaData ninja;
    CactusData cactus;
    ChessData chess;
    GunData gun;
    DiceData dice;
    GamblerData gambler;
    PsychicData psychic;
    ChargeData charge;
    UnshackledData unshackled;
    JudgementData judgement;
    TrainData train;
    SpiderData spider;
    RangeData range;
    KnightData knight;
    CavalryData cavalry;
    ExecuterData executer;
    KingData king;
    CatapultData catapult;
    ArcherData archer;

    IceSeg ice[ICE_MAX];
    int iceHead;
} Ball;

typedef struct { Vector2 pos, vel; float angle; float travelled; int team; float dmgMult; bool active; } Kunai;
typedef struct {
    Vector2 pos;
    Vector2 dir;
    Vector2 endPos;
    int team;
    float dmgMult;
    float shootTimer;
    float beamTimer;
    bool active;
} LaserTurret;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float travelled;
    int team;
    float dmgMult;
    bool active;
} CactusBomb;

typedef struct {
    Vector2 origin;
    Vector2 pos;
    float baseAngle;
    float angle;
    float dist;
    float maxDist;
    bool curving;
    int team;
    float dmgMult;
    bool active;
} CactusSpike;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float spinAngle;
    float travelled;        // distance flown so far (limited to TORNADO_RANGE)
    int team;
    float dmgMult;
    bool hit[MAX_BALLS];    // tracks enemies already stunned by this tornado along its path
    bool active;
} WindTornado;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float travelled;
    int team;
    float dmgMult;
    bool active;
} GunBullet;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float angle;
    Color cardCol;
    int team;
    float dmgMult;
    bool active;
} TarotCard;

typedef struct {
    Vector2 pos;
    float timer;            // 2.0s duration
    float spinAngle;
    int ownerIdx;
    int team;
    bool active;
} BlackHole;

typedef struct { Vector2 pos, target; PotionKind kind; int ownerIdx; bool active; } Potion;
typedef struct { Vector2 pos; float radius, age; PotionKind kind; int ownerIdx; bool hit[MAX_BALLS]; bool active; } Zone;
typedef struct { Vector2 pos; float radius, age; Color col; bool active; } BoomFx;
typedef struct { Vector2 pos, vel; float age, life, size, grav; Color col; bool plus, active; } Particle;
typedef struct { Vector2 pos; int value; float age; bool heal; bool active; } Popup;

// ======================= SMALL MATH HELPERS =======================
static inline Vector2 V2Add(Vector2 a, Vector2 b) { return (Vector2){ a.x + b.x, a.y + b.y }; }
static inline Vector2 V2Sub(Vector2 a, Vector2 b) { return (Vector2){ a.x - b.x, a.y - b.y }; }
static inline Vector2 V2Mul(Vector2 a, float s)   { return (Vector2){ a.x * s, a.y * s }; }
static inline float   V2DistSq(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}
static inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline Vector2 ClampScreen(Vector2 p, float margin) {
    return (Vector2){ ClampF(p.x, margin, SCREEN_W - margin),
                      ClampF(p.y, margin, SCREEN_H - margin) };
}
static Vector2 Norm(Vector2 v)
{
    float l = sqrtf(v.x * v.x + v.y * v.y);
    return (l < 0.0001f) ? (Vector2){ 1.0f, 0.0f } : (Vector2){ v.x / l, v.y / l };
}

// Distance-based accuracy helper:
// At close range (dist -> 0), hit probability approaches maxHitPct (e.g., 95%).
// At maxRange, hit probability drops linearly to minHitPctAtMaxRange (e.g., 40% for Bow, 70% for Ninja, 50%/30% for Gun).
static float AimAngleWithDistanceFalloff(Vector2 shooterPos, const Ball* enemy, float projSpeed,
                                         float maxRange, float maxHitPct, float minHitPctAtMaxRange)
{
    float dist = sqrtf(V2DistSq(enemy->pos, shooterPos));
    float tFrames = dist / fmaxf(projSpeed, 1.0f);
    Vector2 pred = V2Add(enemy->pos, V2Mul(enemy->vel, tFrames * 0.82f));
    float baseAngle = atan2f(pred.y - shooterPos.y, pred.x - shooterPos.x);

    float distRatio = ClampF(dist / fmaxf(maxRange, 1.0f), 0.0f, 1.0f);
    float hitChance = maxHitPct - distRatio * (maxHitPct - minHitPctAtMaxRange);

    if ((float)GetRandomValue(1, 1000) <= hitChance * 10.0f) {
        // On-target shot: tiny jitter proportional to distance
        float tightSpread = (1.2f + 2.5f * distRatio) * DEG2RAD;
        float r = (float)GetRandomValue(-100, 100) / 100.0f;
        return baseAngle + r * tightSpread;
    } else {
        // Missed shot: angular deviation outside enemy's angular radius
        int sign = (GetRandomValue(0, 1) == 0) ? -1 : 1;
        float missDeg = 15.0f + (float)GetRandomValue(0, 16) + 8.0f * distRatio;
        return baseAngle + (float)sign * missDeg * DEG2RAD;
    }
}

static void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, col);
    else           DrawTriangle(a, c, b, col);
}

static void DrawCenteredText(const char* text, int y, int fontSize, Color col)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, fontSize) / 2, y, fontSize, col);
}

// ======================= GLOBAL STATE & EFFECTS =======================
#define POPUP_MAX    64
#define POPUP_TIME   0.9f
#define PARTICLE_MAX 360
#define POTION_MAX   48
#define ZONE_MAX     16
#define FX_MAX       48
#define FX_TIME      0.35f

static Ball        g_balls[MAX_BALLS];
static int         g_ballCount = 0;
static bool        g_wasColliding[MAX_BALLS][MAX_BALLS];

static Kunai       g_kunais[KUNAI_MAX];             static int g_kunaiNext = 0;
static LaserTurret g_turrets[TURRET_MAX];           static int g_turretNext = 0;
static CactusBomb  g_cactusBombs[CACTUS_BOMB_MAX];  static int g_cactusBombNext = 0;
static CactusSpike g_cactusSpikes[CACTUS_SPIKE_MAX];static int g_cactusSpikeNext = 0;
static WindTornado g_tornados[TORNADO_MAX];         static int g_tornadoNext = 0;
static GunBullet   g_bullets[GUN_BULLET_MAX];       static int g_bulletNext = 0;
static TarotCard   g_tarots[TAROT_MAX];             static int g_tarotNext = 0;
static BlackHole   g_blackHoles[BLACKHOLE_MAX];     static int g_blackHoleNext = 0;
static float       g_diceRedFlashTimer = 0.0f;      // red screen border flash when Dice deals damage
static Popup       g_popups[POPUP_MAX];             static int g_popupNext = 0;
static Particle    g_particles[PARTICLE_MAX];       static int g_particleNext = 0;
static Potion      g_potions[POTION_MAX];           static int g_potionNext = 0;
static Zone        g_zones[ZONE_MAX];               static int g_zoneNext = 0;
static BurnZone    g_burnZones[BURN_ZONE_MAX];      static int g_burnZoneNext = 0;
static BoomFx      g_fx[FX_MAX];                    static int g_fxNext = 0;

static void AddPopup(Vector2 pos, int value, bool heal)
{
    Popup* p = &g_popups[g_popupNext];
    g_popupNext = (g_popupNext + 1) % POPUP_MAX;
    *p = (Popup){
        .pos = { pos.x + (float)GetRandomValue(-18, 18), pos.y },
        .value = value, .age = 0.0f, .heal = heal, .active = true
    };
}

static void SpawnParticle(Vector2 pos, Vector2 vel, float life, float size, Color col, float grav, bool plus)
{
    Particle* p = &g_particles[g_particleNext];
    g_particleNext = (g_particleNext + 1) % PARTICLE_MAX;
    *p = (Particle){
        .pos = pos, .vel = vel, .age = 0.0f, .life = life,
        .size = size, .grav = grav, .col = col, .plus = plus, .active = true
    };
}

static void AddFx(Vector2 pos, float radius, Color col)
{
    BoomFx* f = &g_fx[g_fxNext];
    g_fxNext = (g_fxNext + 1) % FX_MAX;
    *f = (BoomFx){ .pos = pos, .radius = radius, .age = 0.0f, .col = col, .active = true };
}

static void CreateZone(Vector2 pos, float radius, PotionKind kind, int ownerIdx)
{
    Zone* z = &g_zones[g_zoneNext];
    g_zoneNext = (g_zoneNext + 1) % ZONE_MAX;
    *z = (Zone){ .pos = pos, .radius = radius, .age = 0.0f, .kind = kind, .ownerIdx = ownerIdx, .active = true };
}

static void CreateBurnZone(Vector2 pos, float radius, int team, float dmgMult)
{
    BurnZone* bz = &g_burnZones[g_burnZoneNext];
    g_burnZoneNext = (g_burnZoneNext + 1) % BURN_ZONE_MAX;
    *bz = (BurnZone){ .pos = ClampScreen(pos, 10.0f), .radius = radius, .age = 0.0f, .team = team, .dmgMult = dmgMult, .active = true };
    AddFx(bz->pos, radius, ORANGE);
}

static void ClearEffects(void)
{
    memset(g_kunais, 0, sizeof(g_kunais));
    memset(g_turrets, 0, sizeof(g_turrets));
    memset(g_cactusBombs, 0, sizeof(g_cactusBombs));
    memset(g_cactusSpikes, 0, sizeof(g_cactusSpikes));
    memset(g_tornados, 0, sizeof(g_tornados));
    memset(g_bullets, 0, sizeof(g_bullets));
    memset(g_tarots, 0, sizeof(g_tarots));
    memset(g_blackHoles, 0, sizeof(g_blackHoles));
    g_diceRedFlashTimer = 0.0f;
    memset(g_popups, 0, sizeof(g_popups));
    memset(g_particles, 0, sizeof(g_particles));
    memset(g_potions, 0, sizeof(g_potions));
    memset(g_zones, 0, sizeof(g_zones));
    memset(g_burnZones, 0, sizeof(g_burnZones));
    memset(g_fx, 0, sizeof(g_fx));
    memset(g_wasColliding, 0, sizeof(g_wasColliding));
}

// ======================= GAMEPLAY HELPERS =======================
static inline bool IsAlive(const Ball* b)
{
    return b && b->active && (b->isSubBall || b->hp > 0);
}

static inline bool IsCombatBall(const Ball* b)
{
    return b && b->active && !b->isSubBall && b->hp > 0;
}

static Ball* FindNearestEnemy(const Ball* b)
{
    Ball* best = NULL;
    float bestDistSq = 1e18f;
    for (int i = 0; i < g_ballCount; i++) {
        Ball* cand = &g_balls[i];
        if (!IsCombatBall(cand) || cand->team == b->team) continue;
        float dSq = V2DistSq(b->pos, cand->pos);
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            best = cand;
        }
    }
    return best;
}

static Ball* FindFarthestEnemy(const Ball* b)
{
    Ball* best = NULL;
    float bestDistSq = -1.0f;
    for (int i = 0; i < g_ballCount; i++) {
        Ball* cand = &g_balls[i];
        if (!IsCombatBall(cand) || cand->team == b->team) continue;
        float dSq = V2DistSq(b->pos, cand->pos);
        if (dSq > bestDistSq) {
            bestDistSq = dSq;
            best = cand;
        }
    }
    return best;
}

static int FindFreeBallSlot(void)
{
    for (int i = 0; i < g_ballCount; i++) {
        if (!g_balls[i].active) return i;
    }
    if (g_ballCount < MAX_BALLS) return g_ballCount++;
    return -1;
}

static float GetValidRandomAngle(void)
{
    int a;
    do {
        a = GetRandomValue(0, 359);
    } while ((a >= 68 && a <= 112) || (a >= 248 && a <= 292) ||
             a <= 18 || a >= 342 || (a >= 162 && a <= 198));
    return (float)a * DEG2RAD;
}

static void SetDirection(Ball* b, Vector2 dir)
{
    b->normalVel = V2Mul(dir, BASE_SPEED);
    b->vel = b->isDashing ? V2Mul(dir, BASE_SPEED * DASH_MULT) : b->normalVel;
}

static void SetRandomDirection(Ball* b)
{
    float a = GetValidRandomAngle();
    SetDirection(b, (Vector2){ cosf(a), sinf(a) });
}

static inline bool IsHarpoonActive(const Ball* b)
{
    return b->type == TYPE_HARPOON && b->harpoon.state != H_READY;
}

static inline bool IsChessDashing(const Ball* b)
{
    return b->type == TYPE_CHESS && b->chess.active;
}

static inline bool IsUnshackledCasting(const Ball* b)
{
    return b->type == TYPE_UNSHACKLED && b->unshackled.mode != 0;
}

static inline bool IsJudgementActive(const Ball* b)
{
    return b->type == TYPE_JUDGEMENT && b->judgement.state != 0;
}

static inline bool IsKingDashing(const Ball* b)
{
    return b->type == TYPE_KING && b->king.state == 2;
}

static inline bool IsExecuterCharging(const Ball* b)
{
    return b->type == TYPE_EXECUTER && b->executer.skillState == 1;
}

static inline bool IsFixed(const Ball* b)
{
    return b->frozen || b->shocked || b->windStunned || b->diceLocked || b->slamLocked || b->execLocked ||
           IsHarpoonActive(b) || IsChessDashing(b) || IsExecuterCharging(b) ||
           (b->type == TYPE_JUDGEMENT && b->judgement.state == 1) ||
           (b->type == TYPE_UNSHACKLED && (b->unshackled.mode == 2 || b->unshackled.mode == 3 || b->unshackled.mode == 4));
}

static inline float SlowMult(const Ball* b)
{
    return (b->slowYTimer > 0.0f ? 0.5f : 1.0f) * (b->slowPTimer > 0.0f ? 0.9f : 1.0f);
}

static void RestoreVelocity(Ball* b)
{
    if (IsHarpoonActive(b)) b->vel = (Vector2){ 0, 0 };
    else b->vel = b->isDashing ? V2Mul(b->normalVel, DASH_MULT) : b->normalVel;
}

static void EnforceAnchors(Ball* b)
{
    if (!b->active) return;
    if (IsHarpoonActive(b)) {
        b->pos = b->harpoon.anchorPos;
        b->vel = b->normalVel = (Vector2){ 0, 0 };
    }
    if (b->type == TYPE_JUDGEMENT && b->judgement.state == 1) {
        b->pos = b->judgement.anchorPos;
    }
    if (b->type == TYPE_UNSHACKLED && b->unshackled.mode == 4) {
        b->pos = b->unshackled.targetPos;
    }
    if (IsExecuterCharging(b)) {
        b->pos = b->executer.anchorPos;
    }
    if (b->frozen)      b->pos = b->freezePos;
    if (b->shocked)     b->pos = b->shockPos;
    if (b->windStunned) b->pos = b->windStunPos;
    if (b->diceLocked)  b->pos = b->diceLockPos;
    if (b->slamLocked)  b->pos = b->slamLockPos;
    if (b->execLocked)  b->pos = b->execLockPos;
}

static void SpawnLanceSparks(Vector2 pos)
{
    for (int k = 0; k < 22; k++) {
        float a = (float)GetRandomValue(0, 359) * DEG2RAD;
        float sp = 2.5f + (float)GetRandomValue(0, 45) * 0.12f;
        Color sc = (k % 3 == 0) ? WHITE : ((k & 1) ? GOLD : (Color){ 255, 120, 20, 255 });
        SpawnParticle(pos, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.38f, 3.2f, sc, 0.08f, false);
    }
    AddFx(pos, 26.0f, GOLD);
}

static int ComputeNinjaKunaiCount(int hp)
{
    // Scales rapidly from 3 kunai at 100 HP up to 8 kunai at <= 35 HP
    float t = ClampF((float)(100 - hp) / 65.0f, 0.0f, 1.0f);
    return 3 + (int)roundf(t * 5.0f);
}

static int InitBallStats(int idx, BallType type, int team, Vector2 startPos)
{
    Ball* b = &g_balls[idx];
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->team = team;
    b->isSubBall = false;
    b->parentIdx = -1;
    b->subIdx = -1;
    b->drainTargetIdx = -1;
    b->harpoon.victimIdx = -1;
    b->pos = startPos;
    b->hp = 100;
    b->maxHp = 100;
    b->dmgMult = 1.0f;
    b->burnDmgMult = 1.0f;
    b->poisonDmgMult = 1.0f;
    b->shockDmgMult = 1.0f;
    b->freezeDmgMult = 1.0f;
    b->radius = BASE_RADIUS;
    b->type = type;
    for (int i = 0; i < SHADOW_N; i++) b->shadowHistory[i] = startPos;

    SetRandomDirection(b);

    switch (type)
    {
        case TYPE_BURST:
            b->color = ORANGE;
            b->damage = BURST_BASE_DMG;
            b->skillTimer = BURST_COOLDOWN;
            break;
        case TYPE_HARPOON:
            b->color = SKYBLUE;
            b->skillTimer = 3.0f;
            b->harpoon.state = H_READY;
            b->harpoon.firstShotDone = false;
            break;
        case TYPE_VERITY:
            b->color = YELLOW;
            b->damage = 5;
            b->verityForm = 1;
            break;
        case TYPE_DEVIL:    b->color = COL_DEVIL;    break;
        case TYPE_ICE:      b->color = COL_ICE;      break;
        case TYPE_TRIDENT:
            b->color = COL_TRIDENT;
            b->trident.state = T_READY;
            b->trident.timer = TRIDENT_COOLDOWN;
            break;
        case TYPE_ELECTRIC: b->color = COL_ELECTRIC; b->chargeTimer = ELEC_CHARGE_TIME; break;
        case TYPE_WITCH:    b->color = COL_WITCH;    b->skillTimer = WITCH_INTERVAL; break;
        case TYPE_BOW:
            b->color = COL_BOW;
            b->damage = 0;
            b->skillTimer = BOW_COOLDOWN;
            break;
        case TYPE_REFORGE:
            b->color = COL_REFORGE;
            b->radius = REFORGE_MAIN_RAD;
            b->damage = 0;
            b->reforgeLevel = 1;
            break;
        case TYPE_NINJA:
            b->color = COL_NINJA;
            b->damage = 0;
            b->ninja.state = N_WAIT;
            b->ninja.currentKunai = 0;
            b->ninja.targetMaxKunai = 3;
            b->ninja.timer = NINJA_WAIT_TIME;
            b->ninja.stepTime = NINJA_RELOAD_WINDOW / 3.0f;
            break;
        case TYPE_CELL:
            b->color = COL_CELL;
            b->radius = CELL_SMALL_RAD;
            b->hp = CELL_HP;
            b->maxHp = CELL_HP;
            b->damage = CELL_DMG;
            b->cellStage = 1;
            b->skillTimer = CELL_GROW_TIME;
            break;
        case TYPE_LASER:
            b->color = COL_LASER;
            b->damage = 0;
            break;
        case TYPE_CACTUS:
            b->color = COL_CACTUS;
            b->damage = 0;
            b->cactus.ammo = CACTUS_MAX_AMMO;
            b->cactus.reloadTimer = CACTUS_RELOAD_TIME;
            b->cactus.fireDelayTimer = 0.25f;
            b->cactus.aiDecisionTimer = 0.2f;
            b->cactus.aiMode = CACTUS_POKE;
            break;
        case TYPE_WIND:
            b->color = COL_WIND;
            b->damage = 0;
            b->skillTimer = WIND_FIRST_DELAY;
            break;
        case TYPE_CHESS:
            b->color = COL_CHESS;
            b->damage = 0;
            b->chess.active = false;
            b->chess.cooldown = CHESS_COOLDOWN;
            break;
        case TYPE_GUN:
            b->color = COL_GUN;
            b->damage = 0;
            b->gun.phase = 0;
            b->gun.phaseTimer = GUN_NORM_DURATION;
            b->gun.shotTimer = GUN_NORM_INTERVAL;
            b->gun.heat = 0.0f;
            break;
        case TYPE_DICE:
            b->color = COL_DICE;
            b->damage = 0;
            b->skillTimer = DICE_COOLDOWN;
            b->dice.rolledVal = 6;
            b->dice.displayFace = 6;
            break;
        case TYPE_GAMBLER:
            b->color = COL_GAMBLER;
            b->damage = GAMBLER_BASE_DMG; // 5 base contact damage
            b->gambler.phase = 0;
            b->gambler.timer = GAMBLER_COOLDOWN;
            b->gambler.diceVals[0] = 1;
            b->gambler.diceVals[1] = 1;
            b->gambler.diceVals[2] = 1;
            b->gambler.cardRank = 0;
            b->gambler.bonusHoming = 0.0f;
            b->gambler.speedMult = 1.0f;
            break;
        case TYPE_PSYCHIC:
            b->color = COL_PSYCHIC;
            b->damage = 0;
            b->skillTimer = PSYCHIC_NORM_CD;
            b->psychic.firstThrowDone = false;
            b->psychic.bhActive = false;
            b->psychic.bhCooldown = PSYCHIC_BH_DELAY;
            break;
        case TYPE_CHARGE:
            b->color = COL_CHARGE;
            b->damage = CHARGE_BASE_DMG; // 10 base contact damage
            b->charge.stacks = 0;
            b->charge.timer = CHARGE_INTERVAL;
            b->charge.exactRadius = (float)BASE_RADIUS;
            b->charge.exactDamage = (float)CHARGE_BASE_DMG;
            break;
        case TYPE_UNSHACKLED:
            b->color = COL_UNSHACKLED;
            b->damage = 0;
            b->unshackled.mode = 0;
            b->unshackled.cooldown = UNSHACKLED_CD;
            b->unshackled.targetEnemyIdx = -1;
            b->unshackled.passiveStacks = 0;
            b->unshackled.passiveTimer = 0.0f;
            b->unshackled.lashCooldown = 0.0f;
            b->unshackled.lashAnimTimer = 0.0f;
            break;
        case TYPE_JUDGEMENT:
            b->color = COL_JUDGEMENT;
            b->damage = 0;
            b->judgement.state = 0;
            b->judgement.timer = JUDGEMENT_CD;
            b->judgement.shrinkTimer = 0.0f;
            b->judgement.immuneTimer = 0.0f;
            b->judgement.targetEnemyIdx = -1;
            b->judgement.guaranteedHit = false;
            break;
        case TYPE_TRAIN:
            b->color = COL_TRAIN;
            b->damage = 0;
            b->train.phase = 0;
            b->train.timer = TRAIN_LAY_TIME;
            b->train.trackCount = 0;
            b->train.totalTrackLen = 0.0f;
            b->train.headDist = 0.0f;
            b->train.numCars = 0;
            break;
        case TYPE_SPIDER:
            b->color = COL_SPIDER;
            b->damage = 0;
            b->spider.hasPendingAnchor = false;
            b->spider.webNext = 0;
            break;
        case TYPE_RANGE:
            b->color = COL_RANGE;
            b->damage = 0;
            b->range.baseRingRadius = (float)BASE_RADIUS * RANGE_INIT_MULT;
            b->range.ringRadius = b->range.baseRingRadius;
            b->range.curStepMult = RANGE_GROW_STEP;
            b->range.stacks = 0;
            b->range.growTimer = RANGE_GROW_INTERVAL;
            b->range.tickTimer = RANGE_TICK_TIME;
            break;
        case TYPE_KNIGHT:
            b->color = COL_KNIGHT;
            b->damage = 0;
            b->knight.windingUp = false;
            b->knight.timer = 0.0f;
            b->knight.fxTimer = 0.0f;
            break;
        case TYPE_CAVALRY:
            b->color = COL_CAVALRY;
            b->damage = 0;
            b->cavalry.state = 0;
            b->cavalry.timer = CAVALRY_CD;
            b->cavalry.travelled = 0.0f;
            b->cavalry.speedMult = 1.0f;
            b->cavalry.aimAngle = atan2f(b->vel.y, b->vel.x);
            break;
        case TYPE_EXECUTER:
            b->color = COL_EXECUTER;
            b->damage = 0;
            b->executer.chopCooldown = 0.0f;
            b->executer.chopAnimTimer = 0.0f;
            b->executer.skillState = 0;
            b->executer.skillTimer = EXECUTER_SKILL_CD;
            break;
        case TYPE_KING:
            b->color = COL_KING;
            b->damage = 0;
            b->king.state = 0;
            b->king.timer = KING_CD;
            b->king.flagActive = false;
            break;
        case TYPE_CATAPULT:
            b->color = COL_CATAPULT;
            b->damage = 0;
            b->catapult.cooldown = CATAPULT_CD;
            b->catapult.boulderActive = false;
            b->catapult.boulderTimer = 0.0f;
            b->catapult.armAnimTimer = 0.0f;
            b->catapult.aimAngle = 0.0f;
            break;
        case TYPE_ARCHER:
            b->color = COL_ARCHER;
            b->damage = 0;
            b->archer.cooldown = ARCHER_CD;
            b->archer.arrowActive = false;
            b->archer.arrowTravelled = 0.0f;
            b->archer.aimAngle = 0.0f;
            break;
        case TYPE_SQUIRE:
            b->color = COL_SQUIRE;
            b->damage = SQUIRE_BASE_DMG; // 10 base contact damage
            break;
        default: break;
    }
    return idx;
}

static void SpawnReforgeSubBall(int parentIdx)
{
    int subIdx = FindFreeBallSlot();
    if (subIdx < 0) return;
    Ball* parent = &g_balls[parentIdx];
    parent->subIdx = subIdx;

    Ball* sub = &g_balls[subIdx];
    memset(sub, 0, sizeof(*sub));
    sub->active = true;
    sub->team = parent->team;
    sub->isSubBall = true;
    sub->parentIdx = parentIdx;
    sub->subIdx = -1;
    sub->drainTargetIdx = -1;
    sub->harpoon.victimIdx = -1;
    sub->type = TYPE_REFORGE_SUB;
    sub->color = COL_REFORGE_SUB;
    sub->radius = REFORGE_SUB_RAD;
    sub->hp = 100;
    sub->maxHp = 100;
    sub->reforgeLevel = 1;
    sub->damage = (int)roundf(REFORGE_BASE_DMG);

    float offsetX = (parent->team == 0) ? 55.0f : -55.0f;
    sub->pos = ClampScreen((Vector2){ parent->pos.x + offsetX, parent->pos.y - 45.0f }, (float)sub->radius);
    for (int i = 0; i < SHADOW_N; i++) sub->shadowHistory[i] = sub->pos;

    Vector2 toParent = Norm(V2Sub(parent->pos, sub->pos));
    SetDirection(sub, toParent);
}

static void SpawnCellSplit(Vector2 deathPos, int team)
{
    float baseAngle = (float)GetRandomValue(0, 359) * DEG2RAD;
    for (int k = 0; k < 3; k++) {
        int slot = FindFreeBallSlot();
        if (slot < 0) break;
        float a = baseAngle + k * (2.0f * PI / 3.0f);
        Vector2 dir = { cosf(a), sinf(a) };
        Vector2 startPos = ClampScreen(V2Add(deathPos, V2Mul(dir, 22.0f)), (float)CELL_SMALL_RAD);
        InitBallStats(slot, TYPE_CELL, team, startPos);
        SetDirection(&g_balls[slot], dir);
    }
    AddFx(deathPos, BASE_RADIUS * 1.4f, COL_CELL);
}

static float LerpChance(int hp, int hpHigh, int hpLow, float minPct, float maxPct)
{
    float t = ClampF((float)(hpHigh - hp) / (float)(hpHigh - hpLow), 0.0f, 1.0f);
    return minPct + t * (maxPct - minPct);
}

static void DealDamageScaled(Ball* victim, int dmg, bool bypass, float atkMult)
{
    if (!IsCombatBall(victim)) return;
    // King is completely immune to ALL damage while dashing to its flag
    if (IsKingDashing(victim)) return;
    // Judgement is 100% immune to damage while ramming (state == 2) and for 0.50s after finishing the ram
    if (victim->type == TYPE_JUDGEMENT && (victim->judgement.state == 2 || victim->judgement.immuneTimer > 0.0f)) return;
    // Shocked or Dice-locked targets are invulnerable to external damage
    if (!bypass && (victim->shocked || victim->diceLocked)) return;

    // Impossible Bot passive: 5% chance to be 100% immune to incoming damage
    if (!bypass && victim->dodgeChance > 0.0f && dmg > 0) {
        if (GetRandomValue(1, 1000) <= (int)(victim->dodgeChance * 1000.0f)) {
            AddFx(victim->pos, (float)victim->radius * 1.15f, WHITE);
            return;
        }
    }

    if (atkMult > 0.0f && atkMult != 1.0f && dmg > 0) {
        dmg = (int)roundf((float)dmg * atkMult);
        if (dmg < 1) dmg = 1;
    }

    // Judgement takes 50% reduced damage during its 0.75s charge (state == 1)
    if (victim->type == TYPE_JUDGEMENT && victim->judgement.state == 1 && dmg > 0) {
        dmg = (int)roundf((float)dmg * 0.50f);
        if (dmg < 1) dmg = 1;
    }

    // Squire takes 15% reduced incoming damage
    if (!bypass && victim->type == TYPE_SQUIRE && dmg > 0) {
        dmg = (int)roundf((float)dmg * (1.0f - SQUIRE_DMG_RED));
        if (dmg < 1) dmg = 1;
    }

    // Impossible Bot passive: 10% chance to reduce incoming damage by 33%
    if (!bypass && victim->dmgRedChance > 0.0f && dmg > 0) {
        if (GetRandomValue(1, 1000) <= (int)(victim->dmgRedChance * 1000.0f)) {
            dmg = (int)roundf((float)dmg * 0.67f);
            if (dmg < 1) dmg = 1;
            AddFx(victim->pos, (float)victim->radius * 1.05f, SKYBLUE);
        }
    }

    int raw = dmg;
    if (dmg > victim->hp) dmg = victim->hp;
    if (dmg <= 0) return;
    victim->hp -= dmg;
    AddPopup(victim->pos, dmg, false);

    if (victim->hp <= 0) {
        if (victim->type == TYPE_REFORGE && victim->subIdx >= 0) {
            g_balls[victim->subIdx].active = false;
        }
        if (victim->type == TYPE_CELL && victim->cellStage == 2) {
            Vector2 deathPos = victim->pos;
            int team = victim->team;
            victim->active = false;
            SpawnCellSplit(deathPos, team);
            return;
        }
        victim->active = false;
        return;
    }

    if (victim->type != TYPE_VERITY) return;

    bool big = (raw >= 10);
    float chance = 0.0f;
    if (victim->verityForm == 1)
        chance = LerpChance(victim->hp, 100, 51, big ? 10.0f : 3.5f, big ? 30.0f : 17.0f);
    else if (victim->verityForm == 2)
        chance = LerpChance(victim->hp, 50, 1, big ? 8.0f : 2.5f, big ? 23.0f : 12.0f);
    else return;

    if (GetRandomValue(0, 99999) < (int)(chance * 1000.0f)) {
        if (victim->verityForm == 1) {
            victim->verityForm = 2;
            victim->damage = 10;
            victim->color = GOLD;
            victim->radius = (int)(BASE_RADIUS * 1.5f);
        } else {
            victim->verityForm = 3;
            victim->damage = 40;
            victim->color = MAROON;
            victim->radius = BASE_RADIUS * 2;
        }
        victim->pos = ClampScreen(victim->pos, (float)victim->radius);
        if (victim->frozen)      victim->freezePos   = victim->pos;
        if (victim->shocked)     victim->shockPos    = victim->pos;
        if (victim->windStunned) victim->windStunPos = victim->pos;
        if (victim->diceLocked)  victim->diceLockPos = victim->pos;
        if (victim->slamLocked)  victim->slamLockPos = victim->pos;
        if (victim->execLocked)  victim->execLockPos = victim->pos;
    }
}

static inline void DealDamage(Ball* victim, int dmg, bool bypass) { DealDamageScaled(victim, dmg, bypass, 1.0f); }
static inline void TakeDamage(Ball* victim, int dmg) { DealDamageScaled(victim, dmg, false, 1.0f); }
static inline void TakeDamageFrom(Ball* victim, const Ball* attacker, int dmg) { DealDamageScaled(victim, dmg, false, attacker ? attacker->dmgMult : 1.0f); }
static inline void TakeDamageScaled(Ball* victim, int dmg, float atkMult) { DealDamageScaled(victim, dmg, false, atkMult); }

static void HealBall(Ball* b, int amt)
{
    if (!IsCombatBall(b) || b->hp >= b->maxHp) return;
    if (amt > b->maxHp - b->hp) amt = b->maxHp - b->hp;
    b->hp += amt;
    AddPopup(b->pos, amt, true);
}

static void ApplyContact(int attIdx, int vicIdx)
{
    Ball* attacker = &g_balls[attIdx];
    Ball* victim   = &g_balls[vicIdx];

    if (!IsCombatBall(victim)) return;
    if (IsKingDashing(victim)) return;
    if (victim->frozen || victim->shocked || victim->diceLocked) return;
    // Victims captured by Executer's 3.0s Sion-Q charge deal 0 contact dmg and are immune to external contact dmg
    if (attacker->execLocked || victim->execLocked) return;

    int dmg = attacker->damage;

    // Shackless (TYPE_UNSHACKLED) Chain Lash passive:
    // If holding >= 1 stack (max 3, lasts 3.0s) and 0.25s delay between lashes has elapsed,
    // colliding with an enemy consumes 1 stack (-1 stack), resets the stack timer to 3.0s (if stacks > 0),
    // deals 10 HP, and reduces the enemy's movement speed by 10%
    if (attacker->type == TYPE_UNSHACKLED) {
        if (attacker->unshackled.passiveStacks > 0 && attacker->unshackled.lashCooldown <= 0.0f) {
            dmg = UNSHACKLED_LASH_DMG;
            attacker->unshackled.passiveStacks--;
            attacker->unshackled.passiveTimer = (attacker->unshackled.passiveStacks > 0) ? UNSHACKLED_STACK_TIME : 0.0f;
            attacker->unshackled.lashCooldown = UNSHACKLED_LASH_DELAY;
            attacker->unshackled.lashAnimTimer = 0.25f;
            attacker->unshackled.lashAngle = atan2f(victim->pos.y - attacker->pos.y, victim->pos.x - attacker->pos.x);
            victim->slowPTimer = 3.0f; // -10% movement speed
            Vector2 mid = V2Mul(V2Add(attacker->pos, victim->pos), 0.5f);
            AddFx(mid, (float)victim->radius * 1.15f, GOLD);
            for (int k = 0; k < 8; k++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                float sp = 2.0f + (float)GetRandomValue(0, 20) * 0.1f;
                SpawnParticle(mid, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.32f, 3.2f, GOLD, 0.04f, false);
            }
        } else {
            dmg = 0;
        }
    }

    // Knight: 0 direct contact damage, triggers a 0.10s delayed 30-degree cone slash toward the target
    if (attacker->type == TYPE_KNIGHT) {
        dmg = 0;
        if (!attacker->knight.windingUp) {
            attacker->knight.windingUp = true;
            attacker->knight.timer = 0.0f;
            attacker->knight.slashAngle = atan2f(victim->pos.y - attacker->pos.y, victim->pos.x - attacker->pos.x);
        }
    }

    // Cavalry: thrusts long iron lance on contact (0 delay, 100% hit)
    if (attacker->type == TYPE_CAVALRY) {
        attacker->cavalry.aimAngle = atan2f(victim->pos.y - attacker->pos.y, victim->pos.x - attacker->pos.x);
        attacker->cavalry.thrustTimer = 0.24f;
        if (attacker->cavalry.state == 1) {
            // Charging state: damage scales from 10 to 25 HP based on current speed (1.0x -> 1.6x),
            // sparks fly, and both Cavalry and the hit target are immobilized for 1.25s
            float speedFrac = ClampF((attacker->cavalry.speedMult - 1.0f) / (CAVALRY_MAX_MULT - 1.0f), 0.0f, 1.0f);
            dmg = (int)roundf((float)CAVALRY_MIN_CHG_DMG + speedFrac * (float)(CAVALRY_MAX_CHG_DMG - CAVALRY_MIN_CHG_DMG));
            Vector2 mid = V2Mul(V2Add(attacker->pos, victim->pos), 0.5f);
            SpawnLanceSparks(mid);

            attacker->cavalry.state = 2;
            attacker->cavalry.timer = CAVALRY_STUN_T;
            attacker->cavalry.speedMult = 1.0f;
            attacker->slamLocked = true;
            attacker->slamLockTimer = CAVALRY_STUN_T;
            attacker->slamLockPos = attacker->pos;

            if (!IsKingDashing(victim)) {
                victim->slamLocked = true;
                victim->slamLockTimer = CAVALRY_STUN_T;
                victim->slamLockPos = victim->pos;
            }
        } else if (attacker->cavalry.state == 0) {
            dmg = CAVALRY_BASE_DMG; // 12 HP normal lance thrust
        } else {
            dmg = 0;
        }
    }

    // Executer: heavy overhead axe chop on contact (10 HP, 2.25s cooldown, 0 dmg while on cooldown)
    if (attacker->type == TYPE_EXECUTER) {
        if (attacker->executer.chopCooldown <= 0.0f && attacker->executer.skillState == 0) {
            dmg = EXECUTER_CHOP_DMG;
            attacker->executer.chopCooldown = EXECUTER_CHOP_CD;
            attacker->executer.chopAnimTimer = 0.44f;
            attacker->executer.chopAngle = atan2f(victim->pos.y - attacker->pos.y, victim->pos.x - attacker->pos.x);
            victim->painFlashTimer = 0.50f;
            for (int q = 0; q < 10; q++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                float sp = 2.0f + (float)GetRandomValue(0, 25) * 0.1f;
                SpawnParticle(victim->pos, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.42f, 3.8f, COL_BLOOD, 0.06f, false);
            }
        } else {
            dmg = 0;
        }
    }

    // Gambler offensive buff: next contact deals (sum of 3 dice) * 3 damage
    // (When Gambler holds Ace, the combined (33% enemy dmg + sum*3) hit is applied in the Ace block below)
    if (attacker->type == TYPE_GAMBLER) {
        if (attacker->gambler.hasCardBuff && attacker->gambler.cardRank == 1 && attacker->gambler.nextHitDmg > 0) {
            dmg = 0; // deferred to Ace reflection block so total damage is 33% enemy dmg + (sum of dice * 3)
        } else if (attacker->gambler.nextHitDmg > 0) {
            dmg = attacker->gambler.nextHitDmg;
            attacker->gambler.nextHitDmg = 0;
            AddFx(victim->pos, (float)victim->radius * 1.35f, GOLD);
        }
        if (attacker->gambler.waitingForHit) {
            attacker->gambler.waitingForHit = false;
            attacker->gambler.timer = GAMBLER_COOLDOWN;
        }
    }

    // Knight defensive shield passive: reduces incoming contact damage by 5%..40%
    if (victim->type == TYPE_KNIGHT && dmg > 0) {
        float kRed = (float)GetRandomValue(5, 40) * 0.01f;
        dmg = (int)roundf((float)dmg * (1.0f - kRed));
        if (dmg < 1) dmg = 1;
    }

    if (victim->type == TYPE_GAMBLER) {
        if (victim->gambler.waitingForHit) {
            victim->gambler.waitingForHit = false;
            victim->gambler.timer = GAMBLER_COOLDOWN;
        }
        // Gambler defensive card buff on next incoming contact:
        // - Card 2..10: reduces incoming damage by 10%..50% linearly
        // - Card J, Q, K (11..13): takes 0 damage
        // - Card Ace (1): takes 0 damage and deals (33% of enemy's contact damage + sum of 3 dice * 3) to attacker
        if (victim->gambler.hasCardBuff && victim->gambler.cardRank > 0) {
            int rank = victim->gambler.cardRank;
            if (rank == 1) {
                victim->gambler.hasCardBuff = false;
                victim->gambler.cardRank = 0;
                int bonusDiceDmg = victim->gambler.nextHitDmg;
                victim->gambler.nextHitDmg = 0;
                int aceTotalDmg = (int)roundf((float)dmg * 0.33f) + bonusDiceDmg;
                if (aceTotalDmg < 1) aceTotalDmg = 1;
                TakeDamageFrom(attacker, victim, aceTotalDmg);
                AddFx(attacker->pos, (float)attacker->radius * 1.35f, GOLD);
                AddFx(victim->pos, (float)victim->radius * 1.25f, GOLD);
                dmg = 0;
            } else if (dmg > 0) {
                victim->gambler.hasCardBuff = false;
                victim->gambler.cardRank = 0;
                if (rank >= 11) { // J, Q, K: take 0 damage
                    AddFx(victim->pos, (float)victim->radius * 1.2f, SKYBLUE);
                    dmg = 0;
                } else if (rank >= 2 && rank <= 10) {
                    float redPct = 0.10f + ((float)(rank - 2) / 8.0f) * 0.40f; // 10%..50%
                    dmg = (int)roundf((float)dmg * (1.0f - redPct));
                    if (dmg < 1) dmg = 1;
                }
            }
        }
    }

    if (dmg > 0 && IsCombatBall(victim)) {
        TakeDamageFrom(victim, attacker, dmg);
    }

    // Charge resets its size and contact damage back to initial values after hitting an enemy
    if (attacker->type == TYPE_CHARGE && attacker->charge.stacks > 0) {
        attacker->charge.stacks = 0;
        attacker->charge.timer = CHARGE_INTERVAL;
        attacker->charge.exactRadius = (float)BASE_RADIUS;
        attacker->charge.exactDamage = (float)CHARGE_BASE_DMG;
        attacker->radius = BASE_RADIUS;
        attacker->damage = CHARGE_BASE_DMG;
        AddFx(victim->pos, (float)victim->radius * 1.3f, SKYBLUE);
    }
    if (!IsCombatBall(victim) || !IsCombatBall(attacker)) return;

    if (attacker->type == TYPE_DEVIL && attacker->drainTicks <= 0) {
        attacker->drainTicks = 3;
        attacker->drainTick = 0.5f;
        attacker->drainAmt = (attacker->hp < 30) ? 6 : (attacker->hp < 60) ? 5 : 4;
        attacker->drainTargetIdx = vicIdx;

        Vector2 n = Norm(V2Sub(victim->pos, attacker->pos));
        attacker->linkOffset = V2Mul(n, (float)(attacker->radius + victim->radius));
    }

    if (attacker->type == TYPE_ELECTRIC && attacker->charged) {
        attacker->charged = false;
        attacker->chargeTimer = ELEC_CHARGE_TIME;
        victim->shocked = true;
        victim->shockTicks = SHOCK_TICKS;
        victim->shockTick = SHOCK_TICK_TIME;
        victim->shockPos = victim->pos;
        victim->shockDmgMult = attacker->dmgMult;
    }
}

static void ReflectAway(Ball* b, float nx, float ny)
{
    Vector2 v = b->vel;
    float dot = v.x * nx + v.y * ny;
    if (dot < 0.0f) { v.x -= 2.0f * dot * nx; v.y -= 2.0f * dot * ny; }
    SetDirection(b, Norm(v));
}

static void Freeze(Ball* t, float duration)
{
    if (!IsCombatBall(t) || IsKingDashing(t)) return;
    t->frozen = true;
    t->frozenTimer = duration;
    t->freezeDmgTimer = 0.5f;
    t->freezePos = t->pos;
}

static void StunWithWind(Ball* t, float duration)
{
    if (!IsCombatBall(t) || t->shocked || IsKingDashing(t)) return;
    t->windStunned = true;
    t->windStunTimer = duration;
    t->windStunPos = t->pos;
}

static float IceFreezeDuration(const Ball* ice, const Ball* target)
{
    float best = 0.0f;
    float rTargetSq = (float)(target->radius * target->radius);
    float rIce = (float)ice->radius;
    float broadR = target->radius + ICE_HALF_L + rIce;
    float broadRSq = broadR * broadR;

    for (int k = 0; k < ICE_MAX; k++)
    {
        const IceSeg* s = &ice->ice[k];
        if (!s->active) continue;

        float dx = target->pos.x - s->pos.x;
        float dy = target->pos.y - s->pos.y;
        if (dx * dx + dy * dy > broadRSq) continue;

        float c = cosf(s->angle), sn = sinf(s->angle);
        float lx =  dx * c + dy * sn;
        float ly = -dx * sn + dy * c;

        float cx = ClampF(lx, -ICE_HALF_L, ICE_HALF_L);
        float cy = ClampF(ly, -rIce, rIce);
        float ddx = lx - cx, ddy = ly - cy;

        if (ddx * ddx + ddy * ddy <= rTargetSq) {
            float d = (s->age < 1.0f) ? 1.5f : (s->age < 2.0f) ? 1.0f : 0.5f;
            if (d > best) best = d;
        }
    }
    return best;
}

// ======================= SKILL UPDATES =======================
static void UpdateBurst(Ball* b, float dt)
{
    if (b->isDashing) {
        b->durationTimer -= dt;
        if (b->durationTimer <= 0.0f) {
            b->isDashing = false;
            b->vel = b->normalVel;
            b->damage = BURST_BASE_DMG;
        }
    } else {
        b->skillTimer -= dt;
        if (b->skillTimer <= BURST_PREFIRE_TIME && b->skillTimer > 0.0f) {
            for (int k = 0; k < 2; k++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                Vector2 p = { b->pos.x + cosf(a) * b->radius, b->pos.y + sinf(a) * b->radius };
                Vector2 v = { cosf(a) * 0.8f + (float)GetRandomValue(-10, 10) * 0.08f,
                              -1.8f - (float)GetRandomValue(0, 15) * 0.1f };
                Color fc = (GetRandomValue(0, 1) == 0) ? (Color){ 255, 80, 0, 255 } : GOLD;
                SpawnParticle(p, v, 0.25f, 4.5f, fc, -0.05f, false);
            }
        }
        if (b->skillTimer <= 0.0f) {
            b->isDashing = true;
            b->durationTimer = BURST_DURATION;
            b->skillTimer = BURST_COOLDOWN;
            b->damage = BURST_DASH_DMG;
            b->vel = V2Mul(b->normalVel, DASH_MULT);
        }
    }
}

static void BounceHarpoonHookUnpredictable(HarpoonData* h)
{
    bool hitX = false, hitY = false;
    if (h->pos.x <= 0.0f)          { h->pos.x = 0.0f;     h->vel.x =  fabsf(h->vel.x); hitX = true; }
    else if (h->pos.x >= SCREEN_W) { h->pos.x = SCREEN_W; h->vel.x = -fabsf(h->vel.x); hitX = true; }
    if (h->pos.y <= 0.0f)          { h->pos.y = 0.0f;     h->vel.y =  fabsf(h->vel.y); hitY = true; }
    else if (h->pos.y >= SCREEN_H) { h->pos.y = SCREEN_H; h->vel.y = -fabsf(h->vel.y); hitY = true; }

    if (hitX || hitY) {
        float baseAngle = atan2f(h->vel.y, h->vel.x);
        float jitter = (float)GetRandomValue(-38, 38) * DEG2RAD;
        float newAngle = baseAngle + jitter;
        Vector2 d = { cosf(newAngle), sinf(newAngle) };

        if (h->pos.x <= 1.0f && d.x < 0.25f)             d.x = 0.35f;
        if (h->pos.x >= SCREEN_W - 1.0f && d.x > -0.25f) d.x = -0.35f;
        if (h->pos.y <= 1.0f && d.y < 0.25f)             d.y = 0.35f;
        if (h->pos.y >= SCREEN_H - 1.0f && d.y > -0.25f) d.y = -0.35f;
        d = Norm(d);
        h->vel = V2Mul(d, HARPOON_SPEED);
    }
}

static void UpdateHarpoon(int bIdx, float dt)
{
    Ball* b = &g_balls[bIdx];
    HarpoonData* h = &b->harpoon;
    Ball* enemy = FindNearestEnemy(b);

    if (h->state == H_READY) {
        b->skillTimer -= dt;
        if (b->skillTimer <= 0.0f && enemy) {
            h->state = H_SHOOTING;
            h->pos = b->pos;
            h->trailCount = 0;
            h->trailCursor = 0.0f;
            h->victimIdx = -1;
            h->anchorPos = b->pos;
            h->firstShotDone = true;

            // Zero aim bias on Harpoon hook: completely random launch angle
            float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
            h->vel = (Vector2){ cosf(angle) * HARPOON_SPEED, sinf(angle) * HARPOON_SPEED };
        }
    }

    if (h->state != H_READY) {
        b->pos = h->anchorPos;
        b->vel = b->normalVel = (Vector2){ 0, 0 };
    }

    if (h->state == H_SHOOTING)
    {
        h->pos = V2Add(h->pos, h->vel);
        if (h->trailCount < TRAIL_MAX - 1) {
            h->trail[h->trailCount++] = h->pos;
        }

        BounceHarpoonHookUnpredictable(h);

        for (int j = 0; j < g_ballCount; j++) {
            Ball* cand = &g_balls[j];
            if (!IsCombatBall(cand) || cand->team == b->team || IsKingDashing(cand)) continue;
            float hitR = cand->radius + HARPOON_HIT_BONUS;
            if (V2DistSq(h->pos, cand->pos) <= hitR * hitR) {
                h->state = H_PULLING;
                h->victimIdx = j;
                h->trailCursor = (float)h->trailCount;
                // Deal 1 damage immediately upon hook impact, then wait 0.05s for the next tick
                TakeDamageFrom(cand, b, 1);
                h->pullDmgTimer = HARPOON_PULL_TICK;
                break;
            }
        }
    }
    else if (h->state == H_PULLING)
    {
        Ball* vic = (h->victimIdx >= 0 && IsCombatBall(&g_balls[h->victimIdx]) && !IsKingDashing(&g_balls[h->victimIdx]))
                    ? &g_balls[h->victimIdx] : NULL;
        if (!vic) {
            h->state = H_READY;
            b->skillTimer = 3.0f;
            SetRandomDirection(b);
            return;
        }

        if (h->trailCursor > 0.0f) {
            h->trailCursor -= HARPOON_PULL_STEP;
            if (h->trailCursor < 0.0f) h->trailCursor = 0.0f;
            h->trailCount = (int)h->trailCursor;

            h->pos = h->trail[h->trailCount];
            vic->pos = vic->freezePos = vic->shockPos = vic->windStunPos = vic->slamLockPos = vic->execLockPos = h->pos;
            vic->vel = vic->normalVel = (Vector2){ 0, 0 };

            h->pullDmgTimer -= dt;
            while (h->pullDmgTimer <= 0.0f && IsCombatBall(vic)) {
                TakeDamageFrom(vic, b, 1);
                h->pullDmgTimer += HARPOON_PULL_TICK;
            }
        } else {
            h->state = H_READY;
            b->skillTimer = 3.0f;
            SetRandomDirection(b);
            if (IsCombatBall(vic)) SetRandomDirection(vic);
            g_wasColliding[bIdx][h->victimIdx] = false;
            g_wasColliding[h->victimIdx][bIdx] = false;
            h->victimIdx = -1;
        }
    }
}

static void UpdateDevil(Ball* b, float dt)
{
    if (b->drainTicks <= 0) return;
    if (b->drainTargetIdx < 0 || !IsCombatBall(&g_balls[b->drainTargetIdx])) {
        b->drainTicks = 0;
        b->drainTargetIdx = -1;
        return;
    }
    Ball* enemy = &g_balls[b->drainTargetIdx];

    if (GetRandomValue(0, 99) < 60) {
        float a = (float)GetRandomValue(0, 359) * DEG2RAD;
        Vector2 p = { enemy->pos.x + cosf(a) * enemy->radius,
                      enemy->pos.y + sinf(a) * enemy->radius };
        SpawnParticle(p, (Vector2){ (float)GetRandomValue(-5, 5) * 0.1f, 0.3f },
                      0.7f, 3.0f, COL_BLOOD, 0.12f, false);
    }

    b->drainTick -= dt;
    if (b->drainTick <= 0.0f) {
        b->drainTick += 0.5f;
        b->drainTicks--;
        int amt = (b->drainAmt > enemy->hp) ? enemy->hp : b->drainAmt;
        if (amt > 0 && !enemy->shocked) {
            TakeDamageFrom(enemy, b, amt);
            b->hp += amt;
            AddPopup(b->pos, amt, true);

            Vector2 toD = Norm(V2Sub(b->pos, enemy->pos));
            for (int q = 0; q < 8; q++) {
                float sp = 3.0f + (float)GetRandomValue(0, 20) * 0.1f;
                Vector2 v = { toD.x * sp + (float)GetRandomValue(-10, 10) * 0.1f,
                              toD.y * sp + (float)GetRandomValue(-10, 10) * 0.1f };
                SpawnParticle(enemy->pos, v, 0.5f, 3.5f, COL_BLOOD, 0.0f, false);
            }
        }

        if (b->drainTicks == 0) {
            Vector2 n = Norm(b->linkOffset);
            SetDirection(b, V2Mul(n, -1.0f));
            b->boostTimer = BOOST_TIME;
            if (IsCombatBall(enemy) && !IsFixed(enemy)) {
                SetDirection(enemy, n);
                enemy->boostTimer = BOOST_TIME;
            }
            b->drainTargetIdx = -1;
        }
    }
}

static void UpdateTrident(Ball* b, float dt)
{
    TridentData* t = &b->trident;
    Ball* enemy = FindNearestEnemy(b);

    switch (t->state)
    {
        case T_READY:
            if (enemy) t->aimAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
            t->timer -= dt;
            if (t->timer <= 0.0f && enemy)
            {
                float ex = enemy->pos.x - b->pos.x, ey = enemy->pos.y - b->pos.y;
                float dist = sqrtf(ex * ex + ey * ey);
                float maxErr = 40.0f * fminf(dist / (SCREEN_W / 2.0f), 1.0f);
                float err = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * maxErr;
                t->aimAngle += err * DEG2RAD;
                t->pos = (Vector2){ b->pos.x + cosf(t->aimAngle) * b->radius,
                                    b->pos.y + sinf(t->aimAngle) * b->radius };
                t->travelled = 0.0f;
                memset(t->hit, 0, sizeof(t->hit));
                t->state = T_FLYING;
            }
            break;

        case T_FLYING: {
            float step = fminf(TRIDENT_SPEED, TRIDENT_RANGE - t->travelled);
            t->pos.x += cosf(t->aimAngle) * step;
            t->pos.y += sinf(t->aimAngle) * step;
            t->travelled += step;

            Vector2 clamped = ClampScreen(t->pos, 0.0f);
            bool outside = (clamped.x != t->pos.x || clamped.y != t->pos.y);
            t->pos = clamped;

            if (outside || t->travelled >= TRIDENT_RANGE - 0.01f) {
                t->state = T_LANDED;
                t->timer = TRIDENT_LAND_TIME;
            }
        } break;

        case T_LANDED:
            t->timer -= dt;
            if (t->timer <= 0.0f) {
                t->state = T_RETURNING;
                memset(t->hit, 0, sizeof(t->hit));
            }
            break;

        case T_RETURNING: {
            Vector2 d = Norm(V2Sub(b->pos, t->pos));
            t->pos = V2Add(t->pos, V2Mul(d, TRIDENT_SPEED));

            float catchR = b->radius + 10.0f;
            if (V2DistSq(b->pos, t->pos) <= catchR * catchR) {
                t->state = T_READY;
                t->timer = TRIDENT_COOLDOWN;
            }
        } break;
    }

    if (t->state == T_FLYING || t->state == T_RETURNING)
    {
        for (int j = 0; j < g_ballCount; j++) {
            Ball* cand = &g_balls[j];
            if (!IsCombatBall(cand) || cand->team == b->team || t->hit[j]) continue;
            float hitR = cand->radius + 8.0f;
            if (V2DistSq(t->pos, cand->pos) <= hitR * hitR) {
                TakeDamageFrom(cand, b, TRIDENT_DAMAGE);
                t->hit[j] = true;
            }
        }
    }
}

// Bow: shoots white arrow every 5.0s, +5% accuracy at all distances (97% close -> 45% at max range = 3/4 screen width)
static void UpdateBow(Ball* b, float dt)
{
    BowData* bw = &b->bow;
    Ball* enemy = FindNearestEnemy(b);

    if (enemy) {
        bw->aimAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
    }

    b->skillTimer -= dt;
    if (b->skillTimer <= 0.0f && !bw->active && enemy) {
        b->skillTimer = BOW_COOLDOWN;
        bw->active = true;
        bw->travelled = 0.0f;

        float angle = AimAngleWithDistanceFalloff(b->pos, enemy, BOW_ARROW_SPEED,
                                                  BOW_ARROW_RANGE, 97.0f, 45.0f);
        bw->angle = angle;
        bw->pos = b->pos;
        bw->vel = (Vector2){ cosf(angle) * BOW_ARROW_SPEED, sinf(angle) * BOW_ARROW_SPEED };
    }

    if (bw->active) {
        bw->pos = V2Add(bw->pos, bw->vel);
        bw->travelled += BOW_ARROW_SPEED;

        Vector2 tip = { bw->pos.x + cosf(bw->angle) * (BOW_ARROW_LEN * 0.5f),
                        bw->pos.y + sinf(bw->angle) * (BOW_ARROW_LEN * 0.5f) };

        if (tip.x <= 0 || tip.x >= SCREEN_W || tip.y <= 0 || tip.y >= SCREEN_H ||
            bw->pos.x <= 0 || bw->pos.x >= SCREEN_W || bw->pos.y <= 0 || bw->pos.y >= SCREEN_H ||
            bw->travelled >= BOW_ARROW_RANGE) {
            bw->active = false;
            return;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* cand = &g_balls[j];
            if (!IsCombatBall(cand) || cand->team == b->team) continue;
            float hitR = cand->radius + 6.0f;
            if (V2DistSq(tip, cand->pos) <= hitR * hitR || V2DistSq(bw->pos, cand->pos) <= hitR * hitR) {
                TakeDamageFrom(cand, b, BOW_ARROW_DMG);
                bw->active = false;
                break;
            }
        }
    }
}

// Ninja: accuracy drops with distance down to 70% at max range (SCREEN_W)
static void UpdateNinja(Ball* b, float dt)
{
    NinjaData* nj = &b->ninja;
    Ball* enemy = FindNearestEnemy(b);
    if (enemy) {
        nj->aimAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
    }

    switch (nj->state)
    {
        case N_WAIT:
            nj->timer -= dt;
            if (nj->timer <= 0.0f) {
                nj->targetMaxKunai = ComputeNinjaKunaiCount(b->hp);
                nj->currentKunai = 0;
                nj->stepTime = NINJA_RELOAD_WINDOW / (float)nj->targetMaxKunai;
                nj->timer = nj->stepTime;
                nj->state = N_RELOADING;
            }
            break;

        case N_RELOADING:
            nj->timer -= dt;
            while (nj->timer <= 0.0f && nj->currentKunai < nj->targetMaxKunai) {
                nj->currentKunai++;
                if (nj->currentKunai < nj->targetMaxKunai) {
                    nj->timer += nj->stepTime;
                } else {
                    nj->state = N_THROWING;
                    nj->timer = 0.08f;
                    break;
                }
            }
            break;

        case N_THROWING:
            nj->timer -= dt;
            if (nj->timer <= 0.0f && nj->currentKunai > 0) {
                if (enemy) {
                    float angle = AimAngleWithDistanceFalloff(b->pos, enemy, KUNAI_SPEED,
                                                              KUNAI_RANGE, 96.0f, 80.0f);
                    Kunai* kn = &g_kunais[g_kunaiNext];
                    g_kunaiNext = (g_kunaiNext + 1) % KUNAI_MAX;
                    *kn = (Kunai){
                        .pos = b->pos,
                        .vel = { cosf(angle) * KUNAI_SPEED, sinf(angle) * KUNAI_SPEED },
                        .angle = angle,
                        .travelled = 0.0f,
                        .team = b->team,
                        .dmgMult = b->dmgMult,
                        .active = true
                    };
                }
                nj->currentKunai--;
                if (nj->currentKunai > 0) {
                    nj->timer = NINJA_THROW_GAP;
                } else {
                    nj->state = N_WAIT;
                    nj->timer = NINJA_WAIT_TIME;
                }
            }
            break;
    }
}

static void UpdateCell(Ball* b, float dt)
{
    if (b->cellStage == 1) {
        b->skillTimer -= dt;
        if (b->skillTimer <= 0.0f) {
            b->cellStage = 2;
            b->radius = BASE_RADIUS;
            b->pos = ClampScreen(b->pos, (float)b->radius);
            AddFx(b->pos, (float)b->radius * 1.25f, COL_CELL);
        }
    }
}

// ======================= GUN (6-BARREL GATLING) =======================
// Phase 0 (Normal):   1 bullet every 0.15s for 2.25s (accuracy 55% at max range)
// Phase 1 (Pre-wait): waits 0.50s after normal fire before rapid fire
// Phase 2 (Rapid):    1 bullet every 0.05s for 1.25s (accuracy 40% at max range, barrel heats up pale -> deep yellow-orange)
// Phase 3 (Cooldown): 2.50s overheat cooldown (no shooting, barrel cools down back to normal dark gray-black)
static void UpdateGun(Ball* b, float dt)
{
    GunData* g = &b->gun;
    Ball* enemy = FindNearestEnemy(b);
    if (enemy) {
        g->aimAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
    }

    g->phaseTimer -= dt;

    if (g->phase == 0) {
        // Phase 0: Normal fire (2.25s)
        g->heat = 0.0f;
        g->barrelSpin += 10.0f * dt;
        g->shotTimer -= dt;
        while (g->shotTimer <= 0.0f) {
            g->shotTimer += GUN_NORM_INTERVAL;
            if (enemy) {
                float angle = AimAngleWithDistanceFalloff(b->pos, enemy, GUN_BULLET_SPEED,
                                                          GUN_BULLET_RANGE, 92.0f, GUN_NORM_MIN_HIT);
                Vector2 muzzle = { b->pos.x + cosf(g->aimAngle) * (b->radius + 18.0f),
                                   b->pos.y + sinf(g->aimAngle) * (b->radius + 18.0f) };
                GunBullet* gb = &g_bullets[g_bulletNext];
                g_bulletNext = (g_bulletNext + 1) % GUN_BULLET_MAX;
                *gb = (GunBullet){
                    .pos = muzzle,
                    .vel = { cosf(angle) * GUN_BULLET_SPEED, sinf(angle) * GUN_BULLET_SPEED },
                    .travelled = 0.0f,
                    .team = b->team,
                    .dmgMult = b->dmgMult,
                    .active = true
                };
            }
        }
        if (g->phaseTimer <= 0.0f) {
            g->phase = 1;
            g->phaseTimer = GUN_PRE_RAPID_WAIT;
        }
    } else if (g->phase == 1) {
        // Phase 1: 0.50s spin-up delay before rapid fire
        g->heat = 0.0f;
        g->barrelSpin += 18.0f * dt;
        if (g->phaseTimer <= 0.0f) {
            g->phase = 2;
            g->phaseTimer = GUN_RAPID_DURATION;
            g->shotTimer = GUN_RAPID_INTERVAL;
        }
    } else if (g->phase == 2) {
        // Phase 2: 1.25s rapid fire (barrel heats up from 0.0 -> 1.0)
        g->barrelSpin += 28.0f * dt;
        g->heat = ClampF(1.0f - (g->phaseTimer / GUN_RAPID_DURATION), 0.0f, 1.0f);
        g->shotTimer -= dt;
        while (g->shotTimer <= 0.0f) {
            g->shotTimer += GUN_RAPID_INTERVAL;
            if (enemy) {
                float angle = AimAngleWithDistanceFalloff(b->pos, enemy, GUN_BULLET_SPEED,
                                                          GUN_BULLET_RANGE, 78.0f, GUN_RAPID_MIN_HIT);
                Vector2 muzzle = { b->pos.x + cosf(g->aimAngle) * (b->radius + 18.0f),
                                   b->pos.y + sinf(g->aimAngle) * (b->radius + 18.0f) };
                GunBullet* gb = &g_bullets[g_bulletNext];
                g_bulletNext = (g_bulletNext + 1) % GUN_BULLET_MAX;
                *gb = (GunBullet){
                    .pos = muzzle,
                    .vel = { cosf(angle) * GUN_BULLET_SPEED, sinf(angle) * GUN_BULLET_SPEED },
                    .travelled = 0.0f,
                    .team = b->team,
                    .dmgMult = b->dmgMult,
                    .active = true
                };
            }
        }
        if (g->phaseTimer <= 0.0f) {
            g->phase = 3;
            g->phaseTimer = GUN_COOLDOWN_DURATION;
            g->heat = 1.0f;
        }
    } else {
        // Phase 3: 2.50s overheat cooldown (no bullets fired; barrel cools from 1.0 -> 0.0)
        g->heat = ClampF(g->phaseTimer / GUN_COOLDOWN_DURATION, 0.0f, 1.0f);
        g->barrelSpin += 3.5f * g->heat * dt;
        if (g->phaseTimer <= 0.0f) {
            g->heat = 0.0f;
            g->phase = 0;
            g->phaseTimer = GUN_NORM_DURATION;
            g->shotTimer = GUN_NORM_INTERVAL;
        }
    }
}

// ======================= CACTUS (SPIKE) AI & PROJECTILES =======================
static void ExplodeCactusBomb(Vector2 pos, int team, float dmgMult)
{
    bool curveball = (GetRandomValue(1, 100) <= 20);
    float maxR = curveball ? CACTUS_CURVE_RANGE : CACTUS_SPIKE_RANGE;

    AddFx(pos, (float)CACTUS_BOMB_RAD * 1.4f, COL_CACTUS);

    for (int k = 0; k < 8; k++) {
        float a = k * (2.0f * PI / 8.0f);
        CactusSpike* sp = &g_cactusSpikes[g_cactusSpikeNext];
        g_cactusSpikeNext = (g_cactusSpikeNext + 1) % CACTUS_SPIKE_MAX;
        *sp = (CactusSpike){
            .origin = pos,
            .pos = pos,
            .baseAngle = a,
            .angle = a,
            .dist = 0.0f,
            .maxDist = maxR,
            .curving = curveball,
            .team = team,
            .dmgMult = dmgMult,
            .active = true
        };
    }
}

static void FireSingleCactusBomb(Ball* b, const Ball* enemy)
{
    // Cactus does NOT suffer from distance accuracy dropoff
    float dist = sqrtf(V2DistSq(enemy->pos, b->pos));
    float tFrames = dist / CACTUS_BOMB_SPEED;
    Vector2 pred = V2Add(enemy->pos, V2Mul(enemy->vel, tFrames * 0.7f));
    float angle = atan2f(pred.y - b->pos.y, pred.x - b->pos.x);
    angle += ((float)GetRandomValue(-20, 20) * 0.1f) * DEG2RAD;

    CactusBomb* cb = &g_cactusBombs[g_cactusBombNext];
    g_cactusBombNext = (g_cactusBombNext + 1) % CACTUS_BOMB_MAX;
    *cb = (CactusBomb){
        .pos = b->pos,
        .vel = { cosf(angle) * CACTUS_BOMB_SPEED, sinf(angle) * CACTUS_BOMB_SPEED },
        .travelled = 0.0f,
        .team = b->team,
        .dmgMult = b->dmgMult,
        .active = true
    };
}

static void UpdateCactus(Ball* b, float dt)
{
    CactusData* c = &b->cactus;

    if (c->ammo < CACTUS_MAX_AMMO) {
        c->reloadTimer -= dt;
        if (c->reloadTimer <= 0.0f) {
            c->ammo++;
            c->reloadTimer = CACTUS_RELOAD_TIME;
        }
    }

    if (c->fireDelayTimer > 0.0f) c->fireDelayTimer -= dt;
    c->aiDecisionTimer -= dt;

    Ball* enemy = FindNearestEnemy(b);
    if (!enemy) return;

    float dist = sqrtf(V2DistSq(enemy->pos, b->pos));
    float effectiveRange = CACTUS_BOMB_RANGE + CACTUS_SPIKE_RANGE;
    bool enemyImmobilized = IsFixed(enemy);

    if (c->burstQueued > 0 && c->ammo > 0 && c->fireDelayTimer <= 0.0f) {
        FireSingleCactusBomb(b, enemy);
        c->ammo--;
        c->burstQueued--;
        c->fireDelayTimer = (c->burstQueued > 0) ? CACTUS_BURST_DELAY : 0.65f;
        return;
    }

    if (c->aiDecisionTimer <= 0.0f && c->fireDelayTimer <= 0.0f && c->ammo > 0) {
        c->aiDecisionTimer = 0.22f;

        if (dist > effectiveRange * 1.18f && c->ammo < CACTUS_MAX_AMMO) {
            c->aiMode = CACTUS_HOLD;
        } else if (enemyImmobilized && dist <= effectiveRange) {
            c->aiMode = CACTUS_DUMP;
            int shotsToDump = (enemy->hp <= 25) ? c->ammo : (c->ammo >= 2 ? 2 : 1);
            FireSingleCactusBomb(b, enemy);
            c->ammo--;
            c->burstQueued = shotsToDump - 1;
            c->fireDelayTimer = (c->burstQueued > 0) ? CACTUS_BURST_DELAY : 0.7f;
        } else if (dist <= CACTUS_BOMB_RANGE * 0.75f && c->ammo >= 2) {
            if (GetRandomValue(1, 100) <= 60) {
                c->aiMode = CACTUS_DUMP;
                FireSingleCactusBomb(b, enemy);
                c->ammo--;
                c->burstQueued = 1;
                c->fireDelayTimer = CACTUS_BURST_DELAY;
            } else {
                c->aiMode = CACTUS_POKE;
                FireSingleCactusBomb(b, enemy);
                c->ammo--;
                c->fireDelayTimer = 0.85f;
            }
        } else if (dist <= effectiveRange && (c->ammo >= 2 || dist <= CACTUS_BOMB_RANGE * 0.9f)) {
            c->aiMode = CACTUS_POKE;
            FireSingleCactusBomb(b, enemy);
            c->ammo--;
            c->fireDelayTimer = (c->ammo == CACTUS_MAX_AMMO - 1) ? 0.65f : 1.05f;
        } else {
            c->aiMode = CACTUS_HOLD;
        }
    }
}

// ======================= WIND: TORNADO SKILL =======================
static void UpdateWind(Ball* b, float dt)
{
    b->skillTimer -= dt;
    if (b->skillTimer > 0.0f) return;

    Ball* enemy = FindNearestEnemy(b);
    if (!enemy) return;

    b->skillTimer = WIND_INTERVAL;

    // Wind does NOT suffer from distance accuracy dropoff
    float dist = sqrtf(V2DistSq(enemy->pos, b->pos));
    float tFrames = dist / TORNADO_SPEED;
    Vector2 pred = V2Add(enemy->pos, V2Mul(enemy->vel, tFrames * 0.75f));
    float angle = atan2f(pred.y - b->pos.y, pred.x - b->pos.x);

    WindTornado* wt = &g_tornados[g_tornadoNext];
    g_tornadoNext = (g_tornadoNext + 1) % TORNADO_MAX;
    memset(wt, 0, sizeof(*wt));
    wt->pos = b->pos;
    wt->vel = (Vector2){ cosf(angle) * TORNADO_SPEED, sinf(angle) * TORNADO_SPEED };
    wt->spinAngle = 0.0f;
    wt->team = b->team;
    wt->dmgMult = b->dmgMult;
    wt->active = true;
}

// ======================= CHESS: QUEEN / ROOK / KNIGHT / BISHOP =======================
static bool IsAnyChessActive(void)
{
    for (int i = 0; i < g_ballCount; i++) {
        if (IsCombatBall(&g_balls[i]) && g_balls[i].type == TYPE_CHESS && g_balls[i].chess.active)
            return true;
    }
    return false;
}

static int ChessNumRays(ChessPiece p)
{
    return (p == CHESS_ROOK || p == CHESS_BISHOP) ? 4 : 8;
}

static Vector2 ChessRayDir(ChessPiece p, int idx)
{
    static const Vector2 ortho[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    static const float s = 0.70710678f;
    static const Vector2 diag[4]  = { { s, s }, { -s, s }, { s, -s }, { -s, -s } };

    if (p == CHESS_ROOK)   return ortho[idx % 4];
    if (p == CHESS_BISHOP) return diag[idx % 4];
    return (idx < 4) ? ortho[idx] : diag[idx - 4];
}

static void ChessKnightLegs(int idx, Vector2* dir1, float* len1, Vector2* dir2, float* len2)
{
    static const Vector2 d1[8] = {
        { 1, 0 }, { 1, 0 }, { -1, 0 }, { -1, 0 },
        { 0, 1 }, { 0, 1 }, {  0,-1 }, {  0,-1 }
    };
    static const Vector2 d2[8] = {
        { 0, 1 }, { 0,-1 }, { 0, 1 }, { 0,-1 },
        { 1, 0 }, {-1, 0 }, { 1, 0 }, {-1, 0 }
    };
    *dir1 = d1[idx % 8];
    *dir2 = d2[idx % 8];
    *len1 = CHESS_DIR_RANGE * (2.0f / 3.0f);
    *len2 = CHESS_DIR_RANGE * (1.0f / 3.0f);
}

static void RecordChessTrail(ChessData* cd, Vector2 pos)
{
    if (cd->trailCount == 0 || V2DistSq(cd->trail[cd->trailCount - 1], pos) >= 36.0f) {
        if (cd->trailCount < CHESS_TRAIL_MAX) {
            cd->trail[cd->trailCount++] = pos;
        } else {
            memmove(&cd->trail[0], &cd->trail[1], (CHESS_TRAIL_MAX - 1) * sizeof(Vector2));
            cd->trail[CHESS_TRAIL_MAX - 1] = pos;
        }
    }
}

static void UpdateChess(Ball* b, float dt)
{
    ChessData* cd = &b->chess;

    for (int j = 0; j < g_ballCount; j++) {
        if (b->chessHitImmune[j] > 0.0f) b->chessHitImmune[j] -= dt;
    }

    if (!cd->active) {
        cd->cooldown -= dt;
        if (cd->cooldown <= 0.0f) {
            cd->active = true;
            cd->piece = (ChessPiece)GetRandomValue(1, 4);
            cd->anchor = b->pos;
            cd->elapsed = 0.0f;
            cd->curRay = 0;
            cd->phase = 0;
            cd->rayDist = 0.0f;
            cd->legTurnPos = b->pos;
            cd->trailCount = 0;
            memset(cd->hitThisRay, 0, sizeof(cd->hitThisRay));
            memset(b->chessHitImmune, 0, sizeof(b->chessHitImmune));
            RecordChessTrail(cd, b->pos);
        }
        return;
    }

    cd->elapsed += dt;

    if (cd->elapsed < CHESS_PAUSE_TIME) {
        b->pos = cd->anchor;
        b->vel = (Vector2){ 0, 0 };
        return;
    }

    int numRays = ChessNumRays(cd->piece);
    float moveWindow = CHESS_DURATION - CHESS_PAUSE_TIME;
    float baseSpeedPerSec = (float)numRays * (2.0f * CHESS_DIR_RANGE) / moveWindow;
    // Brief burst of acceleration at the start of each ray (first 35% of outward travel) for a punchy feel
    float rayFrac = ClampF(cd->rayDist / CHESS_DIR_RANGE, 0.0f, 1.0f);
    float accelMult = (cd->phase == 0 && rayFrac < 0.35f) ? (1.65f - rayFrac * 1.2f) : 1.0f;
    float step = baseSpeedPerSec * accelMult * dt;

    float minX = (float)b->radius, maxX = (float)(SCREEN_W - b->radius);
    float minY = (float)b->radius, maxY = (float)(SCREEN_H - b->radius);

    while (step > 0.0001f && cd->curRay < numRays)
    {
        if (cd->piece != CHESS_KNIGHT)
        {
            Vector2 dir = ChessRayDir(cd->piece, cd->curRay);
            if (cd->phase == 0) {
                float rem = CHESS_DIR_RANGE - cd->rayDist;
                float s = fminf(step, rem);
                b->pos = V2Add(b->pos, V2Mul(dir, s));
                cd->rayDist += s;
                step -= s;

                bool hitWall = false;
                if (b->pos.x <= minX) { b->pos.x = minX; hitWall = true; }
                if (b->pos.x >= maxX) { b->pos.x = maxX; hitWall = true; }
                if (b->pos.y <= minY) { b->pos.y = minY; hitWall = true; }
                if (b->pos.y >= maxY) { b->pos.y = maxY; hitWall = true; }

                if (hitWall || cd->rayDist >= CHESS_DIR_RANGE - 0.01f) {
                    cd->phase = 1;
                }
            } else {
                Vector2 toAnchor = V2Sub(cd->anchor, b->pos);
                float distToAnchor = sqrtf(toAnchor.x * toAnchor.x + toAnchor.y * toAnchor.y);
                if (distToAnchor <= step + 0.5f) {
                    b->pos = cd->anchor;
                    step -= distToAnchor;
                    cd->curRay++;
                    cd->phase = 0;
                    cd->rayDist = 0.0f;
                    memset(cd->hitThisRay, 0, sizeof(cd->hitThisRay));
                } else {
                    b->pos = V2Add(b->pos, V2Mul(Norm(toAnchor), step));
                    step = 0.0f;
                }
            }
        }
        else
        {
            Vector2 d1, d2;
            float len1, len2;
            ChessKnightLegs(cd->curRay, &d1, &len1, &d2, &len2);

            if (cd->phase == 0) {
                float rem = len1 - cd->rayDist;
                float s = fminf(step, rem);
                b->pos = V2Add(b->pos, V2Mul(d1, s));
                cd->rayDist += s;
                step -= s;

                bool hitWall = false;
                if (b->pos.x <= minX) { b->pos.x = minX; hitWall = true; }
                if (b->pos.x >= maxX) { b->pos.x = maxX; hitWall = true; }
                if (b->pos.y <= minY) { b->pos.y = minY; hitWall = true; }
                if (b->pos.y >= maxY) { b->pos.y = maxY; hitWall = true; }

                if (hitWall) {
                    cd->phase = 3;
                } else if (cd->rayDist >= len1 - 0.01f) {
                    cd->legTurnPos = b->pos;
                    cd->phase = 1;
                    cd->rayDist = 0.0f;
                }
            } else if (cd->phase == 1) {
                float rem = len2 - cd->rayDist;
                float s = fminf(step, rem);
                b->pos = V2Add(b->pos, V2Mul(d2, s));
                cd->rayDist += s;
                step -= s;

                bool hitWall = false;
                if (b->pos.x <= minX) { b->pos.x = minX; hitWall = true; }
                if (b->pos.x >= maxX) { b->pos.x = maxX; hitWall = true; }
                if (b->pos.y <= minY) { b->pos.y = minY; hitWall = true; }
                if (b->pos.y >= maxY) { b->pos.y = maxY; hitWall = true; }

                if (hitWall || cd->rayDist >= len2 - 0.01f) {
                    cd->phase = 2;
                }
            } else if (cd->phase == 2) {
                Vector2 toCorner = V2Sub(cd->legTurnPos, b->pos);
                float d = sqrtf(toCorner.x * toCorner.x + toCorner.y * toCorner.y);
                if (d <= step + 0.5f) {
                    b->pos = cd->legTurnPos;
                    step -= d;
                    cd->phase = 3;
                } else {
                    b->pos = V2Add(b->pos, V2Mul(Norm(toCorner), step));
                    step = 0.0f;
                }
            } else {
                Vector2 toAnchor = V2Sub(cd->anchor, b->pos);
                float d = sqrtf(toAnchor.x * toAnchor.x + toAnchor.y * toAnchor.y);
                if (d <= step + 0.5f) {
                    b->pos = cd->anchor;
                    step -= d;
                    cd->curRay++;
                    cd->phase = 0;
                    cd->rayDist = 0.0f;
                    memset(cd->hitThisRay, 0, sizeof(cd->hitThisRay));
                } else {
                    b->pos = V2Add(b->pos, V2Mul(Norm(toAnchor), step));
                    step = 0.0f;
                }
            }
        }

        RecordChessTrail(cd, b->pos);

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == b->team || cd->hitThisRay[j] || b->chessHitImmune[j] > 0.0f) continue;
            float rsum = (float)(b->radius + e->radius);
            if (V2DistSq(b->pos, e->pos) <= rsum * rsum) {
                TakeDamageFrom(e, b, CHESS_SKILL_DMG);
                cd->hitThisRay[j] = true;
                // 0.65s per-target immunity prevents rapid consecutive rays at the anchor from melting an enemy
                b->chessHitImmune[j] = 0.65f;

                // Knockback push + brief speed boost away from Chess
                if (IsCombatBall(e) && !IsFixed(e)) {
                    Vector2 pushDir = Norm(V2Sub(e->pos, b->pos));
                    e->pos = ClampScreen(V2Add(e->pos, V2Mul(pushDir, 28.0f)), (float)e->radius);
                    SetDirection(e, pushDir);
                    e->boostTimer = 0.22f;
                }
            }
        }
    }

    if (cd->curRay >= numRays || cd->elapsed >= CHESS_DURATION) {
        b->pos = cd->anchor;
        cd->active = false;
        cd->piece = CHESS_NONE;
        cd->cooldown = CHESS_COOLDOWN;
        SetRandomDirection(b);
    }
}

// ======================= DICE, GAMBLER & PSYCHIC SKILLS =======================
// Dice: every 2.0s rolls a die for 1.0s (rotating 3 times across faces, shown prominently on screen);
// on completion: all enemies are anchored & invulnerable to external damage for 0.25s,
// red border flashes across the screen, and enemies take pip damage: 1->5, 2->7, 3->9, 4->12, 5->15, 6->18 HP.
// Once rolling has started, the animation continues even if Dice is stunned/frozen; if stunned during cooldown,
// the cooldown still ticks down to 0 and waits until Dice is free to start rolling.
static void UpdateDice(Ball* b, float dt, bool immobilized)
{
    DiceData* d = &b->dice;

    if (!d->rolling) {
        b->skillTimer -= dt;
        if (b->skillTimer < 0.0f) b->skillTimer = 0.0f;
        if (b->skillTimer <= 0.0f && !immobilized) {
            d->rolling = true;
            d->animTimer = 0.0f;
            d->rolledVal = GetRandomValue(1, 6);
            d->displayFace = GetRandomValue(1, 6);
        }
        return;
    }

    d->animTimer += dt;
    // Rotate 3 full turns during the first 0.72s, then lock onto rolledVal for the final 0.28s reveal
    if (d->animTimer < 0.72f) {
        d->spinAngle = (d->animTimer / 0.72f) * (3.0f * 2.0f * PI);
        int stepIdx = (int)(d->animTimer / 0.06f);
        d->displayFace = (stepIdx % 6) + 1;
    } else {
        d->spinAngle = 0.0f;
        d->displayFace = d->rolledVal;
    }

    if (d->animTimer >= DICE_ANIM_TIME) {
        static const int pipDmg[7] = { 0, 5, 7, 9, 12, 15, 18 };
        int dmg = pipDmg[d->rolledVal];

        d->rolling = false;
        b->skillTimer = DICE_COOLDOWN;
        g_diceRedFlashTimer = 0.45f; // red flash around the entire screen border

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == b->team || IsKingDashing(e)) continue;
            // Anchor & grant external damage immunity for 0.25s, then deal Dice pip damage (bypass=true)
            e->diceLocked = true;
            e->diceLockTimer = DICE_LOCK_TIME;
            e->diceLockPos = e->pos;
            DealDamageScaled(e, dmg, true, b->dmgMult);
        }
    }
}

// Gambler: rolls 3 dice sequentially over 1.5s, then draws 1 card (2..10, J, Q, K, A) for 0.75s;
// afterwards: gains +5% homing bias and +5% movement speed, and waits until colliding with an enemy before starting the next roll.
// Once phase > 0 has started, the animation continues uninterrupted even if Gambler is stunned/frozen.
static void UpdateGambler(Ball* b, float dt, bool immobilized)
{
    GamblerData* g = &b->gambler;

    if (g->phase == 0) {
        // Wait until Gambler has collided and consumed the previous roll's buff before counting down
        if (g->waitingForHit) return;

        g->timer -= dt;
        if (g->timer < 0.0f) g->timer = 0.0f;
        if (g->timer <= 0.0f && !immobilized) {
            g->phase = 1; // 1.5s sequential 3-dice roll
            g->timer = 0.0f;
            for (int k = 0; k < 3; k++) g->diceVals[k] = GetRandomValue(1, 6);
            g->cardRank = GetRandomValue(1, 13); // 1=A, 2..10, 11=J, 12=Q, 13=K (equal 1/13 probability)
        }
    } else if (g->phase == 1) {
        g->timer += dt;
        if (g->timer >= GAMBLER_DICE_TIME) {
            g->phase = 2; // 0.75s card draw showcase
            g->timer = 0.0f;
        }
    } else if (g->phase == 2) {
        g->timer += dt;
        if (g->timer >= GAMBLER_CARD_TIME) {
            int sum = g->diceVals[0] + g->diceVals[1] + g->diceVals[2];
            g->nextHitDmg = sum * 3;
            g->hasCardBuff = true;
            g->waitingForHit = true;
            g->bonusHoming += GAMBLER_HOMING_STEP; // +5% homing bias after casting skill
            g->speedMult   += GAMBLER_SPEED_STEP;  // +5% movement speed after casting skill
            g->phase = 0;
            g->timer = 0.0f;
        }
    }
}

// Psychic: throws 3 tarot cards simultaneously every 3.0s (slow speed, moderate accuracy, unlimited range, 10 dmg each,
// pierces teammates); 7.0s after the first throw, spawns a Black Hole for 2.0s (diameter = 33% of window width)
// that pulls all enemies to its center (0.75s pull time from outer rim) and resets/reduces card throw cooldown to 2/3s.
// If afflicted by a status effect, cooldowns continue to tick down to 0, and skills fire once Psychic is free.
static void FirePsychicTarotVolley(Ball* b, const Ball* enemy)
{
    static const Color cardPalette[3] = {
        { 235, 65, 190, 255 },
        { 90, 210, 255, 255 },
        { 255, 215, 65, 255 }
    };
    float baseAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
    // Moderate accuracy due to slow projectile speed + 3-card fan spread (like Tara / Twisted Fate)
    float jitter = ((float)GetRandomValue(-100, 100) / 100.0f) * 8.0f * DEG2RAD;
    baseAngle += jitter;

    for (int k = 0; k < 3; k++) {
        float spread = (k - 1) * 13.0f * DEG2RAD;
        float a = baseAngle + spread;
        TarotCard* tc = &g_tarots[g_tarotNext];
        g_tarotNext = (g_tarotNext + 1) % TAROT_MAX;
        *tc = (TarotCard){
            .pos = b->pos,
            .vel = { cosf(a) * TAROT_SPEED, sinf(a) * TAROT_SPEED },
            .angle = a,
            .cardCol = cardPalette[k],
            .team = b->team,
            .dmgMult = b->dmgMult,
            .active = true
        };
    }
}

static void UpdatePsychic(int bIdx, float dt, bool immobilized)
{
    Ball* b = &g_balls[bIdx];
    PsychicData* ps = &b->psychic;
    Ball* enemy = FindNearestEnemy(b);

    // Count down Black Hole timer once first tarot volley has been thrown
    if (ps->firstThrowDone) {
        if (ps->bhActive) {
            ps->bhTimer -= dt;
            if (ps->bhTimer <= 0.0f) {
                ps->bhActive = false;
                ps->bhCooldown = PSYCHIC_BH_DELAY;
                // Return to State 1 (normal 3.0s card throw interval)
                b->skillTimer = PSYCHIC_NORM_CD;
            }
        } else {
            ps->bhCooldown -= dt;
            if (ps->bhCooldown < 0.0f) ps->bhCooldown = 0.0f;
            if (ps->bhCooldown <= 0.0f && !immobilized && enemy) {
                ps->bhActive = true;
                ps->bhTimer = PSYCHIC_BH_TIME;
                ps->bhPos = ClampScreen(enemy->pos, 45.0f);

                BlackHole* bh = &g_blackHoles[g_blackHoleNext];
                g_blackHoleNext = (g_blackHoleNext + 1) % BLACKHOLE_MAX;
                *bh = (BlackHole){
                    .pos = ps->bhPos,
                    .timer = PSYCHIC_BH_TIME,
                    .spinAngle = 0.0f,
                    .ownerIdx = bIdx,
                    .team = b->team,
                    .active = true
                };

                // Immediately reset card throw timer into State 2 (fast 2/3s interval)
                // regardless of whether Psychic just threw cards or was waiting
                b->skillTimer = 0.0f;
            }
        }
    }

    b->skillTimer -= dt;
    if (b->skillTimer < 0.0f) b->skillTimer = 0.0f;
    if (b->skillTimer <= 0.0f && !immobilized && enemy) {
        FirePsychicTarotVolley(b, enemy);
        ps->firstThrowDone = true;
        b->skillTimer = ps->bhActive ? PSYCHIC_FAST_CD : PSYCHIC_NORM_CD;
    }
}

// ======================= CHARGE, SHACKLESS, JUDGEMENT, TRAIN, WEB & RANGE SKILLS =======================
// Charge (Common): every 3.0s increases size by +10% and contact damage by +30% relative to its previous stack;
// resets back to initial size (BASE_RADIUS) and damage (10) upon colliding with an enemy.
static void UpdateCharge(Ball* b, float dt)
{
    ChargeData* ch = &b->charge;
    ch->timer -= dt;
    if (ch->timer <= 0.0f) {
        ch->timer += CHARGE_INTERVAL;
        if (ch->stacks < CHARGE_MAX_STACKS) {
            ch->stacks++;
            ch->exactRadius *= (1.0f + CHARGE_RAD_STEP);
            ch->exactDamage *= (1.0f + CHARGE_DMG_STEP);
            b->radius = (int)roundf(ch->exactRadius);
            b->damage = (int)roundf(ch->exactDamage);
            b->pos = ClampScreen(b->pos, (float)b->radius);
            AddFx(b->pos, (float)b->radius * 1.25f, SKYBLUE);
        }
    }
}

// Shackless (Legendary - Sylas style): 0 contact dmg; every 5.0s rolls 1 of 3 ultimates (33.3% each):
// 1. Leap Slam: jumps for 0.50s toward remembered position of nearest enemy within 3x radius,
//    then slams the ground creating a black fissure (+25% size = 1.875x radius) with a jagged brown earth rim dealing 20 HP;
//    Shackless and any hit enemies stand still at the seismic zone for 0.35s, then the fissure fades out in 0.10s.
// 2. Lux Final Spark: remembers farthest enemy position and charges for 0.95s, then fires an
//    infinite-range rainbow laser beam (width increased by +25% = 1.5x ball diameter) dealing 25 HP.
// 3. Garen Demacian Justice: fast 0.35s drop, 100% locks onto target enemy, +20% sword visual size with 2 tilted translucent
//    side swords (-10% size) and bright golden ground cracks; executes targets with <= 20 HP, else deals 7 HP to target and 3 HP to nearby enemies.
// Helper to grant +1 Chain Lash stack (up to 3 stacks) and reset the 3.0s passive timer whenever Shackless's skill hits an enemy
static void AddUnshackledPassiveStack(UnshackledData* u)
{
    if (u->passiveStacks < UNSHACKLED_MAX_STACKS) {
        u->passiveStacks++;
    }
    u->passiveTimer = UNSHACKLED_STACK_TIME;
}

static void UpdateUnshackled(Ball* b, float dt)
{
    UnshackledData* u = &b->unshackled;
    if (u->fxTimer > 0.0f)       u->fxTimer -= dt;
    if (u->lashCooldown > 0.0f)  u->lashCooldown -= dt;
    if (u->lashAnimTimer > 0.0f) u->lashAnimTimer -= dt;

    // Tick 3.0s Chain Lash stack timer: if it expires without hitting a skill or lashing, reset stacks to 0
    if (u->passiveStacks > 0) {
        u->passiveTimer -= dt;
        if (u->passiveTimer <= 0.0f) {
            u->passiveStacks = 0;
            u->passiveTimer = 0.0f;
        }
    }

    if (u->mode == 0) {
        u->cooldown -= dt;
        if (u->cooldown <= 0.0f) {
            Ball* nearE = FindNearestEnemy(b);
            Ball* farE  = FindFarthestEnemy(b);
            if (!nearE) {
                u->cooldown = 0.5f;
                return;
            }

            int pick = GetRandomValue(1, 3); // 33.3% each
            u->mode = pick;
            u->timer = 0.0f;
            u->startPos = b->pos;
            u->targetEnemyIdx = -1;

            if (pick == 1) {
                // Clamp leap target within 3x ball radius toward nearest enemy
                Vector2 diff = V2Sub(nearE->pos, b->pos);
                float dist = sqrtf(diff.x * diff.x + diff.y * diff.y);
                float maxLeap = (float)b->radius * 3.0f;
                if (dist > maxLeap && dist > 0.001f) {
                    u->targetPos = ClampScreen(V2Add(b->pos, V2Mul(diff, maxLeap / dist)), (float)b->radius);
                } else {
                    u->targetPos = ClampScreen(nearE->pos, (float)b->radius);
                }
            } else if (pick == 2) {
                Ball* aimE = farE ? farE : nearE;
                u->targetPos = aimE->pos;
                u->aimAngle = atan2f(aimE->pos.y - b->pos.y, aimE->pos.x - b->pos.x);
            } else {
                // Mode 3: lock onto nearest enemy so the sword 100% hits them
                for (int j = 0; j < g_ballCount; j++) {
                    if (&g_balls[j] == nearE) {
                        u->targetEnemyIdx = j;
                        break;
                    }
                }
                u->targetPos = nearE->pos;
            }
        }
        return;
    }

    u->timer += dt;

    if (u->mode == 1) {
        // Mode 1: 0.50s leap toward remembered targetPos, then ground slam
        float t = ClampF(u->timer / UNSHACKLED_LEAP_T, 0.0f, 1.0f);
        b->pos = V2Add(u->startPos, V2Mul(V2Sub(u->targetPos, u->startPos), t));

        if (u->timer >= UNSHACKLED_LEAP_T) {
            b->pos = u->targetPos;
            float slamR = UNSHACKLED_SLAM_RAD;
            u->fxKind = 1;
            u->fxPos = b->pos;
            u->fxTimer = UNSHACKLED_SLAM_HOLD_T + UNSHACKLED_SLAM_FADE_T; // 0.35s hold + 0.10s fade

            bool hitAny = false;
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                float hitR = slamR + (float)e->radius;
                if (V2DistSq(b->pos, e->pos) <= hitR * hitR) {
                    hitAny = true;
                    TakeDamageFrom(e, b, UNSHACKLED_SLAM_DMG);
                    if (IsCombatBall(e) && !IsKingDashing(e)) {
                        e->slamLocked = true;
                        e->slamLockTimer = UNSHACKLED_SLAM_HOLD_T;
                        e->slamLockPos = e->pos;
                    }
                }
            }
            if (hitAny) AddUnshackledPassiveStack(u);

            // Shackless stands still at the seismic center for 0.35s after slamming
            u->mode = 4;
            u->timer = 0.0f;
            b->vel = (Vector2){ 0.0f, 0.0f };
        }
    } else if (u->mode == 4) {
        // Post-slam 0.35s hold where Shackless remains stationary at the earthquake center
        b->pos = u->targetPos;
        if (u->timer >= UNSHACKLED_SLAM_HOLD_T) {
            u->mode = 0;
            u->cooldown = UNSHACKLED_CD;
            SetRandomDirection(b);
        }
    } else if (u->mode == 2) {
        // Mode 2: 0.95s stationary charge aiming at remembered farthest enemy, then infinite-range rainbow beam
        b->pos = u->startPos;
        if (u->timer >= UNSHACKLED_LUX_T) {
            Vector2 dir = { cosf(u->aimAngle), sinf(u->aimAngle) };
            float halfW = UNSHACKLED_LUX_W * 0.5f;

            bool hitAny = false;
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                Vector2 rel = V2Sub(e->pos, b->pos);
                float proj = rel.x * dir.x + rel.y * dir.y;
                if (proj >= -e->radius) {
                    float perp = fabsf(rel.x * (-dir.y) + rel.y * dir.x);
                    if (perp <= halfW + (float)e->radius) {
                        hitAny = true;
                        TakeDamageFrom(e, b, UNSHACKLED_LUX_DMG);
                    }
                }
            }
            if (hitAny) AddUnshackledPassiveStack(u);

            u->fxKind = 2;
            u->fxPos = b->pos;
            u->fxAngle = u->aimAngle;
            u->fxTimer = 0.45f;
            u->mode = 0;
            u->cooldown = UNSHACKLED_CD;
            SetRandomDirection(b);
        }
    } else if (u->mode == 3) {
        // Mode 3: 0.35s fast Demacian Justice sword drop, 100% locked onto targetEnemyIdx
        b->pos = u->startPos;
        if (u->targetEnemyIdx >= 0 && IsCombatBall(&g_balls[u->targetEnemyIdx])) {
            u->targetPos = g_balls[u->targetEnemyIdx].pos;
        } else {
            Ball* fallbackE = FindNearestEnemy(b);
            if (fallbackE) {
                for (int j = 0; j < g_ballCount; j++) {
                    if (&g_balls[j] == fallbackE) { u->targetEnemyIdx = j; break; }
                }
                u->targetPos = fallbackE->pos;
            }
        }

        if (u->timer >= UNSHACKLED_SWORD_T) {
            float splashRadius = (float)BASE_RADIUS * 1.85f;
            bool hitAny = false;
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;

                if (j == u->targetEnemyIdx) {
                    hitAny = true;
                    // 100% guaranteed hit on targeted enemy: execute if <= 20 HP, else 7 damage
                    if (e->hp <= UNSHACKLED_EXEC_HP) {
                        DealDamage(e, e->hp, true);
                    } else {
                        TakeDamageFrom(e, b, UNSHACKLED_SWORD_TARGET_DMG);
                    }
                } else {
                    float rCheck = splashRadius + (float)e->radius;
                    if (V2DistSq(u->targetPos, e->pos) <= rCheck * rCheck) {
                        hitAny = true;
                        // Surrounding enemy: execute if <= 20 HP, else 3 damage
                        if (e->hp <= UNSHACKLED_EXEC_HP) {
                            DealDamage(e, e->hp, true);
                        } else {
                            TakeDamageFrom(e, b, UNSHACKLED_SWORD_AOE_DMG);
                        }
                    }
                }
            }
            if (hitAny) AddUnshackledPassiveStack(u);

            u->fxKind = 3;
            u->fxPos = u->targetPos;
            u->fxTimer = 0.55f;
            u->mode = 0;
            u->cooldown = UNSHACKLED_CD;
            SetRandomDirection(b);
        }
    }
}

// Judgement (Legendary): every 3.0s stops in place and charges for 0.75s
// (growing up to +25% size with an outer gold & white aura + spinning Kizaru 4-pointed yellow star, taking 50% reduced damage);
// if the targeted enemy is still within the 35% window-width radius (280px) when the 0.75s charge finishes,
// Judgement is 100% guaranteed to ram and hit them; if they escaped outside that radius during the 0.75s charge, the ram misses.
// Judgement is 100% immune to damage while ramming and for 0.50s after finishing the ram, and shrinks back over 0.30s.
static void UpdateJudgement(Ball* b, float dt)
{
    JudgementData* jd = &b->judgement;
    if (jd->immuneTimer > 0.0f) jd->immuneTimer -= dt;

    if (jd->state == 0) {
        if (jd->shrinkTimer > 0.0f) {
            jd->shrinkTimer -= dt;
            if (jd->shrinkTimer <= 0.0f) {
                jd->shrinkTimer = 0.0f;
                b->radius = BASE_RADIUS;
            } else {
                float sRatio = ClampF(jd->shrinkTimer / JUDGEMENT_SHRINK_T, 0.0f, 1.0f);
                b->radius = (int)roundf((float)BASE_RADIUS * (1.0f + JUDGEMENT_MAX_GROW * sRatio));
            }
            b->pos = ClampScreen(b->pos, (float)b->radius);
        }

        jd->timer -= dt;
        if (jd->timer <= 0.0f) {
            Ball* enemy = FindNearestEnemy(b);
            if (!enemy) {
                jd->timer = 0.3f;
                return;
            }
            jd->state = 1; // 0.75s charge
            jd->timer = 0.0f;
            jd->shrinkTimer = 0.0f;
            jd->anchorPos = b->pos;
            jd->targetPos = enemy->pos;
            jd->ramDir = Norm(V2Sub(enemy->pos, b->pos));
            jd->targetEnemyIdx = -1;
            for (int j = 0; j < g_ballCount; j++) {
                if (&g_balls[j] == enemy) { jd->targetEnemyIdx = j; break; }
            }
            jd->guaranteedHit = false;
            jd->travelled = 0.0f;
            memset(jd->hitEnemy, 0, sizeof(jd->hitEnemy));
        }
    } else if (jd->state == 1) {
        jd->timer += dt;
        float prog = ClampF(jd->timer / JUDGEMENT_CHARGE_T, 0.0f, 1.0f);
        b->radius = (int)roundf((float)BASE_RADIUS * (1.0f + JUDGEMENT_MAX_GROW * prog));
        jd->anchorPos = ClampScreen(jd->anchorPos, (float)b->radius);
        b->pos = jd->anchorPos;

        // Track target enemy direction while charging if they are still inside the 35% window-width range
        Ball* lockE = (jd->targetEnemyIdx >= 0 && IsCombatBall(&g_balls[jd->targetEnemyIdx]) &&
                       g_balls[jd->targetEnemyIdx].team != b->team)
                      ? &g_balls[jd->targetEnemyIdx]
                      : FindNearestEnemy(b);
        if (lockE) {
            float dSq = V2DistSq(jd->anchorPos, lockE->pos);
            float maxR = JUDGEMENT_RAM_RANGE + (float)lockE->radius;
            if (dSq <= maxR * maxR) {
                jd->ramDir = Norm(V2Sub(lockE->pos, jd->anchorPos));
                jd->targetPos = lockE->pos;
            }
        }

        if (jd->timer >= JUDGEMENT_CHARGE_T) {
            b->radius = (int)roundf((float)BASE_RADIUS * (1.0f + JUDGEMENT_MAX_GROW));
            jd->state = 2; // high-speed ram (100% damage immune)
            jd->timer = 0.0f;

            // Check if the enemy is STILL within the 35% window-width radius at the moment the 0.75s charge finishes:
            // - Still inside -> 100% guaranteed to hit!
            // - Escaped outside -> guaranteed to miss!
            jd->guaranteedHit = false;
            if (lockE) {
                float dSq = V2DistSq(jd->anchorPos, lockE->pos);
                float maxR = JUDGEMENT_RAM_RANGE + (float)lockE->radius;
                if (dSq <= maxR * maxR) {
                    jd->guaranteedHit = true;
                    for (int j = 0; j < g_ballCount; j++) {
                        if (&g_balls[j] == lockE) { jd->targetEnemyIdx = j; break; }
                    }
                    jd->ramDir = Norm(V2Sub(lockE->pos, b->pos));
                }
            }
        }
    } else if (jd->state == 2) {
        b->radius = (int)roundf((float)BASE_RADIUS * (1.0f + JUDGEMENT_MAX_GROW));

        if (jd->guaranteedHit) {
            // Guaranteed hit: home directly onto the locked enemy until impact
            Ball* lockE = (jd->targetEnemyIdx >= 0 && IsCombatBall(&g_balls[jd->targetEnemyIdx]))
                          ? &g_balls[jd->targetEnemyIdx]
                          : FindNearestEnemy(b);
            if (!lockE) {
                jd->state = 0;
                jd->timer = JUDGEMENT_CD;
                jd->shrinkTimer = JUDGEMENT_SHRINK_T;
                jd->immuneTimer = JUDGEMENT_POST_IMMUNE_T; // 0.50s post-ram damage immunity
                SetDirection(b, jd->ramDir);
                return;
            }

            jd->ramDir = Norm(V2Sub(lockE->pos, b->pos));
            float distToTarget = sqrtf(V2DistSq(lockE->pos, b->pos));
            float step = fminf(JUDGEMENT_RAM_SPEED, distToTarget);
            b->pos = ClampScreen(V2Add(b->pos, V2Mul(jd->ramDir, step)), (float)b->radius);
            jd->travelled += step;

            float rsum = (float)(b->radius + lockE->radius);
            if (V2DistSq(b->pos, lockE->pos) <= (rsum + 4.0f) * (rsum + 4.0f)) {
                TakeDamageFrom(lockE, b, JUDGEMENT_RAM_DMG);
                AddFx(lockE->pos, (float)lockE->radius * 1.6f, GOLD);

                if (IsCombatBall(lockE) && !IsFixed(lockE)) {
                    Vector2 pushDir = Norm(V2Add(jd->ramDir, V2Mul(Norm(V2Sub(lockE->pos, b->pos)), 0.6f)));
                    lockE->pos = ClampScreen(V2Add(lockE->pos, V2Mul(pushDir, 38.0f)), (float)lockE->radius);
                    SetDirection(lockE, pushDir);
                    lockE->boostTimer = 0.45f;
                }

                jd->state = 0;
                jd->timer = JUDGEMENT_CD;
                jd->shrinkTimer = JUDGEMENT_SHRINK_T;
                jd->immuneTimer = JUDGEMENT_POST_IMMUNE_T; // 0.50s post-ram damage immunity
                SetDirection(b, jd->ramDir);
                b->boostTimer = 0.16f;
            }
        } else {
            // Enemy escaped the 35% window-width radius during the 0.75s charge -> ram misses!
            float step = fminf(JUDGEMENT_RAM_SPEED, JUDGEMENT_RAM_RANGE - jd->travelled);
            b->pos = V2Add(b->pos, V2Mul(jd->ramDir, step));
            jd->travelled += step;

            float minX = (float)b->radius, maxX = (float)(SCREEN_W - b->radius);
            float minY = (float)b->radius, maxY = (float)(SCREEN_H - b->radius);
            bool hitWall = false;
            if (b->pos.x <= minX) { b->pos.x = minX; hitWall = true; }
            if (b->pos.x >= maxX) { b->pos.x = maxX; hitWall = true; }
            if (b->pos.y <= minY) { b->pos.y = minY; hitWall = true; }
            if (b->pos.y >= maxY) { b->pos.y = maxY; hitWall = true; }

            if (hitWall || jd->travelled >= JUDGEMENT_RAM_RANGE - 0.01f) {
                jd->state = 0;
                jd->timer = JUDGEMENT_CD;
                jd->shrinkTimer = JUDGEMENT_SHRINK_T;
                jd->immuneTimer = JUDGEMENT_POST_IMMUNE_T; // 0.50s post-ram damage immunity
                SetDirection(b, jd->ramDir);
            }
        }
    }
}

// Helper to sample position and tangent angle at arc-length distance `dist` along Train's recorded track
static void SampleTrainTrackAtDist(const TrainData* tr, float dist, Vector2* outPos, float* outAngle)
{
    if (tr->trackCount <= 0) {
        *outPos = (Vector2){ 0.0f, 0.0f };
        *outAngle = 0.0f;
        return;
    }
    if (tr->trackCount == 1 || dist <= 0.0f) {
        *outPos = tr->track[0];
        *outAngle = (tr->trackCount > 1)
            ? atan2f(tr->track[1].y - tr->track[0].y, tr->track[1].x - tr->track[0].x)
            : 0.0f;
        return;
    }

    float accum = 0.0f;
    for (int i = 0; i < tr->trackCount - 1; i++) {
        Vector2 p0 = tr->track[i];
        Vector2 p1 = tr->track[i + 1];
        Vector2 seg = V2Sub(p1, p0);
        float segLen = sqrtf(seg.x * seg.x + seg.y * seg.y);
        if (segLen < 0.001f) continue;
        if (accum + segLen >= dist || i == tr->trackCount - 2) {
            float t = ClampF((dist - accum) / segLen, 0.0f, 1.0f);
            *outPos = V2Add(p0, V2Mul(seg, t));
            *outAngle = atan2f(seg.y, seg.x);
            return;
        }
        accum += segLen;
    }
    *outPos = tr->track[tr->trackCount - 1];
    Vector2 a0 = tr->track[tr->trackCount - 2];
    Vector2 a1 = tr->track[tr->trackCount - 1];
    *outAngle = atan2f(a1.y - a0.y, a1.x - a0.x);
}

// Train (Uncommon):
// Phase 0 (4.0s): lays railway tracks behind the ball while moving (track width = ball diameter = 60px).
// Phase 1 (1.0s): stops laying tracks and waits 1.0s before the train arrives.
// Phase 2: up to 5 train cars connected by short chains rush along the recorded track dealing 15 HP on hit (0.5s immunity per victim).
// Phase 3 (2.0s): waits 2.0s after the train finishes, then starts a fresh 4.0s track-laying cycle.
static void UpdateTrain(Ball* b, float dt)
{
    TrainData* tr = &b->train;

    for (int j = 0; j < MAX_BALLS; j++) {
        if (tr->hitImmune[j] > 0.0f) tr->hitImmune[j] -= dt;
    }

    if (tr->phase == 0) {
        if (tr->trackCount == 0) {
            tr->track[tr->trackCount++] = b->pos;
        } else {
            Vector2 last = tr->track[tr->trackCount - 1];
            if (V2DistSq(b->pos, last) >= 100.0f && tr->trackCount < TRAIN_TRACK_MAX) {
                tr->track[tr->trackCount++] = b->pos;
            }
        }
        tr->timer -= dt;
        if (tr->timer <= 0.0f) {
            tr->phase = 1;
            tr->timer = TRAIN_PRE_WAIT;
        }
    } else if (tr->phase == 1) {
        tr->timer -= dt;
        if (tr->timer <= 0.0f) {
            if (tr->trackCount < 2) {
                tr->phase = 3;
                tr->timer = TRAIN_POST_WAIT;
            } else {
                float totalLen = 0.0f;
                for (int i = 0; i < tr->trackCount - 1; i++) {
                    Vector2 seg = V2Sub(tr->track[i + 1], tr->track[i]);
                    totalLen += sqrtf(seg.x * seg.x + seg.y * seg.y);
                }
                tr->totalTrackLen = totalLen;
                // Up to 5 cars maximum (even if the trail is longer, capped at 5 cars)
                int fitCars = 1 + (int)(totalLen / TRAIN_CAR_SPACING);
                if (fitCars < 1) fitCars = 1;
                if (fitCars > TRAIN_MAX_CARS) fitCars = TRAIN_MAX_CARS;
                tr->numCars = fitCars;
                tr->headDist = 0.0f;
                memset(tr->carActive, 0, sizeof(tr->carActive));
                tr->phase = 2;
            }
        }
    } else if (tr->phase == 2) {
        tr->headDist += TRAIN_LOCO_SPEED;
        float tailEndDist = (float)(tr->numCars - 1) * TRAIN_CAR_SPACING;

        for (int c = 0; c < tr->numCars; c++) {
            float carDist = tr->headDist - (float)c * TRAIN_CAR_SPACING;
            if (carDist >= 0.0f && carDist <= tr->totalTrackLen) {
                tr->carActive[c] = true;
                SampleTrainTrackAtDist(tr, carDist, &tr->carPos[c], &tr->carAngle[c]);

                // Check each active car against enemies (with 0.5s per-victim immunity)
                for (int j = 0; j < g_ballCount; j++) {
                    Ball* e = &g_balls[j];
                    if (!IsCombatBall(e) || e->team == b->team || tr->hitImmune[j] > 0.0f) continue;
                    float hitR = (TRAIN_TRACK_W * 0.5f) + (float)e->radius;
                    if (V2DistSq(tr->carPos[c], e->pos) <= hitR * hitR) {
                        tr->hitImmune[j] = TRAIN_IMMUNE_TIME;
                        TakeDamageFrom(e, b, TRAIN_HIT_DMG);
                        AddFx(e->pos, (float)e->radius * 1.3f, DARKGRAY);
                    }
                }
            } else {
                tr->carActive[c] = false;
            }
        }

        if (tr->headDist >= tr->totalTrackLen + tailEndDist) {
            tr->phase = 3;
            tr->timer = TRAIN_POST_WAIT;
            tr->trackCount = 0;
        }
    } else if (tr->phase == 3) {
        tr->timer -= dt;
        if (tr->timer <= 0.0f) {
            tr->phase = 0;
            tr->timer = TRAIN_LAY_TIME;
            tr->trackCount = 0;
        }
    }
}

// Web (Common): 0 contact dmg, 0 homing bias toward enemies (+10% wall-seeking bias);
// on odd wall hits (1st, 3rd...) remembers wall point; on even wall hits (2nd, 4th...) spins a white web line;
// any enemy crossing a web line takes 1 HP damage (can trigger repeatedly each frame/crossing)
static void OnSpiderWallBounce(Ball* b, Vector2 wallPoint)
{
    SpiderData* sp = &b->spider;
    if (!sp->hasPendingAnchor) {
        sp->hasPendingAnchor = true;
        sp->pendingAnchor = wallPoint;
        AddFx(wallPoint, 10.0f, WHITE);
    } else {
        // Only complete the web if the second wall touch is at a distinct spot; otherwise keep the first anchor waiting
        if (V2DistSq(sp->pendingAnchor, wallPoint) > 100.0f) {
            sp->hasPendingAnchor = false;
            SpiderWebLine* w = &sp->webs[sp->webNext];
            sp->webNext = (sp->webNext + 1) % SPIDER_WEB_MAX;
            memset(w, 0, sizeof(*w));
            w->p1 = sp->pendingAnchor;
            w->p2 = wallPoint;
            w->active = true;
            AddFx(wallPoint, 14.0f, WHITE);
        }
    }
}

static void UpdateSpider(Ball* b, float dt)
{
    (void)dt;
    SpiderData* sp = &b->spider;

    for (int wIdx = 0; wIdx < SPIDER_WEB_MAX; wIdx++) {
        SpiderWebLine* w = &sp->webs[wIdx];
        if (!w->active) continue;

        Vector2 ab = V2Sub(w->p2, w->p1);
        float abLenSq = ab.x * ab.x + ab.y * ab.y;
        if (abLenSq < 1.0f) continue;

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == b->team) {
                w->touching[j] = false;
                continue;
            }
            Vector2 ap = V2Sub(e->pos, w->p1);
            float u = ClampF((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);
            Vector2 closest = V2Add(w->p1, V2Mul(ab, u));
            float hitR = (float)e->radius + 2.5f;
            bool inside = (V2DistSq(e->pos, closest) <= hitR * hitR);

            // Deals 1 HP whenever an enemy crosses/touches the web (and can trigger repeatedly)
            if (inside) {
                if (!w->touching[j]) {
                    w->touching[j] = true;
                    TakeDamageFrom(e, b, SPIDER_WEB_DMG);
                } else if (GetRandomValue(1, 10) <= 3) {
                    // Continuous 1 HP damage while moving across the sticky web thread
                    TakeDamageFrom(e, b, SPIDER_WEB_DMG);
                }
            } else {
                w->touching[j] = false;
            }
        }
    }
}

// Range (Uncommon): yellow ball, 0 contact dmg;
// starts with an outer ring of radius = 1.1x ball radius (33px);
// every 2.0s the outer ring radius grows 10% faster each cycle with NO upper limit;
// any enemy inside the outer ring takes 2 HP every 0.5s
static void UpdateRange(Ball* b, float dt)
{
    RangeData* rg = &b->range;

    rg->growTimer -= dt;
    if (rg->growTimer <= 0.0f) {
        rg->growTimer += RANGE_GROW_INTERVAL;
        rg->stacks++;
        rg->curStepMult *= RANGE_GROW_ACCEL; // +10% faster growth each cycle
        rg->ringRadius += rg->baseRingRadius * rg->curStepMult;
        AddFx(b->pos, rg->ringRadius, Fade(GOLD, 0.45f));
    }

    rg->tickTimer -= dt;
    if (rg->tickTimer <= 0.0f) {
        rg->tickTimer += RANGE_TICK_TIME;
        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == b->team) continue;
            float hitR = rg->ringRadius + (float)e->radius;
            if (V2DistSq(b->pos, e->pos) <= hitR * hitR) {
                TakeDamageFrom(e, b, RANGE_TICK_DMG);
            }
        }
    }
}

// ======================= MEDIEVAL BALLS: KNIGHT, CAVALRY, EXECUTER & KING =======================
// Helper to test if an enemy ball overlapsExecuter's Sion-Q trapezoid
static bool IsInsideExecuterTrapezoid(Vector2 origin, float angle, const Ball* e)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };
    Vector2 rel = V2Sub(e->pos, origin);
    float fwd  = rel.x * d.x + rel.y * d.y;
    float side = fabsf(rel.x * n.x + rel.y * n.y);

    float len = EXECUTER_TRAP_LEN;
    if (fwd < -(float)e->radius * 0.4f || fwd > len + (float)e->radius) return false;
    float t = ClampF(fwd / len, 0.0f, 1.0f);
    float halfW = (EXECUTER_TRAP_SHORT * 0.5f) + t * ((EXECUTER_TRAP_LONG - EXECUTER_TRAP_SHORT) * 0.5f);
    return side <= (halfW + (float)e->radius * 0.85f);
}

// Knight (Rare): 0 direct contact dmg, 5-40% contact dmg reduction;
// on contact: waits 0.10s (allowing target a chance to dodge), then sweeps a 30-degree cone slash
// (reach = 85% ball size) dealing 12 HP
static void UpdateKnight(Ball* b, float dt)
{
    KnightData* kn = &b->knight;
    if (kn->fxTimer > 0.0f) kn->fxTimer -= dt;

    if (kn->windingUp) {
        kn->timer += dt;
        if (kn->timer >= KNIGHT_SLASH_DELAY) {
            kn->windingUp = false;
            kn->timer = 0.0f;
            kn->fxTimer = 0.22f;
            kn->fxAngle = kn->slashAngle;

            float maxReach = (float)b->radius + KNIGHT_CONE_REACH;
            float halfConeRad = (KNIGHT_CONE_DEG * 0.5f) * DEG2RAD;
            Vector2 slashDir = { cosf(kn->slashAngle), sinf(kn->slashAngle) };

            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                Vector2 toE = V2Sub(e->pos, b->pos);
                float dist = sqrtf(toE.x * toE.x + toE.y * toE.y);
                if (dist <= maxReach + (float)e->radius && dist > 0.001f) {
                    Vector2 dirE = { toE.x / dist, toE.y / dist };
                    float dot = ClampF(slashDir.x * dirE.x + slashDir.y * dirE.y, -1.0f, 1.0f);
                    float angDiff = acosf(dot);
                    // Account for the target ball's angular radius
                    float angSubtend = atan2f((float)e->radius * 0.85f, fmaxf(dist, (float)b->radius));
                    if (angDiff <= halfConeRad + angSubtend) {
                        TakeDamageFrom(e, b, KNIGHT_SLASH_DMG);
                        AddFx(e->pos, (float)e->radius * 1.15f, WHITE);
                    }
                }
            }
        }
    }
}

// Cavalry (Rare): normal contact thrusts long lance for 12 HP (0 delay, 100% hit);
// every 4.0s enters Charge state: accelerates up to 1.6x speed for up to 50% window width (400px),
// scaling damage from 10 to 25 HP; hitting a wall or enemy during Charge spawns sparks and immobilizes for 1.25s
static void UpdateCavalry(Ball* b, float dt)
{
    CavalryData* cv = &b->cavalry;
    if (cv->thrustTimer > 0.0f) cv->thrustTimer -= dt;
    if (V2DistSq(b->vel, (Vector2){ 0, 0 }) > 0.01f) {
        cv->aimAngle = atan2f(b->vel.y, b->vel.x);
    }

    if (cv->state == 0) {
        cv->speedMult = 1.0f;
        cv->timer -= dt;
        if (cv->timer <= 0.0f) {
            cv->state = 1; // enter Charge state
            cv->travelled = 0.0f;
            cv->speedMult = 1.0f;
        }
    } else if (cv->state == 1) {
        float prog = ClampF(cv->travelled / (CAVALRY_MAX_DIST * 0.65f), 0.0f, 1.0f);
        cv->speedMult = 1.0f + (CAVALRY_MAX_MULT - 1.0f) * prog;
        cv->travelled += BASE_SPEED * cv->speedMult * SlowMult(b);

        if (cv->travelled >= CAVALRY_MAX_DIST) {
            // Reached max 50% window width without crashing into enemy or wall
            cv->state = 0;
            cv->speedMult = 1.0f;
            cv->timer = CAVALRY_CD;
        }
    } else if (cv->state == 2) {
        cv->speedMult = 1.0f;
        cv->timer -= dt;
        if (cv->timer <= 0.0f) {
            cv->state = 0;
            cv->timer = CAVALRY_CD;
        }
    }
}

// Executer (Common): heavy axe chop on contact (10 HP, 2.25s cooldown, 0 dmg during cooldown);
// every 5.0s captures all enemies within 2.5x ball radius (stunned & immune to external contact dmg)
// and charges for 3.0s in a Sion-Q style red gradient trapezoid before slamming down for 20 HP
static void UpdateExecuter(Ball* b, float dt)
{
    ExecuterData* ex = &b->executer;
    if (ex->chopCooldown > 0.0f)  ex->chopCooldown -= dt;
    if (ex->chopAnimTimer > 0.0f) ex->chopAnimTimer -= dt;
    if (ex->slamFxTimer > 0.0f)   ex->slamFxTimer -= dt;

    if (ex->skillState == 0) {
        ex->skillTimer -= dt;
        if (ex->skillTimer < 0.0f) ex->skillTimer = 0.0f;
        if (ex->skillTimer <= 0.0f) {
            // Find all enemies within 2.5x ball radius to capture
            int capturedCount = 0;
            Ball* primaryTarget = NULL;
            float bestDistSq = 1e18f;
            memset(ex->captured, 0, sizeof(ex->captured));

            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team || IsKingDashing(e)) continue;
                float capR = EXECUTER_CAPTURE_R + (float)e->radius;
                float dSq = V2DistSq(b->pos, e->pos);
                if (dSq <= capR * capR) {
                    ex->captured[j] = true;
                    capturedCount++;
                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        primaryTarget = e;
                    }
                }
            }

            if (capturedCount > 0 && primaryTarget) {
                ex->skillState = 1;
                ex->skillTimer = 0.0f;
                ex->anchorPos = b->pos;
                ex->trapAngle = atan2f(primaryTarget->pos.y - b->pos.y, primaryTarget->pos.x - b->pos.x);

                for (int j = 0; j < g_ballCount; j++) {
                    if (!ex->captured[j]) continue;
                    Ball* e = &g_balls[j];
                    e->execLocked = true;
                    e->execLockTimer = EXECUTER_CHARGE_T;
                    e->execLockPos = e->pos;
                }
            }
        }
    } else if (ex->skillState == 1) {
        b->pos = ex->anchorPos;
        ex->skillTimer += dt;

        for (int j = 0; j < g_ballCount; j++) {
            if (ex->captured[j] && IsCombatBall(&g_balls[j])) {
                g_balls[j].execLocked = true;
                g_balls[j].execLockTimer = fmaxf(0.05f, EXECUTER_CHARGE_T - ex->skillTimer);
                g_balls[j].pos = g_balls[j].execLockPos;
            }
        }

        if (ex->skillTimer >= EXECUTER_CHARGE_T) {
            // 3.0s Sion-Q charge complete: slam giant axe onto trapezoid for 20 HP!
            ex->slamFxTimer = 0.45f;
            ex->chopAnimTimer = 0.38f;
            ex->chopAngle = ex->trapAngle;

            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                bool hit = ex->captured[j] || IsInsideExecuterTrapezoid(b->pos, ex->trapAngle, e);
                if (ex->captured[j]) {
                    e->execLocked = false;
                    e->execLockTimer = 0.0f;
                    RestoreVelocity(e);
                }
                if (hit) {
                    DealDamageScaled(e, EXECUTER_SLAM_DMG, true, b->dmgMult);
                    if (IsCombatBall(e)) e->painFlashTimer = 0.65f;
                    for (int q = 0; q < 12; q++) {
                        float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                        float sp = 2.5f + (float)GetRandomValue(0, 30) * 0.1f;
                        SpawnParticle(e->pos, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.48f, 4.2f, COL_BLOOD, 0.06f, false);
                    }
                }
            }

            memset(ex->captured, 0, sizeof(ex->captured));
            ex->skillState = 0;
            ex->skillTimer = EXECUTER_SKILL_CD;
            SetRandomDirection(b);
        }
    }
}

// King (Mythic - Jarvan IV style):
// Every 2.5s plants a blue-and-gold triangular flag at 30% window width from King (dealing 10 HP in 1.25x ball radius around the flag),
// waits 0.25s, then dashes rapidly to the flag (dealing 15 HP + 0.4s stun to all enemies along the path, while immune to all dmg & CC)
static void UpdateKing(Ball* b, float dt)
{
    KingData* kg = &b->king;

    if (kg->state == 0) {
        kg->timer -= dt;
        if (kg->timer <= 0.0f) {
            Ball* enemy = FindNearestEnemy(b);
            if (!enemy) {
                kg->timer = 0.3f;
                return;
            }
            Vector2 dir = Norm(V2Sub(enemy->pos, b->pos));
            kg->flagPos = ClampScreen(V2Add(b->pos, V2Mul(dir, KING_FLAG_DIST)), (float)b->radius);
            kg->flagActive = true;
            kg->state = 1;
            kg->timer = KING_DASH_DELAY; // 0.25s delay before dashing to flag

            AddFx(kg->flagPos, KING_FLAG_RAD, GOLD);
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                float hitR = KING_FLAG_RAD + (float)e->radius;
                if (V2DistSq(kg->flagPos, e->pos) <= hitR * hitR) {
                    TakeDamageFrom(e, b, KING_FLAG_DMG); // 10 HP flag impact damage
                }
            }
        }
    } else if (kg->state == 1) {
        kg->timer -= dt;
        if (kg->timer <= 0.0f) {
            kg->state = 2; // dash to flag (immune to all damage and crowd control)
            b->frozen = b->shocked = b->windStunned = b->diceLocked = b->slamLocked = b->execLocked = false;
            memset(kg->hitOnDash, 0, sizeof(kg->hitOnDash));
        }
    } else if (kg->state == 2) {
        Vector2 toFlag = V2Sub(kg->flagPos, b->pos);
        float dist = sqrtf(toFlag.x * toFlag.x + toFlag.y * toFlag.y);
        Vector2 dashDir = (dist > 0.001f) ? V2Mul(toFlag, 1.0f / dist) : (Vector2){ 1.0f, 0.0f };
        float step = fminf(KING_DASH_SPEED, dist);
        b->pos = V2Add(b->pos, V2Mul(dashDir, step));

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == b->team || kg->hitOnDash[j]) continue;
            float rsum = (float)(b->radius + e->radius);
            if (V2DistSq(b->pos, e->pos) <= rsum * rsum) {
                kg->hitOnDash[j] = true;
                TakeDamageFrom(e, b, KING_DASH_DMG); // 15 HP dash damage
                if (IsCombatBall(e) && !IsKingDashing(e)) {
                    e->slamLocked = true;
                    e->slamLockTimer = KING_STUN_T; // 0.40s stun
                    e->slamLockPos = e->pos;
                    AddFx(e->pos, (float)e->radius * 1.25f, GOLD);
                }
            }
        }

        if (dist <= KING_DASH_SPEED + 0.5f) {
            b->pos = kg->flagPos;
            kg->flagActive = false;
            kg->state = 0;
            kg->timer = KING_CD; // 2.5s cooldown
            SetDirection(b, dashDir);
        }
    }
}

// Helper to ignite an enemy with Archer's Burn debuff (3 HP every 0.75s for 3 ticks = 9 HP total)
static void ApplyArcherBurn(Ball* e, float dmgMult)
{
    if (!IsCombatBall(e) || IsKingDashing(e)) return;
    e->burnDmgMult = dmgMult;
    if (e->burnTicks <= 0) {
        e->burnTicks = ARCHER_BURN_TICKS;
        e->burnTickTimer = ARCHER_BURN_TICK_T;
    } else {
        e->burnTicks = ARCHER_BURN_TICKS;
    }
}

// Catapult (Mythic - Medieval V2):
// Every 10.0s launches 1 stone boulder in a high parabolic arc (rising into the sky and falling down,
// dealing 0 damage along the flight path and passing through all objects/walls),
// landing at the farthest enemy's position with a 75% hit rate at all distances,
// dealing 35 HP to all enemies standing within a 3.5x ball radius AoE
static void UpdateCatapult(Ball* b, float dt)
{
    CatapultData* cp = &b->catapult;
    if (cp->armAnimTimer > 0.0f) cp->armAnimTimer -= dt;

    Ball* farEnemy = FindFarthestEnemy(b);
    if (farEnemy) {
        cp->aimAngle = atan2f(farEnemy->pos.y - b->pos.y, farEnemy->pos.x - b->pos.x);
    }

    if (cp->boulderActive) {
        cp->boulderTimer += dt;
        if (cp->boulderTimer >= CATAPULT_FLIGHT_T) {
            cp->boulderActive = false;
            cp->boulderTimer = 0.0f;

            Vector2 landPos = cp->boulderTarget;
            AddFx(landPos, CATAPULT_AOE_RAD, (Color){ 195, 202, 210, 255 });
            for (int k = 0; k < 22; k++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                float sp = 2.5f + (float)GetRandomValue(0, 35) * 0.12f;
                Color pc = (k & 1) ? (Color){ 195, 202, 210, 255 } : (Color){ 155, 110, 58, 255 };
                SpawnParticle(landPos, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.48f, 4.5f, pc, 0.08f, false);
            }

            // Deal 35 HP to all enemies standing within 3.5x ball radius AoE
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == b->team) continue;
                float hitR = CATAPULT_AOE_RAD + (float)e->radius;
                if (V2DistSq(landPos, e->pos) <= hitR * hitR) {
                    TakeDamageFrom(e, b, CATAPULT_DMG);
                }
            }
        }
    } else {
        cp->cooldown -= dt;
        if (cp->cooldown <= 0.0f && farEnemy) {
            cp->cooldown = CATAPULT_CD;
            cp->boulderActive = true;
            cp->boulderTimer = 0.0f;
            cp->boulderStart = b->pos;
            cp->armAnimTimer = 0.45f;

            // 75% hit rate at all distances toward the farthest enemy
            float tFrames = (CATAPULT_FLIGHT_T * 60.0f);
            Vector2 pred = ClampScreen(V2Add(farEnemy->pos, V2Mul(farEnemy->vel, tFrames * 0.45f)), (float)farEnemy->radius);
            if (GetRandomValue(1, 100) <= CATAPULT_HIT_PCT) {
                // On-target landing at predicted/current farthest enemy position
                Vector2 jitter = { (float)GetRandomValue(-18, 18), (float)GetRandomValue(-18, 18) };
                cp->boulderTarget = ClampScreen(V2Add(pred, jitter), 24.0f);
            } else {
                // 25% miss: lands outside the 3.5x ball radius AoE
                float missAng = (float)GetRandomValue(0, 359) * DEG2RAD;
                float missDist = CATAPULT_AOE_RAD + (float)farEnemy->radius + 25.0f + (float)GetRandomValue(0, 35);
                Vector2 missPos = {
                    farEnemy->pos.x + cosf(missAng) * missDist,
                    farEnemy->pos.y + sinf(missAng) * missDist
                };
                cp->boulderTarget = ClampScreen(missPos, 24.0f);
            }
        }
    }
}

// Archer (Rare - Medieval V2):
// Every 7.5s shoots a fire-tipped arrow at the nearest enemy (max range = 55% window width, accuracy drops down to 30% at max range):
// - Direct hit deals 10 HP and guarantees Burn (thiêu đốt)
// - Creates a Burning Ground Zone of radius 1.8x ball radius (any enemy touching the zone is afflicted with Burn regardless of duration)
// - Burn deals 3 HP every 0.75s for 3 ticks (total 9 HP)
static void UpdateArcher(Ball* b, float dt)
{
    ArcherData* ar = &b->archer;
    Ball* enemy = FindNearestEnemy(b);
    if (enemy) {
        ar->aimAngle = atan2f(enemy->pos.y - b->pos.y, enemy->pos.x - b->pos.x);
    }

    if (!ar->arrowActive) {
        ar->cooldown -= dt;
        if (ar->cooldown <= 0.0f && enemy) {
            ar->cooldown = ARCHER_CD;
            ar->arrowActive = true;
            ar->arrowTravelled = 0.0f;

            float angle = AimAngleWithDistanceFalloff(b->pos, enemy, ARCHER_ARROW_SPEED,
                                                      ARCHER_MAX_RANGE, 95.0f, ARCHER_MIN_HIT_PCT);
            ar->arrowAngle = angle;
            ar->arrowPos = b->pos;
            ar->arrowVel = (Vector2){ cosf(angle) * ARCHER_ARROW_SPEED, sinf(angle) * ARCHER_ARROW_SPEED };
        }
    } else {
        ar->arrowPos = V2Add(ar->arrowPos, ar->arrowVel);
        ar->arrowTravelled += ARCHER_ARROW_SPEED;

        Vector2 tip = {
            ar->arrowPos.x + cosf(ar->arrowAngle) * (ARCHER_ARROW_LEN * 0.5f),
            ar->arrowPos.y + sinf(ar->arrowAngle) * (ARCHER_ARROW_LEN * 0.5f)
        };

        // Fire sparks trailing from the burning arrowhead
        if (GetRandomValue(0, 99) < 75) {
            Color fc = (GetRandomValue(0, 1) == 0) ? ORANGE : GOLD;
            SpawnParticle(tip, (Vector2){ (float)GetRandomValue(-8, 8) * 0.1f, (float)GetRandomValue(-8, 8) * 0.1f },
                          0.24f, 3.2f, fc, -0.03f, false);
        }

        // Check direct hit against enemies
        for (int j = 0; j < g_ballCount; j++) {
            Ball* cand = &g_balls[j];
            if (!IsCombatBall(cand) || cand->team == b->team) continue;
            float hitR = (float)cand->radius + 6.0f;
            if (V2DistSq(tip, cand->pos) <= hitR * hitR || V2DistSq(ar->arrowPos, cand->pos) <= hitR * hitR) {
                TakeDamageFrom(cand, b, ARCHER_DIRECT_DMG); // 10 HP direct hit
                if (IsCombatBall(cand)) {
                    ApplyArcherBurn(cand, b->dmgMult); // guaranteed Burn on direct hit
                }
                CreateBurnZone(cand->pos, ARCHER_ZONE_RAD, b->team, b->dmgMult);
                ar->arrowActive = false;
                return;
            }
        }

        // If arrow reaches max range (55% window width) or touches a wall, spawn Burning Ground Zone there
        if (tip.x <= 0 || tip.x >= SCREEN_W || tip.y <= 0 || tip.y >= SCREEN_H ||
            ar->arrowPos.x <= 0 || ar->arrowPos.x >= SCREEN_W || ar->arrowPos.y <= 0 || ar->arrowPos.y >= SCREEN_H ||
            ar->arrowTravelled >= ARCHER_MAX_RANGE) {
            CreateBurnZone(ClampScreen(tip, 14.0f), ARCHER_ZONE_RAD, b->team, b->dmgMult);
            ar->arrowActive = false;
        }
    }
}

// ======================= LASER TURRETS & WITCH POTIONS =======================
static Vector2 ComputeLaserEnd(Vector2 start, Vector2 dir)
{
    float tMin = 2000.0f;
    if (dir.x > 0.0001f)       tMin = fminf(tMin, (SCREEN_W - start.x) / dir.x);
    else if (dir.x < -0.0001f) tMin = fminf(tMin, (0.0f - start.x) / dir.x);
    if (dir.y > 0.0001f)       tMin = fminf(tMin, (SCREEN_H - start.y) / dir.y);
    else if (dir.y < -0.0001f) tMin = fminf(tMin, (0.0f - start.y) / dir.y);
    return V2Add(start, V2Mul(dir, tMin));
}

static void SpawnLaserTurret(Vector2 wallPos, Vector2 axisDir, int team, float dmgMult)
{
    LaserTurret* t = &g_turrets[g_turretNext];
    g_turretNext = (g_turretNext + 1) % TURRET_MAX;
    *t = (LaserTurret){
        .pos = wallPos,
        .dir = axisDir,
        .endPos = ComputeLaserEnd(wallPos, axisDir),
        .team = team,
        .dmgMult = dmgMult,
        .shootTimer = TURRET_INTERVAL,
        .beamTimer = 0.0f,
        .active = true
    };
}

static inline bool Overlaps(Vector2 c, float r, const Ball* b)
{
    float rr = r + b->radius;
    return V2DistSq(b->pos, c) <= rr * rr;
}

static Color PotionColor(PotionKind k)
{
    static const Color cols[P_COUNT] = { RED, YELLOW, COL_POISON, COL_HEAL, COL_FREEZE };
    return (k >= 0 && k < P_COUNT) ? cols[k] : COL_FREEZE;
}

static void ExplodePotion(PotionKind kind, Vector2 pos, int ownerIdx, bool direct)
{
    Ball* w = &g_balls[ownerIdx];
    float base = (float)w->radius;
    float mult = direct ? 2.0f : 1.5f;

    switch (kind)
    {
        case P_DAMAGE: {
            float R = base * mult;
            AddFx(pos, R, RED);
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (IsCombatBall(e) && e->team != w->team && Overlaps(pos, R, e))
                    TakeDamageFrom(e, w, 15);
            }
        } break;

        case P_SLOW:
        case P_POISON: {
            float R = base * mult;
            AddFx(pos, R, PotionColor(kind));
            CreateZone(pos, R, kind, ownerIdx);
        } break;

        case P_HEAL: {
            float R = base * 3.0f;
            AddFx(pos, R, COL_HEAL);
            for (int j = 0; j < g_ballCount; j++)
                if (IsCombatBall(&g_balls[j]) && Overlaps(pos, R, &g_balls[j]))
                    HealBall(&g_balls[j], 10);
            for (int i = 0; i < 8; i++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                float d = (float)GetRandomValue(0, (int)(R * 0.8f));
                SpawnParticle((Vector2){ pos.x + cosf(a) * d, pos.y + sinf(a) * d },
                              (Vector2){ 0.0f, -1.2f }, 0.9f, 0.0f, COL_PLUS, 0.0f, true);
            }
        } break;

        case P_FREEZE: {
            float R = base * 1.5f;
            AddFx(pos, R, COL_FREEZE);
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (IsCombatBall(e) && e->team != w->team && Overlaps(pos, R, e)) {
                    TakeDamageFrom(e, w, 10);
                    if (IsCombatBall(e) && !e->frozen && !e->shocked && e->freezeImmune <= 0.0f) {
                        e->freezeDmgMult = w->dmgMult;
                        Freeze(e, WITCH_FREEZE_TIME);
                    }
                }
            }
        } break;

        default: break;
    }
}

static void WitchThrow(Ball* w, int ownerIdx)
{
    Ball* e = FindNearestEnemy(w);
    if (!e) return;

    PotionKind kind = (PotionKind)GetRandomValue(0, P_COUNT - 1);
    Vector2 target;

    if (kind == P_HEAL) {
        if (GetRandomValue(1, 100) <= 15) {
            float a = (float)GetRandomValue(0, 359) * DEG2RAD;
            float d = (float)GetRandomValue(130, 200);
            target = (Vector2){ w->pos.x + cosf(a) * d, w->pos.y + sinf(a) * d };
        } else {
            target = (Vector2){ w->pos.x + (float)GetRandomValue(-8, 8),
                                w->pos.y + (float)GetRandomValue(-8, 8) };
        }
    } else {
        // Distance-scaled scatter on Witch throws (+10% base scatter, more scatter when enemy is further)
        float distToE = sqrtf(V2DistSq(e->pos, w->pos));
        float distRatio = ClampF(distToE / WITCH_MAX_RANGE, 0.0f, 1.0f);
        float a = (float)GetRandomValue(0, 359) * DEG2RAD;
        float d = (float)GetRandomValue(0, (int)(75.0f + 40.0f * distRatio));
        target = (Vector2){ e->pos.x + cosf(a) * d, e->pos.y + sinf(a) * d };

        Vector2 diff = V2Sub(target, w->pos);
        float dist = sqrtf(diff.x * diff.x + diff.y * diff.y);
        if (dist > WITCH_MAX_RANGE) {
            target = V2Add(w->pos, V2Mul(Norm(diff), WITCH_MAX_RANGE));
        }
    }

    Potion* p = &g_potions[g_potionNext];
    g_potionNext = (g_potionNext + 1) % POTION_MAX;
    *p = (Potion){
        .pos = w->pos, .target = ( target ),
        .kind = kind, .ownerIdx = ownerIdx, .active = true
    };
}

static void UpdateEffects(float dt)
{
    if (g_diceRedFlashTimer > 0.0f) g_diceRedFlashTimer -= dt;

    // Flying kunai (max flight distance = KUNAI_RANGE = 40% window width, disappear on wall or enemy hit)
    for (int i = 0; i < KUNAI_MAX; i++) {
        Kunai* kn = &g_kunais[i];
        if (!kn->active) continue;
        kn->pos = V2Add(kn->pos, kn->vel);
        kn->travelled += KUNAI_SPEED;

        if (kn->travelled >= KUNAI_RANGE ||
            kn->pos.x <= 0 || kn->pos.x >= SCREEN_W || kn->pos.y <= 0 || kn->pos.y >= SCREEN_H) {
            kn->active = false;
            continue;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == kn->team) continue;
            float hitR = e->radius + 6.0f;
            if (V2DistSq(kn->pos, e->pos) <= hitR * hitR) {
                TakeDamageScaled(e, KUNAI_DMG, kn->dmgMult);
                kn->active = false;
                break;
            }
        }
    }

    // Psychic tarot cards (unlimited range, pierce teammates, disappear on wall or enemy hit, 10 dmg each)
    for (int i = 0; i < TAROT_MAX; i++) {
        TarotCard* tc = &g_tarots[i];
        if (!tc->active) continue;
        tc->pos = V2Add(tc->pos, tc->vel);

        if (tc->pos.x <= 0 || tc->pos.x >= SCREEN_W || tc->pos.y <= 0 || tc->pos.y >= SCREEN_H) {
            tc->active = false;
            continue;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == tc->team) continue; // pierces teammates
            float hitR = e->radius + 9.0f;
            if (V2DistSq(tc->pos, e->pos) <= hitR * hitR) {
                TakeDamageScaled(e, TAROT_DMG, tc->dmgMult);
                AddFx(tc->pos, 14.0f, tc->cardCol);
                tc->active = false;
                break;
            }
        }
    }

    // Psychic Black Holes (2.0s duration, radius = 15% of total window length, pulls enemies to center in <= 0.75s)
    for (int i = 0; i < BLACKHOLE_MAX; i++) {
        BlackHole* bh = &g_blackHoles[i];
        if (!bh->active) continue;

        bh->timer -= dt;
        bh->spinAngle += 9.5f * dt;
        if (bh->timer <= 0.0f) {
            bh->active = false;
            continue;
        }

        float pullSpeed = (PSYCHIC_BH_RADIUS / PSYCHIC_BH_PULL_T) * dt;
        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == bh->team || IsKingDashing(e)) continue; // ignores teammates & dashing King
            Vector2 toCenter = V2Sub(bh->pos, e->pos);
            float dist = sqrtf(toCenter.x * toCenter.x + toCenter.y * toCenter.y);
            if (dist <= PSYCHIC_BH_RADIUS && dist > 0.001f) {
                float step = fminf(dist, pullSpeed);
                e->pos = ClampScreen(V2Add(e->pos, V2Mul(toCenter, step / dist)), (float)e->radius);
                if (e->frozen)      e->freezePos   = e->pos;
                if (e->shocked)     e->shockPos    = e->pos;
                if (e->windStunned) e->windStunPos = e->pos;
                if (e->diceLocked)  e->diceLockPos = e->pos;
                if (e->slamLocked)  e->slamLockPos = e->pos;
                if (e->execLocked)  e->execLockPos = e->pos;
            }
        }
    }

    // Gun bullets (radius = 15% of normal ball, max range = 1/3 window width, disappear on wall or enemy hit)
    for (int i = 0; i < GUN_BULLET_MAX; i++) {
        GunBullet* gb = &g_bullets[i];
        if (!gb->active) continue;
        gb->pos = V2Add(gb->pos, gb->vel);
        gb->travelled += GUN_BULLET_SPEED;

        if (gb->pos.x <= GUN_BULLET_RAD || gb->pos.x >= SCREEN_W - GUN_BULLET_RAD ||
            gb->pos.y <= GUN_BULLET_RAD || gb->pos.y >= SCREEN_H - GUN_BULLET_RAD ||
            gb->travelled >= GUN_BULLET_RANGE) {
            gb->active = false;
            continue;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == gb->team) continue;
            float hitR = (float)e->radius + GUN_BULLET_RAD;
            if (V2DistSq(gb->pos, e->pos) <= hitR * hitR) {
                TakeDamageScaled(e, GUN_BULLET_DMG, gb->dmgMult);
                gb->active = false;
                break;
            }
        }
    }

    // Cactus small bombs (explode into 8 spikes on hitting wall, enemy, or max range)
    for (int i = 0; i < CACTUS_BOMB_MAX; i++) {
        CactusBomb* cb = &g_cactusBombs[i];
        if (!cb->active) continue;

        cb->pos = V2Add(cb->pos, cb->vel);
        cb->travelled += CACTUS_BOMB_SPEED;

        bool trigger = false;
        if (cb->pos.x <= CACTUS_BOMB_RAD || cb->pos.x >= SCREEN_W - CACTUS_BOMB_RAD ||
            cb->pos.y <= CACTUS_BOMB_RAD || cb->pos.y >= SCREEN_H - CACTUS_BOMB_RAD ||
            cb->travelled >= CACTUS_BOMB_RANGE) {
            cb->pos = ClampScreen(cb->pos, (float)CACTUS_BOMB_RAD);
            trigger = true;
        } else {
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == cb->team) continue;
                float hitR = (float)(e->radius + CACTUS_BOMB_RAD);
                if (V2DistSq(cb->pos, e->pos) <= hitR * hitR) {
                    trigger = true;
                    break;
                }
            }
        }

        if (trigger) {
            ExplodeCactusBomb(cb->pos, cb->team, cb->dmgMult);
            cb->active = false;
        }
    }

    // Cactus red spikes (disappear on wall hit, enemy hit, or max range)
    for (int i = 0; i < CACTUS_SPIKE_MAX; i++) {
        CactusSpike* sp = &g_cactusSpikes[i];
        if (!sp->active) continue;

        float step = 7.5f;
        sp->dist += step;
        if (sp->curving) {
            float progress = sp->dist / sp->maxDist;
            float theta = sp->baseAngle + progress * (1.35f * PI);
            sp->pos = (Vector2){ sp->origin.x + cosf(theta) * sp->dist,
                                 sp->origin.y + sinf(theta) * sp->dist };
            sp->angle = theta + PI * 0.38f;
        } else {
            sp->pos = (Vector2){ sp->origin.x + cosf(sp->baseAngle) * sp->dist,
                                 sp->origin.y + sinf(sp->baseAngle) * sp->dist };
            sp->angle = sp->baseAngle;
        }

        if (sp->dist >= sp->maxDist ||
            sp->pos.x <= 0 || sp->pos.x >= SCREEN_W || sp->pos.y <= 0 || sp->pos.y >= SCREEN_H) {
            sp->active = false;
            continue;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == sp->team) continue;
            float hitR = e->radius + 5.0f;
            if (V2DistSq(sp->pos, e->pos) <= hitR * hitR) {
                TakeDamageScaled(e, CACTUS_SPIKE_DMG, sp->dmgMult);
                sp->active = false;
                break;
            }
        }
    }

    // Wind tornados: pierce through and stun ALL enemies along the flight path, disappear when touching a wall
    for (int i = 0; i < TORNADO_MAX; i++) {
        WindTornado* wt = &g_tornados[i];
        if (!wt->active) continue;

        wt->pos = V2Add(wt->pos, wt->vel);
        wt->travelled += TORNADO_SPEED;
        wt->spinAngle += 14.0f * dt;

        if (wt->travelled >= TORNADO_RANGE ||
            wt->pos.x <= TORNADO_RADIUS || wt->pos.x >= SCREEN_W - TORNADO_RADIUS ||
            wt->pos.y <= TORNADO_RADIUS || wt->pos.y >= SCREEN_H - TORNADO_RADIUS) {
            wt->active = false;
            continue;
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == wt->team || wt->hit[j]) continue;
            float hitR = e->radius + TORNADO_RADIUS;
            if (V2DistSq(wt->pos, e->pos) <= hitR * hitR) {
                wt->hit[j] = true;
                TakeDamageScaled(e, TORNADO_DMG, wt->dmgMult);
                if (IsCombatBall(e)) StunWithWind(e, WIND_STUN_TIME);
                AddFx(e->pos, TORNADO_RADIUS * 1.3f, COL_WIND);
                // Tornado continues flying along its path to stun any other enemies in line
            }
        }
    }

    // Wall laser turrets
    for (int i = 0; i < TURRET_MAX; i++) {
        LaserTurret* t = &g_turrets[i];
        if (!t->active) continue;

        if (t->beamTimer > 0.0f) {
            t->beamTimer -= dt;
        }
        t->shootTimer -= dt;
        if (t->shootTimer <= 0.0f) {
            t->shootTimer += TURRET_INTERVAL;
            t->beamTimer = TURRET_BEAM_TIME;

            Vector2 ab = V2Sub(t->endPos, t->pos);
            float abLenSq = ab.x * ab.x + ab.y * ab.y;
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == t->team) continue;
                Vector2 ap = V2Sub(e->pos, t->pos);
                float u = (abLenSq > 0.0001f) ? ClampF((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f) : 0.0f;
                Vector2 closest = V2Add(t->pos, V2Mul(ab, u));
                if (V2DistSq(e->pos, closest) <= (float)(e->radius * e->radius)) {
                    TakeDamageScaled(e, TURRET_LASER_DMG, t->dmgMult);
                }
            }
        }
    }

    // Flying potions (disappear immediately if touching a wall)
    for (int i = 0; i < POTION_MAX; i++)
    {
        Potion* p = &g_potions[i];
        if (!p->active) continue;
        int ownerTeam = g_balls[p->ownerIdx].team;

        if (p->kind != P_HEAL) {
            bool hitDirect = false;
            for (int j = 0; j < g_ballCount; j++) {
                Ball* e = &g_balls[j];
                if (!IsCombatBall(e) || e->team == ownerTeam) continue;
                float hitR = e->radius + 8.0f;
                if (V2DistSq(e->pos, p->pos) <= hitR * hitR) {
                    ExplodePotion(p->kind, p->pos, p->ownerIdx, true);
                    p->active = false;
                    hitDirect = true;
                    break;
                }
            }
            if (hitDirect) continue;
        }

        float dSq = V2DistSq(p->target, p->pos);
        if (dSq <= POTION_SPEED * POTION_SPEED) {
            p->pos = p->target;
            if (p->pos.x <= 2.0f || p->pos.x >= SCREEN_W - 2.0f ||
                p->pos.y <= 2.0f || p->pos.y >= SCREEN_H - 2.0f) {
                p->active = false;
            } else {
                ExplodePotion(p->kind, p->pos, p->ownerIdx, false);
                p->active = false;
            }
        } else {
            float invD = POTION_SPEED / sqrtf(dSq);
            p->pos.x += (p->target.x - p->pos.x) * invD;
            p->pos.y += (p->target.y - p->pos.y) * invD;
            if (p->pos.x <= 2.0f || p->pos.x >= SCREEN_W - 2.0f ||
                p->pos.y <= 2.0f || p->pos.y >= SCREEN_H - 2.0f) {
                p->active = false;
            }
        }
    }

    for (int i = 0; i < ZONE_MAX; i++)
    {
        Zone* z = &g_zones[i];
        if (!z->active) continue;
        z->age += dt;
        if (z->age >= ZONE_TIME) { z->active = false; continue; }

        int ownerTeam = g_balls[z->ownerIdx].team;
        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == ownerTeam) continue;
            if (!Overlaps(z->pos, z->radius, e)) continue;

            if (z->kind == P_SLOW) {
                e->slowYTimer = 2.0f;
                if (!z->hit[j]) { z->hit[j] = true; TakeDamageScaled(e, 5, g_balls[z->ownerIdx].dmgMult); }
            } else if (z->kind == P_POISON && !z->hit[j]) {
                z->hit[j] = true;
                e->poisonTicks = 5;
                e->poisonTick = 0.6f;
                e->poisonDmgMult = g_balls[z->ownerIdx].dmgMult;
                e->slowPTimer = 3.0f;
            }
        }
    }

    // Archer Burning Ground Zones (radius = 1.8x ball radius):
    // any enemy that touches the burning zone (regardless of how briefly) is afflicted with Burn (3 HP every 0.75s for 3 ticks)
    for (int i = 0; i < BURN_ZONE_MAX; i++)
    {
        BurnZone* bz = &g_burnZones[i];
        if (!bz->active) continue;
        bz->age += dt;
        if (bz->age >= ARCHER_ZONE_TIME) { bz->active = false; continue; }

        if (GetRandomValue(0, 99) < 35) {
            float a = (float)GetRandomValue(0, 359) * DEG2RAD;
            float d = (float)GetRandomValue(0, (int)(bz->radius * 0.9f));
            Color fc = (GetRandomValue(0, 1) == 0) ? (Color){ 255, 95, 15, 255 } : GOLD;
            SpawnParticle((Vector2){ bz->pos.x + cosf(a) * d, bz->pos.y + sinf(a) * d },
                          (Vector2){ (float)GetRandomValue(-5, 5) * 0.08f, -1.3f },
                          0.35f, 3.5f, fc, -0.02f, false);
        }

        for (int j = 0; j < g_ballCount; j++) {
            Ball* e = &g_balls[j];
            if (!IsCombatBall(e) || e->team == bz->team) continue;
            if (Overlaps(bz->pos, bz->radius, e)) {
                ApplyArcherBurn(e, bz->dmgMult);
            }
        }
    }

    for (int i = 0; i < FX_MAX; i++) {
        if (!g_fx[i].active) continue;
        if ((g_fx[i].age += dt) >= FX_TIME) g_fx[i].active = false;
    }

    for (int i = 0; i < PARTICLE_MAX; i++) {
        Particle* p = &g_particles[i];
        if (!p->active) continue;
        p->vel.y += p->grav;
        p->pos = V2Add(p->pos, p->vel);
        if ((p->age += dt) >= p->life) p->active = false;
    }

    for (int i = 0; i < POPUP_MAX; i++) {
        if (!g_popups[i].active) continue;
        if ((g_popups[i].age += dt) >= POPUP_TIME) g_popups[i].active = false;
    }
}

// ======================= MOVEMENT, WALLS & COLLISIONS =======================
static float GetEnemyHomingBias(const Ball* b)
{
    if (b->isSubBall) return WALL_HOMING_BIAS; // 5% for Reforge companion ball
    switch (b->type) {
        // Balls with base contact damage: 5%
        case TYPE_BURST:
        case TYPE_VERITY:
        case TYPE_CELL:
        case TYPE_CHARGE:
        case TYPE_SQUIRE:
            return WALL_HOMING_BIAS + b->homingBonus;

        // Gambler: base 5% + 5% per completed skill cast
        case TYPE_GAMBLER:
            return WALL_HOMING_BIAS + b->gambler.bonusHoming + b->homingBonus;

        // Balls with 0 initial contact dmg that deal contact/collision effect via skill,
        // plus Shackless, Range, Cactus, Psychic, and the 4 medieval melee balls (Knight, Cavalry, Executer, King): 5%
        case TYPE_DEVIL:
        case TYPE_ELECTRIC:
        case TYPE_CHESS:
        case TYPE_UNSHACKLED:
        case TYPE_JUDGEMENT:
        case TYPE_RANGE:
        case TYPE_CACTUS:
        case TYPE_PSYCHIC:
        case TYPE_KNIGHT:
        case TYPE_CAVALRY:
        case TYPE_EXECUTER:
        case TYPE_KING:
            return WALL_HOMING_BIAS + b->homingBonus;

        // All other balls with 0 contact damage: 0% enemy homing bias
        default:
            return 0.0f;
    }
}

static void BounceWalls(Ball* b, const Ball* enemy)
{
    float minX = (float)b->radius, maxX = (float)(SCREEN_W - b->radius);
    float minY = (float)b->radius, maxY = (float)(SCREEN_H - b->radius);
    bool hitX = false, hitY = false;
    Vector2 laserPos = { 0, 0 }, laserDir = { 0, 0 }, webPos = { 0, 0 };

    if (b->pos.x >= maxX) {
        b->pos.x = maxX; b->vel.x = -fabsf(b->vel.x); b->normalVel.x = -fabsf(b->normalVel.x); hitX = true;
        laserPos = (Vector2){ SCREEN_W - 4.0f, b->pos.y };
        laserDir = (Vector2){ -1.0f, 0.0f };
        webPos   = (Vector2){ SCREEN_W - 2.0f, b->pos.y };
    } else if (b->pos.x <= minX) {
        b->pos.x = minX; b->vel.x = fabsf(b->vel.x); b->normalVel.x = fabsf(b->normalVel.x); hitX = true;
        laserPos = (Vector2){ 4.0f, b->pos.y };
        laserDir = (Vector2){ 1.0f, 0.0f };
        webPos   = (Vector2){ 2.0f, b->pos.y };
    }

    if (b->pos.y >= maxY) {
        b->pos.y = maxY; b->vel.y = -fabsf(b->vel.y); b->normalVel.y = -fabsf(b->normalVel.y); hitY = true;
        if (!hitX) {
            laserPos = (Vector2){ b->pos.x, SCREEN_H - 4.0f };
            laserDir = (Vector2){ 0.0f, -1.0f };
            webPos   = (Vector2){ b->pos.x, SCREEN_H - 2.0f };
        }
    } else if (b->pos.y <= minY) {
        b->pos.y = minY; b->vel.y = fabsf(b->vel.y); b->normalVel.y = fabsf(b->normalVel.y); hitY = true;
        if (!hitX) {
            laserPos = (Vector2){ b->pos.x, 4.0f };
            laserDir = (Vector2){ 0.0f, 1.0f };
            webPos   = (Vector2){ b->pos.x, 2.0f };
        }
    }

    if (!hitX && !hitY) {
        if (b->pos.x > minX + 3.0f && b->pos.x < maxX - 3.0f &&
            b->pos.y > minY + 3.0f && b->pos.y < maxY - 3.0f) {
            b->touchingWall = false;
        }
        return;
    }

    // Cavalry crashing into a wall while in Charge state (state == 1):
    // spawns a shower of sparks and immobilizes Cavalry for 1.25s before starting the 4.0s cooldown
    if (b->type == TYPE_CAVALRY && b->cavalry.state == 1 && !IsFixed(b)) {
        SpawnLanceSparks(webPos);
        b->cavalry.state = 2;
        b->cavalry.timer = CAVALRY_STUN_T;
        b->cavalry.speedMult = 1.0f;
        b->slamLocked = true;
        b->slamLockTimer = CAVALRY_STUN_T;
        b->slamLockPos = b->pos;
    }

    // Strictly 1 turret (Laser) or 1 web anchor (Web) per wall collision
    if (!IsFixed(b) && !b->touchingWall && b->wallBounceLockTimer <= 0.0f) {
        b->touchingWall = true;
        b->wallBounceLockTimer = 0.14f;
        if (b->type == TYPE_LASER) {
            SpawnLaserTurret(laserPos, laserDir, b->team, b->dmgMult);
        } else if (b->type == TYPE_SPIDER) {
            OnSpiderWallBounce(b, webPos);
        }
    }

    if (!IsFixed(b))
    {
        // Laser and Web have 0 homing bias toward enemies, and instead gain +10% wall-seeking bias
        if (b->type == TYPE_LASER || b->type == TYPE_SPIDER) {
            float wallBias = (b->type == TYPE_SPIDER) ? SPIDER_WALL_BIAS : LASER_WALL_BIAS;
            Vector2 curDir = Norm(b->normalVel);
            float nearX = (b->pos.x < SCREEN_W * 0.5f) ? minX : maxX;
            float nearY = (b->pos.y < SCREEN_H * 0.5f) ? minY : maxY;
            Vector2 wallTarget;
            if (hitX) wallTarget = (Vector2){ b->pos.x + curDir.x * 160.0f, nearY };
            else      wallTarget = (Vector2){ nearX, b->pos.y + curDir.y * 160.0f };
            Vector2 toWall  = Norm(V2Sub(wallTarget, b->pos));
            Vector2 blended = Norm(V2Add(V2Mul(curDir, 1.0f - wallBias), V2Mul(toWall, wallBias)));
            if (hitX && ((b->pos.x <= minX + 0.5f && blended.x < 0.25f) ||
                         (b->pos.x >= maxX - 0.5f && blended.x > -0.25f))) {
                blended.x = (b->pos.x <= minX + 0.5f) ? 0.38f : -0.38f;
                blended = Norm(blended);
            }
            if (hitY && ((b->pos.y <= minY + 0.5f && blended.y < 0.25f) ||
                         (b->pos.y >= maxY - 0.5f && blended.y > -0.25f))) {
                blended.y = (b->pos.y <= minY + 0.5f) ? 0.38f : -0.38f;
                blended = Norm(blended);
            }
            SetDirection(b, blended);
            return;
        }

        const Ball* target = enemy;
        float bias = GetEnemyHomingBias(b);

        if (b->type == TYPE_REFORGE && b->subIdx >= 0 && g_balls[b->subIdx].active) {
            if (GetRandomValue(1, 100) <= 53) {
                target = &g_balls[b->subIdx];
                bias = REFORGE_HOMING_BIAS;
            }
        } else if (b->isSubBall && b->parentIdx >= 0 && IsCombatBall(&g_balls[b->parentIdx])) {
            if (GetRandomValue(1, 100) <= 53) {
                target = &g_balls[b->parentIdx];
                bias = REFORGE_HOMING_BIAS;
            }
        }

        if (target && bias > 0.0001f) {
            Vector2 curDir   = Norm(b->normalVel);
            Vector2 toTarget = Norm(V2Sub(target->pos, b->pos));
            Vector2 blended  = Norm(V2Add(V2Mul(curDir, 1.0f - bias), V2Mul(toTarget, bias)));

            if (hitX && ((b->pos.x <= minX + 0.5f && blended.x < 0.2f) ||
                         (b->pos.x >= maxX - 0.5f && blended.x > -0.2f))) {
                blended.x = (b->pos.x <= minX + 0.5f) ? 0.35f : -0.35f;
                blended = Norm(blended);
            }
            if (hitY && ((b->pos.y <= minY + 0.5f && blended.y < 0.2f) ||
                         (b->pos.y >= maxY - 0.5f && blended.y > -0.2f))) {
                blended.y = (b->pos.y <= minY + 0.5f) ? 0.35f : -0.35f;
                blended = Norm(blended);
            }
            SetDirection(b, blended);
        }
    }
}

static void ResolveDevilLinks(void)
{
    for (int i = 0; i < g_ballCount; i++) {
        Ball* linkD = &g_balls[i];
        if (!IsCombatBall(linkD) || linkD->type != TYPE_DEVIL || linkD->drainTicks <= 0) continue;
        if (linkD->drainTargetIdx < 0 || !IsCombatBall(&g_balls[linkD->drainTargetIdx])) continue;

        Ball* linkV = &g_balls[linkD->drainTargetIdx];
        if (IsFixed(linkD) && IsFixed(linkV)) continue;

        Vector2 linkDir = Norm(linkD->linkOffset);
        linkD->linkOffset = V2Mul(linkDir, (float)(linkD->radius + linkV->radius));

        if (IsFixed(linkV)) {
            linkD->pos = ClampScreen(V2Sub(linkV->pos, linkD->linkOffset), (float)linkD->radius);
            continue;
        }

        linkV->pos = V2Add(linkD->pos, linkD->linkOffset);

        float r = (float)linkV->radius;
        if (linkV->pos.x > SCREEN_W - r) {
            float s = linkV->pos.x - (SCREEN_W - r);
            linkV->pos.x -= s; linkD->pos.x -= s;
            linkD->vel.x = -fabsf(linkD->vel.x); linkD->normalVel.x = -fabsf(linkD->normalVel.x);
        } else if (linkV->pos.x < r) {
            float s = r - linkV->pos.x;
            linkV->pos.x += s; linkD->pos.x += s;
            linkD->vel.x = fabsf(linkD->vel.x); linkD->normalVel.x = fabsf(linkD->normalVel.x);
        }
        if (linkV->pos.y > SCREEN_H - r) {
            float s = linkV->pos.y - (SCREEN_H - r);
            linkV->pos.y -= s; linkD->pos.y -= s;
            linkD->vel.y = -fabsf(linkD->vel.y); linkD->normalVel.y = -fabsf(linkD->normalVel.y);
        } else if (linkV->pos.y < r) {
            float s = r - linkV->pos.y;
            linkV->pos.y += s; linkD->pos.y += s;
            linkD->vel.y = fabsf(linkD->vel.y); linkD->normalVel.y = fabsf(linkD->normalVel.y);
        }
    }
}

static void ResolvePairCollision(int i, int j)
{
    Ball* b1 = &g_balls[i];
    Ball* b2 = &g_balls[j];
    if (!IsAlive(b1) || !IsAlive(b2)) {
        g_wasColliding[i][j] = g_wasColliding[j][i] = false;
        return;
    }

    float dx = b1->pos.x - b2->pos.x;
    float dy = b1->pos.y - b2->pos.y;
    float rsum = (float)(b1->radius + b2->radius);
    float distSq = dx * dx + dy * dy;

    if (distSq > rsum * rsum) {
        g_wasColliding[i][j] = g_wasColliding[j][i] = false;
        return;
    }

    float distance = sqrtf(distSq);
    float nx = 1.0f, ny = 0.0f;
    if (distance > 0.001f) { nx = dx / distance; ny = dy / distance; }
    float overlap = rsum - distance;

    bool f1 = IsFixed(b1), f2 = IsFixed(b2);

    if (f1 && !f2)       { b2->pos.x -= nx * overlap; b2->pos.y -= ny * overlap; }
    else if (!f1 && f2)  { b1->pos.x += nx * overlap; b1->pos.y += ny * overlap; }
    else if (!f1 && !f2) {
        b1->pos.x += nx * overlap * 0.5f; b1->pos.y += ny * overlap * 0.5f;
        b2->pos.x -= nx * overlap * 0.5f; b2->pos.y -= ny * overlap * 0.5f;
    }

    bool pairPulled = (b1->type == TYPE_HARPOON && b1->harpoon.state == H_PULLING && b1->harpoon.victimIdx == j) ||
                      (b2->type == TYPE_HARPOON && b2->harpoon.state == H_PULLING && b2->harpoon.victimIdx == i);

    if (!g_wasColliding[i][j] && !pairPulled)
    {
        if (!f1) ReflectAway(b1,  nx,  ny);
        if (!f2) ReflectAway(b2, -nx, -ny);

        bool isReforgePair = (b1->type == TYPE_REFORGE && b1->subIdx == j) ||
                             (b2->type == TYPE_REFORGE && b2->subIdx == i);
        if (isReforgePair) {
            Ball* mainB = (b1->type == TYPE_REFORGE) ? b1 : b2;
            Ball* subB  = (b1->type == TYPE_REFORGE) ? b2 : b1;
            mainB->reforgeLevel++;
            subB->reforgeLevel = mainB->reforgeLevel;
            subB->damage = (int)roundf(REFORGE_BASE_DMG * powf(REFORGE_LVL_MULT, (float)(subB->reforgeLevel - 1)));
            Vector2 mid = V2Mul(V2Add(mainB->pos, subB->pos), 0.5f);
            for (int k = 0; k < 10; k++) {
                float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                float sp = 2.0f + (float)GetRandomValue(0, 25) * 0.1f;
                SpawnParticle(mid, (Vector2){ cosf(a) * sp, sinf(a) * sp }, 0.35f, 3.0f, GOLD, 0.08f, false);
            }
        } else if (b1->team != b2->team) {
            ApplyContact(i, j);
            if (IsAlive(b1) && IsAlive(b2)) ApplyContact(j, i);
        }
    } else if (b1->team != b2->team && !pairPulled) {
        // If two balls remain in contact across frames (e.g. while slam-locked after Leap Slam),
        // allow Shackless's Chain Lash to trigger once the 0.25s lashCooldown has elapsed
        if (b1->type == TYPE_UNSHACKLED && b1->unshackled.passiveStacks > 0 && b1->unshackled.lashCooldown <= 0.0f) {
            ApplyContact(i, j);
        }
        if (IsAlive(b1) && IsAlive(b2) && b2->type == TYPE_UNSHACKLED &&
            b2->unshackled.passiveStacks > 0 && b2->unshackled.lashCooldown <= 0.0f) {
            ApplyContact(j, i);
        }
    }
    g_wasColliding[i][j] = g_wasColliding[j][i] = true;
}

// ======================= DRAWING FUNCTIONS =======================
static void DrawChessboardArena(void)
{
    int tile = 50;
    Color lightSq = { 226, 226, 230, 255 };
    Color darkSq  = { 176, 178, 184, 255 };
    for (int y = 0; y < SCREEN_H; y += tile) {
        for (int x = 0; x < SCREEN_W; x += tile) {
            bool isDark = (((x / tile) + (y / tile)) & 1) != 0;
            DrawRectangle(x, y, tile, tile, isDark ? darkSq : lightSq);
        }
    }
}

static void DrawVerityFace(const Ball* b)
{
    float x = b->pos.x, y = b->pos.y;
    float s = (float)b->radius / (float)BASE_RADIUS;

    if (b->verityForm == 1) {
        DrawCircle((int)(x - 10 * s), (int)(y - 5 * s), 4 * s, BLACK);
        DrawCircle((int)(x + 10 * s), (int)(y - 5 * s), 4 * s, BLACK);
        DrawRing((Vector2){ x, y + 2 * s }, 9 * s, 13 * s, 20, 160, 16, BLACK);
    } else if (b->verityForm == 2) {
        DrawCircle((int)(x - 10 * s), (int)(y - 5 * s), 4 * s, BLACK);
        DrawCircle((int)(x + 10 * s), (int)(y - 5 * s), 4 * s, BLACK);
        DrawRectangle((int)(x - 12 * s), (int)(y + 8 * s), (int)(24 * s), (int)(4 * s), BLACK);
    } else {
        DrawLineEx((Vector2){ x - 15 * s, y - 12 * s }, (Vector2){ x - 5 * s, y - 2 * s }, 5 * s, BLACK);
        DrawLineEx((Vector2){ x + 15 * s, y - 12 * s }, (Vector2){ x + 5 * s, y - 2 * s }, 5 * s, BLACK);
        DrawCircle((int)(x - 10 * s), (int)(y - 2 * s), 4 * s, WHITE);
        DrawCircle((int)(x + 10 * s), (int)(y - 2 * s), 4 * s, WHITE);
        DrawLineEx((Vector2){ x - 14 * s, y + 12 * s }, (Vector2){ x - 5 * s, y + 6 * s }, 4 * s, BLACK);
        DrawLineEx((Vector2){ x - 5 * s, y + 6 * s },   (Vector2){ x + 5 * s, y + 14 * s }, 4 * s, BLACK);
        DrawLineEx((Vector2){ x + 5 * s, y + 14 * s },  (Vector2){ x + 14 * s, y + 8 * s }, 4 * s, BLACK);
    }
}

static void DrawDevilFangs(const Ball* b, const Ball* enemy)
{
    Vector2 d = enemy ? Norm(V2Sub(enemy->pos, b->pos)) : (Vector2){ 1.0f, 0.0f };
    Vector2 n = { -d.y, d.x };

    for (int s = -1; s <= 1; s += 2)
    {
        Vector2 c = V2Add(b->pos, V2Add(V2Mul(d, b->radius - 6.0f), V2Mul(n, 11.0f * s)));
        Vector2 a1  = V2Add(c, V2Mul(n, -5.0f));
        Vector2 a2  = V2Add(c, V2Mul(n, 5.0f));
        Vector2 tip = V2Add(c, V2Mul(d, 16.0f));
        DrawTri(a1, a2, tip, WHITE);
        DrawLineEx(a1, tip, 1.5f, BLACK);
        DrawLineEx(a2, tip, 1.5f, BLACK);
        DrawLineEx(a1, a2,  1.5f, BLACK);
    }
}

static void DrawBurstFireRim(const Ball* b)
{
    int spikes = 16;
    float step = 2.0f * PI / (float)spikes;
    float r = (float)b->radius;
    Vector2 dir = Norm(b->vel);
    Vector2 back = { -dir.x, -dir.y };

    DrawCircleV(b->pos, r + 6.0f + (float)GetRandomValue(0, 4), Fade((Color){ 255, 70, 0, 255 }, 0.28f));

    for (int i = 0; i < spikes; i++)
    {
        float a = i * step + (float)GetRandomValue(-12, 12) * 0.01f;
        Vector2 radial = { cosf(a), sinf(a) };
        Vector2 tang   = { -radial.y, radial.x };

        Vector2 outDir = b->isDashing ? Norm(V2Add(radial, V2Mul(back, 0.65f))) : radial;
        float len = (b->isDashing ? 12.0f : 9.0f) + (float)GetRandomValue(0, 10);

        Vector2 centerBase = V2Add(b->pos, V2Mul(radial, r - 2.0f));
        Vector2 b1  = V2Add(centerBase, V2Mul(tang, -5.5f));
        Vector2 b2  = V2Add(centerBase, V2Mul(tang,  5.5f));
        Vector2 tip = V2Add(centerBase, V2Mul(outDir, len));

        DrawTri(b1, b2, tip, Fade((Color){ 255, 60, 0, 255 }, 0.85f));

        Vector2 ib1  = V2Add(centerBase, V2Mul(tang, -3.0f));
        Vector2 ib2  = V2Add(centerBase, V2Mul(tang,  3.0f));
        Vector2 itip = V2Add(centerBase, V2Mul(outDir, len * 0.6f));
        DrawTri(ib1, ib2, itip, Fade(YELLOW, 0.9f));
    }
}

static void DrawBurstCenterFlame(Vector2 c)
{
    Color outer = { 220, 40, 0, 255 };
    Color mid   = { 255, 145, 0, 255 };
    Color core  = { 255, 240, 120, 255 };

    DrawCircleV((Vector2){ c.x, c.y + 4 }, 9.0f, outer);
    DrawTri((Vector2){ c.x - 9, c.y + 3 }, (Vector2){ c.x + 9, c.y + 3 }, (Vector2){ c.x, c.y - 16 }, outer);
    DrawTri((Vector2){ c.x - 8, c.y + 5 }, (Vector2){ c.x - 1, c.y + 2 }, (Vector2){ c.x - 11, c.y - 8 }, outer);
    DrawTri((Vector2){ c.x + 1, c.y + 2 }, (Vector2){ c.x + 8, c.y + 5 }, (Vector2){ c.x + 11, c.y - 8 }, outer);

    DrawCircleV((Vector2){ c.x, c.y + 5 }, 6.0f, mid);
    DrawTri((Vector2){ c.x - 6, c.y + 4 }, (Vector2){ c.x + 6, c.y + 4 }, (Vector2){ c.x, c.y - 11 }, mid);

    DrawCircleV((Vector2){ c.x, c.y + 6 }, 3.5f, core);
    DrawTri((Vector2){ c.x - 3.5f, c.y + 5 }, (Vector2){ c.x + 3.5f, c.y + 5 }, (Vector2){ c.x, c.y - 5 }, core);
}

static void DrawBlackArrow(Vector2 pos, float angle, float len, float scale)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };
    float L = len * scale;
    Vector2 tail     = V2Add(pos, V2Mul(d, -L * 0.5f));
    Vector2 headBase = V2Add(pos, V2Mul(d,  L * 0.15f));
    Vector2 tip      = V2Add(pos, V2Mul(d,  L * 0.55f));

    DrawLineEx(tail, headBase, 3.5f * scale, BLACK);
    DrawTri(tip, V2Add(headBase, V2Mul(n, 6.5f * scale)), V2Add(headBase, V2Mul(n, -6.5f * scale)), BLACK);
    DrawTri(headBase, V2Add(headBase, V2Add(V2Mul(d, -4.0f * scale), V2Mul(n,  7.5f * scale))),
            V2Add(headBase, V2Mul(n, 2.0f * scale)), BLACK);
    DrawTri(headBase, V2Add(headBase, V2Add(V2Mul(d, -4.0f * scale), V2Mul(n, -7.5f * scale))),
            V2Add(headBase, V2Mul(n, -2.0f * scale)), BLACK);
}

static void DrawHarpoonCenterEmblem(const Ball* b, const Ball* enemy)
{
    Vector2 c = b->pos;
    DrawRing(c, 4.0f, 6.0f, 0.0f, 360.0f, 24, BLACK);
    DrawRing(c, 8.5f, 10.5f, 0.0f, 360.0f, 24, BLACK);
    DrawRing(c, 13.0f, 14.8f, 0.0f, 360.0f, 24, BLACK);

    if (b->harpoon.state == H_READY) {
        float a = enemy ? atan2f(enemy->pos.y - c.y, enemy->pos.x - c.x) : 0.0f;
        DrawBlackArrow(c, a, 28.0f, 1.0f);
    }
}

// Tilted 3D ice cube with unequal face areas for a natural perspective look
static void DrawIce3DCube(Vector2 c)
{
    Vector2 pCenter = { c.x + 2.5f, c.y - 1.5f };
    Vector2 pTop    = { c.x - 2.0f, c.y - 15.0f };
    Vector2 pTopR   = { c.x + 14.0f, c.y - 8.5f };
    Vector2 pBotR   = { c.x + 12.5f, c.y + 8.5f };
    Vector2 pBot    = { c.x + 1.5f, c.y + 15.0f };
    Vector2 pBotL   = { c.x - 13.5f, c.y + 6.5f };
    Vector2 pTopL   = { c.x - 14.5f, c.y - 6.0f };

    Color topCol   = WHITE;
    Color leftCol  = SKYBLUE;
    Color rightCol = { 55, 145, 220, 255 };

    // Top face (tilted perspective)
    DrawTri(pCenter, pTopL, pTop, topCol);
    DrawTri(pCenter, pTop, pTopR, topCol);
    // Broad Left-Front face (largest area)
    DrawTri(pCenter, pBotL, pTopL, leftCol);
    DrawTri(pCenter, pBot, pBotL, leftCol);
    // Narrower Right-Side face (foreshortened)
    DrawTri(pCenter, pTopR, pBotR, rightCol);
    DrawTri(pCenter, pBotR, pBot, rightCol);

    DrawLineEx(pTop, pTopR, 1.5f, WHITE);
    DrawLineEx(pTopR, pBotR, 1.5f, BLACK);
    DrawLineEx(pBotR, pBot, 1.5f, BLACK);
    DrawLineEx(pBot, pBotL, 1.5f, BLACK);
    DrawLineEx(pBotL, pTopL, 1.5f, BLACK);
    DrawLineEx(pTopL, pTop, 1.5f, WHITE);
    DrawLineEx(pCenter, pTopL, 1.5f, SKYBLUE);
    DrawLineEx(pCenter, pTopR, 1.5f, WHITE);
    DrawLineEx(pCenter, pBot, 1.5f, WHITE);
}

static void DrawTridentCrown(const Ball* b)
{
    float x = b->pos.x;
    float baseY = b->pos.y - b->radius + 3.0f;
    float w = 15.0f;

    Vector2 bl = { x - w, baseY };
    Vector2 br = { x + w, baseY };
    Vector2 tl = { x - w - 3.0f, baseY - 14.0f };
    Vector2 vl = { x - w * 0.45f, baseY - 6.0f };
    Vector2 tc = { x, baseY - 17.0f };
    Vector2 vr = { x + w * 0.45f, baseY - 6.0f };
    Vector2 tr = { x + w + 3.0f, baseY - 14.0f };

    DrawTri(bl, vl, tl, GOLD);
    DrawTri(bl, vr, vl, GOLD);
    DrawTri(vl, vr, tc, GOLD);
    DrawTri(bl, br, vr, GOLD);
    DrawTri(vr, br, tr, GOLD);

    DrawLineEx(bl, br, 2.0f, BLACK);
    DrawLineEx(bl, tl, 1.5f, BLACK);
    DrawLineEx(tl, vl, 1.5f, BLACK);
    DrawLineEx(vl, tc, 1.5f, BLACK);
    DrawLineEx(tc, vr, 1.5f, BLACK);
    DrawLineEx(vr, tr, 1.5f, BLACK);
    DrawLineEx(tr, br, 1.5f, BLACK);
    DrawCircleV(tl, 2.2f, RED);
    DrawCircleV(tc, 2.5f, RED);
    DrawCircleV(tr, 2.2f, RED);
}

static void DrawWitchHat(const Ball* b)
{
    float x = b->pos.x;
    float brimY = b->pos.y - b->radius + 4.0f;
    Color hatCol = { 35, 18, 52, 255 };
    Color bandCol = { 185, 50, 220, 255 };

    Vector2 cL = { x - 14.0f, brimY };
    Vector2 cR = { x + 14.0f, brimY };
    Vector2 tip = { x + 6.0f, brimY - 25.0f };
    DrawTri(cL, cR, tip, hatCol);
    DrawLineEx(cL, tip, 1.5f, BLACK);
    DrawLineEx(cR, tip, 1.5f, BLACK);

    DrawLineEx((Vector2){ x - 12.0f, brimY - 3.0f }, (Vector2){ x + 12.0f, brimY - 3.0f }, 4.0f, bandCol);
    DrawRectangleLines((int)x - 3, (int)brimY - 6, 6, 5, GOLD);

    DrawLineEx((Vector2){ x - 24.0f, brimY }, (Vector2){ x + 24.0f, brimY }, 5.0f, hatCol);
    DrawLineEx((Vector2){ x - 24.0f, brimY }, (Vector2){ x + 24.0f, brimY }, 1.5f, BLACK);
}

static void DrawBowEmblem(const Ball* b)
{
    float a = b->bow.aimAngle;
    Vector2 d = { cosf(a), sinf(a) };
    Vector2 n = { -d.y, d.x };
    Vector2 c = b->pos;

    Color wood = { 95, 45, 12, 255 };
    Vector2 topTip = V2Add(c, V2Add(V2Mul(d, -4.0f), V2Mul(n, 16.0f)));
    Vector2 midTop = V2Add(c, V2Add(V2Mul(d, 7.0f),  V2Mul(n, 9.0f)));
    Vector2 handle = V2Add(c, V2Mul(d, 10.0f));
    Vector2 midBot = V2Add(c, V2Add(V2Mul(d, 7.0f),  V2Mul(n, -9.0f)));
    Vector2 botTip = V2Add(c, V2Add(V2Mul(d, -4.0f), V2Mul(n, -16.0f)));

    DrawLineEx(topTip, botTip, 1.8f, RAYWHITE);
    DrawLineEx(topTip, midTop, 3.5f, wood);
    DrawLineEx(midTop, handle, 4.0f, wood);
    DrawLineEx(handle, midBot, 4.0f, wood);
    DrawLineEx(midBot, botTip, 3.5f, wood);

    if (!b->bow.active) {
        Vector2 aTail = V2Add(c, V2Mul(d, -5.0f));
        Vector2 aTip  = V2Add(c, V2Mul(d, 16.0f));
        DrawLineEx(aTail, aTip, 2.5f, WHITE);
        DrawTri(V2Add(aTip, V2Mul(d, 5.0f)),
                V2Add(aTip, V2Mul(n, 4.0f)),
                V2Add(aTip, V2Mul(n, -4.0f)), WHITE);
    }
}

static void DrawFlyingBowArrow(const BowData* bw)
{
    if (!bw->active) return;
    Vector2 d = { cosf(bw->angle), sinf(bw->angle) };
    Vector2 n = { -d.y, d.x };

    float halfL = BOW_ARROW_LEN * 0.5f;
    Vector2 tail = V2Add(bw->pos, V2Mul(d, -halfL));
    Vector2 headBase = V2Add(bw->pos, V2Mul(d, halfL - 12.0f));
    Vector2 tip  = V2Add(bw->pos, V2Mul(d, halfL));

    DrawLineEx(tail, headBase, 6.0f, BLACK);
    DrawLineEx(tail, headBase, 3.6f, WHITE);

    Vector2 hL = V2Add(headBase, V2Mul(n, 7.0f));
    Vector2 hR = V2Add(headBase, V2Mul(n, -7.0f));
    DrawTri(tip, hL, hR, WHITE);
    DrawLineEx(tip, hL, 1.5f, BLACK);
    DrawLineEx(tip, hR, 1.5f, BLACK);
    DrawLineEx(hL,  hR, 1.5f, BLACK);

    Vector2 f1 = V2Add(tail, V2Mul(d, 8.0f));
    DrawLineEx(f1, V2Add(tail, V2Mul(n, 5.0f)), 2.5f, WHITE);
    DrawLineEx(f1, V2Add(tail, V2Mul(n, -5.0f)), 2.5f, WHITE);
}

static void DrawReforgeHammer(Vector2 c)
{
    float a = -45.0f * DEG2RAD;
    Vector2 u = { cosf(a), sinf(a) };
    Vector2 v = { -u.y, u.x };

    Color handleCol = { 135, 80, 35, 255 };
    Color headCol   = { 225, 228, 232, 255 };

    Vector2 hStart = V2Add(c, V2Mul(u, -14.0f));
    Vector2 hEnd   = V2Add(c, V2Mul(u, 6.0f));
    DrawLineEx(hStart, hEnd, 6.5f, BLACK);
    DrawLineEx(hStart, hEnd, 4.5f, handleCol);

    Vector2 headCenter = V2Add(c, V2Mul(u, 7.0f));
    Vector2 p1 = V2Add(headCenter, V2Add(V2Mul(v, -11.0f), V2Mul(u, -5.0f)));
    Vector2 p2 = V2Add(headCenter, V2Add(V2Mul(v,  11.0f), V2Mul(u, -5.0f)));
    Vector2 p3 = V2Add(headCenter, V2Add(V2Mul(v,  11.0f), V2Mul(u,  5.0f)));
    Vector2 p4 = V2Add(headCenter, V2Add(V2Mul(v, -11.0f), V2Mul(u,  5.0f)));

    DrawTri(p1, p2, p3, headCol);
    DrawTri(p1, p3, p4, headCol);
    DrawLineEx(p1, p2, 1.5f, BLACK);
    DrawLineEx(p2, p3, 1.5f, BLACK);
    DrawLineEx(p3, p4, 1.5f, BLACK);
    DrawLineEx(p4, p1, 1.5f, BLACK);
}

static void DrawKunaiShape(Vector2 pos, float angle)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };
    Color bladeCol = { 70, 75, 82, 255 };

    Vector2 tip   = V2Add(pos, V2Mul(d, 12.0f));
    Vector2 mid   = V2Add(pos, V2Mul(d, -1.0f));
    Vector2 left  = V2Add(mid, V2Mul(n, 4.5f));
    Vector2 right = V2Add(mid, V2Mul(n, -4.5f));
    Vector2 base  = V2Add(pos, V2Mul(d, -5.0f));
    Vector2 ringC = V2Add(pos, V2Mul(d, -10.0f));

    DrawLineEx(base, V2Add(pos, V2Mul(d, -8.0f)), 2.5f, BLACK);
    DrawRing(ringC, 1.5f, 3.2f, 0.0f, 360.0f, 12, BLACK);

    DrawTri(tip, left, right, bladeCol);
    DrawTri(base, right, left, bladeCol);
    DrawLineEx(tip, left, 1.2f, BLACK);
    DrawLineEx(tip, right, 1.2f, BLACK);
}

static void DrawNinjaHeadbandAndFan(const Ball* b)
{
    float r = (float)b->radius;
    float yTop = b->pos.y - 16.0f;
    float yBot = b->pos.y - 8.0f;
    float dyMid = -12.0f;
    float halfW = sqrtf(fmaxf(0.0f, r * r - dyMid * dyMid)) - 1.0f;

    DrawRectangleRounded((Rectangle){ b->pos.x - halfW, yTop, halfW * 2.0f, yBot - yTop }, 0.3f, 6, BLACK);
    DrawRectangle((int)b->pos.x - 8, (int)yTop + 2, 16, 4, (Color){ 195, 205, 215, 255 });
    Vector2 knot = { b->pos.x - halfW + 1.0f, yTop + 3.0f };
    DrawLineEx(knot, (Vector2){ knot.x - 10.0f, knot.y - 5.0f }, 3.0f, BLACK);
    DrawLineEx(knot, (Vector2){ knot.x - 11.0f, knot.y + 4.0f }, 3.0f, BLACK);

    int n = b->ninja.currentKunai;
    int maxN = (b->ninja.targetMaxKunai > 0) ? b->ninja.targetMaxKunai : 3;
    float fanStep = 12.0f * DEG2RAD;
    float startOffset = -((float)(maxN - 1) * 0.5f) * fanStep;

    for (int k = 0; k < n; k++) {
        float a = b->ninja.aimAngle + startOffset + k * fanStep;
        Vector2 kp = { b->pos.x + cosf(a) * (r + 8.0f), b->pos.y + sinf(a) * (r + 8.0f) };
        DrawKunaiShape(kp, a);
    }
}

static void DrawCellNucleus(const Ball* b)
{
    float s = (float)b->radius / (float)BASE_RADIUS;
    Color nucCol = { 55, 165, 65, 255 };
    DrawCircleV(b->pos, 8.0f * s, Fade(nucCol, 0.7f));
    DrawCircleV((Vector2){ b->pos.x - 6.0f * s, b->pos.y - 5.0f * s }, 3.0f * s, Fade(WHITE, 0.55f));
}

static void DrawLaserCore(const Ball* b)
{
    DrawCircleV(b->pos, 9.0f, WHITE);
    DrawCircleV(b->pos, 5.0f, (Color){ 255, 220, 80, 255 });
    DrawRing(b->pos, 11.0f, 13.0f, 0.0f, 360.0f, 24, BLACK);
}

static void DrawCactusOuterSpikes(Vector2 pos, float r, float spikeLen)
{
    for (int k = 0; k < 8; k++) {
        float a = k * (2.0f * PI / 8.0f);
        Vector2 d = { cosf(a), sinf(a) };
        Vector2 n = { -d.y, d.x };
        Vector2 base = V2Add(pos, V2Mul(d, r - 1.0f));
        Vector2 b1   = V2Add(base, V2Mul(n, -4.0f));
        Vector2 b2   = V2Add(base, V2Mul(n,  4.0f));
        Vector2 tip  = V2Add(base, V2Mul(d, spikeLen));
        DrawTri(b1, b2, tip, RED);
        DrawLineEx(b1, tip, 1.2f, BLACK);
        DrawLineEx(b2, tip, 1.2f, BLACK);
    }
}

static void DrawCactusDetails(const Ball* b)
{
    Color ribCol = { 18, 88, 32, 255 };
    DrawLineEx((Vector2){ b->pos.x, b->pos.y - b->radius + 4 }, (Vector2){ b->pos.x, b->pos.y + b->radius - 4 }, 2.0f, ribCol);
    DrawLineEx((Vector2){ b->pos.x - 10, b->pos.y - b->radius + 7 }, (Vector2){ b->pos.x - 10, b->pos.y + b->radius - 7 }, 2.0f, ribCol);
    DrawLineEx((Vector2){ b->pos.x + 10, b->pos.y - b->radius + 7 }, (Vector2){ b->pos.x + 10, b->pos.y + b->radius - 7 }, 2.0f, ribCol);

    for (int i = 0; i < CACTUS_MAX_AMMO; i++) {
        Vector2 p = { b->pos.x + (i - 1) * 9.0f, b->pos.y };
        Color c = (i < b->cactus.ammo) ? RED : Fade(BLACK, 0.35f);
        DrawCircleV(p, 3.2f, c);
        DrawCircleLines((int)p.x, (int)p.y, 3.2f, BLACK);
    }
}

static void DrawSwirlVortex(Vector2 c, float r, float spinOffset)
{
    for (int arm = 0; arm < 3; arm++) {
        float baseA = spinOffset + arm * (2.0f * PI / 3.0f);
        Color col = (arm == 0) ? WHITE : (arm == 1 ? DARKGRAY : BLACK);
        Vector2 prev = c;
        for (int s = 1; s <= 7; s++) {
            float frac = (float)s / 7.0f;
            float a = baseA + frac * 2.4f;
            float rad = frac * (r - 3.0f);
            Vector2 cur = { c.x + cosf(a) * rad, c.y + sinf(a) * rad };
            DrawLineEx(prev, cur, 2.8f * (1.15f - frac * 0.5f), col);
            prev = cur;
        }
    }
    DrawCircleV(c, 3.5f, WHITE);
    DrawCircleLines((int)c.x, (int)c.y, 3.5f, BLACK);
}

static void DrawChessBallPattern(const Ball* b)
{
    float r = (float)b->radius;
    Vector2 c = b->pos;

    int rings = 3;
    int sectors = 8;
    float dr = (r - 1.5f) / (float)rings;
    for (int ri = 0; ri < rings; ri++) {
        float r0 = ri * dr;
        float r1 = (ri + 1) * dr;
        for (int si = 0; si < sectors; si++) {
            Color sqCol = ((ri + si) & 1) ? (Color){ 30, 30, 35, 255 } : (Color){ 242, 242, 245, 255 };
            DrawRing(c, r0, r1, si * 45.0f, (si + 1) * 45.0f, 6, sqCol);
        }
    }

    if (b->chess.active && b->chess.piece != CHESS_NONE) {
        static const char* const sym[5] = { "", "Q", "R", "N", "B" };
        const char* s = sym[b->chess.piece];
        DrawCircleV(c, 10.0f, GOLD);
        DrawCircleLines((int)c.x, (int)c.y, 10.0f, BLACK);
        DrawText(s, (int)c.x - MeasureText(s, 16) / 2, (int)c.y - 8, 16, BLACK);
    }
}

static Color LerpColorLinear(Color a, Color b, float t)
{
    t = ClampF(t, 0.0f, 1.0f);
    return (Color){
        (unsigned char)roundf(a.r + (b.r - a.r) * t),
        (unsigned char)roundf(a.g + (b.g - a.g) * t),
        (unsigned char)roundf(a.b + (b.b - a.b) * t),
        (unsigned char)roundf(a.a + (b.a - a.a) * t)
    };
}

// Gun: rotating 6-chamber ammo cylinder in the center + 6-barrel gatling bundle protruding outside the ball
// (barrels heat up from gray-black -> pale yellow-orange -> deep orange during 1.25s rapid fire, then cool down over 2.5s)
static void DrawGunGatling(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    float aim = b->gun.aimAngle;
    Vector2 d = { cosf(aim), sinf(aim) };
    Vector2 n = { -d.y, d.x };

    Color darkMetal  = { 50, 54, 62, 255 };
    Color midMetal   = { 95, 100, 110, 255 };
    Color holeCol    = { 20, 22, 25, 255 };
    Color paleOrange = { 255, 212, 95, 255 };  // pale yellow-orange overheat start
    Color deepOrange = { 255, 98, 15, 255 };   // deep yellow-orange peak overheat

    float heat = ClampF(b->gun.heat, 0.0f, 1.0f);
    Color barrelDark = darkMetal;
    Color barrelMid  = midMetal;
    if (heat > 0.001f) {
        if (heat <= 0.5f) {
            float t = heat * 2.0f;
            barrelDark = LerpColorLinear(darkMetal, (Color){ 195, 145, 55, 255 }, t);
            barrelMid  = LerpColorLinear(midMetal,  paleOrange, t);
        } else {
            float t = (heat - 0.5f) * 2.0f;
            barrelDark = LerpColorLinear((Color){ 195, 145, 55, 255 }, (Color){ 215, 55, 10, 255 }, t);
            barrelMid  = LerpColorLinear(paleOrange, deepOrange, t);
        }
    }

    // 1. Protruding 6-barrel gatling bundle sticking OUT of the ball (drawn with 5 parallel barrel lines)
    Vector2 barrelBase = V2Add(c, V2Mul(d, r - 2.0f));
    Vector2 barrelTip  = V2Add(c, V2Mul(d, r + 18.0f));

    // Outer black silhouette housing for the protruding barrel bundle
    DrawLineEx(barrelBase, barrelTip, 16.5f, BLACK);
    DrawLineEx(barrelBase, barrelTip, 14.0f, barrelDark);

    // 5 parallel longitudinal lines representing the individual barrels of the 6-barrel gatling bundle
    static const float barrelOffsets[5] = { -5.6f, -2.8f, 0.0f, 2.8f, 5.6f };
    for (int i = 0; i < 5; i++) {
        Vector2 p0 = V2Add(barrelBase, V2Mul(n, barrelOffsets[i]));
        Vector2 p1 = V2Add(barrelTip,  V2Mul(n, barrelOffsets[i]));
        DrawLineEx(p0, p1, 1.9f, barrelMid);
    }

    // Two clamping bands across the protruding barrel bundle
    Vector2 clamp1 = V2Add(c, V2Mul(d, r + 5.0f));
    Vector2 clamp2 = V2Add(c, V2Mul(d, r + 14.0f));
    DrawLineEx(V2Add(clamp1, V2Mul(n, -7.8f)), V2Add(clamp1, V2Mul(n, 7.8f)), 2.6f, barrelDark);
    DrawLineEx(V2Add(clamp2, V2Mul(n, -7.8f)), V2Add(clamp2, V2Mul(n, 7.8f)), 2.6f, barrelDark);
    DrawLineEx(V2Add(clamp1, V2Mul(n, -7.8f)), V2Add(clamp1, V2Mul(n, 7.8f)), 1.2f, BLACK);
    DrawLineEx(V2Add(clamp2, V2Mul(n, -7.8f)), V2Add(clamp2, V2Mul(n, 7.8f)), 1.2f, BLACK);

    // Subtle heat glow around the barrel when overheated
    if (heat > 0.08f) {
        Vector2 midBarrel = V2Add(c, V2Mul(d, r + 8.0f));
        DrawCircleV(midBarrel, 11.0f, Fade(deepOrange, 0.28f * heat));
    }

    // Muzzle flash during rapid-fire phase (phase == 2)
    if (b->gun.phase == 2) {
        Vector2 flashTip = V2Add(barrelTip, V2Mul(d, 9.0f));
        DrawTri(V2Add(barrelTip, V2Mul(n, -5.5f)), V2Add(barrelTip, V2Mul(n, 5.5f)), flashTip, Fade(ORANGE, 0.88f));
    }

    // 2. Center of the ball: ONLY the rotating 6-chamber cylinder (no internal protruding barrel)
    DrawCircleV(c, 12.5f, darkMetal);
    DrawCircleLines((int)c.x, (int)c.y, 12.5f, BLACK);
    DrawCircleV(c, 3.2f, midMetal);

    for (int k = 0; k < 6; k++) {
        float a = b->gun.barrelSpin + k * (2.0f * PI / 6.0f);
        Vector2 bp = { c.x + cosf(a) * 7.2f, c.y + sinf(a) * 7.2f };
        DrawCircleV(bp, 2.9f, midMetal);
        DrawCircleV(bp, 1.8f, holeCol);
    }
}

// Helper to draw standard die pips (1..6) inside a square face centered at `c` with half-size `hs`
static void DrawDiePips2D(Vector2 c, float hs, int val, Color pipCol)
{
    float d = hs * 0.52f;
    float r = fmaxf(2.0f, hs * 0.18f);
    if (val == 1 || val == 3 || val == 5)
        DrawCircleV(c, r, pipCol);
    if (val >= 2) {
        DrawCircleV((Vector2){ c.x - d, c.y - d }, r, pipCol);
        DrawCircleV((Vector2){ c.x + d, c.y + d }, r, pipCol);
    }
    if (val >= 4) {
        DrawCircleV((Vector2){ c.x + d, c.y - d }, r, pipCol);
        DrawCircleV((Vector2){ c.x - d, c.y + d }, r, pipCol);
    }
    if (val == 6) {
        DrawCircleV((Vector2){ c.x - d, c.y }, r, pipCol);
        DrawCircleV((Vector2){ c.x + d, c.y }, r, pipCol);
    }
}

// 3D cube showing 3 die faces (white, red, and black) inside Dice ball
static void DrawDice3DEmblem(const Ball* b)
{
    Vector2 c = b->pos;
    float a = b->dice.rolling ? b->dice.spinAngle : 0.25f;
    float cs = cosf(a) * 2.0f, sn = sinf(a) * 2.0f;

    Vector2 pCenter = { c.x + 1.5f + cs * 0.4f, c.y - 1.0f + sn * 0.4f };
    Vector2 pTop    = { c.x - 1.5f, c.y - 16.0f };
    Vector2 pTopR   = { c.x + 15.0f, c.y - 8.0f };
    Vector2 pBotR   = { c.x + 13.5f, c.y + 8.5f };
    Vector2 pBot    = { c.x + 1.0f, c.y + 15.5f };
    Vector2 pBotL   = { c.x - 14.0f, c.y + 7.0f };
    Vector2 pTopL   = { c.x - 15.0f, c.y - 6.5f };

    // 3 visible faces using White, Red, and Black
    DrawTri(pCenter, pTopL, pTop, WHITE);
    DrawTri(pCenter, pTop, pTopR, WHITE);
    DrawTri(pCenter, pBotL, pTopL, (Color){ 220, 30, 35, 255 });
    DrawTri(pCenter, pBot, pBotL, (Color){ 220, 30, 35, 255 });
    DrawTri(pCenter, pTopR, pBotR, (Color){ 32, 32, 38, 255 });
    DrawTri(pCenter, pBotR, pBot, (Color){ 32, 32, 38, 255 });

    DrawLineEx(pTop, pTopR, 1.5f, BLACK);
    DrawLineEx(pTopR, pBotR, 1.5f, BLACK);
    DrawLineEx(pBotR, pBot, 1.5f, BLACK);
    DrawLineEx(pBot, pBotL, 1.5f, BLACK);
    DrawLineEx(pBotL, pTopL, 1.5f, BLACK);
    DrawLineEx(pTopL, pTop, 1.5f, BLACK);
    DrawLineEx(pCenter, pTopL, 1.5f, BLACK);
    DrawLineEx(pCenter, pTopR, 1.5f, BLACK);
    DrawLineEx(pCenter, pBot, 1.5f, WHITE);

    // Pips on the 3 faces: Top (1 red pip), Left (2 white pips), Right (3 white pips)
    Vector2 topC  = { (pCenter.x + pTop.x) * 0.5f, (pCenter.y + pTop.y) * 0.5f };
    Vector2 leftC = { (pCenter.x + pBotL.x) * 0.5f, (pCenter.y + pBotL.y) * 0.5f };
    Vector2 rightC= { (pCenter.x + pBotR.x) * 0.5f, (pCenter.y + pBotR.y) * 0.5f };
    DrawCircleV(topC, 2.3f, RED);
    DrawCircleV((Vector2){ leftC.x - 2.5f, leftC.y - 2.0f }, 1.8f, WHITE);
    DrawCircleV((Vector2){ leftC.x + 2.5f, leftC.y + 2.0f }, 1.8f, WHITE);
    DrawCircleV((Vector2){ rightC.x - 2.8f, rightC.y - 2.8f }, 1.6f, WHITE);
    DrawCircleV(rightC, 1.6f, WHITE);
    DrawCircleV((Vector2){ rightC.x + 2.8f, rightC.y + 2.8f }, 1.6f, WHITE);
}

// Gambler: pink ball with sly eyes, gold necklace, and gold ring
static void DrawGamblerDetails(const Ball* b)
{
    float x = b->pos.x, y = b->pos.y;

    // Sly narrowed eyes with slanted eyebrows
    DrawLineEx((Vector2){ x - 15, y - 10 }, (Vector2){ x - 4, y - 6 }, 3.0f, BLACK);
    DrawLineEx((Vector2){ x + 15, y - 10 }, (Vector2){ x + 4, y - 6 }, 3.0f, BLACK);
    DrawRectangle((int)x - 13, (int)y - 5, 9, 4, WHITE);
    DrawRectangle((int)x + 4,  (int)y - 5, 9, 4, WHITE);
    DrawCircle((int)x - 8, (int)y - 3, 2.0f, BLACK);
    DrawCircle((int)x + 9, (int)y - 3, 2.0f, BLACK);

    // Smirking grin
    DrawLineEx((Vector2){ x - 8, y + 4 }, (Vector2){ x + 4, y + 6 }, 2.2f, BLACK);
    DrawLineEx((Vector2){ x + 4, y + 6 }, (Vector2){ x + 11, y + 2 }, 2.2f, BLACK);

    // Gold necklace chain along lower chest
    for (int k = -4; k <= 4; k++) {
        float ang = (90.0f + k * 12.0f) * DEG2RAD;
        Vector2 linkPos = { x + cosf(ang) * 16.0f, y + sinf(ang) * 13.0f + 3.0f };
        DrawCircleV(linkPos, 2.6f, GOLD);
        DrawCircleLines((int)linkPos.x, (int)linkPos.y, 2.6f, BLACK);
    }
    // Gold pendant medallion in the middle of the necklace
    DrawCircleV((Vector2){ x, y + 19.0f }, 4.5f, GOLD);
    DrawCircleLines((int)x, (int)y + 19, 4.5f, BLACK);

    // Gold ring with diamond on the side of the ball
    Vector2 ringCenter = { x + b->radius - 3.0f, y - 2.0f };
    DrawRing(ringCenter, 3.8f, 6.5f, 0.0f, 360.0f, 18, GOLD);
    DrawCircleLines((int)ringCenter.x, (int)ringCenter.y, 6.5f, BLACK);
    DrawCircleV((Vector2){ ringCenter.x + 5.5f, ringCenter.y }, 2.8f, SKYBLUE);
}

// Psychic: pink mystic head-scarf with black folds (Tara style) and a scarred third eye
static void DrawPsychicDetails(const Ball* b)
{
    float x = b->pos.x, y = b->pos.y;
    float r = (float)b->radius;
    Color wrapPink = { 225, 65, 145, 255 };
    Color darkFold = { 145, 25, 90, 255 };

    // Pink scarf wrapping the head
    DrawCircleSector(b->pos, r - 1.5f, 195.0f, 345.0f, 24, wrapPink);
    DrawRectangleRounded((Rectangle){ x - r + 2.0f, y - 10.0f, (r - 2.0f) * 2.0f, 18.0f }, 0.4f, 8, wrapPink);

    // Black cloth fold lines across the scarf
    DrawLineEx((Vector2){ x - r + 4.0f, y - 10.0f }, (Vector2){ x + r - 4.0f, y - 4.0f }, 2.0f, BLACK);
    DrawLineEx((Vector2){ x - r + 3.0f, y + 2.0f },  (Vector2){ x + r - 5.0f, y + 8.0f }, 2.0f, BLACK);
    DrawLineEx((Vector2){ x - 12.0f, y - r + 6.0f }, (Vector2){ x + 14.0f, y - 10.0f }, 1.8f, darkFold);

    // Vertical scar mark cutting across the eye
    DrawLineEx((Vector2){ x + 2.0f, y - 16.0f }, (Vector2){ x - 2.0f, y + 10.0f }, 2.5f, BLACK);

    // Mystic eye peeking out from the scarf folds (black & white)
    Vector2 eL = { x - 13.0f, y - 2.0f };
    Vector2 eR = { x + 13.0f, y - 2.0f };
    Vector2 eT = { x, y - 9.5f };
    Vector2 eB = { x, y + 5.5f };
    DrawTri(eL, eB, eT, WHITE);
    DrawTri(eR, eT, eB, WHITE);
    DrawLineEx(eL, eT, 2.2f, BLACK);
    DrawLineEx(eT, eR, 2.2f, BLACK);
    DrawLineEx(eL, eB, 2.2f, BLACK);
    DrawLineEx(eB, eR, 2.2f, BLACK);
    DrawCircleV((Vector2){ x, y - 2.0f }, 4.5f, BLACK);
    DrawCircleV((Vector2){ x - 1.5f, y - 3.5f }, 1.6f, WHITE);
}

// Charge: multi-shade blue energy rings + lightning/power core
static void DrawChargeDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color deepBlue  = { 10, 50, 145, 255 };
    Color midBlue   = { 35, 135, 245, 255 };
    Color lightBlue = { 125, 215, 255, 255 };
    Color coreCyan  = { 215, 250, 255, 255 };

    DrawCircleV(c, r * 0.82f, deepBlue);
    DrawCircleV(c, r * 0.62f, midBlue);
    DrawCircleV(c, r * 0.40f, lightBlue);
    DrawCircleV(c, r * 0.20f, coreCyan);

    // Orbiting charge arcs proportional to current stacks
    int arcs = 3 + b->charge.stacks;
    float spin = (float)GetTime() * (3.5f + b->charge.stacks * 0.6f);
    for (int k = 0; k < arcs; k++) {
        float a0 = spin + k * (2.0f * PI / (float)arcs);
        Vector2 p0 = { c.x + cosf(a0) * (r * 0.48f), c.y + sinf(a0) * (r * 0.48f) };
        Vector2 p1 = { c.x + cosf(a0 + 0.35f) * (r * 0.78f), c.y + sinf(a0 + 0.35f) * (r * 0.78f) };
        DrawLineEx(p0, p1, 2.4f, coreCyan);
    }
}

// Unshackled: blue, purple, and black multi-shade body carrying 2 golden chains (Sylas style)
static void DrawUnshackledDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color darkShadow = { 22, 18, 34, 255 };
    Color royalPurp  = { 95, 40, 175, 255 };
    Color mysticBlue = { 35, 115, 220, 255 };

    // Multi-shade segments inside the ball
    DrawCircleSector(c, r - 1.5f, 135.0f, 315.0f, 20, darkShadow);
    DrawCircleSector(c, r * 0.74f, 0.0f, 180.0f, 20, royalPurp);
    DrawCircleSector(c, r * 0.74f, 180.0f, 360.0f, 20, mysticBlue);
    DrawCircleV(c, r * 0.35f, darkShadow);

    // Glowing arcane eyes
    DrawCircleV((Vector2){ c.x - 8.0f, c.y - 3.0f }, 3.2f, SKYBLUE);
    DrawCircleV((Vector2){ c.x + 8.0f, c.y - 3.0f }, 3.2f, SKYBLUE);

    // Two heavy golden chains with shackles hanging from left and right wrists
    float wave = sinf((float)GetTime() * 5.0f) * 3.0f;
    for (int side = -1; side <= 1; side += 2) {
        Vector2 shackle = { c.x + side * (r - 4.0f), c.y + 3.0f };
        DrawRing(shackle, 3.0f, 5.5f, 0.0f, 360.0f, 14, GOLD);
        DrawCircleLines((int)shackle.x, (int)shackle.y, 5.5f, BLACK);

        for (int link = 1; link <= 4; link++) {
            float lx = shackle.x + side * (link * 5.2f);
            float ly = shackle.y + link * 3.2f + sinf(wave + link) * 1.8f;
            Vector2 lp = { lx, ly };
            DrawCircleV(lp, 3.6f, GOLD);
            DrawCircleV(lp, 1.5f, darkShadow);
            DrawCircleLines((int)lp.x, (int)lp.y, 3.6f, BLACK);
        }
    }

    // Chain Lash passive stack orbs (up to 3 golden chain-link orbs orbiting below/around Shackless while passiveStacks > 0)
    if (b->unshackled.passiveStacks > 0) {
        for (int k = 0; k < b->unshackled.passiveStacks; k++) {
            float sx = c.x + ((float)k - 0.5f * (float)(b->unshackled.passiveStacks - 1)) * 11.0f;
            float sy = c.y + r - 6.0f;
            DrawCircleV((Vector2){ sx, sy }, 4.2f, GOLD);
            DrawCircleV((Vector2){ sx, sy }, 1.8f, SKYBLUE);
            DrawCircleLines((int)sx, (int)sy, 4.2f, BLACK);
        }
    }

    // Whipping golden chain lash animation when Shackless collides and consumes a stack
    if (b->unshackled.lashAnimTimer > 0.0f) {
        float p = 1.0f - ClampF(b->unshackled.lashAnimTimer / 0.25f, 0.0f, 1.0f);
        float ang = b->unshackled.lashAngle;
        Vector2 d = { cosf(ang), sinf(ang) };
        Vector2 n = { -d.y, d.x };
        float reach = (r + 26.0f) * sinf(p * PI);
        for (int link = 1; link <= 6; link++) {
            float frac = (float)link / 6.0f;
            float whipWave = sinf(frac * PI * 2.0f + p * 8.0f) * 6.0f;
            Vector2 lp = V2Add(c, V2Add(V2Mul(d, r * 0.6f + frac * reach), V2Mul(n, whipWave)));
            DrawCircleV(lp, 4.2f, GOLD);
            DrawCircleLines((int)lp.x, (int)lp.y, 4.2f, BLACK);
        }
    }
}

// Helper to rotate a local offset (lx, ly) around (0,0) by `tiltRad` and translate to `origin`
static inline Vector2 RotOffset(Vector2 origin, float lx, float ly, float cs, float sn)
{
    return (Vector2){
        origin.x + lx * cs - ly * sn,
        origin.y + lx * sn + ly * cs
    };
}

// Helper to draw a majestic Garen-style Demacian Justice broadsword (supports tilt angle & alpha for side swords)
static void DrawJusticeSwordEx(Vector2 tipPos, float scale, float tiltRad, float alpha)
{
    float cs = cosf(tiltRad), sn = sinf(tiltRad);
    Color bladeWhite = Fade((Color){ 248, 250, 255, 255 }, alpha);
    Color bladeGray  = Fade((Color){ 185, 195, 210, 255 }, alpha);
    Color hiltGold   = Fade((Color){ 255, 210, 45, 255 }, alpha);
    Color deepGold   = Fade((Color){ 205, 145, 15, 255 }, alpha);
    Color lineBlk    = Fade(BLACK, alpha);

    // Blade body (pointing down into tipPos along tilted axis)
    Vector2 tip  = tipPos;
    Vector2 bL   = RotOffset(tipPos, -9.0f * scale, -14.0f * scale, cs, sn);
    Vector2 bR   = RotOffset(tipPos,  9.0f * scale, -14.0f * scale, cs, sn);
    Vector2 topL = RotOffset(tipPos, -11.0f * scale, -62.0f * scale, cs, sn);
    Vector2 topR = RotOffset(tipPos,  11.0f * scale, -62.0f * scale, cs, sn);
    Vector2 midT = RotOffset(tipPos,  0.0f,          -62.0f * scale, cs, sn);

    DrawTri(tip, midT, bL, bladeWhite);
    DrawTri(bL, midT, topL, bladeWhite);
    DrawTri(tip, bR, midT, bladeGray);
    DrawTri(bR, topR, midT, bladeGray);
    DrawLineEx(RotOffset(tipPos, 0.0f, -10.0f * scale, cs, sn),
               RotOffset(tipPos, 0.0f, -60.0f * scale, cs, sn), 3.5f * scale, hiltGold);

    DrawLineEx(tip, bL, 2.0f, lineBlk);
    DrawLineEx(bL, topL, 2.0f, lineBlk);
    DrawLineEx(tip, bR, 2.0f, lineBlk);
    DrawLineEx(bR, topR, 2.0f, lineBlk);

    // Winged golden crossguard
    Vector2 gL = RotOffset(tipPos, -24.0f * scale, -66.0f * scale, cs, sn);
    Vector2 gR = RotOffset(tipPos,  24.0f * scale, -66.0f * scale, cs, sn);
    Vector2 gC = RotOffset(tipPos,   0.0f,         -61.0f * scale, cs, sn);
    DrawLineEx(gL, gC, 7.0f * scale, hiltGold);
    DrawLineEx(gR, gC, 7.0f * scale, hiltGold);
    DrawCircleV(gC, 5.5f * scale, deepGold);
    DrawCircleV(gC, 3.0f * scale, Fade(WHITE, alpha));

    // Grip & pommel
    Vector2 gripBase = RotOffset(tipPos, 0.0f, -64.0f * scale, cs, sn);
    Vector2 pommel   = RotOffset(tipPos, 0.0f, -82.0f * scale, cs, sn);
    DrawLineEx(gripBase, pommel, 5.0f * scale, deepGold);
    DrawCircleV(pommel, 5.5f * scale, hiltGold);
    DrawCircleLines((int)pommel.x, (int)pommel.y, 5.5f * scale, lineBlk);
}

// Draws the center Justice Sword (+20% visual size = 1.38x) flanked by 2 translucent side swords
// tilted outward and 10% smaller than the center sword
static void DrawJusticeTripleSwords(Vector2 centerTipPos)
{
    float mainScale = 1.15f * 1.20f;       // +20% visual size
    float sideScale = mainScale * 0.90f;   // 10% smaller than center sword
    Vector2 leftTip  = { centerTipPos.x - 28.0f, centerTipPos.y + 4.0f };
    Vector2 rightTip = { centerTipPos.x + 28.0f, centerTipPos.y + 4.0f };

    DrawJusticeSwordEx(leftTip,  sideScale, -16.0f * DEG2RAD, 0.46f);
    DrawJusticeSwordEx(rightTip, sideScale,  16.0f * DEG2RAD, 0.46f);
    DrawJusticeSwordEx(centerTipPos, mainScale, 0.0f, 1.0f);
}

// Bright golden ground cracks radiating from the Justice Sword impact point (replaces the old circle underneath)
static void DrawGoldenGroundCracks(Vector2 pos, float radius, float alpha)
{
    Color brightGold = Fade((Color){ 255, 230, 55, 255 }, alpha);
    Color coreWhite  = Fade((Color){ 255, 252, 210, 255 }, alpha);
    Color deepGold   = Fade((Color){ 215, 145, 10, 255 }, alpha * 0.85f);

    static const float rayAngles[8] = { -0.15f, 0.55f, 1.18f, 1.95f, 2.58f, 3.25f, 4.02f, 5.15f };
    static const float rayLens[8]   = { 1.05f, 0.82f, 0.95f, 0.78f, 1.08f, 0.88f, 0.76f, 0.92f };

    for (int k = 0; k < 8; k++) {
        float a = rayAngles[k];
        float len = radius * rayLens[k];
        Vector2 p1 = { pos.x + cosf(a + 0.14f) * (len * 0.42f), pos.y + sinf(a + 0.14f) * (len * 0.42f) * 0.65f };
        Vector2 p2 = { pos.x + cosf(a - 0.10f) * (len * 0.76f), pos.y + sinf(a - 0.10f) * (len * 0.76f) * 0.65f };
        Vector2 p3 = { pos.x + cosf(a + 0.06f) * len,           pos.y + sinf(a + 0.06f) * len * 0.65f };

        DrawLineEx(pos, p1, 4.8f, deepGold);
        DrawLineEx(p1,  p2, 3.6f, brightGold);
        DrawLineEx(p2,  p3, 2.2f, brightGold);
        DrawLineEx(pos, p1, 2.2f, coreWhite);

        // Secondary fork branch on alternating cracks
        if ((k & 1) == 0) {
            float branchA = a + 0.48f;
            Vector2 bTip = { p1.x + cosf(branchA) * (len * 0.38f), p1.y + sinf(branchA) * (len * 0.38f) * 0.65f };
            DrawLineEx(p1, bTip, 2.2f, brightGold);
        }
    }
}

// Kizaru-style translucent yellow 4-pointed light star that spins and grows right before Judgement finishes charging
static void DrawKizaruFourPointStar(Vector2 c, float maxLen, float rotAngle, float alpha)
{
    Color outerGlow = Fade((Color){ 255, 215, 35, 255 }, alpha * 0.38f);
    Color starGold  = Fade((Color){ 255, 240, 75, 255 }, alpha * 0.68f);
    Color coreLight = Fade((Color){ 255, 255, 220, 255 }, alpha * 0.82f);

    float waist = maxLen * 0.16f;
    // 4 main long points at 90-degree intervals
    for (int i = 0; i < 4; i++) {
        float a = rotAngle + i * (PI * 0.5f);
        Vector2 d = { cosf(a), sinf(a) };
        Vector2 n = { -d.y, d.x };
        Vector2 tip  = V2Add(c, V2Mul(d, maxLen));
        Vector2 wL   = V2Add(c, V2Mul(n, -waist));
        Vector2 wR   = V2Add(c, V2Mul(n,  waist));
        DrawTri(c, wL, tip, outerGlow);
        DrawTri(c, tip, wR, outerGlow);

        Vector2 inTip = V2Add(c, V2Mul(d, maxLen * 0.82f));
        Vector2 inWL  = V2Add(c, V2Mul(n, -waist * 0.55f));
        Vector2 inWR  = V2Add(c, V2Mul(n,  waist * 0.55f));
        DrawTri(c, inWL, inTip, starGold);
        DrawTri(c, inTip, inWR, starGold);
    }
    // 4 shorter diagonal glints at 45-degree offsets
    for (int i = 0; i < 4; i++) {
        float a = rotAngle + (PI * 0.25f) + i * (PI * 0.5f);
        Vector2 d = { cosf(a), sinf(a) };
        Vector2 n = { -d.y, d.x };
        Vector2 tip = V2Add(c, V2Mul(d, maxLen * 0.42f));
        Vector2 wL  = V2Add(c, V2Mul(n, -waist * 0.38f));
        Vector2 wR  = V2Add(c, V2Mul(n,  waist * 0.38f));
        DrawTri(c, wL, tip, starGold);
        DrawTri(c, tip, wR, starGold);
    }
    DrawCircleV(c, waist * 0.85f, coreLight);
}

// Judgement: gold, white, and gray multi-shade justice ball with angel wings and a golden halo above
static void DrawJudgementDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    float s = r / (float)BASE_RADIUS;
    Color pureWhite  = { 250, 250, 255, 255 };
    Color silverGray = { 175, 182, 195, 255 };
    Color brightGold = { 255, 215, 45, 255 };
    Color warmGold   = { 215, 155, 20, 255 };

    // Dynamic fluttering angel wings on left & right sides (3 distinct feather triangles per wing with phase-shifted flapping)
    float flapSpeed = (b->judgement.state > 0) ? 13.5f : 7.2f;
    float flapAmp   = (b->judgement.state > 0) ? 0.34f : 0.24f;
    float timeNow   = (float)GetTime() * flapSpeed;

    static const float baseAngles[3] = { -0.52f, -0.06f, 0.38f };   // upper, middle, lower feather angles (radians)
    static const float featherLen[3] = { 25.0f,  21.0f,  16.5f };   // length of each triangular feather
    static const float halfWidth[3]  = {  5.2f,   4.6f,   4.0f };   // base half-thickness of each triangle

    for (int side = -1; side <= 1; side += 2) {
        for (int f = 2; f >= 0; f--) {
            // Phase-lagged wave so upper, middle, and lower feathers flutter in a natural ripple
            float wave = sinf(timeNow - f * 0.65f) * flapAmp;
            float ang  = baseAngles[f] - wave; // negative y is up

            Vector2 rootCenter = {
                c.x + side * (r - 3.5f * s),
                c.y - 3.0f * s + f * 4.2f * s - sinf(timeNow) * 1.8f * s
            };
            Vector2 dir  = { side * cosf(ang), sinf(ang) };
            Vector2 perp = { -dir.y, dir.x };

            float len = featherLen[f] * s * (1.0f + 0.06f * cosf(timeNow - f * 0.5f));
            float hw  = halfWidth[f] * s;
            Vector2 pRootTop = V2Add(rootCenter, V2Mul(perp, -hw * (float)side));
            Vector2 pRootBot = V2Add(rootCenter, V2Mul(perp,  hw * (float)side));
            Vector2 pTip     = V2Add(rootCenter, V2Mul(dir, len));

            Color featherFill = (f == 0) ? pureWhite : ((f == 1) ? (Color){ 228, 234, 245, 255 } : silverGray);
            DrawTri(pRootTop, pTip, pRootBot, featherFill);

            // Golden leading edge on the top feather + crisp outline on all 3 feather triangles
            DrawLineEx(pRootTop, pTip, (f == 0) ? 2.2f : 1.5f, (f == 0) ? brightGold : BLACK);
            DrawLineEx(pTip, pRootBot, 1.5f, BLACK);
            DrawLineEx(pRootBot, pRootTop, 1.4f, warmGold);
        }
    }

    // Multi-shade gold, white, and gray concentric justice emblem inside the ball
    DrawCircleSector(c, r - 1.5f, 0.0f, 180.0f, 20, silverGray);
    DrawCircleSector(c, r - 1.5f, 180.0f, 360.0f, 20, pureWhite);
    DrawCircleV(c, r * 0.68f, warmGold);
    DrawCircleV(c, r * 0.42f, pureWhite);
    // Golden cross of justice in the center
    DrawLineEx((Vector2){ c.x, c.y - r * 0.58f }, (Vector2){ c.x, c.y + r * 0.58f }, 4.5f * s, brightGold);
    DrawLineEx((Vector2){ c.x - r * 0.48f, c.y }, (Vector2){ c.x + r * 0.48f, c.y }, 4.5f * s, brightGold);
    DrawCircleV(c, 4.0f * s, brightGold);

    // Golden angel halo floating above Judgement (gently bobbing with the wingbeat)
    float haloY = c.y - r - 10.0f * s - sinf(timeNow) * 1.5f;
    for (int k = 0; k < 20; k++) {
        float a0 = k * (2.0f * PI / 20.0f);
        float a1 = (k + 1) * (2.0f * PI / 20.0f);
        Vector2 p0 = { c.x + cosf(a0) * 16.0f * s, haloY + sinf(a0) * 4.5f * s };
        Vector2 p1 = { c.x + cosf(a1) * 16.0f * s, haloY + sinf(a1) * 4.5f * s };
        DrawLineEx(p0, p1, 3.2f, brightGold);
    }

    // Outer gold & white charging aura + Kizaru spinning 4-pointed yellow star during the 0.75s charge (state == 1)
    if (b->judgement.state == 1) {
        float prog = ClampF(b->judgement.timer / JUDGEMENT_CHARGE_T, 0.0f, 1.0f);
        float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 18.0f);

        // Multi-layered outer gold and white aura around Judgement while charging
        DrawCircleV(c, r + 18.0f + pulse * 5.0f, Fade(GOLD, 0.22f + 0.20f * prog));
        DrawCircleV(c, r + 11.0f + pulse * 3.0f, Fade(WHITE, 0.30f + 0.25f * prog));
        DrawRing(c, r + 4.0f, r + 8.5f, 0.0f, 360.0f, 36, Fade(GOLD, 0.88f));
        DrawRing(c, r + 9.0f, r + 12.5f, 0.0f, 360.0f, 36, Fade(WHITE, 0.82f));

        // Alternating gold & white radiant aura spikes outside the ball
        int auraSpikes = 12;
        for (int k = 0; k < auraSpikes; k++) {
            float a = (float)GetTime() * 3.5f + k * (2.0f * PI / (float)auraSpikes);
            Vector2 d = { cosf(a), sinf(a) };
            Vector2 n = { -d.y, d.x };
            float spkLen = 11.0f + 8.0f * prog + ((k & 1) ? 4.0f * pulse : 4.0f * (1.0f - pulse));
            Vector2 base = V2Add(c, V2Mul(d, r + 2.0f));
            Vector2 bL   = V2Add(base, V2Mul(n, -4.5f));
            Vector2 bR   = V2Add(base, V2Mul(n,  4.5f));
            Vector2 tip  = V2Add(base, V2Mul(d, spkLen));
            Color spkCol = (k & 1) ? Fade(WHITE, 0.85f) : Fade(GOLD, 0.88f);
            DrawTri(bL, bR, tip, spkCol);
        }

        float gatherR = r + 28.0f * (1.0f - prog);
        DrawRing(c, gatherR - 2.5f, gatherR + 2.5f, 0.0f, 360.0f, 32, Fade(GOLD, 0.85f));
        for (int k = 0; k < 8; k++) {
            float a = (float)GetTime() * 6.0f + k * (PI * 0.25f);
            Vector2 outerP = { c.x + cosf(a) * gatherR, c.y + sinf(a) * gatherR };
            Vector2 innerP = { c.x + cosf(a) * r,       c.y + sinf(a) * r };
            DrawLineEx(outerP, innerP, 2.2f, WHITE);
        }

        // Right before charging completes, a Kizaru-style 4-pointed translucent yellow star spins & grows from small to large
        if (prog >= 0.28f) {
            float starT = ClampF((prog - 0.28f) / 0.72f, 0.0f, 1.0f);
            float starLen = (r * 0.25f) + starT * (r * 2.15f);
            float starRot = starT * (PI * 1.65f);
            DrawKizaruFourPointStar(c, starLen, starRot, 0.35f + 0.65f * starT);
        }
    } else if (b->judgement.state == 2 || b->judgement.immuneTimer > 0.0f) {
        // Golden & white divine immunity shield while ramming and during the 0.50s post-ram immunity window
        float a = (b->judgement.state == 2) ? 1.0f : ClampF(b->judgement.immuneTimer / JUDGEMENT_POST_IMMUNE_T, 0.0f, 1.0f);
        DrawCircleV(c, r + 11.0f, Fade(GOLD, 0.38f * a));
        DrawRing(c, r + 2.0f, r + 6.0f, 0.0f, 360.0f, 32, Fade(WHITE, 0.92f * a));
        DrawRing(c, r + 6.0f, r + 9.5f, 0.0f, 360.0f, 32, Fade(GOLD, 0.85f * a));
    }
}

// Train: black, dark blue, wood brown, and iron gray steam-locomotive themed ball
static void DrawTrainBallDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color darkBlue = { 24, 48, 82, 255 };
    Color woodCol  = { 135, 82, 38, 255 };
    Color ironCol  = { 155, 162, 172, 255 };
    Color coalBlk  = { 22, 24, 28, 255 };

    // Wooden crosstie bands & dark blue boiler casing
    DrawCircleV(c, r - 1.5f, darkBlue);
    DrawRectangle((int)(c.x - r * 0.75f), (int)(c.y - 5.0f), (int)(r * 1.5f), 10, woodCol);
    DrawRectangleLines((int)(c.x - r * 0.75f), (int)(c.y - 5.0f), (int)(r * 1.5f), 10, BLACK);

    // Iron rim wheel & smokestack core
    DrawRing(c, r * 0.55f, r * 0.72f, 0.0f, 360.0f, 24, ironCol);
    for (int k = 0; k < 6; k++) {
        float a = (float)GetTime() * 4.0f + k * (PI / 3.0f);
        DrawLineEx(c, (Vector2){ c.x + cosf(a) * (r * 0.65f), c.y + sinf(a) * (r * 0.65f) }, 2.2f, ironCol);
    }
    DrawCircleV(c, 8.5f, coalBlk);
    DrawCircleLines((int)c.x, (int)c.y, 8.5f, ironCol);
    DrawCircleV(c, 3.5f, (Color){ 255, 140, 40, 255 });
}

// Draws the lead black locomotive engine (Car 0) along Train's tracks during Phase 2
static void DrawLocomotiveEngine(Vector2 pos, float angle)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };
    Color ironCol = { 145, 152, 162, 255 };
    Color woodCab = { 125, 72, 32, 255 };
    Color darkBlu = { 28, 52, 88, 255 };

    // Cowcatcher (iron plow at front)
    Vector2 front = V2Add(pos, V2Mul(d, 22.0f));
    Vector2 nose  = V2Add(pos, V2Mul(d, 33.0f));
    DrawTri(nose, V2Add(front, V2Mul(n, -20.0f)), V2Add(front, V2Mul(n, 20.0f)), ironCol);
    DrawLineEx(nose, V2Add(front, V2Mul(n, -20.0f)), 2.0f, BLACK);
    DrawLineEx(nose, V2Add(front, V2Mul(n,  20.0f)), 2.0f, BLACK);

    // Main black locomotive body (length = 44px, width = 42px)
    Rectangle bodyRect = { pos.x, pos.y, 44.0f, 42.0f };
    DrawRectanglePro(bodyRect, (Vector2){ 22.0f, 21.0f }, angle * RAD2DEG, BLACK);

    // Dark-blue boiler stripe & wooden cab at the back
    Vector2 cabCenter = V2Add(pos, V2Mul(d, -11.0f));
    Rectangle cabRect = { cabCenter.x, cabCenter.y, 18.0f, 38.0f };
    DrawRectanglePro(cabRect, (Vector2){ 9.0f, 19.0f }, angle * RAD2DEG, woodCab);

    Rectangle boilerRect = { pos.x + d.x * 5.0f, pos.y + d.y * 5.0f, 24.0f, 28.0f };
    DrawRectanglePro(boilerRect, (Vector2){ 12.0f, 14.0f }, angle * RAD2DEG, darkBlu);

    // Smokestack puffing at front
    Vector2 stackPos = V2Add(pos, V2Mul(d, 13.0f));
    DrawCircleV(stackPos, 7.0f, ironCol);
    DrawCircleV(stackPos, 4.2f, BLACK);
    DrawCircleV(V2Add(nose, V2Mul(d, -4.0f)), 3.8f, GOLD); // headlight
}

// Draws a following train carriage (Cars 1..4) pulled behind the locomotive
static void DrawTrainCarriage(Vector2 pos, float angle, int carIndex)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Color ironCol = { 145, 152, 162, 255 };
    Color woodCar = (carIndex & 1) ? (Color){ 132, 76, 34, 255 } : (Color){ 30, 54, 90, 255 };
    Color coalBlk = { 24, 26, 30, 255 };

    // Outer iron-trimmed carriage frame (length = 40px, width = 38px)
    Rectangle outerRect = { pos.x, pos.y, 40.0f, 38.0f };
    DrawRectanglePro(outerRect, (Vector2){ 20.0f, 19.0f }, angle * RAD2DEG, BLACK);

    Rectangle innerRect = { pos.x, pos.y, 36.0f, 34.0f };
    DrawRectanglePro(innerRect, (Vector2){ 18.0f, 17.0f }, angle * RAD2DEG, woodCar);

    // Center cargo/coal bay & iron cross-brace
    Rectangle bayRect = { pos.x, pos.y, 24.0f, 22.0f };
    DrawRectanglePro(bayRect, (Vector2){ 12.0f, 11.0f }, angle * RAD2DEG, coalBlk);

    Vector2 frontCoupler = V2Add(pos, V2Mul(d, 20.0f));
    Vector2 rearCoupler  = V2Add(pos, V2Mul(d, -20.0f));
    DrawCircleV(frontCoupler, 3.2f, ironCol);
    DrawCircleV(rearCoupler,  3.2f, ironCol);
}

// Draws a short iron chain connecting the back of car `c` to the front of car `c+1`
static void DrawTrainCouplerChain(Vector2 frontCarPos, float frontAngle, Vector2 backCarPos, float backAngle)
{
    Vector2 dFront = { cosf(frontAngle), sinf(frontAngle) };
    Vector2 dBack  = { cosf(backAngle),  sinf(backAngle) };
    Vector2 pRear  = V2Add(frontCarPos, V2Mul(dFront, -21.0f));
    Vector2 pFront = V2Add(backCarPos,  V2Mul(dBack,   20.0f));

    Color chainIron = { 175, 182, 192, 255 };
    DrawLineEx(pRear, pFront, 4.5f, BLACK);
    DrawLineEx(pRear, pFront, 2.4f, chainIron);
    for (int k = 1; k <= 2; k++) {
        float t = (float)k / 3.0f;
        Vector2 linkP = V2Add(pRear, V2Mul(V2Sub(pFront, pRear), t));
        DrawCircleV(linkP, 3.0f, chainIron);
        DrawCircleLines((int)linkP.x, (int)linkP.y, 3.0f, BLACK);
    }
}

// Spider: red ball with a white cobweb emblem in the center
static void DrawSpiderDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;

    // 8 radial white silk spokes
    for (int k = 0; k < 8; k++) {
        float a = k * (PI * 0.25f);
        Vector2 tip = { c.x + cosf(a) * (r - 2.5f), c.y + sinf(a) * (r - 2.5f) };
        DrawLineEx(c, tip, 1.8f, WHITE);
    }
    // 3 concentric octagonal web rings
    for (int ring = 1; ring <= 3; ring++) {
        float rr = (r - 4.0f) * ((float)ring / 3.0f);
        for (int k = 0; k < 8; k++) {
            float a0 = k * (PI * 0.25f);
            float a1 = (k + 1) * (PI * 0.25f);
            Vector2 p0 = { c.x + cosf(a0) * rr, c.y + sinf(a0) * rr };
            Vector2 p1 = { c.x + cosf(a1) * rr, c.y + sinf(a1) * rr };
            DrawLineEx(p0, p1, 1.6f, WHITE);
        }
    }
    DrawCircleV(c, 2.8f, WHITE);
}

// Range: yellow ball with concentric targeting rings in the center
static void DrawRangeDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color darkGold = { 175, 115, 10, 255 };

    DrawRing(c, r * 0.58f, r * 0.68f, 0.0f, 360.0f, 28, darkGold);
    DrawRing(c, r * 0.28f, r * 0.38f, 0.0f, 360.0f, 24, WHITE);
    DrawCircleV(c, 4.0f, darkGold);
    for (int k = 0; k < 4; k++) {
        float a = k * (PI * 0.5f);
        Vector2 p0 = { c.x + cosf(a) * (r * 0.42f), c.y + sinf(a) * (r * 0.42f) };
        Vector2 p1 = { c.x + cosf(a) * (r * 0.82f), c.y + sinf(a) * (r * 0.82f) };
        DrawLineEx(p0, p1, 2.2f, darkGold);
    }
}

// Helper to draw a fluttering truncated-triangle royal cape behind a ball (Knight or King):
// - Rendered BEFORE the ball circle so the center is naturally hidden behind the ball and flares out on the left, right, and bottom
// - Uses dark fold colors, black, and shades of gray with horizontal wave motion of the folds for a natural 3D fluttering look
static void DrawFlutteringRoyalCape(Vector2 c, float r, bool isKnight)
{
    float t = (float)GetTime() * 6.5f;
    float topHalfW = r * 0.86f;
    float topY     = c.y - r * 0.25f;
    float botHalfW = r * 1.38f;
    float botY     = c.y + r + 11.0f;

    // 6 vertical panels forming the truncated triangle cape with horizontal wave motion on the fold seams
    int panels = 6;
    Vector2 topPts[7];
    Vector2 botPts[7];
    for (int i = 0; i <= panels; i++) {
        float u = (float)i / (float)panels; // 0..1 from left to right
        float xNorm = (u - 0.5f) * 2.0f;    // -1..+1
        float foldWave = sinf(t + i * 1.15f) * 3.4f;
        float hemBob   = cosf(t * 1.2f + i * 0.9f) * 1.8f;

        topPts[i] = (Vector2){ c.x + xNorm * topHalfW + foldWave * 0.35f, topY + fabsf(xNorm) * 2.0f };
        botPts[i] = (Vector2){ c.x + xNorm * botHalfW + foldWave,         botY - (1.0f - xNorm * xNorm) * 2.5f + hemBob };
    }

    for (int i = 0; i < panels; i++) {
        Color baseCol;
        Color foldDark;
        if (isKnight) {
            // Left half red, right half blue, with darker/grayish-shaded alternating fold valleys
            bool leftSide = (i < panels / 2);
            if (leftSide) {
                baseCol  = (i & 1) ? (Color){ 155, 22, 28, 255 } : (Color){ 210, 36, 42, 255 };
                foldDark = (Color){ 78, 18, 24, 255 };
            } else {
                baseCol  = (i & 1) ? (Color){ 24, 65, 155, 255 } : (Color){ 38, 98, 215, 255 };
                foldDark = (Color){ 18, 36, 82, 255 };
            }
        } else {
            // King: royal crimson red cape with deep dark-red / charcoal-gray shaded folds
            baseCol  = (i & 1) ? (Color){ 152, 18, 26, 255 } : (Color){ 212, 32, 40, 255 };
            foldDark = (Color){ 82, 14, 20, 255 };
        }

        DrawTri(topPts[i], botPts[i], botPts[i + 1], baseCol);
        DrawTri(topPts[i], botPts[i + 1], topPts[i + 1], baseCol);

        // 3D shaded pleat wedge along each fold seam using darker tones + gray/black crease lines
        Vector2 midBot = { 0.5f * (botPts[i].x + botPts[i + 1].x), 0.5f * (botPts[i].y + botPts[i + 1].y) };
        DrawTri(topPts[i], botPts[i], midBot, Fade(foldDark, 0.72f));
    }

    // Horizontal gold stripe across the middle of Knight's cape ("một nét màu vàng ngang ở giữa")
    // or golden bottom border trim on King's cape ("màu đỏ và viền vàng")
    Color trimGold = { 255, 212, 42, 255 };
    if (isKnight) {
        for (int i = 0; i < panels; i++) {
            Vector2 m0 = { 0.45f * topPts[i].x + 0.55f * botPts[i].x,         0.45f * topPts[i].y + 0.55f * botPts[i].y };
            Vector2 m1 = { 0.45f * topPts[i + 1].x + 0.55f * botPts[i + 1].x, 0.45f * topPts[i + 1].y + 0.55f * botPts[i + 1].y };
            DrawLineEx(m0, m1, 3.6f, trimGold);
            DrawLineEx(m0, m1, 1.2f, Fade(BLACK, 0.45f));
        }
    } else {
        for (int i = 0; i < panels; i++) {
            DrawLineEx(botPts[i], botPts[i + 1], 4.2f, trimGold);
        }
    }

    // Draw moving vertical fold lines in black and shades of gray for 3D depth
    Color darkGrayFold = { 58, 62, 70, 255 };
    Color midGrayFold  = { 108, 114, 124, 255 };
    for (int i = 1; i < panels; i++) {
        Color creaseCol = (i & 1) ? BLACK : darkGrayFold;
        DrawLineEx(topPts[i], botPts[i], 2.2f, creaseCol);
        Vector2 hiliteTop = { topPts[i].x + 1.5f, topPts[i].y };
        Vector2 hiliteBot = { botPts[i].x + 1.5f, botPts[i].y };
        DrawLineEx(hiliteTop, hiliteBot, 1.2f, Fade(midGrayFold, 0.65f));
    }

    // Outer cape silhouette border
    DrawLineEx(topPts[0], botPts[0], 2.2f, BLACK);
    DrawLineEx(topPts[panels], botPts[panels], 2.2f, BLACK);
    for (int i = 0; i < panels; i++) {
        DrawLineEx(botPts[i], botPts[i + 1], 1.8f, BLACK);
    }
}

// Knight: truncated-triangle fluttering cape behind the ball, recessed T-visor helmet with bright rim bevels in the center,
// tall curved heater shield (red/blue with gold cross) on the left, and a large mighty broadsword with hilt/pommel on the right
static void DrawKnightDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color armorWhite = { 238, 242, 248, 255 };
    Color armorGray  = { 142, 152, 168, 255 };
    Color darkSteel  = { 88, 96, 110, 255 };
    Color voidBlack  = { 12, 14, 18, 255 };
    Color capeRed    = { 205, 32, 38, 255 };
    Color capeBlue   = { 32, 90, 205, 255 };
    Color trimGold   = { 255, 210, 40, 255 };
    Color hiltBrown  = { 115, 64, 26, 255 };

    // 1. Center of Knight ball: Great Helm faceplate with deep recessed black T-opening surrounded by brighter beveled edges
    DrawCircleV(c, r * 0.82f, armorGray);
    DrawCircleSector(c, r * 0.82f, 180.0f, 360.0f, 20, armorWhite);
    DrawRing(c, r * 0.78f, r * 0.84f, 0.0f, 360.0f, 24, darkSteel);

    // Outer bright metallic/gold bevel frame around the T-visor recess to create 3D depth looking into the dark hollow inside
    DrawRectangleRounded((Rectangle){ c.x - 16.0f, c.y - 10.0f, 32.0f, 9.0f }, 0.25f, 4, trimGold);
    DrawRectangleRounded((Rectangle){ c.x - 6.0f,  c.y - 10.0f, 12.0f, 25.0f }, 0.25f, 4, trimGold);
    // Inner bright white-silver highlight rim right around the recessed T-slit
    DrawRectangle((int)(c.x - 14.0f), (int)(c.y - 8.0f), 28, 6, armorWhite);
    DrawRectangle((int)(c.x - 4.5f),  (int)(c.y - 8.0f), 9, 22, armorWhite);
    // Deep recessed black interior of the T-visor
    DrawRectangle((int)(c.x - 12.0f), (int)(c.y - 6.5f), 24, 3, voidBlack);
    DrawRectangle((int)(c.x - 2.5f),  (int)(c.y - 6.5f), 5, 19, voidBlack);

    // 2. Larger, taller kite/heater shield on the left side (vertical height > horizontal width, curved bottom point,
    // red & blue halves, gold cross in the center)
    Vector2 shC = { c.x - r - 2.0f, c.y + 3.0f };
    float shHW = 10.5f;  // total width = 21px
    float shTop = shC.y - 15.0f;
    float shMid = shC.y + 2.0f;
    float shBot = shC.y + 18.0f; // total height = 33px (taller than wide)

    // Outer steel/gold shield backing
    Vector2 sTL = { shC.x - shHW, shTop };
    Vector2 sTR = { shC.x + shHW, shTop };
    Vector2 sML = { shC.x - shHW, shMid };
    Vector2 sMR = { shC.x + shHW, shMid };
    Vector2 sBL = { shC.x - shHW * 0.55f, shMid + 9.0f };
    Vector2 sBR = { shC.x + shHW * 0.55f, shMid + 9.0f };
    Vector2 sTip= { shC.x, shBot };

    // Left half (Red) and Right half (Blue)
    DrawRectangle((int)sTL.x, (int)shTop, (int)shHW, (int)(shMid - shTop), capeRed);
    DrawRectangle((int)shC.x, (int)shTop, (int)shHW, (int)(shMid - shTop), capeBlue);
    DrawTri(sML, sBL, (Vector2){ shC.x, shMid + 9.0f }, capeRed);
    DrawTri(sML, (Vector2){ shC.x, shMid + 9.0f }, (Vector2){ shC.x, shMid }, capeRed);
    DrawTri(sBL, sTip, (Vector2){ shC.x, shMid + 9.0f }, capeRed);
    DrawTri((Vector2){ shC.x, shMid }, (Vector2){ shC.x, shMid + 9.0f }, sMR, capeBlue);
    DrawTri((Vector2){ shC.x, shMid + 9.0f }, sBR, sMR, capeBlue);
    DrawTri((Vector2){ shC.x, shMid + 9.0f }, sTip, sBR, capeBlue);

    // Bold gold cross in the center of the shield
    DrawLineEx((Vector2){ shC.x, shTop + 2.0f }, (Vector2){ shC.x, shBot - 2.5f }, 3.8f, trimGold);
    DrawLineEx((Vector2){ shC.x - shHW + 2.0f, shC.y - 3.0f }, (Vector2){ shC.x + shHW - 2.0f, shC.y - 3.0f }, 3.8f, trimGold);

    // Shield metallic bevel border
    DrawLineEx(sTL, sTR, 2.8f, armorWhite);
    DrawLineEx(sTL, sML, 2.6f, armorWhite);
    DrawLineEx(sML, sBL, 2.6f, armorWhite);
    DrawLineEx(sBL, sTip, 2.6f, armorWhite);
    DrawLineEx(sTR, sMR, 2.6f, armorGray);
    DrawLineEx(sMR, sBR, 2.6f, armorGray);
    DrawLineEx(sBR, sTip, 2.6f, armorGray);
    DrawLineEx(sTL, sTR, 1.2f, BLACK);
    DrawLineEx(sTL, sML, 1.2f, BLACK);
    DrawLineEx(sML, sBL, 1.2f, BLACK);
    DrawLineEx(sBL, sTip, 1.2f, BLACK);
    DrawLineEx(sTR, sMR, 1.2f, BLACK);
    DrawLineEx(sMR, sBR, 1.2f, BLACK);
    DrawLineEx(sBR, sTip, 1.2f, BLACK);

    // 3. Larger, more imposing medieval broadsword with full handle/grip, crossguard, and pommel on the right side
    float swordAng = -0.52f;
    if (b->knight.windingUp) {
        swordAng = b->knight.slashAngle - 0.55f;
    } else if (b->knight.fxTimer > 0.0f) {
        float p = 1.0f - ClampF(b->knight.fxTimer / 0.22f, 0.0f, 1.0f);
        swordAng = b->knight.fxAngle - 0.35f + p * 0.70f;
    }
    Vector2 sd = { cosf(swordAng), sinf(swordAng) };
    Vector2 sn = { -sd.y, sd.x };
    Vector2 guardPos = { c.x + r + 1.0f, c.y + 1.0f };
    Vector2 pommelPos = V2Add(guardPos, V2Mul(sd, -11.0f));
    Vector2 bladeBase = V2Add(guardPos, V2Mul(sd, 2.0f));
    Vector2 bladeNeck = V2Add(guardPos, V2Mul(sd, 26.0f));
    Vector2 bladeTip  = V2Add(guardPos, V2Mul(sd, 35.0f));

    // Leather-wrapped sword grip & golden pommel
    DrawLineEx(pommelPos, guardPos, 6.2f, BLACK);
    DrawLineEx(pommelPos, guardPos, 4.2f, hiltBrown);
    DrawCircleV(pommelPos, 4.2f, trimGold);
    DrawCircleLines((int)pommelPos.x, (int)pommelPos.y, 4.2f, BLACK);

    // Wide two-tone steel broadsword blade with central fuller ridge & diamond point
    float bw = 5.4f;
    Vector2 bL0 = V2Add(bladeBase, V2Mul(sn, -bw));
    Vector2 bR0 = V2Add(bladeBase, V2Mul(sn,  bw));
    Vector2 bL1 = V2Add(bladeNeck, V2Mul(sn, -bw * 0.82f));
    Vector2 bR1 = V2Add(bladeNeck, V2Mul(sn,  bw * 0.82f));

    DrawTri(bL0, bL1, bladeNeck, armorWhite);
    DrawTri(bL0, bladeNeck, bladeBase, armorWhite);
    DrawTri(bladeBase, bladeNeck, bR1, armorGray);
    DrawTri(bladeBase, bR1, bR0, armorGray);
    DrawTri(bL1, bladeTip, bladeNeck, armorWhite);
    DrawTri(bladeNeck, bladeTip, bR1, armorGray);

    // Central fuller groove & crisp black blade outline
    DrawLineEx(V2Add(bladeBase, V2Mul(sd, 3.0f)), V2Add(bladeNeck, V2Mul(sd, -2.0f)), 2.0f, darkSteel);
    DrawLineEx(bL0, bL1, 1.5f, BLACK);
    DrawLineEx(bL1, bladeTip, 1.5f, BLACK);
    DrawLineEx(bR0, bR1, 1.5f, BLACK);
    DrawLineEx(bR1, bladeTip, 1.5f, BLACK);

    // Prominent golden cruciform crossguard with flared quillons
    Vector2 gL = V2Add(guardPos, V2Mul(sn, -11.0f));
    Vector2 gR = V2Add(guardPos, V2Mul(sn,  11.0f));
    DrawLineEx(gL, gR, 6.0f, BLACK);
    DrawLineEx(gL, gR, 4.0f, trimGold);
    DrawCircleV(gL, 2.6f, trimGold);
    DrawCircleV(gR, 2.6f, trimGold);
}

// Cavalry: knight in shining helmet with red feather plume riding a warhorse with layered neck armor plates
// and holding a long conical jousting lance built from stacked triangles (round base at handle, tapering to a sharp tip)
static void DrawCavalryDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color horseBrown = { 92, 46, 20, 255 };
    Color darkBrown  = { 54, 26, 10, 255 };
    Color ironLight  = { 215, 224, 236, 255 };
    Color ironMid    = { 162, 174, 192, 255 };
    Color darkIron   = { 88, 96, 112, 255 };
    Color plumeRed   = { 225, 32, 38, 255 };
    Color plumeDark  = { 155, 18, 24, 255 };

    // Golden speed lines / aura around Cavalry during Charge state
    if (b->cavalry.state == 1) {
        float chargeRatio = ClampF((b->cavalry.speedMult - 1.0f) / (CAVALRY_MAX_MULT - 1.0f), 0.0f, 1.0f);
        DrawCircleV(c, r + 5.0f + chargeRatio * 5.0f, Fade(GOLD, 0.22f + 0.22f * chargeRatio));
    }

    // Iron barding armor ring around outer rim
    DrawRing(c, r * 0.74f, r - 1.5f, 0.0f, 360.0f, 24, darkIron);

    // 1. Warhorse neck & sleeker forward-leaning head with thin black outline
    Vector2 nBL = { c.x - 7.5f, c.y + 15.0f };
    Vector2 nBR = { c.x + 7.5f, c.y + 15.0f };
    Vector2 nTop = { c.x + 3.5f, c.y - 5.5f };
    DrawTri(nBL, nBR, nTop, horseBrown);
    DrawLineEx(nBL, nTop, 1.4f, BLACK);
    DrawLineEx(nBR, nTop, 1.4f, BLACK);

    // Horse head angled more horizontally and tilted slightly forward toward the lance
    Vector2 sTop  = { c.x + 2.0f, c.y - 5.8f };
    Vector2 sNose = { c.x + 19.5f, c.y - 0.5f };
    Vector2 sJaw  = { c.x + 5.5f, c.y + 5.2f };
    DrawTri(sTop, sNose, sJaw, horseBrown);
    DrawLineEx(sTop, sNose, 1.4f, BLACK);
    DrawLineEx(sNose, sJaw, 1.4f, BLACK);
    DrawLineEx(sJaw, sTop, 1.2f, darkBrown);

    // Horse ear
    Vector2 earL = { c.x + 1.5f, c.y - 5.5f };
    Vector2 earR = { c.x + 5.5f, c.y - 4.8f };
    Vector2 earT = { c.x + 3.2f, c.y - 11.2f };
    DrawTri(earL, earR, earT, darkBrown);
    DrawLineEx(earL, earT, 1.2f, BLACK);
    DrawLineEx(earR, earT, 1.2f, BLACK);

    // Overlapping segmented armor plates along the horse's neck
    for (int p = 0; p < 4; p++) {
        float py = c.y - 3.0f + p * 4.4f;
        float px = c.x - 1.5f - p * 1.1f;
        Vector2 pL = { px - 6.0f, py + 2.2f };
        Vector2 pR = { px + 6.0f, py + 0.8f };
        Vector2 pT = { px - 1.0f, py - 2.8f };
        DrawTri(pL, pR, pT, (p & 1) ? ironMid : ironLight);
        DrawLineEx(pL, pR, 1.4f, darkIron);
        DrawLineEx(pL, pT, 1.2f, BLACK);
    }
    // Horse eye (no nose armor)
    DrawCircleV((Vector2){ c.x + 8.0f, c.y - 1.6f }, 1.8f, WHITE);
    DrawCircleV((Vector2){ c.x + 8.5f, c.y - 1.6f }, 1.0f, BLACK);

    // 2. Knight helmet perched above/behind, larger for better proportion with horse head
    Vector2 helmC = { c.x - 9.0f, c.y - 10.5f };
    float helmR = 9.2f;
    // Flowing red plume trailing backward from the top of the helmet (3 layered feather triangles)
    float plumeWave = sinf((float)GetTime() * 8.0f) * 1.8f;
    Vector2 plumeRoot = { helmC.x - 1.0f, helmC.y - helmR + 0.5f };
    DrawTri(plumeRoot, (Vector2){ plumeRoot.x - 16.0f, plumeRoot.y - 5.0f + plumeWave },
            (Vector2){ plumeRoot.x - 9.0f,  plumeRoot.y + 2.0f + plumeWave }, plumeDark);
    DrawTri(plumeRoot, (Vector2){ plumeRoot.x - 19.0f, plumeRoot.y - 1.5f + plumeWave },
            (Vector2){ plumeRoot.x - 11.0f, plumeRoot.y - 6.5f + plumeWave }, plumeRed);
    DrawTri(plumeRoot, (Vector2){ plumeRoot.x - 12.0f, plumeRoot.y + 3.0f + plumeWave },
            (Vector2){ plumeRoot.x - 18.0f, plumeRoot.y - 1.0f + plumeWave }, (Color){ 255, 75, 75, 255 });
    DrawLineEx(plumeRoot, (Vector2){ plumeRoot.x - 18.0f, plumeRoot.y - 2.0f + plumeWave }, 1.3f, BLACK);

    // Shining steel knight helmet with pointed visor
    DrawCircleV(helmC, helmR, ironMid);
    DrawCircleSector(helmC, helmR, -110.0f, 70.0f, 12, ironLight);
    DrawTri((Vector2){ helmC.x + 2.5f, helmC.y - 6.0f },
            (Vector2){ helmC.x + 12.2f, helmC.y - 0.5f },
            (Vector2){ helmC.x + 2.5f, helmC.y + 5.5f }, ironLight);
    DrawLineEx((Vector2){ helmC.x + 2.5f, helmC.y - 1.8f }, (Vector2){ helmC.x + 10.0f, helmC.y - 0.5f }, 2.2f, BLACK); // visor slit
    DrawCircleLines((int)helmC.x, (int)helmC.y, helmR, BLACK);

    // 3. Conical jousting lance built from stacked triangles:
    // round base attached to the handle, tapering down to a sharp tip ("dùng các hình tam giác xếp chồng lên nhau,
    // phần đế gắn với cán hình tròn, và phần đỉnh bóp nhỏ lại để nhọn")
    float ang = b->cavalry.aimAngle;
    Vector2 d = { cosf(ang), sinf(ang) };
    Vector2 n = { -d.y, d.x };
    float thrustExtra = (b->cavalry.thrustTimer > 0.0f) ? (sinf((b->cavalry.thrustTimer / 0.24f) * PI) * 14.0f) : 0.0f;

    Vector2 lanceOrigin = V2Add(c, V2Add(V2Mul(d, -6.0f + thrustExtra), V2Mul(n, 9.5f)));
    Vector2 handleRear  = V2Add(lanceOrigin, V2Mul(d, -11.0f));
    Vector2 baseCenter  = V2Add(lanceOrigin, V2Mul(d, r - 2.0f));

    // Cylindrical handle & rounded circularvamplate base attached to the handle
    DrawLineEx(handleRear, baseCenter, 6.0f, BLACK);
    DrawLineEx(handleRear, baseCenter, 4.0f, darkIron);
    DrawCircleV(baseCenter, 7.2f, darkIron);
    DrawCircleV(baseCenter, 5.6f, ironLight);
    DrawCircleLines((int)baseCenter.x, (int)baseCenter.y, 7.2f, BLACK);

    // 4 stacked overlapping conical triangles tapering from the wide circular base down to the sharp point
    float totalLanceLen = 40.0f;
    int numCones = 4;
    for (int k = 0; k < numCones; k++) {
        float f0 = (float)k / (float)numCones;
        float startDist = f0 * (totalLanceLen * 0.62f);
        float tipDist   = startDist + (totalLanceLen - startDist) * ((k == numCones - 1) ? 1.0f : 0.58f);
        float halfW     = 6.6f * (1.0f - f0 * 0.72f);

        Vector2 segBase = V2Add(baseCenter, V2Mul(d, startDist));
        Vector2 segTip  = V2Add(baseCenter, V2Mul(d, tipDist));
        Vector2 bL      = V2Add(segBase, V2Mul(n, -halfW));
        Vector2 bR      = V2Add(segBase, V2Mul(n,  halfW));

        // Two-tone shaded cone triangle (bright top half, steel bottom half)
        DrawTri(bL, segTip, segBase, (k & 1) ? ironMid : ironLight);
        DrawTri(segBase, segTip, bR, (k & 1) ? darkIron : ironMid);
        DrawLineEx(bL, segTip, 1.3f, BLACK);
        DrawLineEx(bR, segTip, 1.3f, BLACK);
        DrawLineEx(bL, bR, 1.2f, darkIron);
    }
}

// Executer: wears a dark iron medieval executioner hood/mask and carries a heavy long-handled greataxe
static void DrawExecuterDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color darkHood  = { 36, 34, 42, 255 };
    Color ironMask  = { 118, 124, 136, 255 };
    Color woodShaft = { 115, 68, 28, 255 };
    Color axeSteel  = { 210, 216, 226, 255 };
    Color bloodEdge = { 185, 20, 25, 255 };

    // Medieval executioner hood & riveted iron face mask
    DrawCircleSector(c, r - 1.5f, 180.0f, 360.0f, 20, darkHood);
    DrawTri((Vector2){ c.x - r + 4.0f, c.y - 4.0f },
            (Vector2){ c.x + r - 4.0f, c.y - 4.0f },
            (Vector2){ c.x, c.y - r - 8.0f }, darkHood);
    DrawRectangle((int)(c.x - 14.0f), (int)(c.y - 6.0f), 28, 16, ironMask);
    DrawRectangleLines((int)(c.x - 14.0f), (int)(c.y - 6.0f), 28, 16, BLACK);

    // Narrow menacing eye slits
    DrawRectangle((int)(c.x - 11.0f), (int)(c.y - 3.0f), 8, 3, BLACK);
    DrawRectangle((int)(c.x + 3.0f),  (int)(c.y - 3.0f), 8, 3, BLACK);
    DrawCircleV((Vector2){ c.x - 7.0f, c.y - 1.5f }, 1.3f, RED);
    DrawCircleV((Vector2){ c.x + 7.0f, c.y - 1.5f }, 1.3f, RED);
    // Vertical breathing grates on iron mask
    for (int k = -2; k <= 2; k++) {
        DrawLineEx((Vector2){ c.x + k * 4.0f, c.y + 3.0f }, (Vector2){ c.x + k * 4.0f, c.y + 8.0f }, 1.5f, BLACK);
    }

    // Heavy long-handled executioner axe:
    // - During 3.0s Sion-Q charge (skillState == 1): slowly raises overhead with trembling weight
    // - During chopAnimTimer > 0: heavy wind-up lift then forceful downward slam
    float axeAng = -0.65f;
    if (b->executer.skillState == 1) {
        float chargeP = ClampF(b->executer.skillTimer / EXECUTER_CHARGE_T, 0.0f, 1.0f);
        float tremble = sinf((float)GetTime() * 38.0f) * 0.05f * chargeP;
        axeAng = b->executer.trapAngle - 1.35f * chargeP + tremble;
    } else if (b->executer.chopAnimTimer > 0.0f) {
        float p = 1.0f - ClampF(b->executer.chopAnimTimer / 0.44f, 0.0f, 1.0f);
        if (p < 0.55f) {
            // Heavy, slow wind-up lifting the massive axe high
            float up = p / 0.55f;
            axeAng = b->executer.chopAngle - 1.45f * up;
        } else {
            // Sudden crushing downward chop onto target
            float down = (p - 0.55f) / 0.45f;
            axeAng = b->executer.chopAngle - 1.45f + 1.85f * down;
        }
    }

    Vector2 d = { cosf(axeAng), sinf(axeAng) };
    Vector2 n = { -d.y, d.x };
    // Extra-long handle ("phần cán dài hơn bình thường")
    Vector2 handleBase = V2Add(c, V2Mul(d, -10.0f));
    Vector2 handleTip  = V2Add(c, V2Mul(d, r + 24.0f));
    DrawLineEx(handleBase, handleTip, 6.0f, BLACK);
    DrawLineEx(handleBase, handleTip, 4.0f, woodShaft);

    // Massive crescent executioner axe blade near the end of the long shaft
    Vector2 headRoot = V2Add(c, V2Mul(d, r + 15.0f));
    Vector2 bladeTop = V2Add(headRoot, V2Add(V2Mul(d,  9.0f), V2Mul(n, 16.0f)));
    Vector2 bladeMid = V2Add(headRoot, V2Mul(n, 19.0f));
    Vector2 bladeBot = V2Add(headRoot, V2Add(V2Mul(d, -9.0f), V2Mul(n, 15.0f)));
    DrawTri(headRoot, bladeTop, bladeMid, axeSteel);
    DrawTri(headRoot, bladeMid, bladeBot, axeSteel);
    DrawLineEx(bladeTop, bladeMid, 3.0f, bloodEdge);
    DrawLineEx(bladeMid, bladeBot, 3.0f, bloodEdge);
    DrawLineEx(headRoot, bladeTop, 1.8f, BLACK);
    DrawLineEx(headRoot, bladeBot, 1.8f, BLACK);
}

// Draws Executer's Sion-Q style trapezoid:
// - Short base = ball diameter (used for geometry only; short base edge is NOT drawn)
// - Length = 1.2x short base, widening to the outer long base with rounded corners
// - Bright crimson border along the sides & outer edge
// - Interior fill starts lighter red at Executer and gradually darkens toward the outer long edge
static void DrawExecuterSionTrapezoid(Vector2 origin, float angle, float chargeProgress, float alphaScale)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };

    float halfShort = EXECUTER_TRAP_SHORT * 0.5f; // 30px
    float halfLong  = EXECUTER_TRAP_LONG  * 0.5f; // 55.5px
    float totalLen  = EXECUTER_TRAP_LEN;          // 72px (1.2x short base)
    float cornerR   = 8.0f;

    // Gradient strips from origin (light red) to outer long edge (deep crimson red)
    int strips = 14;
    for (int s = 0; s < strips; s++) {
        float t0 = (float)s / (float)strips;
        float t1 = (float)(s + 1) / (float)strips;
        float d0 = t0 * totalLen;
        float d1 = t1 * totalLen;
        float hw0 = halfShort + t0 * (halfLong - halfShort);
        float hw1 = halfShort + t1 * (halfLong - halfShort);

        // Slightly inset the very last strip for rounded outer corners
        if (s == strips - 1) hw1 -= cornerR * 0.35f;

        Vector2 p0L = V2Add(origin, V2Add(V2Mul(d, d0), V2Mul(n, -hw0)));
        Vector2 p0R = V2Add(origin, V2Add(V2Mul(d, d0), V2Mul(n,  hw0)));
        Vector2 p1L = V2Add(origin, V2Add(V2Mul(d, d1), V2Mul(n, -hw1)));
        Vector2 p1R = V2Add(origin, V2Add(V2Mul(d, d1), V2Mul(n,  hw1)));

        // Lighter red near Executer -> darker/deeper red toward the outer edge, intensifying as charge progresses
        float grad = t1;
        Color lightRed = { 255, 130, 130, (unsigned char)(70 + (int)(60.0f * chargeProgress)) };
        Color deepRed  = { 210, 15,  20,  (unsigned char)(145 + (int)(85.0f * chargeProgress)) };
        Color sliceCol = LerpColorLinear(lightRed, deepRed, grad);
        sliceCol = Fade(sliceCol, alphaScale * ((float)sliceCol.a / 255.0f));

        DrawTri(p0L, p1L, p1R, sliceCol);
        DrawTri(p0L, p1R, p0R, sliceCol);
    }

    // Rounded corners & bright red border (short base at `origin` is intentionally NOT drawn!)
    Color brightBorder = Fade((Color){ 255, 25, 30, 255 }, 0.92f * alphaScale);
    Vector2 nearL = V2Add(origin, V2Mul(n, -halfShort));
    Vector2 nearR = V2Add(origin, V2Mul(n,  halfShort));
    Vector2 farSideL = V2Add(origin, V2Add(V2Mul(d, totalLen - cornerR), V2Mul(n, -(halfLong - cornerR * 0.25f))));
    Vector2 farSideR = V2Add(origin, V2Add(V2Mul(d, totalLen - cornerR), V2Mul(n,  (halfLong - cornerR * 0.25f))));
    Vector2 farTopL  = V2Add(origin, V2Add(V2Mul(d, totalLen), V2Mul(n, -(halfLong - cornerR))));
    Vector2 farTopR  = V2Add(origin, V2Add(V2Mul(d, totalLen), V2Mul(n,  (halfLong - cornerR))));
    Vector2 farMidL  = V2Add(origin, V2Add(V2Mul(d, totalLen - cornerR * 0.28f), V2Mul(n, -(halfLong - cornerR * 0.55f))));
    Vector2 farMidR  = V2Add(origin, V2Add(V2Mul(d, totalLen - cornerR * 0.28f), V2Mul(n,  (halfLong - cornerR * 0.55f))));

    // Left angled side, rounded outer-left corner, outer long base, rounded outer-right corner, right angled side
    DrawLineEx(nearL,    farSideL, 3.2f, brightBorder);
    DrawLineEx(farSideL, farMidL,  3.2f, brightBorder);
    DrawLineEx(farMidL,  farTopL,  3.2f, brightBorder);
    DrawLineEx(farTopL,  farTopR,  3.6f, brightBorder);
    DrawLineEx(farTopR,  farMidR,  3.2f, brightBorder);
    DrawLineEx(farMidR,  farSideR, 3.2f, brightBorder);
    DrawLineEx(farSideR, nearR,    3.2f, brightBorder);
}

// Helper to draw King's triangular royal flag (blue pennant with gold stripes) at any position
static void DrawKingTriangularFlag(Vector2 basePos, float poleHeight, float scale)
{
    Color poleGold = { 235, 190, 35, 255 };
    Color flagBlue = { 25, 85, 210, 255 };
    Color stripeGd = { 255, 215, 40, 255 };

    Vector2 poleTop = { basePos.x, basePos.y - poleHeight * scale };
    Vector2 fTop    = { poleTop.x, poleTop.y + 2.0f * scale };
    Vector2 fBot    = { poleTop.x, poleTop.y + 22.0f * scale };
    Vector2 fTip    = { poleTop.x + 28.0f * scale, poleTop.y + 12.0f * scale };

    // Triangular blue flag body
    DrawTri(fTop, fBot, fTip, flagBlue);

    // Gold horizontal stripes inside the triangular flag
    for (int s = 1; s <= 2; s++) {
        float frac = (float)s / 3.0f;
        float y = fTop.y + frac * (fBot.y - fTop.y);
        float maxW = (1.0f - fabsf(frac - 0.5f) * 2.0f) * (26.0f * scale);
        DrawLineEx((Vector2){ fTop.x + 1.0f, y }, (Vector2){ fTop.x + maxW, y }, 2.6f * scale, stripeGd);
    }
    DrawLineEx(fTop, fTip, 1.8f, stripeGd);
    DrawLineEx(fBot, fTip, 1.8f, stripeGd);

    // Golden flagpole & finial spearhead
    DrawLineEx(basePos, poleTop, 4.2f * scale, BLACK);
    DrawLineEx(basePos, poleTop, 2.6f * scale, poleGold);
    DrawCircleV(poleTop, 3.2f * scale, stripeGd);
}

// King: wears a golden crown with multiple triangular peaks, a white center cross, and a red velvet cushion lining on top,
// wears a fluttering crimson-and-gold royal cape behind the ball, and holds a golden royal scepter topped with a red orb
static void DrawKingDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color velvetRed = { 205, 28, 35, 255 };
    Color crownGold = { 255, 212, 35, 255 };
    Color deepGold  = { 205, 148, 15, 255 };

    // Royal robe folds inside the ball
    DrawCircleV(c, r * 0.76f, velvetRed);
    DrawRing(c, r * 0.70f, r * 0.78f, 0.0f, 360.0f, 24, crownGold);

    // Crown perched on top of King:
    float crownBaseY = c.y - r + 5.0f;
    float crownHalfW = 19.0f;

    // 1. Red velvet cushion lining rounded on top of the crown ("bên trên vương miện lót một miếng đệm màu đỏ")
    DrawCircleV((Vector2){ c.x - 6.0f, crownBaseY - 9.0f }, 8.5f, velvetRed);
    DrawCircleV((Vector2){ c.x + 6.0f, crownBaseY - 9.0f }, 8.5f, velvetRed);
    DrawCircleV((Vector2){ c.x,        crownBaseY - 11.0f }, 9.5f, velvetRed);

    // 2. Outer golden crown with 5 triangular peaks around the band ("vương miện bên ngoài màu vàng có nhiều hình tam giác xung quanh")
    DrawRectangle((int)(c.x - crownHalfW), (int)(crownBaseY - 5.0f), (int)(crownHalfW * 2.0f), 6, deepGold);
    int numPeaks = 5;
    float peakStep = (crownHalfW * 2.0f) / (float)numPeaks;
    for (int p = 0; p < numPeaks; p++) {
        float xL = (c.x - crownHalfW) + p * peakStep;
        float xR = xL + peakStep;
        float xM = 0.5f * (xL + xR);
        float h  = (p == 2) ? 15.0f : ((p == 1 || p == 3) ? 12.5f : 10.5f);
        Vector2 bL = { xL, crownBaseY - 4.0f };
        Vector2 bR = { xR, crownBaseY - 4.0f };
        Vector2 tip= { xM, crownBaseY - 4.0f - h };
        DrawTri(bL, bR, tip, crownGold);
        DrawLineEx(bL, tip, 1.4f, BLACK);
        DrawLineEx(tip, bR, 1.4f, BLACK);
    }
    DrawRectangleLines((int)(c.x - crownHalfW), (int)(crownBaseY - 5.0f), (int)(crownHalfW * 2.0f), 6, BLACK);

    // 3. White cross in the center of the crown ("ở giữa có cây thánh giá màu trắng")
    Vector2 crossC = { c.x, crownBaseY - 10.0f };
    DrawLineEx((Vector2){ crossC.x, crossC.y - 6.5f }, (Vector2){ crossC.x, crossC.y + 6.5f }, 3.2f, WHITE);
    DrawLineEx((Vector2){ crossC.x - 5.0f, crossC.y - 1.5f }, (Vector2){ crossC.x + 5.0f, crossC.y - 1.5f }, 3.2f, WHITE);

    // 4. Golden royal scepter topped with a crimson red orb held at King's side
    // ("cho vua cầm một cây trượng với thân trượng màu vàng và trên đỉnh có quả cầu đỏ")
    float scepterAng = -1.18f; // angled slightly outward to the right
    Vector2 sd = { cosf(scepterAng), sinf(scepterAng) };
    Vector2 sn = { -sd.y, sd.x };
    Vector2 sBase = { c.x + r - 8.0f, c.y + 12.0f };
    Vector2 sTop  = V2Add(sBase, V2Mul(sd, 34.0f));

    // Golden scepter shaft & decorative collar
    DrawLineEx(sBase, sTop, 6.2f, BLACK);
    DrawLineEx(sBase, sTop, 4.0f, crownGold);
    DrawCircleV(sBase, 3.2f, deepGold);
    DrawCircleLines((int)sBase.x, (int)sBase.y, 3.2f, BLACK);

    // Golden prongs cradling the red orb at the top of the scepter
    Vector2 collar = V2Add(sTop, V2Mul(sd, -4.0f));
    DrawLineEx(V2Add(collar, V2Mul(sn, -6.0f)), V2Add(collar, V2Mul(sn, 6.0f)), 3.5f, crownGold);
    DrawCircleV(sTop, 6.2f, (Color){ 225, 25, 32, 255 });
    DrawCircleV((Vector2){ sTop.x - 1.8f, sTop.y - 1.8f }, 2.2f, (Color){ 255, 150, 150, 255 });
    DrawCircleLines((int)sTop.x, (int)sTop.y, 6.2f, crownGold);
    DrawCircleLines((int)sTop.x, (int)sTop.y, 6.8f, BLACK);
}

// Catapult (Mythic - Medieval V2): bright wood brown & bright stone gray siege catapult
static void DrawCatapultDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color darkWood  = { 128, 72, 26, 255 };
    Color brightWood= { 212, 142, 68, 255 };
    Color stoneGray = { 195, 202, 210, 255 };
    Color stoneDark = { 135, 142, 152, 255 };
    Color ironBand  = { 72, 76, 84, 255 };

    // Wooden siege frame crossbeams & A-frame struts inside the ball
    DrawRectangle((int)(c.x - r * 0.78f), (int)(c.y + 4.0f), (int)(r * 1.56f), 8, darkWood);
    DrawRectangleLines((int)(c.x - r * 0.78f), (int)(c.y + 4.0f), (int)(r * 1.56f), 8, BLACK);
    DrawLineEx((Vector2){ c.x - 13.0f, c.y + 8.0f }, (Vector2){ c.x - 2.0f, c.y - 10.0f }, 4.5f, brightWood);
    DrawLineEx((Vector2){ c.x + 13.0f, c.y + 8.0f }, (Vector2){ c.x + 2.0f, c.y - 10.0f }, 4.5f, brightWood);

    // Two wooden siege wheels with iron rims at lower-left and lower-right
    for (int side = -1; side <= 1; side += 2) {
        Vector2 wPos = { c.x + side * 13.0f, c.y + 11.0f };
        DrawCircleV(wPos, 6.5f, darkWood);
        DrawRing(wPos, 4.8f, 6.8f, 0.0f, 360.0f, 16, ironBand);
        DrawCircleV(wPos, 2.2f, stoneGray);
    }

    // Swinging catapult throwing arm & bright gray stone bucket
    float armSwing = -0.75f;
    if (b->catapult.armAnimTimer > 0.0f) {
        float p = 1.0f - ClampF(b->catapult.armAnimTimer / 0.45f, 0.0f, 1.0f);
        armSwing = -0.75f + sinf(p * PI) * 1.35f;
    }
    Vector2 pivot = { c.x, c.y - 4.0f };
    Vector2 armDir = { cosf(armSwing - PI * 0.5f), sinf(armSwing - PI * 0.5f) };
    Vector2 bucketPos = V2Add(pivot, V2Mul(armDir, 19.0f));

    DrawLineEx(pivot, bucketPos, 6.0f, BLACK);
    DrawLineEx(pivot, bucketPos, 4.0f, brightWood);
    DrawCircleV(pivot, 4.2f, ironBand);

    // Spoon bucket & bright gray stone boulder loaded when ready
    DrawCircleV(bucketPos, 7.5f, darkWood);
    DrawCircleLines((int)bucketPos.x, (int)bucketPos.y, 7.5f, BLACK);
    if (!b->catapult.boulderActive) {
        DrawCircleV(bucketPos, 6.2f, stoneGray);
        DrawCircleV((Vector2){ bucketPos.x - 1.8f, bucketPos.y - 1.8f }, 2.2f, WHITE);
        DrawCircleV((Vector2){ bucketPos.x + 2.0f, bucketPos.y + 1.5f }, 1.8f, stoneDark);
        DrawCircleLines((int)bucketPos.x, (int)bucketPos.y, 6.2f, BLACK);
    }
}

// Helper to draw Archer's fire-tipped arrow in flight
static void DrawFlyingArcherFireArrow(const ArcherData* ar)
{
    if (!ar->arrowActive) return;
    Vector2 d = { cosf(ar->arrowAngle), sinf(ar->arrowAngle) };
    Vector2 n = { -d.y, d.x };

    float halfL = ARCHER_ARROW_LEN * 0.5f;
    Vector2 tail     = V2Add(ar->arrowPos, V2Mul(d, -halfL));
    Vector2 headBase = V2Add(ar->arrowPos, V2Mul(d, halfL - 12.0f));
    Vector2 tip      = V2Add(ar->arrowPos, V2Mul(d, halfL));

    // Steel-gray & wood shaft
    DrawLineEx(tail, headBase, 5.5f, BLACK);
    DrawLineEx(tail, headBase, 3.4f, (Color){ 175, 182, 192, 255 });

    // Green fletching feathers at tail
    Vector2 f1 = V2Add(tail, V2Mul(d, 8.0f));
    DrawLineEx(f1, V2Add(tail, V2Mul(n,  5.5f)), 2.8f, (Color){ 65, 200, 85, 255 });
    DrawLineEx(f1, V2Add(tail, V2Mul(n, -5.5f)), 2.8f, (Color){ 65, 200, 85, 255 });

    // Steel arrowhead engulfed in fire at the tip
    Vector2 hL = V2Add(headBase, V2Mul(n,  6.8f));
    Vector2 hR = V2Add(headBase, V2Mul(n, -6.8f));
    DrawTri(tip, hL, hR, (Color){ 195, 202, 212, 255 });
    DrawCircleV(headBase, 8.5f, Fade((Color){ 255, 90, 15, 255 }, 0.65f));
    DrawTri(V2Add(tip, V2Mul(d, 5.0f)),
            V2Add(headBase, V2Mul(n,  5.5f)),
            V2Add(headBase, V2Mul(n, -5.5f)), (Color){ 255, 140, 20, 255 });
    DrawCircleV(tip, 3.8f, GOLD);
}

// Archer (Rare - Medieval V2): multi-shade forest green background & bright gray recurve bow with a fire-tipped arrow
static void DrawArcherDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color deepGreen   = { 24, 92, 38, 255 };
    Color midGreen    = { 46, 148, 66, 255 };
    Color lightGreen  = { 88, 195, 102, 255 };
    Color brightGray  = { 222, 228, 238, 255 };
    Color silverWhite = { 246, 248, 252, 255 };

    // Multi-shade green ranger tunic/hood background inside the ball ("archer nên dùng nền màu xanh lá (nhiều sắc độ)")
    DrawCircleSector(c, r - 1.5f, 135.0f, 315.0f, 20, deepGreen);
    DrawCircleSector(c, r - 1.5f, -45.0f, 135.0f, 20, midGreen);
    DrawCircleV(c, r * 0.66f, lightGreen);
    DrawCircleV(c, r * 0.42f, deepGreen);

    float a = b->archer.aimAngle;
    Vector2 d = { cosf(a), sinf(a) };
    Vector2 n = { -d.y, d.x };

    // Prominent bright-gray recurve bow ("phần cung dùng màu xám sáng, vì màu xám tối hiện tại đang khá khó nhìn")
    Vector2 topTip = V2Add(c, V2Add(V2Mul(d, -4.0f), V2Mul(n, 18.5f)));
    Vector2 midTop = V2Add(c, V2Add(V2Mul(d,  9.5f), V2Mul(n, 10.5f)));
    Vector2 handle = V2Add(c, V2Mul(d, 12.5f));
    Vector2 midBot = V2Add(c, V2Add(V2Mul(d,  9.5f), V2Mul(n, -10.5f)));
    Vector2 botTip = V2Add(c, V2Add(V2Mul(d, -4.0f), V2Mul(n, -18.5f)));

    // Bowstring
    DrawLineEx(topTip, botTip, 2.2f, WHITE);

    // Black outline behind the bow limbs so the bright gray bow stands out crisply against the green background
    DrawLineEx(topTip, midTop, 6.8f, BLACK);
    DrawLineEx(midTop, handle, 7.4f, BLACK);
    DrawLineEx(handle, midBot, 7.4f, BLACK);
    DrawLineEx(midBot, botTip, 6.8f, BLACK);

    // Bright steel-gray bow limbs & silver grip highlight
    DrawLineEx(topTip, midTop, 4.4f, brightGray);
    DrawLineEx(midTop, handle, 5.0f, silverWhite);
    DrawLineEx(handle, midBot, 5.0f, silverWhite);
    DrawLineEx(midBot, botTip, 4.4f, brightGray);

    // Nocked arrow with burning fire at the arrowhead when ready
    if (!b->archer.arrowActive) {
        Vector2 aTail = V2Add(c, V2Mul(d, -5.0f));
        Vector2 aTip  = V2Add(c, V2Mul(d, 19.0f));
        DrawLineEx(aTail, aTip, 4.5f, BLACK);
        DrawLineEx(aTail, aTip, 2.8f, silverWhite);
        // Burning flame on the arrowhead
        DrawCircleV(aTip, 6.2f, Fade((Color){ 255, 85, 10, 255 }, 0.78f));
        DrawTri(V2Add(aTip, V2Mul(d, 7.5f)),
                V2Add(aTip, V2Mul(n, 4.8f)),
                V2Add(aTip, V2Mul(n, -4.8f)), (Color){ 255, 165, 25, 255 });
        DrawCircleV(aTip, 3.0f, YELLOW);
    }
}

// Squire (Common - Medieval): sand yellow & skin tone body, bright red cross in the center,
// gray rim near the outer edge, and carries a medieval sword
static void DrawSquireDetails(const Ball* b)
{
    Vector2 c = b->pos;
    float r = (float)b->radius;
    Color rimGray    = { 148, 156, 168, 255 };
    Color sandYellow = { 232, 202, 135, 255 };
    Color skinTone   = { 244, 208, 172, 255 };
    Color brightRed  = { 225, 28, 34, 255 };
    Color bladeSteel = { 228, 234, 242, 255 };
    Color bladeGray  = { 162, 170, 182, 255 };
    Color hiltWood   = { 122, 68, 28, 255 };

    // Gray mail/steel band right along the outer rim ("màu xám ở sát viền ngoài")
    DrawRing(c, r - 5.5f, r - 1.5f, 0.0f, 360.0f, 32, rimGray);
    DrawCircleLines((int)c.x, (int)c.y, r - 5.5f, Fade(BLACK, 0.45f));

    // Sand yellow surcoat & warm skin-tone upper/inner face ("màu vàng cát, màu da")
    DrawCircleV(c, r - 5.5f, sandYellow);
    DrawCircleSector(c, r - 6.0f, 200.0f, 340.0f, 18, skinTone);
    DrawCircleV((Vector2){ c.x, c.y - 6.0f }, 8.5f, skinTone);

    // Bright red cross in the center ("có chữ thập màu đỏ tươi ở giữa")
    DrawRectangle((int)(c.x - 3.5f), (int)(c.y - 14.0f), 7, 28, brightRed);
    DrawRectangle((int)(c.x - 13.0f), (int)(c.y - 3.5f), 26, 7, brightRed);
    DrawRectangleLines((int)(c.x - 3.5f), (int)(c.y - 14.0f), 7, 28, Fade(BLACK, 0.45f));
    DrawRectangleLines((int)(c.x - 13.0f), (int)(c.y - 3.5f), 26, 7, Fade(BLACK, 0.45f));

    // Medieval squire short-sword held on the right side ("cầm thêm thanh kiếm kiểu medieval")
    float swordAng = (V2DistSq(b->vel, (Vector2){ 0, 0 }) > 0.01f)
                   ? (atan2f(b->vel.y, b->vel.x) - 0.35f)
                   : -0.55f;
    Vector2 sd = { cosf(swordAng), sinf(swordAng) };
    Vector2 sn = { -sd.y, sd.x };
    Vector2 guardPos  = { c.x + r - 2.0f, c.y + 3.0f };
    Vector2 pommelPos = V2Add(guardPos, V2Mul(sd, -8.5f));
    Vector2 bladeNeck = V2Add(guardPos, V2Mul(sd, 19.0f));
    Vector2 bladeTip  = V2Add(guardPos, V2Mul(sd, 26.0f));

    // Grip & pommel
    DrawLineEx(pommelPos, guardPos, 5.2f, BLACK);
    DrawLineEx(pommelPos, guardPos, 3.4f, hiltWood);
    DrawCircleV(pommelPos, 3.4f, rimGray);
    DrawCircleLines((int)pommelPos.x, (int)pommelPos.y, 3.4f, BLACK);

    // Medieval sword blade
    float bw = 4.2f;
    Vector2 bL0 = V2Add(guardPos,  V2Mul(sn, -bw));
    Vector2 bR0 = V2Add(guardPos,  V2Mul(sn,  bw));
    Vector2 bL1 = V2Add(bladeNeck, V2Mul(sn, -bw * 0.8f));
    Vector2 bR1 = V2Add(bladeNeck, V2Mul(sn,  bw * 0.8f));
    DrawTri(bL0, bL1, bladeNeck, bladeSteel);
    DrawTri(bL0, bladeNeck, guardPos, bladeSteel);
    DrawTri(guardPos, bladeNeck, bR1, bladeGray);
    DrawTri(guardPos, bR1, bR0, bladeGray);
    DrawTri(bL1, bladeTip, bladeNeck, bladeSteel);
    DrawTri(bladeNeck, bladeTip, bR1, bladeGray);
    DrawLineEx(bL0, bL1, 1.4f, BLACK);
    DrawLineEx(bL1, bladeTip, 1.4f, BLACK);
    DrawLineEx(bR0, bR1, 1.4f, BLACK);
    DrawLineEx(bR1, bladeTip, 1.4f, BLACK);

    // Steel crossguard
    Vector2 gL = V2Add(guardPos, V2Mul(sn, -8.5f));
    Vector2 gR = V2Add(guardPos, V2Mul(sn,  8.5f));
    DrawLineEx(gL, gR, 5.0f, BLACK);
    DrawLineEx(gL, gR, 3.2f, rimGray);
}

static void DrawWindStunOverlay(const Ball* b)
{
    if (!b->windStunned) return;

    float spin = (float)GetTime() * 10.0f;
    Vector2 headCenter = { b->pos.x, b->pos.y - b->radius - 34.0f };

    for (int k = 0; k < 14; k++) {
        float a0 = spin + k * (2.0f * PI / 14.0f);
        float a1 = a0 + 0.35f;
        Vector2 p0 = { headCenter.x + cosf(a0) * 16.0f, headCenter.y + sinf(a0) * 5.5f };
        Vector2 p1 = { headCenter.x + cosf(a1) * 16.0f, headCenter.y + sinf(a1) * 5.5f };
        Color swCol = (k & 1) ? (Color){ 165, 45, 235, 255 } : BLACK;
        DrawLineEx(p0, p1, 2.6f, swCol);
    }
    Vector2 innerCenter = { headCenter.x, headCenter.y - 3.0f };
    for (int k = 0; k < 8; k++) {
        float a0 = -spin * 1.3f + k * (2.0f * PI / 8.0f);
        float a1 = a0 + 0.45f;
        Vector2 p0 = { innerCenter.x + cosf(a0) * 9.0f, innerCenter.y + sinf(a0) * 3.2f };
        Vector2 p1 = { innerCenter.x + cosf(a1) * 9.0f, innerCenter.y + sinf(a1) * 3.2f };
        Color swCol = (k & 1) ? BLACK : (Color){ 200, 90, 255, 255 };
        DrawLineEx(p0, p1, 2.2f, swCol);
    }

    float ratio = ClampF(b->windStunTimer / WIND_STUN_TIME, 0.0f, 1.0f);
    float barW = 46.0f, barH = 6.0f;
    float barX = b->pos.x - barW * 0.5f;
    float barY = b->pos.y - b->radius - 50.0f;
    DrawRectangle((int)barX, (int)barY, (int)barW, (int)barH, Fade(BLACK, 0.65f));
    DrawRectangle((int)barX, (int)barY, (int)(barW * ratio), (int)barH, (Color){ 175, 60, 245, 255 });
    DrawRectangleLines((int)barX, (int)barY, (int)barW, (int)barH, BLACK);

    const char* tStr = TextFormat("%.1fs", b->windStunTimer);
    DrawText(tStr, (int)b->pos.x - MeasureText(tStr, 11) / 2, (int)barY - 12, 11, (Color){ 110, 20, 165, 255 });
}

// Trident: shaft length increased by +20% (-24.8f instead of -20.0f) and shaft width increased by +30% (3.9f instead of 3.0f)
static void DrawTrident(Vector2 c, float angle, Color col)
{
    Vector2 d = { cosf(angle), sinf(angle) };
    Vector2 n = { -d.y, d.x };

    Vector2 base = V2Add(c, V2Mul(d, -24.8f)); // +20% shaft length
    Vector2 neck = V2Add(c, V2Mul(d, 4.0f));
    Vector2 tip  = V2Add(c, V2Mul(d, 20.0f));
    Vector2 l0 = V2Add(neck, V2Mul(n, 9.0f)),  r0 = V2Add(neck, V2Mul(n, -9.0f));
    Vector2 l1 = V2Add(l0, V2Mul(d, 14.0f)),   r1 = V2Add(r0, V2Mul(d, 14.0f));

    DrawLineEx(base, tip, 3.9f, col);          // +30% shaft width
    DrawLineEx(l0, r0, 3.2f, col);
    DrawLineEx(l0, l1, 3.0f, col);
    DrawLineEx(r0, r1, 3.0f, col);

    Vector2 ends[3] = { tip, l1, r1 };
    for (int i = 0; i < 3; i++) {
        DrawTri(V2Add(ends[i], V2Mul(d, 7.0f)),
                V2Add(ends[i], V2Mul(n, 3.5f)),
                V2Add(ends[i], V2Mul(n, -3.5f)), col);
    }
}

static void DrawBolt(Vector2 c)
{
    Vector2 p[4] = { { c.x + 6, c.y - 22 }, { c.x - 7, c.y + 1 }, { c.x + 3, c.y + 1 }, { c.x - 6, c.y + 22 } };
    Color blue = { 30, 100, 255, 255 };
    for (int i = 0; i < 3; i++) DrawLineEx(p[i], p[i + 1], 5, blue);
}

static void DrawElectricRim(Vector2 c, float r, int segs, Color col)
{
    float step = 2.0f * PI / (float)segs;
    for (int i = 0; i < segs; i++)
    {
        float a0 = i * step + (float)GetRandomValue(-10, 10) * 0.01f;
        float a1 = a0 + step * 0.5f;
        float a2 = a0 + step;
        float rm = r + (float)GetRandomValue(4, 13);
        Vector2 p0 = { c.x + cosf(a0) * r,  c.y + sinf(a0) * r };
        Vector2 p1 = { c.x + cosf(a1) * rm, c.y + sinf(a1) * rm };
        Vector2 p2 = { c.x + cosf(a2) * r,  c.y + sinf(a2) * r };
        DrawLineEx(p0, p1, 2, col);
        DrawLineEx(p1, p2, 2, col);
    }
}

static void DrawUnderlay(const Ball* b)
{
    if (!IsAlive(b)) return;

    if (IsHarpoonActive(b)) {
        for (int k = 0; k < b->harpoon.trailCount; k += 2)
            DrawCircleV(b->harpoon.trail[k], 2.5f, Fade(BLACK, 0.45f));
        float arrowAngle = atan2f(b->harpoon.vel.y, b->harpoon.vel.x);
        if (b->harpoon.state == H_PULLING && b->harpoon.trailCount > 0) {
            Vector2 d = V2Sub(b->harpoon.pos, b->pos);
            arrowAngle = atan2f(d.y, d.x);
        }
        DrawBlackArrow(b->harpoon.pos, arrowAngle, 26.0f, HARPOON_HEAD_SCALE);
    }

    if (b->type == TYPE_ICE) {
        for (int k = 0; k < ICE_MAX; k++) {
            const IceSeg* s = &b->ice[k];
            if (!s->active) continue;
            float a = 0.06f + 0.22f * (1.0f - s->age / ICE_TRAIL_TIME);
            Rectangle r = { s->pos.x, s->pos.y, ICE_HALF_L * 2.0f, b->radius * 2.0f };
            DrawRectanglePro(r, (Vector2){ ICE_HALF_L, (float)b->radius },
                             s->angle * RAD2DEG, Fade(SKYBLUE, a));
        }
    }

    if (b->type == TYPE_CHESS && b->chess.active && b->chess.trailCount > 1) {
        for (int k = 0; k < b->chess.trailCount - 1; k++) {
            DrawLineEx(b->chess.trail[k], b->chess.trail[k + 1], 3.0f, Fade(DARKGRAY, 0.65f));
        }
        DrawCircleV(b->chess.anchor, 5.0f, Fade(BLACK, 0.45f));
    }

    // Train railway tracks (width = ball diameter = 60px: wooden crossties + 2 iron rails)
    if (b->type == TYPE_TRAIN && b->train.trackCount > 1) {
        float halfW = TRAIN_TRACK_W * 0.5f;
        float railOffset = halfW - 6.0f;
        Color woodTie = { 125, 76, 34, 210 };
        Color ironRail = { 70, 75, 84, 235 };

        for (int k = 0; k < b->train.trackCount - 1; k++) {
            Vector2 p0 = b->train.track[k];
            Vector2 p1 = b->train.track[k + 1];
            Vector2 seg = V2Sub(p1, p0);
            float len = sqrtf(seg.x * seg.x + seg.y * seg.y);
            if (len < 0.01f) continue;
            Vector2 d = { seg.x / len, seg.y / len };
            Vector2 n = { -d.y, d.x };

            // Wooden crosstie sleeper at each track node
            DrawLineEx(V2Add(p0, V2Mul(n, -halfW)), V2Add(p0, V2Mul(n, halfW)), 5.0f, woodTie);
            // Left and right iron rails
            DrawLineEx(V2Add(p0, V2Mul(n, -railOffset)), V2Add(p1, V2Mul(n, -railOffset)), 3.2f, ironRail);
            DrawLineEx(V2Add(p0, V2Mul(n,  railOffset)), V2Add(p1, V2Mul(n,  railOffset)), 3.2f, ironRail);
        }
    }

    // Shackless ground target telegraphs, lingering earthquake fissure, & golden sword ground cracks
    if (b->type == TYPE_UNSHACKLED) {
        const UnshackledData* u = &b->unshackled;
        if (u->mode == 1) {
            // 0.50s leap target warning ring (+25% size)
            DrawCircleLines((int)u->targetPos.x, (int)u->targetPos.y, UNSHACKLED_SLAM_RAD, Fade(BLACK, 0.65f));
            DrawCircleV(u->targetPos, UNSHACKLED_SLAM_RAD, Fade(PURPLE, 0.18f));
        } else if (u->mode == 2) {
            // 0.95s Lux rainbow laser charge telegraph line (width = +25% wider)
            Vector2 dir = { cosf(u->aimAngle), sinf(u->aimAngle) };
            Vector2 n   = { -dir.y, dir.x };
            Vector2 end = V2Add(b->pos, V2Mul(dir, 1200.0f));
            float hw = UNSHACKLED_LUX_W * 0.5f;
            DrawLineEx(b->pos, end, 2.5f, Fade(GOLD, 0.8f));
            DrawLineEx(V2Add(b->pos, V2Mul(n, -hw)), V2Add(end, V2Mul(n, -hw)), 1.8f, Fade(MAGENTA, 0.65f));
            DrawLineEx(V2Add(b->pos, V2Mul(n,  hw)), V2Add(end, V2Mul(n,  hw)), 1.8f, Fade(SKYBLUE, 0.65f));
        } else if (u->mode == 3) {
            // Bright golden ground cracks forming underneath the descending sword (no circle underneath)
            float p = ClampF(u->timer / UNSHACKLED_SWORD_T, 0.0f, 1.0f);
            DrawGoldenGroundCracks(u->targetPos, (float)BASE_RADIUS * (0.9f + 0.6f * p), 0.45f + 0.45f * p);
        }

        if (u->fxTimer > 0.0f && u->fxKind == 1) {
            // Black ground fissure (+25% size) + bumpy brown upheaved earth rim:
            // stays at full opacity for 0.35s, then fades out and disappears over 0.10s
            float slamR = UNSHACKLED_SLAM_RAD;
            float a = (u->fxTimer >= UNSHACKLED_SLAM_FADE_T)
                ? 1.0f
                : ClampF(u->fxTimer / UNSHACKLED_SLAM_FADE_T, 0.0f, 1.0f);

            // Outer bumpy brown earthen rim simulating soil upheaved in a circle
            int segs = 24;
            Color darkEarth  = Fade((Color){ 92, 52, 22, 255 }, 0.92f * a);
            Color midEarth   = Fade((Color){ 132, 78, 34, 255 }, 0.92f * a);
            Color lightEarth = Fade((Color){ 168, 108, 52, 255 }, 0.88f * a);
            for (int s = 0; s < segs; s++) {
                float a0 = s * (2.0f * PI / (float)segs);
                float a1 = (s + 1) * (2.0f * PI / (float)segs);
                float aMid = 0.5f * (a0 + a1);
                // Uneven bumpy radius profile around the crater rim
                float bump = 6.5f + ((s % 3 == 0) ? 5.5f : ((s & 1) ? 3.2f : 1.5f));
                Vector2 in0  = { u->fxPos.x + cosf(a0)   * (slamR - 4.0f), u->fxPos.y + sinf(a0)   * (slamR - 4.0f) };
                Vector2 in1  = { u->fxPos.x + cosf(a1)   * (slamR - 4.0f), u->fxPos.y + sinf(a1)   * (slamR - 4.0f) };
                Vector2 peak = { u->fxPos.x + cosf(aMid) * (slamR + bump), u->fxPos.y + sinf(aMid) * (slamR + bump) };
                DrawTri(in0, peak, in1, (s & 1) ? midEarth : darkEarth);
                DrawLineEx(in0, peak, 2.0f, darkEarth);
                DrawLineEx(peak, in1, 2.0f, darkEarth);
                DrawCircleV((Vector2){ u->fxPos.x + cosf(a0) * (slamR + 1.5f), u->fxPos.y + sinf(a0) * (slamR + 1.5f) },
                            4.2f, (s & 1) ? lightEarth : midEarth);
            }

            // Inner black seismic crater & cracks
            DrawCircleV(u->fxPos, slamR - 2.0f, Fade(BLACK, 0.52f * a));
            DrawRing(u->fxPos, slamR - 5.0f, slamR - 1.0f, 0.0f, 360.0f, 28, Fade(BLACK, 0.90f * a));
            for (int k = 0; k < 8; k++) {
                float ang = k * (PI * 0.25f) + 0.2f;
                Vector2 mid = { u->fxPos.x + cosf(ang + 0.15f) * (slamR * 0.5f),
                                u->fxPos.y + sinf(ang + 0.15f) * (slamR * 0.5f) };
                Vector2 tip = { u->fxPos.x + cosf(ang) * (slamR - 2.0f),
                                u->fxPos.y + sinf(ang) * (slamR - 2.0f) };
                DrawLineEx(u->fxPos, mid, 3.8f, Fade(BLACK, a));
                DrawLineEx(mid, tip, 2.4f, Fade(BLACK, a));
            }
        } else if (u->fxTimer > 0.0f && u->fxKind == 3) {
            // Bright golden ground cracks at sword impact point (no circle underneath)
            float a = ClampF(u->fxTimer / 0.55f, 0.0f, 1.0f);
            DrawGoldenGroundCracks(u->fxPos, (float)BASE_RADIUS * 1.65f, a);
        }
    }

    // Judgement 0.75s charge: 35% window-width range ring (guaranteed hit if target stays inside) + telegraph line
    if (b->type == TYPE_JUDGEMENT && b->judgement.state == 1) {
        DrawCircleV(b->judgement.anchorPos, JUDGEMENT_RAM_RANGE, Fade(GOLD, 0.07f));
        DrawCircleLines((int)b->judgement.anchorPos.x, (int)b->judgement.anchorPos.y, JUDGEMENT_RAM_RANGE, Fade(GOLD, 0.65f));
        DrawCircleLines((int)b->judgement.anchorPos.x, (int)b->judgement.anchorPos.y, JUDGEMENT_RAM_RANGE - 2.0f, Fade(WHITE, 0.50f));
        Vector2 endP = V2Add(b->pos, V2Mul(b->judgement.ramDir, JUDGEMENT_RAM_RANGE));
        DrawLineEx(b->pos, endP, 3.0f, Fade(GOLD, 0.65f));
        DrawCircleLines((int)endP.x, (int)endP.y, (float)b->radius * 0.6f, Fade(GOLD, 0.65f));
    }

    // Spider white web lines stretched across the arena between wall bounce points
    if (b->type == TYPE_SPIDER) {
        const SpiderData* sp = &b->spider;
        if (sp->hasPendingAnchor) {
            // Subtle preview thread from pending wall anchor to Spider's current position
            DrawLineEx(sp->pendingAnchor, b->pos, 1.6f, Fade(WHITE, 0.55f));
            DrawCircleV(sp->pendingAnchor, 4.5f, WHITE);
            DrawCircleLines((int)sp->pendingAnchor.x, (int)sp->pendingAnchor.y, 4.5f, BLACK);
        }
        for (int wIdx = 0; wIdx < SPIDER_WEB_MAX; wIdx++) {
            const SpiderWebLine* w = &sp->webs[wIdx];
            if (!w->active) continue;
            DrawLineEx(w->p1, w->p2, 4.5f, Fade(BLACK, 0.35f));
            DrawLineEx(w->p1, w->p2, 2.8f, WHITE);
            DrawCircleV(w->p1, 3.5f, WHITE);
            DrawCircleV(w->p2, 3.5f, WHITE);
        }
    }

    // Range expanding outer damage ring (starts at 1.1x ball radius, expands every 2.0s, deals 2 HP every 0.5s)
    if (b->type == TYPE_RANGE) {
        float rr = b->range.ringRadius;
        DrawCircleV(b->pos, rr, Fade(GOLD, 0.14f));
        DrawRing(b->pos, rr - 2.5f, rr + 0.5f, 0.0f, 360.0f, 48, (Color){ 245, 190, 15, 230 });
        DrawCircleLines((int)b->pos.x, (int)b->pos.y, rr, BLACK);
    }

    // Knight 30-degree cone slash telegraph (0.10s delay) & active sword sweep (reach = 85% ball size)
    if (b->type == TYPE_KNIGHT) {
        float coneR = (float)b->radius + KNIGHT_CONE_REACH;
        if (b->knight.windingUp) {
            float deg = b->knight.slashAngle * RAD2DEG;
            DrawCircleSector(b->pos, coneR, deg - 15.0f, deg + 15.0f, 16, Fade(WHITE, 0.25f));
            DrawCircleSectorLines(b->pos, coneR, deg - 15.0f, deg + 15.0f, 16, Fade(GOLD, 0.65f));
        } else if (b->knight.fxTimer > 0.0f) {
            float a = ClampF(b->knight.fxTimer / 0.22f, 0.0f, 1.0f);
            float deg = b->knight.fxAngle * RAD2DEG;
            DrawCircleSector(b->pos, coneR, deg - 15.0f, deg + 15.0f, 16, Fade(WHITE, 0.62f * a));
            DrawRing(b->pos, coneR - 7.0f, coneR, deg - 15.0f, deg + 15.0f, 16, Fade(GOLD, 0.90f * a));
        }
    }

    // Executer 3.0s Sion-Q trapezoid charge & slam flash
    if (b->type == TYPE_EXECUTER) {
        if (b->executer.skillState == 1) {
            float p = ClampF(b->executer.skillTimer / EXECUTER_CHARGE_T, 0.0f, 1.0f);
            DrawExecuterSionTrapezoid(b->pos, b->executer.trapAngle, p, 1.0f);
        } else if (b->executer.slamFxTimer > 0.0f) {
            float a = ClampF(b->executer.slamFxTimer / 0.45f, 0.0f, 1.0f);
            DrawExecuterSionTrapezoid(b->pos, b->executer.trapAngle, 1.0f, a);
        }
    }

    // King planted blue-and-gold triangular flag & 1.25x radius impact ring
    if (b->type == TYPE_KING && b->king.flagActive) {
        DrawCircleV(b->king.flagPos, KING_FLAG_RAD, Fade(SKYBLUE, 0.16f));
        DrawCircleLines((int)b->king.flagPos.x, (int)b->king.flagPos.y, KING_FLAG_RAD, Fade(GOLD, 0.75f));
        DrawKingTriangularFlag(b->king.flagPos, 42.0f, 1.05f);
    }

    // Catapult incoming boulder landing target telegraph ring (3.5x ball radius) & ground shadow
    if (b->type == TYPE_CATAPULT && b->catapult.boulderActive) {
        float t = ClampF(b->catapult.boulderTimer / CATAPULT_FLIGHT_T, 0.0f, 1.0f);
        Vector2 tPos = b->catapult.boulderTarget;
        DrawCircleV(tPos, CATAPULT_AOE_RAD, Fade((Color){ 155, 110, 58, 255 }, 0.12f + 0.14f * t));
        DrawCircleLines((int)tPos.x, (int)tPos.y, CATAPULT_AOE_RAD, Fade(BLACK, 0.45f + 0.40f * t));
        DrawCircleLines((int)tPos.x, (int)tPos.y, CATAPULT_AOE_RAD * (1.0f - t), Fade(RED, 0.65f));

        // Ground shadow moving linearly from start to target underneath the airborne boulder
        Vector2 groundPos = V2Add(b->catapult.boulderStart, V2Mul(V2Sub(tPos, b->catapult.boulderStart), t));
        float shadowScale = 0.6f + 0.4f * (1.0f - 4.0f * t * (1.0f - t));
        DrawEllipse((int)groundPos.x, (int)groundPos.y, 14.0f * shadowScale, 8.0f * shadowScale, Fade(BLACK, 0.38f));
    }
}

static void DrawBallEntity(const Ball* b)
{
    if (!IsAlive(b)) return;
    const Ball* enemy = FindNearestEnemy(b);

    bool burstFire = (b->type == TYPE_BURST) &&
                     (b->isDashing || (b->skillTimer <= BURST_PREFIRE_TIME && b->skillTimer > 0.0f));

    if ((b->type == TYPE_BURST && b->isDashing) || b->type == TYPE_ICE) {
        Color blurCol = (b->type == TYPE_ICE) ? SKYBLUE : b->color;
        float maxAlpha = (b->type == TYPE_ICE) ? 0.18f : 0.25f;
        for (int j = SHADOW_N - 1; j >= 0; j--) {
            float t = 1.0f - j / (float)SHADOW_N;
            DrawCircleV(b->shadowHistory[j], b->radius * (0.6f + 0.4f * t),
                        Fade(blurCol, 0.04f + maxAlpha * t));
        }
    }

    if (b->type == TYPE_ICE) {
        DrawCircleV(b->pos, b->radius + 7.0f, Fade(SKYBLUE, 0.22f));
        DrawCircleV(b->pos, b->radius + 12.0f, Fade(WHITE, 0.14f));
    }

    if (burstFire) DrawBurstFireRim(b);

    if (b->type == TYPE_CACTUS) {
        DrawCactusOuterSpikes(b->pos, (float)b->radius, 9.0f);
    }

    // Knight and King wear a fluttering truncated-triangle royal cape behind the ball
    // (drawn before the ball circle so the middle is hidden behind the ball and flares out on both sides)
    if (b->type == TYPE_KNIGHT || b->type == TYPE_KING) {
        DrawFlutteringRoyalCape(b->pos, (float)b->radius, b->type == TYPE_KNIGHT);
    }

    DrawCircleV(b->pos, (float)b->radius, b->color);

    // Thin black outline for the ball body (drawn before protruding weapons/crowns so emblems sit naturally on top)
    DrawRing(b->pos, (float)b->radius - 1.5f, (float)b->radius + 0.5f, 0.0f, 360.0f, 36, BLACK);

    switch (b->type)
    {
        case TYPE_BURST:       DrawBurstCenterFlame(b->pos); break;
        case TYPE_HARPOON:     DrawHarpoonCenterEmblem(b, enemy); break;
        case TYPE_VERITY:      DrawVerityFace(b); break;
        case TYPE_DEVIL:       DrawDevilFangs(b, enemy); break;
        case TYPE_ICE:         DrawIce3DCube(b->pos); break;
        case TYPE_ELECTRIC:    DrawBolt(b->pos); break;
        case TYPE_WITCH:       DrawWitchHat(b); break;
        case TYPE_BOW:         DrawBowEmblem(b); DrawFlyingBowArrow(&b->bow); break;
        case TYPE_REFORGE:     DrawReforgeHammer(b->pos); break;
        case TYPE_NINJA:       DrawNinjaHeadbandAndFan(b); break;
        case TYPE_CELL:        DrawCellNucleus(b); break;
        case TYPE_LASER:       DrawLaserCore(b); break;
        case TYPE_CACTUS:      DrawCactusDetails(b); break;
        case TYPE_WIND:        DrawSwirlVortex(b->pos, (float)b->radius, (float)GetTime() * 5.0f); break;
        case TYPE_CHESS:       DrawChessBallPattern(b); break;
        case TYPE_GUN:         DrawGunGatling(b); break;
        case TYPE_DICE:        DrawDice3DEmblem(b); break;
        case TYPE_GAMBLER:     DrawGamblerDetails(b); break;
        case TYPE_PSYCHIC:     DrawPsychicDetails(b); break;
        case TYPE_CHARGE:      DrawChargeDetails(b); break;
        case TYPE_UNSHACKLED:  DrawUnshackledDetails(b); break;
        case TYPE_JUDGEMENT:   DrawJudgementDetails(b); break;
        case TYPE_TRAIN:       DrawTrainBallDetails(b); break;
        case TYPE_SPIDER:      DrawSpiderDetails(b); break;
        case TYPE_RANGE:       DrawRangeDetails(b); break;
        case TYPE_KNIGHT:      DrawKnightDetails(b); break;
        case TYPE_CAVALRY:     DrawCavalryDetails(b); break;
        case TYPE_EXECUTER:    DrawExecuterDetails(b); break;
        case TYPE_KING:        DrawKingDetails(b); break;
        case TYPE_CATAPULT:    DrawCatapultDetails(b); break;
        case TYPE_ARCHER:      DrawArcherDetails(b); DrawFlyingArcherFireArrow(&b->archer); break;
        case TYPE_SQUIRE:      DrawSquireDetails(b); break;
        case TYPE_REFORGE_SUB: {
            const char* lvTxt = TextFormat("Lv%d", b->reforgeLevel);
            int tw = MeasureText(lvTxt, 16);
            DrawText(lvTxt, (int)b->pos.x - tw / 2, (int)b->pos.y - 8, 16, WHITE);
        } break;
        case TYPE_TRIDENT: {
            DrawTridentCrown(b);
            const TridentData* t = &b->trident;
            if (t->state == T_READY) {
                Vector2 d = { cosf(t->aimAngle), sinf(t->aimAngle) };
                DrawTrident(V2Add(b->pos, V2Mul(d, b->radius + 6.0f)), t->aimAngle, YELLOW);
            } else if (t->state == T_RETURNING) {
                float a = atan2f(b->pos.y - t->pos.y, b->pos.x - t->pos.x);
                DrawTrident(t->pos, a, YELLOW);
            } else {
                DrawTrident(t->pos, t->aimAngle, YELLOW);
            }
        } break;
        default: break;
    }

    // Crimson pain flash overlay when hit by Executer's heavy axe
    if (b->painFlashTimer > 0.0f) {
        float pa = ClampF(b->painFlashTimer / 0.50f, 0.0f, 1.0f);
        DrawCircleV(b->pos, (float)b->radius + 2.0f, Fade(RED, 0.55f * pa));
        DrawRing(b->pos, (float)b->radius, (float)b->radius + 5.0f, 0.0f, 360.0f, 28, Fade(MAROON, 0.85f * pa));
    }

    // Golden immunity shield glow around King while dashing to flag
    if (IsKingDashing(b)) {
        DrawRing(b->pos, (float)b->radius + 2.0f, (float)b->radius + 6.0f, 0.0f, 360.0f, 32, GOLD);
    }

    if (b->frozen) {
        DrawCircleV(b->pos, (float)b->radius, Fade(SKYBLUE, 0.6f));
        DrawCircleLines((int)b->pos.x, (int)b->pos.y, (float)b->radius, WHITE);
    }

    if (b->type == TYPE_ELECTRIC && b->charged)
        DrawElectricRim(b->pos, (float)b->radius, 12, (Color){ 60, 140, 255, 255 });
    if (b->shocked) {
        DrawElectricRim(b->pos, (float)b->radius, 16, (Color){ 60, 140, 255, 255 });
        DrawCircleLines((int)b->pos.x, (int)b->pos.y, (float)b->radius, (Color){ 255, 240, 80, 255 });
    }

    // Archer Burn (thiêu đốt) flame overlay when afflicted with burnTicks > 0
    if (b->burnTicks > 0) {
        float flk = (float)GetTime() * 14.0f;
        DrawRing(b->pos, (float)b->radius, (float)b->radius + 4.0f, 0.0f, 360.0f, 28, Fade((Color){ 255, 95, 15, 255 }, 0.78f));
        for (int f = 0; f < 5; f++) {
            float ang = -PI * 0.5f + (f - 2) * 0.45f + sinf(flk + f * 1.4f) * 0.12f;
            Vector2 fBase = { b->pos.x + cosf(ang) * (b->radius - 2.0f), b->pos.y + sinf(ang) * (b->radius - 2.0f) };
            Vector2 fTip  = { fBase.x + cosf(ang) * 9.0f, fBase.y - 11.0f };
            DrawTri((Vector2){ fBase.x - 4.0f, fBase.y }, (Vector2){ fBase.x + 4.0f, fBase.y }, fTip,
                    (f & 1) ? GOLD : (Color){ 255, 85, 10, 255 });
        }
    }

    DrawWindStunOverlay(b);

    if (!b->isSubBall) {
        int extraTop = (b->type == TYPE_WITCH || b->type == TYPE_KING) ? 20
                     : ((b->type == TYPE_TRIDENT || b->type == TYPE_JUDGEMENT || b->type == TYPE_EXECUTER) ? 16 : 0);
        const char* hpText = TextFormat("%d HP", b->hp);
        Color hpCol = (b->team == 0) ? (Color){ 20, 90, 200, 255 } : (Color){ 200, 30, 30, 255 };
        DrawText(hpText, (int)b->pos.x - MeasureText(hpText, 15) / 2,
                 (int)b->pos.y - b->radius - 22 - extraTop, 15, hpCol);
    }
}

static void DrawEffectsLayer(void)
{
    // Psychic Black Holes (2.0s vortex pulling enemies toward center)
    for (int i = 0; i < BLACKHOLE_MAX; i++) {
        const BlackHole* bh = &g_blackHoles[i];
        if (!bh->active) continue;
        float r = PSYCHIC_BH_RADIUS;
        DrawCircleV(bh->pos, r, Fade((Color){ 45, 10, 80, 255 }, 0.22f));
        DrawCircleLines((int)bh->pos.x, (int)bh->pos.y, r, Fade((Color){ 195, 70, 255, 255 }, 0.65f));
        for (int arm = 0; arm < 4; arm++) {
            float baseA = bh->spinAngle + arm * (PI * 0.5f);
            Vector2 prev = bh->pos;
            for (int s = 1; s <= 10; s++) {
                float frac = (float)s / 10.0f;
                float a = baseA - frac * 2.8f;
                float rad = frac * (r * 0.9f);
                Vector2 cur = { bh->pos.x + cosf(a) * rad, bh->pos.y + sinf(a) * rad };
                Color col = (s < 5) ? BLACK : (Color){ 170, 45, 235, 255 };
                DrawLineEx(prev, cur, 3.2f * (1.1f - frac * 0.6f), Fade(col, 0.7f));
                prev = cur;
            }
        }
        DrawCircleV(bh->pos, 18.0f, BLACK);
        DrawRing(bh->pos, 17.0f, 21.0f, 0.0f, 360.0f, 28, (Color){ 220, 80, 255, 255 });
    }

    // Wall laser turrets & active laser beams
    for (int i = 0; i < TURRET_MAX; i++) {
        const LaserTurret* t = &g_turrets[i];
        if (!t->active) continue;

        if (t->beamTimer > 0.0f) {
            float a = t->beamTimer / TURRET_BEAM_TIME;
            DrawLineEx(t->pos, t->endPos, 7.0f * a, Fade(RED, 0.45f * a));
            DrawLineEx(t->pos, t->endPos, 2.8f, Fade((Color){ 255, 60, 60, 255 }, a));
        }

        Vector2 barrelEnd = V2Add(t->pos, V2Mul(t->dir, 12.0f));
        DrawLineEx(t->pos, barrelEnd, 5.0f, DARKGRAY);
        DrawCircleV(t->pos, 7.5f, GRAY);
        DrawCircleLines((int)t->pos.x, (int)t->pos.y, 7.5f, BLACK);
        DrawCircleV(t->pos, 3.0f, RED);
    }

    // Flying kunai
    for (int i = 0; i < KUNAI_MAX; i++) {
        if (!g_kunais[i].active) continue;
        DrawKunaiShape(g_kunais[i].pos, g_kunais[i].angle);
    }

    // Psychic flying tarot cards
    for (int i = 0; i < TAROT_MAX; i++) {
        const TarotCard* tc = &g_tarots[i];
        if (!tc->active) continue;
        Rectangle r = { tc->pos.x, tc->pos.y, 20.0f, 12.0f };
        DrawRectanglePro(r, (Vector2){ 10.0f, 6.0f }, tc->angle * RAD2DEG, BLACK);
        Rectangle rIn = { tc->pos.x, tc->pos.y, 17.0f, 9.0f };
        DrawRectanglePro(rIn, (Vector2){ 8.5f, 4.5f }, tc->angle * RAD2DEG, tc->cardCol);
        DrawCircleV(tc->pos, 2.2f, WHITE);
    }

    // Gun bullets (15% of normal ball radius)
    for (int i = 0; i < GUN_BULLET_MAX; i++) {
        const GunBullet* gb = &g_bullets[i];
        if (!gb->active) continue;
        DrawCircleV(gb->pos, GUN_BULLET_RAD, (Color){ 55, 58, 65, 255 });
        DrawCircleV(gb->pos, GUN_BULLET_RAD * 0.55f, GOLD);
        DrawCircleLines((int)gb->pos.x, (int)gb->pos.y, GUN_BULLET_RAD, BLACK);
    }

    // Cactus small bombs
    for (int i = 0; i < CACTUS_BOMB_MAX; i++) {
        const CactusBomb* cb = &g_cactusBombs[i];
        if (!cb->active) continue;
        DrawCactusOuterSpikes(cb->pos, (float)CACTUS_BOMB_RAD, 5.5f);
        DrawCircleV(cb->pos, (float)CACTUS_BOMB_RAD, COL_CACTUS);
        DrawCircleLines((int)cb->pos.x, (int)cb->pos.y, (float)CACTUS_BOMB_RAD, BLACK);
    }

    // Cactus red spikes
    for (int i = 0; i < CACTUS_SPIKE_MAX; i++) {
        const CactusSpike* sp = &g_cactusSpikes[i];
        if (!sp->active) continue;
        Vector2 d = { cosf(sp->angle), sinf(sp->angle) };
        Vector2 n = { -d.y, d.x };
        Vector2 tip  = V2Add(sp->pos, V2Mul(d, 7.0f));
        Vector2 base = V2Add(sp->pos, V2Mul(d, -5.0f));
        Vector2 b1   = V2Add(base, V2Mul(n, -3.5f));
        Vector2 b2   = V2Add(base, V2Mul(n,  3.5f));
        DrawTri(b1, b2, tip, RED);
        DrawLineEx(b1, tip, 1.2f, BLACK);
        DrawLineEx(b2, tip, 1.2f, BLACK);
        DrawLineEx(b1, b2,  1.2f, BLACK);
    }

    // Wind tornados
    for (int i = 0; i < TORNADO_MAX; i++) {
        const WindTornado* wt = &g_tornados[i];
        if (!wt->active) continue;
        DrawCircleV(wt->pos, TORNADO_RADIUS, Fade(COL_WIND, 0.75f));
        DrawSwirlVortex(wt->pos, TORNADO_RADIUS, wt->spinAngle);
        DrawCircleLines((int)wt->pos.x, (int)wt->pos.y, TORNADO_RADIUS, BLACK);
    }

    for (int i = 0; i < ZONE_MAX; i++) {
        const Zone* z = &g_zones[i];
        if (!z->active) continue;
        Color c = (z->kind == P_SLOW) ? YELLOW : COL_POISON;
        float a = 0.35f * (1.0f - 0.5f * z->age / ZONE_TIME);
        DrawCircleV(z->pos, z->radius, Fade(c, a));
        DrawCircleLines((int)z->pos.x, (int)z->pos.y, z->radius, Fade(c, 0.9f));
    }

    // Archer Burning Ground Zones (radius = 1.8x ball radius)
    for (int i = 0; i < BURN_ZONE_MAX; i++) {
        const BurnZone* bz = &g_burnZones[i];
        if (!bz->active) continue;
        float a = 0.38f * (1.0f - 0.45f * bz->age / ARCHER_ZONE_TIME);
        DrawCircleV(bz->pos, bz->radius, Fade((Color){ 255, 85, 10, 255 }, a));
        DrawCircleV(bz->pos, bz->radius * 0.65f, Fade(GOLD, a * 0.75f));
        DrawRing(bz->pos, bz->radius - 2.5f, bz->radius + 0.5f, 0.0f, 360.0f, 36, Fade((Color){ 255, 65, 0, 255 }, 0.92f));
    }

    for (int i = 0; i < POTION_MAX; i++) {
        const Potion* p = &g_potions[i];
        if (!p->active) continue;
        DrawRectangle((int)p->pos.x - 2, (int)p->pos.y - 13, 4, 6, DARKGRAY);
        DrawCircleV(p->pos, 8, PotionColor(p->kind));
        DrawCircleLines((int)p->pos.x, (int)p->pos.y, 8, BLACK);
        if (p->kind == P_HEAL) {
            DrawRectangle((int)p->pos.x - 1, (int)p->pos.y - 5, 2, 10, COL_PLUS);
            DrawRectangle((int)p->pos.x - 5, (int)p->pos.y - 1, 10, 2, COL_PLUS);
        }
    }

    for (int i = 0; i < FX_MAX; i++) {
        const BoomFx* f = &g_fx[i];
        if (!f->active) continue;
        float t = f->age / FX_TIME;
        DrawCircleV(f->pos, f->radius * (0.5f + 0.5f * t), Fade(f->col, 0.5f * (1.0f - t)));
        DrawCircleLines((int)f->pos.x, (int)f->pos.y, f->radius, Fade(f->col, 1.0f - t));
    }

    for (int i = 0; i < PARTICLE_MAX; i++) {
        const Particle* p = &g_particles[i];
        if (!p->active) continue;
        float a = 1.0f - p->age / p->life;
        if (p->plus) DrawText("+", (int)p->pos.x - 6, (int)p->pos.y - 12, 24, Fade(p->col, a));
        else         DrawCircleV(p->pos, p->size, Fade(p->col, a));
    }

    // Train rushing cars (up to 5 cars connected by short chains) & Shackless ultimates (Rainbow Laser & Triple Justice Swords)
    for (int i = 0; i < g_ballCount; i++) {
        const Ball* b = &g_balls[i];
        if (!IsCombatBall(b)) continue;

        if (b->type == TYPE_TRAIN && b->train.phase == 2) {
            const TrainData* tr = &b->train;
            // Draw short coupler chains between consecutive active cars first
            for (int c = 0; c < tr->numCars - 1; c++) {
                if (tr->carActive[c] && tr->carActive[c + 1]) {
                    DrawTrainCouplerChain(tr->carPos[c], tr->carAngle[c],
                                          tr->carPos[c + 1], tr->carAngle[c + 1]);
                }
            }
            // Draw carriages from rear to front so the lead locomotive renders on top
            for (int c = tr->numCars - 1; c >= 0; c--) {
                if (!tr->carActive[c]) continue;
                if (c == 0) DrawLocomotiveEngine(tr->carPos[0], tr->carAngle[0]);
                else        DrawTrainCarriage(tr->carPos[c], tr->carAngle[c], c);
            }
        }

        if (b->type == TYPE_UNSHACKLED) {
            const UnshackledData* u = &b->unshackled;
            // Active descending Garen sword (+20% size + 2 tilted translucent side swords) during Mode 3 or lingering impact (fxKind == 3)
            if (u->mode == 3) {
                float p = ClampF(u->timer / UNSHACKLED_SWORD_T, 0.0f, 1.0f);
                Vector2 fallPos = { u->targetPos.x, u->targetPos.y - (1.0f - p) * 165.0f };
                DrawJusticeTripleSwords(fallPos);
            } else if (u->fxTimer > 0.0f && u->fxKind == 3) {
                DrawJusticeTripleSwords(u->fxPos);
            }

            // Lux Final Spark infinite-range Rainbow Laser beam (width = 1.2x ball diameter)
            if (u->fxTimer > 0.0f && u->fxKind == 2) {
                static const Color rainbowCols[7] = {
                    { 255, 50, 50, 255 },
                    { 255, 155, 30, 255 },
                    { 255, 235, 50, 255 },
                    { 55, 225, 85, 255 },
                    { 55, 195, 255, 255 },
                    { 95, 85, 255, 255 },
                    { 215, 70, 255, 255 }
                };
                float a = ClampF(u->fxTimer / 0.45f, 0.0f, 1.0f);
                Vector2 dir = { cosf(u->fxAngle), sinf(u->fxAngle) };
                Vector2 n   = { -dir.y, dir.x };
                Vector2 end = V2Add(u->fxPos, V2Mul(dir, 1200.0f));
                float totalW = UNSHACKLED_LUX_W;
                float stripeW = totalW / 7.0f;
                for (int s = 0; s < 7; s++) {
                    float offset = ((float)s - 3.0f) * stripeW;
                    Vector2 s0 = V2Add(u->fxPos, V2Mul(n, offset));
                    Vector2 s1 = V2Add(end,      V2Mul(n, offset));
                    DrawLineEx(s0, s1, stripeW + 1.5f, Fade(rainbowCols[s], 0.88f * a));
                }
                DrawLineEx(u->fxPos, end, 8.0f, Fade(WHITE, 0.92f * a));
            }
        }

        // Catapult airborne stone boulder flying along a high parabolic arc (passes over all objects/walls)
        if (b->type == TYPE_CATAPULT && b->catapult.boulderActive) {
            float t = ClampF(b->catapult.boulderTimer / CATAPULT_FLIGHT_T, 0.0f, 1.0f);
            Vector2 groundPos = V2Add(b->catapult.boulderStart,
                                      V2Mul(V2Sub(b->catapult.boulderTarget, b->catapult.boulderStart), t));
            // Parabolic height offset: peaks at t = 0.5 (rising into the sky then plummeting down)
            float arcH = 4.0f * t * (1.0f - t) * CATAPULT_ARC_HEIGHT;
            Vector2 skyPos = { groundPos.x, groundPos.y - arcH };
            float scale = 1.0f + 0.55f * (4.0f * t * (1.0f - t));
            float rockR = 13.0f * scale;

            Color stoneLight = { 215, 222, 230, 255 };
            Color stoneBase  = { 185, 192, 202, 255 };
            Color stoneShade = { 128, 134, 144, 255 };

            DrawCircleV(skyPos, rockR + 2.0f, BLACK);
            DrawCircleV(skyPos, rockR, stoneBase);
            DrawCircleV((Vector2){ skyPos.x - rockR * 0.28f, skyPos.y - rockR * 0.28f }, rockR * 0.42f, stoneLight);
            DrawCircleV((Vector2){ skyPos.x + rockR * 0.28f, skyPos.y + rockR * 0.25f }, rockR * 0.32f, stoneShade);
            DrawCircleV((Vector2){ skyPos.x - rockR * 0.15f, skyPos.y + rockR * 0.32f }, rockR * 0.20f, stoneShade);
        }
    }

    // Prominent full-screen overlays for Dice roll, Red border flash, and Gambler 3-dice + Card draw
    if (g_diceRedFlashTimer > 0.0f) {
        float a = ClampF(g_diceRedFlashTimer / 0.45f, 0.0f, 1.0f);
        DrawRectangleLinesEx((Rectangle){ 0, 0, SCREEN_W, SCREEN_H }, 18.0f, Fade(RED, 0.75f * a));
        DrawRectangleLinesEx((Rectangle){ 14, 14, SCREEN_W - 28, SCREEN_H - 28 }, 8.0f, Fade(MAROON, 0.5f * a));
    }

    for (int i = 0; i < g_ballCount; i++) {
        const Ball* b = &g_balls[i];
        if (!IsCombatBall(b)) continue;

        // Dice prominent full-screen roll animation (1.0s: rotates 3 times then shows rolled pips)
        if (b->type == TYPE_DICE && b->dice.rolling) {
            Vector2 center = { SCREEN_W * 0.5f, SCREEN_H * 0.34f };
            float sz = 88.0f;
            float deg = b->dice.spinAngle * RAD2DEG;
            DrawCircleV(center, 66.0f, Fade(RED, 0.18f));
            DrawRectanglePro((Rectangle){ center.x, center.y, sz + 6.0f, sz + 6.0f },
                             (Vector2){ (sz + 6.0f) * 0.5f, (sz + 6.0f) * 0.5f }, deg, BLACK);
            DrawRectanglePro((Rectangle){ center.x, center.y, sz, sz },
                             (Vector2){ sz * 0.5f, sz * 0.5f }, deg, RAYWHITE);
            DrawDiePips2D(center, sz * 0.5f, b->dice.displayFace,
                          (b->dice.displayFace == 1 || b->dice.displayFace == 4) ? RED : BLACK);
            if (b->dice.animTimer >= 0.72f) {
                static const int pipDmg[7] = { 0, 5, 7, 9, 12, 15, 18 };
                const char* msg = TextFormat("DICE ROLLED %d! (-%d HP & LOCK)",
                                             b->dice.rolledVal, pipDmg[b->dice.rolledVal]);
                DrawCenteredText(msg, (int)(center.y + 58.0f), 20, RED);
            }
        }

        // Gambler prominent full-screen 3-dice roll (1.5s) and Playing Card draw (0.75s)
        if (b->type == TYPE_GAMBLER && b->gambler.phase > 0) {
            Vector2 center = { SCREEN_W * 0.5f, SCREEN_H * 0.68f };
            if (b->gambler.phase == 1) {
                DrawRectangle((int)center.x - 165, (int)center.y - 48, 330, 96, Fade(BLACK, 0.45f));
                for (int k = 0; k < 3; k++) {
                    float slotStart = k * 0.5f;
                    float localT = b->gambler.timer - slotStart;
                    Vector2 dPos = { center.x + (k - 1) * 95.0f, center.y };
                    float sz = 62.0f;
                    int face = b->gambler.diceVals[k];
                    float deg = 0.0f;
                    if (localT < 0.0f) {
                        face = 1;
                    } else if (localT < 0.36f) {
                        deg = (localT / 0.36f) * (3.0f * 360.0f);
                        face = (((int)(localT / 0.04f) + k) % 6) + 1;
                    }
                    DrawRectanglePro((Rectangle){ dPos.x, dPos.y, sz + 5.0f, sz + 5.0f },
                                     (Vector2){ (sz + 5.0f) * 0.5f, (sz + 5.0f) * 0.5f }, deg, GOLD);
                    DrawRectanglePro((Rectangle){ dPos.x, dPos.y, sz, sz },
                                     (Vector2){ sz * 0.5f, sz * 0.5f }, deg, RAYWHITE);
                    if (localT >= 0.0f) {
                        DrawDiePips2D(dPos, sz * 0.5f, face, (face == 1 || face == 4) ? RED : BLACK);
                    }
                }
                int sum = b->gambler.diceVals[0] + b->gambler.diceVals[1] + b->gambler.diceVals[2];
                if (b->gambler.timer > 1.32f) {
                    DrawCenteredText(TextFormat("GAMBLER DICE: %d x 3 = %d DMG NEXT HIT!", sum, sum * 3),
                                     (int)center.y + 54, 18, GOLD);
                }
            } else if (b->gambler.phase == 2) {
                static const char* const rankStr[14] = {
                    "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
                };
                int rk = b->gambler.cardRank;
                float t = ClampF(b->gambler.timer / 0.25f, 0.0f, 1.0f);
                float cw = 84.0f * (0.4f + 0.6f * t), ch = 118.0f * (0.4f + 0.6f * t);
                Rectangle cRect = { center.x - cw * 0.5f, center.y - ch * 0.5f, cw, ch };
                DrawRectangleRec(cRect, RAYWHITE);
                DrawRectangleLinesEx(cRect, 3.0f, GOLD);
                DrawRectangleLines((int)cRect.x, (int)cRect.y, (int)cw, (int)ch, BLACK);
                Color suitCol = (rk == 1 || rk >= 11) ? RED : BLACK;
                const char* rs = rankStr[rk];
                DrawText(rs, (int)cRect.x + 8, (int)cRect.y + 6, 22, suitCol);
                DrawText(rs, (int)(center.x - MeasureText(rs, 38) * 0.5f), (int)(center.y - 19), 38, suitCol);

                const char* buffDesc = "";
                if (rk == 1)      buffDesc = "CARD ACE: 0 DMG TAKEN + (33% ENEMY DMG + DICE x3)!";
                else if (rk >= 11)buffDesc = TextFormat("CARD %s: BLOCK 100%% NEXT HIT!", rs);
                else {
                    int pct = (int)roundf(10.0f + ((float)(rk - 2) / 8.0f) * 40.0f);
                    buffDesc = TextFormat("CARD %s: -%d%% INCOMING DMG!", rs, pct);
                }
                DrawCenteredText(buffDesc, (int)(center.y + 68.0f), 18, GOLD);
            }
        }
    }
}

static void DrawPopups(void)
{
    for (int i = 0; i < POPUP_MAX; i++) {
        const Popup* p = &g_popups[i];
        if (!p->active) continue;
        float t = p->age / POPUP_TIME;
        const char* s = TextFormat(p->heal ? "+%d HP" : "-%d HP", p->value);
        int w = MeasureText(s, 18);
        DrawText(s, (int)(p->pos.x - w * 0.5f), (int)(p->pos.y - 45.0f - p->age * 45.0f), 18,
                 Fade(p->heal ? GREEN : RED, 1.0f - t));
    }
}

// ======================= RARITY, GACHA, TICKETS & UPGRADE SYSTEM =======================
// Rarity tier assigned to each of the 33 balls:
// Common (Green):         BURST, ELECTRIC, WIND, CHARGE, WEB, EXECUTER, SQUIRE
// Uncommon (Sky Blue):    VERITY, DEVIL, ICE, NINJA, CELL, LASER, TRAIN, RANGE
// Rare (Yellow):          TRIDENT, WITCH, BOW, REFORGE, GUN, KNIGHT, CAVALRY, ARCHER
// Mythic (Bright Purple): CACTUS, DICE, GAMBLER, KING, CATAPULT
// Legendary (Red):        HARPOON, CHESS, PSYCHIC, SHACKLESS, JUDGEMENT
static const BallRarity g_ballRarity[TYPE_COUNT] = {
    RARITY_COMMON,      // TYPE_BURST
    RARITY_LEGENDARY,   // TYPE_HARPOON
    RARITY_UNCOMMON,    // TYPE_VERITY (moved to Uncommon)
    RARITY_UNCOMMON,    // TYPE_DEVIL
    RARITY_UNCOMMON,    // TYPE_ICE
    RARITY_RARE,        // TYPE_TRIDENT
    RARITY_COMMON,      // TYPE_ELECTRIC
    RARITY_RARE,        // TYPE_WITCH
    RARITY_RARE,        // TYPE_BOW
    RARITY_RARE,        // TYPE_REFORGE
    RARITY_UNCOMMON,    // TYPE_NINJA
    RARITY_UNCOMMON,    // TYPE_CELL
    RARITY_UNCOMMON,    // TYPE_LASER
    RARITY_MYTHIC,      // TYPE_CACTUS
    RARITY_COMMON,      // TYPE_WIND (moved to Common)
    RARITY_LEGENDARY,   // TYPE_CHESS
    RARITY_RARE,        // TYPE_GUN
    RARITY_MYTHIC,      // TYPE_DICE
    RARITY_MYTHIC,      // TYPE_GAMBLER
    RARITY_LEGENDARY,   // TYPE_PSYCHIC
    RARITY_COMMON,      // TYPE_CHARGE
    RARITY_LEGENDARY,   // TYPE_UNSHACKLED (SHACKLESS)
    RARITY_LEGENDARY,   // TYPE_JUDGEMENT
    RARITY_UNCOMMON,    // TYPE_TRAIN
    RARITY_COMMON,      // TYPE_SPIDER (WEB)
    RARITY_UNCOMMON,    // TYPE_RANGE
    RARITY_RARE,        // TYPE_KNIGHT
    RARITY_RARE,        // TYPE_CAVALRY
    RARITY_COMMON,      // TYPE_EXECUTER
    RARITY_MYTHIC,      // TYPE_KING
    RARITY_MYTHIC,      // TYPE_CATAPULT
    RARITY_RARE,        // TYPE_ARCHER
    RARITY_COMMON       // TYPE_SQUIRE
};

static const char* const g_rarityNames[RARITY_COUNT] = {
    "COMMON", "UNCOMMON", "RARE", "MYTHIC", "LEGENDARY"
};

static Color RarityColor(BallRarity r)
{
    switch (r) {
        case RARITY_COMMON:    return (Color){ 45, 195, 65, 255 };   // green
        case RARITY_UNCOMMON:  return (Color){ 90, 190, 255, 255 };  // lighter sky blue
        case RARITY_RARE:      return (Color){ 255, 215, 35, 255 };  // yellow
        case RARITY_MYTHIC:    return (Color){ 185, 45, 255, 255 };  // bright purple
        case RARITY_LEGENDARY: return (Color){ 255, 35, 25, 255 };   // bright legendary red
        default:               return LIGHTGRAY;
    }
}

static bool  g_unlocked[TYPE_COUNT];
static int   g_ballCopies[TYPE_COUNT];
static int   g_ballUpgradeLv[TYPE_COUNT];
static int   g_playerTickets = 15; // New player starter gift: 15 tickets
static int   g_botWinStreak[4] = { 0, 0, 0, 0 };
static int   g_botTotalWins = 0;   // Tracks first 15 VS BOT wins (+20 tickets each)
static int   g_pityRare = 0;       // 60 pulls -> next (61st) guaranteed Rare
static int   g_pityMythic = 0;     // 500 pulls -> next (501st) guaranteed Mythic
static int   g_pityLegend = 0;     // 1400 pulls -> next (1401st) guaranteed Legendary
static float g_online15mSec = 0.0f; // 900s -> +50 tickets
static float g_online1hSec = 0.0f;  // 3600s -> +300 tickets
static bool  g_challengerActive = false;
static int   g_challengerWins = 0;
static BotDifficulty g_challengerCurDiff = BOT_IMPOSSIBLE;

static void BuildSaveCandidatePaths(char paths[3][512], int* outCount)
{
    int count = 0;
    const char* appData = getenv("APPDATA");
    if (!appData || !appData[0]) appData = getenv("USERPROFILE");
    if (appData && appData[0]) {
        snprintf(paths[count++], 512, "%s/ballvball_save.cfg", appData);
    }
    const char* appDir = GetApplicationDirectory();
    if (appDir && appDir[0]) {
        snprintf(paths[count++], 512, "%sballvball_save.cfg", appDir);
    }
    snprintf(paths[count++], 512, "ballvball_save.cfg");
    *outCount = count;
}

static bool WriteProgressToFile(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "BALLVBALL_SAVE_V6\n");
    fprintf(f, "TICKETS %d\n", g_playerTickets);
    fprintf(f, "BOT_TOTAL_WINS %d\n", g_botTotalWins);
    fprintf(f, "BOT_STREAKS %d %d %d %d\n",
            g_botWinStreak[0], g_botWinStreak[1], g_botWinStreak[2], g_botWinStreak[3]);
    fprintf(f, "PITY %d %d %d\n", g_pityRare, g_pityMythic, g_pityLegend);
    fprintf(f, "ONLINE_TIMERS %.2f %.2f\n", g_online15mSec, g_online1hSec);
    fprintf(f, "CHALLENGER %d %d %d\n",
            g_challengerActive ? 1 : 0, g_challengerWins, (int)g_challengerCurDiff);
    fprintf(f, "BALLS %d\n", (int)TYPE_COUNT);
    for (int i = 0; i < TYPE_COUNT; i++) {
        fprintf(f, "%d %d %d %d\n",
                i, g_unlocked[i] ? 1 : 0, g_ballCopies[i], g_ballUpgradeLv[i]);
    }
    fflush(f);
    fclose(f);
    return true;
}

static bool ReadProgressFromFile(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char header[64] = { 0 };
    if (fscanf(f, "%63s", header) != 1) {
        fclose(f);
        return false;
    }

    if (strcmp(header, "BALLVBALL_SAVE_V6") == 0) {
        bool tempUnlocked[TYPE_COUNT];
        int  tempCopies[TYPE_COUNT];
        int  tempUpgrades[TYPE_COUNT];
        memset(tempUnlocked, 0, sizeof(tempUnlocked));
        memset(tempCopies, 0, sizeof(tempCopies));
        memset(tempUpgrades, 0, sizeof(tempUpgrades));
        tempUnlocked[TYPE_BURST] = true;
        tempCopies[TYPE_BURST] = 1;

        int tempTickets = 15;
        int tempBotWins = 0;
        int tempStreaks[4] = { 0, 0, 0, 0 };
        int tempPR = 0, tempPM = 0, tempPL = 0;
        float tempT15 = 0.0f, tempT1h = 0.0f;
        int tempChAct = 0, tempChWins = 0, tempChDiff = (int)BOT_IMPOSSIBLE;

        char key[64];
        while (fscanf(f, "%63s", key) == 1) {
            if (strcmp(key, "TICKETS") == 0) {
                fscanf(f, "%d", &tempTickets);
            } else if (strcmp(key, "BOT_TOTAL_WINS") == 0) {
                fscanf(f, "%d", &tempBotWins);
            } else if (strcmp(key, "BOT_STREAKS") == 0) {
                fscanf(f, "%d %d %d %d", &tempStreaks[0], &tempStreaks[1], &tempStreaks[2], &tempStreaks[3]);
            } else if (strcmp(key, "PITY") == 0) {
                fscanf(f, "%d %d %d", &tempPR, &tempPM, &tempPL);
            } else if (strcmp(key, "ONLINE_TIMERS") == 0) {
                fscanf(f, "%f %f", &tempT15, &tempT1h);
            } else if (strcmp(key, "CHALLENGER") == 0) {
                fscanf(f, "%d %d %d", &tempChAct, &tempChWins, &tempChDiff);
            } else if (strcmp(key, "BALLS") == 0) {
                int savedCount = 0;
                if (fscanf(f, "%d", &savedCount) == 1) {
                    for (int i = 0; i < savedCount; i++) {
                        int idx = -1, u = 0, cp = 0, lv = 0;
                        if (fscanf(f, "%d %d %d %d", &idx, &u, &cp, &lv) == 4) {
                            if (idx >= 0 && idx < TYPE_COUNT) {
                                tempUnlocked[idx] = (u != 0);
                                tempCopies[idx] = (cp >= 0) ? cp : 0;
                                tempUpgrades[idx] = (lv >= 0) ? lv : 0;
                            }
                        }
                    }
                }
            }
        }
        fclose(f);

        tempUnlocked[TYPE_BURST] = true;
        memcpy(g_unlocked, tempUnlocked, sizeof(g_unlocked));
        memcpy(g_ballCopies, tempCopies, sizeof(g_ballCopies));
        memcpy(g_ballUpgradeLv, tempUpgrades, sizeof(g_ballUpgradeLv));
        g_playerTickets = (tempTickets >= 0) ? tempTickets : 0;
        g_botTotalWins = (tempBotWins >= 0) ? tempBotWins : 0;
        for (int i = 0; i < 4; i++) g_botWinStreak[i] = (tempStreaks[i] >= 0) ? tempStreaks[i] : 0;
        g_pityRare = (tempPR >= 0) ? tempPR : 0;
        g_pityMythic = (tempPM >= 0) ? tempPM : 0;
        g_pityLegend = (tempPL >= 0) ? tempPL : 0;
        g_online15mSec = ClampF(tempT15, 0.0f, 900.0f);
        g_online1hSec = ClampF(tempT1h, 0.0f, 3600.0f);
        g_challengerActive = (tempChAct != 0);
        g_challengerWins = (tempChWins >= 0 && tempChWins < 5) ? tempChWins : 0;
        g_challengerCurDiff = (tempChDiff == (int)BOT_HARD) ? BOT_HARD : BOT_IMPOSSIBLE;
        return true;
    }

    if (strcmp(header, "BALLVBALL_SAVE_V5") == 0) {
        bool tempUnlocked[TYPE_COUNT];
        memset(tempUnlocked, 0, sizeof(tempUnlocked));
        tempUnlocked[TYPE_BURST] = true;
        char key[64];
        while (fscanf(f, "%63s", key) == 1) {
            if (strcmp(key, "UNLOCKED") == 0) {
                int savedCount = 0;
                if (fscanf(f, "%d", &savedCount) == 1) {
                    for (int i = 0; i < savedCount; i++) {
                        int idx = -1, val = 0;
                        if (fscanf(f, "%d %d", &idx, &val) == 2 && idx >= 0 && idx < TYPE_COUNT) {
                            tempUnlocked[idx] = (val != 0);
                        }
                    }
                }
            }
        }
        fclose(f);
        tempUnlocked[TYPE_BURST] = true;
        memcpy(g_unlocked, tempUnlocked, sizeof(g_unlocked));
        for (int i = 0; i < TYPE_COUNT; i++) {
            g_ballCopies[i] = g_unlocked[i] ? 1 : 0;
            g_ballUpgradeLv[i] = 0;
        }
        g_playerTickets = 15;
        return true;
    }

    fclose(f);
    return false;
}

static void SaveProgress(void)
{
    g_unlocked[TYPE_BURST] = true;
    char paths[3][512];
    int pathCount = 0;
    BuildSaveCandidatePaths(paths, &pathCount);
    for (int i = 0; i < pathCount; i++) {
        WriteProgressToFile(paths[i]);
    }
}

static void LoadProgress(void)
{
    char paths[3][512];
    int pathCount = 0;
    BuildSaveCandidatePaths(paths, &pathCount);

    for (int i = 0; i < pathCount; i++) {
        if (ReadProgressFromFile(paths[i])) {
            SaveProgress();
            return;
        }
    }

    // Default new-player state: ONLY BURST unlocked + 15 starter tickets
    memset(g_unlocked, 0, sizeof(g_unlocked));
    memset(g_ballCopies, 0, sizeof(g_ballCopies));
    memset(g_ballUpgradeLv, 0, sizeof(g_ballUpgradeLv));
    g_unlocked[TYPE_BURST] = true;
    g_ballCopies[TYPE_BURST] = 1;
    g_playerTickets = 15;
    memset(g_botWinStreak, 0, sizeof(g_botWinStreak));
    g_botTotalWins = 0;
    g_pityRare = 0;
    g_pityMythic = 0;
    g_pityLegend = 0;
    g_online15mSec = 0.0f;
    g_online1hSec = 0.0f;
    g_challengerActive = false;
    g_challengerWins = 0;
    SaveProgress();
}

// Rolls a Bot ball using exact difficulty rarity rates (Common, Uncommon, Rare, Mythic, Legendary)
static BallType RollBotBallForDifficulty(BotDifficulty diff)
{
    static const int diffRates[4][RARITY_COUNT] = {
        { 30, 25, 20, 17,  8 }, // EASY:       L 8  - M 17 - R 20 - U 25 - C 30
        { 18, 22, 30, 20, 10 }, // MEDIUM:     L 10 - M 20 - R 30 - U 22 - C 18
        { 13, 20, 33, 22, 12 }, // HARD:       L 12 - M 22 - R 33 - U 20 - C 13
        { 13, 20, 33, 22, 12 }  // IMPOSSIBLE: same as HARD
    };
    int dIdx = (diff >= 0 && diff <= BOT_IMPOSSIBLE) ? (int)diff : 0;
    int roll = GetRandomValue(1, 100);
    int accum = 0;
    BallRarity chosenRarity = RARITY_COMMON;
    for (int r = 0; r < RARITY_COUNT; r++) {
        accum += diffRates[dIdx][r];
        if (roll <= accum || r == RARITY_COUNT - 1) {
            chosenRarity = (BallRarity)r;
            break;
        }
    }

    BallType pool[TYPE_COUNT];
    int poolCount = 0;
    for (int i = 0; i < TYPE_COUNT; i++) {
        if (g_ballRarity[i] == chosenRarity) {
            pool[poolCount++] = (BallType)i;
        }
    }
    if (poolCount == 0) return TYPE_BURST;
    return pool[GetRandomValue(0, poolCount - 1)];
}

static int CalcWinStreakBonusPct(int streakBeforeWin)
{
    if (streakBeforeWin >= 20) return 105; // 21st+ win
    if (streakBeforeWin >= 15) return 75;  // 16th..20th win
    if (streakBeforeWin >= 10) return 50;  // 11th..15th win
    if (streakBeforeWin >= 5)  return 25;  // 6th..10th win
    if (streakBeforeWin >= 3)  return 10;  // 4th..5th win
    return 0;
}

static void ApplyMatchBotAndUpgradeStats(Ball* b, bool isBotBall, BotDifficulty botDiff)
{
    if (!b) return;
    if (!isBotBall) {
        int lv = g_ballUpgradeLv[b->type];
        if (lv > 0) {
            b->hp += lv * 5;
            b->maxHp += lv * 5;
            b->dmgMult *= (1.0f + 0.025f * (float)lv);
        }
    } else {
        if (botDiff == BOT_HARD) {
            b->hp = 110;
            b->maxHp = 110;
            b->dmgMult *= 1.15f;
            b->homingBonus += 0.05f;
        } else if (botDiff == BOT_IMPOSSIBLE) {
            b->hp = 120;
            b->maxHp = 120;
            b->dmgMult *= 1.25f;
            b->homingBonus += 0.10f;
            b->dodgeChance = 0.05f;
            b->dmgRedChance = 0.10f;
        }
    }
}

static inline bool IsBallAvailable(BallType t, bool sandboxAllUnlocked)
{
    return sandboxAllUnlocked || g_unlocked[t];
}

// When the player has 4 or more available balls and enters VS BOT / Challenger mode,
// randomly draw 3 distinct balls from their available collection so they pick 1 of the 3.
static bool RollVsBotPlayerDraft(BallType outDraft[3])
{
    BallType pool[TYPE_COUNT];
    int poolCount = 0;
    for (int i = 0; i < TYPE_COUNT; i++) {
        if (g_unlocked[i]) {
            pool[poolCount++] = (BallType)i;
        }
    }
    if (poolCount < 4) return false;

    for (int i = 0; i < 3; i++) {
        int r = GetRandomValue(i, poolCount - 1);
        BallType tmp = pool[i];
        pool[i] = pool[r];
        pool[r] = tmp;
        outDraft[i] = pool[i];
    }
    return true;
}

// Gacha Pool:
// Total rarity rates: Common 55%, Uncommon 29%, Rare 10%, Mythic 5%, Legendary 1%
// Divided equally among all balls in that rarity.
// Pity: 60 pulls -> 61st Rare; 500 pulls -> 501st Mythic; 1400 pulls -> 1401st Legendary.
static BallType RollGachaBall(void)
{
    BallRarity chosenRarity = RARITY_COMMON;

    if (g_pityLegend >= 1400) {
        chosenRarity = RARITY_LEGENDARY;
    } else if (g_pityMythic >= 500) {
        chosenRarity = RARITY_MYTHIC;
    } else if (g_pityRare >= 60) {
        chosenRarity = RARITY_RARE;
    } else {
        int roll = GetRandomValue(1, 10000);
        if (roll <= 5500)      chosenRarity = RARITY_COMMON;    // 55.00%
        else if (roll <= 8400) chosenRarity = RARITY_UNCOMMON;  // 29.00%
        else if (roll <= 9400) chosenRarity = RARITY_RARE;      // 10.00%
        else if (roll <= 9900) chosenRarity = RARITY_MYTHIC;    //  5.00%
        else                   chosenRarity = RARITY_LEGENDARY; //  1.00%
    }

    if (chosenRarity == RARITY_LEGENDARY) {
        g_pityLegend = 0; g_pityMythic = 0; g_pityRare = 0;
    } else if (chosenRarity == RARITY_MYTHIC) {
        g_pityLegend++; g_pityMythic = 0; g_pityRare = 0;
    } else if (chosenRarity == RARITY_RARE) {
        g_pityLegend++; g_pityMythic++; g_pityRare = 0;
    } else {
        g_pityLegend++; g_pityMythic++; g_pityRare++;
    }

    BallType pool[TYPE_COUNT];
    int poolCount = 0;
    for (int i = 0; i < TYPE_COUNT; i++) {
        if (g_ballRarity[i] == chosenRarity) {
            pool[poolCount++] = (BallType)i;
        }
    }
    if (poolCount == 0) return TYPE_BURST;
    return pool[GetRandomValue(0, poolCount - 1)];
}

// Renders a faithful visual preview of any ball type inside UI cards (Select Menu & Gacha Machine)
static void DrawBallPreviewIcon(BallType type, Vector2 center, Color baseCol)
{
    Ball dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.active = true;
    dummy.type = type;
    dummy.pos = center;
    dummy.radius = BASE_RADIUS;
    dummy.color = baseCol;
    dummy.verityForm = 1;
    dummy.harpoon.state = H_READY;
    dummy.trident.state = T_READY;
    dummy.trident.aimAngle = -0.35f;
    dummy.bow.active = false;
    dummy.bow.aimAngle = 0.0f;
    dummy.ninja.currentKunai = 3;
    dummy.ninja.targetMaxKunai = 3;
    dummy.ninja.aimAngle = -PI * 0.5f;
    dummy.cactus.ammo = CACTUS_MAX_AMMO;
    dummy.gun.aimAngle = 0.0f;
    dummy.gun.barrelSpin = (float)GetTime() * 2.5f;
    dummy.dice.rolling = false;

    if (type == TYPE_ICE) {
        DrawCircleV(center, BASE_RADIUS + 5.0f, Fade(SKYBLUE, 0.22f));
    }
    if (type == TYPE_CACTUS) {
        DrawCactusOuterSpikes(center, (float)BASE_RADIUS, 7.5f);
    }
    if (type == TYPE_KNIGHT || type == TYPE_KING) {
        DrawFlutteringRoyalCape(center, (float)BASE_RADIUS, type == TYPE_KNIGHT);
    }

    DrawCircleV(center, (float)BASE_RADIUS, baseCol);
    DrawRing(center, (float)BASE_RADIUS - 1.5f, (float)BASE_RADIUS + 0.5f, 0.0f, 360.0f, 36, BLACK);

    switch (type) {
        case TYPE_BURST:      DrawBurstCenterFlame(center); break;
        case TYPE_HARPOON:    DrawHarpoonCenterEmblem(&dummy, NULL); break;
        case TYPE_VERITY:     DrawVerityFace(&dummy); break;
        case TYPE_DEVIL:      DrawDevilFangs(&dummy, NULL); break;
        case TYPE_ICE:        DrawIce3DCube(center); break;
        case TYPE_TRIDENT:    DrawTridentCrown(&dummy); break;
        case TYPE_ELECTRIC:   DrawBolt(center); break;
        case TYPE_WITCH:      DrawWitchHat(&dummy); break;
        case TYPE_BOW:        DrawBowEmblem(&dummy); break;
        case TYPE_REFORGE:    DrawReforgeHammer(center); break;
        case TYPE_NINJA:      DrawNinjaHeadbandAndFan(&dummy); break;
        case TYPE_CELL:       DrawCellNucleus(&dummy); break;
        case TYPE_LASER:      DrawLaserCore(&dummy); break;
        case TYPE_CACTUS:     DrawCactusDetails(&dummy); break;
        case TYPE_WIND:       DrawSwirlVortex(center, (float)BASE_RADIUS, (float)GetTime() * 3.5f); break;
        case TYPE_CHESS:      DrawChessBallPattern(&dummy); break;
        case TYPE_GUN:        DrawGunGatling(&dummy); break;
        case TYPE_DICE:       DrawDice3DEmblem(&dummy); break;
        case TYPE_GAMBLER:    DrawGamblerDetails(&dummy); break;
        case TYPE_PSYCHIC:    DrawPsychicDetails(&dummy); break;
        case TYPE_CHARGE:     DrawChargeDetails(&dummy); break;
        case TYPE_UNSHACKLED: DrawUnshackledDetails(&dummy); break;
        case TYPE_JUDGEMENT:  DrawJudgementDetails(&dummy); break;
        case TYPE_TRAIN:      DrawTrainBallDetails(&dummy); break;
        case TYPE_SPIDER:     DrawSpiderDetails(&dummy); break;
        case TYPE_RANGE:
            DrawCircleLines((int)center.x, (int)center.y, BASE_RADIUS * RANGE_INIT_MULT, (Color){ 245, 190, 15, 255 });
            DrawRangeDetails(&dummy);
            break;
        case TYPE_KNIGHT:     DrawKnightDetails(&dummy); break;
        case TYPE_CAVALRY:    DrawCavalryDetails(&dummy); break;
        case TYPE_EXECUTER:   DrawExecuterDetails(&dummy); break;
        case TYPE_KING:       DrawKingDetails(&dummy); break;
        case TYPE_CATAPULT:   DrawCatapultDetails(&dummy); break;
        case TYPE_ARCHER:     DrawArcherDetails(&dummy); break;
        case TYPE_SQUIRE:     DrawSquireDetails(&dummy); break;
        default: break;
    }
}

// ======================= MAIN =======================
int main(void)
{
    // Automatically close any existing BallvBall window before launching the new one
    ClosePreviousInstanceIfRunning(WINDOW_TITLE);

    InitWindow(SCREEN_W, SCREEN_H, WINDOW_TITLE);
    SetRandomSeed((unsigned int)time(NULL));
    LoadProgress();

    GameState state = MENU_MODE;
    bool is2v2 = false;
    bool isVsBot = false;
    bool isChallenger = false;
    BotDifficulty selectedBotDiff = BOT_EASY;
    InventoryTab invTab = INV_TAB_INVENTORY;
    bool vsBotDraftActive = false;
    BallType vsBotDraft[3] = { TYPE_BURST, TYPE_BURST, TYPE_BURST };
    bool isPaused = false;
    bool surrenderConfirm = false;
    int selectStep = 0;
    int totalSlots = 2;
    BallType pickedTypes[4] = { 0 };
    float countdownTimer = 3.0f;
    float matchTimer = 0.0f;
    int winningTeam = -1;
    bool matchRewardGiven = false;
    int lastEarnedTickets = 0;
    int lastStreakBonusPct = 0;
    bool lastFirst15Bonus = false;

    // Gacha wheel animation state
    bool gachaRolling = false;
    float gachaTimer = 0.0f;
    float gachaStepTimer = 0.0f;
    BallType gachaPreview = TYPE_BURST;
    BallType gachaResult  = TYPE_BURST;
    bool gachaHasResult   = false;
    bool gachaWasNew      = false;

    static const char* const names[TYPE_COUNT] = {
        "BURST", "HARPOON", "VERITY", "DEVIL", "ICE",
        "TRIDENT", "ELECTRIC", "WITCH", "BOW", "REFORGE",
        "NINJA", "CELL", "LASER", "CACTUS", "WIND",
        "CHESS", "GUN", "DICE", "GAMBLER", "PSYCHIC",
        "CHARGE", "SHACKLESS", "JUDGEMENT", "TRAIN",
        "WEB", "RANGE", "KNIGHT", "CAVALRY", "EXECUTER", "KING",
        "CATAPULT", "ARCHER", "SQUIRE"
    };
    static const Color btnColors[TYPE_COUNT] = {
        ORANGE, SKYBLUE, YELLOW, COL_DEVIL, COL_ICE,
        COL_TRIDENT, COL_ELECTRIC, COL_WITCH, COL_BOW, COL_REFORGE,
        COL_NINJA, COL_CELL, COL_LASER, COL_CACTUS, COL_WIND,
        COL_CHESS, COL_GUN, COL_DICE, COL_GAMBLER, COL_PSYCHIC,
        COL_CHARGE, COL_UNSHACKLED, COL_JUDGEMENT, COL_TRAIN,
        COL_SPIDER, COL_RANGE, COL_KNIGHT, COL_CAVALRY, COL_EXECUTER, COL_KING,
        COL_CATAPULT, COL_ARCHER, COL_SQUIRE
    };

    // Sort all 20 balls in ascending order of rarity (COMMON -> UNCOMMON -> RARE -> MYSTIC -> LEGENDARY)
    BallType sortedBalls[TYPE_COUNT];
    int sortedCount = 0;
    for (int r = 0; r < RARITY_COUNT; r++) {
        for (int i = 0; i < TYPE_COUNT; i++) {
            if (g_ballRarity[i] == (BallRarity)r) {
                sortedBalls[sortedCount++] = (BallType)i;
            }
        }
    }

    // Scrollable 3-per-row square card layout for MENU_SELECT
    const int selectCols = 3;
    const int selectRows = (TYPE_COUNT + selectCols - 1) / selectCols;
    const float cardSize = 152.0f;
    const float cardGapX = 36.0f;
    const float cardGapY = 18.0f;
    const float gridStartX = (SCREEN_W - (selectCols * cardSize + (selectCols - 1) * cardGapX)) * 0.5f;
    const Rectangle selectViewport = { 45.0f, 92.0f, SCREEN_W - 90.0f, 344.0f };
    const float totalGridH = selectRows * cardSize + (selectRows - 1) * cardGapY + 16.0f;
    const float maxSelectScroll = fmaxf(0.0f, totalGridH - selectViewport.height);
    float selectScrollY = 0.0f;
    float invScrollY = 0.0f;
    bool draggingScrollbar = false;

    const Rectangle btnModeSandbox   = { SCREEN_W / 2.0f - 255.0f, 142.0f, 155.0f, 56.0f };
    const Rectangle btnModeVsBot     = { SCREEN_W / 2.0f - 77.5f,  142.0f, 155.0f, 56.0f };
    const Rectangle btnModeChall     = { SCREEN_W / 2.0f + 100.0f, 142.0f, 155.0f, 56.0f };
    const Rectangle btnOpenGacha     = { SCREEN_W / 2.0f - 170.0f, 218.0f, 155.0f, 56.0f };
    const Rectangle btnOpenInv       = { SCREEN_W / 2.0f + 15.0f,  218.0f, 155.0f, 56.0f };
    const Rectangle btnSandbox1v1    = { SCREEN_W / 2.0f - 185.0f, 200.0f, 170.0f, 64.0f };
    const Rectangle btnSandbox2v2    = { SCREEN_W / 2.0f + 15.0f,  200.0f, 170.0f, 64.0f };
    const Rectangle btnBot1v1        = { SCREEN_W / 2.0f - 185.0f, 325.0f, 170.0f, 56.0f };
    const Rectangle btnBot2v2        = { SCREEN_W / 2.0f + 15.0f,  325.0f, 170.0f, 56.0f };
    const Rectangle btnChallAction   = { SCREEN_W / 2.0f - 140.0f, 285.0f, 280.0f, 58.0f };
    const Rectangle btnRollNow       = { SCREEN_W / 2.0f - 100.0f, 345.0f, 200.0f, 52.0f };
    const Rectangle btnTopRight      = { SCREEN_W - 115, 10, 100, 30 };
    const Rectangle btnPause         = { SCREEN_W - 225, 10, 100, 30 };
    const Rectangle btnSurrYes       = { SCREEN_W / 2.0f - 120.0f, SCREEN_H / 2.0f + 12.0f, 105.0f, 40.0f };
    const Rectangle btnSurrNo        = { SCREEN_W / 2.0f + 15.0f,  SCREEN_H / 2.0f + 12.0f, 105.0f, 40.0f };

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        // Online reward timers (15m -> +50 tickets, 1h -> +300 tickets)
        g_online15mSec += dt;
        g_online1hSec += dt;
        bool onlineRewardTriggered = false;
        while (g_online15mSec >= 900.0f) {
            g_online15mSec -= 900.0f;
            g_playerTickets += 50;
            onlineRewardTriggered = true;
        }
        while (g_online1hSec >= 3600.0f) {
            g_online1hSec -= 3600.0f;
            g_playerTickets += 300;
            onlineRewardTriggered = true;
        }
        if (onlineRewardTriggered) SaveProgress();

        bool inBotMatch = (state == COUNTDOWN || state == PLAYING) && (isVsBot || isChallenger);

        // Surrender confirmation modal handling during bot matches
        if (surrenderConfirm && inBotMatch) {
            if (clicked) {
                if (CheckCollisionPointRec(mousePos, btnSurrYes)) {
                    surrenderConfirm = false;
                    isPaused = false;
                    winningTeam = 1;
                    matchRewardGiven = true;
                    lastEarnedTickets = 0;
                    lastStreakBonusPct = 0;
                    lastFirst15Bonus = false;
                    if (isChallenger) {
                        g_challengerActive = false;
                        g_challengerWins = 0;
                    } else if (isVsBot) {
                        static const int loseBase[4] = { 2, 3, 0, 0 };
                        int dIdx = (selectedBotDiff >= 0 && selectedBotDiff <= BOT_IMPOSSIBLE) ? (int)selectedBotDiff : 0;
                        g_botWinStreak[dIdx] = 0;
                        lastEarnedTickets = loseBase[dIdx];
                        g_playerTickets += lastEarnedTickets;
                    }
                    SaveProgress();
                    state = GAME_OVER;
                } else if (CheckCollisionPointRec(mousePos, btnSurrNo)) {
                    surrenderConfirm = false;
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) surrenderConfirm = false;
        } else {
            // Top-right button: SURRENDER in bot matches, RESET in sandbox, RETURN in menus/game over
            if (state != MENU_MODE &&
                ((clicked && CheckCollisionPointRec(mousePos, btnTopRight)) || (!inBotMatch && IsKeyPressed(KEY_R)))) {
                if (inBotMatch) {
                    surrenderConfirm = true;
                } else {
                    state = MENU_MODE;
                    selectStep = 0;
                    isPaused = false;
                    surrenderConfirm = false;
                    gachaRolling = false;
                    ClearEffects();
                    continue;
                }
            }

            if ((state == COUNTDOWN || state == PLAYING) &&
                ((clicked && CheckCollisionPointRec(mousePos, btnPause)) || IsKeyPressed(KEY_P))) {
                isPaused = !isPaused;
            }
        }

        // ======================= UPDATE =======================
        if (state == MENU_MODE)
        {
            if (clicked) {
                if (CheckCollisionPointRec(mousePos, btnModeSandbox)) {
                    isVsBot = false;
                    isChallenger = false;
                    vsBotDraftActive = false;
                    state = MENU_SANDBOX;
                } else if (CheckCollisionPointRec(mousePos, btnModeVsBot)) {
                    isChallenger = false;
                    state = MENU_BOT_MODE;
                } else if (CheckCollisionPointRec(mousePos, btnModeChall)) {
                    state = MENU_CHALLENGER;
                } else if (CheckCollisionPointRec(mousePos, btnOpenGacha)) {
                    gachaRolling = false;
                    gachaHasResult = false;
                    state = MENU_GACHA;
                } else if (CheckCollisionPointRec(mousePos, btnOpenInv)) {
                    invScrollY = 0.0f;
                    state = MENU_INVENTORY;
                }
            }
        }
        else if (state == MENU_SANDBOX)
        {
            if (clicked) {
                if (CheckCollisionPointRec(mousePos, btnSandbox1v1)) {
                    is2v2 = false;
                    isVsBot = false;
                    isChallenger = false;
                    vsBotDraftActive = false;
                    totalSlots = 2;
                    selectStep = 0;
                    state = MENU_SELECT;
                } else if (CheckCollisionPointRec(mousePos, btnSandbox2v2)) {
                    is2v2 = true;
                    isVsBot = false;
                    isChallenger = false;
                    vsBotDraftActive = false;
                    totalSlots = 4;
                    selectStep = 0;
                    state = MENU_SELECT;
                }
            }
        }
        else if (state == MENU_BOT_MODE)
        {
            if (clicked) {
                for (int d = 0; d < 4; d++) {
                    Rectangle diffCard = { 34.0f + d * 186.0f, 118.0f, 174.0f, 172.0f };
                    if (CheckCollisionPointRec(mousePos, diffCard)) {
                        selectedBotDiff = (BotDifficulty)d;
                    }
                }
                if (CheckCollisionPointRec(mousePos, btnBot1v1)) {
                    is2v2 = false;
                    isVsBot = true;
                    isChallenger = false;
                    vsBotDraftActive = RollVsBotPlayerDraft(vsBotDraft);
                    totalSlots = 2;
                    selectStep = 0;
                    state = MENU_SELECT;
                } else if (CheckCollisionPointRec(mousePos, btnBot2v2)) {
                    is2v2 = true;
                    isVsBot = true;
                    isChallenger = false;
                    vsBotDraftActive = RollVsBotPlayerDraft(vsBotDraft);
                    totalSlots = 4;
                    selectStep = 0;
                    state = MENU_SELECT;
                }
            }
        }
        else if (state == MENU_CHALLENGER)
        {
            if (clicked && CheckCollisionPointRec(mousePos, btnChallAction)) {
                if (!g_challengerActive) {
                    if (g_playerTickets >= 80) {
                        g_playerTickets -= 80;
                        g_challengerActive = true;
                        g_challengerWins = 0;
                        g_challengerCurDiff = (GetRandomValue(1, 100) <= 75) ? BOT_IMPOSSIBLE : BOT_HARD;
                        SaveProgress();
                    }
                } else {
                    is2v2 = false;
                    isVsBot = true;
                    isChallenger = true;
                    selectedBotDiff = g_challengerCurDiff;
                    vsBotDraftActive = RollVsBotPlayerDraft(vsBotDraft);
                    totalSlots = 2;
                    selectStep = 0;
                    state = MENU_SELECT;
                }
            }
        }
        else if (state == MENU_INVENTORY)
        {
            if (clicked) {
                for (int t = 0; t < 4; t++) {
                    Rectangle tabBtn = { 110.0f + t * 148.0f, 48.0f, 136.0f, 34.0f };
                    if (CheckCollisionPointRec(mousePos, tabBtn)) {
                        invTab = (InventoryTab)t;
                        invScrollY = 0.0f;
                    }
                }
            }
            if (invTab == INV_TAB_INVENTORY || invTab == INV_TAB_UPGRADE) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) invScrollY -= wheel * 48.0f;
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) invScrollY += 320.0f * dt;
                if (IsKeyDown(KEY_UP)   || IsKeyDown(KEY_W)) invScrollY -= 320.0f * dt;
                invScrollY = ClampF(invScrollY, 0.0f, maxSelectScroll);

                if (invTab == INV_TAB_UPGRADE && clicked && CheckCollisionPointRec(mousePos, selectViewport)) {
                    for (int idx = 0; idx < TYPE_COUNT; idx++) {
                        int row = idx / selectCols;
                        int col = idx % selectCols;
                        Rectangle cardRect = {
                            gridStartX + col * (cardSize + cardGapX),
                            selectViewport.y + 8.0f + row * (cardSize + cardGapY) - invScrollY,
                            cardSize, cardSize
                        };
                        if (cardRect.y + cardRect.height < selectViewport.y || cardRect.y > selectViewport.y + selectViewport.height) continue;
                        BallType bType = sortedBalls[idx];
                        if (g_unlocked[bType] && g_ballCopies[bType] >= 5 && CheckCollisionPointRec(mousePos, cardRect)) {
                            g_ballCopies[bType] -= 5;
                            g_ballUpgradeLv[bType]++;
                            SaveProgress();
                            break;
                        }
                    }
                }
            }
        }
        else if (state == MENU_GACHA)
        {
            if (gachaRolling) {
                gachaTimer -= dt;
                gachaStepTimer -= dt;
                if (gachaStepTimer <= 0.0f) {
                    float progress = 1.0f - ClampF(gachaTimer / 1.8f, 0.0f, 1.0f);
                    gachaStepTimer = 0.04f + progress * 0.16f;
                    gachaPreview = (BallType)((gachaPreview + GetRandomValue(1, TYPE_COUNT - 1)) % TYPE_COUNT);
                }
                if (gachaTimer <= 0.0f) {
                    gachaRolling = false;
                    gachaHasResult = true;
                    gachaPreview = gachaResult;
                }
            } else if (clicked && CheckCollisionPointRec(mousePos, btnRollNow)) {
                if (g_playerTickets > 0) {
                    g_playerTickets--;
                    gachaResult = RollGachaBall();
                    gachaWasNew = !g_unlocked[gachaResult];
                    g_unlocked[gachaResult] = true;
                    g_ballCopies[gachaResult]++;
                    SaveProgress();
                    gachaRolling = true;
                    gachaHasResult = false;
                    gachaTimer = 1.8f;
                    gachaStepTimer = 0.04f;
                }
            }
        }
        else if (state == MENU_SELECT)
        {
            bool sandboxAllUnlocked = (!isVsBot && !isChallenger);
            if (!isVsBot || !vsBotDraftActive) {
                // Mouse wheel + Up/Down arrow keys + scrollbar dragging for 3-per-row square grid
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    selectScrollY -= wheel * 48.0f;
                }
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) selectScrollY += 320.0f * dt;
                if (IsKeyDown(KEY_UP)   || IsKeyDown(KEY_W)) selectScrollY -= 320.0f * dt;

                Rectangle scrollTrack = { selectViewport.x + selectViewport.width + 6.0f, selectViewport.y, 12.0f, selectViewport.height };
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    (draggingScrollbar || CheckCollisionPointRec(mousePos, scrollTrack))) {
                    draggingScrollbar = true;
                    float ratio = ClampF((mousePos.y - scrollTrack.y) / scrollTrack.height, 0.0f, 1.0f);
                    selectScrollY = ratio * maxSelectScroll;
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    draggingScrollbar = false;
                }
                selectScrollY = ClampF(selectScrollY, 0.0f, maxSelectScroll);
            }

            if (clicked && CheckCollisionPointRec(mousePos, selectViewport)) {
                int itemCount = (isVsBot && vsBotDraftActive) ? 3 : TYPE_COUNT;
                float draftCardY = selectViewport.y + (selectViewport.height - cardSize) * 0.5f;

                for (int idx = 0; idx < itemCount; idx++) {
                    int row = idx / selectCols;
                    int col = idx % selectCols;
                    Rectangle cardRect = {
                        gridStartX + col * (cardSize + cardGapX),
                        (isVsBot && vsBotDraftActive)
                            ? draftCardY
                            : (selectViewport.y + 8.0f + row * (cardSize + cardGapY) - selectScrollY),
                        cardSize,
                        cardSize
                    };
                    if (!CheckCollisionPointRec(mousePos, cardRect)) continue;

                    BallType bType = (isVsBot && vsBotDraftActive) ? vsBotDraft[idx] : sortedBalls[idx];
                    if (!IsBallAvailable(bType, sandboxAllUnlocked)) continue;

                    pickedTypes[selectStep++] = bType;
                    if (isVsBot) {
                        if (!is2v2 && selectStep == 1) {
                            pickedTypes[selectStep++] = RollBotBallForDifficulty(selectedBotDiff);
                        } else if (is2v2 && selectStep == 1) {
                            vsBotDraftActive = RollVsBotPlayerDraft(vsBotDraft);
                        } else if (is2v2 && selectStep == 2) {
                            pickedTypes[selectStep++] = RollBotBallForDifficulty(selectedBotDiff);
                            pickedTypes[selectStep++] = RollBotBallForDifficulty(selectedBotDiff);
                        }
                    }
                    if (selectStep >= totalSlots) {
                        ClearEffects();
                        memset(g_balls, 0, sizeof(g_balls));
                        g_ballCount = totalSlots;

                        if (!is2v2) {
                            InitBallStats(0, pickedTypes[0], 0, (Vector2){ 200.0f, SCREEN_H / 2.0f });
                            InitBallStats(1, pickedTypes[1], 1, (Vector2){ 600.0f, SCREEN_H / 2.0f });
                            ApplyMatchBotAndUpgradeStats(&g_balls[0], false, selectedBotDiff);
                            ApplyMatchBotAndUpgradeStats(&g_balls[1], isVsBot, selectedBotDiff);
                        } else {
                            InitBallStats(0, pickedTypes[0], 0, (Vector2){ 180.0f, SCREEN_H * 0.32f });
                            InitBallStats(1, pickedTypes[1], 0, (Vector2){ 180.0f, SCREEN_H * 0.68f });
                            InitBallStats(2, pickedTypes[2], 1, (Vector2){ 620.0f, SCREEN_H * 0.32f });
                            InitBallStats(3, pickedTypes[3], 1, (Vector2){ 620.0f, SCREEN_H * 0.68f });
                            ApplyMatchBotAndUpgradeStats(&g_balls[0], false, selectedBotDiff);
                            ApplyMatchBotAndUpgradeStats(&g_balls[1], false, selectedBotDiff);
                            ApplyMatchBotAndUpgradeStats(&g_balls[2], isVsBot, selectedBotDiff);
                            ApplyMatchBotAndUpgradeStats(&g_balls[3], isVsBot, selectedBotDiff);
                        }

                        int baseCount = g_ballCount;
                        for (int k = 0; k < baseCount; k++) {
                            if (g_balls[k].type == TYPE_REFORGE)
                                SpawnReforgeSubBall(k);
                        }

                        countdownTimer = 3.0f;
                        matchTimer = (isVsBot || isChallenger) ? 180.0f : 0.0f;
                        isPaused = false;
                        surrenderConfirm = false;
                        matchRewardGiven = false;
                        state = COUNTDOWN;
                    }
                    break;
                }
            }
        }
        else if (state == COUNTDOWN)
        {
            if (!isPaused && !surrenderConfirm) {
                countdownTimer -= dt;
                if (countdownTimer <= 0.0f) state = PLAYING;
            }
        }
        else if (state == PLAYING && !isPaused && !surrenderConfirm)
        {
            if (isVsBot || isChallenger) {
                matchTimer -= dt;
                if (matchTimer < 0.0f) matchTimer = 0.0f;
            } else {
                matchTimer += dt;
            }
            UpdateEffects(dt);

            for (int i = 0; i < g_ballCount; i++)
            {
                Ball* b = &g_balls[i];
                if (!IsAlive(b)) continue;
                Ball* enemy = FindNearestEnemy(b);

                memmove(&b->shadowHistory[1], &b->shadowHistory[0], (SHADOW_N - 1) * sizeof(Vector2));
                b->shadowHistory[0] = b->pos;

                if (b->type == TYPE_ICE) {
                    for (int k = 0; k < ICE_MAX; k++) {
                        if (!b->ice[k].active) continue;
                        if ((b->ice[k].age += dt) >= ICE_TRAIL_TIME) b->ice[k].active = false;
                    }
                    if (GetRandomValue(0, 99) < 45) {
                        float a = (float)GetRandomValue(0, 359) * DEG2RAD;
                        float r = (float)b->radius + (float)GetRandomValue(-2, 6);
                        Vector2 p = { b->pos.x + cosf(a) * r, b->pos.y + sinf(a) * r };
                        Vector2 v = { cosf(a) * 0.6f, sinf(a) * 0.6f - 0.3f };
                        Color col = (GetRandomValue(0, 1) == 0) ? SKYBLUE : WHITE;
                        SpawnParticle(p, v, 0.45f, 3.0f, col, -0.02f, false);
                    }
                }

                if (b->wallBounceLockTimer > 0.0f) b->wallBounceLockTimer -= dt;
                if (b->painFlashTimer > 0.0f)      b->painFlashTimer -= dt;
                if (b->freezeImmune > 0.0f) b->freezeImmune -= dt;
                if (b->slowYTimer > 0.0f)   b->slowYTimer -= dt;
                if (b->slowPTimer > 0.0f)   b->slowPTimer -= dt;
                if (b->poisonTicks > 0) {
                    b->poisonTick -= dt;
                    if (b->poisonTick <= 0.0f) {
                        b->poisonTick += 0.6f;
                        b->poisonTicks--;
                        TakeDamageScaled(b, 1, b->poisonDmgMult);
                        if (!IsAlive(b)) continue;
                    }
                }

                // Archer Burn debuff: deals 3 HP every 0.75s for 3 ticks (total 9 HP)
                if (b->burnTicks > 0) {
                    b->burnTickTimer -= dt;
                    if (b->burnTickTimer <= 0.0f) {
                        b->burnTickTimer += ARCHER_BURN_TICK_T;
                        b->burnTicks--;
                        TakeDamageScaled(b, ARCHER_BURN_DMG, b->burnDmgMult);
                        if (!IsAlive(b)) continue;
                    }
                }

                // Dice, Gambler, and Psychic continue their active animations or tick down their cooldowns
                // even while immobilized (frozen/shocked/windStunned/diceLocked/slamLocked/execLocked)
                bool isImmobilized = (b->frozen || b->shocked || b->windStunned || b->diceLocked || b->slamLocked || b->execLocked);
                if (isImmobilized) {
                    if (b->type == TYPE_DICE)         UpdateDice(b, dt, true);
                    else if (b->type == TYPE_GAMBLER) UpdateGambler(b, dt, true);
                    else if (b->type == TYPE_PSYCHIC) UpdatePsychic(i, dt, true);
                    else if (b->type == TYPE_UNSHACKLED) {
                        if (b->unshackled.fxTimer > 0.0f)       b->unshackled.fxTimer -= dt;
                        if (b->unshackled.lashCooldown > 0.0f)  b->unshackled.lashCooldown -= dt;
                        if (b->unshackled.lashAnimTimer > 0.0f) b->unshackled.lashAnimTimer -= dt;
                        if (b->unshackled.passiveStacks > 0) {
                            b->unshackled.passiveTimer -= dt;
                            if (b->unshackled.passiveTimer <= 0.0f) {
                                b->unshackled.passiveStacks = 0;
                                b->unshackled.passiveTimer = 0.0f;
                            }
                        }
                    } else if (b->type == TYPE_JUDGEMENT && b->judgement.immuneTimer > 0.0f) {
                        b->judgement.immuneTimer -= dt;
                    } else if (b->type == TYPE_KNIGHT) {
                        UpdateKnight(b, dt);
                    } else if (b->type == TYPE_CATAPULT && b->catapult.boulderActive) {
                        UpdateCatapult(b, dt);
                    } else if (b->type == TYPE_ARCHER && b->archer.arrowActive) {
                        UpdateArcher(b, dt);
                    }
                }

                if (b->frozen)
                {
                    b->pos = b->freezePos;
                    b->freezeDmgTimer -= dt;
                    if (b->freezeDmgTimer <= 0.0f) {
                        TakeDamageScaled(b, 3, b->freezeDmgMult);
                        b->freezeDmgTimer += 0.5f;
                        if (!IsAlive(b)) continue;
                    }
                    b->frozenTimer -= dt;
                    if (b->frozenTimer <= 0.0f) {
                        b->frozen = false;
                        b->freezeImmune = 1.0f;
                        RestoreVelocity(b);
                    }
                    continue;
                }

                if (b->shocked)
                {
                    b->pos = b->shockPos;
                    b->shockTick -= dt;
                    if (b->shockTick <= 0.0f) {
                        b->shockTick += SHOCK_TICK_TIME;
                        b->shockTicks--;
                        DealDamageScaled(b, SHOCK_DMG, true, b->shockDmgMult);
                        if (!IsAlive(b)) continue;
                    }
                    if (b->shockTicks <= 0) {
                        b->shocked = false;
                        RestoreVelocity(b);
                    }
                    continue;
                }

                if (b->windStunned)
                {
                    b->pos = b->windStunPos;
                    b->windStunTimer -= dt;
                    if (b->windStunTimer <= 0.0f) {
                        b->windStunned = false;
                        RestoreVelocity(b);
                    }
                    continue;
                }

                if (b->diceLocked)
                {
                    b->pos = b->diceLockPos;
                    b->diceLockTimer -= dt;
                    if (b->diceLockTimer <= 0.0f) {
                        b->diceLocked = false;
                        RestoreVelocity(b);
                    }
                    continue;
                }

                if (b->slamLocked)
                {
                    b->pos = b->slamLockPos;
                    b->slamLockTimer -= dt;
                    if (b->slamLockTimer <= 0.0f) {
                        b->slamLocked = false;
                        if (b->type == TYPE_CAVALRY && b->cavalry.state == 2) {
                            b->cavalry.state = 0;
                            b->cavalry.timer = CAVALRY_CD;
                            b->cavalry.speedMult = 1.0f;
                        }
                        RestoreVelocity(b);
                    }
                    continue;
                }

                if (b->execLocked)
                {
                    b->pos = b->execLockPos;
                    b->execLockTimer -= dt;
                    if (b->execLockTimer <= 0.0f) {
                        b->execLocked = false;
                        RestoreVelocity(b);
                    }
                    continue;
                }

                // 1. Skills
                switch (b->type)
                {
                    case TYPE_BURST:   UpdateBurst(b, dt); break;
                    case TYPE_HARPOON: UpdateHarpoon(i, dt); break;
                    case TYPE_DEVIL:   UpdateDevil(b, dt); break;
                    case TYPE_TRIDENT: UpdateTrident(b, dt); break;
                    case TYPE_BOW:     UpdateBow(b, dt); break;
                    case TYPE_NINJA:   UpdateNinja(b, dt); break;
                    case TYPE_CELL:    UpdateCell(b, dt); break;
                    case TYPE_CACTUS:  UpdateCactus(b, dt); break;
                    case TYPE_WIND:    UpdateWind(b, dt); break;
                    case TYPE_CHESS:   UpdateChess(b, dt); break;
                    case TYPE_GUN:     UpdateGun(b, dt); break;
                    case TYPE_DICE:       UpdateDice(b, dt, false); break;
                    case TYPE_GAMBLER:    UpdateGambler(b, dt, false); break;
                    case TYPE_PSYCHIC:    UpdatePsychic(i, dt, false); break;
                    case TYPE_CHARGE:     UpdateCharge(b, dt); break;
                    case TYPE_UNSHACKLED: UpdateUnshackled(b, dt); break;
                    case TYPE_JUDGEMENT:  UpdateJudgement(b, dt); break;
                    case TYPE_TRAIN:      UpdateTrain(b, dt); break;
                    case TYPE_SPIDER:     UpdateSpider(b, dt); break;
                    case TYPE_RANGE:      UpdateRange(b, dt); break;
                    case TYPE_KNIGHT:     UpdateKnight(b, dt); break;
                    case TYPE_CAVALRY:    UpdateCavalry(b, dt); break;
                    case TYPE_EXECUTER:   UpdateExecuter(b, dt); break;
                    case TYPE_KING:       UpdateKing(b, dt); break;
                    case TYPE_CATAPULT:   UpdateCatapult(b, dt); break;
                    case TYPE_ARCHER:     UpdateArcher(b, dt); break;
                    case TYPE_ELECTRIC:
                        if (!b->charged && (b->chargeTimer -= dt) <= 0.0f) b->charged = true;
                        break;
                    case TYPE_WITCH:
                        if ((b->skillTimer -= dt) <= 0.0f) {
                            b->skillTimer += WITCH_INTERVAL;
                            WitchThrow(b, i);
                        }
                        break;
                    default: break;
                }

                if (IsChessDashing(b) || IsUnshackledCasting(b) || IsJudgementActive(b) ||
                    IsExecuterCharging(b) || IsKingDashing(b)) continue;

                // 2. Movement
                float k = (b->type == TYPE_GAMBLER && b->gambler.speedMult > 0.0f) ? b->gambler.speedMult : 1.0f;
                if (b->type == TYPE_CAVALRY && b->cavalry.speedMult > 0.0f) k *= b->cavalry.speedMult;
                for (int d = 0; d < g_ballCount; d++) {
                    Ball* dev = &g_balls[d];
                    if (!IsCombatBall(dev) || dev->type != TYPE_DEVIL || dev->drainTicks <= 0) continue;
                    if (dev->drainTargetIdx == i) { k = 0.0f; break; }
                    if (d == i && dev->drainTargetIdx >= 0) {
                        k = IsFixed(&g_balls[dev->drainTargetIdx]) ? 0.0f : LINK_SLOW;
                        break;
                    }
                }
                k *= SlowMult(b);
                if (b->boostTimer > 0.0f) { k *= BOOST_MULT; b->boostTimer -= dt; }

                b->pos = V2Add(b->pos, V2Mul(b->vel, k));

                // 3. Wall bounce
                BounceWalls(b, enemy);

                // Ice trail spawning & enemy freeze checks
                if (b->type == TYPE_ICE)
                {
                    IceSeg* s = &b->ice[b->iceHead];
                    *s = (IceSeg){ .pos = b->pos, .angle = atan2f(b->vel.y, b->vel.x), .age = 0.0f, .active = true };
                    b->iceHead = (b->iceHead + 1) % ICE_MAX;

                    for (int j = 0; j < g_ballCount; j++) {
                        Ball* e = &g_balls[j];
                        if (!IsCombatBall(e) || e->team == b->team) continue;
                        if (!e->frozen && !e->shocked && e->freezeImmune <= 0.0f) {
                            float d = IceFreezeDuration(b, e);
                            if (d > 0.0f) Freeze(e, d);
                        }
                    }
                }
            }

            ResolveDevilLinks();

            for (int i = 0; i < g_ballCount; i++) {
                for (int j = i + 1; j < g_ballCount; j++) {
                    ResolvePairCollision(i, j);
                }
            }

            for (int i = 0; i < g_ballCount; i++) EnforceAnchors(&g_balls[i]);

            bool teamAlive[2] = { false, false };
            for (int i = 0; i < g_ballCount; i++) {
                if (IsCombatBall(&g_balls[i]))
                    teamAlive[g_balls[i].team] = true;
            }
            bool matchEnded = (!teamAlive[0] || !teamAlive[1]);
            bool timeUp = (!matchEnded && (isVsBot || isChallenger) && matchTimer <= 0.0f);
            if (matchEnded || timeUp) {
                if (matchEnded) {
                    winningTeam = teamAlive[0] ? 0 : (teamAlive[1] ? 1 : -1);
                } else {
                    int teamHp[2] = { 0, 0 };
                    for (int i = 0; i < g_ballCount; i++) {
                        if (IsCombatBall(&g_balls[i]))
                            teamHp[g_balls[i].team] += g_balls[i].hp;
                    }
                    winningTeam = (teamHp[0] >= teamHp[1]) ? 0 : 1;
                }
                if (!matchRewardGiven) {
                    matchRewardGiven = true;
                    lastEarnedTickets = 0;
                    lastStreakBonusPct = 0;
                    lastFirst15Bonus = false;

                    if (!isVsBot && !isChallenger) {
                        // 1v1 and 2v2 sandbox: 0 tickets win or lose
                        lastEarnedTickets = 0;
                    } else if (isChallenger) {
                        if (winningTeam == 0) {
                            g_challengerWins++;
                            if (g_challengerWins >= 5) {
                                lastEarnedTickets = 600;
                                g_playerTickets += 600;
                                g_challengerActive = false;
                                g_challengerWins = 0;
                            } else {
                                g_challengerCurDiff = (GetRandomValue(1, 100) <= 75) ? BOT_IMPOSSIBLE : BOT_HARD;
                            }
                        } else {
                            g_challengerActive = false;
                            g_challengerWins = 0;
                        }
                    } else if (isVsBot) {
                        static const int winBase[4]  = { 5, 20, 40, 80 };
                        static const int loseBase[4] = { 2,  3,  0,  0 };
                        int dIdx = (selectedBotDiff >= 0 && selectedBotDiff <= BOT_IMPOSSIBLE) ? (int)selectedBotDiff : 0;
                        if (winningTeam == 0) {
                            int base = winBase[dIdx];
                            lastStreakBonusPct = CalcWinStreakBonusPct(g_botWinStreak[dIdx]);
                            int earned = base + (int)roundf((float)base * (float)lastStreakBonusPct / 100.0f);
                            if (g_botTotalWins < 15) {
                                earned += 20;
                                lastFirst15Bonus = true;
                            }
                            g_botTotalWins++;
                            g_botWinStreak[dIdx]++;
                            lastEarnedTickets = earned;
                            g_playerTickets += earned;
                        } else {
                            g_botWinStreak[dIdx] = 0;
                            lastEarnedTickets = loseBase[dIdx];
                            g_playerTickets += lastEarnedTickets;
                        }
                    }
                    SaveProgress();
                }
                state = GAME_OVER;
            }
        }

        // ======================= RENDER =======================
        BeginDrawing();
            if ((state == COUNTDOWN || state == PLAYING || state == GAME_OVER) && IsAnyChessActive()) {
                DrawChessboardArena();
            } else {
                ClearBackground(BG_COLOR);
            }

            // Top-left ticket & online gift bar in menus
            if (state == MENU_MODE || state == MENU_SANDBOX || state == MENU_BOT_MODE || state == MENU_CHALLENGER ||
                state == MENU_SELECT || state == MENU_GACHA || state == MENU_INVENTORY) {
                Rectangle tBox = { 15.0f, 10.0f, 160.0f, 30.0f };
                DrawRectangleRec(tBox, GOLD);
                DrawRectangleLinesEx(tBox, 1.5f, BLACK);
                DrawText(TextFormat("TICKETS: %d", g_playerTickets), 25, 17, 15, BLACK);

                int rem15 = (int)ceilf(fmaxf(0.0f, 900.0f - g_online15mSec));
                int rem1h = (int)ceilf(fmaxf(0.0f, 3600.0f - g_online1hSec));
                DrawText(TextFormat("15m(+50): %02d:%02d  |  1h(+300): %02d:%02d",
                                    rem15 / 60, rem15 % 60, rem1h / 60, rem1h % 60),
                         188, 18, 14, DARKGRAY);
            }

            if (state != MENU_MODE) {
                const char* topBtnTxt = inBotMatch
                    ? "SURRENDER"
                    : ((state == MENU_SANDBOX || state == MENU_BOT_MODE || state == MENU_CHALLENGER ||
                        state == MENU_SELECT || state == MENU_GACHA || state == MENU_INVENTORY || state == GAME_OVER)
                       ? "RETURN" : "RESET");
                DrawRectangleRec(btnTopRight, inBotMatch ? (Color){ 235, 130, 130, 255 } : LIGHTGRAY);
                DrawRectangleLinesEx(btnTopRight, 1.5f, BLACK);
                DrawText(topBtnTxt,
                         (int)(btnTopRight.x + (btnTopRight.width - MeasureText(topBtnTxt, 14)) * 0.5f),
                         (int)btnTopRight.y + 8, 14, BLACK);
            }

            if (state == COUNTDOWN || state == PLAYING) {
                const char* pauseTxt = isPaused ? "RESUME" : "PAUSE";
                DrawRectangleRec(btnPause, isPaused ? GOLD : LIGHTGRAY);
                DrawRectangleLinesEx(btnPause, 1.5f, BLACK);
                DrawText(pauseTxt,
                         (int)(btnPause.x + (btnPause.width - MeasureText(pauseTxt, 14)) * 0.5f),
                         (int)btnPause.y + 8, 14, BLACK);
            }

            if (state == COUNTDOWN || state == PLAYING || state == GAME_OVER) {
                int totalSecs = (isVsBot || isChallenger) ? (int)ceilf(matchTimer) : (int)floorf(matchTimer);
                if (totalSecs < 0) totalSecs = 0;
                int mins = totalSecs / 60;
                int secs = totalSecs % 60;
                const char* timeTxt = TextFormat("%02d:%02d", mins, secs);
                bool lowTime = (isVsBot || isChallenger) && matchTimer <= 15.0f && state == PLAYING;
                Rectangle timerBox = { SCREEN_W / 2.0f - 46.0f, 10.0f, 92.0f, 30.0f };
                DrawRectangleRec(timerBox, lowTime ? Fade((Color){ 255, 220, 220, 255 }, 0.92f) : Fade(RAYWHITE, 0.88f));
                DrawRectangleLinesEx(timerBox, 1.5f, lowTime ? RED : BLACK);
                DrawText(timeTxt,
                         (int)(timerBox.x + (timerBox.width - MeasureText(timeTxt, 18)) * 0.5f),
                         (int)timerBox.y + 6, 18, lowTime ? RED : BLACK);
            }

            if (state == MENU_MODE)
            {
                DrawCenteredText("BALLvBALL - SELECT MODE", 76, 26, BLACK);

                DrawRectangleRec(btnModeSandbox, SKYBLUE);
                DrawRectangleLinesEx(btnModeSandbox, 2.0f, BLACK);
                DrawText("SANDBOX", (int)(btnModeSandbox.x + (btnModeSandbox.width - MeasureText("SANDBOX", 22)) * 0.5f),
                         (int)btnModeSandbox.y + 16, 22, BLACK);

                DrawRectangleRec(btnModeVsBot, (Color){ 220, 45, 75, 255 });
                DrawRectangleLinesEx(btnModeVsBot, 2.0f, BLACK);
                DrawText("VS BOT", (int)(btnModeVsBot.x + (btnModeVsBot.width - MeasureText("VS BOT", 22)) * 0.5f),
                         (int)btnModeVsBot.y + 16, 22, WHITE);

                DrawRectangleRec(btnModeChall, (Color){ 165, 35, 35, 255 });
                DrawRectangleLinesEx(btnModeChall, 2.0f, BLACK);
                DrawText("CHALLENGER", (int)(btnModeChall.x + (btnModeChall.width - MeasureText("CHALLENGER", 19)) * 0.5f),
                         (int)btnModeChall.y + 18, 19, GOLD);

                DrawRectangleRec(btnOpenGacha, (Color){ 145, 55, 205, 255 });
                DrawRectangleLinesEx(btnOpenGacha, 2.0f, BLACK);
                DrawText("GACHA", (int)(btnOpenGacha.x + (btnOpenGacha.width - MeasureText("GACHA", 22)) * 0.5f),
                         (int)btnOpenGacha.y + 16, 22, WHITE);

                DrawRectangleRec(btnOpenInv, (Color){ 45, 145, 95, 255 });
                DrawRectangleLinesEx(btnOpenInv, 2.0f, BLACK);
                DrawText("INVENTORY", (int)(btnOpenInv.x + (btnOpenInv.width - MeasureText("INVENTORY", 20)) * 0.5f),
                         (int)btnOpenInv.y + 17, 20, WHITE);

                DrawCenteredText(TextFormat("FIRST-15 BOT WINS BONUS: %d / 15 (+20 TICKETS/WIN)",
                                            (g_botTotalWins < 15 ? g_botTotalWins : 15)),
                                 315, 15, DARKGRAY);
            }
            else if (state == MENU_SANDBOX)
            {
                DrawCenteredText("SANDBOX - SELECT TEAM SIZE", 110, 26, BLACK);
                DrawCenteredText("ALL BALLS UNLOCKED FOR TESTING (0 TICKETS)", 148, 15, (Color){ 40, 95, 165, 255 });

                DrawRectangleRec(btnSandbox1v1, SKYBLUE);
                DrawRectangleLinesEx(btnSandbox1v1, 2.0f, BLACK);
                DrawText("1 VS 1", (int)(btnSandbox1v1.x + (btnSandbox1v1.width - MeasureText("1 VS 1", 24)) * 0.5f),
                         (int)btnSandbox1v1.y + 20, 24, BLACK);

                DrawRectangleRec(btnSandbox2v2, ORANGE);
                DrawRectangleLinesEx(btnSandbox2v2, 2.0f, BLACK);
                DrawText("2 VS 2", (int)(btnSandbox2v2.x + (btnSandbox2v2.width - MeasureText("2 VS 2", 24)) * 0.5f),
                         (int)btnSandbox2v2.y + 20, 24, WHITE);
            }
            else if (state == MENU_BOT_MODE)
            {
                static const char* const diffNames[4] = { "EASY", "MEDIUM", "HARD", "IMPOSSIBLE" };
                static const char* const diffRew[4]   = { "W:+5  L:+2", "W:+20 L:+3", "W:+40 L:0", "W:+80 L:0" };
                static const char* const diffBuffs[4] = { "100 HP | BASE", "100 HP | BASE", "110 HP +15%DMG +5%HOM", "120 HP +25%DMG +10%HOM" };
                static const Color diffCols[4] = {
                    { 75, 185, 95, 255 }, { 70, 155, 225, 255 }, { 225, 125, 40, 255 }, { 195, 35, 55, 255 }
                };

                DrawCenteredText("VS BOT - SELECT DIFFICULTY & TEAM SIZE", 68, 22, BLACK);
                for (int d = 0; d < 4; d++) {
                    Rectangle dc = { 34.0f + d * 186.0f, 118.0f, 174.0f, 172.0f };
                    bool sel = ((int)selectedBotDiff == d);
                    DrawRectangleRec(dc, sel ? (Color){ 255, 252, 232, 255 } : RAYWHITE);
                    DrawRectangle((int)dc.x, (int)dc.y, (int)dc.width, 30, diffCols[d]);
                    DrawText(diffNames[d], (int)(dc.x + (dc.width - MeasureText(diffNames[d], 17)) * 0.5f), (int)dc.y + 6, 17, WHITE);
                    DrawText(diffRew[d], (int)(dc.x + (dc.width - MeasureText(diffRew[d], 14)) * 0.5f), (int)dc.y + 48, 14, BLACK);
                    DrawText(diffBuffs[d], (int)(dc.x + (dc.width - MeasureText(diffBuffs[d], 10)) * 0.5f), (int)dc.y + 78, 10, (d >= 2) ? MAROON : DARKGRAY);
                    if (d == 3) {
                        const char* imp2 = "5% IMMUNE | 10% -33%DMG";
                        DrawText(imp2, (int)(dc.x + (dc.width - MeasureText(imp2, 10)) * 0.5f), (int)dc.y + 96, 10, MAROON);
                    }
                    const char* stTxt = TextFormat("STREAK: %d (+%d%%)", g_botWinStreak[d], CalcWinStreakBonusPct(g_botWinStreak[d]));
                    DrawText(stTxt, (int)(dc.x + (dc.width - MeasureText(stTxt, 13)) * 0.5f), (int)dc.y + 134, 13, (Color){ 40, 95, 165, 255 });
                    DrawRectangleLinesEx(dc, sel ? 3.5f : 1.5f, sel ? GOLD : BLACK);
                }

                DrawRectangleRec(btnBot1v1, SKYBLUE);
                DrawRectangleLinesEx(btnBot1v1, 2.0f, BLACK);
                DrawText("1 VS 1 BOT", (int)(btnBot1v1.x + (btnBot1v1.width - MeasureText("1 VS 1 BOT", 20)) * 0.5f),
                         (int)btnBot1v1.y + 18, 20, BLACK);

                DrawRectangleRec(btnBot2v2, (Color){ 220, 45, 75, 255 });
                DrawRectangleLinesEx(btnBot2v2, 2.0f, BLACK);
                DrawText("2 VS 2 BOT", (int)(btnBot2v2.x + (btnBot2v2.width - MeasureText("2 VS 2 BOT", 20)) * 0.5f),
                         (int)btnBot2v2.y + 18, 20, WHITE);

                DrawCenteredText("STREAK BONUSES: 4th+:+10% | 6th+:+25% | 11th+:+50% | 16th+:+75% | 21st+:+105%", 405, 12, DARKGRAY);
            }
            else if (state == MENU_CHALLENGER)
            {
                DrawCenteredText("CHALLENGER MODE", 76, 28, MAROON);
                DrawCenteredText("WIN 5 MATCHES IN A ROW TO CLAIM +600 TICKETS!", 122, 18, BLACK);
                DrawCenteredText("ENTRY FEE: 80 TICKETS  |  BOT ODDS: 75% IMPOSSIBLE / 25% HARD", 152, 14, DARKGRAY);

                if (!g_challengerActive) {
                    bool canEnter = (g_playerTickets >= 80);
                    DrawRectangleRec(btnChallAction, canEnter ? (Color){ 175, 35, 35, 255 } : GRAY);
                    DrawRectangleLinesEx(btnChallAction, 2.0f, BLACK);
                    const char* txt = canEnter ? "ENTER (80 TICKETS)" : "NEED 80 TICKETS";
                    DrawText(txt, (int)(btnChallAction.x + (btnChallAction.width - MeasureText(txt, 20)) * 0.5f),
                             (int)btnChallAction.y + 18, 20, WHITE);
                } else {
                    DrawCenteredText(TextFormat("CURRENT RUN: %d / 5 WINS", g_challengerWins), 205, 24, (Color){ 35, 130, 65, 255 });
                    DrawCenteredText(TextFormat("NEXT OPPONENT: %s BOT (1V1)",
                                                (g_challengerCurDiff == BOT_IMPOSSIBLE) ? "IMPOSSIBLE" : "HARD"),
                                     240, 17, MAROON);
                    DrawRectangleRec(btnChallAction, (Color){ 210, 55, 45, 255 });
                    DrawRectangleLinesEx(btnChallAction, 2.0f, GOLD);
                    const char* txt = TextFormat("FIGHT ROUND %d / 5", g_challengerWins + 1);
                    DrawText(txt, (int)(btnChallAction.x + (btnChallAction.width - MeasureText(txt, 20)) * 0.5f),
                             (int)btnChallAction.y + 18, 20, WHITE);
                }
            }
            else if (state == MENU_INVENTORY)
            {
                static const char* const tabNames[4] = { "INVENTORY", "UPGRADE", "FUSION", "FORGE" };
                for (int t = 0; t < 4; t++) {
                    Rectangle tabBtn = { 110.0f + t * 148.0f, 48.0f, 136.0f, 34.0f };
                    bool active = ((int)invTab == t);
                    DrawRectangleRec(tabBtn, active ? GOLD : LIGHTGRAY);
                    DrawRectangleLinesEx(tabBtn, 1.5f, BLACK);
                    DrawText(tabNames[t], (int)(tabBtn.x + (tabBtn.width - MeasureText(tabNames[t], 15)) * 0.5f),
                             (int)tabBtn.y + 9, 15, BLACK);
                }

                if (invTab == INV_TAB_FUSION || invTab == INV_TAB_FORGE) {
                    DrawCenteredText(TextFormat("%s - COMING SOON", tabNames[invTab]), 210, 32, DARKGRAY);
                } else {
                    BeginScissorMode((int)selectViewport.x, (int)selectViewport.y,
                                     (int)selectViewport.width, (int)selectViewport.height);
                    for (int idx = 0; idx < TYPE_COUNT; idx++) {
                        int row = idx / selectCols;
                        int col = idx % selectCols;
                        Rectangle cardRect = {
                            gridStartX + col * (cardSize + cardGapX),
                            selectViewport.y + 8.0f + row * (cardSize + cardGapY) - invScrollY,
                            cardSize, cardSize
                        };
                        if (cardRect.y + cardRect.height < selectViewport.y ||
                            cardRect.y > selectViewport.y + selectViewport.height) continue;

                        BallType bType = sortedBalls[idx];
                        bool owned = g_unlocked[bType];
                        BallRarity rar = g_ballRarity[bType];
                        Color rarCol = RarityColor(rar);
                        bool canUpg = (invTab == INV_TAB_UPGRADE && owned && g_ballCopies[bType] >= 5);
                        bool hovered = canUpg && CheckCollisionPointRec(mousePos, selectViewport) &&
                                       CheckCollisionPointRec(mousePos, cardRect);

                        DrawRectangleRec(cardRect, owned ? (hovered ? (Color){ 255, 255, 230, 255 } : RAYWHITE) : (Color){ 210, 212, 218, 255 });
                        DrawRectangle((int)cardRect.x, (int)cardRect.y, (int)cardRect.width, 22, rarCol);
                        const char* rarTag = g_rarityNames[rar];
                        DrawText(rarTag, (int)(cardRect.x + (cardRect.width - MeasureText(rarTag, 12)) * 0.5f),
                                 (int)cardRect.y + 5, 12, (rar >= RARITY_MYTHIC) ? WHITE : BLACK);

                        Vector2 iconCenter = { cardRect.x + cardRect.width * 0.5f, cardRect.y + 68.0f };
                        DrawBallPreviewIcon(bType, iconCenter, btnColors[bType]);

                        if (owned) {
                            const char* info = (invTab == INV_TAB_INVENTORY)
                                ? TextFormat("x%d  |  LV.%d", g_ballCopies[bType], g_ballUpgradeLv[bType])
                                : TextFormat("LV.%d (%d/5)", g_ballUpgradeLv[bType], g_ballCopies[bType]);
                            DrawText(info, (int)(cardRect.x + (cardRect.width - MeasureText(info, 12)) * 0.5f),
                                     (int)(cardRect.y + cardRect.height - 44), 12, canUpg ? DARKGREEN : DARKGRAY);
                            if (canUpg) {
                                DrawText("CLICK UPGRADE", (int)(cardRect.x + (cardRect.width - MeasureText("CLICK UPGRADE", 11)) * 0.5f),
                                         (int)(cardRect.y + 25), 11, DARKGREEN);
                            }
                        }

                        Rectangle nameBar = { cardRect.x, cardRect.y + cardRect.height - 26.0f, cardRect.width, 26.0f };
                        DrawRectangleRec(nameBar, owned ? (Color){ 32, 34, 40, 255 } : (Color){ 95, 98, 105, 255 });
                        DrawText(names[bType], (int)(cardRect.x + (cardRect.width - MeasureText(names[bType], 14)) * 0.5f),
                                 (int)nameBar.y + 6, 14, owned ? WHITE : LIGHTGRAY);

                        if (!owned) {
                            DrawRectangleRec(cardRect, Fade((Color){ 155, 158, 165, 255 }, 0.68f));
                            int lw = MeasureText("LOCKED", 18);
                            DrawRectangle((int)(iconCenter.x - lw * 0.5f - 8), (int)(iconCenter.y - 12), lw + 16, 24, Fade(LIGHTGRAY, 0.88f));
                            DrawRectangleLines((int)(iconCenter.x - lw * 0.5f - 8), (int)(iconCenter.y - 12), lw + 16, 24, BLACK);
                            DrawText("LOCKED", (int)(iconCenter.x - lw * 0.5f), (int)(iconCenter.y - 9), 18, BLACK);
                        }

                        DrawRectangleLinesEx(cardRect, hovered ? 3.5f : 2.0f, canUpg ? GREEN : rarCol);
                        DrawRectangleLines((int)cardRect.x, (int)cardRect.y, (int)cardRect.width, (int)cardRect.height, BLACK);
                    }
                    EndScissorMode();
                    DrawRectangleLinesEx(selectViewport, 1.5f, DARKGRAY);
                    if (invTab == INV_TAB_UPGRADE) {
                        DrawCenteredText("UPGRADE COST: 5 COPIES -> +5 STARTING HP & +2.5% DAMAGE PER LEVEL", 438, 12, DARKGRAY);
                    } else if (invTab == INV_TAB_INVENTORY) {
                        int unlockedCnt = 0;
                        for (int i = 0; i < TYPE_COUNT; i++) if (g_unlocked[i]) unlockedCnt++;
                        DrawCenteredText(TextFormat("UNLOCKED: %d / %d", unlockedCnt, TYPE_COUNT), 438, 14, DARKGRAY);
                    }
                }
            }
            else if (state == MENU_GACHA)
            {
                DrawCenteredText("GACHA BALL MACHINE", 58, 26, BLACK);
                DrawCenteredText(TextFormat("TICKETS: %d   |   PITY  R:%d/60  M:%d/500  L:%d/1400",
                                            g_playerTickets, g_pityRare, g_pityMythic, g_pityLegend),
                                 92, 15, DARKGRAY);

                Rectangle card = { SCREEN_W / 2.0f - 145.0f, 124.0f, 290.0f, 190.0f };
                BallType showType = (gachaRolling || gachaHasResult) ? gachaPreview : TYPE_BURST;
                BallRarity showRarity = g_ballRarity[showType];
                Color rCol = RarityColor(showRarity);

                DrawRectangleRec(card, RAYWHITE);
                DrawRectangleLinesEx(card, 4.0f, (gachaRolling || gachaHasResult) ? rCol : DARKGRAY);

                if (gachaRolling || gachaHasResult) {
                    DrawRectangle((int)card.x + 4, (int)card.y + 4, (int)card.width - 8, 32, rCol);
                    Color bannerTxtCol = (showRarity >= RARITY_MYTHIC) ? WHITE : BLACK;
                    const char* rLabel = g_rarityNames[showRarity];
                    DrawText(rLabel, (int)(card.x + (card.width - MeasureText(rLabel, 18)) * 0.5f),
                             (int)card.y + 11, 18, bannerTxtCol);

                    Vector2 cPos = { SCREEN_W / 2.0f, card.y + 98.0f };
                    DrawBallPreviewIcon(showType, cPos, btnColors[showType]);

                    const char* bName = names[showType];
                    DrawText(bName, (int)(card.x + (card.width - MeasureText(bName, 22)) * 0.5f),
                             (int)card.y + 148, 22, BLACK);

                    if (gachaHasResult) {
                        const char* statusTxt = gachaWasNew
                            ? "NEW BALL UNLOCKED! (x1 COPY)"
                            : TextFormat("DUPLICATE! (COPIES: x%d)", g_ballCopies[gachaResult]);
                        DrawCenteredText(statusTxt, 319, 15, gachaWasNew ? DARKGREEN : DARKGRAY);
                    }
                } else {
                    DrawCenteredText("PRESS ROLL TO SPIN!", (int)card.y + 85, 20, DARKGRAY);
                }

                bool canRoll = !gachaRolling && (g_playerTickets > 0);
                DrawRectangleRec(btnRollNow, canRoll ? (Color){ 45, 180, 65, 255 } : GRAY);
                DrawRectangleLinesEx(btnRollNow, 2.0f, BLACK);
                const char* rollTxt = gachaRolling ? "ROLLING..." : (canRoll ? "ROLL GACHA (1 TICKET)" : "NO TICKETS LEFT");
                DrawText(rollTxt,
                         (int)(btnRollNow.x + (btnRollNow.width - MeasureText(rollTxt, 16)) * 0.5f),
                         (int)btnRollNow.y + 18, 16, WHITE);

                DrawCenteredText("RATES: COMMON 55% | UNCOMMON 29% | RARE 10% | MYTHIC 5% | LEGENDARY 1%",
                                 415, 12, DARKGRAY);
            }
            else if (state == MENU_SELECT)
            {
                const char* title = "";
                if (isVsBot) {
                    if (!is2v2) {
                        title = vsBotDraftActive
                            ? "1V1 VS BOT - CHOOSE 1 OF 3 RANDOM BALLS (BLUE TEAM)"
                            : "1V1 VS BOT - SELECT YOUR BALL (BLUE TEAM)";
                    } else {
                        title = (selectStep == 0)
                            ? (vsBotDraftActive ? "2V2 VS BOT - PICK BALL 1 OF 3 (BLUE TEAM)" : "2V2 VS BOT - SELECT BALL 1 (BLUE TEAM)")
                            : (vsBotDraftActive ? "2V2 VS BOT - PICK BALL 2 OF 3 (BLUE TEAM)" : "2V2 VS BOT - SELECT BALL 2 (BLUE TEAM)");
                    }
                } else if (!is2v2) {
                    title = (selectStep == 0) ? "SELECT P1 BALL (BLUE TEAM)" : "SELECT P2 BALL (RED TEAM)";
                } else {
                    static const char* const titles2v2[4] = {
                        "SELECT BLUE TEAM - BALL 1",
                        "SELECT BLUE TEAM - BALL 2",
                        "SELECT RED TEAM - BALL 1",
                        "SELECT RED TEAM - BALL 2"
                    };
                    title = titles2v2[selectStep];
                }
                DrawCenteredText(title, 52, 22, BLACK);
                if (isVsBot && vsBotDraftActive) {
                    DrawCenteredText("3 RANDOM BALLS DRAWN FROM YOUR OWNED BALLS - PICK 1 TO FIGHT BOT", 74, 12, DARKGRAY);
                } else {
                    DrawCenteredText("SCROLL WITH MOUSE WHEEL OR UP/DOWN KEYS (SORTED BY RARITY)", 74, 12, DARKGRAY);
                }

                BeginScissorMode((int)selectViewport.x, (int)selectViewport.y,
                                 (int)selectViewport.width, (int)selectViewport.height);

                int itemCount = (isVsBot && vsBotDraftActive) ? 3 : TYPE_COUNT;
                float draftCardY = selectViewport.y + (selectViewport.height - cardSize) * 0.5f;

                for (int idx = 0; idx < itemCount; idx++) {
                    int row = idx / selectCols;
                    int col = idx % selectCols;
                    Rectangle cardRect = {
                        gridStartX + col * (cardSize + cardGapX),
                        (isVsBot && vsBotDraftActive)
                            ? draftCardY
                            : (selectViewport.y + 8.0f + row * (cardSize + cardGapY) - selectScrollY),
                        cardSize,
                        cardSize
                    };
                    if (cardRect.y + cardRect.height < selectViewport.y ||
                        cardRect.y > selectViewport.y + selectViewport.height) {
                        continue;
                    }

                    BallType bType = (isVsBot && vsBotDraftActive) ? vsBotDraft[idx] : sortedBalls[idx];
                    bool avail = IsBallAvailable(bType, (!isVsBot && !isChallenger));
                    BallRarity rar = g_ballRarity[bType];
                    Color rarCol = RarityColor(rar);
                    bool hovered = avail && CheckCollisionPointRec(mousePos, selectViewport) &&
                                   CheckCollisionPointRec(mousePos, cardRect);

                    // Square card background
                    Color cardBg = avail ? (hovered ? (Color){ 255, 255, 245, 255 } : RAYWHITE)
                                         : (Color){ 210, 212, 218, 255 };
                    DrawRectangleRec(cardRect, cardBg);

                    // Top rarity header strip
                    DrawRectangle((int)cardRect.x, (int)cardRect.y, (int)cardRect.width, 22, rarCol);
                    const char* rarTag = g_rarityNames[rar];
                    Color rarTxtCol = (rar >= RARITY_MYTHIC) ? WHITE : BLACK;
                    DrawText(rarTag,
                             (int)(cardRect.x + (cardRect.width - MeasureText(rarTag, 12)) * 0.5f),
                             (int)cardRect.y + 5, 12, rarTxtCol);

                    // Actual ball illustration in the center of the square card
                    Vector2 iconCenter = { cardRect.x + cardRect.width * 0.5f, cardRect.y + 78.0f };
                    DrawBallPreviewIcon(bType, iconCenter, btnColors[bType]);

                    // Bottom name plate
                    Rectangle nameBar = { cardRect.x, cardRect.y + cardRect.height - 28.0f, cardRect.width, 28.0f };
                    DrawRectangleRec(nameBar, avail ? (Color){ 32, 34, 40, 255 } : (Color){ 95, 98, 105, 255 });
                    const char* label = names[bType];
                    int fontSz = 15;
                    DrawText(label,
                             (int)(cardRect.x + (cardRect.width - MeasureText(label, fontSz)) * 0.5f),
                             (int)nameBar.y + (28 - fontSz) / 2, fontSz, avail ? WHITE : LIGHTGRAY);

                    // Translucent gray overlay + black "LOCKED" text in the center for locked balls
                    if (!avail) {
                        DrawRectangleRec(cardRect, Fade((Color){ 155, 158, 165, 255 }, 0.68f));
                        const char* lockSym = "LOCKED";
                        int lw = MeasureText(lockSym, 18);
                        DrawRectangle((int)(iconCenter.x - lw * 0.5f - 8), (int)(iconCenter.y - 12),
                                      lw + 16, 24, Fade(LIGHTGRAY, 0.88f));
                        DrawRectangleLines((int)(iconCenter.x - lw * 0.5f - 8), (int)(iconCenter.y - 12),
                                           lw + 16, 24, BLACK);
                        DrawText(lockSym,
                                 (int)(iconCenter.x - lw * 0.5f),
                                 (int)(iconCenter.y - 9), 18, BLACK);
                    }

                    // Card border
                    DrawRectangleLinesEx(cardRect, hovered ? 3.5f : 2.5f, hovered ? GOLD : rarCol);
                    DrawRectangleLines((int)cardRect.x, (int)cardRect.y, (int)cardRect.width, (int)cardRect.height, BLACK);
                }

                EndScissorMode();

                // Viewport frame & vertical scrollbar
                DrawRectangleLinesEx(selectViewport, 1.5f, DARKGRAY);
                if (!isVsBot || !vsBotDraftActive) {
                    Rectangle scrollTrack = { selectViewport.x + selectViewport.width + 6.0f, selectViewport.y, 12.0f, selectViewport.height };
                    DrawRectangleRec(scrollTrack, (Color){ 180, 182, 188, 255 });
                    DrawRectangleLinesEx(scrollTrack, 1.0f, BLACK);
                    if (maxSelectScroll > 0.0f) {
                        float thumbH = fmaxf(36.0f, selectViewport.height * (selectViewport.height / totalGridH));
                        float thumbY = scrollTrack.y + (selectScrollY / maxSelectScroll) * (scrollTrack.height - thumbH);
                        Rectangle thumbRect = { scrollTrack.x + 1.0f, thumbY, scrollTrack.width - 2.0f, thumbH };
                        DrawRectangleRec(thumbRect, draggingScrollbar ? GOLD : (Color){ 80, 85, 95, 255 });
                        DrawRectangleLinesEx(thumbRect, 1.0f, BLACK);
                    }
                }
            }
            else
            {
                for (int i = 0; i < g_ballCount; i++) DrawUnderlay(&g_balls[i]);
                for (int i = 0; i < g_ballCount; i++) DrawBallEntity(&g_balls[i]);

                DrawEffectsLayer();
                DrawPopups();

                if (state == COUNTDOWN && countdownTimer > 0.0f) {
                    DrawCenteredText(TextFormat("%d", (int)ceilf(countdownTimer)), SCREEN_H / 2 - 40, 80, BLACK);
                }
                else if (state == GAME_OVER) {
                    DrawRectangle(0, SCREEN_H / 2 - 65, SCREEN_W, 138, Fade(BLACK, 0.78f));
                    DrawCenteredText("GAME OVER", SCREEN_H / 2 - 52, 48, RED);
                    const char* winTxt = isVsBot
                        ? ((winningTeam == 0) ? "PLAYER WINS VS BOT!" : (winningTeam == 1 ? "BOT WINS!" : "DRAW!"))
                        : ((winningTeam == 0) ? "BLUE TEAM WINS!"     : (winningTeam == 1 ? "RED TEAM WINS!" : "DRAW!"));
                    Color winCol = (winningTeam == 0) ? SKYBLUE : (winningTeam == 1 ? ORANGE : RAYWHITE);
                    DrawCenteredText(winTxt, SCREEN_H / 2 + 2, 24, winCol);
                    const char* rewTxt = "";
                    if (!isVsBot && !isChallenger) {
                        rewTxt = "TEST MODE: +0 TICKETS";
                    } else if (isChallenger) {
                        if (lastEarnedTickets > 0) rewTxt = "CHALLENGER 5/5 COMPLETE! +600 TICKETS!";
                        else if (g_challengerActive) rewTxt = TextFormat("CHALLENGER PROGRESS: %d / 5 WINS!", g_challengerWins);
                        else rewTxt = "CHALLENGER FAILED! (+0 TICKETS)";
                    } else {
                        rewTxt = TextFormat("+%d TICKETS EARNED!%s%s",
                                            lastEarnedTickets,
                                            (lastStreakBonusPct > 0) ? TextFormat(" (+%d%% STREAK)", lastStreakBonusPct) : "",
                                            lastFirst15Bonus ? " (+20 NEWBIE)" : "");
                    }
                    DrawCenteredText(rewTxt, SCREEN_H / 2 + 34, 16, GOLD);
                }

                if (isPaused && !surrenderConfirm && (state == COUNTDOWN || state == PLAYING)) {
                    DrawRectangle(0, SCREEN_H / 2 - 45, SCREEN_W, 90, Fade(BLACK, 0.6f));
                    DrawCenteredText("PAUSED", SCREEN_H / 2 - 22, 48, GOLD);
                }
                if (surrenderConfirm && inBotMatch) {
                    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f));
                    Rectangle dlg = { SCREEN_W / 2.0f - 175.0f, SCREEN_H / 2.0f - 68.0f, 350.0f, 136.0f };
                    DrawRectangleRec(dlg, RAYWHITE);
                    DrawRectangleLinesEx(dlg, 3.0f, BLACK);
                    DrawCenteredText("ARE YOU SURE?", (int)dlg.y + 22, 24, BLACK);
                    DrawCenteredText("SURRENDER COUNTS AS A LOSS", (int)dlg.y + 52, 13, MAROON);

                    DrawRectangleRec(btnSurrYes, (Color){ 210, 45, 45, 255 });
                    DrawRectangleLinesEx(btnSurrYes, 2.0f, BLACK);
                    DrawText("YES", (int)(btnSurrYes.x + (btnSurrYes.width - MeasureText("YES", 20)) * 0.5f),
                             (int)btnSurrYes.y + 10, 20, WHITE);

                    DrawRectangleRec(btnSurrNo, LIGHTGRAY);
                    DrawRectangleLinesEx(btnSurrNo, 2.0f, BLACK);
                    DrawText("NO", (int)(btnSurrNo.x + (btnSurrNo.width - MeasureText("NO", 20)) * 0.5f),
                             (int)btnSurrNo.y + 10, 20, BLACK);
                }
            }
        EndDrawing();
    }

    SaveProgress();
    CloseWindow();
    return 0;
}
