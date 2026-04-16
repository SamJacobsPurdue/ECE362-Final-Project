#include "LEDS.h"

arm_rfft_fast_instance_f32 fft_instance;
float32_t adc_buffer[FFT_SIZE]; //incoming ADC data
float32_t fft_complex_output[FFT_SIZE]; //Temporary array for complex numbers
float32_t frequency_bins[FFT_SIZE / 2]; //usable FFT data

uint16_t raw_adc_buffer[FFT_SIZE]; //16 bit array for the RAW DMA data
uint32_t ROW_COLORS[ROWS];
uint32_t INDEX_LUT[COLS][ROWS];

void init_row_colors(uint8_t brightness){
    ROW_COLORS[0] = set_color(0, 255, 0, brightness); //green
    ROW_COLORS[1] = set_color(0, 255, 0, brightness);
    ROW_COLORS[2] = set_color(0, 255, 0, brightness);
    ROW_COLORS[3] = set_color(0, 255, 0, brightness);
    ROW_COLORS[4] = set_color(255, 225, 0, brightness); //yellow
    ROW_COLORS[5] = set_color(255, 225, 0, brightness);
    ROW_COLORS[6] = set_color(255, 0, 0, brightness); //red
    ROW_COLORS[7] = set_color(255, 0, 0, brightness);
}

void init_index_lut(){

    for(int col = 0; col < COLS; col++){
        for(int row = 0; row < ROWS; row++){
            int physical_row = (ROWS - 1) - row;
            if(col % 2 == 0){
                INDEX_LUT[col][row] = (col * 8) + physical_row;
            }
            else{
                INDEX_LUT[col][row] = (col * 8) + (7 - physical_row);
            }
        }
    }
}

// *frame is pointer to 256 element array that holds LED values
// *heights is and array of 32 integers, containing how many rows from bottom are lit
void fill_frame(uint32_t *frame, int *heights) {
    memset(frame, 0, NUM_PIXELS * sizeof(uint32_t)); //sets every pixel to off

    for (int col = 0; col < COLS; col++) { //loops through all columns
        int h = heights[col]; //how many rows should be lit for this column 
        if(h > ROWS){
            h = ROWS;
        }
        else if(h < 0){
            h = 0;
        }
        
        for(int row = 0; row < h; row++){
            frame[INDEX_LUT[col][row]] = ROW_COLORS[row];
        }
    }
}

// Send the full frame to the matrix (must send all 256 pixels in order)
void send_frame(PIO pio, uint sm, uint32_t *frame) {
    for (int i = 0; i < NUM_PIXELS; i++) {
        put_pixel(pio, sm, frame[i]);
    }
}

void fft_init() {
    arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
}

void process_audio_chunk() {
    // 3. Execute the FFT
    // This takes your raw adc_buffer and writes to fft_complex_output
    // The '0' means forward FFT (not inverse)
    arm_rfft_fast_f32(&fft_instance, adc_buffer, fft_complex_output, 0);

    // 4. Calculate Magnitudes
    // The raw FFT outputs complex numbers (real and imaginary pairs).
    // This function combines them into usable amplitude values for your bins.
    arm_cmplx_mag_f32(fft_complex_output, frequency_bins, FFT_SIZE / 2);
    
    // Now, frequency_bins[0] to frequency_bins[255] contain your frequency data!
}

void init_adc_dma() {
    // 1. Initialize ADC 
    adc_init();
    adc_gpio_init(ADC_PIN); // GPIO 41 is ADC Channel 5 on RP2350
    adc_select_input(1);
    adc_hw->div = 1200 << 8; // 40kHz audio rate
    
    // 2. Setup ADC FIFO safely via SDK
    adc_fifo_setup(
        true,    // Enable FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ asserted when at least 1 sample present
        false,   // No error bit
        false    // Keep full 12-bit resolution (not 8-bit)
    );

    // 3. Initialize DMA Channel 6 via SDK
    dma_channel_config c = dma_channel_get_default_config(6);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false); // Keep reading from same FIFO address
    channel_config_set_write_increment(&c, true); // Increment through your array
    channel_config_set_dreq(&c, DREQ_ADC);        // Pace transfers with ADC

    dma_channel_configure(
        6,              // Channel 6
        &c,             // Config
        raw_adc_buffer, // Write address
        &adc_hw->fifo,  // Read address
        FFT_SIZE,       // Transfer 64 samples
        false           // Do not start yet
    );
}