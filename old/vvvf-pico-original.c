#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"

// 設定：使用GPIOピンと周波数
#define PIN_U 22
#define PIN_V 21
#define PIN_W 20
#define GPIO_UP 16
#define GPIO_DOWN 17

#define CARRIER_FREQ_HZ_INIT 4000.0f // 10kHz キャリア
#define SINE_FREQ_HZ_INIT 0.0f       // 10Hz 変調
#define PHASE_MAX (1U << 31)
#define PHASE_RSL (1U << 11)

#define PHASE_120 (PHASE_MAX / 3)
#define PHASE_240 (PHASE_MAX * 2 / 3)

volatile uint32_t phase = 0;
uint32_t delta_phase; // 1キャリア周期ごとの位相ステップ
volatile int carrier_freq = 4000;
volatile float sin_freq = 0.0f;
bool is_sync = false;         // 同期モードフラグ
int g_pulse_mode = 0;         // 0=非同期, 3,5,9,15=同期
volatile int pulse_count = 0; // 同期パルス数カウンタ

// sysclk 周波数取得
uint sysclk_hz;
float clock_div = 16.0f;
// PWM wrap 値（TOP）
volatile uint32_t wrap_val;
volatile uint32_t next_wrap_val;
uint16_t dither_range = 0;
// 各スライス番号・チャネル
uint slice_u, slice_v, slice_w;
uint chan_u, chan_v, chan_w;
float MR = 0.0f;
float sinValues[2 * PHASE_RSL]; // half of sin
float rdmValues[2 * PHASE_RSL];
int rotate_mode = -1;
volatile bool run_rot = false;

typedef struct
{
    int u;
    int v;
    int w;
} PWM_Levels;

PWM_Levels pwm_levels[] = {
    {1, 0, 1},
    {1, 0, 0},
    {1, 1, 0},
    {0, 1, 0},
    {0, 1, 1},
    {0, 0, 1},
};

typedef struct
{
    float sin_min;
    float sin_max;
    int pulse_mode;
    int carrier_freq;
} VVVF_Table;

VVVF_Table gto[] = {
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
    {70.0f, 80.0f, 3, 0},
};

