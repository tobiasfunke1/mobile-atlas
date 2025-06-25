#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "pico/printf.h"
#include "pico/stdio/driver.h"
#include "hardware/clocks.h"

void write_usb_uart(uint8_t opcode, uint8_t * data, uint32_t len)
{
	stdio_usb.out_chars(&opcode, 1);
	stdio_usb.out_chars(&len, 4);
	if (len > 0) {
		stdio_usb.out_chars(data, len);
	}
}

void write_usb_uart_apdu(uint8_t * data, uint32_t len)
{
	write_usb_uart(OP_APDU, data, len);
}

static char message_buffer[1024];
int write_usb_debug(uint8_t msglevel, const char *format, ...)
{
	if (current_loglevel < msglevel) return 0;
	char *ll;
	switch (msglevel) {
	case LOG_LEVEL_INFO:
		ll = "INFO: ";
		break;
	case LOG_LEVEL_DEBUG:
		ll = "DEBUG:";
		break;
	case LOG_LEVEL_TRACE:
		ll = "TRACE:";
		break;
	}

	va_list va;
	va_start(va, format);
	const int ret_header = snprintf(message_buffer, sizeof(message_buffer), "%s %lu == ", ll, to_ms_since_boot(get_absolute_time()));
    const int ret_body =
	    vsnprintf(message_buffer + ret_header, sizeof(message_buffer) - ret_header, format, va);
	va_end(va);
	const int ret_total = ret_header + ret_body;
	write_usb_uart(OP_DEBUGMSG, message_buffer, ret_total);
	return ret_total;
}

int write_usb_measurement(const char *format, ...)
{
	va_list va;
	va_start(va, format);
	const int ret =
	    vsnprintf(message_buffer, sizeof(message_buffer), format, va);
	va_end(va);
	write_usb_uart(OP_MEASUREMENT, message_buffer, ret);
	return ret;
}

/**
 * Print buffer as a hex string
 * 
 * @param header start the printed line with header
 * @param buf buffer to be converted to hex string
 * @param len length of the buffer
 * @return void
 */
void print_hex(uint8_t msglevel, char *header, uint8_t * buf, size_t len)
{
	if (current_loglevel < msglevel) return;
	uint8_t *a = malloc((3 * len + 1) * sizeof(char));
	for (int i = 0; i < len; i++) {
		sprintf(a + i * 3, " %02X", buf[i]);
	}
	write_usb_debug(msglevel, "%s | %s", header, a);
	free(a);
}

/**
 * Print tpdu as a hex string
 * 
 * @param header start the printed line with header
 * @param tpdu tpdu to be converted to hex string
 * @param checksum checksum of the tpdu
 * @return void
 */
void print_tpdu(uint8_t msglevel, char *header, t1_tpdu tpdu)
{
	if (current_loglevel < msglevel) return;
	uint8_t *a = malloc(((3+tpdu.len+1) * 3) * sizeof(char));
	char checksum = 0x00;
	for (int i = 0; i < tpdu.len + 3; i++) {
		char c;
		switch (i) {
		case 0:
			c = (char)tpdu.nad;
			break;
		case 1:
			c = (char)tpdu.pcb;
			break;
		case 2:
			c = (char)tpdu.len;
			break;
		default:
			c = (char)tpdu.apdu[i - 3];
		}
		sprintf(a + i * 3, " %02X", c);
		checksum ^= c;
	}
	sprintf(a + (tpdu.len + 3) * 3, " %02X", checksum);	
	write_usb_debug(msglevel, "%s | %s", header, a);
	free(a);
}

/**
 * Discard incoming/outgoing chars from uart
 * 
 * @param uart uart to be drained
 * @return void
 */
void discard_uart_buffer(uart_inst_t * uart)
{
	while (uart_is_readable(uart)) {
		char c = uart_getc(uart);
		write_usb_debug(LOG_LEVEL_TRACE, "discard %02x\n", c);		
	}
	uart_tx_wait_blocking(uart);
}

/**
 * Write out buffer to uart
 * 
 * @param uart uart where the buffer is written to
 * @param buf buffer to be written
 * @param len length of the buffer
 * @return length of the written buffer
 */
