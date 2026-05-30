// ═════════════════════════════════════════════════════════
//  BLACKJACK — Pebble Watchapp
//  Platforms: flint (Pebble 2 Duo, 144×168 B&W)
//             emery (Pebble Time 2, 200×228 colour)
//             aplite / basalt / diorite (rectangular, B&W / colour)
//  SDK      : Pebble C SDK 3, built via CloudPebble
//  Author   : Joel Penton / Persistent Productions
// ═════════════════════════════════════════════════════════
//
//  GAME RULES
//  ──────────
//  · Single 52-card deck. Reshuffles silently when <15 cards remain.
//  · Dealer stands on ALL 17s (including soft 17).
//  · Aces count as 11, drop to 1 to avoid a bust.
//  · Natural blackjack (21 on exactly 2 cards) auto-stands.
//  · No betting, doubling down, splitting, or surrender.
//
//  CONTROLS
//  ────────
//  Launch           Any button        → deal first hand
//  Player turn      UP                → hit
//                   DOWN              → stand
//                   SELECT (hold)     → score + hints; release to dismiss
//  Result screen    UP or DOWN        → deal new hand
//                   SELECT (hold)     → peek at board; release to return
//  Always           BACK              → exit
//
//  SCORING  (persisted across sessions)
//    Win / Blackjack / Dealer bust  +100
//    Lose / Bust                    − 60
//    Tie                              ±0
//
//  RESOURCE REQUIREMENTS
//  ─────────────────────
//  All platforms:    suit_club.png   (20×20, black)   → SUIT_CLUB
//                    suit_spade.png  (20×20, black)   → SUIT_SPADE
//                    suit_heart.png  (20×20, black)   → SUIT_HEART
//                    suit_diamond.png(20×20, black)   → SUIT_DIAMOND
//  Colour platforms: suit_heart_red.png  (20×20, red) → SUIT_HEART_RED
//                    suit_diamond_red.png(20×20, red) → SUIT_DIAMOND_RED
//  All PNGs: solid white background, Memory Format = "Smallest Palette"
// ═════════════════════════════════════════════════════════

#include <pebble.h>

// ─────────────────────────────────────────────────────────
//  Platform-specific layout constants
//
//  All layout maths derive from these — changing them here
//  is sufficient to re-flow the whole app for a new platform.
// ─────────────────────────────────────────────────────────
#ifdef PBL_PLATFORM_EMERY
  // ── Pebble Time 2 — 200×228 colour display ────────────
  #define SCREEN_W       200
  #define SCREEN_H       228
  #define TOP_BAR_H       20
  #define BOT_BAR_H       20
  #define ROW_H           24
  #define CARD_W          36
  #define CARD_H          60
  #define CARD_STEP       24
  #define CARD_X          10
  #define SUIT_SZ         20
  #define CARD_RANK_H     22
  #define SUIT_BMP_DX      8
  #define SUIT_BMP_DY     27
  #define LABEL_CARD_GAP   5
  #define CARD_DIV_GAP     5
  // Clock bar text position
  #define CLOCK_Y_OFF     -7
  #define CLOCK_H_ADD      8   // clock rect height = TOP_BAR_H + this
  // Launch screen layout
  #define LAUNCH_BOX_X    25   // = SCREEN_W/8
  #define LAUNCH_BOX_H    46
  #define LAUNCH_TITLE_H  36
  #define LAUNCH_SY_OFF   38   // suits top = title_y + this
  #define LAUNCH_SY_GAP    6   // gap between suits bottom and box top
  #define LAUNCH_CR_GAP    4   // gap between box bottom and credits top
  #define LAUNCH_CR_H     64
  // Result overlay — y offsets are from 'by' (= TOP_BAR_H + 1)
  #define RES_HEADER_Y     6
  #define RES_HEADER_H    36
  #define RES_WTL_Y1      46
  #define RES_WTL_STEP    18
  #define RES_TOTALS_Y   106
  #define RES_PROMPT_Y   146
  #define RES_PROMPT_H    38
  #define RES_INNER_W    108
  // Score overlay
  #define SCR_OVL_BH      96
  #define SCR_HEADER_H    22
  #define SCR_WTL_Y1      30
  #define SCR_WTL_STEP    18

