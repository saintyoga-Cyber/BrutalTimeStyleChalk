#include <pebble.h>
#include "brutal_clock.h"
#include "settings.h"
#include "message_keys.auto.h"
#include <string.h>

#define SIDEBAR_WIDTH 30

#ifdef PBL_PLATFORM_EMERY
        #define IF_EMERY_ELSE(if_true, if_false) (if_true)
#else
        #define IF_EMERY_ELSE(if_true, if_false) (if_false)
#endif

#define WIDTH PBL_DISPLAY_WIDTH
#define HEIGHT PBL_DISPLAY_HEIGHT
#define PADDING 6       // Distance from screen edges
#define MARGIN IF_EMERY_ELSE(7, 5)      // Distance between big numbers

#define SCALE_X IF_EMERY_ELSE(6, 4)
#define SCALE_Y IF_EMERY_ELSE(7, 5)
#define GLYPH_DEFAULT_HEIGHT (14)
#define GLYPH_DEFAULT_WIDTH (12)

#define BMPW 64
#define BMP_HEIGHT 96 // Based on max Y-coordinate (88+5) in fonts array. Actual glyphs bitmap height.
#define BUFW IF_EMERY_ELSE(88, 64)
#define BIGH IF_EMERY_ELSE(98, 70) /* Max scaled glyph height: 14px * 7 (Emery) or 5 */
#define BIGW (GLYPH_DEFAULT_WIDTH * SCALE_X) // Keep BIGW consistent with GLYPH_DEFAULT_WIDTH and SCALE_X

#define GET_BIT(buf,w,x,y) (((buf)[((w)>>3)*(y)+((x)>>3)]) &  (0x80 >> (x)%8))
#define SET_BIT(buf,w,x,y) (((buf)[((w)>>3)*(y)+((x)>>3)]) |= (0x80 >> (x)%8))

enum font { BIG, SMALL, TINY };
enum tile { TILE_EMPTY, FULL, CORNER0, CORNER1, CORNER2 };
enum direction { UP, RIGHT, DOWN, LEFT, HORIZONTAL };

// --- Static Variable Declarations ---
static GColor config_bg;
static GColor config_fg;
static uint8_t opacity;

static int s_hours_scale_x;
static int s_hours_scale_y;
static int s_scale_x; // Initialized in brutal_clock_load
static int s_scale_y; // Initialized in brutal_clock_load
static int s_bigh; // Calculated in update_big_digit_size
static int s_bigw; // Calculated in update_big_digit_size

static int16_t s_unobstructed_offset;
static int16_t s_prev_unobstructed_height = 0;

static PropertyAnimation *s_hours_pos_anim;
static PropertyAnimation *s_minutes_pos_anim;
static Animation *s_hours_scale_anim;

typedef struct {
    int start_scale_x;
    int end_scale_x;
    int start_scale_y;
    int end_scale_y;
    int start_y_offset;
    int end_y_offset;
} ScaleAnimData;

static ScaleAnimData s_scale_anim_data;

static Layer *hours, *minutes;
static GBitmap *glyphs;
#ifndef PBL_PLATFORM_APLITE
static uint8_t *s_scale_buf = NULL;
#endif

#define BUFFER 3
enum { HOURS_LAYER, MINUTES_LAYER };
static GRect bounds[2];
static GRect screen_bounds;
static GRect s_initial_hours_bounds;
static GRect s_initial_minutes_bounds;

// --- Static Function Prototypes ---
static struct tm *now();
static void draw_pixel(GBitmapDataRowInfo info, int16_t x, GColor color);
#ifndef PBL_PLATFORM_APLITE
static GRect scale_glyph(GRect glyph, uint8_t **pixels);
#endif
static GRect get_glyph(enum font font, char c, uint8_t **pixels);
static void print_font(GContext *ctx, GRect rect,
                       enum font font, enum direction direction, char *str,
                       uint8_t spacing, uint8_t dither);
static void Hours(Layer*, GContext*);
static void Minutes(Layer*, GContext*);
static void brutal_clock_animate_in_quick_view(int final_y_offset);
static void brutal_clock_animate_out_quick_view();
static void prv_scale_anim_update(Animation *animation, AnimationProgress progress);
static void prv_scale_anim_teardown(Animation *animation);
static void update_big_digit_size();
static void brutal_clock_calculate_bounds();