int sc_write(uart_inst_t * uart, uint8_t * buf, size_t len)
{
	uint8_t *read_back = calloc(len, sizeof(char));
	size_t pos = 0;
	discard_uart_buffer(uart);

	while (pos < len) {
		uart_putc(uart, buf[pos]);
		read_back[pos] = uart_getc(uart);
		if (buf[pos] != read_back[pos]) {
			write_usb_debug
			    (LOG_LEVEL_DEBUG, "index[%d]: write: %02X read: %02X", pos, buf[pos],
			     read_back[pos]);
		}
		pos++;
	}

	if (len != pos && buf[pos] != read_back[pos]) {
		write_usb_debug(LOG_LEVEL_DEBUG, "index[%d]: write: %02X read: %02X",
					pos, buf[pos - 1], read_back[pos]);
	}
	free(read_back);
	return pos;
}

/**
 * Read from UART a given number of characters into a buffer
 * 
 * @param uart uart where chars are read from
 * @param buf buffer to read chars into
 * @param len max number of chars to read
 * @return number of chars read
 */
int sc_read(uart_inst_t * uart, uint8_t * buf, size_t len)
{
	size_t pos = 0;
	while (pos < len) {
		buf[pos++] = uart_getc(uart);
	}
	if (pos == len) {
		print_hex(LOG_LEVEL_TRACE, "read", buf, len);
	}
	return pos;
}

/**
 * Determine the kind of block of a tpdu
 * 
 * @param tpdu the tpdu to be examined
 * @return 0 if it is an I Block
 *         1 if it is an R Block
 *         2 if it is an S Block
 */
int check_block_kind(t1_tpdu tpdu)
{
	if (tpdu.pcb < 128) {
		return T1_I_BLOCK; // I block b8 == 0
	}
	if (tpdu.pcb >= 128 && tpdu.pcb < 128 + 64) {
		return T1_R_BLOCK; // R block b8,b7 == 1,0
	}
	return T1_S_BLOCK; // S block b8,b7 == 1,1
}

/**
 * Given that the tpdu is an S Block, take appropriate action 
 * depending on the type of S Block
 * 
 * @param uart uart the reponse is written to
 * @param tpdu tpdu which is responded to
 * @param sequence_counter tracks state over the protocol
 * @param accepted_size max size of the information field
 *  		the apdu is allowed to have
 * @return -1 whenn S Block indicates success
 * 			e.g. abort-, wte-, ifsd-success
 *         0 when the type of the S Block could not be determined
 * 		   1 when the S Block type was a resync request
 * 		   2 when the S Block type was a request for ifsd
 *  	   3 when the S Block tpye was a abort request
 */
int t1_handle_s_block(uart_inst_t * uart, t1_tpdu * tpdu,
		      int *sequence_counter, uint8_t * accepted_size)
{
	switch (tpdu->pcb) {
	case 0xC0:
		write_usb_debug(LOG_LEVEL_TRACE, "Resync Request\n");
		tpdu->nad = 0x00;
		tpdu->pcb = 0xE0;	// Resync Response
		tpdu->len = 0x00;
		t1_write(uart, *tpdu);
		*sequence_counter = 0;
		*accepted_size = 32;
		return 1;
	case 0xC1:
		write_usb_debug(LOG_LEVEL_TRACE, "IFSD Request");
		tpdu->pcb = 0xE1;
		*accepted_size = tpdu->apdu[0];
		*sequence_counter += t1_write(uart, *tpdu);
		return 2;
	case 0xE1:
		write_usb_debug(LOG_LEVEL_TRACE, "IFSD Response Success");
		return -1;
	case 0xC2:
		tpdu->pcb = 0xD2;
		tpdu->nad = 0x00;
		tpdu->pcb = 0xE2;	// Resync Response
		tpdu->len = 0x00;
		*sequence_counter += t1_write(uart, *tpdu);
		return 3;
	case 0xE2:
		write_usb_debug(LOG_LEVEL_TRACE, "Abort Response Success");
		return -1;
	case 0xE3:
		write_usb_debug(LOG_LEVEL_TRACE, "WTE Response Success");
		return -1;
	default:
		write_usb_debug(LOG_LEVEL_TRACE, "Unknown S Block detected!");
		// do nothing, let the reader proceed
		return 0;
	}
}

/**
 * Determine which type of R Block tpdu is
 * 
 * @param tpdu to be examined
 * @return -1 when the R Block type could not be determined
 *         0 when the R Block type has no error
 *         1 when the R Block type has an error (parity or other)
 */