VVVF_Table igbt[] = {
    {0.0f, 40.0f, 0, 1000}, // 非同期
    // {20.0f,   35.0f, 15,    0}, // 15パルス
    // {35.0f,   50.0f,  9,    0}, // 9パルス
    // {50.0f,   60.0f,  5,    0}, // 5パルス
    {40.0f, 50.0f, 9, 0},  // 3パルス
    {50.0f, 120.0f, 3, 0}, // 1パルス
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

/*---------------------------------------------*/
// wrap IRQ ハンドラ（例としてU相のラップIRQを使い、3相同時更新）
void pwm_wrap_irq_handler()
{
    // IRQ フラグクリア（U相スライス）
    pwm_clear_irq(slice_u);

    uint32_t applied_wrap = next_wrap_val;

    if (dither_range > 0)
    {
        // -range ~ +range のランダム整数を生成
        int32_t r = (int32_t)(xorshift32() % (dither_range * 2 + 1)) - dither_range;
        int32_t new_wrap = (int32_t)applied_wrap + r;
        if (new_wrap < 50)
            new_wrap = 50; // 下限ガード（極端な高キャリア化を防ぐ）
        applied_wrap = (uint32_t)new_wrap;
    }

    pwm_set_wrap(slice_u, applied_wrap);
    pwm_set_wrap(slice_v, applied_wrap);
    pwm_set_wrap(slice_w, applied_wrap);
    wrap_val = applied_wrap;

    // // 位相更新
    // phase += delta_phase;
    // phase = phase & (PHASE_MAX - 1);

    // 位相更新
    uint32_t next_phase = phase + delta_phase;

    if (is_sync)
    {
        pulse_count++;
    }

    // 同期モード時、基本波が1周期(PHASE_MAX)を超えたらゼロクロス同期をかける
    if (pulse_count >= (2 * g_pulse_mode) && g_pulse_mode != 0) // next_phase >= PHASE_MAX && g_pulse_mode > 0
    {
        phase = next_phase & (PHASE_MAX - 1); // 位相をリセット
        phase = 0;
        pulse_count = 0;
        // PWMスライスのカウンタを0に強制同期
        pwm_set_counter(slice_u, 0);
        pwm_set_counter(slice_v, 0);
        pwm_set_counter(slice_w, 0);
    }
    else
    {
        phase = next_phase;
    }
    phase = phase & (PHASE_MAX - 1);

    // 3相の振幅計算（浮動小数点 sinf)
    float su = 0, sv = 0, sw = 0;
    if (rotate_mode == 1) // 正転
    {
        // U相: θ
        su = sinValues[phase >> 20];
        // V相: θ + 120° = θ + 4π/3
        sv = sinValues[(phase >> 20) + 683 /*→(2^11 * 1/3)*/];
        // W相: θ + 240° = θ + 2π/3
        sw = sinValues[(phase >> 20) + 1365 /*→(2^11 * 2/3)*/];
    }
    else if (rotate_mode == -1) // 逆転
    {
        // U相: θ
        su = sinValues[phase >> 20];
        // V相: θ + 240° = θ + 2π/3
        sv = sinValues[(phase >> 20) + 1365 /*→(2^11 * 2/3)*/];
        // W相: θ + 120° = θ + 4π/3
        sw = sinValues[(phase >> 20) + 683 /*→(2^11 * 1/3)*/];
    }
    // デューティ（0 ～ wrap_val）の計算: (sin*0.5 + 0.5) を乗算
    uint32_t du = (uint32_t)(MR * (su + 0.5f) * (float)wrap_val); // + rdmValues[phase >> 20]
    uint32_t dv = (uint32_t)(MR * (sv + 0.5f) * (float)wrap_val); // + rdmValues[(phase >> 20) + 683]
    uint32_t dw = (uint32_t)(MR * (sw + 0.5f) * (float)wrap_val); // + rdmValues[(phase >> 20) + 1365]

    if (g_pulse_mode == 3)
    {
        // du = (phase < (PHASE_MAX / 2)) ? wrap_val : 0;
        // dv = (((phase + PHASE_120) & (PHASE_MAX - 1)) < (PHASE_MAX / 2)) ? wrap_val : 0;
        // dw = (((phase + PHASE_240) & (PHASE_MAX - 1)) < (PHASE_MAX / 2)) ? wrap_val : 0;
        du = (uint32_t)((float)MR * (float)(pwm_levels[pulse_count].u * wrap_val));
        dv = (uint32_t)((float)MR * (float)(pwm_levels[pulse_count].v * wrap_val));
        dw = (uint32_t)((float)MR * (float)(pwm_levels[pulse_count].w * wrap_val));
    }

    // 比較レジスタ更新：次周期から反映
    pwm_set_chan_level(slice_u, chan_u, du);
    pwm_set_chan_level(slice_v, chan_v, dv);
    pwm_set_chan_level(slice_w, chan_w, dw);
}

void pwm_init_set()
{
    // PWM GPIO 設定
    gpio_set_function(PIN_U, GPIO_FUNC_PWM);
    gpio_set_function(PIN_V, GPIO_FUNC_PWM);
    gpio_set_function(PIN_W, GPIO_FUNC_PWM);
    slice_u = pwm_gpio_to_slice_num(PIN_U);
    chan_u = pwm_gpio_to_channel(PIN_U);
    slice_v = pwm_gpio_to_slice_num(PIN_V);
    chan_v = pwm_gpio_to_channel(PIN_V);
    slice_w = pwm_gpio_to_slice_num(PIN_W);
    chan_w = pwm_gpio_to_channel(PIN_W);

    // sysclk 周波数取得
    sysclk_hz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) * 1000;

    // clkdiv 設定（ここでは 1.0f、必要に応じ変更可）
    pwm_set_clkdiv(slice_u, clock_div);
    pwm_set_clkdiv(slice_v, clock_div);
    pwm_set_clkdiv(slice_w, clock_div);

    // wrap (TOP) 計算: sysclk / キャリア周波数 - 1 (三角波キャリアにしたため)
    wrap_val = (uint32_t)(sysclk_hz / clock_div / (CARRIER_FREQ_HZ_INIT * 2.0f)) - 1;
    next_wrap_val = wrap_val;
    // wrap_val = (uint32_t)(sysclk_hz / clock_div / (CARRIER_FREQ_HZ_INIT * 1.0f)) - 1;
    pwm_set_wrap(slice_u, wrap_val);
    pwm_set_wrap(slice_v, wrap_val);
    pwm_set_wrap(slice_w, wrap_val);

    pwm_set_phase_correct(slice_u, true);
    pwm_set_phase_correct(slice_v, true);
    pwm_set_phase_correct(slice_w, true);
    // pwm_set_phase_correct(slice_u, false);
    // pwm_set_phase_correct(slice_v, false);
    // pwm_set_phase_correct(slice_w, false);

    // 初期デューティ: 0（出力OFF相当）
    pwm_set_chan_level(slice_u, chan_u, 0);
    pwm_set_chan_level(slice_v, chan_v, 0);
    pwm_set_chan_level(slice_w, chan_w, 0);

    // 正弦位相ステップ Δθ = 2π * SINE_FREQ_HZ / CARRIER_FREQ_HZ
    delta_phase = (float)PHASE_MAX * (float)SINE_FREQ_HZ_INIT / (float)CARRIER_FREQ_HZ_INIT;

    // wrap IRQ を U相スライスで有効化
    pwm_clear_irq(slice_u);
    pwm_set_irq_enabled(slice_u, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // 3つのスライスをマスク指定で同時に有効化し、位相を完全同期させる
    uint32_t slice_mask = (1u << slice_u) | (1u << slice_v) | (1u << slice_w);
    pwm_set_mask_enabled(slice_mask);
    sin_freq = SINE_FREQ_HZ_INIT;
}

void update_pwm(VVVF_Table mode[], int size)
{
    if (gpio_get(GPIO_UP) == 0) // 加速
    {
        sin_freq = sin_freq + 0.05f;
    }
    else if (gpio_get(GPIO_DOWN) == 0) // 減速
    {
        sin_freq = sin_freq - 0.05f;
    }
    if (sin_freq < 0.0f)
    {
        sin_freq = 0.0f;
    }

    if (sin_freq > 0.01f)
    {
        MR = (sin_freq > 0.001f) ? (sin_freq * 0.01f) : 0.0f; // 例: 簡易V/f制御
        if (MR > 1.0f)
            MR = 1.0f;
        // MR = MR * 5.0f;
    }
    else
    {
        MR = 0.0f;
    }

    for (int i = 0; i < size; i++)
    {
        if (sin_freq >= mode[i].sin_min && sin_freq < mode[i].sin_max)
        {
            if (mode[i].pulse_mode == 0)
            {
                is_sync = false;
                g_pulse_mode = 0;
                carrier_freq = mode[i].carrier_freq;
                // if (sin_freq > 10.0f)
                // {
                //     carrier_freq = sin_freq * 100.0f;
                // }
                // wrap (TOP) 計算: sysclk / キャリア周波数 - 1 (三角波キャリアにしたため)
                wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 2.0f)) - 1;
                // wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 1.0f)) - 1;
                // 正弦位相ステップ Δθ = 2π * SINE_FREQ / CARRIER_FREQ
                delta_phase = (uint32_t)((float)PHASE_MAX * (float)sin_freq / (float)carrier_freq);
            }
            else
            {
                is_sync = true;
                g_pulse_mode = mode[i].pulse_mode;
                // TODO 位相もそろえたい　→　パルス数とsin位相からキャリア側の初期カウンタ値が一意に決まるはず。
                carrier_freq = sin_freq * mode[i].pulse_mode * 2;
                // wrap (TOP) 計算: sysclk / キャリア周波数 - 1 (三角波キャリアにしたため)
                wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 2.0f)) - 1;
                // wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 1.0f)) - 1;
                // 正弦位相ステップ Δθ = 2π * SINE_FREQ / CARRIER_FREQ
                delta_phase = (uint32_t)(PHASE_MAX / (mode[i].pulse_mode * 2)); // 1キャリア周期あたりの位相ステップ
            }
            break;
        }
    }

    // 非同期時のみディザを設定（同期モードでは0のまま=ランダム化なし）
    if (!is_sync)
    {
        dither_range = (uint16_t)((float)wrap_val * 0.05f); // ±5%
    }
    else
    {
        dither_range = 0;
    }

    next_wrap_val = wrap_val;
}

void setup()
{
    gpio_init(GPIO_UP);
    gpio_init(GPIO_DOWN);
    gpio_set_dir(GPIO_UP, GPIO_IN);
    gpio_set_dir(GPIO_DOWN, GPIO_IN);
    gpio_pull_up(GPIO_UP);
    gpio_pull_up(GPIO_DOWN);

    for (int x = 0; x < 2 * PHASE_RSL; x++)
    {
        float s = (float)(2.0f * M_PI * (float)x) / (float)PHASE_RSL;
        sinValues[x] = 0.5f * sinf(s);
    }
    for (int x = 0; x < 2 * PHASE_RSL; x++)
    {
        rdmValues[x] = (float)(rand() % 100 - 50) / 1000.0f;
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
        update_pwm(igbt, (sizeof(igbt) / sizeof(igbt[0])));
        printf("Sin Freq: %f, MR: %f, Pulse Count: %d\n", sin_freq, MR, pulse_count);
    }
    return 0;
}