#else
  // ── All other rectangular platforms — 144×168 ─────────
  #define SCREEN_W       144
  #define SCREEN_H       168
  #define TOP_BAR_H       16
  #define BOT_BAR_H       17
  #define ROW_H           14
  #define CARD_W          32
  #define CARD_H          49
  #define CARD_STEP       22
  #define CARD_X           4
  #define SUIT_SZ         20
  #define CARD_RANK_H     22
  #define SUIT_BMP_DX      6
  #define SUIT_BMP_DY     23
  #define LABEL_CARD_GAP   1
  #define CARD_DIV_GAP     2
  // Clock bar — nudge down 1px vs emery so digits sit centred in 16px bar
  #define CLOCK_Y_OFF     -5
  #define CLOCK_H_ADD      6
  // Launch screen — 2px gap between every element so nothing touches
  #define LAUNCH_BOX_X     6   // wider box → "PRESS ANY BUTTON" fits on one line
  #define LAUNCH_BOX_H    32   // 2 lines of GOTHIC_14_BOLD + 4px padding = 32px
  #define LAUNCH_TITLE_H  28   // full GOTHIC_28_BOLD glyph height
  #define LAUNCH_SY_OFF   34   // extra offset compensates for GOTHIC_28_BOLD internal leading
  #define LAUNCH_SY_GAP    2   // 2px gap between suits bottom and box top
  #define LAUNCH_CR_GAP    2   // 2px gap between box bottom and credits top
  #define LAUNCH_CR_H     40   // trimmed 4px to compensate for larger SY_OFF; 3 lines still fit
  // Result overlay
  #define RES_HEADER_Y     2
  #define RES_HEADER_H    26
  #define RES_WTL_Y1      30
  #define RES_WTL_STEP    14
  #define RES_TOTALS_Y    74
  #define RES_PROMPT_Y   104
  #define RES_PROMPT_H    26
  #define RES_INNER_W     90
  // Score overlay
  #define SCR_OVL_BH      72
  #define SCR_HEADER_H    16
  #define SCR_WTL_Y1      24
  #define SCR_WTL_STEP    14
#endif

// ─────────────────────────────────────────────────────────
//  Game constants (platform-independent)
// ─────────────────────────────────────────────────────────
#define DECK_SIZE     52
#define RESHUFFLE_AT  15
#define MAX_HAND      11

#define FIRST_MS      400
#define DEAL_MS      1000
#define DEALER_END_MS 600
#define BUST_PAUSE_MS 800

#define SCORE_WIN    100
#define SCORE_LOSE   -60
#define PERSIST_KEY      42   // cumulative score
#define PERSIST_KEY_WINS 43   // win count
#define PERSIST_KEY_LOSS 44   // loss count
#define PERSIST_KEY_TIES 45   // tie count

// ═════════════════════════════════════════════════════════
//  TYPES
// ═════════════════════════════════════════════════════════

// rank: 1=Ace, 2–10, 11=J, 12=Q, 13=K
// suit: 0=Clubs, 1=Diamonds, 2=Hearts, 3=Spades
//       (index also maps to s_suit_bmp[] array)
typedef struct { int rank; int suit; } Card;

// State machine — see header comment for transitions.
typedef enum {
    STATE_LAUNCH,
    STATE_DEALING,
    STATE_PLAYER_TURN,
    STATE_DEALER_TURN,  // also used as input-blocking pause on bust
    STATE_RESULT
} GameState;

typedef enum {
    RESULT_NONE,
    RESULT_WIN,
    RESULT_LOSE,
    RESULT_TIE,
    RESULT_BUST,
    RESULT_DEALER_BUST,
    RESULT_BLACKJACK
} ResultType;

// ═════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═════════════════════════════════════════════════════════

static Window   *s_win;
static Layer    *s_layer;
static AppTimer *s_timer;

// Suit bitmaps. Index 0=Clubs, 1=Diamonds, 2=Hearts, 3=Spades.
// On colour platforms, indices 1 and 2 hold the red variants.
static GBitmap *s_suit_bmp[4];

static Card s_deck[DECK_SIZE];
static int  s_deck_top;

static Card s_player[MAX_HAND];  static int s_pn;
static Card s_dealer[MAX_HAND];  static int s_dn;
static bool s_hole_shown;

static GameState  s_state;
static ResultType s_result;
static int        s_score;
static int        s_wins;    // lifetime win count, persisted
static int        s_losses;  // lifetime loss count, persisted
static int        s_ties;    // lifetime tie count, persisted
static bool       s_show_overlay;
static bool       s_board_peek;
static int        s_deal_step;

// Forward declarations
static void update_clicks(void);

// ═════════════════════════════════════════════════════════
//  TICK HANDLER
// ═════════════════════════════════════════════════════════

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(s_layer);
}