int t1_classify_r_block(t1_tpdu * tpdu)
{
	switch (tpdu->pcb) {
	case 0x80:		// No Error
	case 0x90:		// No Error
		write_usb_debug(LOG_LEVEL_TRACE, "No Error");
		return 0;
	case 0x81:		// Parity Error
	case 0x91:		// Parity Error
	case 0x82:		// Other Error
	case 0x92:		// Other Error
		write_usb_debug(LOG_LEVEL_TRACE, "Error");
		return 1;
	default:
		write_usb_debug(LOG_LEVEL_TRACE, "Malformed R Block");
		return -1;
	}
}

/**
 * Set the correct sequence bit in the tpdu
 * 
 * @param sequence_counter the correct sequence bit
 * @param tpdu tpdu which is modified
 * @return void
 */
void t1_adjust_sequence_bit(int *sequence_counter, t1_tpdu * tpdu)
{
	if (*sequence_counter % 2 == 0) {
		tpdu->pcb &= 0xBF;	// set sequence counter to 0
		*sequence_counter = 0;
	} else {
		tpdu->pcb |= 0x40;	// set sequence counter to 1
		*sequence_counter = 1;
	}
}

/**
 * Write tpdu to uart. Calculates checksum incorrectly. Used for testing
 * Tpdu information field size needs to be < BLOCKSIZE. Where BLOCKSIZE 
 * is the negotiated size of the information field.
 * Can be used to check the error handling of reader
 * 
 * @param uart uart where the buffer is written to
 * @param tpdu tpdu to be sent (incorrectly)
 * @return 1, the number blocks sent
 */
int t1_write_faulty(uart_inst_t * uart, t1_tpdu tpdu)
{
	write_usb_debug(LOG_LEVEL_INFO, "WRITE FAULTY");
	char checksum = 0x11;
	size_t pos = 0;
	char rb;
	for (; pos < 3 + (int)tpdu.len; pos++) {
		char c;
		switch (pos) {
		case 0:
			c = (char)tpdu.nad;
			break;
		case 1:
			c = (char)tpdu.pcb;
			break;
		case 2:
			c = (char)tpdu.len;
			break;
		default:
			c = (char)tpdu.apdu[pos - 3];
		}
		checksum ^= c;
		uart_putc_raw(uart, c);
		rb = uart_getc(uart);
		if (c != rb) {
			write_usb_debug(LOG_LEVEL_DEBUG, "[%d] write: %02X  read: %02X", pos, c, rb);
		}
	}
	// write checksum LRC
	uart_putc_raw(uart, checksum);
	rb = uart_getc(uart);
	if (checksum != rb) {
		write_usb_debug(LOG_LEVEL_DEBUG, "[%d] write: %02X  read: %02X", pos, checksum, rb);
	}
	print_tpdu(LOG_LEVEL_INFO, "write", tpdu);
	return 1;
}

/**
 * Write tpdu to uart. Tpdu I Field size needs to be < BLOCKSIZE. 
 * Where BLOCKSIZE is the negotiated size of the information field.
 * 
 * @param uart uart where the buffer is written to
 * @param tpdu tpdu to be sent
 * @return 1, (=the number blocks sent)
 */
int t1_write(uart_inst_t * uart, t1_tpdu tpdu)
{
	print_tpdu(LOG_LEVEL_TRACE, "t1_write", tpdu);
	char checksum = 0x00;
	size_t pos = 0;
	char rb;
	for (; pos < 3 + (int)tpdu.len; pos++) {
		char c;
		switch (pos) {
		case 0:
			c = (char)tpdu.nad;
			break;
		case 1:
			c = (char)tpdu.pcb;
			break;
		case 2:
			c = (char)tpdu.len;
			break;
		default:
			c = (char)tpdu.apdu[pos - 3];
		}
		checksum ^= c;
		uart_putc_raw(uart, c);
		rb = uart_getc(uart);
		if (c != rb) {
			write_usb_debug(LOG_LEVEL_INFO, "index[%d] write: %02X read: %02X", pos, c, rb);
		}
	}
	// write checksum LRC
	uart_putc_raw(uart, checksum);
	rb = uart_getc(uart);
	if (checksum != rb) {
		write_usb_debug(LOG_LEVEL_INFO, "index[%d] write: %02X read: %02X", pos, checksum, rb);
	}
	return 1;
}

