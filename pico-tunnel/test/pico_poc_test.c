#include <stdio.h>
#include <stdlib.h>

#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "hardware/structs/uart.h"
#include "hardware/regs/dreq.h"

#include "../util/util.h"

/*
 *  read apdu into buffer
 *  structure:
 *      command byte | size | data...
 *  return length of buf
 */
int usb_read(uint8_t * buf);

void test_t1_on_demand_bwt()
{
	uint8_t *buf = malloc(1024 * sizeof(uint8_t));
	uint8_t accepted_size = 32;
	t1_tpdu res;
	t1_tpdu test_tpdu;
	seq = 1;
	printf("This is the BTW on demand test\n");
	alarm_pool_t *alarm_pool =
	    alarm_pool_create_with_unused_hardware_alarm(10);

	while (true) {
		int posr = t1_read(UART_ID, &res);
		uint64_t start = time_us_64();

		if (posr == -1) {
			printf("checksum mismatch\n");
			t1_tpdu r_tpdu;
			r_tpdu.nad = 0x00;
			r_tpdu.len = 0x00;
			r_tpdu.pcb = seq % 2 == 0 ? 0x81 : 0x91;
			seq += t1_write(UART_ID, r_tpdu);
			continue;
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(res);
		if (block_kind == 2) {	// S Block
			printf("S Block detected\n");
			print_tpdu("S BLOCK", res);
			int s_case = t1_handle_s_block(UART_ID, &res, &seq,
						       &accepted_size);
			printf("s_case: %d\n", s_case);
			continue;
		}
		if (block_kind == 1) {	// R Block
			int r_case = t1_classify_r_block(&res);
			if (r_case == 1 || r_case == -1) {
				t1_adjust_sequence_bit(&seq, &test_tpdu);
				seq += t1_write(UART_ID, test_tpdu);
				printf("sequence_counter: %d\n", seq);
			}
			continue;
		}
		long bwt_interval = (1600000 * 3);
		printf("interval %d %d %d\n", 1600000, bwt_interval,
		       bwt_interval / 4);
		alarm_id_t bwt_alarm =
		    alarm_pool_add_alarm_in_us(alarm_pool, bwt_interval,
					       t1_timer_block_waiting_extension,
					       &bwt_interval, true);

		write_apdu_usb(res.apdu, res.len);
		int pos = usb_read(buf);
		printf("GOT RESPONSE\n");
		alarm_pool_cancel_alarm(alarm_pool, bwt_alarm);
		t1_adjust_sequence_bit(&seq, &res);

		if (pos >= 254) {
			print_tpdu("VERY LONG APDU", test_tpdu);
			test_tpdu.nad = res.nad;
			test_tpdu.pcb = res.pcb;
			seq +=
			    t1_write_complete_buffer(UART_ID, accepted_size,
						     &test_tpdu, buf, pos);
		} else {
			printf("single\n");
			t1_read_into_tpdu(res.nad, res.pcb, pos, buf,
					  &test_tpdu);
			seq += t1_write(UART_ID, test_tpdu);
		}
		printf("sequence_counter: %d\n", seq);
		uint64_t end = time_us_64();

		printf("diff = %lld\n", end - start);
	}

	printf("done\n");
	while (1) {
		sleep_ms(100);
	}
}

void test_t1_block_waiting_time()
{
	uint8_t *buf = malloc(1024 * sizeof(uint8_t));
	uint8_t accepted_size = 32;
	t1_tpdu res;
	t1_tpdu test_tpdu;
	int sequence_counter = 1;

	alarm_pool_t *alarm_pool =
	    alarm_pool_create_with_unused_hardware_alarm(10);

	while (true) {

		int posr = t1_read(UART_ID, &res);
		uint64_t start = time_us_64();
		if (posr == -1) {
			printf("checksum mismatch\n");
			t1_tpdu r_tpdu;
			r_tpdu.nad = 0x00;
			r_tpdu.len = 0x00;
			r_tpdu.pcb = sequence_counter % 2 == 0 ? 0x81 : 0x91;
			sequence_counter += t1_write(UART_ID, r_tpdu);
			continue;
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(res);
		if (block_kind == 2) {	// S Block
			printf("S Block detected\n");
			print_tpdu("S BLOCK", res);
			int s_case =
			    t1_handle_s_block(UART_ID, &res, &sequence_counter,
					      &accepted_size);
			printf("s_case: %d\n", s_case);
			continue;
		}
		if (block_kind == 1) {	// R Block            
			int r_case = t1_classify_r_block(&res);
			if (r_case == 1 || r_case == -1) {
				t1_adjust_sequence_bit(&sequence_counter,
						       &test_tpdu);
				sequence_counter +=
				    t1_write(UART_ID, test_tpdu);
				printf("sequence_counter: %d\n",
				       sequence_counter);
			}
			continue;
		}

		write_apdu_usb(res.apdu, res.len);

		// test this part
		{
			uint64_t before = time_us_64();
			printf("SEND WTT\n");
			int s = t1_block_waiting_extension(&sequence_counter);
			uint64_t after = time_us_64();
			printf("start - before: %lld\n", before - start);
			printf("start - after:  %lld\n", after - start);
			if (s == -1) {
				continue;
			}
		}
		sleep_ms(1000);
		{
			uint64_t before = time_us_64();
			printf("SEND WTT\n");
			int s = t1_block_waiting_extension(&sequence_counter);
			uint64_t after = time_us_64();
			printf("start - before: %lld\n", before - start);
			printf("start - after:  %lld\n", after - start);
			if (s == -1) {
				continue;
			}
		}

		t1_adjust_sequence_bit(&sequence_counter, &res);
		int pos = usb_read(buf);

		if (pos >= 254) {
			print_tpdu("VERY LONG APDU", test_tpdu);
			test_tpdu.nad = res.nad;
			test_tpdu.pcb = res.pcb;
			sequence_counter +=
			    t1_write_complete_buffer(UART_ID, accepted_size,
						     &test_tpdu, buf, pos);
		} else {
			printf("single\n");
			t1_read_into_tpdu(res.nad, res.pcb, pos, buf,
					  &test_tpdu);
			sequence_counter += t1_write(UART_ID, test_tpdu);
		}
		printf("sequence_counter: %d\n", sequence_counter);
		uint64_t end = time_us_64();

		printf("diff = %lld\n", end - start);
	}

	printf("done\n");
	while (1) {
		sleep_ms(100);
	}
}

void test_t1_resync_block()
{
	printf("Test Resync\n");
	uint8_t *buf = malloc(1024 * sizeof(uint8_t));
	uint8_t accepted_size = 32;
	t1_tpdu res;
	t1_tpdu test_tpdu;
	int sequence_counter = 1;

	bool resynced = false;

	while (true) {
		uint64_t start = time_us_64();
		int posr = t1_read(UART_ID, &res);
		if (posr == -1) {
			printf("checksum mismatch\n");
			t1_tpdu r_tpdu;
			r_tpdu.nad = 0x00;
			r_tpdu.len = 0x00;
			r_tpdu.pcb = sequence_counter % 2 == 0 ? 0x81 : 0x91;
			r_tpdu.check = r_tpdu.pcb;
			sequence_counter += t1_write(UART_ID, r_tpdu);
			continue;
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(res);
		if (block_kind == 2) {	// S Block
			printf("S Block detected\n");
			print_tpdu("S BLOCK", res);
			int s_case =
			    t1_handle_s_block(UART_ID, &res, &sequence_counter,
					      &accepted_size);
			printf("s_case: %d\n", s_case);
			if (s_case == 1) {
				resynced = true;
			}
			continue;
		}
		if (block_kind == 1) {	// R Block            
			int r_case = t1_classify_r_block(&res);
			if (r_case == 1 || r_case == -1) {
				printf("R Block detected\n");
				t1_adjust_sequence_bit(&sequence_counter,
						       &test_tpdu);
				sequence_counter +=
				    resynced ? t1_write(UART_ID,
							test_tpdu) :
				    t1_write_faulty(UART_ID, test_tpdu);
			}
			continue;
		}

		printf("before %d res.pcb: %02X\n", sequence_counter, res.pcb);
		t1_adjust_sequence_bit(&sequence_counter, &res);
		printf("after %d res.pcb: %02X\n", sequence_counter, res.pcb);

		write_apdu_usb(res.apdu, res.len);
		int pos = usb_read(buf);

		if (pos >= 254) {
			print_tpdu("VERY LONG APDU", test_tpdu);
			test_tpdu.nad = res.nad;
			test_tpdu.pcb = res.pcb;
			sequence_counter +=
			    t1_write_complete_buffer(UART_ID, accepted_size,
						     &test_tpdu, buf, pos);
		} else {
			printf("single\n");
			t1_read_into_tpdu(res.nad, res.pcb, pos, buf,
					  &test_tpdu);
			sequence_counter +=
			    resynced ? t1_write(UART_ID,
						test_tpdu) :
			    t1_write_faulty(UART_ID, test_tpdu);
		}
		printf("sequence_counter: %d\n", sequence_counter);
		uint64_t end = time_us_64();

		printf("diff = %lld\n", end - start);
	}

	printf("done\n");
	sleep_ms(100);
	exit(0);
}

void test_t1_receive_faulty_block()
{
	uint8_t *buf = malloc(1024 * sizeof(uint8_t));
	uint8_t accepted_size = 32;
	t1_tpdu res;
	t1_tpdu test_tpdu;
	int sequence_counter = 1;

	for (int i = 0; i < 1000; i++) {
		uint64_t start = time_us_64();
		int posr = t1_read_faulty(UART_ID, &res);
		if (posr == -1) {
			if (i % 3 == 2) {
				printf("checksum mismatch\n");
				t1_tpdu r_tpdu;
				r_tpdu.nad = 0x00;
				r_tpdu.len = 0x00;
				r_tpdu.pcb =
				    sequence_counter % 2 == 0 ? 0x81 : 0x91;
				r_tpdu.check = r_tpdu.pcb;
				sequence_counter += t1_write(UART_ID, r_tpdu);
				continue;
			}
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(res);
		if (block_kind == 2) {	// S Block
			printf("S Block detected\n");
			print_tpdu("S BLOCK", res);
			int s_case =
			    t1_handle_s_block(UART_ID, &res, &sequence_counter,
					      &accepted_size);
			printf("s_case: %d\n", s_case);
			continue;
		}
		if (block_kind == 1) {	// R Block
			int r_case = t1_classify_r_block(&res);
			if (r_case == 1 || r_case == -1) {
				printf("R Block detected\n");
				t1_adjust_sequence_bit(&sequence_counter,
						       &test_tpdu);
				sequence_counter +=
				    t1_write(UART_ID, test_tpdu);
			}
			continue;
		}

		printf("before %d res.pcb: %02X\n", sequence_counter, res.pcb);
		t1_adjust_sequence_bit(&sequence_counter, &res);
		printf("after %d res.pcb: %02X\n", sequence_counter, res.pcb);

		write_apdu_usb(res.apdu, res.len);
		int pos = usb_read(buf);

		if (pos >= 254) {
			print_tpdu("VERY LONG APDU", test_tpdu);
			test_tpdu.nad = res.nad;
			test_tpdu.pcb = res.pcb;
			sequence_counter +=
			    t1_write_complete_buffer(UART_ID, accepted_size,
						     &test_tpdu, buf, pos);
		} else {
			printf("single\n");
			t1_read_into_tpdu(res.nad, res.pcb, pos, buf,
					  &test_tpdu);
			sequence_counter += t1_write(UART_ID, test_tpdu);
		}
		printf("sequence_counter: %d\n", sequence_counter);
		uint64_t end = time_us_64();

		printf("diff = %lld\n", end - start);
	}

	printf("done\n");
	sleep_ms(100);
	exit(0);
}

void test_t1_sending_faulty_block()	// Parity Error
{
	uint8_t *buf = malloc(1024 * sizeof(uint8_t));
	uint8_t accepted_size = 32;
	t1_tpdu res;
	t1_tpdu test_tpdu;
	int sequence_counter = 1;

	while (true) {
		uint64_t start = time_us_64();
		int posr = t1_read(UART_ID, &res);
		if (posr == -1) {
			printf("checksum mismatch\n");
			t1_tpdu r_tpdu;
			r_tpdu.nad = 0x00;
			r_tpdu.len = 0x00;
			r_tpdu.pcb = sequence_counter % 2 == 0 ? 0x81 : 0x91;
			r_tpdu.check = r_tpdu.pcb;
			sequence_counter += t1_write(UART_ID, r_tpdu);
			continue;
		}
		// Check the kind of the tpdu received
		int block_kind = check_block_kind(res);
		if (block_kind == 2) {	// S Block
			printf("S Block detected\n");
			print_tpdu("S BLOCK", res);
			int s_case =
			    t1_handle_s_block(UART_ID, &res, &sequence_counter,
					      &accepted_size);
			printf("s_case: %d\n", s_case);
			continue;
		}
		if (block_kind == 1) {	// R Block            
			int r_case = t1_classify_r_block(&res);
			if (r_case == 1 || r_case == -1) {
				printf("R Block with parity error detected\n");
				t1_adjust_sequence_bit(&sequence_counter,
						       &test_tpdu);
				sequence_counter +=
				    t1_write(UART_ID, test_tpdu);
			}
			continue;
		}

		printf("before %d res.pcb: %02X\n", sequence_counter, res.pcb);
		t1_adjust_sequence_bit(&sequence_counter, &res);
		printf("after %d res.pcb: %02X\n", sequence_counter, res.pcb);

		write_apdu_usb(res.apdu, res.len);
		int pos = usb_read(buf);

		if (pos >= 254) {
			print_tpdu("VERY LONG APDU", test_tpdu);
			test_tpdu.nad = res.nad;
			test_tpdu.pcb = res.pcb;
			sequence_counter +=
			    t1_write_complete_buffer(UART_ID, accepted_size,
						     &test_tpdu, buf, pos);
		} else {
			printf("single\n");
			t1_read_into_tpdu(res.nad, res.pcb, pos, buf,
					  &test_tpdu);
			sequence_counter += t1_write_faulty(UART_ID, test_tpdu);
		}
		printf("sequence_counter: %d\n", sequence_counter);
		uint64_t end = time_us_64();

		printf("diff = %lld\n", end - start);
	}

	printf("done\n");
	sleep_ms(100);
	exit(0);
}

int usb_read(uint8_t * buf)
{
	size_t pos = 0;
	uint8_t cb = getchar();	// command byte
	uint8_t size_left = getchar();	// size
	uint8_t size_right = getchar();
	int total_size = size_left << 8 | size_right;

	for (int i = 0; i < total_size; i++) {
		buf[pos++] = getchar();
	}
	return pos;
}