// Fonts array moved here to be accessible by get_glyph immediately
static const GRect fonts[3][128] = {
  // BIG
  [BIG] = {
  [' '] = {{ 1, 1},{12,14}},
  ['0'] = {{ 1, 1},{12,14}},
  ['1'] = {{14, 1},{ 6,14}},
  ['2'] = {{21, 1},{12,14}},
  ['3'] = {{34, 1},{12,14}},
  ['4'] = {{47, 1},{ 9,14}},
  ['5'] = {{ 1,16},{12,14}},
  ['6'] = {{14,16},{12,14}},
  ['7'] = {{27,16},{ 9,14}},
  ['8'] = {{37,16},{12,14}},
  ['9'] = {{50,16},{12,14}},
  [FULL] = {{49,67},{7,7}},
  [CORNER0] = {{IF_EMERY_ELSE(57, 59),1},{7,7}},
  [CORNER1] = {{57, 8},{7,7}},
  [CORNER2] = {{IF_EMERY_ELSE(57, 59),67},{7,7}}},

  // SMALL
  [SMALL] = {
        ['A'] = {{ 0,31},{6,8}}, ['a'] = {{ 0,31},{6,8}},
        ['B'] = {{ 7,31},{6,8}}, ['b'] = {{ 7,31},{6,8}},
        ['C'] = {{14,31},{6,8}}, ['c'] = {{14,31},{6,8}},
        ['D'] = {{21,31},{6,8}}, ['d'] = {{21,31},{6,8}},
        ['E'] = {{28,31},{6,8}}, ['e'] = {{28,31},{6,8}},
        ['F'] = {{35,31},{6,8}}, ['f'] = {{35,31},{6,8}},
        ['G'] = {{42,31},{6,8}}, ['g'] = {{42,31},{6,8}},
        ['H'] = {{49,31},{6,8}}, ['h'] = {{49,31},{6,8}},
        ['I'] = {{56,31},{6,8}}, ['i'] = {{56,31},{6,8}},
        ['J'] = {{ 0,40},{6,8}}, ['j'] = {{ 0,40},{6,8}},
        ['K'] = {{ 7,40},{6,8}}, ['k'] = {{ 7,40},{6,8}},
        ['L'] = {{14,40},{6,8}}, ['l'] = {{14,40},{6,8}},
        ['M'] = {{21,40},{6,8}}, ['m'] = {{21,40},{6,8}},
        ['N'] = {{28,40},{6,8}}, ['n'] = {{28,40},{6,8}},
        ['O'] = {{35,40},{6,8}}, ['o'] = {{35,40},{6,8}},
        ['P'] = {{42,40},{6,8}}, ['p'] = {{42,40},{6,8}},
        ['Q'] = {{49,40},{6,8}}, ['q'] = {{49,40},{6,8}},
        ['R'] = {{56,40},{6,8}}, ['r'] = {{56,40},{6,8}},
        ['S'] = {{ 0,49},{6,8}}, ['s'] = {{ 0,49},{6,8}},
        ['T'] = {{ 7,49},{6,8}}, ['t'] = {{ 7,49},{6,8}},
        ['U'] = {{14,49},{6,8}}, ['u'] = {{14,49},{6,8}},
        ['V'] = {{21,49},{6,8}}, ['v'] = {{21,49},{6,8}},
        ['W'] = {{28,49},{6,8}}, ['w'] = {{28,49},{6,8}},
        ['X'] = {{35,49},{6,8}}, ['x'] = {{35,49},{6,8}},
        ['Y'] = {{42,49},{6,8}}, ['y'] = {{42,49},{6,8}},
        ['Z'] = {{49,49},{6,8}}, ['z'] = {{49,49},{6,8}},
        ['0'] = {{56,49},{6,8}}, ['1'] = {{ 0,58},{6,8}},
        ['2'] = {{ 7,58},{6,8}}, ['3'] = {{14,58},{6,8}},
        ['4'] = {{21,58},{6,8}}, ['5'] = {{28,58},{6,8}},
        ['6'] = {{35,58},{6,8}}, ['7'] = {{42,58},{6,8}},
        ['8'] = {{49,58},{6,8}}, ['9'] = {{56,58},{6,8}},
        ['%'] = {{ 0,67},{6,8}}, ['.'] = {{ 7,67},{6,8}},
        ['-'] = {{14,67},{6,8}}, ['/'] = {{21,67},{6,8}},
        [':'] = {{28,67},{6,8}}, [' '] = {{35,67},{6,8}}},

  // TINY
  [TINY] = {
        ['A'] = {{ 0,76},{4,5}}, ['a'] = {{ 0,76},{4,5}},
        ['B'] = {{ 5,76},{4,5}}, ['b'] = {{ 5,76},{4,5}},
        ['C'] = {{10,76},{4,5}}, ['c'] = {{10,76},{4,5}},
        ['D'] = {{15,76},{4,5}}, ['d'] = {{15,76},{4,5}},
        ['E'] = {{20,76},{4,5}}, ['e'] = {{20,76},{4,5}},
        ['F'] = {{25,76},{4,5}}, ['f'] = {{25,76},{4,5}},
        ['G'] = {{30,76},{4,5}}, ['g'] = {{30,76},{4,5}},
        ['H'] = {{35,76},{4,5}}, ['h'] = {{35,76},{4,5}},
        ['I'] = {{40,76},{4,5}}, ['i'] = {{40,76},{4,5}},
        ['J'] = {{45,76},{4,5}}, ['j'] = {{45,76},{4,5}},
        ['K'] = {{50,76},{4,5}}, ['k'] = {{50,76},{4,5}},
        ['L'] = {{55,76},{4,5}}, ['l'] = {{55,76},{4,5}},
        ['M'] = {{60,76},{4,5}}, ['m'] = {{60,76},{4,5}},
        ['N'] = {{ 0,82},{4,5}}, ['n'] = {{ 0,82},{4,5}},
        ['O'] = {{ 5,82},{4,5}}, ['o'] = {{ 5,82},{4,5}},
        ['P'] = {{10,82},{4,5}}, ['p'] = {{10,82},{4,5}},
        ['Q'] = {{15,82},{4,5}}, ['q'] = {{15,82},{4,5}},
        ['R'] = {{20,82},{4,5}}, ['r'] = {{20,82},{4,5}},
        ['S'] = {{25,82},{4,5}}, ['s'] = {{25,82},{4,5}},
        ['T'] = {{30,82},{4,5}}, ['t'] = {{30,82},{4,5}},
        ['U'] = {{35,82},{4,5}}, ['u'] = {{35,82},{4,5}},
        ['V'] = {{40,82},{4,5}}, ['v'] = {{40,82},{4,5}},
        ['W'] = {{45,82},{4,5}}, ['w'] = {{45,82},{4,5}},
        ['X'] = {{50,82},{4,5}}, ['x'] = {{50,82},{4,5}},
        ['Y'] = {{55,82},{4,5}}, ['y'] = {{55,82},{4,5}},
        ['Z'] = {{60,82},{4,5}}, ['z'] = {{60,82},{4,5}},
        ['0'] = {{ 0,88},{4,5}}, ['1'] = {{ 5,88},{4,5}},
        ['2'] = {{10,88},{4,5}}, ['3'] = {{15,88},{4,5}},
        ['4'] = {{20,88},{4,5}}, ['5'] = {{25,88},{4,5}},
        ['6'] = {{30,88},{4,5}}, ['7'] = {{35,88},{4,5}},
        ['8'] = {{40,88},{4,5}}, ['9'] = {{45,88},{4,5}},
        ['%'] = {{50,88},{4,5}}, ['.'] = {{55,88},{4,5}},
        ['/'] = {{60,88},{4,5}}, [' '] = {{35,67},{4,5}}}};

