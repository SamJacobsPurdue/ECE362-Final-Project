#include "communication.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"   
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "SD_config.h" 

FATFS fs;
FIL fil;
FRESULT fr;

uint16_t dma_buffer0[SAMPLES_PER_BLOCK * 2];
uint16_t dma_buffer1[SAMPLES_PER_BLOCK * 2];

int dma_chan0;
int dma_chan1;

volatile bool refill_buffer0 = false;
volatile bool refill_buffer1 = false;


void start_i2c_dma() {
    i2c_bus_recover();
    i2c_init(i2c1, I2C_BAUDRATE);
    gpio_set_function(I2C1_SCL, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(I2C1_SCL);
    gpio_pull_up(I2C1_SDA);

    i2c1_hw->enable = 0;
    i2c1_hw->tar = I2C_ADDR;  // 0x60
    i2c1_hw->enable = 1;

    dma_chan0 = 10;
    dma_chan1 = 11;
    dma_claim_mask((1u << 10) | (1u << 11));

    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t wrap_value = (sys_clk / (SAMPLE_RATE * 2)) - 1;

    pwm_config p_cfg = pwm_get_default_config();
    pwm_config_set_wrap(&p_cfg, wrap_value);
    pwm_init(PWM_SLICE, &p_cfg, true);

    // Configure channel 0 properly using SDK
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, DREQ_PWM_WRAP0 + PWM_SLICE);
    channel_config_set_chain_to(&c0, dma_chan1);

    dma_channel_configure(
        dma_chan0,
        &c0,
        &i2c1_hw->data_cmd,  // write to I2C data register
        dma_buffer0,          // read from buffer 0
        I2C_CMDS_PER_BLOCK,  // number of transfers
        false                 // don't start yet
    );

    // Configure channel 1
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_16);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, DREQ_PWM_WRAP0 + PWM_SLICE);
    channel_config_set_chain_to(&c1, dma_chan0);

    dma_channel_configure(
        dma_chan1,
        &c1,
        &i2c1_hw->data_cmd,
        dma_buffer1,
        I2C_CMDS_PER_BLOCK,
        false
    );

    dma_channel_set_irq1_enabled(dma_chan0, true);
    dma_channel_set_irq1_enabled(dma_chan1, true);
    dma_hw->ints1 = (1u << dma_chan0) | (1u << dma_chan1);
    irq_clear(DMA_IRQ_1);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}

void format_audio_for_i2c(int16_t *audio_data, uint16_t *target_dma_buf) {
    for (int i = 0; i < SAMPLES_PER_BLOCK; i++) {

        // 1. Convert 16-bit signed WAV to 12-bit unsigned DAC value
        // Add 32768 to shift the wave up so the lowest point is 0.
        // Shift right by 4 (>> 4) to shrink it from 16-bit down to 12-bit.
        uint16_t unsigned_12bit = (uint16_t)((audio_data[i] + 32768) >> 4);

        // 2. Pack into MCP4725 Fast Mode format
        // First Byte: Control bits (0000) + Upper 4 data bits
        target_dma_buf[i * 2] = (unsigned_12bit >> 8) & 0x0F; 
        
        // Second Byte: Lower 8 data bits
        target_dma_buf[(i * 2) + 1] = unsigned_12bit & 0xFF;
    }
}

void dma_irq_handler() {


    if (dma_hw->ints1 & (1u << dma_chan0)) { // Check if Channel 0 is finished
        dma_hw->ints1 = 1u << dma_chan0; // Clear interrupt flag
        dma_channel_set_read_addr(dma_chan0, dma_buffer0, false); // Reset read address for the next time it's triggered
        refill_buffer0 = true; // Tell main loop to get new data
    }
    
    if (dma_hw->ints1 & (1u << dma_chan1)) { // Check if Channel 1 is finished
        dma_hw->ints1 = 1u << dma_chan1; // Clear interrupt flag
        dma_channel_set_read_addr(dma_chan1, dma_buffer1, false); // Reset read address for the next time it's triggered
        refill_buffer1 = true; // Tell main loop to get new SD data
    }
}

