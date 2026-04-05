// ═════════════════════════════════════════════════════════
//  BLACKJACK — Pebble Watchapp
//  Platform : flint (Pebble 2 Duo, 144×168 px, B&W e-paper)
//  SDK      : Pebble C SDK 3, built via CloudPebble
//  Author   : Joel Penton / Persistent Productions
// ═════════════════════════════════════════════════════════
//
//  GAME RULES
//  ──────────
//  · Single 52-card deck. When fewer than RESHUFFLE_AT (15) cards
//    remain the deck is silently reshuffled mid-shoe.
//  · Dealer stands on ALL 17s — including soft 17 (Ace + 6).
//    This follows Planet Hollywood high-limit salon rules and is
//    more favourable to the player than the common H17 rule.
//  · Aces count as 11 and drop to 1 if they would cause a bust.
//  · A natural blackjack (21 on exactly 2 cards) auto-stands and
//    is distinguished from a non-natural 21 on the result screen.
//  · No betting, no doubling down, no splitting, no surrender.
//
//  CONTROLS
//  ────────
//  Launch screen    Any button        → deal first hand
//  Player turn      UP                → hit (draw one card)
//                   DOWN              → stand (end player turn)
//                   SELECT (hold)     → show running score + button hints
//                   SELECT (release)  → dismiss score overlay
//  Result screen    UP or DOWN        → deal new hand
//                   SELECT (hold)     → peek at the final board state
//                   SELECT (release)  → return to result overlay
//  Always           BACK              → exit app
//
//  SCORING  (persisted to watch storage, survives app restarts)
//  ────────
//  Win / Blackjack / Dealer bust  +100
//  Lose / Player bust             − 60
//  Tie                              ±0
//
//  PROJECT STRUCTURE
//  ─────────────────
//  Everything lives in this single .c file.
//  Resources (suit PNGs + menu icon) are registered in CloudPebble
//  and referenced via RESOURCE_ID_* constants generated at build time.
//  Suit PNGs must be exported from Aseprite (or equivalent) with a
//  solid WHITE background — not transparent — and set to
//  "Smallest Palette" memory format in the CloudPebble resource editor.
//  A transparent background compiles to solid black on the 1-bit
//  display, making the entire bitmap appear as a filled rectangle.
// ═════════════════════════════════════════════════════════

#include <pebble.h>

// ─────────────────────────────────────────────────────────
//  Display constants
//  flint = Pebble 2 Duo — 144 × 168 px, black-and-white e-paper
// ─────────────────────────────────────────────────────────
#define SCREEN_W     144   // display width in pixels
#define SCREEN_H     168   // display height in pixels

// ─────────────────────────────────────────────────────────
//  UI chrome heights
// ─────────────────────────────────────────────────────────
#define TOP_BAR_H     16   // black clock bar at the very top
#define BOT_BAR_H     17   // black "BLACKJACK" title bar at the very bottom
#define ROW_H         14   // height of the DEALER / PLAYER label rows

// ─────────────────────────────────────────────────────────
//  Card geometry
// ─────────────────────────────────────────────────────────
#define CARD_W        32   // card width  (px)
#define CARD_H        49   // card height (px)
#define CARD_STEP     22   // horizontal distance between successive cards
                           // (< CARD_W so cards fan/overlap left-to-right)
#define CARD_X         4   // left margin for all card rows
#define SUIT_SZ       20   // suit bitmap size — must match the PNG resources (20×20)

// ─────────────────────────────────────────────────────────
//  Pixel layout — game board (all 168 rows accounted for)
//
//   Row  0 – 16   black clock bar          (TOP_BAR_H = 16)
//   Row 16 – 30   DEALER label row         (ROW_H = 14)
//   Row 30 – 31   1 px gap
//   Row 31 – 80   dealer card area         (CARD_H = 49)
//   Row 80 – 82   2 px gap
//   Row 82        horizontal divider line
//   Row 82 – 85   3 px gap
//   Row 85 – 99   PLAYER label row         (ROW_H = 14)
//   Row 99 – 100  1 px gap
//   Row 100 – 149 player card area         (CARD_H = 49)
//   Row 149 – 151 2 px gap
//   Row 151 – 168 black BLACKJACK footer   (BOT_BAR_H = 17)
//
//   Total: 16+14+1+49+2+1+2+14+1+49+2+17 = 168 ✓
//
//  Launch screen layout (within the same chrome):
//   Row  18 – 38   four suit bitmaps (SUIT_SZ = 20)
//   Row  42 – 80   prompt box with 5 px inner padding top + bottom
//   Row  86 – 150  credits text (GOTHIC_14, up to 4 wrapped lines × 16 px)
//   Row 151 – 168  BLACKJACK footer (same as game board)
// ─────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────
//  Deck / hand limits
// ─────────────────────────────────────────────────────────
#define DECK_SIZE     52   // standard single deck
#define RESHUFFLE_AT  15   // reshuffle when this many cards or fewer remain
                           // prevents late-shoe bias without a visible cut card
#define MAX_HAND      11   // maximum cards in one hand
                           // (10+A is a 2-card 21; 7-card 21 is the practical max)

// ─────────────────────────────────────────────────────────
//  Animation timing
// ─────────────────────────────────────────────────────────
#define FIRST_MS      400  // ms before the very first card appears —
                           // short pause lets the screen clear after dealing
#define DEAL_MS      1000  // ms between each subsequent card during the
                           // opening deal and the dealer's hit sequence