// --- Function Definitions (Ordered for compilation) ---

// Animation Callbacks
static void prv_scale_anim_update(Animation *animation, AnimationProgress progress) {
    s_hours_scale_x = s_scale_anim_data.start_scale_x + ((s_scale_anim_data.end_scale_x - s_scale_anim_data.start_scale_x) * progress / ANIMATION_NORMALIZED_MAX);
    s_hours_scale_y = s_scale_anim_data.start_scale_y + ((s_scale_anim_data.end_scale_y - s_scale_anim_data.start_scale_y) * progress / ANIMATION_NORMALIZED_MAX);
    s_unobstructed_offset = s_scale_anim_data.start_y_offset + ((s_scale_anim_data.end_y_offset - s_scale_anim_data.start_y_offset) * progress / ANIMATION_NORMALIZED_MAX);

    update_big_digit_size(s_hours_scale_x, s_hours_scale_y);
    brutal_clock_calculate_bounds();
    layer_set_frame(hours, bounds[HOURS_LAYER]);
    layer_set_frame(minutes, bounds[MINUTES_LAYER]);
    layer_mark_dirty(hours);
    layer_mark_dirty(minutes);
}

static void prv_scale_anim_teardown(Animation *animation) {
    if (s_hours_scale_anim) {
        s_hours_scale_anim = NULL;
    }
}