/**
 * handle response from t1_write
 * 
 * S Block: delegate to t1_handle_s_block()
 * R Block: resend if an error occurred, otherwise do nothing
 * I Block: do nothing, not expected
 * 
 * @param uart reponse is written to uart
 * @param send_tpdu the tpdu which was sent
 * @param recv_tpdu the tpdu which was received as a response
 * @param sequence_counter tracks state over the protocol
 * @param accepted_size max size of the information field the apdu is allowed to have
 * @return -2 when the recv_tpdu is an I Block
 *         -1 when the recv_tpdu is a S Block
 *          0 when the recv_tpdu is a R Block, but no error is communicated
 */
int
t1_handle_response_from_write(uart_inst_t * uart, t1_tpdu * send_tpdu,
			      t1_tpdu * recv_tpdu, int *sequence_counter,
			      uint8_t * accepted_size)
{
	int block_kind = check_block_kind(*recv_tpdu);
	if (block_kind == T1_S_BLOCK) {
		int s_case =
		    t1_handle_s_block(UART_ID, recv_tpdu, sequence_counter,
				      accepted_size);
		write_usb_debug(LOG_LEVEL_DEBUG, "s_case: %d", s_case);
		return -1;
	}
	if (block_kind == T1_R_BLOCK) {
		write_usb_debug(LOG_LEVEL_DEBUG, "R Block detected");
		int r_case = t1_classify_r_block(recv_tpdu);
		if (r_case == 0) {
			return 0;
		}
		*sequence_counter += t1_write(uart, *send_tpdu);
		print_tpdu(LOG_LEVEL_TRACE,"resend_tpdu", *send_tpdu);
		int posr = t1_read(uart, recv_tpdu);
		return t1_handle_response_from_write(uart, send_tpdu,
						     recv_tpdu,
						     sequence_counter,
						     accepted_size);
	}
	return -2;
}

/**
 * Write tpdu to uart. Tpdu I Field size is not restricted.
 * Uses block chaining when needed.
 * 
 * taken from:
 * https://github.com/wookey-project/libiso7816/blob/master/smartcard_iso7816.c#L1741
 * 
 * @param uart uart where the buffer is written to
 * @param block_size max size of the information field
 * @param tpdu struct used to transfer the buffer
 * @param buffer contains the apdu transferred via uart
 * @param buffer_size size of the buffer
 * @return 0 if the number of blocks sent is even
 *         1 if the number of blocks sent is odd
 */
int
t1_write_complete_buffer(uart_inst_t * uart, uint8_t block_size,
			 t1_tpdu * tpdu, uint8_t * buffer, size_t buffer_size)
{
	uint8_t *send_buffer = malloc(MAX_INFORMATION_FIELD_SIZE * sizeof(uint8_t));

	int num_iblocks = (buffer_size / block_size) + 1;
	if (((buffer_size % block_size) == 0) && (buffer_size != 0)) {
		num_iblocks--;
	}

	t1_tpdu send_tpdu;
	t1_tpdu recv_tpdu;
	memset(&send_tpdu, 0, sizeof(send_tpdu));
	memset(&recv_tpdu, 0, sizeof(recv_tpdu));

	send_tpdu.nad = tpdu->nad;
	send_tpdu.pcb = tpdu->pcb;
	int sequence_counter = 0;
	size_t sent_size = 0;
	int posw;
	for (int i = 0; i < num_iblocks; i++) {
		uint8_t current_block_size;
		if (buffer_size - sent_size > block_size) {
			current_block_size = block_size;
		} else {
			current_block_size = buffer_size - sent_size;
		}
		write_usb_debug(LOG_LEVEL_TRACE, "max block size: %d", block_size);
		write_usb_debug(LOG_LEVEL_TRACE, "buffer size: %d", buffer_size);
		write_usb_debug(LOG_LEVEL_TRACE, "current block size: %d", current_block_size);

		if (sent_size + current_block_size < buffer_size) {
			write_usb_debug
			    (LOG_LEVEL_TRACE, "buffer set M-bit: accepted size %d", block_size);
			send_tpdu.pcb |= (0x1 << 5);	// set M-bit, to indicate
			// block chaining
		} else {
			send_tpdu.pcb &= 0xdf;	// unset M-bit
		}
		send_tpdu.len = current_block_size;
		t1_read_into_tpdu(send_tpdu.nad, send_tpdu.pcb,
				  current_block_size, buffer + sent_size,
				  &send_tpdu);
		sequence_counter += t1_write(uart, send_tpdu);
		if (sequence_counter % 2 == 0) {
			send_tpdu.pcb &= 0xBF;
			sequence_counter = 0;
		} else {
			send_tpdu.pcb |= 0x40;
			sequence_counter = 1;
		}

		//print_hex("send_tpdu", send_tpdu.apdu, send_tpdu.len);
		sent_size += current_block_size;
		if (sent_size == buffer_size) {
			continue;
		}

		int posr = t1_read(uart, &recv_tpdu);
		int r = t1_handle_response_from_write(uart, &send_tpdu,
						      &recv_tpdu,
						      &sequence_counter,
						      &block_size);
		write_usb_debug(LOG_LEVEL_TRACE, "t1 bc handling: %d\n", r);

	}
	return send_tpdu.pcb && 0x40;
}