#define DEALER_END_MS 600  // ms between the dealer's final card appearing
                           // and the result overlay painting over it —
                           // gives the player a moment to see the last card
                           // without the confusing full DEAL_MS wait
#define BUST_PAUSE_MS 800  // ms the busting card is visible before the
                           // result overlay appears — same rationale as above

// ─────────────────────────────────────────────────────────
//  Scoring
// ─────────────────────────────────────────────────────────
#define SCORE_WIN    100
#define SCORE_LOSE   -60
#define PERSIST_KEY   42   // arbitrary storage key; must stay constant between
                           // app versions or saved scores will be lost

// ═════════════════════════════════════════════════════════
//  TYPES
// ═════════════════════════════════════════════════════════

// A single playing card.
//   rank : 1 = Ace, 2–10 = pip value, 11 = Jack, 12 = Queen, 13 = King
//   suit : 0 = Clubs, 1 = Diamonds, 2 = Hearts, 3 = Spades
//          This index also maps directly to the s_suit_bmp[] resource array.
typedef struct {
    int rank;
    int suit;
} Card;

// ─────────────────────────────────────────────────────────
//  Game state machine
//
//  Allowed transitions:
//    LAUNCH      → DEALING      (any button pressed)
//    DEALING     → PLAYER_TURN  (all 4 opening cards dealt, no natural 21)
//    DEALING     → DEALER_TURN  (natural 21 on the deal — auto-stand)
//    PLAYER_TURN → DEALER_TURN  (player stands, or hits to exactly 21)
//    PLAYER_TURN → DEALER_TURN* (player busts — used as blocking pause state)
//    DEALER_TURN → RESULT       (dealer reaches 17+ or busts)
//    RESULT      → DEALING      (UP or DOWN pressed)
//
//  * When the player busts we set STATE_DEALER_TURN (not STATE_RESULT)
//    so that the busting card stays on screen for BUST_PAUSE_MS before
//    the result overlay appears. STATE_DEALER_TURN blocks all player input
//    without any extra machinery.
// ─────────────────────────────────────────────────────────
typedef enum {
    STATE_LAUNCH,       // title screen — four suit symbols + deal prompt
    STATE_DEALING,      // opening 4-card deal animation in progress
    STATE_PLAYER_TURN,  // waiting for UP (hit) or DOWN (stand)
    STATE_DEALER_TURN,  // dealer playing out automatically (or bust pause)
    STATE_RESULT        // hand complete — result overlay displayed
} GameState;

// Outcome of a completed hand.
// Determines the result word, point delta, and which score is applied.
typedef enum {
    RESULT_NONE,         // hand not yet finished (default / in-progress value)
    RESULT_WIN,          // player total > dealer total, no blackjack
    RESULT_LOSE,         // dealer total > player total
    RESULT_TIE,          // equal totals — push, no score change
    RESULT_BUST,         // player total exceeded 21
    RESULT_DEALER_BUST,  // dealer total exceeded 21 — player wins
    RESULT_BLACKJACK     // player reached 21 on exactly 2 cards
} ResultType;

// ═════════════════════════════════════════════════════════
//  GLOBAL STATE
//  All variables are file-scoped (static). The Pebble SDK is
//  single-threaded so no synchronisation is needed.
// ═════════════════════════════════════════════════════════

static Window   *s_win;    // the single application window
static Layer    *s_layer;  // full-screen custom drawing layer
static AppTimer *s_timer;  // shared one-shot timer — only one fires at a time

// Suit bitmaps loaded from PNG resources at window creation.
// Index matches Card.suit: [0]=Clubs [1]=Diamonds [2]=Hearts [3]=Spades
// PNG files must have a solid WHITE background and "Smallest Palette"
// memory format in CloudPebble (see file header note for details).
static GBitmap *s_suit_bmp[4];

// Deck storage and cursor.
static Card s_deck[DECK_SIZE];
static int  s_deck_top;     // index of the next card to be drawn;
                            // reset to 0 after every shuffle

// Player and dealer hands.
// s_pn and s_dn are the card counts for each hand.
static Card s_player[MAX_HAND];  static int s_pn;
static Card s_dealer[MAX_HAND];  static int s_dn;

// Controls whether the dealer's hole card (index 1) is shown face-up.
// Stays false until the player stands or busts, then set to true so
// the dealer's full hand is revealed as they play out their turn.
static bool s_hole_shown;

static GameState  s_state;   // current position in the state machine
static ResultType s_result;  // outcome of the most recently completed hand

// Running total score — loaded from persistent storage on launch,
// written back after every hand and again when the app exits.
static int s_score;

// True while SELECT is physically held during STATE_PLAYER_TURN.
// Triggers the score box + ^HIT / vSTAY hints overlay.
// False as soon as SELECT is released.
static bool s_show_overlay;

// True while SELECT is physically held during STATE_RESULT.
// Hides the result overlay so the player can see the final board.
// UP / DOWN are ignored during a board peek to prevent accidental deals.
// False as soon as SELECT is released.
static bool s_board_peek;

// Counts which opening card we are about to deal (0–3).
// Resets to 0 in start_hand().
// Deal order: player[0] → dealer[0] (up) → player[1] → dealer[1] (hole)
static int s_deal_step;

// Forward declaration — update_clicks() is defined after the button
// handlers because it references click_provider_active and
// click_provider_blocked which in turn reference the handler functions.
// Placing the forward declaration here lets the game-logic functions
// call update_clicks() without having to reorder the file.
static void update_clicks(void);

