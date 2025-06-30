#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico.h"
#include "pico/stdio/driver.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "hardware/pwm.h"
#include "hardware/uart.h"

#include "util/util.h"
#include "test/pico_poc_test.h"

#include "util/iso7816_t0/class_tables.h"
#include "util/iso7816_t0/class_tables.c"

#define COMMAND_BYTE_SET_ATR 0x20
#define MAX_ATR_SIZE 34
#define APDU_BUFFER_LEN 1024

#define WXT_BYTE 0x60


typedef struct {
	answer_to_reset atr;
	alarm_pool_t *alarm_pool;
	uint8_t uart_mode;
	int conf_clk;
	uint8_t loglevel;
} relay_config_entry_t;

typedef struct {
	answer_to_reset atr;
} update_atr_queue_entry_t;

typedef struct {
	uint8_t uartmode;
	int conf_clk;
} update_uartmode_queue_entry_t;

typedef struct {
	uint8_t loglevel;
} update_loglevel_queue_entry_t;

queue_t relay_config_queue;
queue_t update_atr_queue;
queue_t update_uartmode_queue;
queue_t update_loglevel_queue;

enum State {
	NEED_ATR = 0,
	RDY_TO_RELAY
};

static enum State current_state = NEED_ATR;
static sig_atomic_t mode = UART_MODE;
sig_atomic_t current_loglevel = LOG_LEVEL_DEBUG;
sig_atomic_t fixed_clk = 4764000;

/**
 * create an alarm pool, add pool to atr-alarm-queue
 * 
 * the alarm pool is created for the core where the 
 * function is called
 * 
 * @return void
 */
void create_alarm_pool()
{
	relay_config_entry_t entry;
	entry.alarm_pool = alarm_pool_create_with_unused_hardware_alarm(10);
	queue_add_blocking(&relay_config_queue, &entry);
	exit(0);
}

/**
 * creates an alarm pool on core1
 * 
 * @returns pointer to the alarm pool
 */
alarm_pool_t *get_alarm_pool_on_core1()
{
	relay_config_entry_t entry;
	multicore_launch_core1(create_alarm_pool);
	queue_remove_blocking(&relay_config_queue, &entry);
	multicore_reset_core1();
	return entry.alarm_pool;
}

int read_usb_uart_blocking(uint8_t * data, uint32_t len)
{
	int pos = 0;
	while (pos < len) {
		int rc = stdio_usb.in_chars(data + pos, len - pos);
		if (rc != PICO_ERROR_NO_DATA)
			pos += rc;
	}
	return pos;
}