// Animation Trigger Functions
static void brutal_clock_animate_in_quick_view(int final_y_offset) {
    if (s_hours_scale_anim) {
        animation_unschedule(s_hours_scale_anim);
        animation_destroy(s_hours_scale_anim);
        s_hours_scale_anim = NULL;
    }
    
    s_initial_hours_bounds = layer_get_frame(hours);
    s_initial_minutes_bounds = layer_get_frame(minutes);

    s_scale_anim_data.start_scale_x = s_hours_scale_x;
    s_scale_anim_data.end_scale_x = SCALE_X * 0.75;
    s_scale_anim_data.start_scale_y = s_hours_scale_y;
    s_scale_anim_data.end_scale_y = SCALE_Y * 0.75;
    s_scale_anim_data.start_y_offset = 0;
    s_scale_anim_data.end_y_offset = final_y_offset;

    s_hours_scale_anim = animation_create();
    static AnimationImplementation anim_impl = {
        .update = prv_scale_anim_update,
        .teardown = prv_scale_anim_teardown
    };
    animation_set_implementation(s_hours_scale_anim, &anim_impl);
    animation_set_duration(s_hours_scale_anim, 300);
    animation_set_curve(s_hours_scale_anim, AnimationCurveEaseOut);
    animation_schedule(s_hours_scale_anim);
}

static void brutal_clock_animate_out_quick_view() {
    if (s_hours_scale_anim) {
        animation_unschedule(s_hours_scale_anim);
        animation_destroy(s_hours_scale_anim);
        s_hours_scale_anim = NULL;
    }

    s_scale_anim_data.start_scale_x = s_hours_scale_x;
    s_scale_anim_data.end_scale_x = SCALE_X;
    s_scale_anim_data.start_scale_y = s_hours_scale_y;
    s_scale_anim_data.end_scale_y = SCALE_Y;
    s_scale_anim_data.start_y_offset = s_unobstructed_offset;
    s_scale_anim_data.end_y_offset = 0;

    s_hours_scale_anim = animation_create();
    static AnimationImplementation anim_impl = {
        .update = prv_scale_anim_update,
        .teardown = prv_scale_anim_teardown
    };
    animation_set_implementation(s_hours_scale_anim, &anim_impl);
    animation_set_duration(s_hours_scale_anim, 300);
    animation_set_curve(s_hours_scale_anim, AnimationCurveEaseOut);
    animation_schedule(s_hours_scale_anim);
}

// Original Helper Functions
static struct tm *
now()
{
        time_t timestamp;
        struct tm *tm;

        timestamp = time(0);
        tm = localtime(&timestamp);

        return tm;
}

static void
draw_pixel(GBitmapDataRowInfo info, int16_t x, GColor color)
{
#if defined(PBL_COLOR)
        memset(info.data + x, color.argb, 1);
#elif defined(PBL_BW)
        uint8_t byte  = x / 8;
        uint8_t bit   = x % 8;
        uint8_t value = gcolor_equal(color, GColorWhite) ? 1 : 0;
        uint8_t *bp   = &info.data[byte];
        *bp ^= (-value ^ *bp) & (1 << bit);
#endif
}