// ═════════════════════════════════════════════════════════
//  TICK HANDLER
// ═════════════════════════════════════════════════════════

// Called once per minute by the Pebble tick timer service.
// Marks the layer dirty so the clock bar repaints with the updated time.
// Subscribing to MINUTE_UNIT (rather than SECOND_UNIT) keeps battery
// usage minimal — e-paper only redraws when told to.
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(s_layer);
}

// ═════════════════════════════════════════════════════════
//  DECK MANAGEMENT
// ═════════════════════════════════════════════════════════

// Populate the deck with all 52 cards then apply a Fisher-Yates
// in-place shuffle (O(n), unbiased).
// Called once on launch, and automatically by deck_draw() whenever
// fewer than RESHUFFLE_AT cards remain — the reshuffle is invisible
// to the player (no animation or notification).
static void deck_shuffle(void) {
    // Fill in order: ranks 1–13 for each of the four suits
    for (int i = 0; i < DECK_SIZE; i++) {
        s_deck[i].rank = (i % 13) + 1;  // 1–13
        s_deck[i].suit = i / 13;         // 0–3
    }
    // Fisher-Yates: iterate from the end, swapping each element with a
    // randomly chosen element at or before it
    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card tmp = s_deck[i];
        s_deck[i] = s_deck[j];
        s_deck[j] = tmp;
    }
    s_deck_top = 0;
}

// Draw and return the top card of the deck.
// Reshuffles automatically when the shoe runs low.
static Card deck_draw(void) {
    if (s_deck_top >= DECK_SIZE - RESHUFFLE_AT) {
        deck_shuffle();
    }
    return s_deck[s_deck_top++];
}

// ═════════════════════════════════════════════════════════
//  HAND VALUE
// ═════════════════════════════════════════════════════════

// Calculate the best (highest non-busting) total for a hand of n cards.
//
// All Aces are initially counted as 11. If the running total exceeds 21
// and we still have soft Aces, we convert one Ace from 11 → 1 (subtract
// 10) and repeat until the total is ≤ 21 or there are no more soft Aces.
//
// Examples:
//   A + 6         = 17  (soft 17)
//   A + 6 + 8     = 15  (Ace demoted from 11 to 1: 1+6+8)
//   A + A + 9     = 21  (first Ace = 11, second = 1: 11+1+9)
//   A + A + A + 8 = 21  (11+1+1+8)
static int hand_val(Card *h, int n) {
    int total = 0;
    int aces  = 0;  // number of Aces currently counted as 11
    for (int i = 0; i < n; i++) {
        int r = h[i].rank;
        if (r == 1) {
            total += 11;
            aces++;
        } else if (r >= 10) {
            total += 10;   // J, Q, K all worth 10
        } else {
            total += r;    // 2–9 face value
        }
    }
    // Demote soft Aces one at a time until we are ≤ 21 or out of soft Aces
    while (total > 21 && aces > 0) {
        total -= 10;
        aces--;
    }
    return total;
}

// ═════════════════════════════════════════════════════════
//  CARD RENDERING
// ═════════════════════════════════════════════════════════

// Draw a single card with its top-left corner at (x, y).
//
// face_down = true  → card back: solid black rounded rectangle with a
//                     recessed white inner border to suggest a back design.
// face_down = false → card face: white background, black border, rank label
//                     in the top-left corner, suit bitmap centred below it.
static void draw_card(GContext *ctx, Card c, int x, int y, bool face_down) {
    GRect bounds = GRect(x, y, CARD_W, CARD_H);

    if (face_down) {
        // Card back
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, bounds, 3, GCornersAll);
        // Inset white border gives the back a framed look
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_round_rect(ctx,
            GRect(x + 3, y + 3, CARD_W - 6, CARD_H - 6), 2);
        return;
    }

    // Card face — white fill with black rounded border
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 3, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_round_rect(ctx, bounds, 3);

    // Build the rank string: A, 2–9, 10, J, Q, K
    char rank[3] = {0};
    switch (c.rank) {
        case  1: rank[0] = 'A'; break;
        case 11: rank[0] = 'J'; break;
        case 12: rank[0] = 'Q'; break;
        case 13: rank[0] = 'K'; break;
        default: snprintf(rank, sizeof(rank), "%d", c.rank); break;
    }

    // Draw rank in the upper-left corner of the card face
    GFont card_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, rank, card_font,
        GRect(x + 2, y + 1, CARD_W - 4, 22),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // Draw the suit bitmap below the rank, horizontally centred.
    // The card is 32 px wide and the bitmap is 20 px wide, so a 6 px
    // left margin centres it: (32 − 20) / 2 = 6.
    //
    // GCompOpSet is required here (and explicitly recommended in the
    // Pebble SDK docs for graphics_draw_bitmap_in_rect):
    //   • Black pixels in the bitmap → drawn as black (the symbol)
    //   • White pixels in the bitmap → treated as transparent (card shows through)
    // Without GCompOpSet the default GCompOpAssign mode copies every
    // source pixel verbatim, meaning a white background from the PNG
    // paints over the white card face — which looks correct — but a
    // transparent background (common Aseprite default) compiles to black
    // on the 1-bit display and turns the whole bitmap rectangle solid black.
    // GCompOpSet is safe regardless of whether the PNG background is
    // white or transparent, making it the robust choice.
    if (s_suit_bmp[c.suit]) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[c.suit],
            GRect(x + 6, y + 23, SUIT_SZ, SUIT_SZ));
        // Reset to the default mode so nothing else in this draw pass
        // is accidentally affected by the compositing change
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }
}

