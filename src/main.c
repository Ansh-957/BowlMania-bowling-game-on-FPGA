
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// audio samples
#include "audio_samples.h"

struct audio_t {
	volatile unsigned int control;
	volatile unsigned char rarc;
	volatile unsigned char ralc;
	volatile unsigned char warc;
	volatile unsigned char walc;
    volatile unsigned int ldata;
	volatile unsigned int rdata;
};

volatile struct audio_t *audiop = ((struct audio_t *)0xFF203040);
volatile int audio_index = 0;


#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define TIMER_BASE    0xFF202000
#define TIMER_IRQ_BIT 16
#define TICKS_PER_SEC        100
#define BUBBLE_SHOW_TICKS    (3 * TICKS_PER_SEC)
#define BUBBLE_HIDE_TICKS    (9 * TICKS_PER_SEC)



#define JP1_BASE 0xFF200060
volatile unsigned int *JP1_DATA = (unsigned int *)JP1_BASE;
volatile unsigned int *JP1_DIR  = (unsigned int *)(JP1_BASE + 4);


#define SCL_BIT     0x01   // D0
#define SDA_BIT     0x02   // D1
#define RELEASE_BIT 0x04   // D2 - Swing Button
#define LEFT_BIT    0x08   // D3 - Move Left
#define RIGHT_BIT   0x10   // D4 - Move Right


// MPU6050 Constants
#define MPU_ADDR 0x68
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B



#define BLACK         0x0000
#define WHITE         0xFFFF
#define RED           0xF800
#define DARK_RED      0xA000
#define BLUE          0x001F
#define DARK_BLUE     0x000F
#define WII_BLUE      0x13DF
#define WII_BLUE_DARK 0x09BE
#define WII_BLUE_MID  0x0C5F
#define ALLEY_TAN     0xCC6C
#define ALLEY_TEAL    0x3D11
#define ALLEY_DARK    0x29C3
#define LANE_LIGHT    0xF6D4
#define LANE_MID      0xE5AA
#define LANE_DARK     0xC449
#define LANE_SEAM     0xB408
#define LANE_GUTTERB  0x2945
#define LANE_GUTTER   0x4228
#define PIN_WHITE     0xFFFF
#define PIN_RED_BAND  0xF000
#define PIN_SHADOW    0x2104
#define MII_SKIN      0xFDEB
#define MII_HAIR      0x2124
#define MII_SHIRT_A   0x13DF
#define MII_SHIRT_B   0x09BE
#define MII_COLLAR    0xFFFF
#define MII_PANTS     0x2965
#define MII_SHOE      0x18C3
#define BALL_BLUE     0x041F
#define BALL_BLUE_HI  0x47FF
#define BALL_BLUE_SHD 0x020F
#define BALL_SHINE    0xCFFF
#define BALL_SHADOW   0x2124
#define UI_BG         0x0008
#define UI_BORDER     0x13DF
#define UI_TEXT       0xFFFF
#define UI_ACCENT     0xFD20
#define GRAY          0x8410
#define DARK_GRAY     0x4208
#define LIGHT_GRAY    0xC618
#define SILVER        0xCE79
#define DARK_BROWN    0x4100
#define DARK_GREEN    0x0320
#define SHADOW        0x2104
#define ORANGE        0xFD20
#define YELLOW        0xFFE0



typedef enum {
    STATE_AIMING   = 0,  // Player moves left/right
    STATE_ARMED    = 1,  // D2 held, sensor being sampled for swing
    STATE_THROWING = 2,  // Ball animating down lane
    STATE_RESULT   = 3   // Brief pause after ball reaches pins
} GameState;



volatile int pixel_buffer_start;
short int Buffer1[240][512] __attribute__((aligned(4)));
short int Buffer2[240][512] __attribute__((aligned(4)));
volatile unsigned int g_timer_ticks = 0;


// --- Game ---
GameState g_state      = STATE_AIMING;
int       g_player_x   = 155;   // Mii foot-center x (90..200)
int       g_score      = 0;
int       g_frame_num  = 0;
int       g_throw_num  = 1;     // 1 or 2 within a frame
int       g_game_over  = 0;


// --- Ball animation ---
int   g_ball_x_fixed;           // fixed-point 
int   g_ball_y;                 // screen pixel
int   g_ball_dx_fixed;          // per-tick x velocity 
int   g_ball_speed;             // pixels per tick (y direction)
int   g_ball_power;             // 1-10 (affects speed)
int   g_ball_angle;             // signed, pixels of final x drift
int   g_result_timer;           // ticks to show result screen


// --- Sensor / arm state ---
int   g_peak_accel;             // peak magnitude while armed (raw units)
int   g_arm_ax;                 // X-axis accel at moment of release
int   g_arm_base_x;             // baseline accel X captured when arming
int   g_arm_base_y;             // baseline accel Y captured when arming
int   g_arm_base_z;             // baseline accel Z captured when arming
static int g_was_release_pressed = 0;
static int g_was_start_pressed   = 0;
static int g_show_start_screen   = 1;


// EMA Smoothing
float alpha = 0.2;
float smooth_x = 0, smooth_y = 0, smooth_z = 0;


// --- Pins (10 booleans: 1=standing) ---
int g_pins[10];
int g_last_knocked       = 0;
int g_first_roll_pins    = 0;
int g_tenth_bonus_rolls  = 0;    // remaining bonus balls in frame 10
int g_rolls[21];
int g_roll_count         = 0;



void plot_pixel(int x, int y, short int color);
void clear_screen(short int color);
void draw_line(int x0, int y0, int x1, int y1, short int color);
void draw_filled_rect(int x1, int y1, int x2, int y2, short int color);
void draw_filled_circle(int xc, int yc, int r, short int color);
void draw_circle_outline(int xc, int yc, int r, short int color);


void draw_wii_scene(int player_x);
void draw_indoor_background(void);
void draw_perspective_lane(void);
void draw_lane_arrows(void);
void draw_foul_line(void);
void draw_wii_pins(void);
void draw_mii_character(int player_x);
void draw_wii_ball_at(int bx, int by, int r);
void draw_ui_overlays(void);
void draw_start_screen(void);
void draw_start_pin(int cx, int top);


void draw_side_crowd(int frame);
void draw_side_fan(int x, int y, short int shirt_color, int face_right);
void draw_crowd_camera_flashes(void);
void draw_crowd_speech_bubble(void);


// I2C Bit-Banging Primitives
void delay();
void scl_high(); void scl_low(); void sda_high(); void sda_low(); int sda_read();
void i2c_start(); void i2c_stop(); int i2c_write(uint8_t data); uint8_t i2c_read(int send_ack);
void mpu6050_init(void);
void mpu6050_read_accel(int *ax, int *ay, int *az);


int   abs_val(int v);
int   isqrt(int n);


int lane_left_at_y(int y);
int lane_right_at_y(int y);
int lane_width_at_y(int y);


void wait_for_vsync(void);
void init_interrupts(void);
void init_interval_timer(void);
void interrupt_handler(void);


void draw_char_5x7(int x, int y, char c, short int color, int scale);
void draw_text_5x7(int x, int y, const char *text, short int color, int scale);
int  text_width_5x7(const char *text, int scale);
void draw_digit_box(int x, int y, int n);


void draw_arm_indicator(void);
void draw_ball_in_motion(void);
void update_ball_physics(void);
void start_throw(int player_x, int power, int angle_raw);
void reset_pins(void);
int  count_pins(void);
void knock_pins(int ball_final_x);
void start_new_game(void);
void record_roll(int pins);
int  calculate_score(void);



int abs_val(int v)     { return v < 0 ? -v : v; }
int isqrt(int n) {
    if (n <= 0) return 0;
    int x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n/x) / 2; }
    return x;
}


int lane_left_at_y(int y) {
    int pn = y - 65, pd = 240 - 65;
    if (pn < 0) pn = 0;
    int w = 70 + ((240 - 70) * pn) / pd;
    return 160 - w / 2;
}
int lane_right_at_y(int y) {
    int pn = y - 65, pd = 240 - 65;
    if (pn < 0) pn = 0;
    int w = 70 + ((240 - 70) * pn) / pd;
    return 160 + w / 2;
}
int lane_width_at_y(int y) { return lane_right_at_y(y) - lane_left_at_y(y); }