// ═════════════════════════════════════════════════════════
//  DECK
// ═════════════════════════════════════════════════════════

static void deck_shuffle(void) {
    for (int i = 0; i < DECK_SIZE; i++) {
        s_deck[i].rank = (i % 13) + 1;
        s_deck[i].suit = i / 13;
    }
    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card tmp = s_deck[i]; s_deck[i] = s_deck[j]; s_deck[j] = tmp;
    }
    s_deck_top = 0;
}

static Card deck_draw(void) {
    if (s_deck_top >= DECK_SIZE - RESHUFFLE_AT) deck_shuffle();
    return s_deck[s_deck_top++];
}

// ═════════════════════════════════════════════════════════
//  HAND VALUE
// ═════════════════════════════════════════════════════════

// Aces start at 11 and drop to 1 as needed to avoid a bust.
static int hand_val(Card *h, int n) {
    int v = 0, aces = 0;
    for (int i = 0; i < n; i++) {
        int r = h[i].rank;
        if      (r == 1)  { v += 11; aces++; }
        else if (r >= 10)   v += 10;
        else                v += r;
    }
    while (v > 21 && aces > 0) { v -= 10; aces--; }
    return v;
}

// ═════════════════════════════════════════════════════════
//  CARD RENDERING
// ═════════════════════════════════════════════════════════

static void draw_card(GContext *ctx, Card c, int x, int y, bool face_down) {
    GRect bounds = GRect(x, y, CARD_W, CARD_H);

    if (face_down) {
        // Card back: solid black with recessed white border
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, bounds, 3, GCornersAll);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_round_rect(ctx,
            GRect(x + 3, y + 3, CARD_W - 6, CARD_H - 6), 2);
        return;
    }

    // Card face: white with black rounded border
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 3, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_round_rect(ctx, bounds, 3);

    // Rank string
    char rank[3] = {0};
    switch (c.rank) {
        case  1: rank[0] = 'A'; break;
        case 11: rank[0] = 'J'; break;
        case 12: rank[0] = 'Q'; break;
        case 13: rank[0] = 'K'; break;
        default: snprintf(rank, sizeof(rank), "%d", c.rank); break;
    }

    // Rank colour: red for Hearts (2) and Diamonds (1) on colour displays,
    // black everywhere else.
#ifdef PBL_COLOR
    GColor rank_color = (c.suit == 1 || c.suit == 2) ? GColorRed : GColorBlack;
#else
    GColor rank_color = GColorBlack;
#endif

    // Larger card font on emery to match the bigger card size
#ifdef PBL_PLATFORM_EMERY
    GFont card_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#else
    GFont card_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#endif

    graphics_context_set_text_color(ctx, rank_color);
    graphics_draw_text(ctx, rank, card_font,
        GRect(x + 2, y + 1, CARD_W - 4, CARD_RANK_H),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // Suit bitmap — centred horizontally using platform-specific offsets.
    // GCompOpSet: draws black/red bitmap pixels, treats white as transparent.
    // This works whether the PNG background is white or transparent.
    if (s_suit_bmp[c.suit]) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[c.suit],
            GRect(x + SUIT_BMP_DX, y + SUIT_BMP_DY, SUIT_SZ, SUIT_SZ));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }
}

// ═════════════════════════════════════════════════════════
//  MAIN DRAW CALLBACK
// ═════════════════════════════════════════════════════════

