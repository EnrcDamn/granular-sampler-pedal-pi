// Granular sampler and delay for Raspberry Pedal Pi

#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Define Input Pins
#define PUSH1 	        RPI_GPIO_P1_08      //GPIO14
#define PUSH2           RPI_V2_GPIO_P1_38  	//GPIO20 
#define TOGGLE_SWITCH 	RPI_V2_GPIO_P1_32 	//GPIO12
#define FOOT_SWITCH     RPI_GPIO_P1_10 		//GPIO15
#define LED             RPI_V2_GPIO_P1_36 	//GPIO16

#define TRUE 1
#define FALSE 0
#define PRESSED 0

#define SAMPLE_RATE 50000 // 50kSPS

// Define Delay Effect parameters: 50000 is 1 second approx.
#define DELAY_MAX 250000 // 5 seconds
#define DELAY_MIN 0

long LED_timer = 0;
long current_sample = 0;

uint32_t Delay_Buffer[DELAY_MAX];
uint32_t sample_is_randomized = FALSE; // Randomize flag
uint32_t grain_counter = 0;
uint32_t max_grain_size = 25000; // 0.5 seconds
uint32_t min_grain_size = 500;
uint32_t grain_size = 0;
uint32_t random_start = 0;
uint32_t input_signal = 0;
uint32_t output_signal = 0;
uint32_t read_timer = 0;
uint32_t playback_mode = 0;
uint32_t starting_sample = 0;
uint32_t ending_sample = DELAY_MAX - 1;
 
uint8_t FOOT_SWITCH_val;
uint8_t TOGGLE_SWITCH_val;
uint8_t PUSH1_val;
uint8_t is_PUSH1_pressed;
uint8_t PUSH2_val;
uint8_t recording = FALSE;
uint8_t is_reversed = FALSE; // randomly choose TRUE or FALSE
uint8_t LED_blinking = FALSE;
uint8_t LED_value = TRUE;
uint8_t is_buffer_maxed_out = FALSE;