int main(void) {
    volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
    init_interrupts();


    // Init GPIO
    *JP1_DATA &= ~(SCL_BIT | SDA_BIT);
    *JP1_DIR &= ~(LEFT_BIT | RIGHT_BIT | RELEASE_BIT);
    srand(12345);


    // Init accelerometer via bit-banging
    scl_high(); sda_high();
    mpu6050_init();


    // Double-buffer setup
    *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
    wait_for_vsync();
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen(BLACK);
    *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
    pixel_buffer_start   = *(pixel_ctrl_ptr + 1);
    clear_screen(BLACK);


    audiop->control = 0x8; // clear the output FIFOs
    audiop->control = 0x0; // resume

    start_new_game();
    int frame = 0;


    while (1) {
        // ---- Read buttons ----
        unsigned int jp1 = *JP1_DATA;
        int left_pressed    = !(jp1 & LEFT_BIT);
        int right_pressed   = !(jp1 & RIGHT_BIT);
        int release_pressed = !(jp1 & RELEASE_BIT);

        // ---- Start screen gate ----
        if (g_show_start_screen) {
            volatile int *KEY_ptr = (int *)0xFF200050;
            int key_edge_capture  = *(KEY_ptr + 3);
            int key0_pressed      = (key_edge_capture & 0x1) != 0;
            *(KEY_ptr + 3) = key_edge_capture; // clear edge capture

            int start_pressed = left_pressed || right_pressed || release_pressed || key0_pressed;
            if (start_pressed && !g_was_start_pressed) {
                g_show_start_screen   = 0;
                g_was_start_pressed   = 1;
                g_was_release_pressed = release_pressed;
                start_new_game();
            } else {
                g_was_start_pressed = start_pressed;
                clear_screen(BLACK);
                draw_start_screen();
                wait_for_vsync();
                pixel_buffer_start = *(pixel_ctrl_ptr + 1);
                frame++;
                if (frame >= 24) frame = 0;
                continue;
            }
        }


        // ---- State machine ----
        switch (g_state) {


        case STATE_AIMING:
            if (g_game_over) {
                int restart_pressed = left_pressed || right_pressed || release_pressed;
                if (restart_pressed && !g_was_start_pressed) {
                    start_new_game();
                    g_was_start_pressed = 1;
                } else {
                    g_was_start_pressed = restart_pressed;
                }
                break;
            }

            if (left_pressed) {
                g_player_x -= 4;
                if (g_player_x < 95) g_player_x = 95;
            }
            if (right_pressed) {
                g_player_x += 4;
                if (g_player_x > 200) g_player_x = 200;
            }


            if (release_pressed && !g_was_release_pressed) {
                g_state      = STATE_ARMED;
                g_peak_accel = 0;
                g_arm_ax     = 0;
                // Seed EMA with a fresh read so the baseline is the actual
                // resting orientation (board upright, arm at side).
                {
                    int bx0 = 0, by0 = 0, bz0 = 0;
                    mpu6050_read_accel(&bx0, &by0, &bz0);
                    smooth_x = (float)bx0;
                    smooth_y = (float)by0;
                    smooth_z = (float)bz0;
                }
                g_arm_base_x = (int)smooth_x;
                g_arm_base_y = (int)smooth_y;
                g_arm_base_z = (int)smooth_z;
            }
            break;


        case STATE_ARMED:
            {
                int raw_ax = 0, raw_ay = 0, raw_az = 0;
                mpu6050_read_accel(&raw_ax, &raw_ay, &raw_az);
               
                // Apply EMA filter
                smooth_x = (alpha * raw_ax) + ((1.0f - alpha) * smooth_x);
                smooth_y = (alpha * raw_ay) + ((1.0f - alpha) * smooth_y);
                smooth_z = (alpha * raw_az) + ((1.0f - alpha) * smooth_z);

                // 1. TRUE VECTOR MAGNITUDE (Preventing 32-bit overflow)
                // Shift down by 4 (divide by 16) before squaring. 1g (16384) becomes 1024.
                int sx = ((int)smooth_x) >> 4;
                int sy = ((int)smooth_y) >> 4;
                int sz = ((int)smooth_z) >> 4;

                // Calculate true 3D magnitude using the built-in integer square root function
                int total_force = isqrt((sx * sx) + (sy * sy) + (sz * sz));

                if (total_force > g_peak_accel) {
                    g_peak_accel = total_force;
                    // Capture X-axis tilt for lane drift at peak swing
                    g_arm_ax = -((int)smooth_x - g_arm_base_x);
                }
            }

            // 2. Release ONLY on physical button lift!
            if (!release_pressed && g_was_release_pressed) {
               
                // Base gravity is now exactly 1024. Subtract to get purely dynamic swing force.
                int dynamic_force = g_peak_accel - 1024;
                if (dynamic_force < 0) dynamic_force = 0;

                // Map force to 1-10 power. 
                // A good swing adds about 1g-2g of centrifugal force (1000 to 2000 shifted units).
                int power = (dynamic_force / 150) + 1; // Base power of 1
                if (power > 10) power = 10;

                // Map ax delta to angle offset: ±30 pixel lane drift.
                int angle = g_arm_ax / 200;  
                if (angle < -30) angle = -30;
                if (angle >  30) angle =  30;

                start_throw(g_player_x, power, angle);
                g_state = STATE_THROWING;
            }
            break;

        case STATE_THROWING:
            update_ball_physics();
            break;


        case STATE_RESULT:
            if (g_result_timer > 0) {
                g_result_timer--;
            } else {
                int knocked = g_last_knocked;

                if (g_frame_num < 9) {
                    if (g_throw_num == 1) {
                        g_first_roll_pins = knocked;
                        if (knocked == 10) {
                            // Strike: frame ends immediately.
                            g_frame_num++;
                            g_throw_num = 1;
                            reset_pins();
                        } else {
                            g_throw_num = 2;
                        }
                    } else {
                        g_throw_num = 1;
                        g_first_roll_pins = 0;
                        g_frame_num++;
                        reset_pins();
                    }
                } else {
                    // Frame 10 rules with bonus balls.
                    if (g_throw_num == 1) {
                        g_first_roll_pins = knocked;
                        g_throw_num = 2;
                        if (knocked == 10) {
                            g_tenth_bonus_rolls = 2;
                            reset_pins();
                        }
                    } else if (g_throw_num == 2) {
                        if (g_first_roll_pins == 10) {
                            // Started frame 10 with strike.
                            g_tenth_bonus_rolls--;
                            if (knocked == 10) reset_pins();
                            if (g_tenth_bonus_rolls > 0) {
                                g_throw_num = 3;
                            } else {
                                g_game_over = 1;
                                g_throw_num = 1;
                            }
                        } else {
                            int frame_sum = g_first_roll_pins + knocked;
                            if (frame_sum >= 10) {
                                // Spare in frame 10 grants one bonus ball.
                                g_tenth_bonus_rolls = 1;
                                g_throw_num = 3;
                                reset_pins();
                            } else {
                                g_game_over = 1;
                                g_throw_num = 1;
                            }
                        }
                    } else {
                        // Third ball in frame 10.
                        g_tenth_bonus_rolls = 0;
                        g_game_over = 1;
                        g_throw_num = 1;
                    }
                }
                g_state = STATE_AIMING;
            }
            break;
        }


        g_was_release_pressed = release_pressed;


        // ---- Draw ----
        clear_screen(BLACK);
        draw_wii_scene(g_player_x);
        draw_side_crowd(frame);

        if (g_state == STATE_ARMED)     draw_arm_indicator();
        if (g_state == STATE_THROWING)  draw_ball_in_motion();
        if (g_state == STATE_RESULT)    draw_ball_in_motion();
        draw_ui_overlays();


        // ---- Swap ----
        wait_for_vsync();
        pixel_buffer_start = *(pixel_ctrl_ptr + 1);


        frame++;
        if (frame >= 24) frame = 0;
    }
}



void delay() { volatile int i; for (i = 0; i < 100; i++); }
void scl_high() { *JP1_DIR &= ~SCL_BIT; delay(); }
void sda_high() { *JP1_DIR &= ~SDA_BIT; delay(); }
void scl_low()  { *JP1_DIR |=  SCL_BIT; delay(); }
void sda_low()  { *JP1_DIR |=  SDA_BIT; delay(); }


int sda_read() {
    *JP1_DIR &= ~SDA_BIT;
    delay();
    return (*JP1_DATA & SDA_BIT) ? 1 : 0;
}


void i2c_start() { sda_high(); scl_high(); sda_low(); scl_low(); }
void i2c_stop()  { sda_low(); scl_high(); sda_high(); }


int i2c_write(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) sda_high();
        else             sda_low();
        scl_high(); scl_low();
        data <<= 1;
    }
    sda_high(); scl_high();
    int ack = sda_read();
    scl_low();
    return ack;
}


uint8_t i2c_read(int send_ack) {
    uint8_t data = 0;
    sda_high();
    for (int i = 0; i < 8; i++) {
        data <<= 1;
        scl_high();
        data |= sda_read();
        scl_low();
    }
    if (send_ack) sda_low(); else sda_high();
    scl_high(); scl_low();
    return data;
}


void mpu6050_init(void) {
    i2c_start();
    i2c_write(MPU_ADDR << 1);
    i2c_write(PWR_MGMT_1);
    i2c_write(0x00);
    i2c_stop();
}


void mpu6050_read_accel(int *ax, int *ay, int *az) {
    i2c_start();
    i2c_write(MPU_ADDR << 1);        
    i2c_write(ACCEL_XOUT_H);        
    i2c_start();                    
    i2c_write((MPU_ADDR << 1) | 1);  
   
    uint8_t axh = i2c_read(1), axl = i2c_read(1);
    uint8_t ayh = i2c_read(1), ayl = i2c_read(1);
    uint8_t azh = i2c_read(1), azl = i2c_read(0);
    *ax = (int16_t)(((uint16_t)axh << 8) | axl);
    *ay = (int16_t)(((uint16_t)ayh << 8) | ayl);
    *az = (int16_t)(((uint16_t)azh << 8) | azl);
    i2c_stop();
}



