//////////////////////GITHUB REPOS THAT WE USED//////////////////////////
//SD Card/SPI: https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
//WS2812: https://github.com/raspberrypi/pico-examples/blob/master/pio/ws2812/ws2812.pio
#include <stdio.h>
#include "communication.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "LEDS.h"
#include "ws2812.pio.h"

#define BRIGHTNESS 50

extern FIL fil;

int main() {

    stdio_init_all();

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    UINT bytes_per_block = SAMPLES_PER_BLOCK * sizeof(int16_t); // FatFs needs the size in bytes. 512 16-bit samples = 1024 bytes etc
    int16_t __attribute__((aligned(4))) audio_buffer[SAMPLES_PER_BLOCK];
    uint32_t frame[NUM_PIXELS];
    int heights[COLS];
    int new_heights[COLS]; //new heights gets updated each loop


    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);
    pio_sm_set_enabled(pio, sm, true);


    i2c_bus_recover(); //might not need
    sd_dummy_clock_flush(); //might not need
    SD_card_init();
    start_i2c_dma();
    fft_init();
    init_adc_dma();
    init_index_lut();
    init_row_colors(BRIGHTNESS); //number is brightness

    memset(heights, 0, sizeof(heights)); //zeroes the whole matrix
    memset(new_heights, 0, sizeof(new_heights));

    dma_channel_set_write_addr(6, raw_adc_buffer, false); //this chunk could probably be put into a function somewhere
    dma_channel_set_read_addr(6, &adc_hw->fifo, false);
    dma_channel_set_trans_count(6, FFT_SIZE, true); // true = trigger
    adc_hw->cs |= 0x00000008; // Start ADC

    printf("Clearing matrix...\n");
    memset(heights, 0, sizeof(heights));
    fill_frame(frame, heights);
    send_frame(pio, sm, frame); // Force the LEDs to actually turn off
    sleep_ms(100);

    read_audio(audio_buffer, bytes_per_block); //these 4 function calls prefill the audio buffers, might not need it tbh
    format_audio_for_i2c(audio_buffer, dma_buffer0);
    read_audio(audio_buffer, bytes_per_block);
    format_audio_for_i2c(audio_buffer, dma_buffer1);



    dma_channel_start(dma_chan0); //starts the dma channel for transfers

    while (true) {
        while(dma_hw->ch[6].ctrl_trig & (1 << 24)){ //waits for the DMA to finish collecting 64 samples (FFT)
            tight_loop_contents();
        }

        dma_channel_set_write_addr(6, raw_adc_buffer, false);
        dma_channel_set_read_addr(6, &adc_hw->fifo, false);
        dma_channel_set_trans_count(6, FFT_SIZE, true);

        // 3. Convert raw 16-bit integers to floats AND remove DC offset
        float sum = 0.0f;
        for(int i = 0; i < FFT_SIZE; i++){
            adc_buffer[i] = (float)raw_adc_buffer[i];
            sum += adc_buffer[i];
        }

        //Calculate and subtract the DC offset (Average voltage)
        float dc_offset = sum / FFT_SIZE;
        for(int i = 0; i < FFT_SIZE; i++){
            adc_buffer[i] -= dc_offset;
        }
        

        process_audio_chunk();

        for(int i = 0; i < COLS; i++){

            int scaled_height = (int)(frequency_bins[i] / 400.0f); //change division value to be (signal amplitude / 3.3) * 4096 = X
                                                                    // X * 32 = Y
                                                                    // Y / 8 = divison value
            if(scaled_height > ROWS){
                scaled_height = ROWS;
            }
            new_heights[i] = scaled_height;
        }
        
        bool changed = false;

        for(int i = 0; i < COLS; i++){
            if(new_heights[i] != heights[i]){
                changed = true;
                break;
            }
        }
        
        if(changed){
            memcpy(heights, new_heights, sizeof(heights));
            fill_frame(frame, heights);
            send_frame(pio, sm, frame);
        }


        if (refill_buffer0) {
            refill_buffer0 = false;
            read_audio(audio_buffer, bytes_per_block);
            format_audio_for_i2c(audio_buffer, dma_buffer0);
        }
        if (refill_buffer1) {
            refill_buffer1 = false;
            read_audio(audio_buffer, bytes_per_block);
            format_audio_for_i2c(audio_buffer, dma_buffer1);
        }
    }
}