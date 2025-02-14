
// ****************************************************************************
//
//                                 Main code
//
// ****************************************************************************

#include <stdio.h>

#include "include.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "main.h"       // main code

void VideoInit()
{
	GenPal16Trans(trans,palwedongongoofedthebits);

	// setup videomode
	VgaCfgDef(&Cfg); // get default configuration
	Cfg.video = &DRV; // video timings
	Cfg.width = WIDTH; // screen width
	Cfg.height = HEIGHT; // screen height
	Cfg.dbly = DBLY;    //Double Line hight. 
	VgaCfg(&Cfg, &Vmode); // calculate videomode setup

    // initialize base layer 0
	//Docs say todo this
	ScreenClear(pScreen);
	//Add a strip which is a "video buffer element" its ewird
	sStrip* t = ScreenAddStrip(pScreen, HEIGHT);
	//This makes our actual 4bpp screen segment
	sSegm* g = ScreenAddSegm(t, WIDTH);
	ScreenSegmGraph4(g, Box,trans, WIDTHBYTE); 

    // initialize system clock
	set_sys_clock_pll(Vmode.vco*1000, Vmode.pd1, Vmode.pd2);

	// initialize videomode
	VgaInitReq(&Vmode);
}

void TEST_CAP_pio_init() {
    // do the setup things
    pio = pio1;
    pio_offset = pio_add_program(pio, &TEST_CAP_program);

    pio_sm = pio_claim_unused_sm(pio, true);
    pio_sm_config c = TEST_CAP_program_get_default_config(pio_offset);
    pio_sm_set_enabled(pio, pio_sm, false);

    sm_config_set_in_pins(&c, FIRST_RGBx); 
    pio_sm_set_consecutive_pindirs(pio, pio_sm, FIRST_INPUT, INPUT_COUNT, GPIO_IN);

    sm_config_set_clkdiv(&c, 1); 

    // From C SDK PDF:
    // sm_config_set_in_shift sets the shift direction to rightward, enables autopush, and sets the autopush threshold to 32.
    // The state machine keeps an eye on the total amount of data shifted into the ISR, and on the in which reaches or
    // breaches a total shift count of 32 (or whatever number you have configured), the ISR contents, along with the new data
    // from the in. goes straight to the RX FIFO. The ISR is cleared to zero in the same operation.
    sm_config_set_in_shift(&c, false, true, 32); // i've setup 8 bits to make this boundry clean, i think
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX); // PIO_FIFO_JOIN_NONE, _RX, _TX
    pio_sm_init(pio, pio_sm, pio_offset, &c);

    magic_pio_offset = pio_add_program(pio, &TIM_MAGIC_program);
    magic_pio_sm = pio_claim_unused_sm(pio, true);
    pio_sm_config c2 = TIM_MAGIC_program_get_default_config(magic_pio_offset);
    pio_sm_set_enabled(pio, magic_pio_sm, false);

    sm_config_set_clkdiv(&c2,1);
    pio_sm_init(pio, magic_pio_sm, magic_pio_offset, &c2);
    pio_sm_set_enabled(pio, magic_pio_sm, true);
}

void TEST_CAP_pio_arm(uint32_t *capture_buf, size_t capture_size_words) {
    pio_sm_set_enabled(pio, pio_sm, false);
    pio_sm_clear_fifos(pio, pio_sm);  
    pio_sm_restart(pio, pio_sm);

	rgb_dma_chan = 10;// dma_claim_unused_channel(false);
    dma_channel_config c = dma_channel_get_default_config(rgb_dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
	channel_config_set_bswap(&c,true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, pio_sm, false));

    dma_channel_configure(rgb_dma_chan, &c,
        capture_buf,            // Destination pointer
        &pio->rxf[pio_sm],      // Source pointer
        capture_size_words,     // Number of transfers
        true                    // Start immediately
    );

    //puts("[PIO Arm] Starting CAP Pio.");
    //pio_sm_restart(pio, pio_sm);
    pio_sm_exec(pio, pio_sm, pio_encode_jmp(TEST_CAP_offset_start));
    pio_sm_set_enabled(pio, pio_sm, true);
}

void print_hex_buf(const uint32_t *buf, uint word_count, uint line_count) {
    printf("---[START]---\n");
    for (int x=0; x < (word_count * line_count); x++) {   // 
        if (x % 10 == 0)
            printf("\n");
        uint32_t current_word = buf[x];
        printf("%08x", current_word);
    }
    printf("\n---[END]---\n");

}

void print_capture_buf(const uint32_t *buf, uint word_count, uint offset) {
   // printf("Captured: Hex, Binary, RGBx[4 Words, right to left]\n");
    for (int x=0; x < word_count; x++) {
       // printf("%d:%08x, b%b\n", x, buf[x], buf[x]); // lol, %b works
       uint32_t current_buf = buf[x+(word_count*offset)];
        printf("%d:%08x, ", x+(word_count*offset), current_buf); // lol, %b works

        // RGB4 RGB8 RGB1 RGB2
        // 1010 1000 0011 0001 0010 0000 1111 1101
        for (int j=0; j<8; j++) {
            uint offset = 4 * j;
            printf((current_buf & (0x1<<2+(offset))) ? "1" : "0");
            printf((current_buf & (0x1<<3+(offset))) ? "1" : "0");
            printf((current_buf & (0x1<<0+(offset))) ? "1" : "0");
            printf((current_buf & (0x1<<1+(offset))) ? "1" : "0");
            printf((j!=7) ? ", " : "");
        } 
        printf("\n");
    }
    printf("\n");
}