void start_throw(int player_x, int power, int angle_raw) {
    g_ball_power = power;
    g_ball_angle = angle_raw;


    g_ball_x_fixed = (player_x + 16) * 256;
    g_ball_y       = 185;


    // Speed: power 1 → 2px/tick, power 10 → 11px/tick (clear visual difference)
    g_ball_speed   = 1 + power;


    int ticks_to_travel = (g_ball_y - 76) / g_ball_speed;
    if (ticks_to_travel < 1) ticks_to_travel = 1;
    g_ball_dx_fixed = (angle_raw * 256) / ticks_to_travel;
}



void update_ball_physics(void) {
    g_ball_y -= g_ball_speed;
    g_ball_x_fixed += g_ball_dx_fixed;


    int bx = g_ball_x_fixed / 256;
    int y   = g_ball_y;
    int pn  = y - 65; if (pn < 0) pn = 0;
    int pd  = 240 - 65;
    int gw  = 10 + ((48 - 10) * pn) / pd;
    int lmin = lane_left_at_y(y)  - gw;
    int lmax = lane_right_at_y(y) + gw;


    if (bx < lmin) { bx = lmin; g_ball_x_fixed = bx * 256; g_ball_dx_fixed = 0; }
    if (bx > lmax) { bx = lmax; g_ball_x_fixed = bx * 256; g_ball_dx_fixed = 0; }


    if (g_ball_y <= 82) {
        g_ball_y = 82;
        knock_pins(bx);
        record_roll(g_last_knocked);   // Update score immediately after impact.
        g_state        = STATE_RESULT;
        g_result_timer = 30;           // Shorter result pause for faster flow.
    }
}



void draw_ball_in_motion(void) {
    int bx = g_ball_x_fixed / 256;
    int by = g_ball_y;


    int pn = by - 65; if (pn < 0) pn = 0;
    int pd = 240 - 65;
    int r  = 3 + (4 * pn) / pd;  


    draw_filled_circle(bx + 2, by + r + 1, r - 1, SHADOW);
    draw_wii_ball_at(bx, by, r);
}


void draw_wii_ball_at(int cx, int cy, int r) {
    short int CYAN_BALL = 0x05DF;
    draw_filled_circle(cx, cy, r, CYAN_BALL);
    if (r >= 5) draw_filled_circle(cx - 2, cy - 2, 1, WHITE);
    if (r >= 4) {
        plot_pixel(cx + 1, cy - 1, BLACK);
        plot_pixel(cx + 2, cy,     BLACK);
        plot_pixel(cx + 2, cy + 2, BLACK);
    }
    draw_circle_outline(cx, cy, r, BLACK);
}



void knock_pins(int ball_final_x) {
    int px[10] = {160, 153, 167, 146, 160, 174, 139, 153, 167, 181};
    g_last_knocked = 0;

    // Slightly boost hit tolerance for strong, centered throws so strikes
    // are more achievable without becoming common/easy.
    int strike_window_bonus = 0;
    if (abs_val(ball_final_x - 160) <= 10) strike_window_bonus++;
    if (g_ball_power >= 7) strike_window_bonus++;
    int hit_radius = 14 + strike_window_bonus;
    if (hit_radius > 16) hit_radius = 16;

    for (int i = 0; i < 10; i++) {
        if (!g_pins[i]) continue;
        int dist = abs_val(ball_final_x - px[i]);
        if (dist <= hit_radius) {
            g_pins[i] = 0;
            g_last_knocked++;
        }
    }
}


void reset_pins(void) {
    for (int i = 0; i < 10; i++) g_pins[i] = 1;
}


int count_pins(void) {
    int c = 0;
    for (int i = 0; i < 10; i++) c += g_pins[i];
    return c;
}

void start_new_game(void) {
    g_state = STATE_AIMING;
    g_player_x = 155;
    g_score = 0;
    g_frame_num = 0;
    g_throw_num = 1;
    g_game_over = 0;
    g_peak_accel = 0;
    g_arm_ax = 0;
    g_arm_base_x = 0;
    g_arm_base_y = 0;
    g_arm_base_z = 0;
    g_ball_power = 0;
    g_ball_angle = 0;
    g_first_roll_pins = 0;
    g_tenth_bonus_rolls = 0;
    g_last_knocked = 0;
    g_roll_count = 0;
    for (int i = 0; i < 21; i++) g_rolls[i] = 0;
    reset_pins();
}

void record_roll(int pins) {
    if (pins < 0) pins = 0;
    if (pins > 10) pins = 10;
    if (g_roll_count < 21) g_rolls[g_roll_count++] = pins;
    g_score = calculate_score();
}

int calculate_score(void) {
    int score = 0;
    int roll_i = 0;

    for (int frame = 0; frame < 10; frame++) {
        if (roll_i >= g_roll_count) break;

        if (g_rolls[roll_i] == 10) {
            // Strike.
            if (roll_i + 2 >= g_roll_count) break;
            score += 10 + g_rolls[roll_i + 1] + g_rolls[roll_i + 2];
            roll_i += 1;
        } else {
            if (roll_i + 1 >= g_roll_count) break;
            int frame_sum = g_rolls[roll_i] + g_rolls[roll_i + 1];
            if (frame_sum == 10) {
                // Spare.
                if (roll_i + 2 >= g_roll_count) break;
                score += 10 + g_rolls[roll_i + 2];
            } else {
                score += frame_sum;
            }
            roll_i += 2;
        }
    }

    return score;
}



void draw_arm_indicator(void) {
    int pulse = (int)(g_timer_ticks & 0xF);
    short int ring_col = (pulse < 8) ? YELLOW : ORANGE;
    draw_circle_outline(g_player_x, 185, 20, ring_col);
    draw_circle_outline(g_player_x, 185, 21, ring_col);

    // Calculate live dynamic force to draw the visual power bar
    // Base gravity is 1024 shifted units
    int dynamic_force = g_peak_accel - 1024;
    if (dynamic_force < 0) dynamic_force = 0;
    
    int live_power = (dynamic_force / 150) + 1;
    if (live_power > 10) live_power = 10;
    
    int bar_w = live_power * 4;  // 0-40 px width
    
    draw_filled_rect(240, 225, 280, 232, DARK_GRAY);
    if (bar_w > 0) {
        short int bar_col = (bar_w > 30) ? RED : (bar_w > 20) ? YELLOW : 0x07E0;
        draw_filled_rect(240, 225, 240 + bar_w, 232, bar_col);
    }
    draw_text_5x7(242, 215, "SWING!", ring_col, 1);
}



void plot_pixel(int x, int y, short int color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    *(short int *)(pixel_buffer_start + (y << 10) + (x << 1)) = color;
}


void clear_screen(short int color) {
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            plot_pixel(x, y, color);
}


void draw_line(int x0, int y0, int x1, int y1, short int color) {
    int dx = abs_val(x1 - x0), dy = abs_val(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        plot_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}


void draw_filled_rect(int x1, int y1, int x2, int y2, short int color) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            plot_pixel(x, y, color);
}


void draw_filled_circle(int xc, int yc, int r, short int color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                plot_pixel(xc + x, yc + y, color);
}


void draw_circle_outline(int xc, int yc, int r, short int color) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        plot_pixel(xc+x, yc+y, color); plot_pixel(xc-x, yc+y, color);
        plot_pixel(xc+x, yc-y, color); plot_pixel(xc-x, yc-y, color);
        plot_pixel(xc+y, yc+x, color); plot_pixel(xc-y, yc+x, color);
        plot_pixel(xc+y, yc-x, color); plot_pixel(xc-y, yc-x, color);
        if (d < 0) d += 4*x + 6; else { d += 4*(x-y) + 10; y--; }
        x++;
    }
}



void draw_wii_scene(int player_x) {
    draw_indoor_background();
    draw_perspective_lane();
    draw_lane_arrows();
    draw_foul_line();
    draw_wii_pins();
    draw_mii_character(player_x);
    if (g_state == STATE_AIMING || g_state == STATE_ARMED) {
        draw_wii_ball_at(player_x + 16, 192, 7);
        draw_filled_circle(player_x + 12, 192, 2, MII_SKIN);
        draw_circle_outline(player_x + 12, 192, 2, BLACK);
    }
}


void draw_indoor_background(void) {
    // Black title banner with Wreck-It Ralph style title treatment.
    draw_filled_rect(0, 0, 319, 19, BLACK);
    draw_line(0, 19, 319, 19, 0x2104);
    draw_line(0, 20, 319, 20, 0x4208);
    int tw = text_width_5x7("BOWLMANIA", 2);
    int tx = (320 - tw) / 2;
    draw_text_5x7(tx + 2, 5, "BOWLMANIA", 0x01AF, 2);   // blue drop shadow
    draw_text_5x7(tx + 1, 4, "BOWLMANIA", DARK_RED, 2); // darker red edge
    draw_text_5x7(tx,     3, "BOWLMANIA", RED, 2);      // main red fill
    draw_filled_rect(0, 21, 319, 39, 0x18C3);  // dark truss housing
    draw_filled_rect(0, 40, 319, 53, 0x2945);  // secondary truss row
    draw_filled_rect(0, 54, 319, 55, 0x630C);  // metallic trim

    // Animated disco lights
    short int disco_cols[4] = {0x01AF, 0x03FF, 0x0C5F, 0x7BFF}; // deep blue/cyan/sky/ice
    int phase = (int)((g_timer_ticks / 10) & 3);
    for (int x = 24; x < 320; x += 40) {
        short int c = disco_cols[((x / 40) + phase) & 3];
        draw_filled_circle(x, 30, 4, c);
        draw_filled_circle(x, 30, 2, WHITE);
        draw_line(x - 4, 25, x + 4, 25, 0x4208);
    }

    for (int x = 44; x < 320; x += 40) {
        short int c = disco_cols[((x / 40) + phase + 2) & 3];
        draw_filled_circle(x, 46, 3, c);
        draw_filled_circle(x, 46, 1, WHITE);
    }

    draw_filled_rect(0, 56, 319, 74, ALLEY_DARK);
}