static void layer_update(Layer *layer, GContext *ctx) {

    // Font set — larger on emery to match the bigger screen
#ifdef PBL_PLATFORM_EMERY
    GFont sm  = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    GFont smb = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GFont md  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont lg  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#else
    GFont sm  = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    GFont smb = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    GFont md  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GFont lg  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#endif

    // Background and foreground colours.
    // On colour platforms: casino felt green background, white text/lines.
    // On B&W platforms:    white background, black text/lines.
    // Cards always stay white with black content — they pop off the green.
    // Clock bar and BLACKJACK footer keep their explicit black fills.
#ifdef PBL_COLOR
    GColor bg_color = GColorDarkGreen;  // darkest casino-felt green (#005500)
    GColor fg_color = GColorWhite;
#else
    GColor bg_color = GColorWhite;
    GColor fg_color = GColorBlack;
#endif
    graphics_context_set_fill_color(ctx, bg_color);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

    // ── Clock bar (top) ───────────────────────────────────
    // Black bar. Time is nudged to y=-5 so it sits visually centred
    // in the bar with equal space above and below the digits.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, TOP_BAR_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        char tbuf[6];
        strftime(tbuf, sizeof(tbuf), "%H:%M", tm_now);
        graphics_draw_text(ctx, tbuf, md,
            GRect(0, CLOCK_Y_OFF, SCREEN_W, TOP_BAR_H + CLOCK_H_ADD),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // ── BLACKJACK footer (bottom) ─────────────────────────
    // Black bar. Text rect starts at the bar top (no +1 offset) so the
    // label sits visually centred rather than pushed toward the bottom edge.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx,
        GRect(0, SCREEN_H - BOT_BAR_H, SCREEN_W, BOT_BAR_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "BLACKJACK", smb,
        GRect(0, SCREEN_H - BOT_BAR_H - 1, SCREEN_W, BOT_BAR_H),
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // ── Launch screen ─────────────────────────────────────
    if (s_state == STATE_LAUNCH) {
        graphics_context_set_text_color(ctx, fg_color);

        // "BLACKJACK" title — centred, bold, GOTHIC_28_BOLD
        int title_y = TOP_BAR_H + 1;
        graphics_draw_text(ctx, "BLACKJACK", lg,
            GRect(0, title_y, SCREEN_W, LAUNCH_TITLE_H),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Four suit bitmaps centred horizontally below the title.
        // Total bitmap row width = 4×20 + 3×8 = 104px → left margin = (SCREEN_W−104)/2
        int sx = (SCREEN_W - 104) / 2;
        int sy = title_y + LAUNCH_SY_OFF;
        for (int i = 0; i < 4; i++) {
            if (s_suit_bmp[i]) {
                graphics_context_set_compositing_mode(ctx, GCompOpSet);
                graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[i],
                    GRect(sx + i * (SUIT_SZ + 8), sy, SUIT_SZ, SUIT_SZ));
                graphics_context_set_compositing_mode(ctx, GCompOpAssign);
            }
        }

        // Prompt box. LAUNCH_BOX_X controls left/right margins — a smaller
        // value gives a wider box so "PRESS ANY BUTTON" fits on one line.
        int bx = LAUNCH_BOX_X;
        int bw = SCREEN_W - bx * 2;
        int by = sy + SUIT_SZ + LAUNCH_SY_GAP;
        graphics_context_set_stroke_color(ctx, fg_color);
        graphics_draw_rect(ctx, GRect(bx, by, bw, LAUNCH_BOX_H));
        graphics_draw_text(ctx, "PRESS ANY BUTTON\nTO DEAL", smb,
            GRect(bx + 4, by + 2, bw - 8, LAUNCH_BOX_H - 4),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Credits
        graphics_draw_text(ctx,
            "Created by:\nPersistent Productions\nJoel Penton",
            sm, GRect(4, by + LAUNCH_BOX_H + LAUNCH_CR_GAP, SCREEN_W - 8, LAUNCH_CR_H),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        return;
    }

    // ── Game board ────────────────────────────────────────
    int y = TOP_BAR_H;

#ifdef PBL_PLATFORM_EMERY
    GFont board_label = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont board_total = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#else
    GFont board_label = sm;
    GFont board_total = smb;
#endif

    // DEALER label — plain; bold total beside it stands out
    graphics_context_set_text_color(ctx, fg_color);
    graphics_draw_text(ctx, "DEALER", board_label,
        GRect(CARD_X, y, 80, ROW_H + 4),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    {   // Dealer total
        char dt[12] = "";
        if (s_dn > 0) {
            if (!s_hole_shown) {
                int v1 = (s_dealer[0].rank >= 10) ? 10 :
                         (s_dealer[0].rank ==  1) ? 11 :
                          s_dealer[0].rank;
                snprintf(dt, sizeof(dt), "%d+?", v1);
            } else {
                snprintf(dt, sizeof(dt), "%d", hand_val(s_dealer, s_dn));
            }
        }
        graphics_draw_text(ctx, dt, board_total,
            GRect(CARD_X + 70, y, 80, ROW_H + 4),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }

    // ^HIT hint — top-right, only while SELECT held in player turn
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_context_set_text_color(ctx, fg_color);
        graphics_draw_text(ctx, "^HIT", sm,
            GRect(0, y, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    y += ROW_H + LABEL_CARD_GAP;

    // Dealer cards — left-aligned from CARD_X, dynamic fan compression
    {
        int gap  = (SCREEN_W - CARD_X - CARD_W) / (s_dn > 1 ? s_dn - 1 : 1);
        int step = (s_dn > 1 && gap < CARD_STEP) ? gap : CARD_STEP;
        for (int i = 0; i < s_dn; i++) {
            bool is_hole = (!s_hole_shown && i == 1);
            draw_card(ctx, s_dealer[i], CARD_X + i * step, y, is_hole);
        }
    }

    y += CARD_H + CARD_DIV_GAP;

    // Divider
    graphics_context_set_stroke_color(ctx, fg_color);
    graphics_draw_line(ctx, GPoint(0, y), GPoint(SCREEN_W - 1, y));
    y += 3;

    // PLAYER label — plain; total bold (mirrors dealer row)
    graphics_context_set_text_color(ctx, fg_color);
    graphics_draw_text(ctx, "PLAYER", board_label,
        GRect(CARD_X, y, 80, ROW_H + 4),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    if (s_pn > 0) {
        char pt[8];
        snprintf(pt, sizeof(pt), "%d", hand_val(s_player, s_pn));
        graphics_draw_text(ctx, pt, board_total,
            GRect(CARD_X + 70, y, 80, ROW_H + 4),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }

    y += ROW_H + LABEL_CARD_GAP;

    // Player cards — left-aligned from CARD_X, dynamic fan compression
    {
        int gap  = (SCREEN_W - CARD_X - CARD_W) / (s_pn > 1 ? s_pn - 1 : 1);
        int step = (s_pn > 1 && gap < CARD_STEP) ? gap : CARD_STEP;
        for (int i = 0; i < s_pn; i++) {
            draw_card(ctx, s_player[i], CARD_X + i * step, y, false);
        }
    }

    // vSTAY hint — bottom-right just above footer, only while SELECT held
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_context_set_text_color(ctx, fg_color);
        graphics_draw_text(ctx, "vSTAY", sm,
            GRect(0, SCREEN_H - BOT_BAR_H - ROW_H - 1, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    // ── Result overlay ────────────────────────────────────
    // Hidden during board peek (SELECT held on result screen).
    if (s_state == STATE_RESULT && !s_board_peek) {
        const char *rs, *ps;
        switch (s_result) {
            case RESULT_WIN:         rs = "WIN";       ps = "+100"; break;
            case RESULT_BLACKJACK:   rs = "BLACKJACK"; ps = "+100"; break;
            case RESULT_DEALER_BUST: rs = "WIN";       ps = "+100"; break;
            case RESULT_LOSE:        rs = "LOSE";      ps = "-60";  break;
            case RESULT_BUST:        rs = "BUST";      ps = "-60";  break;
            case RESULT_TIE:         rs = "TIE";       ps = "+0";   break;
            default:                 rs = "";           ps = "";     break;
        }

        // Box covers the full game area — starts 1px below the clock bar,
        // ends 1px above the footer, so no game content bleeds through.
        int bx = 4;
        int by = TOP_BAR_H + 1;
        int bw = SCREEN_W - 8;
        int bh = SCREEN_H - BOT_BAR_H - by - 1;

        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);

        // Line 1 — result word and score delta on the same line
        char header[26];
        snprintf(header, sizeof(header), "%s  %s", rs, ps);
        GFont rf = (s_result == RESULT_BLACKJACK) ? md : lg;
        graphics_draw_text(ctx, header, rf,
            GRect(bx + 2, by + RES_HEADER_Y, bw - 4, RES_HEADER_H),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Fixed two-column layout — label left, bold count at fixed column
        int inner_x = bx + (bw - RES_INNER_W) / 2;

        char wn[8], tn[8], ln[8];
        snprintf(wn, sizeof(wn), "%d", s_wins);
        snprintf(tn, sizeof(tn), "%d", s_ties);
        snprintf(ln, sizeof(ln), "%d", s_losses);

        graphics_draw_text(ctx, "Wins:", sm,
            GRect(inner_x, by + RES_WTL_Y1, 74, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, wn, smb,
            GRect(inner_x + 78, by + RES_WTL_Y1, 30, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        graphics_draw_text(ctx, "Ties:", sm,
            GRect(inner_x, by + RES_WTL_Y1 + RES_WTL_STEP, 74, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, tn, smb,
            GRect(inner_x + 78, by + RES_WTL_Y1 + RES_WTL_STEP, 30, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        graphics_draw_text(ctx, "Losses:", sm,
            GRect(inner_x, by + RES_WTL_Y1 + RES_WTL_STEP * 2, 74, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, ln, smb,
            GRect(inner_x + 78, by + RES_WTL_Y1 + RES_WTL_STEP * 2, 30, RES_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        // Hand totals — left-aligned at same inner_x column
        char ds[16], ps2[16];
        snprintf(ds,  sizeof(ds),  "Dealer: %d", hand_val(s_dealer, s_dn));
        snprintf(ps2, sizeof(ps2), "Player: %d", hand_val(s_player, s_pn));
        graphics_draw_text(ctx, ds, sm,
            GRect(inner_x, by + RES_TOTALS_Y, RES_INNER_W, RES_WTL_STEP),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, ps2, sm,
            GRect(inner_x, by + RES_TOTALS_Y + RES_WTL_STEP, RES_INNER_W, RES_WTL_STEP),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

        // Deal prompt
        graphics_draw_text(ctx, "PRESS UP OR DOWN\nTO DEAL", smb,
            GRect(bx + 2, by + RES_PROMPT_Y, bw - 4, RES_PROMPT_H),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // ── Score overlay (SELECT held in player turn) ────────
    if (s_show_overlay && s_state == STATE_PLAYER_TURN) {
        int bw  = SCREEN_W - 16;
        int bh  = SCR_OVL_BH;
        int bx2 = (SCREEN_W - bw) / 2;
        int bby = (SCREEN_H - bh) / 2;
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx2, bby, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);

        // "Total Score: N" on one line
        char sc_str[24];
        snprintf(sc_str, sizeof(sc_str), "Total Score: %d", s_score);
        graphics_draw_text(ctx, sc_str, smb,
            GRect(bx2 + 4, bby + 4, bw - 8, SCR_HEADER_H),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Same fixed two-column layout as result overlay
        int ow = RES_INNER_W;
        int ox = bx2 + (bw - ow) / 2;

        char wn2[8], tn2[8], ln2[8];
        snprintf(wn2, sizeof(wn2), "%d", s_wins);
        snprintf(tn2, sizeof(tn2), "%d", s_ties);
        snprintf(ln2, sizeof(ln2), "%d", s_losses);

        graphics_draw_text(ctx, "Wins:", sm,
            GRect(ox, bby + SCR_WTL_Y1, 74, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, wn2, smb,
            GRect(ox + 78, bby + SCR_WTL_Y1, 30, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        graphics_draw_text(ctx, "Ties:", sm,
            GRect(ox, bby + SCR_WTL_Y1 + SCR_WTL_STEP, 74, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, tn2, smb,
            GRect(ox + 78, bby + SCR_WTL_Y1 + SCR_WTL_STEP, 30, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        graphics_draw_text(ctx, "Losses:", sm,
            GRect(ox, bby + SCR_WTL_Y1 + SCR_WTL_STEP * 2, 74, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, ln2, smb,
            GRect(ox + 78, bby + SCR_WTL_Y1 + SCR_WTL_STEP * 2, 30, SCR_WTL_STEP),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
}

// ═════════════════════════════════════════════════════════
//  GAME LOGIC
// ═════════════════════════════════════════════════════════

static void game_evaluate(void) {
    int p = hand_val(s_player, s_pn);
    int d = hand_val(s_dealer, s_dn);
    APP_LOG(APP_LOG_LEVEL_INFO, "Evaluating: player=%d dealer=%d", p, d);

    if (d > 21) {
        s_result = RESULT_DEALER_BUST; s_score += SCORE_WIN; s_wins++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: DEALER BUST");
    } else if (p > d) {
        s_result = (s_pn == 2 && p == 21) ? RESULT_BLACKJACK : RESULT_WIN;
        s_score += SCORE_WIN; s_wins++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: %s",
            s_result == RESULT_BLACKJACK ? "BLACKJACK" : "WIN");
    } else if (d > p) {
        s_result = RESULT_LOSE; s_score += SCORE_LOSE; s_losses++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: LOSE");
    } else {
        s_result = RESULT_TIE; s_ties++;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: TIE");
    }

    persist_write_int(PERSIST_KEY,      s_score);
    persist_write_int(PERSIST_KEY_WINS, s_wins);
    persist_write_int(PERSIST_KEY_LOSS, s_losses);
    persist_write_int(PERSIST_KEY_TIES, s_ties);
    s_state = STATE_RESULT;
    update_clicks();
    layer_mark_dirty(s_layer);
}

static void dealer_done_cb(void *data) {
    s_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Dealer done — evaluating");
    game_evaluate();
}

static void bust_cb(void *data) {
    s_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Bust pause complete");
    s_state = STATE_RESULT;
    update_clicks();
    layer_mark_dirty(s_layer);
}

// Dealer hits one card at a time. Checks the new total immediately
// after drawing to avoid an extra-second pause after the final card.
// Dealer stands on ALL 17s (including soft 17).
static void dealer_cb(void *data) {
    s_timer = NULL;
    int current = hand_val(s_dealer, s_dn);
    APP_LOG(APP_LOG_LEVEL_INFO, "dealer_cb: total=%d cards=%d", current, s_dn);

    if (current < 17) {
        s_dealer[s_dn++] = deck_draw();
        int new_total = hand_val(s_dealer, s_dn);
        APP_LOG(APP_LOG_LEVEL_INFO, "Dealer drew — new total=%d", new_total);
        layer_mark_dirty(s_layer);
        if (new_total >= 17) {
            s_timer = app_timer_register(DEALER_END_MS, dealer_done_cb, NULL);
        } else {
            s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
        }
    } else {
        APP_LOG(APP_LOG_LEVEL_INFO, "Dealer already at %d on reveal", current);
        game_evaluate();
    }
}

static void player_stand(void) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Player stands at %d", hand_val(s_player, s_pn));
    s_state        = STATE_DEALER_TURN;
    s_hole_shown   = true;
    s_show_overlay = false;
    update_clicks();
    layer_mark_dirty(s_layer);
    s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
}

static void player_hit(void) {
    if (s_pn >= MAX_HAND) return;
    s_player[s_pn++] = deck_draw();
    int v = hand_val(s_player, s_pn);
    APP_LOG(APP_LOG_LEVEL_INFO, "Player hit: total=%d cards=%d", v, s_pn);

    if (v > 21) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Player busted");
        s_result     = RESULT_BUST;
        s_score     += SCORE_LOSE;
        s_losses++;
        persist_write_int(PERSIST_KEY,      s_score);
        persist_write_int(PERSIST_KEY_LOSS, s_losses);
        s_hole_shown = true;           // reveal dealer hand during pause
        s_state      = STATE_DEALER_TURN;
        update_clicks();
        layer_mark_dirty(s_layer);
        s_timer = app_timer_register(BUST_PAUSE_MS, bust_cb, NULL);
    } else if (v == 21) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Hit 21 — auto-standing");
        player_stand();
    } else {
        layer_mark_dirty(s_layer);
    }
}

// Deals 4 opening cards: player[0] → dealer up → player[1] → dealer hole
static void deal_cb(void *data) {
    s_timer = NULL;
    switch (s_deal_step) {
        case 0: s_player[s_pn++] = deck_draw(); break;
        case 1: s_dealer[s_dn++] = deck_draw(); break;
        case 2: s_player[s_pn++] = deck_draw(); break;
        case 3: s_dealer[s_dn++] = deck_draw(); break;
    }
    s_deal_step++;
    layer_mark_dirty(s_layer);

    if (s_deal_step < 4) {
        s_timer = app_timer_register(DEAL_MS, deal_cb, NULL);
    } else {
        int pv = hand_val(s_player, s_pn);
        APP_LOG(APP_LOG_LEVEL_INFO, "Deal complete: player=%d", pv);
        if (pv == 21) {
            APP_LOG(APP_LOG_LEVEL_INFO, "Natural 21 — auto-standing");
            player_stand();
        } else {
            s_state = STATE_PLAYER_TURN;
            update_clicks();
            layer_mark_dirty(s_layer);
        }
    }
}

static void start_hand(void) {
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    APP_LOG(APP_LOG_LEVEL_INFO, "--- New hand ---");
    s_pn           = 0;
    s_dn           = 0;
    s_hole_shown   = false;
    s_result       = RESULT_NONE;
    s_show_overlay = false;
    s_board_peek   = false;
    s_deal_step    = 0;
    s_state        = STATE_DEALING;
    update_clicks();
    layer_mark_dirty(s_layer);
    s_timer = app_timer_register(FIRST_MS, deal_cb, NULL);
}

// ═════════════════════════════════════════════════════════
//  BUTTON HANDLERS
// ═════════════════════════════════════════════════════════

static void btn_up(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;
    if (s_state == STATE_LAUNCH) { start_hand(); return; }
    if (s_state == STATE_RESULT && !s_board_peek) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN && hand_val(s_player, s_pn) < 21) player_hit();
}

static void btn_down(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;
    if (s_state == STATE_LAUNCH) { start_hand(); return; }
    if (s_state == STATE_RESULT && !s_board_peek) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN) player_stand();
}

static void btn_sel_dn(ClickRecognizerRef r, void *ctx) {
    if (s_state == STATE_LAUNCH) { start_hand(); return; }
    if (s_state == STATE_RESULT) {
        s_board_peek = true;
        layer_mark_dirty(s_layer);
        return;
    }
    if (s_state == STATE_PLAYER_TURN) {
        s_show_overlay = true;
        layer_mark_dirty(s_layer);
    }
}

static void btn_sel_up(ClickRecognizerRef r, void *ctx) {
    if (s_board_peek)    { s_board_peek    = false; layer_mark_dirty(s_layer); return; }
    if (s_show_overlay)  { s_show_overlay  = false; layer_mark_dirty(s_layer); }
}

// ═════════════════════════════════════════════════════════
//  CLICK CONFIG MANAGEMENT
//
//  An empty provider during animation states (DEALING, DEALER_TURN)
//  causes the OS to discard button events rather than queue them.
//  Without this, rapid presses during an animation fire all at once
//  when the next interactive state begins.
// ═════════════════════════════════════════════════════════

static void click_provider_blocked(void *ctx) { (void)ctx; }

static void click_provider_active(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP,   btn_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, btn_down);
    window_raw_click_subscribe(BUTTON_ID_SELECT, btn_sel_dn, btn_sel_up, ctx);
}

static void update_clicks(void) {
    bool interactive = (s_state == STATE_LAUNCH     ||
                        s_state == STATE_PLAYER_TURN ||
                        s_state == STATE_RESULT);
    window_set_click_config_provider(s_win,
        interactive ? click_provider_active : click_provider_blocked);
}

// ═════════════════════════════════════════════════════════
//  WINDOW LIFECYCLE
// ═════════════════════════════════════════════════════════

static void win_load(Window *w) {
    Layer *root = window_get_root_layer(w);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);

    // Load suit bitmaps.
    // _color variants: transparent background, targeted to colour platforms
    //                  (basalt, chalk, emery, gabbro) in CloudPebble.
    // Plain variants:  white background, targeted to B&W platforms
    //                  (aplite, diorite, flint) in CloudPebble.
    // Index: 0=Clubs, 1=Diamonds, 2=Hearts, 3=Spades.
#ifdef PBL_COLOR
    s_suit_bmp[0] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_CLUB_COLOR);
    s_suit_bmp[1] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_DIAMOND_COLOR);
    s_suit_bmp[2] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_HEART_COLOR);
    s_suit_bmp[3] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_SPADE_COLOR);
#else
    s_suit_bmp[0] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_CLUB);
    s_suit_bmp[1] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_DIAMOND);
    s_suit_bmp[2] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_HEART);
    s_suit_bmp[3] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_SPADE);
#endif

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    srand((unsigned)time(NULL));
    deck_shuffle();

    s_score   = persist_read_int(PERSIST_KEY);
    s_wins    = persist_read_int(PERSIST_KEY_WINS);
    s_losses  = persist_read_int(PERSIST_KEY_LOSS);
    s_ties    = persist_read_int(PERSIST_KEY_TIES);
    s_state        = STATE_LAUNCH;
    s_show_overlay = false;
    s_board_peek   = false;
    s_timer        = NULL;

    APP_LOG(APP_LOG_LEVEL_INFO, "Blackjack loaded. Score: %d  Platform: %dx%d",
        s_score, SCREEN_W, SCREEN_H);
}

static void win_unload(Window *w) {
    tick_timer_service_unsubscribe();
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    persist_write_int(PERSIST_KEY,      s_score);
    persist_write_int(PERSIST_KEY_WINS, s_wins);
    persist_write_int(PERSIST_KEY_LOSS, s_losses);
    persist_write_int(PERSIST_KEY_TIES, s_ties);
    APP_LOG(APP_LOG_LEVEL_INFO, "Blackjack unloaded. Final score: %d", s_score);
    for (int i = 0; i < 4; i++) {
        if (s_suit_bmp[i]) { gbitmap_destroy(s_suit_bmp[i]); s_suit_bmp[i] = NULL; }
    }
    layer_destroy(s_layer);
}

// ═════════════════════════════════════════════════════════
//  APP ENTRY POINTS
// ═════════════════════════════════════════════════════════

static void init(void) {
    s_win = window_create();
    window_set_click_config_provider(s_win, click_provider_active);
    window_set_window_handlers(s_win, (WindowHandlers){
        .load   = win_load,
        .unload = win_unload,
    });
    window_stack_push(s_win, true);
}

static void deinit(void) {
    window_destroy(s_win);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