uint32_t read_usb_uart_apdu(uint8_t * data)
{
	uint8_t opcode;
	uint32_t len;
	int rc = read_usb_uart_blocking(&opcode, 1);
	rc = read_usb_uart_blocking(&len, 4);
	rc = read_usb_uart_blocking(data, len);
	write_usb_debug(LOG_LEVEL_DEBUG, "getting response, opcode %x, len %x, read %d/%lu", opcode, len, rc, len);
	//TODO check if opcode == 0

	if (opcode == OP_SENDATR) {
		write_usb_debug(LOG_LEVEL_DEBUG, "new atr sent!");
		answer_to_reset atr;
		uint8_t *atr_buf = malloc(MAX_ATR_SIZE * sizeof(uint8_t));
		atr.len = len;
		atr.payload = atr_buf;
		for (int i = 0; i < atr.len; i++) {
			atr.payload[i] = data[i];
		}
		parse_ATR(&atr);
		update_atr_queue_entry_t entry = { atr };
		if (!queue_try_add(&update_atr_queue, &entry)) {
			write_usb_debug(LOG_LEVEL_DEBUG, "could not add atr to queue!");
		}
		return read_usb_uart_apdu(data);
	} else if (opcode == OP_REQUEST_STATE) {
		uint8_t cs = current_state;
		write_usb_uart(OP_REQUEST_STATE, &cs, 1);
		return read_usb_uart_apdu(data);
	} else if (opcode == OP_SET_UARTMODE) {
		uint8_t m = data[0];
		int clk = (data[1]<< 24) | (data[2] << 16) | (data[3] << 8) | data[4];
		update_uartmode_queue_entry_t entry = { m, clk };
		if (!queue_try_add(&update_uartmode_queue, &entry)) {
			write_usb_debug(LOG_LEVEL_INFO, "could not add uartmode to queue!");
		} else {
			if (m == 1) {
				clk = clk > 0 ? clk : fixed_clk;
				write_usb_debug(LOG_LEVEL_INFO, "change uart mode from %d to %d and clock from %d to %d", mode, m, fixed_clk, clk);
				mode = m;
				fixed_clk = clk;
			} else {
				write_usb_debug(LOG_LEVEL_INFO, "change uart mode from %d to %d", mode, m);
				mode = m;
			}
		}
		return read_usb_uart_apdu(data);
	} else if (opcode == OP_SET_LOGLEVEL) {
		uint8_t ll = data[0];
		update_uartmode_queue_entry_t entry = { ll };
		if (!queue_try_add(&update_loglevel_queue, &entry)) {
			write_usb_debug(LOG_LEVEL_INFO, "could not add uartmode to queue!");
		} else {
			write_usb_debug(LOG_LEVEL_INFO, "change loglevel from %d to %d", current_loglevel, ll);
		}
		current_loglevel = ll;
		return read_usb_uart_apdu(data);
	}

	return len;
}

uint32_t read_usb_atr(answer_to_reset * atr)
{
	uint8_t opcode;
	uint32_t len;
	int rc = read_usb_uart_blocking(&opcode, 1);
	rc = read_usb_uart_blocking(&len, 4);
	rc = read_usb_uart_blocking(atr->payload, len);
	write_usb_debug(LOG_LEVEL_INFO, "getting response, opcode %x, len %x, read %d/%lu", opcode, len, rc, len);
	//TODO check if opcode == 0

	atr->len = len;
	if (opcode == OP_SENDATR) {
		print_hex(LOG_LEVEL_INFO, "atr recv", atr->payload, atr->len);
		parse_ATR(atr);
		return 0;
	} else if (opcode == OP_REQUEST_STATE) {
		uint8_t cs = current_state;
		write_usb_uart(OP_REQUEST_STATE, &cs, 1);
	} else if (opcode == OP_SET_UARTMODE) {
		mode = atr->payload[0];		
		return read_usb_atr(atr);
	} else if (opcode == OP_SET_LOGLEVEL) {
		current_loglevel = atr->payload[0];
		return read_usb_atr(atr);
	}

	return -1;
}


void prot_waiting()
{
	uint8_t *buf = malloc(APDU_BUFFER_LEN * sizeof(uint8_t));
	write_usb_debug(LOG_LEVEL_TRACE, "Wait for Config");
	while(true){		
		read_usb_uart_apdu(buf);
	}
	
}
/**
 * Protocol T=0
 * 
 * @return void
 */