void draw_perspective_lane(void) {
    const short int GUTTER_GOLD     = 0xEEA0;
    const short int GUTTER_GOLD_SHD = 0xB560;
    const short int GUTTER_DARK     = 0x2104;
    int top_y  = 65,  bot_y  = 240;
    int top_w  = 70,  bot_w  = 240;
    int top_gw = 10,  bot_gw = 48;
    for (int y = top_y; y < bot_y; y++) {
        int pn = y - top_y, pd = bot_y - top_y;
        int w  = top_w  + ((bot_w  - top_w)  * pn) / pd;
        int gw = top_gw + ((bot_gw - top_gw) * pn) / pd;
        int cx = 160;
        int left = cx - (w / 2), right = cx + (w / 2);
        for (int x = left - gw; x < left; x++) {
            int frac = ((left - x) * 8) / gw;
            short int gc = (frac <= 1 || frac >= 7) ? GUTTER_GOLD : (frac <= 2 || frac >= 6) ? GUTTER_GOLD_SHD : GUTTER_DARK;
            plot_pixel(x, y, gc);
        }
        for (int x = right + 1; x <= right + gw; x++) {
            int frac = ((x - right) * 8) / gw;
            short int gc = (frac <= 1 || frac >= 7) ? GUTTER_GOLD : (frac <= 2 || frac >= 6) ? GUTTER_GOLD_SHD : GUTTER_DARK;
            plot_pixel(x, y, gc);
        }
        plot_pixel(left - 1, y, DARK_BROWN); plot_pixel(right + 1, y, DARK_BROWN);
        int board_px = 3 + (8 * pn) / pd;
        for (int x = left; x <= right; x++) {
            int board_idx = (x - left) / board_px;
            short int wood = (board_idx % 4 == 0) ? LANE_DARK : (board_idx % 2 == 0) ? LANE_LIGHT : LANE_MID;
            if ((x - left) < 6 || (right - x) < 6) wood = LANE_DARK;
            plot_pixel(x, y, wood);
        }
        plot_pixel(left,  y, LANE_LIGHT); plot_pixel(right, y, LANE_LIGHT);
    }
}


void draw_lane_arrows(void) {
    int top_y = 65, bot_y = 240, y_base = 155;
    int pn = y_base - top_y, pd = bot_y  - top_y;
    int w  = 70 + ((240 - 70) * pn) / pd;
    int cx = 160, left = cx - w / 2, right = cx + w / 2, lane_w = right - left;
    int inner_left  = left  + lane_w / 10, inner_right = right - lane_w / 10;
    int spacing = (inner_right - inner_left) / 6;
    for (int i = 0; i < 7; i++) {
        int ax = inner_left + i * spacing;
        plot_pixel(ax, y_base, BLACK); plot_pixel(ax - 1, y_base - 1, BLACK); plot_pixel(ax + 1, y_base - 1, BLACK);
        plot_pixel(ax - 2, y_base - 2, BLACK); plot_pixel(ax + 2, y_base - 2, BLACK);
        plot_pixel(ax - 3, y_base - 3, BLACK); plot_pixel(ax + 3, y_base - 3, BLACK);
    }
}


void draw_foul_line(void) {
    int y = 107, pn = y - 65, pd = 240 - 65;
    int w  = 70 + ((240 - 70) * pn) / pd, cx = 160;
    draw_line(cx - w/2, y,   cx + w/2, y,   BLACK);
    draw_line(cx - w/2, y+1, cx + w/2, y+1, DARK_GRAY);
}


void draw_wii_pins(void) {
    int y_open = 76, top_y = 65, bot_y = 240;
    int pn = y_open - top_y, pd = bot_y - top_y;
    int w  = 70 + ((240 - 70) * pn) / pd, gw = 10 + ((48 - 10) * pn) / pd;
    int tunnel_left  = 160 - w/2 - gw, tunnel_right = 160 + w/2 + gw;
    draw_filled_rect(tunnel_left, 56, tunnel_right, y_open, BLACK);
    draw_filled_rect(tunnel_left - 4, 56, tunnel_left, y_open, 0x18C3);
    draw_filled_rect(tunnel_right, 56, tunnel_right + 4, y_open, 0x18C3);
    int px[10] = {160, 153, 167, 146, 160, 174, 139, 153, 167, 181};
    int py[10] = { 88,  82,  82,  76,  76,  76,  70,  70,  70,  70};
    for (int i = 9; i >= 0; i--) {
        if (!g_pins[i]) continue;
        int cx  = px[i], top = py[i];
        draw_filled_circle(cx, top + 18, 4, PIN_SHADOW);
        draw_line(cx - 1, top + 0,  cx + 1, top + 0,  BLACK); draw_line(cx - 2, top + 1,  cx + 2, top + 1,  BLACK);
        draw_line(cx - 3, top + 2,  cx + 3, top + 2,  BLACK); draw_line(cx - 3, top + 3,  cx + 3, top + 3,  BLACK);
        draw_line(cx - 2, top + 4,  cx + 2, top + 4,  BLACK); draw_line(cx - 2, top + 5,  cx + 2, top + 5,  BLACK);
        draw_line(cx - 2, top + 6,  cx + 2, top + 6,  BLACK); draw_line(cx - 2, top + 7,  cx + 2, top + 7,  BLACK);
        draw_line(cx - 3, top + 8,  cx + 3, top + 8,  BLACK); draw_line(cx - 3, top + 9,  cx + 3, top + 9,  BLACK);
        draw_line(cx - 4, top + 10, cx + 4, top + 10, BLACK); draw_line(cx - 4, top + 11, cx + 4, top + 11, BLACK);
        draw_line(cx - 5, top + 12, cx + 5, top + 12, BLACK); draw_line(cx - 5, top + 13, cx + 5, top + 13, BLACK);
        draw_line(cx - 5, top + 14, cx + 5, top + 14, BLACK); draw_line(cx - 4, top + 15, cx + 4, top + 15, BLACK);
        draw_line(cx - 3, top + 16, cx + 3, top + 16, BLACK); draw_line(cx - 2, top + 17, cx + 2, top + 17, BLACK);
        draw_line(cx - 1, top + 18, cx + 1, top + 18, BLACK);
        draw_line(cx - 1, top + 1,  cx + 1, top + 1,  PIN_WHITE); draw_line(cx - 2, top + 2,  cx + 2, top + 2,  PIN_WHITE);
        draw_line(cx - 2, top + 3,  cx + 2, top + 3,  PIN_WHITE); draw_line(cx - 1, top + 4,  cx + 1, top + 4,  PIN_WHITE);
        draw_line(cx - 1, top + 5,  cx + 1, top + 5,  PIN_RED_BAND); draw_line(cx - 1, top + 6,  cx + 1, top + 6,  PIN_RED_BAND);
        draw_line(cx - 1, top + 7,  cx + 1, top + 7,  PIN_WHITE); draw_line(cx - 2, top + 8,  cx + 2, top + 8,  PIN_RED_BAND);
        draw_line(cx - 2, top + 9,  cx + 2, top + 9,  PIN_RED_BAND); draw_line(cx - 3, top + 10, cx + 3, top + 10, PIN_WHITE);
        draw_line(cx - 3, top + 11, cx + 3, top + 11, PIN_WHITE); draw_line(cx - 4, top + 12, cx + 4, top + 12, PIN_WHITE);
        draw_line(cx - 4, top + 13, cx + 4, top + 13, PIN_WHITE); draw_line(cx - 4, top + 14, cx + 4, top + 14, PIN_WHITE);
        draw_line(cx - 3, top + 15, cx + 3, top + 15, PIN_WHITE); draw_line(cx - 2, top + 16, cx + 2, top + 16, PIN_WHITE);
        draw_line(cx - 1, top + 17, cx + 1, top + 17, PIN_WHITE);
        plot_pixel(cx + 1, top + 1,  LIGHT_GRAY); plot_pixel(cx + 2, top + 2,  LIGHT_GRAY); plot_pixel(cx + 2, top + 3,  LIGHT_GRAY);
        plot_pixel(cx + 1, top + 4,  LIGHT_GRAY); plot_pixel(cx + 1, top + 5,  DARK_RED); plot_pixel(cx + 1, top + 6,  DARK_RED);
        plot_pixel(cx + 1, top + 7,  LIGHT_GRAY); plot_pixel(cx + 2, top + 8,  DARK_RED); plot_pixel(cx + 2, top + 9,  DARK_RED);
        plot_pixel(cx + 3, top + 10, LIGHT_GRAY); plot_pixel(cx + 3, top + 11, LIGHT_GRAY); plot_pixel(cx + 4, top + 12, LIGHT_GRAY);
        plot_pixel(cx + 4, top + 13, LIGHT_GRAY); plot_pixel(cx + 4, top + 14, LIGHT_GRAY); plot_pixel(cx + 3, top + 15, LIGHT_GRAY);
        plot_pixel(cx + 2, top + 16, LIGHT_GRAY); plot_pixel(cx + 1, top + 17, LIGHT_GRAY);
    }
}