void SD_card_init(){
    gpio_pull_up(16); // MISO
    gpio_pull_up(19); // MOSI
    gpio_pull_up(17); // CS

    gpio_set_drive_strength(19, GPIO_DRIVE_STRENGTH_12MA); // MOSI
    gpio_set_drive_strength(18, GPIO_DRIVE_STRENGTH_12MA); // SCK
    gpio_set_drive_strength(17, GPIO_DRIVE_STRENGTH_12MA);

    // Mount the drive
    fr = f_mount(&fs, "0:", 1); 
    if (fr != FR_OK) {
        printf("Error mounting SD card. Code: %d\n", fr);
        while (true);
    }
    printf("SD card mounted!\n");

    sleep_ms(100);

    fr = f_open(&fil, "0:ECE362.wav", FA_READ);
    if (fr != FR_OK) {
        printf("Error opening file. Code: %d\n", fr);
        f_unmount("0:");
        while (true);
    }
    f_lseek(&fil, 512); //seeks past the 44 byte WAV header

}

void read_audio(int16_t *audio_data, UINT bytes_to_read) {
    UINT bytes_read;
    f_read(&fil, audio_data, bytes_to_read, &bytes_read);

    if (bytes_read != bytes_to_read) {
        printf("Short read! Got %u of %u bytes\n", bytes_read, bytes_to_read);
    }
}

void i2c_bus_recover() {
    // Take manual control of pins
    gpio_init(I2C1_SDA);
    gpio_init(I2C1_SCL);
    gpio_set_dir(I2C1_SDA, GPIO_OUT);
    gpio_set_dir(I2C1_SCL, GPIO_OUT);
    
    gpio_put(I2C1_SDA, 1);
    gpio_put(I2C1_SCL, 1);
    sleep_us(10);

    // Toggle SCL 9 times to clock out stuck byte
    for (int i = 0; i < 9; i++) {
        gpio_put(I2C1_SCL, 0);
        sleep_us(10);
        gpio_put(I2C1_SCL, 1);
        sleep_us(10);
        // Check if SDA released
        gpio_set_dir(I2C1_SDA, GPIO_IN);
        if (gpio_get(I2C1_SDA)) {
            printf("SDA released after %d clocks\n", i + 1);
            break;
        }
        gpio_set_dir(I2C1_SDA, GPIO_OUT);
        gpio_put(I2C1_SDA, 1);
    }

    // Send STOP condition
    gpio_set_dir(I2C1_SDA, GPIO_OUT);
    gpio_put(I2C1_SDA, 0);
    sleep_us(10);
    gpio_put(I2C1_SCL, 1);
    sleep_us(10);
    gpio_put(I2C1_SDA, 1);
    sleep_us(10);

    // Release pins back to I2C
    gpio_deinit(I2C1_SDA);
    gpio_deinit(I2C1_SCL);
    
    printf("Bus recovery done\n");
    sleep_ms(10);
}


void sd_dummy_clock_flush() {
    // Define your physical SPI pins
    uint cs_pin = 17;   
    uint sck_pin = 18;  
    uint mosi_pin = 19; 

    // Take manual control of the GPIO pins away from the SPI hardware
    gpio_init(cs_pin);
    gpio_init(sck_pin);
    gpio_init(mosi_pin);

    gpio_set_dir(cs_pin, GPIO_OUT);
    gpio_set_dir(sck_pin, GPIO_OUT);
    gpio_set_dir(mosi_pin, GPIO_OUT);

    // 1. Pull CS HIGH (This tells the SD card to ignore incoming commands)
    gpio_put(cs_pin, 1);
    
    // 2. Keep MOSI HIGH (Standard idle state for SPI data lines)
    gpio_put(mosi_pin, 1);

    // 3. Manually toggle the clock line 100 times
    // The SD card requires at least 74 clock cycles to fully reset its internal state
    for (int i = 0; i < 100; i++) {
        gpio_put(sck_pin, 0);
        sleep_us(10); // Keep the clock relatively slow
        gpio_put(sck_pin, 1);
        sleep_us(10);
    }

    // 4. Release the pins so the FatFs library can re-initialize them properly
    gpio_deinit(cs_pin);
    gpio_deinit(sck_pin);
    gpio_deinit(mosi_pin);
    
    // Give the card a few milliseconds to settle after the flush
    sleep_ms(10); 
}