/**
 * Read tpdu from uart. alawys return -1 which indicates a
 * checksum mismatch
 * Can be used to check error handling of the reader
 * 
 * @param uart uart where characters are read from
 * @param tpdu the characters from uart are read into this struct
 * @return -1 always
 */
int t1_read_faulty(uart_inst_t * uart, t1_tpdu * tpdu)
{
	write_usb_debug(LOG_LEVEL_TRACE, "T1 READ FAULTY");
	int pos = 3;
	char checksum = 0x00, c;

	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -1;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->nad = c;
	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -1;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->pcb = c;
	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -1;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->len = c;

	write_usb_debug(LOG_LEVEL_TRACE, "len: %d", (int)tpdu->len);
	for (int i = 0; i < (int)tpdu->len; i++) {
		c = uart_getc(uart);
		checksum ^= c;
		tpdu->apdu[i] = c;
		if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT)) {
			return -1;
		}
		pos++;
	}
	c = uart_getc(uart);
	pos++;
	print_tpdu(LOG_LEVEL_TRACE, "read", *tpdu);
	if (c != checksum) {
		return -1;
	}
	return -1;
}

/**
 * Read tpdu from uart
 * 
 * @param uart uart where characters are read from
 * @param tpdu the characters from uart are read into this struct
 * @return -2 when a timeout occurs reading
 *         -1 when the checksum does not match
 *         otherwise the length of the information field
 */
int t1_read(uart_inst_t * uart, t1_tpdu * tpdu)
{
	int pos = 3;
	char checksum = 0x00, c;

	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -2;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->nad = c;
	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -2;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->pcb = c;
	if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
		return -2;
	c = uart_getc(uart);
	checksum ^= c;
	tpdu->len = c;

	if (tpdu->nad == 0xFF){
		if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT))
			return -2;
		char c4 = uart_getc(uart); //forth character

		char pps[4] = {tpdu->nad, tpdu->pcb, tpdu->len, c4};
		// echo pps
		sc_write(UART_ID, pps, PPS_LEN);

		int fi = convert_f(tpdu->len);
		uint8_t di = convert_d(tpdu->len);
		uart_tx_wait_blocking(UART_ID);
		uint32_t br = calculate_baudrate(fi, di, fixed_clk);
		uart_set_baudrate(UART_ID,br);
		write_usb_debug(LOG_LEVEL_INFO, "set baudrate = %d", br);
		return -3;
	}

	for (int i = 0; i < (int)tpdu->len; i++) {
		c = uart_getc(uart);
		checksum ^= c;
		tpdu->apdu[i] = c;
		if (!uart_is_readable_within_us(uart, UART_READ_TIMEOUT)) {
			return -2;
		}
		pos++;
	}
	c = uart_getc(uart);
	print_tpdu(LOG_LEVEL_TRACE, "read", *tpdu);
	pos++;
	if (c != checksum) {
		return -1;
	}
	return pos;
}

/**
 * Read the provided arguments into the tpdu struct.
 * Also sets the checksum.
 * 
 * @param nad node address, first byte of the prologue field
 * @param pcb procedure byte, second byte of the prologe field
 * @param len length of the information field, third byte of the prologue field
 * @param buffer content of information field. Size is exactly @param len
 * @param tpdu the struct the other parameters are read into
 * @return 0
 */
int
t1_read_into_tpdu(uint8_t nad, uint8_t pcb, uint8_t len, uint8_t * buffer,
		  t1_tpdu * tpdu)
{
	char checksum = 0x00;
	tpdu->nad = nad;
	checksum ^= nad;
	tpdu->pcb = pcb;
	checksum ^= pcb;
	tpdu->len = len;
	checksum ^= len;
	memcpy(tpdu->apdu, buffer, (int)tpdu->len);
	for (int i = 0; i < (int)tpdu->len; i++) {
		checksum ^= buffer[i];
	}
	tpdu->check = checksum;
	return 0;
}

