#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#define RAW_UART DT_NODELABEL(usart3)
#define RAW_REQUEST_PREFIX "KGROUND-RAW-PING "
#define RAW_RESPONSE_PREFIX "KGROUND-RAW-PONG "
#define RAW_SEQUENCE_LENGTH 4

static const struct device *const raw_uart = DEVICE_DT_GET(RAW_UART);

static void send_bytes(const char *bytes, size_t length)
{
	for (size_t index = 0; index < length; ++index) {
		uart_poll_out(raw_uart, bytes[index]);
	}
}

static bool valid_request(const char *line, size_t length)
{
	const size_t prefix_length = sizeof(RAW_REQUEST_PREFIX) - 1;

	if (length != prefix_length + RAW_SEQUENCE_LENGTH + 1 ||
	    memcmp(line, RAW_REQUEST_PREFIX, prefix_length) != 0 || line[length - 1] != '\r') {
		return false;
	}

	for (size_t index = prefix_length; index < prefix_length + RAW_SEQUENCE_LENGTH; ++index) {
		if (line[index] < '0' || line[index] > '9') {
			return false;
		}
	}

	return true;
}

int main(void)
{
	char line[64];
	size_t length = 0;
	uint8_t byte;

	if (!device_is_ready(raw_uart)) {
		printk("HOLYBRO RAW PEER: FAIL (USART3 not ready)\n");
		return 1;
	}

	printk("HOLYBRO RAW PEER: READY uart=USART3 baud=57600\n");
	for (;;) {
		if (uart_poll_in(raw_uart, &byte) != 0) {
			continue;
		}

		if (byte == '\n') {
			if (valid_request(line, length)) {
				const size_t prefix_length = sizeof(RAW_REQUEST_PREFIX) - 1;

				send_bytes(RAW_RESPONSE_PREFIX, sizeof(RAW_RESPONSE_PREFIX) - 1);
				send_bytes(&line[prefix_length], RAW_SEQUENCE_LENGTH);
				send_bytes("\r\n", 2);
				printk("HOLYBRO RAW PEER: EXCHANGE PASS\n");
			} else {
				printk("HOLYBRO RAW PEER: RX INVALID length=%u\n",
				       (unsigned int)length);
			}
			length = 0;
			continue;
		}

		if (length < sizeof(line)) {
			line[length++] = (char)byte;
		} else {
			length = 0;
		}
	}

	return 0;
}
