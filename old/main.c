#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/rand.h"

#define PIN_U 22
#define PIN_V 21
#define PIN_W 20
#define GPIO_UP 16
#define GPIO_DOWN 17

#define TABLE_BITS 11
#define TABLE_SIZE (1 << TABLE_BITS) // 2048
#define PHASE_MAX_BITS 32

// Q15固定小数点用のサインテーブル (値の範囲: 0 ～ 32767)
static uint16_t sin_table_q15[TABLE_SIZE];

typedef struct
{
    uint32_t delta_phase;
    uint32_t wrap_val;
    uint16_t mr_q15; // 変調率 (0~32767 -> 0.0~1.0)
    bool sync_mode;
    uint8_t pulse_num;
    uint16_t dither_range; // wrap_valに対するランダム変動幅(±)
} PWM_Params;

// 同期モード用のステップ管理変数
static uint32_t sync_step = 0;

static volatile PWM_Params g_next_params;
static PWM_Params g_cur_params;

uint slice_u, slice_v, slice_w;
uint chan_u, chan_v, chan_w;
uint32_t sysclk_hz;
const float clock_div = 16.0f;

volatile uint32_t phase = 0;
float sin_freq = 0.0f;

typedef struct
{
    float sin_min;
    float sin_max;
    int pulse_mode; // 0: 非同期, 3, 5, 9, 15: 同期
    int carrier_freq;
} VVVF_Table;

static float carrier_random_offset = 0.0f;

VVVF_Table gto[] = {
    // 開始周波数、終了周波数、同期パルス数、キャリア周波数
    {0.0f, 2.0f, 0, 175},
    {2.0f, 4.0f, 0, 197},
    {4.0f, 6.0f, 0, 223},
    {6.0f, 8.0f, 0, 234},
    {8.0f, 10.0f, 0, 262},
    {10.0f, 12.0f, 0, 294},
    {12.0f, 14.0f, 0, 312},
    {14.0f, 16.0f, 0, 350},
    {16.0f, 46.0f, 0, 400},
    {46.0f, 60.0f, 15, 0},
    {60.0f, 70.0f, 9, 0},
    {70.0f, 75.0f, 5, 0}, // 5パルスモード追加
    {75.0f, 80.0f, 3, 0},
    {80.0f, 120.0f, 1, 0},
};

VVVF_Table igbt[] = {
    {0.0f, 60.0f, 0, 1500}, // 非同期
    // {20.0f,   35.0f, 15,    0}, // 15パルス
    // {35.0f,   50.0f,  9,    0}, // 9パルス
    // {50.0f,   60.0f,  5,    0}, // 5パルス
    {60.0f, 70.0f, 3, 0},  // 3パルス
    {70.0f, 120.0f, 1, 0}, // 1パルス
};

// --- グローバルに軽量PRNGの状態を追加 ---
static uint32_t rng_state = 0xABCD1234; // 適当な非ゼロ初期値

static inline uint32_t xorshift32()
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

void __not_in_flash_func(pwm_wrap_irq_handler)()
{
    pwm_clear_irq(slice_u);

    // ダブルバッファリングされたパラメータを適用
    g_cur_params = g_next_params;

    uint32_t applied_wrap = g_cur_params.wrap_val;

    if (g_cur_params.dither_range > 0)
    {
        // -range ~ +range のランダム整数を生成
        int32_t r = (int32_t)(xorshift32() % (g_cur_params.dither_range * 2 + 1)) - g_cur_params.dither_range;
        int32_t new_wrap = (int32_t)applied_wrap + r;
        if (new_wrap < 50)
            new_wrap = 50; // 下限ガード（極端な高キャリア化を防ぐ）
        applied_wrap = (uint32_t)new_wrap;
    }

    // wrap値の更新（全スライス共通）
    pwm_set_wrap(slice_u, applied_wrap);
    pwm_set_wrap(slice_v, applied_wrap);
    pwm_set_wrap(slice_w, applied_wrap);

    // --- 位相（phase）の決定処理 ---
    if (g_cur_params.sync_mode && g_cur_params.pulse_num > 0)
    {
        // 【同期モード】
        uint32_t N = g_cur_params.pulse_num;

        // キャリア1周期あたりの位相幅 = 2^32 / N
        uint64_t step_phase = (1ULL << 32) / N;
        phase += step_phase;

        // 現在のステップから位相を一意に算出 (位相ズレの蓄積を防ぐ)
        // phase = (uint32_t)(sync_step * step_phase);

        sync_step++;
        if (sync_step >= N)
        {
            sync_step = 0;
            // 正弦波1周期完了(0度)のタイミングで、PWM内部カウンタを0に同期リセット
            // pwm_set_counter(slice_u, 0);
            // pwm_set_counter(slice_v, 0);
            // pwm_set_counter(slice_w, 0);
            phase = 0;
        }
    }
    else
    {
        // 【非同期モード】
        phase += g_cur_params.delta_phase;
        sync_step = 0; // 非同期中はステップを初期化しておく
    }

    // テーブルインデックス計算 (32bit位相から上意11bitを抽出)
    uint32_t idx_u = (phase >> (PHASE_MAX_BITS - TABLE_BITS)) & (TABLE_SIZE - 1);
    uint32_t idx_v = (idx_u + (TABLE_SIZE * 1 / 3)) & (TABLE_SIZE - 1);
    uint32_t idx_w = (idx_u + (TABLE_SIZE * 2 / 3)) & (TABLE_SIZE - 1);

    uint32_t su = sin_table_q15[idx_u];
    uint32_t sv = sin_table_q15[idx_v];
    uint32_t sw = sin_table_q15[idx_w];

    // デューティ計算（整数演算のみ）
    // (sin_val * MR) >> 15 により 0~32767 の範囲にスケーリング
    uint32_t du = (((su * g_cur_params.mr_q15) >> 15) * g_cur_params.wrap_val) >> 15;
    uint32_t dv = (((sv * g_cur_params.mr_q15) >> 15) * g_cur_params.wrap_val) >> 15;
    uint32_t dw = (((sw * g_cur_params.mr_q15) >> 15) * g_cur_params.wrap_val) >> 15;

    pwm_set_chan_level(slice_u, chan_u, du);
    pwm_set_chan_level(slice_v, chan_v, dv);
    pwm_set_chan_level(slice_w, chan_w, dw);
}