#ifndef PBL_PLATFORM_APLITE
static GRect
scale_glyph(GRect glyph, uint8_t **pixels)
{
        if (s_scale_buf == NULL) {
                return glyph;
        }
        GRect scaled;
        uint16_t x, y, maxx, maxy;
        uint16_t bx, by, maxbx, maxby;
        uint8_t pixel;
        uint8_t neighbors;
        enum tile tile;

        scaled.origin.x = 0;
        scaled.origin.y = 0;
        scaled.size.w = glyph.size.w * s_scale_x;
        scaled.size.h = glyph.size.h * s_scale_y;

        maxx = glyph.origin.x + glyph.size.w;
        maxy = glyph.origin.y + glyph.size.h;

        memset(s_scale_buf, 0, (BUFW / 8) * BIGH);

        for (y=glyph.origin.y; y<maxy; y++)
        for (x=glyph.origin.x; x<maxx; x++) {
                pixel = GET_BIT(*pixels, BMPW, x, y);
                tile = TILE_EMPTY;

                if (pixel) {
                        tile = FULL;
                } else {
                        neighbors = 0;
            if (y > 0) {
                            if (x > 0 && GET_BIT(*pixels, BMPW, x-1, y-1)) neighbors |= 0b10000000;
                            if (GET_BIT(*pixels, BMPW, x  , y-1)) neighbors |= 0b01000000;
                            if (x < BMPW-1 && GET_BIT(*pixels, BMPW, x+1, y-1)) neighbors |= 0b00100000;
            }
            if (x > 0 && GET_BIT(*pixels, BMPW, x-1, y  )) neighbors |= 0b00010000;
                        if (x < BMPW-1 && GET_BIT(*pixels, BMPW, x+1, y  )) neighbors |= 0b00001000;

            if (y < BMP_HEIGHT-1) {
                            if (x > 0 && GET_BIT(*pixels, BMPW, x-1, y+1)) neighbors |= 0b00000100;
                            if (GET_BIT(*pixels, BMPW, x  , y+1)) neighbors |= 0b00000010;
                            if (x < BMPW-1 && GET_BIT(*pixels, BMPW, x+1, y+1)) neighbors |= 0b00000001;
            }

                        switch (neighbors) {
                        case 0b11010000: tile = CORNER0; break;
                        case 0b01101000: tile = CORNER1; break;
                        case 0b00001011: tile = CORNER2; break;
                        }
                }

                if (tile == TILE_EMPTY)
                        continue;

                by = (y - glyph.origin.y) * s_scale_y;
                maxby = by + s_scale_y;

                for (; by<maxby; by++) {
                        bx = (x - glyph.origin.x) * s_scale_x;
                        maxbx = bx + s_scale_x;

                        for (; bx<maxbx; bx++) {
                                if (!GET_BIT(*pixels,
                                             BMPW,
                                             fonts[BIG][tile].origin.x + (bx%s_scale_x),
                                             fonts[BIG][tile].origin.y + (by%s_scale_y)))
                                        continue;

                                SET_BIT(s_scale_buf, BUFW, bx, by);
                        }
                }
        }

        *pixels = s_scale_buf;

        return scaled;
}
#endif

static GRect
get_glyph(enum font font, char c, uint8_t **pixels)
{
        GRect glyph;

        if (c > 128 || c < ' ')
                c = ' ';

#ifdef PBL_PLATFORM_APLITE
    if (font == BIG) {
        font = SMALL;
    }
#endif

        glyph = fonts[font][(int)c];

        if (!glyph.size.w)
                glyph = fonts[font][' '];

        *pixels = gbitmap_get_data(glyphs);

#ifndef PBL_PLATFORM_APLITE
        if (font == BIG)
                glyph = scale_glyph(glyph, pixels);
#endif

        return glyph;
}