/**
 * Alarm callback. Sends a waiting time extension request and reschedules
 * the alarm with the same interval
 * 
 * @param id alarm id
 * @param bwt_interval interval to reschedule the alarm
 * @return whether the alarm should be rescheduled
 */
int64_t t1_timer_block_waiting_extension(alarm_id_t id, void *bwt_interval)
{
	long bwt = *(long *)bwt_interval;
	write_usb_debug(LOG_LEVEL_DEBUG, "WTE %d %d", bwt, (long *)bwt_interval);
	t1_tpdu s_block;
	s_block.nad = 0x00;
	s_block.pcb = 0xC3;
	s_block.len = 0x01;
	s_block.apdu[0] = WAITING_TIME_EXTENSION_MULTIPIER;

	seq += t1_write(UART_ID, s_block);

	t1_tpdu response;
	int posr = t1_read(UART_ID, &response);
	if (posr == -2) {
		write_usb_debug(LOG_LEVEL_DEBUG, "timeout int WTE Response");
	}
	if (posr == -1) {
		write_usb_debug(LOG_LEVEL_DEBUG, "Checksum mismatch");
	} else if (response.pcb == 0xE3) {
		write_usb_debug(LOG_LEVEL_DEBUG, "correct pcb");
	} else {
		print_tpdu(LOG_LEVEL_DEBUG, "WTE Response", response);
	}
	return bwt;
}

/**
 * Sends a waiting time extension request
 * 
 * @param sequence_counter tracks state over the protocol
 * @return -1 when an error is communicated in the response
 *          1 otherwise i.e. in case of success
 */
int t1_block_waiting_extension(int *sequence_counter)
{
	t1_tpdu s_block;
	s_block.nad = 0x00;
	s_block.pcb = 0xC3;
	s_block.len = 0x01;
	s_block.apdu[0] = WAITING_TIME_EXTENSION_MULTIPIER;

	*sequence_counter += t1_write(UART_ID, s_block);

	t1_tpdu response;
	int posr = t1_read(UART_ID, &response);
	if (posr == -2) {
		write_usb_debug(LOG_LEVEL_DEBUG, "timeout int WTE Response");
		return -1;
	}
	if (posr == -1) {
		write_usb_debug(LOG_LEVEL_DEBUG, "Checksum mismatch");
		return -1;
	}
	if (response.pcb == 0xE3) {
		write_usb_debug(LOG_LEVEL_DEBUG, "correct pcb");
		return 1;
	}
	print_tpdu(LOG_LEVEL_DEBUG, "WTE Response", response);
	return -1;
}

/**
 * Conversion table for F Parameter
 * 
 * @return the converted value
 */
int convert_f(uint8_t TA1)
{
	switch (TA1 >> 4) {
	case 2:
		return 558;
	case 3:
		return 744;
	case 4:
		return 1116;
	case 5:
		return 1408;
	case 6:
		return 1860;
	case 9:
		return 512;
	case 10:
		return 768;
	case 11:
		return 1024;
	case 12:
		return 1536;
	case 13:
		return 2048;
	default:
		break;
	}
	return 372;
}

/**
 * Conversion table for D Parameter
 * 
 * @return the converted value
 */
int convert_d(uint8_t TA1)
{
	switch (TA1 % 16) {
	case 1:
		return 1;
	case 2:
		return 2;
	case 3:
		return 4;
	case 4:
		return 8;
	case 5:
		return 16;
	case 6:
		return 32;
	case 8:
		return 12;
	case 9:
		return 20;
	default:
		break;
	}
	return 1;
}

/**
 * Parse the given atr and sets attributes.
 * The function expects atr.payload and atr.len to be set.
 * 
 * @param atr atr which is parsed and its attributes are set.
 * @return void
 */
