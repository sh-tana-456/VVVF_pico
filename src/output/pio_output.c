#include "vvvf/output.h"
#include "vvvf/config.h"
#include "vvvf/modulator.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "three_phase.pio.h"
static PIO const pio = pio0;
static uint sm;
static int channels[2];
static uint32_t channel_mask;
static uint32_t buffers[2][VVVF_BLOCK_SAMPLES * VVVF_WORDS_PER_FRAME];
static vvvf_modulator_t modulator;
static volatile vvvf_command_t pending;
static volatile bool fault;
static volatile uint32_t max_render_us;
static uint32_t stall_mask;
static uint32_t ticks_per_frame;
static void fail_output(void) {
    fault = true;
    pio_sm_set_enabled(pio,sm,false);
    /* Abort is unnecessary here: disable DREQ consumer and leave DMA quiescent.
       Keep the SIO output latch low before switching pin functions. */
    for (uint pin=VVVF_PIN_BASE; pin<VVVF_PIN_BASE+3; ++pin) {
        gpio_init(pin); gpio_put(pin,0); gpio_set_dir(pin,GPIO_OUT);
    }
    dma_channel_set_irq0_enabled(channels[0],false);
    dma_channel_set_irq0_enabled(channels[1],false);
}
static void __not_in_flash_func(output_irq)(void) {
    uint32_t completed = dma_hw->ints0 & channel_mask;
    dma_hw->ints0 = completed;
    if (!completed || fault) return;
    if (completed == channel_mask || (pio->fdebug & stall_mask)) {
        fail_output(); return;
    }
    unsigned index = (completed & (1u << channels[0])) ? 0 : 1;
    /* main publishes with interrupts disabled on this same core. */
    vvvf_command_t command = pending;
    uint32_t start = time_us_32();
    size_t words = vvvf_render_timed(&modulator,&command,buffers[index],VVVF_BLOCK_SAMPLES,ticks_per_frame);
    uint32_t elapsed = time_us_32()-start;
    if (elapsed > max_render_us) max_render_us = elapsed;
    /* If the other transfer finished while rendering, do not rearm a stale
       chain: stop instead of repeating old waveform data. */
    if (!dma_channel_is_busy(channels[1-index]) || (pio->fdebug & stall_mask)) {
        fail_output(); return;
    }
    dma_channel_set_read_addr(channels[index],buffers[index],false);
    dma_channel_set_trans_count(channels[index],words,false);
}
void vvvf_output_init(vvvf_command_t initial) {
    pending = initial;
    vvvf_modulator_init();
    sm = pio_claim_unused_sm(pio,true);
    uint offset = pio_add_program(pio,&three_phase_program);
    pio_sm_config config = three_phase_program_get_default_config(offset);
    sm_config_set_out_pins(&config,VVVF_PIN_BASE,3);
    sm_config_set_out_shift(&config,true,true,32);
    sm_config_set_fifo_join(&config,PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config,1.0f);
    /* Exact control-frame duration avoids drift between DDS and PIO timing. */
    uint32_t clock_hz = clock_get_hz(clk_sys);
    if (clock_hz % VVVF_SAMPLE_HZ) panic("clk_sys must be a multiple of sample rate");
    ticks_per_frame = clock_hz/VVVF_SAMPLE_HZ;
    if (ticks_per_frame < 3) panic("control frame too short");
    for (uint pin=VVVF_PIN_BASE; pin<VVVF_PIN_BASE+3; ++pin) {
        gpio_init(pin); gpio_set_dir(pin,GPIO_OUT); gpio_put(pin,0);
        pio_gpio_init(pio,pin);
    }
    pio_sm_init(pio,sm,offset,&config);
    pio_sm_set_pins_with_mask(pio,sm,0,7u << VVVF_PIN_BASE);
    pio_sm_set_consecutive_pindirs(pio,sm,VVVF_PIN_BASE,3,true);
    channels[0] = dma_claim_unused_channel(true);
    channels[1] = dma_claim_unused_channel(true);
    for (unsigned i=0; i<2; ++i) {
        size_t words = vvvf_render_timed(&modulator,&initial,buffers[i],VVVF_BLOCK_SAMPLES,ticks_per_frame);
        dma_channel_config dc = dma_channel_get_default_config(channels[i]);
        channel_config_set_transfer_data_size(&dc,DMA_SIZE_32);
        channel_config_set_read_increment(&dc,true);
        channel_config_set_write_increment(&dc,false);
        channel_config_set_dreq(&dc,pio_get_dreq(pio,sm,true));
        channel_config_set_chain_to(&dc,channels[1-i]);
        channel_config_set_high_priority(&dc,true);
        dma_channel_configure(channels[i],&dc,&pio->txf[sm],buffers[i],words,false);
        channel_mask |= 1u << channels[i];
        dma_channel_set_irq0_enabled(channels[i],true);
    }
    stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stall_mask;
    dma_hw->ints0 = channel_mask;
    irq_set_exclusive_handler(DMA_IRQ_0,output_irq);
    irq_set_priority(DMA_IRQ_0,0x40);
    irq_set_enabled(DMA_IRQ_0,true);
    dma_start_channel_mask(1u << channels[0]);
    /* Preload FIFO before starting so an empty initial pull is not a fault. */
    while (pio_sm_is_tx_fifo_empty(pio,sm)) tight_loop_contents();
    pio_sm_set_enabled(pio,sm,true);
}
void vvvf_output_command(vvvf_command_t command) {
    uint32_t saved = save_and_disable_interrupts();
    pending = command;
    restore_interrupts(saved);
}
bool vvvf_output_fault(void) { return fault; }
uint32_t vvvf_output_max_render_us(void) { return max_render_us; }
