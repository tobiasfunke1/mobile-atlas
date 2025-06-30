
#include <signal.h>

#include "pico.h"
#include "pico/util/queue.h"
#include "pico/stdlib.h"
#include "pico/printf.h"
#include "pico/stdio/driver.h"

// Protocol Bytes
#define OP_APDU 0x00
#define OP_RESET 0x01
#define OP_DEBUGMSG 0x02
#define OP_SENDATR 0x03
#define OP_MEASUREMENT 0x04
#define OP_REQUEST_STATE 0x05
#define OP_SET_UARTMODE 0x06
#define OP_SET_LOGLEVEL 0x07


#define PPS_BYTE 0xFF
#define HEADER_LEN 5 // 5 header bytes (cla, ins, p1, p2, p3)
#define PPS_LEN 4
#define SW_LEN 2

#define MAX_INFORMATION_FIELD_SIZE 254
#define T1_I_BLOCK 0
#define T1_R_BLOCK 1
#define T1_S_BLOCK 2


#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_DEBUG 2
#define LOG_LEVEL_TRACE 3

#define UART_ID uart0
#define BAUD_RATE 9600

// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
#define UART_TX_PIN 16
#define UART_RX_PIN 17
#define GPIO_RESET_PIN 18
#define PIN_SIM_CLK 20

#define CLK_DEFAULT 3571200

#define USB_READ_TIMEOUT 1000000
#define UART_READ_TIMEOUT 1200000

#define WAITING_TIME_EXTENSION_MULTIPIER 0x02

#define UART_MODE_SYNCHRONOUS 0
#define UART_MODE_ASYNCHRONOUS 1

#ifdef UART_ASYNCHRONOUS 
#define UART_MODE UART_MODE_ASYNCHRONOUS
#else
#define UART_MODE UART_MODE_SYNCHRONOUS
#endif

static sig_atomic_t seq = 0;
extern sig_atomic_t fixed_clk;
extern sig_atomic_t current_loglevel;

typedef struct {
	uint8_t nad, pcb, len;
	char apdu[256];
	uint8_t check;
} t1_tpdu;

typedef struct {
	int D;
	int F;
	uint8_t IFSC;
	uint8_t BWI;
	long BWT_us;
	uint8_t CWI;
	long CWT_us;
	double work_etu;
	uint8_t len;
	uint8_t protocol;
	uint8_t *payload;
} answer_to_reset;


void write_usb_uart(uint8_t opcode, uint8_t * data, uint32_t len);

void write_usb_uart_apdu(uint8_t * data, uint32_t len);

int write_usb_debug(uint8_t msglevel, const char *format, ...);
int write_usb_measurement(const char *format, ...);

/**
 * Print buffer as a hex string
 * 
 * @param header start the printed line with header
 * @param buf buffer to be converted to hex string
 * @param len length of the buffer
 * @return void
 */
void print_hex(uint8_t msglevel, char *header, uint8_t * buf, size_t len);

/**
 * Print tpdu as a hex string
 * 
 * @param header start the printed line with header
 * @param tpdu tpdu to be converted to hex string
 * @param checksum checksum of the tpdu
 * @return void
 */
void print_tpdu(uint8_t msglevel, char *header, t1_tpdu tpdu);

/**
 * Discard incoming/outgoing chars from uart
 * 
 * @param uart uart to be drained
 * @return void
 */
void discard_uart_buffer(uart_inst_t * uart);

/**
 * Write out buffer to uart
 * 
 * @param uart uart where the buffer is written to
 * @param buf buffer to be written
 * @param len length of the buffer
 * @return length of the written buffer
 */
int sc_write(uart_inst_t * uart, uint8_t * buf, size_t len);

/**
 * Read from UART a given number of characters into a buffer
 * 
 * @param uart uart where chars are read from
 * @param buf buffer to read chars into
 * @param len max number of chars to read
 * @return number of chars read
 */
int sc_read(uart_inst_t * uart, uint8_t * buf, size_t len);

/**
 * Determine the kind of block of a tpdu
 * 
 * @param tpdu the tpdu to be examined
 * @return 0 if it is an I Block
 *         1 if it is an R Block
 *         2 if it is an S Block
 */
int check_block_kind(t1_tpdu tpdu);

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
		      int *sequence_counter, uint8_t * accepted_size);