// ═════════════════════════════════════════════════════════
//  MAIN DRAW CALLBACK
//
//  layer_update() is called by the Pebble SDK whenever layer_mark_dirty()
//  is invoked. It redraws the ENTIRE screen from scratch on every call —
//  the Pebble graphics model has no concept of dirty regions within a layer.
//
//  Drawing order (painter's algorithm — later calls paint over earlier ones):
//    1. White background
//    2. Clock bar (top) + BLACKJACK footer (bottom) — present on all screens
//    3a. Launch screen content (returns early; skips steps 4+)
//    3b. Game board: DEALER row → dealer cards → divider → PLAYER row → player cards
//    4.  Result overlay      (STATE_RESULT && !s_board_peek)
//    5.  Score/hints overlay (STATE_PLAYER_TURN && s_show_overlay)
// ═════════════════════════════════════════════════════════
static void layer_update(Layer *layer, GContext *ctx) {

    // Cache font handles for the duration of this draw pass.
    // All four are system fonts — no custom resources needed.
    GFont sm  = fonts_get_system_font(FONT_KEY_GOTHIC_14);       // body text
    GFont smb = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);  // section labels
    GFont md  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);  // clock, points
    GFont lg  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);  // result word, score

    // ── 1. White background ───────────────────────────────
    // Painting the full screen white each frame is intentional —
    // it clears any artefacts from the previous frame without needing
    // to track what changed.
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

    // ── 2a. Clock bar (top) ───────────────────────────────
    // Black bar spanning the full width. The time is drawn at y = −3
    // (one pixel above the top of the bar) so that a clean 1 px strip
    // of black is visible below the digits — without this nudge the
    // descenders of the GOTHIC_18_BOLD font bleed into the white area below.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, TOP_BAR_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        char time_buf[6];  // "HH:MM\0"
        strftime(time_buf, sizeof(time_buf), "%H:%M", tm_now);
        graphics_draw_text(ctx, time_buf, md,
            GRect(0, -3, SCREEN_W, TOP_BAR_H + 4),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // ── 2b. BLACKJACK footer (bottom) ─────────────────────
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx,
        GRect(0, SCREEN_H - BOT_BAR_H, SCREEN_W, BOT_BAR_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    // +1 on the y to give the text a little breathing room from the top
    // edge of the footer bar
    graphics_draw_text(ctx, "BLACKJACK", sm,
        GRect(0, SCREEN_H - BOT_BAR_H + 1, SCREEN_W, BOT_BAR_H),
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // ── 3a. Launch screen ─────────────────────────────────
    // Shown only on the title screen. Returns early so the game board
    // elements below are never drawn on top of it.
    if (s_state == STATE_LAUNCH) {

        // Four suit bitmaps in a horizontal row, centred on the screen.
        // Total width = 4 bitmaps × 20 px + 3 gaps × 8 px = 104 px
        // Left margin = (144 − 104) / 2 = 20 px
        for (int i = 0; i < 4; i++) {
            if (s_suit_bmp[i]) {
                graphics_context_set_compositing_mode(ctx, GCompOpSet);
                graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[i],
                    GRect(20 + i * (SUIT_SZ + 8), 18, SUIT_SZ, SUIT_SZ));
                graphics_context_set_compositing_mode(ctx, GCompOpAssign);
            }
        }

        // Bordered prompt box — 5 px of inner padding on all sides so
        // the text never touches the border rectangle.
        // Box:  GRect(8, 42, 128, 38)  → x=8, y=42, w=128, h=38
        // Text: GRect(12, 47, 120, 28) → 4 px left/right, 5 px top inset
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_draw_rect(ctx, GRect(8, 42, SCREEN_W - 16, 38));
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, "PRESS ANY BUTTON\nTO DEAL", sm,
            GRect(12, 47, SCREEN_W - 24, 28),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Credits — drawn with GOTHIC_14 so they match the rest of the UI.
        // "Persistent Productions" is ~154 px wide at this font size, wider
        // than the screen (144 px), so GTextOverflowModeWordWrap splits it
        // onto two lines automatically, giving four lines total × 16 px = 64 px.
        // Starting at y=86 the block ends at y=150, leaving 1 px before footer.
        graphics_draw_text(ctx,
            "Created by:\nPersistent Productions\nJoel Penton",
            sm, GRect(4, 86, SCREEN_W - 8, 64),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        return;  // nothing below applies to the launch screen
    }

    // ── 3b. Game board ────────────────────────────────────
    // Drawn for STATE_DEALING, STATE_PLAYER_TURN, STATE_DEALER_TURN,
    // and STATE_RESULT. The result and score overlays are layered on
    // top at steps 4 and 5, so the board is always present underneath.

    int y = TOP_BAR_H;  // start just below the clock bar (y = 16)

    // DEALER label and running total
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "DEALER", smb,
        GRect(CARD_X, y, 50, ROW_H + 2),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // Show the dealer's visible total. Before the hole card is revealed,
    // show only the face-up card's value followed by "+?" to indicate an
    // unknown second card. After the hole card is revealed, show the true total.
    {
        char dt[12] = "";
        if (s_dn > 0) {
            if (!s_hole_shown) {
                // Calculate the single face-up card value (dealer[0] only)
                int v1 = (s_dealer[0].rank >= 10) ? 10 :
                         (s_dealer[0].rank ==  1) ? 11 :
                          s_dealer[0].rank;
                snprintf(dt, sizeof(dt), "%d+?", v1);
            } else {
                snprintf(dt, sizeof(dt), "%d", hand_val(s_dealer, s_dn));
            }
        }
        graphics_draw_text(ctx, dt, sm,
            GRect(56, y, 50, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }

    // ^HIT button hint — top-right corner of the DEALER row.
    // Only shown while SELECT is held during the player's turn.
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_draw_text(ctx, "^HIT", sm,
            GRect(0, y, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    y += ROW_H + 1;  // y = 31 (1 px gap between label row and cards)

    // Dealer card area (y = 31, bottom at 80)
    // Card at index 1 is the hole card — rendered face-down until
    // s_hole_shown is set to true by player_stand() or player_hit(bust).
    for (int i = 0; i < s_dn; i++) {
        bool is_hole = (!s_hole_shown && i == 1);
        draw_card(ctx, s_dealer[i], CARD_X + i * CARD_STEP, y, is_hole);
    }

    y += CARD_H + 2;  // y = 82 (2 px gap below dealer cards)

    // Horizontal divider line separating dealer and player areas
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(0, y), GPoint(SCREEN_W - 1, y));
    y += 3;  // y = 85 (3 px gap above player label row)

    // PLAYER label and current hand total
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "PLAYER", smb,
        GRect(CARD_X, y, 50, ROW_H + 2),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    if (s_pn > 0) {
        char pt[8];
        snprintf(pt, sizeof(pt), "%d", hand_val(s_player, s_pn));
        graphics_draw_text(ctx, pt, sm,
            GRect(56, y, 50, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }

    y += ROW_H + 1;  // y = 100 (1 px gap between label row and cards)

    // Player card area (y = 100, bottom at 149)
    // All player cards are always face-up.
    // 149 → 2 px gap → 151 = start of BLACKJACK footer.
    for (int i = 0; i < s_pn; i++) {
        draw_card(ctx, s_player[i], CARD_X + i * CARD_STEP, y, false);
    }

    // vSTAY button hint — bottom-right corner, just above the footer.
    // Mirrors ^HIT at the top: y = 168 − 17 (footer) − 14 (row height) − 1 = 136.
    // Only shown while SELECT is held during the player's turn.
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_draw_text(ctx, "vSTAY", sm,
            GRect(0, SCREEN_H - BOT_BAR_H - ROW_H - 1, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    // ── 4. Result overlay ─────────────────────────────────
    // A black rounded rectangle covering most of the game board.
    // Skipped when s_board_peek is true — the player has held SELECT
    // to inspect the final board state and the overlay will return
    // when SELECT is released.
    if (s_state == STATE_RESULT && !s_board_peek) {

        // Map the result enum to display strings
        const char *result_word, *point_str;
        switch (s_result) {
            case RESULT_WIN:         result_word = "WIN";       point_str = "+100"; break;
            case RESULT_BLACKJACK:   result_word = "BLACKJACK"; point_str = "+100"; break;
            case RESULT_DEALER_BUST: result_word = "WIN";       point_str = "+100"; break;
            case RESULT_LOSE:        result_word = "LOSE";      point_str = "-60";  break;
            case RESULT_BUST:        result_word = "BUST";      point_str = "-60";  break;
            case RESULT_TIE:         result_word = "TIE";       point_str = "+0";   break;
            default:                 result_word = "";           point_str = "";     break;
        }

        // Overlay box geometry — sits between the clock bar and footer
        // with a 4 px margin on each side and 14 px below the clock bar
        int bx = 4;
        int by = TOP_BAR_H + 14;                    // y = 30
        int bw = SCREEN_W - 8;                       // 136 px wide
        int bh = SCREEN_H - BOT_BAR_H - by - 4;     // 117 px tall

        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);

        // Result word — "BLACKJACK" is too wide for GOTHIC_28_BOLD at this
        // box width, so we use the slightly smaller GOTHIC_18_BOLD for it
        GFont result_font = (s_result == RESULT_BLACKJACK) ? md : lg;
        graphics_draw_text(ctx, result_word, result_font,
            GRect(bx + 2, by + 2, bw - 4, 34),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Point delta (+100 / −60 / +0)
        graphics_draw_text(ctx, point_str, md,
            GRect(bx + 2, by + 32, bw - 4, 20),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Final hand totals on separate lines, mirroring the board layout
        // (dealer on top, player below).
        char dealer_str[16], player_str[16];
        snprintf(dealer_str, sizeof(dealer_str), "Dealer: %d",
            hand_val(s_dealer, s_dn));
        snprintf(player_str, sizeof(player_str), "Player: %d",
            hand_val(s_player, s_pn));
        graphics_draw_text(ctx, dealer_str, sm,
            GRect(bx + 2, by + 52, bw - 4, 16),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, player_str, sm,
            GRect(bx + 2, by + 68, bw - 4, 16),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Deal prompt — explicitly UP/DOWN to avoid confusion with the
        // SELECT board-peek feature
        graphics_draw_text(ctx, "PRESS UP OR DOWN\nTO DEAL", sm,
            GRect(bx + 2, by + 86, bw - 4, 28),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // ── 5. Score overlay ──────────────────────────────────
    // Centred black box showing the cumulative score, painted on top of
    // the board while SELECT is held during the player's turn.
    // The ^HIT / vSTAY hints (drawn in step 3b above) remain visible at
    // the screen edges even while this box is present.
    if (s_show_overlay && s_state == STATE_PLAYER_TURN) {
        int bw = 112, bh = 54;
        int bx = (SCREEN_W - bw) / 2;   // horizontally centred
        int by = (SCREEN_H - bh) / 2;   // vertically centred
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "TOTAL SCORE", sm,
            GRect(bx, by + 4, bw, 16),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        char score_str[14];
        snprintf(score_str, sizeof(score_str), "%d", s_score);
        graphics_draw_text(ctx, score_str, lg,
            GRect(bx, by + 18, bw, 34),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

// ═════════════════════════════════════════════════════════
//  GAME LOGIC
// ═════════════════════════════════════════════════════════

// Compare final hand totals, apply the score delta, persist it, and
// transition to STATE_RESULT. Called from dealer_done_cb() after the
// dealer finishes playing.
//
// Note: player bust is handled separately in player_hit() — game_evaluate()
// is only reached when the dealer has played out their hand completely.
static void game_evaluate(void) {
    int p = hand_val(s_player, s_pn);
    int d = hand_val(s_dealer, s_dn);
    APP_LOG(APP_LOG_LEVEL_INFO, "Evaluating: player=%d dealer=%d", p, d);

    if (d > 21) {
        // Dealer bust — player wins regardless of their total
        s_result = RESULT_DEALER_BUST;
        s_score += SCORE_WIN;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: DEALER BUST");
    } else if (p > d) {
        // Player total is higher — check for natural blackjack
        // (exactly 2 cards totalling 21) to award the special result
        s_result = (s_pn == 2 && p == 21) ? RESULT_BLACKJACK : RESULT_WIN;
        s_score += SCORE_WIN;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: %s",
            s_result == RESULT_BLACKJACK ? "BLACKJACK" : "WIN");
    } else if (d > p) {
        s_result = RESULT_LOSE;
        s_score += SCORE_LOSE;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: LOSE");
    } else {
        // Equal totals — push, no score change
        s_result = RESULT_TIE;
        APP_LOG(APP_LOG_LEVEL_INFO, "Result: TIE");
    }

    persist_write_int(PERSIST_KEY, s_score);
    s_state = STATE_RESULT;
    update_clicks();       // restore UP/DOWN so the player can deal again
    layer_mark_dirty(s_layer);
}

// Timer callback: fired DEALER_END_MS after the dealer's final card appeared.
// The short pause gives the player time to see the card before the result
// overlay covers the board. Immediately calls game_evaluate().
static void dealer_done_cb(void *data) {
    s_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Dealer done — evaluating");
    game_evaluate();
}

// Timer callback: fired BUST_PAUSE_MS after the player's busting card appeared.
// Transitions directly to STATE_RESULT (score was already applied in player_hit).
static void bust_cb(void *data) {
    s_timer = NULL;
    APP_LOG(APP_LOG_LEVEL_INFO, "Bust pause complete — showing result");
    s_state = STATE_RESULT;
    update_clicks();       // restore UP/DOWN so the player can deal again
    layer_mark_dirty(s_layer);
}

// Timer callback: dealer draws one card, then immediately checks the new total.
//
// Design note — why check AFTER drawing rather than BEFORE:
// The old approach (check → draw → schedule → check → draw ...) scheduled
// an extra full DEAL_MS wait after the dealer's final card before calling
// game_evaluate(). Players saw the bust/stand card appear and then nothing
// happened for a second, which made it look like another card was being drawn.
// Checking the new total immediately and using the shorter DEALER_END_MS
// pause eliminates that confusion.
//
// Dealer rules: stand on ALL 17s (including soft 17 — Ace + 6 = 17 counts).
static void dealer_cb(void *data) {
    s_timer = NULL;
    int current = hand_val(s_dealer, s_dn);
    APP_LOG(APP_LOG_LEVEL_INFO, "dealer_cb: total=%d cards=%d", current, s_dn);

    if (current < 17) {
        // Dealer must hit
        s_dealer[s_dn++] = deck_draw();
        int new_total = hand_val(s_dealer, s_dn);
        APP_LOG(APP_LOG_LEVEL_INFO, "Dealer drew — new total=%d", new_total);
        layer_mark_dirty(s_layer);

        if (new_total >= 17) {
            // Dealer has reached their standing point (or bust).
            // Short pause so the player can see the final card.
            s_timer = app_timer_register(DEALER_END_MS, dealer_done_cb, NULL);
        } else {
            // Still below 17 — draw another card after DEAL_MS
            s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
        }
    } else {
        // Hole card reveal pushed the dealer to 17+ — evaluate immediately,
        // no extra draw needed.
        APP_LOG(APP_LOG_LEVEL_INFO, "Dealer already at %d on reveal", current);
        game_evaluate();
    }
}

// Player chooses to stand (or is auto-stood when they reach 21).
// Reveals the dealer's hole card and begins the dealer's turn.
static void player_stand(void) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Player stands at %d", hand_val(s_player, s_pn));
    s_state        = STATE_DEALER_TURN;
    s_hole_shown   = true;   // flip the hole card face-up
    s_show_overlay = false;  // dismiss score overlay if it was open
    update_clicks();         // block input while the dealer plays out
    layer_mark_dirty(s_layer);
    // Wait DEAL_MS before the dealer begins, giving the player a moment
    // to see the revealed hole card before action continues
    s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
}

// Player chooses to hit (draw one card). Three outcomes:
//
//   Total > 21 — bust:
//     Record the result and score immediately, then reveal the dealer's
//     hole card and enter STATE_DEALER_TURN (used here as an input-blocking
//     pause state). After BUST_PAUSE_MS the result overlay appears via bust_cb.
//     Using STATE_DEALER_TURN rather than a new state means no extra state
//     machine branches are needed — DEALER_TURN already blocks all input.
//
//   Total = 21 — auto-stand:
//     Hitting exactly 21 can't be improved, so we stand immediately without
//     requiring player input. This prevents accidental extra hits and keeps
//     the game moving.
//
//   Total < 21 — continue:
//     Redraw and wait for the next button press.
static void player_hit(void) {
    if (s_pn >= MAX_HAND) return;  // safety guard — should not be reachable
    s_player[s_pn++] = deck_draw();
    int v = hand_val(s_player, s_pn);
    APP_LOG(APP_LOG_LEVEL_INFO, "Player hit: total=%d cards=%d", v, s_pn);

    if (v > 21) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Player busted — pausing before result");
        s_result     = RESULT_BUST;
        s_score     += SCORE_LOSE;
        persist_write_int(PERSIST_KEY, s_score);  // save immediately on bust
        s_hole_shown = true;          // show dealer's hand during the pause
        s_state      = STATE_DEALER_TURN;  // block further player input
        update_clicks();
        layer_mark_dirty(s_layer);
        s_timer = app_timer_register(BUST_PAUSE_MS, bust_cb, NULL);

    } else if (v == 21) {
        APP_LOG(APP_LOG_LEVEL_INFO, "Player hit 21 — auto-standing");
        player_stand();

    } else {
        layer_mark_dirty(s_layer);   // just show the new card
    }
}

// Timer callback: deal the four opening cards one at a time with DEAL_MS
// between each. The deal order mirrors real blackjack:
//   step 0 → player card 1  (face-up)
//   step 1 → dealer card 1  (face-up, the "up-card")
//   step 2 → player card 2  (face-up)
//   step 3 → dealer card 2  (face-down, the "hole card")
//
// After all four cards, if the player has a natural 21 they are
// auto-stood immediately without waiting for input.
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
        // More cards to deal
        s_timer = app_timer_register(DEAL_MS, deal_cb, NULL);
    } else {
        // All four cards dealt — check for a natural 21
        int pv = hand_val(s_player, s_pn);
        APP_LOG(APP_LOG_LEVEL_INFO, "Deal complete: player=%d", pv);
        if (pv == 21) {
            APP_LOG(APP_LOG_LEVEL_INFO, "Natural 21 on deal — auto-standing");
            player_stand();
        } else {
            s_state = STATE_PLAYER_TURN;
            update_clicks();   // enable UP / DOWN / SELECT for the player
            layer_mark_dirty(s_layer);
        }
    }
}

// Reset all hand-level state and begin the opening deal animation.
// Called at the start of every new hand, from both button handlers and
// internal transitions.
static void start_hand(void) {
    // Cancel any in-flight timer from a previous hand (defensive — under
    // normal play this should already be NULL by the time we get here)
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
    update_clicks();         // block input during the deal animation
    layer_mark_dirty(s_layer);
    // FIRST_MS delay before the first card so the screen has time to paint
    // cleanly after clearing the previous hand's result overlay
    s_timer = app_timer_register(FIRST_MS, deal_cb, NULL);
}

// ═════════════════════════════════════════════════════════
//  BUTTON HANDLERS
//  Each handler checks s_state explicitly as a secondary safety net.
//  The primary input-blocking mechanism is update_clicks() which swaps
//  in an empty click provider during animation states — see below.
// ═════════════════════════════════════════════════════════

// UP button:
//   LAUNCH             → deal the first hand
//   RESULT (not peek)  → deal a new hand
//   PLAYER_TURN (<21)  → hit (draw one card)
//   All other states   → no-op
static void btn_up(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;   // score overlay is showing; ignore navigation
    if (s_state == STATE_LAUNCH) { start_hand(); return; }
    if (s_state == STATE_RESULT && !s_board_peek) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN && hand_val(s_player, s_pn) < 21) {
        player_hit();
    }
}

// DOWN button:
//   LAUNCH             → deal the first hand  (mirrors UP for convenience)
//   RESULT (not peek)  → deal a new hand       (mirrors UP for convenience)
//   PLAYER_TURN        → stand (end player turn, dealer plays out)
//   All other states   → no-op
static void btn_down(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;
    if (s_state == STATE_LAUNCH) { start_hand(); return; }
    if (s_state == STATE_RESULT && !s_board_peek) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN) player_stand();
}

// SELECT press (raw_click fires on physical press, not release):
//   LAUNCH         → deal (maintains "any button" behaviour on title screen)
//   RESULT         → begin board peek (hide result overlay)
//   PLAYER_TURN    → show score + ^HIT / vSTAY hints overlay
//   All other      → no-op (DEALING / DEALER_TURN are blocked at provider level)
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

// SELECT release (raw_click fires on physical release):
// Dismisses whichever overlay is currently active.
// Board peek takes priority over the score overlay — though in practice
// only one can be active at a time (they belong to different states).
static void btn_sel_up(ClickRecognizerRef r, void *ctx) {
    if (s_board_peek) {
        s_board_peek = false;
        layer_mark_dirty(s_layer);
        return;
    }
    if (s_show_overlay) {
        s_show_overlay = false;
        layer_mark_dirty(s_layer);
    }
}

// ═════════════════════════════════════════════════════════
//  CLICK CONFIG MANAGEMENT
//
//  Problem: The Pebble click recognizer queues physical button events even
//  when the game is in an animation state and the handlers take no action.
//  If the player taps UP ten times while cards are dealing, those ten events
//  drain through the handler the moment STATE_PLAYER_TURN begins, causing
//  the player to hit (or bust) without any intentional input.
//
//  Solution: Install an EMPTY click provider during animation states
//  (STATE_DEALING, STATE_DEALER_TURN). With no subscriptions registered the
//  OS discards events rather than queuing them. When an interactive state
//  begins we install the full provider and events flow normally again.
//
//  The explicit state checks inside each handler above remain as a secondary
//  safety net in case any event slips through.
// ═════════════════════════════════════════════════════════

// Empty click provider — no button subscriptions.
// Installed during STATE_DEALING and STATE_DEALER_TURN.
static void click_provider_blocked(void *ctx) {
    (void)ctx;   // intentionally no subscriptions; suppress unused-param warning
}

// Full click provider — all three buttons subscribed.
// Installed during STATE_LAUNCH, STATE_PLAYER_TURN, and STATE_RESULT.
//
// UP / DOWN use single_click: fires once per button press.
// SELECT uses raw_click: fires a callback on press AND on release,
// which lets us show the overlay while held and hide it on release.
static void click_provider_active(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP,   btn_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, btn_down);
    window_raw_click_subscribe(BUTTON_ID_SELECT, btn_sel_dn, btn_sel_up, ctx);
}

// Swap in the correct click provider for the current game state.
// Called at every state transition (start_hand, deal_cb, player_stand,
// player_hit, bust_cb, game_evaluate).
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

// win_load — called once when the window is pushed onto the stack.
// Allocate all resources here; free them in win_unload.
static void win_load(Window *w) {

    // Create a full-screen drawing layer and register our update callback.
    // This single layer handles all game rendering; no sub-layers are used.
    Layer *root = window_get_root_layer(w);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);

    // Load suit bitmaps from compiled resources.
    // Array index must match Card.suit: [0]=Club [1]=Diamond [2]=Heart [3]=Spade.
    // Resource identifiers are generated by CloudPebble from the names entered
    // in the resource editor (SUIT_CLUB → RESOURCE_ID_SUIT_CLUB, etc.).
    // PNG requirements: solid white background, "Smallest Palette" memory format.
    s_suit_bmp[0] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_CLUB);
    s_suit_bmp[1] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_DIAMOND);
    s_suit_bmp[2] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_HEART);
    s_suit_bmp[3] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_SPADE);

    // Subscribe to minute-tick events to keep the clock bar current.
    // MINUTE_UNIT uses far less battery than SECOND_UNIT and is appropriate
    // for an HH:MM display that doesn't show seconds.
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    // Seed the RNG with the current epoch time so every session gets
    // a different shuffle sequence. time(NULL) has second-level resolution
    // which is sufficient — we just need it to differ between runs.
    srand((unsigned)time(NULL));
    deck_shuffle();

    // Restore the cumulative score from persistent storage.
    // persist_read_int returns 0 if PERSIST_KEY has never been written,
    // so first-launch behaviour (score = 0) is automatic.
    s_score        = persist_read_int(PERSIST_KEY);
    s_state        = STATE_LAUNCH;
    s_show_overlay = false;
    s_board_peek   = false;
    s_timer        = NULL;

    APP_LOG(APP_LOG_LEVEL_INFO, "Blackjack loaded. Saved score: %d", s_score);
}

// win_unload — called when the window is popped (e.g. BACK button).
// Free every resource allocated in win_load.
static void win_unload(Window *w) {
    tick_timer_service_unsubscribe();

    // Cancel any pending timer. This shouldn't be necessary under normal
    // exit paths but is good defensive practice — a dangling timer pointing
    // to a destroyed layer would crash the watch.
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }

    // Persist the score one final time in case the last hand's write
    // was somehow missed (also handles the "exited from result screen" case)
    persist_write_int(PERSIST_KEY, s_score);
    APP_LOG(APP_LOG_LEVEL_INFO, "Blackjack unloaded. Final score: %d", s_score);

    // Free bitmaps. NULL-check is defensive — gbitmap_destroy(NULL) would crash.
    for (int i = 0; i < 4; i++) {
        if (s_suit_bmp[i]) {
            gbitmap_destroy(s_suit_bmp[i]);
            s_suit_bmp[i] = NULL;
        }
    }

    layer_destroy(s_layer);
}

// ═════════════════════════════════════════════════════════
//  APP ENTRY POINTS
// ═════════════════════════════════════════════════════════

static void init(void) {
    s_win = window_create();

    // Install the active click provider immediately (before the window loads)
    // so that STATE_LAUNCH button presses are handled from the first frame.
    window_set_click_config_provider(s_win, click_provider_active);

    window_set_window_handlers(s_win, (WindowHandlers){
        .load   = win_load,
        .unload = win_unload,
    });

    // true = animated slide-in transition when the app opens
    window_stack_push(s_win, true);
}

static void deinit(void) {
    window_destroy(s_win);
}

// Standard Pebble app entry point.
// init() sets up the window; app_event_loop() blocks until the user
// exits (BACK button); deinit() cleans up before the process ends.
int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}