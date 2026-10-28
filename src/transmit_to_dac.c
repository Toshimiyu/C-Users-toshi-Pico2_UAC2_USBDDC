/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <string.h>
#include "transmit_to_dac.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "i2s_pio_interface.h"

static const PIO pio = pio0;
static const uint sm = 0;
static pio_sm_config sm_config;
static uint offset;

#define DMA_TX_CHAIN_CHANNELS (2)

typedef enum
{
    DMA_CH_STATE_IDLE = 0,
    DMA_CH_STATE_ARMED,
    DMA_CH_STATE_RUNNING
} dma_ch_state_t;

static volatile int dma_ch[DMA_TX_CHAIN_CHANNELS];
static dma_channel_config c_dma[DMA_TX_CHAIN_CHANNELS];
static volatile uint8_t dma_ch_state[DMA_TX_CHAIN_CHANNELS];
DMA_TX_STRUCTURE dma_tx;

volatile bool enable_output = false;

// PWM
static uint16_t pwm_slice;

void __not_in_flash_func(dma_tx_start)(void);
static inline uint32_t dma_tx_active_count(void);
static inline bool dma_tx_any_running(void);
static void __not_in_flash_func(dma_tx_chain_service)(void);

bool calc_pwm_clkdiv_and_wrap_us(float target_period_us, uint16_t *wrap_out, float *clkdiv_out);
void set_pwm_isr_1(float period_us);
void set_period_pwm_us(float period_us);
void pwm_i2s_streaming_rate_change(void);
void __not_in_flash_func(pwm_wrap_handler)(void);

static inline uint32_t dma_tx_active_count(void)
{
    uint32_t count = 0;
    for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
    {
        if (dma_ch_state[i] != DMA_CH_STATE_IDLE)
            count++;
    }
    return count;
}

static inline bool dma_tx_any_running(void)
{
    for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
    {
        if (dma_ch_state[i] == DMA_CH_STATE_RUNNING)
            return true;
    }
    return false;
}

static void __not_in_flash_func(dma_tx_chain_service)(void)
{
    for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
    {
        bool busy = dma_channel_is_busy(dma_ch[i]);
        if (dma_ch_state[i] == DMA_CH_STATE_ARMED)
        {
            if (busy)
                dma_ch_state[i] = DMA_CH_STATE_RUNNING;
        }
        else if (dma_ch_state[i] == DMA_CH_STATE_RUNNING)
        {
            if (!busy)
            {
                dma_ch_state[i] = DMA_CH_STATE_IDLE;
                if (dma_tx.using > 0)
                    dma_tx.using--;
            }
        }
    }

    uint32_t active = dma_tx_active_count();
    int32_t unassigned = (int32_t)dma_tx.using - (int32_t)active;
    if (unassigned < 0)
        unassigned = 0;

    while (unassigned > 0)
    {
        int idle_idx = -1;
        for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
        {
            if (dma_ch_state[i] == DMA_CH_STATE_IDLE)
            {
                idle_idx = i;
                break;
            }
        }
        if (idle_idx < 0)
            break;

        uint32_t rp = dma_tx.rp;
        dma_channel_set_read_addr(dma_ch[idle_idx], dma_tx.data[rp].tx_buf, false);
        dma_channel_set_trans_count(dma_ch[idle_idx], dma_tx.data[rp].tx_size, false);
        dma_tx.rp++;
        if (dma_tx.rp >= DEPTH_DMA_TX_BUFFER)
            dma_tx.rp = 0;
        dma_ch_state[idle_idx] = DMA_CH_STATE_ARMED;
        active++;
        unassigned--;
    }

    if (!dma_tx_any_running())
    {
        int armed_count = 0;
        int start_idx = -1;
        for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
        {
            if (dma_ch_state[i] == DMA_CH_STATE_ARMED)
            {
                armed_count++;
                if (start_idx < 0)
                    start_idx = i;
            }
        }
        if ((armed_count >= 2) && start_idx >= 0)
        {
            dma_channel_start(dma_ch[start_idx]);
            dma_ch_state[start_idx] = DMA_CH_STATE_RUNNING;
        }
    }
}

void reset_i2s_freq(void)
{
    I2S_freq_init(pio, sm, &sm_config, offset);
}