void pwm_init_set()
{
    gpio_set_function(PIN_U, GPIO_FUNC_PWM);
    gpio_set_function(PIN_V, GPIO_FUNC_PWM);
    gpio_set_function(PIN_W, GPIO_FUNC_PWM);

    slice_u = pwm_gpio_to_slice_num(PIN_U);
    chan_u = pwm_gpio_to_channel(PIN_U);
    slice_v = pwm_gpio_to_slice_num(PIN_V);
    chan_v = pwm_gpio_to_channel(PIN_V);
    slice_w = pwm_gpio_to_slice_num(PIN_W);
    chan_w = pwm_gpio_to_channel(PIN_W);

    sysclk_hz = clock_get_hz(clk_sys);

    pwm_set_clkdiv(slice_u, clock_div);
    pwm_set_clkdiv(slice_v, clock_div);
    pwm_set_clkdiv(slice_w, clock_div);

    pwm_set_phase_correct(slice_u, true);
    pwm_set_phase_correct(slice_v, true);
    pwm_set_phase_correct(slice_w, true);

    // U相のWRAP割り込みを使用
    pwm_clear_irq(slice_u);
    pwm_set_irq_enabled(slice_u, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // 3つのスライスをマスク指定で同時に有効化し、位相を完全同期させる
    uint32_t slice_mask = (1u << slice_u) | (1u << slice_v) | (1u << slice_w);
    pwm_set_mask_enabled(slice_mask);
}

void update_pwm(VVVF_Table mode[], int size)
{
    if (gpio_get(GPIO_UP) == 0)
        sin_freq += 0.05f;
    if (gpio_get(GPIO_DOWN) == 0)
        sin_freq -= 0.05f;
    if (sin_freq < 0.0f)
        sin_freq = 0.0f;
    if (sin_freq > 110.0f)
        sin_freq = 110.0f;

    float mr = 0.0f;
    if (sin_freq > 0.01f)
    {
        mr = (sin_freq > 0.001f) ? (sin_freq * 0.01f) : 0.0f; // 例: 簡易V/f制御
        if (mr > 1.0f)
            mr = 1.0f;
        // mr = mr * 0.01f;
    }
    else
    {
        mr = 0.0f;
    }

    float carrier_f = 4000.0f;
    bool is_sync = false;
    uint8_t pulse_n = 0;

    for (int i = 0; i < size; i++)
    {
        if (sin_freq >= mode[i].sin_min && sin_freq < mode[i].sin_max)
        {
            if (mode[i].pulse_mode == 0)
            {
                carrier_f = (float)mode[i].carrier_freq;
            }
            else
            {
                is_sync = true;
                pulse_n = mode[i].pulse_mode;
                carrier_f = sin_freq * (float)pulse_n;
            }
            break;
        }
    }

    if (carrier_f < 50.0f)
        carrier_f = 50.0f;

    // 次周期用計算パラメータ（割り込み安全に設定）
    PWM_Params local_params;
    local_params.wrap_val = (uint32_t)((float)sysclk_hz / clock_div / (carrier_f * 2.0f)) - 1;

    // Δθ = 2^32 * sin_freq / (carrier_f * 2)  ※Center-alignedのためキャリア割り込みはcarrier_f回/秒
    local_params.delta_phase = (uint32_t)((double)(1ULL << 32) * (double)sin_freq / ((double)carrier_f));
    local_params.mr_q15 = (uint16_t)(mr * 32767.0f);
    local_params.sync_mode = is_sync;
    local_params.pulse_num = pulse_n;

    // 非同期時のみディザを設定（同期モードでは0のまま=ランダム化なし）
    if (!is_sync)
    {
        // local_params.dither_range = (uint16_t)((float)local_params.wrap_val * 0.05f); // ±5%
        local_params.dither_range = 0;
    }
    else
    {
        local_params.dither_range = 0;
    }

    // 割込みハンドラへ渡す構造体の排他アトミック更新
    uint32_t save = save_and_disable_interrupts();
    g_next_params = local_params;
    restore_interrupts(save);
}

void setup()
{
    gpio_init(GPIO_UP);
    gpio_init(GPIO_DOWN);
    gpio_set_dir(GPIO_UP, GPIO_IN);
    gpio_set_dir(GPIO_DOWN, GPIO_IN);
    gpio_pull_up(GPIO_UP);
    gpio_pull_up(GPIO_DOWN);

    // サインテーブル生成 (オフセット+0.5済みの 0.0~1.0 を Q15(0~32767) 化)
    for (int x = 0; x < TABLE_SIZE; x++)
    {
        float rad = (2.0f * M_PI * (float)x) / (float)TABLE_SIZE;
        float val = 0.5f * sinf(rad) + 0.5f; // 0.0 ～ 1.0
        sin_table_q15[x] = (uint16_t)(val * 32767.0f);
    }
}

int main()
{
    stdio_init_all();
    setup();
    pwm_init_set();

    while (true)
    {
        sleep_ms(10);
        update_pwm(gto, sizeof(gto) / sizeof(gto[0]));
        printf("Sin Freq: %f, MR: %f\n", sin_freq, (float)g_cur_params.mr_q15 / 32767.0f);
    }
    return 0;
}