void draw_mii_character(int player_x) {
    int cx = player_x, cy = 185;
    draw_filled_circle(cx, cy + 30, 10, SHADOW);
    draw_filled_rect(cx - 6, cy + 5, cx - 2, cy + 25, WHITE); draw_filled_rect(cx + 2, cy + 5, cx + 6, cy + 25, WHITE);
    draw_filled_rect(cx - 9, cy + 26, cx - 1, cy + 29, WHITE); draw_filled_rect(cx + 1, cy + 26, cx + 9, cy + 29, WHITE);
    draw_filled_rect(cx - 9, cy - 15, cx + 9, cy + 4, WII_BLUE); draw_filled_rect(cx - 13, cy - 13, cx - 10, cy - 4, WII_BLUE);
    draw_filled_rect(cx + 10, cy - 13, cx + 13, cy - 4, WII_BLUE); draw_filled_rect(cx - 12, cy - 3, cx - 11, cy + 6, MII_SKIN);
    draw_filled_circle(cx - 11, cy + 7, 2, MII_SKIN); draw_filled_rect(cx + 11, cy - 3, cx + 12, cy + 6, MII_SKIN);
    draw_filled_circle(cx + 12, cy + 7, 2, MII_SKIN); draw_filled_rect(cx - 3, cy - 18, cx + 3, cy - 15, MII_SKIN);
    draw_filled_circle(cx, cy - 30, 13, MII_HAIR);
    draw_circle_outline(cx, cy - 30, 13, BLACK);
    draw_line(cx - 9, cy - 15, cx + 9, cy - 15, BLACK); draw_line(cx - 9, cy - 15, cx - 9, cy + 4,  BLACK);
    draw_line(cx + 9, cy - 15, cx + 9, cy + 4,  BLACK); draw_line(cx - 9, cy + 4,  cx + 9, cy + 4,  BLACK);
    draw_line(cx - 13, cy - 13, cx - 10, cy - 13, BLACK); draw_line(cx - 13, cy - 13, cx - 13, cy - 4,  BLACK);
    draw_line(cx - 13, cy - 4,  cx - 10, cy - 4,  BLACK); draw_line(cx + 10, cy - 13, cx + 13, cy - 13, BLACK);
    draw_line(cx + 13, cy - 13, cx + 13, cy - 4,  BLACK); draw_line(cx + 10, cy - 4,  cx + 13, cy - 4,  BLACK);
    draw_line(cx - 13, cy - 3, cx - 13, cy + 6, BLACK); draw_line(cx - 10, cy - 3, cx - 10, cy + 6, BLACK);
    draw_circle_outline(cx - 11, cy + 7, 2, BLACK); draw_line(cx + 10, cy - 3, cx + 10, cy + 6, BLACK);
    draw_line(cx + 13, cy - 3, cx + 13, cy + 6, BLACK); draw_circle_outline(cx + 12, cy + 7, 2, BLACK);
    draw_line(cx - 7, cy + 5, cx - 7, cy + 25, BLACK); draw_line(cx - 1, cy + 5, cx - 1, cy + 25, BLACK);
    draw_line(cx + 1, cy + 5, cx + 1, cy + 25, BLACK); draw_line(cx + 7, cy + 5, cx + 7, cy + 25, BLACK);
    draw_line(cx - 9, cy + 26, cx, cy + 26, BLACK); draw_line(cx - 9, cy + 26, cx - 9, cy + 29, BLACK);
    draw_line(cx - 9, cy + 29, cx, cy + 29, BLACK); draw_line(cx, cy + 26, cx + 9, cy + 26, BLACK);
    draw_line(cx + 9, cy + 26, cx + 9, cy + 29, BLACK); draw_line(cx, cy + 29, cx + 9, cy + 29, BLACK);
}


void draw_ui_overlays(void) {
    // Compact HUD block so text stays readable over moving lights.
    int hud_x1 = 224, hud_y1 = 22, hud_x2 = 316, hud_y2 = 44;
    draw_filled_rect(hud_x1, hud_y1, hud_x2, hud_y2, 0x0841);
    draw_line(hud_x1, hud_y1, hud_x2, hud_y1, 0x2104);
    draw_line(hud_x1, hud_y2, hud_x2, hud_y2, 0x2104);
    draw_line(hud_x1, hud_y1, hud_x1, hud_y2, 0x2104);
    draw_line(hud_x2, hud_y1, hud_x2, hud_y2, 0x2104);
    draw_text_5x7(228, 25, "SCORE", LIGHT_GRAY, 1);

    char score_str[6];
    int s = g_score;
    if (s > 9999) s = 9999;
    int si = 0;
    if (s == 0) score_str[si++] = '0';
    else {
        int tmp = s, digits = 0, d[4];
        while (tmp > 0) { d[digits++] = tmp % 10; tmp /= 10; }
        for (int k = digits - 1; k >= 0; k--) score_str[si++] = '0' + d[k];
    }
    score_str[si] = '\0';

    draw_text_5x7(272, 25, score_str, WHITE, 1);

    int frame_display = g_frame_num + 1;
    if (frame_display > 10) frame_display = 10;
    char frame_str[3];
    if (frame_display == 10) { frame_str[0] = '1'; frame_str[1] = '0'; frame_str[2] = '\0'; }
    else { frame_str[0] = '0' + frame_display; frame_str[1] = '\0'; }

    draw_text_5x7(228, 35, "FRAME", LIGHT_GRAY, 1);
    draw_text_5x7(268, 35, frame_str, UI_ACCENT, 1);
    draw_text_5x7(280, 35, "/10", WHITE, 1);

    draw_filled_rect(4, 185, 84, 237, UI_BG); draw_filled_rect(5, 186, 83, 236, 0x0821);
    draw_line(4, 185, 84, 185, UI_BORDER); draw_line(4, 237, 84, 237, UI_BORDER);
    draw_line(4, 185, 4, 237, UI_BORDER); draw_line(84, 185, 84, 237, UI_BORDER);
    draw_text_5x7(30, 188, "PINS", LIGHT_GRAY, 1);
    int dpx = 44, dpy = 200, row_gap = 10, col_gap = 13;
    int pin_positions[10][2] = {
        {dpx, dpy}, {dpx - col_gap/2, dpy + row_gap}, {dpx + col_gap/2, dpy + row_gap},
        {dpx - col_gap, dpy + 2*row_gap}, {dpx, dpy + 2*row_gap}, {dpx + col_gap, dpy + 2*row_gap},
        {dpx - 3*col_gap/2, dpy + 3*row_gap}, {dpx - col_gap/2, dpy + 3*row_gap},
        {dpx + col_gap/2, dpy + 3*row_gap}, {dpx + 3*col_gap/2, dpy + 3*row_gap}
    };
    for (int i = 0; i < 10; i++) {
        short int pin_col = g_pins[i] ? WHITE : DARK_GRAY;
        draw_filled_circle(pin_positions[i][0], pin_positions[i][1], 4, pin_col);
        draw_circle_outline(pin_positions[i][0], pin_positions[i][1], 4, DARK_GRAY);
    }

    if (g_game_over) {
        draw_text_5x7(104, 4, "GAME OVER", RED, 1);
        draw_text_5x7(90, 14, "PRESS ANY BTN", WHITE, 1);
    } else if (g_state == STATE_RESULT) {
        int knocked = g_last_knocked;
        const char *callout = 0;
        short int main_col = WHITE;
        short int shadow_col = BLACK;

        if (knocked == 10) {
            callout = "STRIKE";
            main_col = ORANGE;
            shadow_col = DARK_RED;
        } else if (knocked + g_first_roll_pins == 10 && g_throw_num == 2) {
            callout = "SPARE";
            main_col = YELLOW;
            shadow_col = WII_BLUE_DARK;
        }

        if (callout) {
            // Place result text over the lane just above the front pin row.
            int scale = 2;
            int tw = text_width_5x7(callout, scale);
            int tx = 160 - (tw / 2);
            int ty = 54;

            draw_filled_rect(tx - 6, ty - 3, tx + tw + 5, ty + 14, BLACK);
            draw_line(tx - 6, ty - 3, tx + tw + 5, ty - 3, UI_BORDER);
            draw_line(tx - 6, ty + 14, tx + tw + 5, ty + 14, UI_BORDER);
            draw_line(tx - 6, ty - 3, tx - 6, ty + 14, UI_BORDER);
            draw_line(tx + tw + 5, ty - 3, tx + tw + 5, ty + 14, UI_BORDER);

            draw_text_5x7(tx + 2, ty + 2, callout, shadow_col, scale);
            draw_text_5x7(tx, ty, callout, main_col, scale);
        }
    }
}