void parse_ATR(answer_to_reset * atr)
{
	// inspect TS
	if (atr->payload[0] == 0x3F) {
		write_usb_debug(LOG_LEVEL_DEBUG, "invert %02X", atr->payload[0]);
		uint8_t inv_atr[atr->len];
		inv_atr[0] = 0x3B;
		write_usb_debug(LOG_LEVEL_DEBUG, "%02X -> %02X", atr->payload[0], *inv_atr);
		for (int i = 1; i < atr->len; i++) {
			inv_atr[i] = ~atr->payload[i];
			write_usb_debug(LOG_LEVEL_DEBUG, "%02X -> %02X", atr->payload[i], inv_atr[i]);
		}
		atr->payload = inv_atr;

		parse_ATR(atr);
		return;
	}
	if (atr->payload[0] != 0x3B) {
		write_usb_debug(LOG_LEVEL_DEBUG, "TS unkown value: %02X\n", atr->payload[0]);
		return;
	}
	// set default values
	atr->D = 1;
	atr->F = 372;
	atr->IFSC = 32;
	atr->BWI = 4;
	atr->CWI = 13;
	atr->protocol = 2;

	// inspect T0
	int index = 2;
	uint8_t TD1 = 0x00;
	if ((atr->payload[1] >> 4) % 2 == 1) {
		atr->D = convert_d(atr->payload[index]);
		atr->F = convert_f(atr->payload[index]);
		index++;
	}
	if ((atr->payload[1] >> 5) % 2 == 1) {
		index++;
	}
	if ((atr->payload[1] >> 6) % 2 == 1) {
		index++;
	}
	if ((atr->payload[1] >> 7) % 2 == 1) {
		TD1 = atr->payload[index];
		if (TD1 % 16 == 1) {
			atr->protocol = 1;
		} else if (TD1 % 16 == 0) {
			atr->protocol = 0;
		} else {
			// reset D and F since the values were meant for another protocol
			atr->D = 1;
			atr->F = 372;
		}
		index++;
	}

	uint8_t TD_next = 0x00;
	{			// dont really care about this
		if ((TD1 >> 4) % 2 == 1) {
			index++;
		}
		if ((TD1 >> 5) % 2 == 1) {
			index++;
		}
		if ((TD1 >> 6) % 2 == 1) {
			index++;
		}
		if ((TD1 >> 7) % 2 == 1) {
			TD_next = atr->payload[index];
			index++;
		}
	}

	int i = 3;
	while (TD_next >= 16) {
		uint8_t TD_now = TD_next;
		TD_next = 0x00;
		int IFSC_tmp = 32;
		int BWI_tmp = 4;
		int CWI_tmp = 13;
		if ((TD_now >> 4) % 2 == 1) {
			IFSC_tmp = atr->payload[index];
			index++;
		}
		if ((TD_now >> 5) % 2 == 1) {
			BWI_tmp = atr->payload[index] >> 4;
			CWI_tmp = atr->payload[index] % 16;
			index++;
		}
		if ((TD_now >> 6) % 2 == 1) {
			index++;
		}
		if ((TD_now >> 7) % 2 == 1) {
			TD_next = atr->payload[index];
			if (TD_next % 16 <= 1) {
				atr->IFSC = IFSC_tmp;
				atr->BWI = BWI_tmp;
				atr->CWI = CWI_tmp;
			}
			index++;
		} else if (TD_now % 16 <= 1) {
			atr->IFSC = IFSC_tmp;
			atr->BWI = BWI_tmp;
			atr->CWI = CWI_tmp;
		}
		i++;
	}

	double D_d = (double)atr->D;
	double F_d = (double)atr->F;
	double Hz = get_sc_reader_clk();

	write_usb_debug(LOG_LEVEL_DEBUG, "ATR D=%d, F=%d", atr->D, atr->F);

	atr->work_etu = (1 / D_d) * (atr->F / Hz);

	atr->BWT_us =
	    ((pow(2, atr->BWI) * 960 * F_d / Hz) + atr->work_etu) * 1000000;

	atr->CWT_us = (pow(2, atr->CWI) + 11) * atr->work_etu * 1000000;
}

uint32_t get_sc_reader_clk()
{
	uint32_t f_clk_peri =
	    frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
	return f_clk_peri * 1000;
}

/**
 * Calculate the baudrate 
 * 
 * @param fi parameter to calculate baudrate
 * @param di parameter to calculate baudrate
 * @param modem_clk clock of the reader
 * 
 */
uint32_t calculate_baudrate(int fi, int di, uint32_t modem_clk)
{
	uint32_t baudrate = (modem_clk * di) / fi;
	write_usb_debug(LOG_LEVEL_DEBUG, "clock %d, fi %d, di %d --> baudrate %d",
				modem_clk, fi, di, baudrate);
	return baudrate;
}