/*
// LFO
static void build_sine_table(int16_t *data, int wave_length) 
{
    const double LFO_FREQUENCY = 5; // LFO frequency [Hz]
    const double SAMPLE_RATE = 44100; // Sample rate [Hz]
    const double CONVERSION_FACTOR = 32768.0;
    double phase_increment = (2 * pi) / (double)wave_length;
    double current_phase = 0;  // Initial phase [rad]
    double dphase = ((2 * pi * LFO_FREQUENCY) / SAMPLE_RATE); // Phase rate of change [rad/sample]
    double lfo = 0;

    for(int i = 0; i < wave_length; i++) {

      (int)(sin(lfo) * INT16_MAX);
      int sample = (int)((sin(current_phase) * INT16_MAX));
      lfo += dphase;
      current_phase += phase_increment + (0.2 * lfo);

      data[i] =  (data[i] / CONVERSION_FACTOR) + (int16_t)sample;
}
*/

 
int main(int argc, char **argv)
{
    // Start the BCM2835 Library to access GPIO.
    if (!bcm2835_init())
    {
      printf("bcm2835_init failed. Are you running as root??\n");
      return 1;
    }
	// Start the SPI BUS.
	if (!bcm2835_spi_begin())
    {
      printf("bcm2835_spi_begin failed. Are you running as root??\n");
      return 1;
    }
 
    // Define PWM	
    bcm2835_gpio_fsel(18,BCM2835_GPIO_FSEL_ALT5 ); // PWM0 signal on GPIO18    
    bcm2835_gpio_fsel(13,BCM2835_GPIO_FSEL_ALT0 ); // PWM1 signal on GPIO13    
    bcm2835_pwm_set_clock(2); // Max clk frequency (19.2MHz/2 = 9.6MHz)
    bcm2835_pwm_set_mode(0,1 , 1); // channel 0, markspace mode, PWM enabled. 
    bcm2835_pwm_set_range(0,64);   // channel 0, 64 is max range (6bits): 9.6MHz/64=150KHz switching PWM freq.
    bcm2835_pwm_set_mode(1, 1, 1); // channel 1, markspace mode, PWM enabled.
    bcm2835_pwm_set_range(1,64);   // channel 0, 64 is max range (6bits): 9.6MHz/64=150KHz switching PWM freq.
 
    // Define SPI bus configuration
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);      // The default
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);                   // The default
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64); 	  // 4MHz clock with _64 
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);                      // The default
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);      // the default
 
    uint8_t mosi[10] = { 0x01, 0x00, 0x00 }; // 12 bit ADC read 0x08 ch0, - 0c for ch1 
    uint8_t miso[10] = { 0 };
 
    // Define GPIO pins configuration
    bcm2835_gpio_fsel(PUSH1, BCM2835_GPIO_FSEL_INPT); 			// PUSH1 button as input
    bcm2835_gpio_fsel(PUSH2, BCM2835_GPIO_FSEL_INPT); 			// PUSH2 button as input
    bcm2835_gpio_fsel(TOGGLE_SWITCH, BCM2835_GPIO_FSEL_INPT);	// TOGGLE_SWITCH as input
    bcm2835_gpio_fsel(FOOT_SWITCH, BCM2835_GPIO_FSEL_INPT); 	// FOOT_SWITCH as input
    bcm2835_gpio_fsel(LED, BCM2835_GPIO_FSEL_OUTP);				// LED as output

    bcm2835_gpio_set_pud(PUSH1, BCM2835_GPIO_PUD_UP);           // PUSH1 pull-up enabled   
    bcm2835_gpio_set_pud(PUSH2, BCM2835_GPIO_PUD_UP);           // PUSH2 pull-up enabled 
    bcm2835_gpio_set_pud(TOGGLE_SWITCH, BCM2835_GPIO_PUD_UP);   // TOGGLE_SWITCH pull-up enabled 
    bcm2835_gpio_set_pud(FOOT_SWITCH, BCM2835_GPIO_PUD_UP);     // FOOT_SWITCH pull-up enabled 
 
    // Main Loop
    while(1)
	{
        // Read 12 bits ADC
        bcm2835_spi_transfernb(mosi, miso, 3);
        input_signal = miso[2] + ((miso[1] & 0x0F) << 8); 
    
        // Read the PUSH buttons approx. every 0.2 seconds to save resources
        read_timer++;
        if (read_timer >= SAMPLE_RATE / 5)
        {
            read_timer = 0;
            uint8_t PUSH1_val = bcm2835_gpio_lev(PUSH1);
            uint8_t PUSH2_val = bcm2835_gpio_lev(PUSH2);
            TOGGLE_SWITCH_val = bcm2835_gpio_lev(TOGGLE_SWITCH);
            uint8_t FOOT_SWITCH_val = bcm2835_gpio_lev(FOOT_SWITCH);
            bcm2835_gpio_write(LED,!FOOT_SWITCH_val); // Light the effect when the footswitch is activated

            if (TOGGLE_SWITCH_val == 0)
            {
                if (PUSH1_val == PRESSED)
                {          
                    is_PUSH1_pressed = TRUE;
                }

                if (PUSH1_val == !PRESSED && is_PUSH1_pressed)
                { 	
                    is_PUSH1_pressed = FALSE;
                    if (recording == TRUE)
                    {
                        recording = FALSE;
                        printf("Stopped recording.\n");
                        ending_sample = current_sample;
                        if (current_sample >= DELAY_MAX - 1) starting_sample = 0;
                        else
                        {
                            if (is_buffer_maxed_out) starting_sample = ending_sample + 1;
                            else starting_sample = 0;
                        }
                        is_buffer_maxed_out = FALSE;
                        current_sample = starting_sample;
                    }
                    else 
                    {
                        recording = TRUE;
                        printf("Recording...\n");
                        current_sample = 0;
                        is_buffer_maxed_out = FALSE;
                    }
                }
                if (PUSH2_val == PRESSED)
                {   
                    bcm2835_delay(100); // 100ms delay for buttons debouncing.
                    playback_mode++;
                    if (playback_mode > 2) playback_mode = 0;

                    if (playback_mode==0) printf("Normal playback (%d)\n", playback_mode);
                    else if (playback_mode==1)
                    {
                        printf("Random granular playback (%d)\n", playback_mode);
                        grain_size = rand() % (max_grain_size - min_grain_size + 1) + min_grain_size;
                        grain_counter = 0;
                        is_reversed = rand() % 2;
                        random_start = (rand() % 250) * 1000;
                        current_sample = random_start;
                    }
                    else printf("Reversed playback (%d)\n", playback_mode);
                }
            }
        


            else
            {
                if (PUSH1_val == PRESSED)
                {
                    // Adjust delay
                }
                if (PUSH2_val == PRESSED) 
                { 	
                    //bcm2835_delay(100); // 100ms delay for buttons debouncing. 
                    // Time stretching
                }
            }
        }


        //**** MODE 1: SAMPLER ***///

        // LOOP ACTIVATE - DEACTIVATE
        if (recording == TRUE)
        {   
            // Start recording
            LED_blinking = TRUE;
            LED_timer--;
            Delay_Buffer[current_sample] = input_signal;
            current_sample++;
            output_signal = input_signal;
            if (current_sample >= DELAY_MAX) 
            {
                is_buffer_maxed_out = TRUE;
                current_sample = 0;
            }

            // Led in blinking mode while recording
            if (LED_blinking && LED_timer < 0)
            {   
		        LED_value = !LED_value;
                bcm2835_gpio_write(LED, LED_value);
                LED_timer = SAMPLE_RATE / 5; // 0.2 seconds
            }
        }
        else 
        {   
            // PLAYBACK MODES (normal, granular, reversed)
            LED_blinking == FALSE;
            if (playback_mode == 0)
            {   
                // Normal playback
                output_signal = (Delay_Buffer[current_sample] + input_signal) >> 1;
                current_sample++;
                if (current_sample == ending_sample) {
                current_sample = starting_sample;
                }
                else 
                {
                    if (current_sample >= DELAY_MAX) current_sample = 0;
                }
            }
            else if (playback_mode == 1)
            {   
                // Randomly chosen and reversed grains
                /*grain_counter++;
                if (grain_counter >= grain_size) 
                {
                    grain_size = rand() % (max_grain_size - min_grain_size + 1) + min_grain_size;
                    grain_counter = 0;
                    is_reversed = rand() % 2;
                    random_start = (rand() % 250) * 1000;
                    current_sample = random_start;
                }

                if (is_reversed)
                {
                    output_signal = (Delay_Buffer[current_sample] + input_signal)>>1;
                    current_sample--;
                    if (current_sample < 0) current_sample = record_length-1;
                }
                else
                {
                    output_signal = (Delay_Buffer[current_sample] + input_signal)>>1;
                    current_sample++;
                    if (current_sample > record_length) current_sample = 0;
                }*/
            }
            else
            {
                // Reverse all signal
                output_signal = (Delay_Buffer[current_sample] + input_signal) >> 1;
                current_sample--;
                if (current_sample == starting_sample) current_sample = ending_sample;
                else
                {
                    if (current_sample <= 0) current_sample = DELAY_MAX - 1;
                }
            }
        }


        //**** MODE 2: EFFECTS ***///

        // ADJUST DELAY

        // TIME STRETCHING

        

        // Add a delay of 20 microseconds (sampling frequency = 50kHz)
        bcm2835_delayMicroseconds(20);
        //usleep(1);

        // Generate output PWM signal 6 bits
        bcm2835_pwm_set_data(1,output_signal & 0x3F);
        bcm2835_pwm_set_data(0,output_signal >> 6);
    }
 
	// Close all and exit
	bcm2835_spi_end();
    bcm2835_close();
    return 0;
}