static void
print_font(GContext *ctx, GRect rect,
           enum font font, enum direction direction, char *str,
           uint8_t spacing, uint8_t dither)
{
        static const uint8_t map[8][8] = {
                {   0, 128,  32, 160,   8, 136,  40, 168 },
                { 192,  64, 224,  96, 200,  72, 232, 104 },
                {  48, 176,  16, 144,  56, 184,  24, 152 },
                { 240, 112, 208,  80, 248, 120, 216,  88 },
                {  12, 140,  44, 172,   4, 132,  36, 164 },
                { 204,  76, 236, 108, 196,  68, 228, 100 },
                {  60, 188,  28, 156,  52, 180,  20, 148 },
                { 252, 124, 220,  92, 244, 116, 212,  84 }
        };
        int16_t offset;
        unsigned i, len;
        GRect glyph;
        GBitmap *fb;
        GBitmapDataRowInfo info;
        int16_t x, y, maxx, maxy;
        int16_t px, py, maxpx, maxpy;
        uint8_t *pixels, w;

        len = strlen(str);

        if (len == 0)
                return;

#ifndef PBL_RECT
        direction = HORIZONTAL;
#endif

        switch (direction) {
        case RIGHT:
        case DOWN:
                break;
        case LEFT:
        case HORIZONTAL:
                offset = rect.size.w;
                offset -= (len-1) * spacing;
                for (i=0; i<len; i++) {
                        glyph = get_glyph(font, str[i], &pixels);
                        offset -= glyph.size.w;
                }

                if (direction == HORIZONTAL)
                        offset /= 2;

                rect.origin.x += offset;
                rect.size.w -= offset;
                break;
        case UP:
                offset = rect.size.h;
                offset -= (len-1) * spacing;
                for (i=0; i<len; i++) {
                        glyph = get_glyph(font, str[i], &pixels);
                        offset -= glyph.size.h;
                }
                rect.origin.y += offset;
                rect.size.h -= offset;
                break;
        }

        maxy = rect.origin.y + rect.size.h;
        fb = graphics_capture_frame_buffer(ctx);
        GRect fb_bounds = gbitmap_get_bounds(fb);

        for (i=0; i<len; i++) {
                glyph = get_glyph(font, str[i], &pixels);
                py = glyph.origin.y;
                maxpx = glyph.origin.x + glyph.size.w;
                maxpy = glyph.origin.y + glyph.size.h;

                // V-clipping
                int16_t loop_start_y = rect.origin.y;
                int16_t loop_end_y = maxy;
                if (loop_start_y < fb_bounds.origin.y) {
                        py += fb_bounds.origin.y - loop_start_y;
                        loop_start_y = fb_bounds.origin.y;
                }
                if (loop_end_y > fb_bounds.origin.y + fb_bounds.size.h) {
                        loop_end_y = fb_bounds.origin.y + fb_bounds.size.h;
                }

                for (y=loop_start_y; y < loop_end_y && py < maxpy; y++, py++) {
                        info = gbitmap_get_data_row_info(fb, y);
                        px = glyph.origin.x;

                        maxx = rect.origin.x + rect.size.w;
                        if (info.max_x < maxx)
                                maxx = info.max_x;

                        for (x=rect.origin.x; x < maxx && px < maxpx; x++, px++) {
                                w = font == BIG ? BUFW : BMPW;
                                if (!GET_BIT(pixels, w, px, py))
                                        continue;

                                if (dither > map[y%8][x%8])
                                        draw_pixel(info, x, config_fg);
                        }
                }

                switch (direction) {
                case UP:
                case DOWN:
                        rect.origin.y += glyph.size.h + spacing;
                        break;
                case RIGHT:
                case LEFT:
                case HORIZONTAL:
                        rect.origin.x += glyph.size.w + spacing;
                        break;
                }
        }

        graphics_release_frame_buffer(ctx, fb);
}

static void
Hours(Layer *_layer, GContext *ctx)
{
        char buf[4], *pt;
        struct tm *tm;
        GRect rect;
        unsigned len;

        s_scale_x = s_hours_scale_x;
        s_scale_y = s_hours_scale_y;

        tm = now();
        strftime(buf, sizeof buf, clock_is_24h_style() ? "%H" : "%I", tm);
        pt = buf;

        pt += !settings.showLeadingZero * (buf[0] == '0');

#ifdef PBL_RECT
        if (opacity == 255)
                print_font(ctx, bounds[HOURS_LAYER], BIG, RIGHT, buf, MARGIN, settings.shadow);

        print_font(ctx, bounds[HOURS_LAYER], BIG, LEFT, pt, MARGIN, opacity);

        if (opacity == 255)
                return;

        len = strlen(pt);
        rect.origin.x = bounds[HOURS_LAYER].size.w - len*8 -2;
        rect.origin.y = 0;
        rect.size.w = len*8 +2;
        rect.size.h = 10;
        graphics_context_set_fill_color(ctx, config_bg);
        graphics_fill_rect(ctx, rect, 0, GCornerNone);
        print_font(ctx, bounds[HOURS_LAYER], SMALL, LEFT, pt, 2, 255);
#else
        print_font(ctx, bounds[HOURS_LAYER], BIG, HORIZONTAL, buf, 5, settings.shadow);

        len = strlen(pt);
        rect.origin.x = bounds[HOURS_LAYER].size.w/2 - (len*8 +2)/2;
        rect.origin.y = 0;
        rect.size.w = len*8 +2;
        rect.size.h = 10;
        graphics_context_set_fill_color(ctx, config_bg);
        graphics_fill_rect(ctx, rect, 0, GCornerNone);
        print_font(ctx, bounds[HOURS_LAYER], SMALL, HORIZONTAL, pt, 2, 255);
#endif
}