void prot_t0()
{
	bool do_pps = true;
	write_usb_debug(LOG_LEVEL_TRACE, "remove atr alarm from queue");
	relay_config_entry_t entry;
	queue_remove_blocking(&relay_config_queue, &entry);
	mode = entry.uart_mode;
	fixed_clk = entry.conf_clk;
	current_loglevel = entry.loglevel;


	uint32_t modem_clk = 0;
	if (mode == UART_MODE_SYNCHRONOUS) {
		modem_clk = get_sc_reader_clk();
		write_usb_debug(LOG_LEVEL_INFO, "measured clock %.3f MHz", modem_clk / 1000000.0);
	} else {
		modem_clk = fixed_clk;
		write_usb_debug(LOG_LEVEL_INFO, "current clk is %.3f MHz)!",
					modem_clk / 1000000.0);
	}
	//TODO else measure clock from corresponding pin?

	uart_set_baudrate(UART_ID,calculate_baudrate(372, 1, CLK_DEFAULT));

	// send ATR
	write_usb_debug(LOG_LEVEL_DEBUG, "Send ATR");
	sc_write(UART_ID, entry.atr.payload, entry.atr.len);

	// pps
	if (do_pps) {
		char *pps = malloc(5 * sizeof(char));
		write_usb_debug(LOG_LEVEL_DEBUG, "Read PPS");
		sc_read(UART_ID, pps, PPS_LEN);
		if (pps[0] != PPS_BYTE) {
			write_usb_debug
			    (LOG_LEVEL_INFO, "ERROR when receiving pps");
		} else {
			// echo pps
			sc_write(UART_ID, pps, PPS_LEN);
			// pps done -> calculate connection params
			uint8_t fidi = pps[2];
			int fi = convert_f(fidi);
			uint8_t di = convert_d(fidi);
			uart_tx_wait_blocking(UART_ID);
			uint32_t br = mode == 0 ? calculate_baudrate(fi, di, CLK_DEFAULT) : calculate_baudrate(fi, di, modem_clk);
			uart_set_baudrate(UART_ID,br);
			write_usb_debug(LOG_LEVEL_INFO, "set baudrate = %d", br);
		}
		free(pps);
	}

	// regular commands
	uint8_t *buf = malloc(APDU_BUFFER_LEN * sizeof(uint8_t));
	uint8_t response_cache[APDU_BUFFER_LEN];
	uint8_t len_response_cache = 0;
	while (true) {
		uint32_t apdu_len = 0;
		int le = SW_LEN;	// per default two SW bytes as expected response
		int lc = 0;
		write_usb_debug(LOG_LEVEL_TRACE, "Read Command");
		apdu_len = sc_read(UART_ID, buf, HEADER_LEN);
		uint64_t start = time_us_64();
		uint8_t apdu_case =
		    osim_determine_apdu_case(&osim_uicc_sim_cic_profile, buf);
		uint8_t p3 = buf[4];
		uint8_t proc_byte = buf[1];
		write_usb_debug(LOG_LEVEL_DEBUG, "apdu_case %d\n",
					apdu_case);

		if (apdu_case == 1) {	//P3 == 0 -> No Lc/Le
			// le = 2 bytes
		} else if (apdu_case == 2) {
			le += p3;
			if (p3 == 0)
				le += 256;
		} else if (apdu_case == 3 ||	//# P3 = Lc
			   apdu_case == 4) {	// P3 = Lc, Le encoded in SW
			lc = p3;
			if (lc > 0) {
				sc_write(UART_ID, &proc_byte, 1);	// send procedure byte
				apdu_len += sc_read(UART_ID, buf + HEADER_LEN, lc);
				//if apdu_case == 4:  case 4 in sim is not allowed --> request seperately
			}
		} else {
			write_usb_debug(LOG_LEVEL_INFO, "cannot determine case for apdu");
		}

		if (len_response_cache > 0 && buf[1] == 0xC0) {
			sc_write(UART_ID, &proc_byte, 1);
			sc_write(UART_ID, response_cache, len_response_cache);
			continue;
		}

		len_response_cache = 0;
		uint64_t step1 = time_us_64();
		write_usb_debug(LOG_LEVEL_DEBUG, "forward apdu[%lu] to usb",apdu_len);
		print_hex(LOG_LEVEL_TRACE, "capdu", buf, apdu_len);
		write_usb_uart_apdu(buf, apdu_len);

		uint32_t response_len = -1;
		while (response_len == -1) {
			response_len = read_usb_uart_apdu(buf);
		}
		uint64_t step2 = time_us_64();
		write_usb_debug(LOG_LEVEL_DEBUG, "received answer[%lu] from usb",response_len);
		print_hex(LOG_LEVEL_TRACE, "rapdu", buf, response_len);

		if (response_len == SW_LEN)
			sc_write(UART_ID, buf, response_len);
		else if (response_len == le ){ 
			sc_write(UART_ID, &proc_byte, 1);
			sc_write(UART_ID, buf, response_len);
		} else {
			len_response_cache = response_len;
			memcpy(response_cache, buf, response_len);

			if (response_len > le) {
				buf[0] = 0x61;
			} else {
				buf[0] = 0x6C;
			}
			buf[1] = response_len - SW_LEN;
			sc_write(UART_ID, buf, SW_LEN);
		}
		uint64_t end = time_us_64();
		write_usb_measurement("%lld, %lld, %lld",
				      end - start, step1 - start,
				      step2 - start);
	}
}

