#include <pebble.h>

// ── Layout constants (144×168 flint display) ──────────────
#define SCREEN_W    144
#define SCREEN_H    168
#define TOP_BAR_H    16   // clock bar
#define BOT_BAR_H    17   // BLACKJACK footer
#define ROW_H        14   // dealer/player label rows
#define CARD_W       32
#define CARD_H       49
#define CARD_STEP    22
#define CARD_X        4
#define SUIT_SZ      20

// Y layout:
//  0–16   clock bar
// 16–30   dealer label row
// 30–31   1px gap
// 31–80   dealer cards (CARD_H=49)
// 80–82   2px gap
// 82      divider line
// 82–85   3px gap
// 85–99   player label row
// 99–100  1px gap
// 100–149 player cards (CARD_H=49)
// 149–151 2px gap
// 151–168 BLACKJACK footer (BOT_BAR_H=17)
// Total:  16+14+1+49+2+1+2+14+1+49+2+17 = 168 ✓

// Button hint Y positions:
//   ^HIT  sits in dealer label row → y=16
//   vSTAY sits just above footer   → y = 168-17-15 = 136

// ── Game constants ────────────────────────────────────────
#define DECK_SIZE    52
#define RESHUFFLE_AT 15
#define MAX_HAND     11
#define FIRST_MS    400
#define DEAL_MS    1000
#define SCORE_WIN   100
#define SCORE_LOSE  -60
#define PERSIST_KEY  42

// ── Types ─────────────────────────────────────────────────
typedef struct { int rank; int suit; } Card;
// suit: 0=Clubs, 1=Diamonds, 2=Hearts, 3=Spades

typedef enum {
    STATE_LAUNCH,
    STATE_DEALING,
    STATE_PLAYER_TURN,
    STATE_DEALER_TURN,
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

// ── Globals ───────────────────────────────────────────────
static Window   *s_win;
static Layer    *s_layer;
static AppTimer *s_timer;

static GBitmap  *s_suit_bmp[4];

static Card s_deck[DECK_SIZE];
static int  s_deck_top;

static Card s_player[MAX_HAND];  static int s_pn;
static Card s_dealer[MAX_HAND];  static int s_dn;
static bool s_hole_shown;

static GameState  s_state;
static ResultType s_result;
static int        s_score;
static bool       s_show_overlay;
static int        s_deal_step;

// ── Tick handler ──────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(s_layer);
}

// ── Deck ──────────────────────────────────────────────────
static void deck_shuffle(void) {
    for (int i = 0; i < DECK_SIZE; i++) {
        s_deck[i].rank = (i % 13) + 1;
        s_deck[i].suit = i / 13;
    }
    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card t = s_deck[i]; s_deck[i] = s_deck[j]; s_deck[j] = t;
    }
    s_deck_top = 0;
}

static Card deck_draw(void) {
    if (s_deck_top >= DECK_SIZE - RESHUFFLE_AT) deck_shuffle();
    return s_deck[s_deck_top++];
}

// ── Hand value ────────────────────────────────────────────
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

// ── Draw one card ─────────────────────────────────────────
static void draw_card(GContext *ctx, Card c, int x, int y, bool face_down) {
    GRect r = GRect(x, y, CARD_W, CARD_H);

    if (face_down) {
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, r, 3, GCornersAll);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_round_rect(ctx,
            GRect(x + 3, y + 3, CARD_W - 6, CARD_H - 6), 2);
        return;
    }

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, r, 3, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_round_rect(ctx, r, 3);

    char rank[3] = {0};
    switch (c.rank) {
        case  1: rank[0] = 'A'; break;
        case 11: rank[0] = 'J'; break;
        case 12: rank[0] = 'Q'; break;
        case 13: rank[0] = 'K'; break;
        default: snprintf(rank, sizeof(rank), "%d", c.rank); break;
    }
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, rank, f,
        GRect(x + 2, y + 1, CARD_W - 4, 22),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    if (s_suit_bmp[c.suit]) {
        graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[c.suit],
            GRect(x + 6, y + 23, SUIT_SZ, SUIT_SZ));
    }
}