void draw_start_pin(int cx, int top) {
    // Reuse the same pin style used in the lane pin renderer.
    draw_filled_circle(cx, top + 18, 4, PIN_SHADOW);
    draw_line(cx - 1, top + 0,  cx + 1, top + 0,  BLACK); draw_line(cx - 2, top + 1,  cx + 2, top + 1,  BLACK);
    draw_line(cx - 3, top + 2,  cx + 3, top + 2,  BLACK); draw_line(cx - 3, top + 3,  cx + 3, top + 3,  BLACK);
    draw_line(cx - 2, top + 4,  cx + 2, top + 4,  BLACK); draw_line(cx - 2, top + 5,  cx + 2, top + 5,  BLACK);
    draw_line(cx - 2, top + 6,  cx + 2, top + 6,  BLACK); draw_line(cx - 2, top + 7,  cx + 2, top + 7,  BLACK);
    draw_line(cx - 3, top + 8,  cx + 3, top + 8,  BLACK); draw_line(cx - 3, top + 9,  cx + 3, top + 9,  BLACK);
    draw_line(cx - 4, top + 10, cx + 4, top + 10, BLACK); draw_line(cx - 4, top + 11, cx + 4, top + 11, BLACK);
    draw_line(cx - 5, top + 12, cx + 5, top + 12, BLACK); draw_line(cx - 5, top + 13, cx + 5, top + 13, BLACK);
    draw_line(cx - 5, top + 14, cx + 5, top + 14, BLACK); draw_line(cx - 4, top + 15, cx + 4, top + 15, BLACK);
    draw_line(cx - 3, top + 16, cx + 3, top + 16, BLACK); draw_line(cx - 2, top + 17, cx + 2, top + 17, BLACK);
    draw_line(cx - 1, top + 18, cx + 1, top + 18, BLACK);
    draw_line(cx - 1, top + 1,  cx + 1, top + 1,  PIN_WHITE); draw_line(cx - 2, top + 2,  cx + 2, top + 2,  PIN_WHITE);
    draw_line(cx - 2, top + 3,  cx + 2, top + 3,  PIN_WHITE); draw_line(cx - 1, top + 4,  cx + 1, top + 4,  PIN_WHITE);
    draw_line(cx - 1, top + 5,  cx + 1, top + 5,  PIN_RED_BAND); draw_line(cx - 1, top + 6,  cx + 1, top + 6,  PIN_RED_BAND);
    draw_line(cx - 1, top + 7,  cx + 1, top + 7,  PIN_WHITE); draw_line(cx - 2, top + 8,  cx + 2, top + 8,  PIN_RED_BAND);
    draw_line(cx - 2, top + 9,  cx + 2, top + 9,  PIN_RED_BAND); draw_line(cx - 3, top + 10, cx + 3, top + 10, PIN_WHITE);
    draw_line(cx - 3, top + 11, cx + 3, top + 11, PIN_WHITE); draw_line(cx - 4, top + 12, cx + 4, top + 12, PIN_WHITE);
    draw_line(cx - 4, top + 13, cx + 4, top + 13, PIN_WHITE); draw_line(cx - 4, top + 14, cx + 4, top + 14, PIN_WHITE);
    draw_line(cx - 3, top + 15, cx + 3, top + 15, PIN_WHITE); draw_line(cx - 2, top + 16, cx + 2, top + 16, PIN_WHITE);
    draw_line(cx - 1, top + 17, cx + 1, top + 17, PIN_WHITE);
    plot_pixel(cx + 1, top + 1,  LIGHT_GRAY); plot_pixel(cx + 2, top + 2,  LIGHT_GRAY); plot_pixel(cx + 2, top + 3,  LIGHT_GRAY);
    plot_pixel(cx + 1, top + 4,  LIGHT_GRAY); plot_pixel(cx + 1, top + 5,  DARK_RED); plot_pixel(cx + 1, top + 6,  DARK_RED);
    plot_pixel(cx + 1, top + 7,  LIGHT_GRAY); plot_pixel(cx + 2, top + 8,  DARK_RED); plot_pixel(cx + 2, top + 9,  DARK_RED);
    plot_pixel(cx + 3, top + 10, LIGHT_GRAY); plot_pixel(cx + 3, top + 11, LIGHT_GRAY); plot_pixel(cx + 4, top + 12, LIGHT_GRAY);
    plot_pixel(cx + 4, top + 13, LIGHT_GRAY); plot_pixel(cx + 4, top + 14, LIGHT_GRAY); plot_pixel(cx + 3, top + 15, LIGHT_GRAY);
    plot_pixel(cx + 2, top + 16, LIGHT_GRAY); plot_pixel(cx + 1, top + 17, LIGHT_GRAY);
}

void draw_start_screen(void) {
    // Keep the start screen clean: title + pins + bowling ball.
    draw_filled_rect(0, 0, 319, 239, BLACK);
    for (int x = 0; x < 320; x += 16) {
        short int col = ((x / 16) & 1) ? 0x000A : 0x0010;
        draw_filled_rect(x, 0, x + 7, 239, col);
    }

    // Big title with red fill and blue shadow (Wreck-It inspired style)
    int scale_title = 6;
    int w_top = text_width_5x7("BOWL", scale_title);
    int x_top = (320 - w_top) / 2;
    int w_bot = text_width_5x7("MANIA", scale_title);
    int x_bot = (320 - w_bot) / 2;
    int y_top = 42;
    int y_bot = 92;

    draw_text_5x7(x_top + 4, y_top + 4, "BOWL", 0x01AF, scale_title);
    draw_text_5x7(x_bot + 4, y_bot + 4, "MANIA", 0x01AF, scale_title);
    draw_text_5x7(x_top + 1, y_top + 1, "BOWL", DARK_RED, scale_title);
    draw_text_5x7(x_bot + 1, y_bot + 1, "MANIA", DARK_RED, scale_title);
    draw_text_5x7(x_top, y_top, "BOWL", RED, scale_title);
    draw_text_5x7(x_bot, y_bot, "MANIA", RED, scale_title);

    // Row of pins under the title (same style as in-game lane pins).
    int pin_top = 150;
    int pin_spacing = 22;
    for (int i = -2; i <= 2; i++) {
        draw_start_pin(160 + i * pin_spacing, pin_top);
    }

    // Bowling ball near the pins.
    draw_filled_circle(160, 176, 10, BALL_BLUE);
    draw_filled_circle(156, 172, 2, BALL_SHINE);
    draw_filled_circle(165, 173, 1, BLACK);
    draw_filled_circle(167, 177, 1, BLACK);
    draw_filled_circle(162, 179, 1, BLACK);
    draw_circle_outline(160, 176, 10, BLACK);

    // Blinking prompt
    if (((g_timer_ticks / 40) & 1) == 0) {
        int press_w = text_width_5x7("PRESS BUTTON", 2);
        int press_x = (320 - press_w) / 2;
        draw_text_5x7(press_x + 2, 218, "PRESS BUTTON", WII_BLUE_DARK, 2);
        draw_text_5x7(press_x, 216, "PRESS BUTTON", WHITE, 2);
    }
}



void wait_for_vsync(void) {
    volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
    *pixel_ctrl_ptr = 1;
    while ((*(pixel_ctrl_ptr + 3) & 0x01) != 0);
}


static inline unsigned int read_mcause(void)  { unsigned int v; asm volatile("csrr %0, mcause"  : "=r"(v)); return v; }
static inline unsigned int read_mstatus(void) { unsigned int v; asm volatile("csrr %0, mstatus" : "=r"(v)); return v; }
static inline void write_mstatus(unsigned int v) { asm volatile("csrw mstatus, %0" :: "r"(v)); }
static inline unsigned int read_mie(void)  { unsigned int v; asm volatile("csrr %0, mie" : "=r"(v)); return v; }
static inline void write_mie(unsigned int v) { asm volatile("csrw mie, %0" :: "r"(v)); }
static inline void write_mtvec(void (*h)(void)) { asm volatile("csrw mtvec, %0" :: "r"(h)); }


void init_interval_timer(void) {
    volatile int *timer_ptr = (int *)TIMER_BASE;
    const int period = 100000000 / 8000; // 8 kHz to match audio sample rate
    *(timer_ptr + 0) = 0;
    *(timer_ptr + 2) = (period & 0xFFFF);
    *(timer_ptr + 3) = ((period >> 16) & 0xFFFF);
    *(timer_ptr + 1) = 0x7;
}


void init_interrupts(void) {
    write_mstatus(read_mstatus() & ~(1u << 3));
    write_mtvec(interrupt_handler);
    init_interval_timer();
    write_mie(read_mie() | (1u << TIMER_IRQ_BIT));
    write_mstatus(read_mstatus() | (1u << 3));
}


static int audio_tick_counter = 0;

void __attribute__((interrupt("machine"))) interrupt_handler(void) {
    const unsigned int IRQ_PREFIX  = 0x80000000u;
    const unsigned int TIMER_CAUSE = IRQ_PREFIX | TIMER_IRQ_BIT;
    volatile int *timer_ptr = (int *)TIMER_BASE;
    *(timer_ptr + 0) = 0;

    if (read_mcause() == TIMER_CAUSE) {
        if (audiop->warc > 0) {
            audiop->ldata = samples[audio_index];
            audiop->rdata = samples[audio_index];
            audio_index++;
            if (audio_index >= samples_n) {
                audio_index = 0;
            }
        }

        audio_tick_counter++;
        if (audio_tick_counter >= 80) { // 8000 / 100 = 80 -> 100 Hz game ticks
            audio_tick_counter = 0;
            g_timer_ticks++;
        }
    }
}