static void
Minutes(Layer *_layer, GContext *ctx)
{
        char buf[4];
        struct tm *tm;

        s_scale_x = SCALE_X;
        s_scale_y = SCALE_Y;

        tm = now();
        strftime(buf, sizeof buf, "%M", tm);

        print_font(ctx, bounds[MINUTES_LAYER], BIG, LEFT, buf, MARGIN, 255);

#ifdef PBL_RECT
        if (s_unobstructed_offset == 0)
                print_font(ctx, bounds[MINUTES_LAYER], BIG, RIGHT, buf, MARGIN, settings.shadow);
#endif
}

static void update_big_digit_size(int scale_x, int scale_y) {
  s_bigh = 14 * scale_y;
  s_bigw = 12 * scale_x;
}

static void brutal_clock_calculate_bounds() {
  GRect clock_bounds = screen_bounds;
#ifdef PBL_RECT
  if (settings.sidebarOnLeft) {
    clock_bounds.origin.x += ACTION_BAR_WIDTH;
  }
  clock_bounds.size.w -= ACTION_BAR_WIDTH;
#endif

#ifdef PBL_RECT
  int16_t hours_y   = screen_bounds.origin.y + PADDING;
  int16_t minutes_y = screen_bounds.origin.y + PADDING
                      + s_bigh + 15;
#else
  // PBL_ROUND: vertically center both digit blocks together
  // on the 180px screen.
  int16_t total_clock_height = s_bigh + 15 + s_bigh;
  int16_t hours_y   = (HEIGHT - total_clock_height) / 2;
  int16_t minutes_y = hours_y + s_bigh + 15;
#endif

  int16_t minutes_bigw = s_bigw;
  int16_t minutes_bigh = s_bigh;

  if (s_unobstructed_offset != 0) {
    // Quickview is active
    minutes_bigw = 12 * SCALE_X;
    minutes_bigh = 14 * SCALE_Y;

#ifdef PBL_RECT
    int16_t unobstructed_height = screen_bounds.size.h - abs(s_unobstructed_offset);
    int16_t clock_height = s_bigh + minutes_bigh + 10; // Reduced margin
    int16_t y_start = (s_unobstructed_offset > 0) ? s_unobstructed_offset : 0;
    hours_y = y_start + (unobstructed_height - clock_height) / 2 + 5; // Shift down
    minutes_y = hours_y + s_bigh + 10; // Reduced margin
#else
    hours_y += s_unobstructed_offset / 2;
    minutes_y += s_unobstructed_offset / 2;
#endif
  }

#ifdef PBL_RECT
  // Bounding box calculation for vertical right-alignment
  int16_t box_width;
  if (s_unobstructed_offset != 0) {
      // In quickview, minutes are wider
      box_width = minutes_bigw * 2 + MARGIN;
  } else {
      // In normal view, both have same width
      box_width = s_bigw * 2 + MARGIN;
  }
  
  int16_t start_x = clock_bounds.origin.x + (clock_bounds.size.w - box_width) / 2;

  bounds[HOURS_LAYER] = (GRect) {
    .origin = { start_x, hours_y },
    .size = { box_width, s_bigh }
  };
  
  bounds[MINUTES_LAYER] = (GRect) {
    .origin = { start_x, minutes_y },
    .size = { box_width, minutes_bigh }
  };
#else
    // Keep original logic for round watches as it seems to be horizontal centering
  bounds[HOURS_LAYER] = (GRect) {
    .origin = {(WIDTH-(s_bigw*2+MARGIN))/2, hours_y},
    .size = {s_bigw*2+MARGIN, s_bigh}
  };
  bounds[MINUTES_LAYER] = (GRect) {
    .origin = {(WIDTH-(minutes_bigw*2+MARGIN))/2, minutes_y},
    .size = {minutes_bigw*2+MARGIN, minutes_bigh}
  };
#endif
}