void init_i2s_interface(void)
{
    if(I2S_SLEWRATE_FAST_ENABLE)
    {
        gpio_set_slew_rate(I2S_DATA_PIN, GPIO_SLEW_RATE_SLOW);
        gpio_set_slew_rate(I2S_SIDESET_BASE, GPIO_SLEW_RATE_SLOW);
        gpio_set_slew_rate(I2S_SIDESET_BASE + 1, GPIO_SLEW_RATE_SLOW);
    }

    if(I2S_STRENGTH_REINFORCE_ENABLE)
    {
        gpio_set_drive_strength(I2S_DATA_PIN, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_drive_strength(I2S_SIDESET_BASE, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_drive_strength(I2S_SIDESET_BASE + 1, GPIO_DRIVE_STRENGTH_2MA);
    }

    // PIO I2Sの初期化
    if(I2S_SIDESET_CHANGE)
    {
        I2S_32bit_inv_program_init(pio, sm, I2S_DATA_PIN, I2S_SIDESET_BASE, AUDIO_INITIAL_FREQ, &sm_config, &offset);
    }
    else
    {
        I2S_32bit_program_init(pio, sm, I2S_DATA_PIN, I2S_SIDESET_BASE, AUDIO_INITIAL_FREQ, &sm_config, &offset);
    }

    reset_i2s_freq();

    // DMA setup (chained, no IRQ)
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);

    c_dma[0] = dma_channel_get_default_config(dma_ch[0]);
    channel_config_set_transfer_data_size(&c_dma[0], DMA_SIZE_32);
    channel_config_set_dreq(&c_dma[0], pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c_dma[0], dma_ch[1]);

    c_dma[1] = dma_channel_get_default_config(dma_ch[1]);
    channel_config_set_transfer_data_size(&c_dma[1], DMA_SIZE_32);
    channel_config_set_dreq(&c_dma[1], pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c_dma[1], dma_ch[0]);

    memset((void *)&dma_tx, 0, sizeof(DMA_TX_STRUCTURE));
    memset((void *)dma_ch_state, 0, sizeof(dma_ch_state));

    dma_channel_set_irq0_enabled(dma_ch[0], false);
    dma_channel_set_irq0_enabled(dma_ch[1], false);

    dma_channel_configure(dma_ch[0], &c_dma[0],
                          &pio->txf[sm], dma_tx.data[0].tx_buf, 0, false);
    dma_channel_configure(dma_ch[1], &c_dma[1],
                          &pio->txf[sm], dma_tx.data[0].tx_buf, 0, false);
}

// I2S送信タイミング通知を行う
void __not_in_flash_func(pwm_wrap_handler)(void)
{
    pwm_clear_irq(pwm_slice); // PWM割り込みフラグをクリア
}

// Maximum number of stereo frames copied into one DMA buffer.
#define DMA_MAX_FRAMES (SIZE_DMA_TX_BUF / 2)

static volatile bool enable_output_prev = false;
extern volatile absolute_time_t time_start_output;

void __not_in_flash_func(dma_tx_start)(void)
{
    int32_t level = (int32_t)get_size_using(&buffer_audio) / 2;
    if (level >= SIZE_BUFFER_FB_THRESHOLD)
        enable_output = true;
    else if (level == 0 && dma_tx.using == 0)
        enable_output = false;

    if (enable_output && !enable_output_prev)
    {
        time_start_output = get_absolute_time();
        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        pio_sm_restart(pio, sm);
        reset_i2s_freq();
    }
    enable_output_prev = enable_output;

    if (!enable_output)
    {
        dma_stop_and_clear();
        return;
    }

    while (dma_tx.using < SIZE_DMA_TX_BUF_STACK)
    {
        int32_t frames = (int32_t)get_size_using(&buffer_audio) / 2;
        if (frames <= 0)
            break;
        if (frames > DMA_MAX_FRAMES)
            frames = DMA_MAX_FRAMES;

        const uint32_t wp = dma_tx.wp;
        uint32_t *tx = dma_tx.data[wp].tx_buf;
        if (ringbuf_read_array_spinlock((int32_t *)tx, (uint32_t)frames * 2u, &buffer_audio) < 0)
            break;

        const uint32_t irq_state = save_and_disable_interrupts();
        dma_tx.data[wp].tx_size = (uint32_t)frames * 2u;
        dma_tx.wp = (wp + 1u) % DEPTH_DMA_TX_BUFFER;
        dma_tx.using++;
        restore_interrupts(irq_state);
    }

    dma_tx_chain_service();
}

void dma_stop_and_clear(void)
{
    // DMAを強制停止
    for (int i = 0; i < DMA_TX_CHAIN_CHANNELS; i++)
        dma_channel_abort(dma_ch[i]);

    // DMAステータスをリセット
    dma_hw->ints0 = (1u << dma_ch[0]) | (1u << dma_ch[1]);

    // FIFOバッファをクリア
    pio_sm_clear_fifos(pio0, 0);

    // 使用バッファをゼロクリア
    memset((void *)&dma_tx, 0, sizeof(DMA_TX_STRUCTURE));
    memset((void *)dma_ch_state, 0, sizeof(dma_ch_state));

    // A clock/rate change starts a fresh playback epoch. Require the normal
    // buffer threshold and I2S phase resynchronization before output resumes.
    enable_output = false;
    enable_output_prev = false;
}