/**
 * Determine which type of R Block tpdu is
 * 
 * @param tpdu to be examined
 * @return -1 when the R Block type could not be determined
 *         0 when the R Block type has no error
 *         1 when the R Block type has an error (parity or other)
 */
int t1_classify_r_block(t1_tpdu * tpdu);

/**
 * Set the correct sequence bit in the tpdu
 * 
 * @param sequence_counter the correct sequence bit
 * @param tpdu tpdu which is modified
 * @return void
 */
void t1_adjust_sequence_bit(int *sequence_counter, t1_tpdu * tpdu);

/**
 * Write tpdu to uart. Calculates checksum incorrectly. Used for testing
 * Tpdu information field size needs to be < BLOCKSIZE. Where BLOCKSIZE 
 * is the negotiated size of the information field.
 * 
 * @param uart uart where the buffer is written to
 * @param tpdu tpdu to be sent (incorrectly)
 * @return 1, the number blocks sent
 */
int t1_write_faulty(uart_inst_t * uart, t1_tpdu tpdu);

/**
 * Write tpdu to uart. Tpdu I Field size needs to be < BLOCKSIZE. 
 * Where BLOCKSIZE is the negotiated size of the information field.
 * 
 * @param uart uart where the buffer is written to
 * @param tpdu tpdu to be sent
 * @return 1, the number blocks sent
 */
int t1_write(uart_inst_t * uart, t1_tpdu tpdu);

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
int t1_write_complete_buffer(uart_inst_t * uart, uint8_t block_size,
			     t1_tpdu * tpdu, uint8_t * buffer,
			     size_t buffer_size);

/**
 * Read tpdu from uart. alawys return -1 which indicates a
 * checksum mismatch
 * 
 * @param uart uart where characters are read from
 * @param tpdu the characters from uart are read into this struct
 * @return -1 always
 */
int t1_read_faulty(uart_inst_t * uart, t1_tpdu * tpdu);

/**
 * Read tpdu from uart
 * 
 * @param uart uart where characters are read from
 * @param tpdu the characters from uart are read into this struct
 * @return -2 when a timeout occurs reading
 *         -1 when the checksum does not match
 *         otherwise the length of the information field
 */
int t1_read(uart_inst_t * uart, t1_tpdu * tpdu);

/**
 * Read the provided arguments into the tpdu struct.
 * Also sets the checksum.
 * 
 * @param nad node address, first byte of the prologue field
 * @param pcb procedure byte, second byte of the prologe field
 * @param len length of the information field, third byte of the prologue field
 * @param buffer content of information field. Size is exactly @param len
 * @param tpdu the struct the other parameters are read into
 */
int t1_read_into_tpdu(uint8_t nad, uint8_t pcb, uint8_t len,
		      uint8_t * buffer, t1_tpdu * tpdu);
/**
 * Alarm callback. Sends a waiting time extension request and reschedules
 * the alarm with the same interval
 * 
 * @param id alarm id
 * @param bwt_interval interval to reschedule the alarm
 * @return whether the alarm should be rescheduled
 */
int64_t t1_timer_block_waiting_extension(alarm_id_t id, void *bwt_interval);

/**
 * Sends a waiting time extension request
 * 
 * @param sequence_counter tracks state over the protocol
 * @return -1 when an error is communicated in the response
 *          1 otherwise i.e. in case of success
 */
int t1_block_waiting_extension(int *sequence_counter);

/**
 * Write the infromatin field i.e. the apdu of a tpdu to usb
 * 
 * @param tpdu contains the apdu which will be sent to usb
 * @return void
 */
void write_apdu_usb(char *apdu, size_t length);

/**
 * Conversion table for F Parameter
 * 
 * @return the converted value
 */
int convert_f(uint8_t TA);

/**
 * Conversion table for D Parameter
 * 
 * @return the converted value
 */
int convert_d(uint8_t TA);

/**
 * Parse the given atr and sets attributes.
 * The function expects atr.payload and atr.len to be set.
 * 
 * @param atr atr which is parsed and its attributes are set.
 * @return void
 */
void parse_ATR(answer_to_reset * atr);

uint32_t get_sc_reader_clk();

/**
 * Calculate the baudrate 
 * 
 * @param fi parameter to calculate baudrate
 * @param di parameter to calculate baudrate
 * @param modem_clk clock of the reader
 * 
 */
uint32_t calculate_baudrate(int fi, int di, uint32_t modem_clk);