void
brutal_clock_load(Layer *root_layer)
{
        glyphs = gbitmap_create_with_resource(RESOURCE_ID_GLYPHS);

#ifndef PBL_PLATFORM_APLITE
    s_scale_buf = malloc((BUFW / 8) * BIGH);
#endif

    screen_bounds = layer_get_bounds(root_layer); // Initialize screen_bounds
    s_hours_scale_x = SCALE_X; // Initialize dynamic hours scale X
    s_hours_scale_y = SCALE_Y; // Initialize dynamic hours scale Y
    s_scale_x = SCALE_X; // Initialize dynamic scale X
    s_scale_y = SCALE_Y; // Initialize dynamic scale Y

        hours = layer_create(bounds[HOURS_LAYER]);
        layer_set_update_proc(hours, Hours);
        layer_add_child(root_layer, hours);
    
    screen_bounds = layer_get_bounds(root_layer); // Store full screen bounds once

        minutes = layer_create(bounds[MINUTES_LAYER]);
        layer_set_update_proc(minutes, Minutes);
        layer_add_child(root_layer, minutes);

    brutal_clock_update_settings();
}

void
brutal_clock_unload()
{
        layer_destroy(hours);
        layer_destroy(minutes);
        gbitmap_destroy(glyphs);

#ifndef PBL_PLATFORM_APLITE
    if (s_scale_buf) {
        free(s_scale_buf);
        s_scale_buf = NULL;
    }
#endif
}

void
brutal_clock_tick(struct tm *_time, TimeUnits change)
{
        if (change & MINUTE_UNIT) {
                layer_mark_dirty(minutes);
        }
        if (change & HOUR_UNIT) {
                layer_mark_dirty(hours);
        }
}

void brutal_clock_update_layout(GRect new_bounds) {
  int current_unobstructed_height = new_bounds.size.h;

  if (s_prev_unobstructed_height == 0) {
    // First call, initialize prev height to full screen
    s_prev_unobstructed_height = screen_bounds.size.h;
  }

  // Check if unobstructed height has changed
  if (current_unobstructed_height != s_prev_unobstructed_height) {
    int offset = s_prev_unobstructed_height - current_unobstructed_height;
    if (current_unobstructed_height < s_prev_unobstructed_height) {
      // Obstruction is appearing
      if (new_bounds.origin.y > 0) {
        // Top obstruction
        s_unobstructed_offset = offset;
      } else {
        // Bottom obstruction
        s_unobstructed_offset = -offset;
      }
      brutal_clock_animate_in_quick_view(s_unobstructed_offset);
    } else {
      // Obstruction is disappearing
      brutal_clock_animate_out_quick_view();
      s_unobstructed_offset = 0; // Reset offset when Quick View is gone
    }
    s_prev_unobstructed_height = current_unobstructed_height;
  } else {
    // No unobstructed height change, so update layout immediately without animation
    update_big_digit_size(s_hours_scale_x, s_hours_scale_y);
    brutal_clock_calculate_bounds();

    layer_set_frame(hours, bounds[HOURS_LAYER]);
    layer_set_frame(minutes, bounds[MINUTES_LAYER]);

    layer_mark_dirty(hours);
    layer_mark_dirty(minutes);
  }

}

void
brutal_clock_update_settings()
{
  config_bg = settings.timeBgColor;
  config_fg = settings.timeColor;
  opacity = 255;

  update_big_digit_size(s_hours_scale_x, s_hours_scale_y); // Update s_bigh and s_bigw
  brutal_clock_calculate_bounds(); // Calculate and set bounds array

  layer_set_frame(hours, bounds[HOURS_LAYER]);
  layer_set_frame(minutes, bounds[MINUTES_LAYER]);

  layer_mark_dirty(hours);
  layer_mark_dirty(minutes);
}