void draw_side_fan(int x, int y, short int shirt_color, int face_right) {
    (void)face_right;
    draw_filled_rect(x - 2, y,     x + 2, y + 3, MII_SKIN);
    draw_filled_rect(x - 2, y,     x + 2, y + 1, MII_HAIR);
    plot_pixel(x - 1, y + 2, BLACK); plot_pixel(x + 1, y + 2, BLACK);
    plot_pixel(x,     y + 3, DARK_RED);
    draw_filled_rect(x - 3, y + 4, x + 3, y + 8, shirt_color);
    draw_filled_rect(x - 4, y + 5, x - 4, y + 8, MII_SKIN);
    draw_filled_rect(x + 4, y + 5, x + 4, y + 8, MII_SKIN);
    draw_filled_rect(x - 2, y + 9, x - 1, y + 12, WHITE);
    draw_filled_rect(x + 1, y + 9, x + 2, y + 12, WHITE);
    draw_filled_rect(x - 2, y + 13, x - 1, y + 13, GRAY);
    draw_filled_rect(x + 1, y + 13, x + 2, y + 13, GRAY);
}


void draw_side_crowd(int frame) {
    const short int shirt_palette[3] = { BLUE, RED, DARK_GREEN };
    int fan_i = 0;
    for (int y = 75; y <= 138; y += 6) {
        int p_num = y - 65, p_den = 240 - 65;
        int w  = 70 + ((240 - 70) * p_num) / p_den, gw = 10 + ((48 - 10) * p_num) / p_den;
        int left  = 160 - (w / 2), right = 160 + (w / 2);
        int left_limit  = left  - gw - 12, right_limit = right + gw + 12;
        int row_offset = ((y / 6) % 2) ? 3 : 0;
        for (int x = 4 + row_offset; x <= left_limit; x += 6) {
            int color_idx = fan_i % 3, bob = ((fan_i + frame / 6) & 1);
            draw_side_fan(x, y + bob, shirt_palette[color_idx], 1);
            fan_i++;
        }
        for (int x = 316 - row_offset; x >= right_limit; x -= 6) {
            int color_idx = fan_i % 3, bob = ((fan_i + frame / 6) & 1);
            draw_side_fan(x, y + bob, shirt_palette[color_idx], 0);
            fan_i++;
        }
    }
    draw_crowd_camera_flashes(); draw_crowd_speech_bubble();
}


void draw_crowd_camera_flashes(void) {
    typedef struct { int x; int y; int ttl; } flash_t;
    static flash_t flashes[6] = {{0,0,0}};
    static int flash_tick = 0;
    if ((flash_tick++ % 4) == 0) {
        int spawn_slot = -1;
        for (int i = 0; i < 6; i++) if (flashes[i].ttl <= 0) { spawn_slot = i; break; }
        if (spawn_slot >= 0) {
            int y = 75 + (rand() % 64);
            int pn = y - 65, pd = 240 - 65;
            int w  = 70 + ((240 - 70) * pn) / pd, gw = 10 + ((48 - 10) * pn) / pd;
            int left  = 160 - (w / 2), right = 160 + (w / 2);
            int left_limit  = left  - gw - 12, right_limit = right + gw + 12;
            int side = rand() & 1, x_min, x_max;
            if (side == 0) { x_min = 4; x_max = left_limit - 2; }
            else           { x_min = right_limit + 2; x_max = 316; }
            if (x_max >= x_min) {
                int x = x_min + (rand() % (x_max - x_min + 1));
                flashes[spawn_slot].x = x; flashes[spawn_slot].y = y; flashes[spawn_slot].ttl = 2 + (rand() & 1);
            }
        }
    }
    for (int i = 0; i < 6; i++) {
        if (flashes[i].ttl > 0) {
            int x = flashes[i].x, y = flashes[i].y, r = (flashes[i].ttl >= 3) ? 3 : 2;
            draw_filled_circle(x, y, r + 1, LIGHT_GRAY); draw_filled_circle(x, y, r, WHITE);
            draw_line(x - (r+2), y, x + (r+2), y, WHITE); draw_line(x, y - (r+2), x, y + (r+2), WHITE);
            flashes[i].ttl--;
        }
    }
}


void draw_crowd_speech_bubble(void) {
    typedef struct { const char *l1; const char *l2; const char *l3; } bubble_msg_t;
    static const bubble_msg_t msgs[] = {
        {"YOU SUCK", NULL, NULL}, {"DONT SELL", "MY PARLAY", NULL},
        {"YOU GOT", "THIS", NULL}, {"LETS DO", "THIS", NULL},
        {"I HAVE", "MONEY ON", "YOU"}, {"GOAT", NULL, NULL},
        {"THE BEST", NULL, NULL}, {"CMON", NULL, NULL}, {"BOWL", NULL, NULL}
    };
    const int msg_count = sizeof(msgs) / sizeof(msgs[0]);
    static int          showing          = 0;
    static unsigned int next_toggle_tick = 40;
    static int          message_idx      = -1;
    static int          side             = 0;


    unsigned int now = g_timer_ticks;
    if ((int)(now - next_toggle_tick) >= 0) {
        if (showing) { showing = 0; next_toggle_tick = now + BUBBLE_HIDE_TICKS; side = rand() & 1; }
        else {
            if (msg_count > 0) {
                int prev_idx = message_idx, next_idx = rand() % msg_count;
                if (msg_count > 1) { while (next_idx == prev_idx) next_idx = rand() % msg_count; }
                message_idx = next_idx;
            }
            showing = 1; next_toggle_tick = now + BUBBLE_SHOW_TICKS;
        }
    }
    if (!showing) return;


    const bubble_msg_t *m = &msgs[message_idx];
    int lines  = 1 + (m->l2 != NULL) + (m->l3 != NULL);
    int text_w = text_width_5x7(m->l1, 1);
    if (m->l2) { int w2 = text_width_5x7(m->l2, 1); if (w2 > text_w) text_w = w2; }
    if (m->l3) { int w3 = text_width_5x7(m->l3, 1); if (w3 > text_w) text_w = w3; }


    int pad_x = 3, pad_y = 2, line_gap = 1;
    int bubble_w = text_w + (pad_x * 2), bubble_h = lines * 7 + (lines - 1) * line_gap + (pad_y * 2);
    const int fan_rows[3] = {80, 96, 112};
    int anchor_y = fan_rows[message_idx % 3];
    int pn = anchor_y - 65, pd = 240 - 65;
    int w  = 70 + ((240 - 70) * pn) / pd, gw = 10 + ((48 - 10) * pn) / pd;
    int left  = 160 - (w / 2), right = 160 + (w / 2);
    int left_limit  = left  - gw - 12, right_limit = right + gw + 12;


    int anchor_x, bubble_x;
    if (side == 0) {
        anchor_x = left_limit - 18; bubble_x = anchor_x - bubble_w + 5;
        if (bubble_x < 4) bubble_x = 4;
        if (bubble_x + bubble_w > left_limit - 1) bubble_x = left_limit - 1 - bubble_w;
    } else {
        anchor_x = right_limit + 18; bubble_x = anchor_x - 5;
        if (bubble_x < right_limit + 1) bubble_x = right_limit + 1;
        if (bubble_x + bubble_w > 316)  bubble_x = 316 - bubble_w;
    }
    int bubble_y = anchor_y - bubble_h - 6;
    if (bubble_y < 75) bubble_y = 75;


    draw_filled_rect(bubble_x + 2, bubble_y, bubble_x + bubble_w - 2, bubble_y + bubble_h, WHITE);
    draw_filled_rect(bubble_x, bubble_y + 2, bubble_x + bubble_w, bubble_y + bubble_h - 2, WHITE);
    draw_line(bubble_x + 2, bubble_y, bubble_x + bubble_w - 2, bubble_y, BLACK);
    draw_line(bubble_x, bubble_y + 2, bubble_x, bubble_y + bubble_h - 2, BLACK);
    draw_line(bubble_x + bubble_w, bubble_y + 2, bubble_x + bubble_w, bubble_y + bubble_h - 2, BLACK);
    draw_line(bubble_x+1, bubble_y+1, bubble_x+1, bubble_y+1, BLACK);
    draw_line(bubble_x+bubble_w-1, bubble_y+1, bubble_x+bubble_w-1, bubble_y+1, BLACK);
    draw_line(bubble_x+1, bubble_y+bubble_h-1, bubble_x+1, bubble_y+bubble_h-1, BLACK);
    draw_line(bubble_x+bubble_w-1, bubble_y+bubble_h-1, bubble_x+bubble_w-1, bubble_y+bubble_h-1, BLACK);


    int yb = bubble_y + bubble_h;
    if (side == 0) {
        int s0=bubble_x+8, e0=bubble_x+16, s1=bubble_x+7, e1=bubble_x+13, s2=bubble_x+6, e2=bubble_x+10;
        int s3=bubble_x+5, e3=bubble_x+8, s4=bubble_x+4, e4=bubble_x+6;
        draw_line(bubble_x+2, yb, s0-1, yb, BLACK); draw_line(e0+1, yb, bubble_x+bubble_w-2, yb, BLACK);
        draw_filled_rect(s0,yb,e0,yb,WHITE); draw_filled_rect(s1,yb+1,e1,yb+1,WHITE);
        draw_filled_rect(s2,yb+2,e2,yb+2,WHITE); draw_filled_rect(s3,yb+3,e3,yb+3,WHITE);
        draw_filled_rect(s4,yb+4,e4,yb+4,WHITE);
        draw_line(s0,yb,s4,yb+4,BLACK); draw_line(e0,yb,e4,yb+4,BLACK); draw_line(s4,yb+4,e4,yb+4,BLACK);
    } else {
        int s0=bubble_x+bubble_w-16, e0=bubble_x+bubble_w-8, s1=bubble_x+bubble_w-13, e1=bubble_x+bubble_w-7;
        int s2=bubble_x+bubble_w-10, e2=bubble_x+bubble_w-6, s3=bubble_x+bubble_w-8, e3=bubble_x+bubble_w-5;
        int s4=bubble_x+bubble_w-6, e4=bubble_x+bubble_w-4;
        draw_line(bubble_x+2, yb, s0-1, yb, BLACK); draw_line(e0+1, yb, bubble_x+bubble_w-2, yb, BLACK);
        draw_filled_rect(s0,yb,e0,yb,WHITE); draw_filled_rect(s1,yb+1,e1,yb+1,WHITE);
        draw_filled_rect(s2,yb+2,e2,yb+2,WHITE); draw_filled_rect(s3,yb+3,e3,yb+3,WHITE);
        draw_filled_rect(s4,yb+4,e4,yb+4,WHITE);
        draw_line(s0,yb,s4,yb+4,BLACK); draw_line(e0,yb,e4,yb+4,BLACK); draw_line(s4,yb+4,e4,yb+4,BLACK);
    }
    int ty = bubble_y + pad_y;
    draw_text_5x7(bubble_x + pad_x, ty, m->l1, BLACK, 1);
    if (m->l2) { ty += 7 + line_gap; draw_text_5x7(bubble_x + pad_x, ty, m->l2, BLACK, 1); }
    if (m->l3) { ty += 7 + line_gap; draw_text_5x7(bubble_x + pad_x, ty, m->l3, BLACK, 1); }
}