/**
 * Protocol T=1
 * 
 * 	1. get ATR and alarm pool from queue
 * 	2. listen for command
 *  3. get time since boot in microseconds (start)
 * 	4. classify command tpdu
 * 		4.1 if S Block: handle immediately, then goto 2
 *      4.2 if R Block: handle immediately, then goto 2
 *      4.3 if timeout: increase counter, 
 * 			if consecutive timeouts big enough, terminate otherwise goto 2
 *  5. set waiting time extension alarm
 *  6. write command to usb
 *  7. listen for response from usb
 * 		6.1 when alarm is invoked:
 * 				send waiting time extension
 * 				reset alarm s.t. it may be invoked again
 * 	8. when response from usb is received: cancel alarm
 *  9. pass response to reader
 *  10. get time since boot in microseconds (end)
 *  11. print(end - start) measurement 
 * 
 * @return void
 */
void prot_t1()
{
	// set baudrate to default s.t. it confirms to ATR 
	uart_set_baudrate(UART_ID, BAUD_RATE);
	uint8_t *buf = malloc(APDU_BUFFER_LEN * sizeof(uint8_t));
	// get atr and alarm_pool
	write_usb_debug(LOG_LEVEL_TRACE, "remove atr alarm from queue");
	relay_config_entry_t entry;
	queue_remove_blocking(&relay_config_queue, &entry);
	mode = entry.uart_mode;
	current_loglevel = entry.loglevel;

	uint8_t accepted_size = 32;	// default information field size
	t1_tpdu command_tpdu;
	t1_tpdu response_tpdu;
	write_usb_debug(LOG_LEVEL_INFO, "Send ATR %d %d", mode, current_loglevel);
	sc_write(UART_ID, entry.atr.payload, entry.atr.len);

	if (mode == UART_MODE_SYNCHRONOUS) {
		fixed_clk = get_sc_reader_clk();
		write_usb_debug(LOG_LEVEL_INFO, "measured clock %.3f MHz", fixed_clk / 1000000.0);
	} else {
		fixed_clk = entry.conf_clk;
		write_usb_debug(LOG_LEVEL_INFO, "card clock %.3f MHz", fixed_clk / 1000000.0);
	}


	if (entry.atr.F != 372 || entry.atr.D != 1){
		// do pps
		int offset = 1;
		uint8_t T0 = entry.atr.payload[offset] >> 4; // get upper nibble
		if ((T0 >> 3) % 2 == 1) {
			offset += 1 + (T0 % 2) + ((T0 >> 1) % 2) + ((T0 >> 2) % 2);
			uint8_t TA2 = entry.atr.payload[offset];
			// PPS in specific mode -> set baudrate early
			if ((TA2 >> 4) % 2 == 1) {
				uint br = uart_set_baudrate(UART_ID,
				    calculate_baudrate(entry.atr.F, entry.atr.D,
						       fixed_clk));
				write_usb_debug(LOG_LEVEL_INFO, "set early baudrate = %d", br);
			}
		}
	}
	
	seq = 1;
	while (true) {
		write_usb_debug(LOG_LEVEL_TRACE, "Read Command");
		int posr = t1_read(UART_ID, &command_tpdu);
		uint64_t start = time_us_64();
		if (posr == -3) {
			write_usb_debug(LOG_LEVEL_INFO, "PPS completed");
			continue;
		}
		if (posr == -2) {
			write_usb_debug(LOG_LEVEL_DEBUG, "reading timeout");
			continue;
		}
		if (posr == -1) {
			write_usb_debug(LOG_LEVEL_INFO, "checksum mismatch");
			t1_tpdu r_tpdu;
			r_tpdu.nad = 0x00;
			r_tpdu.len = 0x00;
			r_tpdu.pcb = seq % 2 == 0 ? 0x81 : 0x91;
			r_tpdu.check = r_tpdu.pcb;
			seq += t1_write(UART_ID, r_tpdu);
			continue;
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(command_tpdu);
		if (block_kind == T1_S_BLOCK) {
			int s_case =
			    t1_handle_s_block(UART_ID, &command_tpdu, &seq,
					      &accepted_size);
			write_usb_debug(LOG_LEVEL_INFO, "s_case: %d", s_case);
			continue;
		}
		if (block_kind == T1_R_BLOCK) {	// R Block      
			print_tpdu(LOG_LEVEL_DEBUG, "R BLOCK", command_tpdu);      
			int r_case = t1_classify_r_block(&command_tpdu);
			if (r_case == 1 || r_case == -1) {
				t1_adjust_sequence_bit(&seq, &response_tpdu);
				seq += t1_write(UART_ID, response_tpdu);
				write_usb_debug(LOG_LEVEL_TRACE, "sequence_counter: %d", seq);
			}
			continue;
		}
		// I Block
		// setup waitin gitme extension timer
		long bwt_interval = (entry.atr.BWT_us * 3) / 4;
		write_usb_debug(LOG_LEVEL_TRACE, "add alarm for bwt");
		alarm_id_t bwt_alarm =
		    alarm_pool_add_alarm_in_us(entry.alarm_pool, bwt_interval,
					       t1_timer_block_waiting_extension,
					       &bwt_interval, true);
		uint64_t step1 = time_us_64();
		uint8_t *apdu = command_tpdu.apdu;
		write_usb_debug(LOG_LEVEL_DEBUG, "forward apdu[%lu] to usb",command_tpdu.len);
		print_tpdu(LOG_LEVEL_TRACE, "capdu", command_tpdu);
		write_usb_uart_apdu(apdu, command_tpdu.len);

		// read from usb until a response is received
		int pos;
		do {
			pos = read_usb_uart_apdu(buf);
		} while (pos == -1);
		uint64_t step2 = time_us_64();
		write_usb_debug(LOG_LEVEL_DEBUG, "received answer[%lu] from usb", pos);
		print_hex(LOG_LEVEL_TRACE, "rapdu", buf, pos);

		// relayed response is here, cancel waiting time extension 
		write_usb_debug(LOG_LEVEL_TRACE, "cancel bwt alarm");
		alarm_pool_cancel_alarm(entry.alarm_pool, bwt_alarm);
		t1_adjust_sequence_bit(&seq, &command_tpdu);
		response_tpdu.nad = command_tpdu.nad;
		response_tpdu.pcb = command_tpdu.pcb;
		seq +=
		    t1_write_complete_buffer(UART_ID, accepted_size,
					     &response_tpdu, buf, pos);
		uint64_t end = time_us_64();

		// print measuremnt from command received to response written
		write_usb_measurement("%lld, %lld, %lld",
				      end - start, step1 - start,
				      step2 - start);
	}
}

int main()
{
	stdio_init_all();

	// Configure clock input pin 20
	clock_configure_gpin(clk_peri, PIN_SIM_CLK, CLK_DEFAULT, CLK_DEFAULT);
	// Re init uart now that clk_peri has changed
	stdio_init_all();
	// Set up our UART with the required speed.
	uart_init(UART_ID, BAUD_RATE);

	// Set the TX and RX pins by using the function select on the GPIO
	// Set datasheet for more information on function select
	gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
	gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
	uart_set_format(UART_ID, 8, 2, UART_PARITY_EVEN);

	queue_init(&relay_config_queue, sizeof(relay_config_entry_t), 2);
	queue_init(&update_atr_queue, sizeof(update_atr_queue_entry_t), 2);
	queue_init(&update_uartmode_queue, sizeof(update_uartmode_queue_entry_t), 2);
	queue_init(&update_loglevel_queue, sizeof(update_loglevel_queue_entry_t), 2);
	alarm_pool_t *alarm_pool = get_alarm_pool_on_core1();

	answer_to_reset ATR;
	ATR.payload = malloc(MAX_ATR_SIZE * sizeof(uint8_t));
	int rc = -1;
	while (rc == -1) {
		write_usb_debug(LOG_LEVEL_DEBUG, "Requesting first ATR");
		uint8_t opcode = OP_SENDATR;
		uint8_t len = 0;
		stdio_usb.out_chars(&opcode, 1);
		stdio_usb.out_chars(&len, 4);
		rc = read_usb_atr(&ATR);
	}
	current_state = RDY_TO_RELAY;

	gpio_init(GPIO_RESET_PIN);
	gpio_set_dir(GPIO_RESET_PIN, false);
	sleep_ms(100);

	// start core1, wait for config
	{
		relay_config_entry_t entry = { ATR, alarm_pool, mode, fixed_clk, current_loglevel};
		queue_add_blocking(&relay_config_queue, &entry);
		multicore_launch_core1(prot_waiting);
	}

	while (true) {
		// line goes up -> initiate protocol
		if (gpio_get(GPIO_RESET_PIN)) {
			write_usb_debug
			    (LOG_LEVEL_INFO, "trigger detected: reset core1, %lld", time_us_64());
			multicore_reset_core1();
			while (queue_try_remove(&relay_config_queue, NULL)) {
				write_usb_debug
				    (LOG_LEVEL_DEBUG, "remove element from relay_config_queue");
			}
			assert(queue_is_empty(&relay_config_queue));
			relay_config_entry_t entry = { ATR, alarm_pool, mode, fixed_clk, current_loglevel};
			// send atr and alarm queue to core1
			queue_add_blocking(&relay_config_queue, &entry);

			if (ATR.protocol == 1) {
				// clear alarm queue

				write_usb_debug(LOG_LEVEL_INFO, "launch t1, enabled %d",
							uart_is_enabled
							(UART_ID));
				multicore_launch_core1(prot_t1);

			} else if (ATR.protocol == 0) {
				write_usb_debug(LOG_LEVEL_INFO, "launch t0");
				multicore_launch_core1(prot_t0);
			} else {
				write_usb_debug(LOG_LEVEL_DEBUG, "ATR parsing failed");
			}
			// wait until line goes down
			while (gpio_get(GPIO_RESET_PIN)) {
				sleep_us(100);
			};
			sleep_us(100);
		} 
		// check whether a new ATR, UART mode, or loglevel was read from usb
		update_atr_queue_entry_t update_atr_entry;
		update_uartmode_queue_entry_t update_uartmode_entry;
		update_loglevel_queue_entry_t update_loglevel_entry;
		if (queue_try_remove
			(&update_atr_queue, &update_atr_entry)) {
			free(ATR.payload);
			ATR = update_atr_entry.atr;
			print_hex(LOG_LEVEL_DEBUG, "new ATR", ATR.payload, ATR.len);
		}
		if (queue_try_remove
			(&update_uartmode_queue, &update_uartmode_entry)) {
			mode = update_uartmode_entry.uartmode;
			if (mode == 1){
				fixed_clk = update_uartmode_entry.conf_clk > 0 ? update_uartmode_entry.conf_clk : fixed_clk;
				write_usb_debug(LOG_LEVEL_DEBUG, "new UART mode %d with clock %d", mode, fixed_clk);
			} else {
				write_usb_debug(LOG_LEVEL_DEBUG, "new UART mode %d", mode);
			}
		}
		if (queue_try_remove
			(&update_loglevel_queue, &update_loglevel_entry)) {
			current_loglevel = update_loglevel_entry.loglevel;
			write_usb_debug(LOG_LEVEL_DEBUG, "new loglevel %d", current_loglevel);
		}	
	}
	// never reached
	return 0;
}
