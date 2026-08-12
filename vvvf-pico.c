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
#define PIN_V PICO_DEFAULT_LED_PIN
#define PIN_W 20
#define GPIO_UP 16
#define GPIO_DOWN 17

#define CARRIER_FREQ_HZ_INIT 4000.0f // 10kHz キャリア
#define SINE_FREQ_HZ_INIT 0.0f       // 10Hz 変調
#define PHASE_MAX (1U << 31)
#define PHASE_RSL (1U << 11)

volatile uint32_t phase = 0;
uint32_t delta_phase; // 1キャリア周期ごとの位相ステップ
volatile int carrier_freq = 4000;
volatile float sin_freq = 0.0f;
int table_size;

// sysclk 周波数取得
uint sysclk_hz;
float clock_div = 8.0f;
// PWM wrap 値（TOP）
volatile uint32_t wrap_val;
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

/*---------------------------------------------*/
// wrap IRQ ハンドラ（例としてU相のラップIRQを使い、3相同時更新）
void pwm_wrap_irq_handler()
{
    // IRQ フラグクリア（U相スライス）
    pwm_clear_irq(slice_u);

    // 位相更新
    phase += delta_phase;
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
    pwm_set_wrap(slice_u, wrap_val);
    pwm_set_wrap(slice_v, wrap_val);
    pwm_set_wrap(slice_w, wrap_val);

    pwm_set_phase_correct(slice_u, true);
    pwm_set_phase_correct(slice_v, true);
    pwm_set_phase_correct(slice_w, true);

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

    // PWM 有効化（フリーラン開始）
    pwm_set_enabled(slice_u, true);
    pwm_set_enabled(slice_v, true);
    pwm_set_enabled(slice_w, true);
}

void update_pwm(VVVF_Table mode[], int size)
{
    if (gpio_get(GPIO_UP) == 0) // 加速
    {
        sin_freq = sin_freq + 0.1f;
    }
    else if (gpio_get(GPIO_DOWN) == 0) // 減速
    {
        sin_freq = sin_freq - 0.1f;
    }
    if (sin_freq < 0.0f)
    {
        sin_freq = 0.0f;
    }

    if (sin_freq > 0.001f)
    {
        MR = 0.002f; // sin_freq * 0.001f;
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
                carrier_freq = mode[i].carrier_freq;
                // wrap (TOP) 計算: sysclk / キャリア周波数 - 1 (三角波キャリアにしたため)
                wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 2.0f)) - 1;
                // 正弦位相ステップ Δθ = 2π * SINE_FREQ / CARRIER_FREQ
                delta_phase = (float)PHASE_MAX * (float)sin_freq / (float)carrier_freq;
            }
            else
            {
                // TODO 位相もそろえたい　→　パルス数とsin位相からキャリア側の初期カウンタ値が一意に決まるはず。
                carrier_freq = sin_freq * mode[i].pulse_mode;
                // wrap (TOP) 計算: sysclk / キャリア周波数 - 1 (三角波キャリアにしたため)
                wrap_val = (uint32_t)(sysclk_hz / clock_div / (carrier_freq * 2.0f)) - 1;
                // 正弦位相ステップ Δθ = 2π * SINE_FREQ / CARRIER_FREQ
                delta_phase = (float)PHASE_MAX * (float)sin_freq / (float)carrier_freq;
            }
            break;
        }
    }

    pwm_set_wrap(slice_u, wrap_val);
    pwm_set_wrap(slice_v, wrap_val);
    pwm_set_wrap(slice_w, wrap_val);
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

    table_size = sizeof(gto) / sizeof(gto[0]);
}

int main()
{
    stdio_init_all();
    setup();
    pwm_init_set();

    while (true)
    {
        sleep_ms(10);
        update_pwm(gto, table_size);
    }
    return 0;
}