void draw_digit_box(int x, int y, int n) {
    char s[3];
    if (n == 10) { s[0]='1'; s[1]='0'; s[2]='\0'; } else { s[0]='0'+n; s[1]='\0'; }
    draw_filled_circle(x, y, 4, WHITE); draw_circle_outline(x, y, 4, DARK_GRAY);
    if (n == 10) draw_text_5x7(x - 5, y - 3, s, BLACK, 1); else draw_text_5x7(x - 2, y - 3, s, BLACK, 1);
}


void draw_char_5x7(int x, int y, char c, short int color, int scale) {
    unsigned char rows[7] = {0,0,0,0,0,0,0};
    switch (c) {
        case '0': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x13;rows[3]=0x15;rows[4]=0x19;rows[5]=0x11;rows[6]=0x0E;break;
        case '1': rows[0]=0x04;rows[1]=0x0C;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x04;rows[6]=0x0E;break;
        case '2': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x01;rows[3]=0x02;rows[4]=0x04;rows[5]=0x08;rows[6]=0x1F;break;
        case '3': rows[0]=0x1F;rows[1]=0x02;rows[2]=0x04;rows[3]=0x02;rows[4]=0x01;rows[5]=0x11;rows[6]=0x0E;break;
        case '4': rows[0]=0x02;rows[1]=0x06;rows[2]=0x0A;rows[3]=0x12;rows[4]=0x1F;rows[5]=0x02;rows[6]=0x02;break;
        case '5': rows[0]=0x1F;rows[1]=0x10;rows[2]=0x1E;rows[3]=0x01;rows[4]=0x01;rows[5]=0x11;rows[6]=0x0E;break;
        case '6': rows[0]=0x06;rows[1]=0x08;rows[2]=0x10;rows[3]=0x1E;rows[4]=0x11;rows[5]=0x11;rows[6]=0x0E;break;
        case '7': rows[0]=0x1F;rows[1]=0x01;rows[2]=0x02;rows[3]=0x04;rows[4]=0x08;rows[5]=0x08;rows[6]=0x08;break;
        case '8': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x0E;rows[4]=0x11;rows[5]=0x11;rows[6]=0x0E;break;
        case '9': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x0F;rows[4]=0x01;rows[5]=0x02;rows[6]=0x0C;break;
        case 'A': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x1F;rows[4]=0x11;rows[5]=0x11;rows[6]=0x11;break;
        case 'B': rows[0]=0x1E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x1E;rows[4]=0x11;rows[5]=0x11;rows[6]=0x1E;break;
        case 'C': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x10;rows[3]=0x10;rows[4]=0x10;rows[5]=0x11;rows[6]=0x0E;break;
        case 'D': rows[0]=0x1E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x11;rows[4]=0x11;rows[5]=0x11;rows[6]=0x1E;break;
        case 'E': rows[0]=0x1F;rows[1]=0x10;rows[2]=0x10;rows[3]=0x1E;rows[4]=0x10;rows[5]=0x10;rows[6]=0x1F;break;
        case 'F': rows[0]=0x1F;rows[1]=0x10;rows[2]=0x10;rows[3]=0x1E;rows[4]=0x10;rows[5]=0x10;rows[6]=0x10;break;
        case 'G': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x10;rows[3]=0x17;rows[4]=0x11;rows[5]=0x11;rows[6]=0x0F;break;
        case 'H': rows[0]=0x11;rows[1]=0x11;rows[2]=0x11;rows[3]=0x1F;rows[4]=0x11;rows[5]=0x11;rows[6]=0x11;break;
        case 'I': rows[0]=0x1F;rows[1]=0x04;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x04;rows[6]=0x1F;break;
        case 'K': rows[0]=0x11;rows[1]=0x12;rows[2]=0x14;rows[3]=0x18;rows[4]=0x14;rows[5]=0x12;rows[6]=0x11;break;
        case 'L': rows[0]=0x10;rows[1]=0x10;rows[2]=0x10;rows[3]=0x10;rows[4]=0x10;rows[5]=0x10;rows[6]=0x1F;break;
        case 'M': rows[0]=0x11;rows[1]=0x1B;rows[2]=0x15;rows[3]=0x15;rows[4]=0x11;rows[5]=0x11;rows[6]=0x11;break;
        case 'N': rows[0]=0x11;rows[1]=0x19;rows[2]=0x15;rows[3]=0x13;rows[4]=0x11;rows[5]=0x11;rows[6]=0x11;break;
        case 'O': rows[0]=0x0E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x11;rows[4]=0x11;rows[5]=0x11;rows[6]=0x0E;break;
        case 'P': rows[0]=0x1E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x1E;rows[4]=0x10;rows[5]=0x10;rows[6]=0x10;break;
        case 'R': rows[0]=0x1E;rows[1]=0x11;rows[2]=0x11;rows[3]=0x1E;rows[4]=0x14;rows[5]=0x12;rows[6]=0x11;break;
        case 'S': rows[0]=0x0F;rows[1]=0x10;rows[2]=0x10;rows[3]=0x0E;rows[4]=0x01;rows[5]=0x01;rows[6]=0x1E;break;
        case 'T': rows[0]=0x1F;rows[1]=0x04;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x04;rows[6]=0x04;break;
        case 'U': rows[0]=0x11;rows[1]=0x11;rows[2]=0x11;rows[3]=0x11;rows[4]=0x11;rows[5]=0x11;rows[6]=0x0E;break;
        case 'V': rows[0]=0x11;rows[1]=0x11;rows[2]=0x11;rows[3]=0x11;rows[4]=0x11;rows[5]=0x0A;rows[6]=0x04;break;
        case 'W': rows[0]=0x11;rows[1]=0x11;rows[2]=0x11;rows[3]=0x15;rows[4]=0x15;rows[5]=0x15;rows[6]=0x0A;break;
        case 'X': rows[0]=0x11;rows[1]=0x0A;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x0A;rows[6]=0x11;break;
        case 'Y': rows[0]=0x11;rows[1]=0x11;rows[2]=0x0A;rows[3]=0x04;rows[4]=0x04;rows[5]=0x04;rows[6]=0x04;break;
        case '/': rows[0]=0x01;rows[1]=0x02;rows[2]=0x04;rows[3]=0x04;rows[4]=0x08;rows[5]=0x10;rows[6]=0x10;break;
        case '!': rows[0]=0x04;rows[1]=0x04;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x00;rows[6]=0x04;break;
        case '.': rows[0]=0x00;rows[1]=0x00;rows[2]=0x00;rows[3]=0x00;rows[4]=0x00;rows[5]=0x00;rows[6]=0x04;break;
        case ' ': break;
        default:  rows[0]=0x1F;rows[1]=0x11;rows[2]=0x04;rows[3]=0x04;rows[4]=0x04;rows[5]=0x00;rows[6]=0x04;break;
    }
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (rows[row] & (1 << (4 - col)))
                draw_filled_rect(x + col*scale, y + row*scale,
                                 x + col*scale + scale - 1, y + row*scale + scale - 1, color);
}


void draw_text_5x7(int x, int y, const char *text, short int color, int scale) {
    int cx = x;
    while (*text) { draw_char_5x7(cx, y, *text, color, scale); cx += 6*scale; text++; }
}


int text_width_5x7(const char *text, int scale) {
    int len = 0;
    while (text[len]) len++;
    return len * 6 * scale;
}