// ── Main draw callback ────────────────────────────────────
static void layer_update(Layer *layer, GContext *ctx) {
    GFont sm  = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    GFont smb = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    GFont md  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GFont lg  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

    // White background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

    // ── TOP: clock bar ────────────────────────────────────
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, TOP_BAR_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[6];
    strftime(time_buf, sizeof(time_buf), "%H:%M", t);
    graphics_draw_text(ctx, time_buf, md,
        GRect(0, -3, SCREEN_W, TOP_BAR_H + 4),
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // ── BOTTOM: BLACKJACK footer ──────────────────────────
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, SCREEN_H - BOT_BAR_H, SCREEN_W, BOT_BAR_H),
        0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "BLACKJACK", sm,
        GRect(0, SCREEN_H - BOT_BAR_H + 1, SCREEN_W, BOT_BAR_H),
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // ── LAUNCH SCREEN ─────────────────────────────────────
    if (s_state == STATE_LAUNCH) {
        int sy = 46;
        for (int i = 0; i < 4; i++) {
            if (s_suit_bmp[i]) {
                graphics_draw_bitmap_in_rect(ctx, s_suit_bmp[i],
                    GRect(20 + i * (SUIT_SZ + 8), sy, SUIT_SZ, SUIT_SZ));
            }
        }
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_draw_rect(ctx, GRect(12, 84, SCREEN_W - 24, 44));
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, "PRESS ANY BUTTON\nTO DEAL", sm,
            GRect(14, 87, SCREEN_W - 28, 40),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        return;
    }

    // ── GAME SCREEN ───────────────────────────────────────
    int y = TOP_BAR_H; // 16

    // Dealer label row (y=16)
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "DEALER", smb,
        GRect(CARD_X, y, 50, ROW_H + 2),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

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
    graphics_draw_text(ctx, dt, sm,
        GRect(56, y, 50, ROW_H + 2),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // ^HIT — top-right, in dealer label row, only while overlay held
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_draw_text(ctx, "^HIT", sm,
            GRect(0, y, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    y += ROW_H + 1; // 31

    // Dealer cards (y=31)
    for (int i = 0; i < s_dn; i++) {
        draw_card(ctx, s_dealer[i],
            CARD_X + i * CARD_STEP, y,
            (!s_hole_shown && i == 1));
    }

    y += CARD_H + 2; // 82

    // Divider (y=82)
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(0, y), GPoint(SCREEN_W - 1, y));
    y += 3; // 85

    // Player label row (y=85) — no hint here anymore
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

    y += ROW_H + 1; // 100

    // Player cards (y=100, bottom at 149, 2px gap to footer at 151)
    for (int i = 0; i < s_pn; i++) {
        draw_card(ctx, s_player[i], CARD_X + i * CARD_STEP, y, false);
    }

    // vSTAY — bottom-right, just above BLACKJACK footer, only while overlay held
    // y = SCREEN_H - BOT_BAR_H - ROW_H - 1 = 168 - 17 - 14 - 1 = 136
    if (s_state == STATE_PLAYER_TURN && s_show_overlay) {
        graphics_draw_text(ctx, "vSTAY", sm,
            GRect(0, SCREEN_H - BOT_BAR_H - ROW_H - 1, SCREEN_W - 2, ROW_H + 2),
            GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    // ── RESULT OVERLAY ────────────────────────────────────
    if (s_state == STATE_RESULT) {
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

        int bx = 4, by = TOP_BAR_H + 14;
        int bw = SCREEN_W - 8;
        int bh = SCREEN_H - BOT_BAR_H - by - 4;

        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);

        GFont rf = (s_result == RESULT_BLACKJACK) ? md : lg;
        graphics_draw_text(ctx, rs, rf,
            GRect(bx + 2, by + 4, bw - 4, 34),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        graphics_draw_text(ctx, ps, md,
            GRect(bx + 2, by + 36, bw - 4, 22),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        char totals[28];
        snprintf(totals, sizeof(totals), "Dealer:%d  You:%d",
            hand_val(s_dealer, s_dn), hand_val(s_player, s_pn));
        graphics_draw_text(ctx, totals, sm,
            GRect(bx + 2, by + 60, bw - 4, 16),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        graphics_draw_text(ctx, "PRESS ANY BUTTON\nTO DEAL", sm,
            GRect(bx + 2, by + 78, bw - 4, 28),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    // ── SELECT OVERLAY: score + hints ─────────────────────
    // (hints ^HIT / vSTAY are drawn above; score box drawn here)
    if (s_show_overlay && s_state == STATE_PLAYER_TURN) {
        int bw = 112, bh = 54;
        int bx = (SCREEN_W - bw) / 2;
        int by = (SCREEN_H - bh) / 2;
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, "TOTAL SCORE", sm,
            GRect(bx, by + 4, bw, 16),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        char sc[14];
        snprintf(sc, sizeof(sc), "%d", s_score);
        graphics_draw_text(ctx, sc, lg,
            GRect(bx, by + 18, bw, 34),
            GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

// ── Game logic ────────────────────────────────────────────

static void game_evaluate(void) {
    int p = hand_val(s_player, s_pn);
    int d = hand_val(s_dealer, s_dn);

    if (d > 21) {
        s_result = RESULT_DEALER_BUST;
        s_score += SCORE_WIN;
    } else if (p > d) {
        s_result = (s_pn == 2 && p == 21) ? RESULT_BLACKJACK : RESULT_WIN;
        s_score += SCORE_WIN;
    } else if (d > p) {
        s_result = RESULT_LOSE;
        s_score += SCORE_LOSE;
    } else {
        s_result = RESULT_TIE;
    }

    persist_write_int(PERSIST_KEY, s_score);
    s_state = STATE_RESULT;
    layer_mark_dirty(s_layer);
}

static void dealer_cb(void *data) {
    s_timer = NULL;
    if (hand_val(s_dealer, s_dn) < 17) {
        s_dealer[s_dn++] = deck_draw();
        layer_mark_dirty(s_layer);
        s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
    } else {
        game_evaluate();
    }
}

static void player_stand(void) {
    s_state        = STATE_DEALER_TURN;
    s_hole_shown   = true;
    s_show_overlay = false;
    layer_mark_dirty(s_layer);
    s_timer = app_timer_register(DEAL_MS, dealer_cb, NULL);
}

static void player_hit(void) {
    if (s_pn >= MAX_HAND) return;
    s_player[s_pn++] = deck_draw();
    int v = hand_val(s_player, s_pn);
    if (v > 21) {
        s_result = RESULT_BUST;
        s_score += SCORE_LOSE;
        persist_write_int(PERSIST_KEY, s_score);
        s_state  = STATE_RESULT;
        layer_mark_dirty(s_layer);
    } else if (v == 21) {
        player_stand();
    } else {
        layer_mark_dirty(s_layer);
    }
}

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
        if (hand_val(s_player, s_pn) == 21) {
            player_stand();
        } else {
            s_state = STATE_PLAYER_TURN;
            layer_mark_dirty(s_layer);
        }
    }
}

static void start_hand(void) {
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    s_pn           = 0;
    s_dn           = 0;
    s_hole_shown   = false;
    s_result       = RESULT_NONE;
    s_show_overlay = false;
    s_deal_step    = 0;
    s_state        = STATE_DEALING;
    layer_mark_dirty(s_layer);
    s_timer = app_timer_register(FIRST_MS, deal_cb, NULL);
}

// ── Button handlers ───────────────────────────────────────

static void btn_up(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;
    if (s_state == STATE_LAUNCH || s_state == STATE_RESULT) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN && hand_val(s_player, s_pn) < 21) player_hit();
}

static void btn_down(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) return;
    if (s_state == STATE_LAUNCH || s_state == STATE_RESULT) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN) player_stand();
}

static void btn_sel_dn(ClickRecognizerRef r, void *ctx) {
    if (s_state == STATE_LAUNCH || s_state == STATE_RESULT) { start_hand(); return; }
    if (s_state == STATE_PLAYER_TURN) {
        s_show_overlay = true;
        layer_mark_dirty(s_layer);
    }
}

static void btn_sel_up(ClickRecognizerRef r, void *ctx) {
    if (s_show_overlay) {
        s_show_overlay = false;
        layer_mark_dirty(s_layer);
    }
}

static void click_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP,   btn_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, btn_down);
    window_raw_click_subscribe(BUTTON_ID_SELECT,
        btn_sel_dn, btn_sel_up, ctx);
}

// ── Window lifecycle ──────────────────────────────────────

static void win_load(Window *w) {
    Layer *root = window_get_root_layer(w);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);

    s_suit_bmp[0] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_CLUB);
    s_suit_bmp[1] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_DIAMOND);
    s_suit_bmp[2] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_HEART);
    s_suit_bmp[3] = gbitmap_create_with_resource(RESOURCE_ID_SUIT_SPADE);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    srand((unsigned)time(NULL));
    deck_shuffle();

    s_score        = persist_read_int(PERSIST_KEY);
    s_state        = STATE_LAUNCH;
    s_show_overlay = false;
    s_timer        = NULL;
}

static void win_unload(Window *w) {
    tick_timer_service_unsubscribe();
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    persist_write_int(PERSIST_KEY, s_score);
    for (int i = 0; i < 4; i++) {
        if (s_suit_bmp[i]) { gbitmap_destroy(s_suit_bmp[i]); s_suit_bmp[i] = NULL; }
    }
    layer_destroy(s_layer);
}

// ── App entry points ──────────────────────────────────────

static void init(void) {
    s_win = window_create();
    window_set_click_config_provider(s_win, click_provider);
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