inline uint ctrl_status_led(uint state) {
    switch (state) {
        case LED_OFF:
            led_pin_state = LED_OFF;
            break;
        case LED_ON:
            led_pin_state = LED_ON;
            break;
        case LED_TOG:
            led_pin_state = !led_pin_state;
            break;
    }
    gpio_put(BLINKY_PIN, led_pin_state);
    return led_pin_state;
}


void init_blinky_pin() {
    gpio_init(BLINKY_PIN);
    gpio_set_dir(BLINKY_PIN, GPIO_OUT);
    ctrl_status_led(LED_ON);
    ctrl_status_led(LED_OFF);
}

void check_uart() {
    if (stdio_usb_connected()) {
        while(uart_is_readable(KBD_UART)) {
            char incoming_char = uart_getc(KBD_UART);
                putchar(incoming_char);
        }
    }
}

void fillBox(int color) {
    if (stdio_usb_connected())
       printf("\nDrawing Solid Color");

    for(int i = 0;i<((WIDTHBYTE)*HEIGHT);i++){
        //Box[i]= RandU8();
        Box[i] = color;
    }
}

int main() {
    init_blinky_pin();
    stdio_usb_init();

    // uart_init(KBD_UART, KBD_BAUD);
    // gpio_set_function(KBD_TX, GPIO_FUNC_UART);
    // gpio_set_function(KBD_RX, GPIO_FUNC_UART);


    // give me a chance to open the serial terminal
    // for (int waits=0; waits < 50; waits++) {
    //     ctrl_status_led(LED_TOG);
    //     busy_wait_us(100000);
    //     if (stdio_usb_connected())
    //         break;
    // }
    ctrl_status_led(LED_OFF);

    if (stdio_usb_connected())
        printf("\n\nVGA2040 Pixelizer 1.0\n");

	// vsync

    if (stdio_usb_connected())
        printf("\nInit VSYNC");
	gpio_init(VSYNC);
	gpio_set_dir(VSYNC, GPIO_OUT);
	// initialize videomode
	// run VGA core
    if (stdio_usb_connected())
        printf("\nConfiguring Core1 for VGA");
	multicore_launch_core1(VgaCore);

	// initialize videomode
    if (stdio_usb_connected())
        printf("\nInit VGA");
	VideoInit();
	
    fillBox(0x22);

	//while(true);

	gpio_init(WINDOW);
    gpio_set_dir(WINDOW,GPIO_IN);

    if (stdio_usb_connected())
        printf("\nInit PIO for Mega Bitstream");
    TEST_CAP_pio_init();
   // printf("done!");	



    if (stdio_usb_connected())
        printf("\nTime for MEGA Fun\n");

    
    uint64_t last_window = time_us_64();

	while(true) {
		// detect window high for 1ms
        uint32_t previous_window_us = 0;
        bool vblank = false;

     //   ctrl_status_led(LED_ON);
        uint64_t last_window = time_us_64();
        bool got_sync = true;
		while (!gpio_get(WINDOW)) {
            if (got_sync && (time_us_64() - last_window) >= 1000000) {
                fillBox(0x22);
                got_sync = false;
            }          
        } // wait for window to deassert 
  //      ctrl_status_led(LED_OFF);
        previous_window_us = time_us_32();

        // wait until we're in VBLANK
        while(time_us_32() - previous_window_us <= 2500) {
            if (gpio_get(WINDOW) == 0x0)
                previous_window_us = time_us_32();
        }

        if (stdio_usb_connected()) {
            // do we need to dump a buffer?
            uint8_t incoming_char = getchar_timeout_us(0);
            if ((incoming_char == '!'))
                print_hex_buf((uint32_t*)Box, WIDTHWORDS, LINE_COUNT);
            if ((incoming_char == '@'))
                print_capture_buf((uint32_t*)Box, WIDTHWORDS, 0);
            if ((incoming_char == '#'))
                print_capture_buf((uint32_t*)Box, WIDTHWORDS, (HEIGHT-1)); 
            if ((incoming_char == '?'))
                printf("\nHi\n");
        }

        // restart the state machines ... 
        TEST_CAP_pio_arm((uint32_t*)Box, ((WIDTHWORDS) * LINE_COUNT));
		dma_channel_wait_for_finish_blocking(10);
		// for (int x=1; x < sizeof(Box); x++) {
		// 	u8 temp = Box[x-1];
		// 	Box[x-1] = Box[x];
		// 	Box[x] = temp;
		// }
		// while(1);
	}
}
