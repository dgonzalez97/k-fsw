#include <string.h>
#include <math.h>
#include <param/param_serializer.h>
#include <param/param_list.h>
#include <csp/csp.h>
#include <csp/csp_crc32.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <csp/interfaces/csp_if_lo.h>
#include <param/param_server.h>
#include <param/param_queue.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/parameter.h>
#include "parameter_internal.h"
#if CONFIG_KFSW_BOTON_TEST
#include <kfsw/modules/boton_test.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

static K_SEM_DEFINE(sample_entered, 0, 1);
static K_SEM_DEFINE(sample_release, 0, 1);
static K_SEM_DEFINE(ping_reply, 0, 1);
static K_SEM_DEFINE(value_reply, 0, 8);
static atomic_t block_sample;
static uint32_t backing;

extern const k_tid_t kfsw_param_list_thread;
static atomic_t fail_list_allocation;
static K_SEM_DEFINE(list_reply, 0, 1);
static uint16_t listed_id, listed_total;
static uint8_t listed_status;
static uint32_t listed_crc;

csp_packet_t *__real_csp_buffer_get(size_t size);

csp_packet_t *__wrap_csp_buffer_get(size_t size)
{
	if (k_current_get() == kfsw_param_list_thread && atomic_cas(&fail_list_allocation, 1, 0)) {
		return NULL;
	}
	return __real_csp_buffer_get(size);
}

static void sample(void *value)
{
	if (atomic_cas(&block_sample, 1, 0)) {
		k_sem_give(&sample_entered);
		zassert_ok(k_sem_take(&sample_release, K_SECONDS(2)));
	}
	*(uint32_t *)value = 42;
}

static uint32_t writable;
static int changes;
static uint8_t narrow;
static int8_t signed_narrow;
static float real;
static char text[8];
static uint8_t bytes[4];

static int validate_real(const union kfsw_param_scalar *value)
{
	return isfinite(value->f32) && value->f32 <= 10.0f ? 0 : -ERANGE;
}

static int validate_text(const char *value)
{
	return strcmp(value, "bad") == 0 ? -EINVAL : 0;
}

static int validate_bytes(const uint8_t *value, size_t size)
{
	ARG_UNUSED(size);
	return value[0] <= 10 ? 0 : -ERANGE;
}

static void text_changed(const char *value)
{
	ARG_UNUSED(value);
	changes++;
}

static void bytes_changed(const uint8_t *value, size_t size)
{
	ARG_UNUSED(value);
	ARG_UNUSED(size);
	changes++;
}

static int validate_timeout(const union kfsw_param_scalar *value)
{
	return value->u32 >= 7200 ? 0 : -ERANGE;
}

static void changed(const union kfsw_param_scalar *value)
{
	ARG_UNUSED(value);
	changes++;
}

#if KFSW_TEST_APPLY
static uint32_t applied_storage;
static uint32_t applied_owner;
static int apply_calls;
static int apply_error;

static int apply_scalar(const union kfsw_param_scalar *value)
{
	apply_calls++;
	if (apply_error != 0) {
		return apply_error;
	}
	applied_owner = value->u32;
	return 0;
}
#endif

static const struct kfsw_param_definition write_definitions[] = {
	{.offset = 0,
	 .type = KFSW_PARAM_U32,
	 .name = "timeout",
	 .value = &writable,
	 .default_value.u32 = 86400,
	 .validate = validate_timeout,
	 .changed = changed},
	{.offset = 1,
	 .type = KFSW_PARAM_U8,
	 .name = "narrow",
	 .value = &narrow,
	 .changed = changed},
	{.offset = 2,
	 .type = KFSW_PARAM_I8,
	 .name = "signed",
	 .value = &signed_narrow,
	 .changed = changed},
	{.offset = 3,
	 .type = KFSW_PARAM_FLOAT,
	 .name = "real",
	 .value = &real,
	 .validate = validate_real,
	 .changed = changed},
	{.offset = 4,
	 .type = KFSW_PARAM_STRING,
	 .name = "text",
	 .value = text,
	 .capacity = sizeof(text),
	 .validate_text = validate_text,
	 .changed_text = text_changed},
	{.offset = 5,
	 .type = KFSW_PARAM_DATA,
	 .name = "bytes",
	 .value = bytes,
	 .capacity = sizeof(bytes),
	 .validate_data = validate_bytes,
	 .changed_data = bytes_changed},
#if KFSW_TEST_APPLY
	{.offset = 6,
	 .type = KFSW_PARAM_U32,
	 .name = "applied",
	 .value = &applied_storage,
	 .apply = apply_scalar},
#endif
};
static const struct kfsw_param_definition_set write_set = {
	.table = 1,
	.name = "writes",
	.definitions = write_definitions,
	.count = ARRAY_SIZE(write_definitions),
};

static void push_timeout(uint32_t value, bool accepted)
{
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t node = 7;
	param_t param = {.id = KFSW_PARAM_ID(1, 0),
			 .node = &node,
			 .type = PARAM_TYPE_UINT32,
			 .array_size = 1,
			 .array_step = 4};

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = 20,
				.dport = CONFIG_KFSW_PARAM_PORT,
				.flags = CSP_FCRC32};
	packet->data[0] = PARAM_PUSH_REQUEST_V2;
	packet->data[1] = 0;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	zassert_ok(param_queue_add(&queue, &param, -1, &value));
	packet->length = queue.used + 2;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
	if (accepted) {
		zassert_ok(k_sem_take(&value_reply, K_SECONDS(1)));
	} else {
		zassert_not_equal(k_sem_take(&value_reply, K_MSEC(50)), 0,
				  "rejected write must not acknowledge success");
	}
}

static void send_encoded(csp_packet_t *packet, bool accepted)
{
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = 20,
				.dport = CONFIG_KFSW_PARAM_PORT,
				.flags = CSP_FCRC32};
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
	if (accepted) {
		zassert_ok(k_sem_take(&value_reply, K_SECONDS(1)));
	} else {
		zassert_not_equal(k_sem_take(&value_reply, K_MSEC(50)), 0);
	}
}

static void check_cached_response(void)
{
	static uint16_t peer = 42;
	static uint32_t cached_value;
	static param_t cached = {.id = KFSW_PARAM_ID(1, 6),
				 .node = &peer,
				 .type = PARAM_TYPE_UINT32,
				 .array_size = 1,
				 .array_step = 4,
				 .addr = &cached_value};
	uint16_t implicit_source = 0;
	param_t wire = cached;
	const uint8_t responses[] = {PARAM_PULL_RESPONSE, PARAM_PULL_RESPONSE_V2};
	int before = changes;
#if KFSW_TEST_APPLY
	int applied_before = apply_calls;
#endif

	zassert_ok(param_list_add(&cached));
	wire.node = &implicit_source;
	for (size_t i = 0; i < ARRAY_SIZE(responses); i++) {
		csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
		param_queue_t queue = {0};
		uint32_t value = 123 + i;

		zassert_not_null(packet);
		packet->data[0] = responses[i];
		packet->data[1] = PARAM_FLAG_END;
		param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0,
				 PARAM_QUEUE_TYPE_SET, i == 0 ? 1 : 2);
		zassert_ok(param_queue_add(&queue, &wire, -1, &value));
		packet->length = queue.used + 2;
		send_encoded(packet, false);
		zassert_equal(cached_value, value);
		zassert_equal(changes, before);
#if KFSW_TEST_APPLY
		zassert_equal(apply_calls, applied_before, "cached response applied local value");
		zassert_equal(applied_storage, 17);
		zassert_equal(applied_owner, 17);
#endif
	}
}

#if KFSW_TEST_APPLY
enum v1_apply_request { APPLY_SINGLETON, APPLY_MIXED, APPLY_REPEATED };

static void check_v1_apply_push(enum v1_apply_request tail, uint32_t value, bool accepted)
{
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t node = 7;
	param_t scalar = {.id = KFSW_PARAM_ID(1, 6),
			  .node = &node,
			  .type = PARAM_TYPE_UINT32,
			  .array_size = 1,
			  .array_step = sizeof(value)};

	BUILD_ASSERT(KFSW_PARAM_ID(1, 6) <= 0x1FF);
	zassert_not_null(packet);
	packet->data[0] = PARAM_PUSH_REQUEST;
	packet->data[1] = 0;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 1);
	zassert_ok(param_queue_add(&queue, &scalar, -1, &value));
	if (tail != APPLY_SINGLETON) {
		scalar.id = KFSW_PARAM_ID(1, tail == APPLY_MIXED ? 0 : 6);
		value = 9000;
		zassert_ok(param_queue_add(&queue, &scalar, -1, &value));
	}
	packet->length = queue.used + 2;
	send_encoded(packet, accepted);
}

static void check_v1_apply(void)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_U32, .size = sizeof(uint32_t), .scalar.u32 = 23};
	int before = changes;

	zassert_equal(apply_calls, 0, "initialization applied owner value");
	check_v1_apply_push(APPLY_SINGLETON, 17, true);
	zassert_equal(apply_calls, 1);
	zassert_equal(applied_storage, 17);
	zassert_equal(applied_owner, 17);
	apply_error = -EIO;
	check_v1_apply_push(APPLY_SINGLETON, 23, false);
	zassert_equal(apply_calls, 2);
	zassert_equal(applied_storage, 17);
	zassert_equal(applied_owner, 17);
	zassert_equal(kfsw_param_set("applied", &value), -EIO);
	zassert_equal(apply_calls, 3);
	zassert_equal(applied_storage, 17);
	zassert_equal(applied_owner, 17);
	apply_error = 0;
	check_v1_apply_push(APPLY_MIXED, 23, false);
	check_v1_apply_push(APPLY_REPEATED, 23, false);
	zassert_equal(apply_calls, 3, "refused v1 batch reached applier");
	zassert_equal(applied_storage, 17);
	zassert_equal(applied_owner, 17);
	zassert_equal(writable, 8000);
	zassert_equal(changes, before);
}
#endif

#if CONFIG_KFSW_BOTON_TEST
static atomic_t gpio_calls;
static int gpio_error;

int __real_kfsw_boton_test_led_gpio_set(enum kfsw_boton_test_led led, bool on);

int __wrap_kfsw_boton_test_led_gpio_set(enum kfsw_boton_test_led led, bool on)
{
	atomic_inc(&gpio_calls);
	return gpio_error != 0 ? gpio_error : __real_kfsw_boton_test_led_gpio_set(led, on);
}

int serial_get(void)
{
	return 99;
}

static void check_led_push(uint8_t request, uint8_t flags, int tail, bool accepted)
{
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t node = 7;
	uint8_t on = 1;
	uint32_t timeout = tail == 1 ? 1 : 9000;
	param_t led = {.id = KFSW_PARAM_ID(67, 8),
		       .node = &node,
		       .type = PARAM_TYPE_UINT8,
		       .array_size = 1,
		       .array_step = 1};
	param_t software = {.id = KFSW_PARAM_ID(1, 0),
			    .node = &node,
			    .type = PARAM_TYPE_UINT32,
			    .array_size = 1,
			    .array_step = 4};
	int before = changes;

	zassert_not_null(packet);
	packet->data[0] = request;
	packet->data[1] = flags;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 6, 0, PARAM_QUEUE_TYPE_SET,
			 request == PARAM_PUSH_REQUEST ? 1 : 2);
	zassert_ok(param_queue_add(&queue, &led, -1, &on));
	if (tail == 1 || tail == 2) {
		zassert_ok(param_queue_add(&queue, &software, -1, &timeout));
	} else if (tail == 3 || tail == 4) {
		led.id = KFSW_PARAM_ID(67, tail == 3 ? 8 : 9);
		zassert_ok(param_queue_add(&queue, &led, -1, &on));
	} else if (tail == 7) {
		software.id = KFSW_PARAM_ID(20, 0);
		zassert_ok(param_queue_add(&queue, &software, -1, &timeout));
	}
	packet->length = queue.used + 2;
	if (tail == 8 || tail == 9) {
		mpack_writer_t writer;

		param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 6, 0,
				 PARAM_QUEUE_TYPE_SET, request == PARAM_PUSH_REQUEST ? 1 : 2);
		mpack_writer_init(&writer, queue.buffer, queue.buffer_size);
		param_serialize_id(&writer, &led, -1, &queue);
		if (tail == 8) {
			mpack_write_cstr(&writer, "wrong");
		} else {
			mpack_write_u32(&writer, 256);
		}
		packet->length = mpack_writer_buffer_used(&writer) + 2;
		zassert_equal(mpack_writer_destroy(&writer), mpack_ok);
	}
	if (tail == 10) {
		packet->length = 1;
	}
	if (tail == 5) {
		packet->length--;
	}
	if (request == PARAM_PUSH_REQUEST_V2_HWID) {
		uint32_t hwid = tail == 6 ? 98 : tail == 11 ? 1234 : 99;

		memcpy(&packet->data[packet->length], &hwid, sizeof(hwid));
		packet->length += sizeof(hwid);
	}
	send_encoded(packet, accepted && !(flags & PARAM_FLAG_NOACK));
	zassert_equal(changes, before, "software callback ran in LED request");
}

static void check_v1_truncated_led_id_is_refused(void)
{
	BUILD_ASSERT((KFSW_PARAM_ID(67, 8) & 0x1FF) == KFSW_PARAM_ID(1, 8));
	zassert_is_null(kfsw_param_find_id(KFSW_PARAM_ID(1, 8)));
	check_led_push(PARAM_PUSH_REQUEST, 0, 0, false);
	zassert_equal(atomic_get(&gpio_calls), 0, "truncated v1 ID drove GPIO");
}

static void check_leds(void)
{
	const uint8_t requests[] = {PARAM_PUSH_REQUEST_V2, PARAM_PUSH_REQUEST_V2_HWID};
	struct kfsw_param_value value = {.type = KFSW_PARAM_U8, .size = 1, .scalar.u8 = 1};
	const struct kfsw_param_entry *entry = kfsw_param_find_name("led_green");
	struct kfsw_boton_test_status status;

	zassert_ok(kfsw_boton_test_init());
	atomic_clear(&gpio_calls);
	/* First assertion must expose GPIO mutation on the parent sources. */
	check_led_push(PARAM_PUSH_REQUEST_V2, 0, 1, false);
	zassert_equal(atomic_get(&gpio_calls), 0, "rejected batch drove GPIO");
	check_v1_truncated_led_id_is_refused();
	zassert_ok(kfsw_param_validate_entry(entry, &value));
	zassert_equal(atomic_get(&gpio_calls), 0, "validation drove GPIO");
	for (size_t i = 0; i < ARRAY_SIZE(requests); i++) {
		for (int tail = 1; tail <= 10; tail++) {
			if (tail == 6 && requests[i] != PARAM_PUSH_REQUEST_V2_HWID) {
				continue;
			}
			check_led_push(requests[i], 0, tail, false);
			zassert_equal(atomic_get(&gpio_calls), 0);
			zassert_ok(kfsw_boton_test_get_status(&status));
			zassert_false(status.led_green);
			zassert_false(status.led_blue);
		}
		check_led_push(requests[i], 0, 0, true);
		zassert_equal(atomic_get(&gpio_calls), 1);
		value.scalar.u8 = 0;
		zassert_ok(kfsw_param_set("led_green", &value));
		zassert_equal(atomic_get(&gpio_calls), 2);
		atomic_clear(&gpio_calls);
		gpio_error = -EIO;
		check_led_push(requests[i], 0, 0, false);
		zassert_equal(atomic_get(&gpio_calls), 1);
		check_led_push(requests[i], PARAM_FLAG_PULLWITHACK, 0, false);
		check_led_push(requests[i], PARAM_FLAG_NOACK, 0, false);
		zassert_equal(atomic_get(&gpio_calls), 3);
		value.scalar.u8 = 1;
		zassert_equal(kfsw_param_set("led_green", &value), -EIO);
		zassert_equal(atomic_get(&gpio_calls), 4);
		zassert_ok(kfsw_param_read_stored_entry(entry, &value));
		zassert_equal(value.scalar.u8, 0);
		zassert_ok(kfsw_boton_test_get_status(&status));
		zassert_false(status.led_green);
		gpio_error = 0;
		atomic_clear(&gpio_calls);
	}
	check_led_push(PARAM_PUSH_REQUEST_V2_HWID, 0, 11, true);
	zassert_equal(atomic_get(&gpio_calls), 1);
	atomic_clear(&gpio_calls);
	check_led_push(PARAM_PUSH_REQUEST_V2, PARAM_FLAG_NOACK, 0, true);
	zassert_equal(atomic_get(&gpio_calls), 1);
	check_led_push(PARAM_PUSH_REQUEST_V2, PARAM_FLAG_PULLWITHACK, 0, true);
	zassert_equal(atomic_get(&gpio_calls), 2);
	check_led_push(PARAM_PULL_RESPONSE_V2, 0, 0, false);
	zassert_equal(atomic_get(&gpio_calls), 2, "response applied local GPIO");
	static uint16_t peer = 42;
	static uint8_t cached_value;
	static param_t cached = {.id = KFSW_PARAM_ID(67, 8),
				 .node = &peer,
				 .type = PARAM_TYPE_UINT8,
				 .array_size = 1,
				 .array_step = 1,
				 .addr = &cached_value};
	uint16_t implicit = 0;
	param_t wire = cached;

	zassert_ok(param_list_add(&cached));
	wire.node = &implicit;
	{
		csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
		param_queue_t queue = {0};
		uint8_t on = 1;

		zassert_not_null(packet);
		packet->data[0] = PARAM_PULL_RESPONSE_V2;
		packet->data[1] = PARAM_FLAG_END;
		param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0,
				 PARAM_QUEUE_TYPE_SET, 2);
		zassert_ok(param_queue_add(&queue, &wire, -1, &on));
		packet->length = queue.used + 2;
		send_encoded(packet, false);
		zassert_equal(cached_value, 1);
		cached_value = 0;
		zassert_equal(atomic_get(&gpio_calls), 2, "cached data applied GPIO");
	}
	const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));

	zassert_equal(gpio_emul_output_get(gpio, 2), 1);
}
#endif

enum push_case {
	TIMEOUT_ONLY,
	UNSIGNED_WIDE,
	SIGNED_WIDE,
	FLOAT_RANGE,
	FLOAT_NAN,
	FLOAT_VALID,
	TEXT_REFUSED,
	TEXT_LONG,
	TEXT_NUL,
	TEXT_VALID,
	DATA_REFUSED,
	DATA_SHORT,
	DATA_VALID,
	DATA_TRUNCATED,
	WRONG_TYPE,
	ARRAY_LONG,
	ARRAY_VALID,
};

static void check_encoded(enum push_case kind, uint8_t request, int offset, bool accepted)
{
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	mpack_writer_t writer;
	uint16_t node = 7;
	param_t param = {.id = KFSW_PARAM_ID(1, 0), .node = &node};
	int version = request == PARAM_PUSH_REQUEST ? 1 : 2;
	int before = changes;

	zassert_not_null(packet);
	packet->data[0] = request;
	packet->data[1] = 0;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 6, 0, PARAM_QUEUE_TYPE_SET,
			 version);
	mpack_writer_init(&writer, queue.buffer, queue.buffer_size);
	param_serialize_id(&writer, &param, kind == TIMEOUT_ONLY ? offset : -1, &queue);
	mpack_write_u32(&writer, 9000);
	if (kind != TIMEOUT_ONLY) {
		param.id = KFSW_PARAM_ID(1, kind <= SIGNED_WIDE      ? kind
					    : kind <= FLOAT_VALID    ? 3
					    : kind <= TEXT_VALID     ? 4
					    : kind <= DATA_TRUNCATED ? 5
								     : 0);
		param_serialize_id(&writer, &param, offset, &queue);
		switch (kind) {
		case UNSIGNED_WIDE:
			mpack_write_u32(&writer, 256);
			break;
		case SIGNED_WIDE:
			mpack_write_i32(&writer, -129);
			break;
		case FLOAT_RANGE:
			mpack_write_float(&writer, 11.0f);
			break;
		case FLOAT_NAN:
			mpack_write_float(&writer, NAN);
			break;
		case FLOAT_VALID:
			mpack_write_float(&writer, 1.5f);
			break;
		case TEXT_REFUSED:
			mpack_write_cstr(&writer, "bad");
			break;
		case TEXT_LONG:
			mpack_write_cstr(&writer, "12345678");
			break;
		case TEXT_NUL:
			mpack_write_str(&writer, "a\0b", 3);
			break;
		case TEXT_VALID:
			mpack_write_cstr(&writer, "good");
			break;
		case DATA_REFUSED:
			mpack_write_bin(&writer, "\x63\x02\x03\x04", 4);
			break;
		case DATA_SHORT:
			mpack_write_bin(&writer, "\x01\x02\x03", 3);
			break;
		case DATA_VALID:
		case DATA_TRUNCATED:
			mpack_write_bin(&writer, "\x01\x02\x03\x04", 4);
			break;
		case WRONG_TYPE:
			mpack_write_cstr(&writer, "wrong type");
			break;
		case ARRAY_LONG:
			mpack_start_array(&writer, 2);
			mpack_write_u32(&writer, 9000);
			mpack_write_u32(&writer, 9000);
			mpack_finish_array(&writer);
			break;
		case ARRAY_VALID:
			mpack_start_array(&writer, 1);
			mpack_write_u32(&writer, 9000);
			mpack_finish_array(&writer);
			break;
		default:
			zassert_unreachable();
		}
	}
	packet->length = mpack_writer_buffer_used(&writer) + 2;
	zassert_equal(mpack_writer_destroy(&writer), mpack_ok);
	if (kind == DATA_TRUNCATED) {
		packet->length--;
	}
	if (request == PARAM_PUSH_REQUEST_V2_HWID) {
		uint32_t hwid = 1234;
		memcpy(&packet->data[packet->length], &hwid, sizeof(hwid));
		packet->length += sizeof(hwid);
	}
	send_encoded(packet, accepted);
	zassert_equal(writable, accepted ? 9000 : 8000);
	zassert_equal(changes, before + (accepted ? (kind == TIMEOUT_ONLY ? 1 : 2) : 0));
	if (accepted) {
		push_timeout(8000, true);
	}
}

static int receive_reply(csp_iface_t *iface, uint16_t via, csp_packet_t *packet, int from_me)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(via);
	ARG_UNUSED(from_me);
	if (packet->id.sport == CSP_PING) {
		k_sem_give(&ping_reply);
	} else if (packet->id.sport == CONFIG_KFSW_PARAM_LIST_PORT) {
		if (packet->data[0] == 4) {
			listed_status = packet->data[1];
			listed_total = sys_get_be16(&packet->data[4]);
			listed_crc = sys_get_be32(&packet->data[6]);
			if (listed_status == 0) {
				listed_id = sys_get_be16(&packet->data[10]);
			}
		} else {
			if (sys_get_be16(packet->data) != KFSW_PARAM_ID(20, 0)) {
				csp_buffer_free(packet);
				return CSP_ERR_NONE;
			}
			listed_id = sys_get_be16(packet->data);
		}
		k_sem_give(&list_reply);
	} else if (packet->id.sport == CONFIG_KFSW_PARAM_PORT) {
		k_sem_give(&value_reply);
	}
	csp_buffer_free(packet);
	return CSP_ERR_NONE;
}

static csp_iface_t link = {.name = "PEER", .addr = 7, .netmask = 14, .nexthop = receive_reply};

static void inject(uint8_t port)
{
	csp_packet_t *packet = csp_buffer_get(8);

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = 20,
				.dport = port,
				.pri = CSP_PRIO_NORM,
				.flags = CSP_FCRC32};
	packet->length = 2;
	/* libparam v2 PULL with an empty identifier queue still returns END. */
	packet->data[0] = PARAM_PULL_REQUEST_V2;
	packet->data[1] = 0;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
}

static void inject_list(bool indexed, uint16_t index, uint32_t crc)
{
	static uint8_t next_port = 21;
	csp_packet_t *packet = csp_buffer_get(16);

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = next_port++,
				.dport = CONFIG_KFSW_PARAM_LIST_PORT,
				.pri = CSP_PRIO_NORM,
				.flags = CSP_FCRC32};
	packet->data[0] = indexed ? 4 : 3;
	sys_put_be16(index, &packet->data[1]);
	sys_put_be32(crc, &packet->data[3]);
	packet->length = indexed ? 7 : 1;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
}

ZTEST(services_param_worker, test_blocked_sampling_keeps_router_running_and_queue_bounded)
{
	static const struct kfsw_param_definition definition = {
		.offset = 0,
		.type = KFSW_PARAM_U32,
		.flags = IS_ENABLED(KFSW_TEST_LOCAL_ONLY) ? KFSW_PARAM_FLAG_LOCAL_ONLY
							  : KFSW_PARAM_FLAG_READ_ONLY,
		.name = "slow",
		.value = &backing,
		.sample = sample,
	};
	static const struct kfsw_param_definition_set set = {
		.table = 20,
		.name = "test",
		.definitions = &definition,
		.count = 1,
	};
	const struct kfsw_param_definition_set *sets[] = {
		&set,
		&write_set,
#if CONFIG_KFSW_BOTON_TEST
		&kfsw_boton_test_param_definitions,
#endif
	};
	struct kfsw_param_stats stats;
	int free_before;

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
#if CONFIG_KFSW_BOTON_TEST
	zassert_equal(atomic_get(&gpio_calls), 0, "initialization drove GPIO");
#endif
	zassert_ok(kfsw_csp_init());
	csp_iflist_add(&link);
	zassert_ok(kfsw_csp_route_table_apply("0/0 PEER"));
	static csp_socket_t occupied;

	zassert_ok(csp_listen(&occupied, 1));
	zassert_ok(csp_bind(&occupied, CONFIG_KFSW_PARAM_LIST_PORT));
	zassert_equal(kfsw_param_server_start(), -EADDRINUSE);
	zassert_ok(csp_socket_close(&occupied));
	zassert_ok(kfsw_param_server_start());
	zassert_ok(kfsw_csp_start());
	free_before = csp_buffer_remaining();
	atomic_set(&block_sample, 1);
	inject(CONFIG_KFSW_PARAM_PORT);
	zassert_ok(k_sem_take(&sample_entered, K_SECONDS(1)));
	for (size_t i = 0; i < CONFIG_KFSW_PARAM_VALUE_QUEUE_DEPTH + 3U; i++) {
		inject(CONFIG_KFSW_PARAM_PORT);
	}
	inject(CSP_PING);
	zassert_ok(k_sem_take(&ping_reply, K_MSEC(100)));
	zassert_equal(kfsw_param_csp_dropped_requests(), 3);
	k_sem_give(&sample_release);
	for (size_t i = 0; i < CONFIG_KFSW_PARAM_VALUE_QUEUE_DEPTH + 1U; i++) {
		zassert_ok(k_sem_take(&value_reply, K_SECONDS(1)));
	}
	k_sleep(K_MSEC(2));
	zassert_equal(csp_buffer_remaining(), free_before);
	zassert_ok(kfsw_param_get_stats(&stats));
	zassert_equal(stats.requests_dropped, 3);

	if (IS_ENABLED(KFSW_TEST_LOCAL_ONLY)) {
		struct kfsw_param_value local_value = {
			.type = KFSW_PARAM_U32,
			.size = sizeof(uint32_t),
			.scalar.u32 = 17,
		};
		zassert_ok(kfsw_param_set("slow", &local_value));
		zassert_equal(backing, 17);
		local_value.scalar.u32 = 42;
		zassert_ok(kfsw_param_set("slow", &local_value));
	}
	/* Remote PUSH must respect read-only and local-only values. */
	csp_packet_t *push = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t local = 0;
	uint32_t replacement = 99;
	param_t parameter = {.id = KFSW_PARAM_ID(20, 0),
			     .node = &local,
			     .type = PARAM_TYPE_UINT32,
			     .array_size = 1,
			     .array_step = 4};

	zassert_not_null(push);
	push->id = (csp_id_t){.src = 42,
			      .dst = 7,
			      .sport = 20,
			      .dport = CONFIG_KFSW_PARAM_PORT,
			      .pri = CSP_PRIO_NORM,
			      .flags = CSP_FCRC32};
	push->data[0] = PARAM_PUSH_REQUEST_V2;
	push->data[1] = 0;
	param_queue_init(&queue, &push->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	zassert_ok(param_queue_add(&queue, &parameter, -1, &replacement));
	push->length = queue.used + 2;
	zassert_ok(csp_crc32_append(push));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, push, 0));
	zassert_not_equal(k_sem_take(&value_reply, K_MSEC(50)), 0);
	zassert_equal(backing, 42);
	zassert_equal(csp_buffer_remaining(), free_before);
	atomic_set(&fail_list_allocation, 1);
	inject_list(false, 0, 0);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(atomic_get(&fail_list_allocation), 0);
	zassert_equal(listed_id, KFSW_PARAM_ID(20, 0));
	inject_list(true, 0, 0);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(listed_status, 0);
	zassert_equal(listed_total,
		      (IS_ENABLED(CONFIG_KFSW_BOTON_TEST) ? 12 : 7) + IS_ENABLED(KFSW_TEST_APPLY));
	zassert_equal(listed_id, KFSW_PARAM_ID(1, 0));
	uint32_t fingerprint = listed_crc;

	inject_list(true, 6 + IS_ENABLED(KFSW_TEST_APPLY), fingerprint);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(listed_status, 0);
	zassert_equal(listed_id, KFSW_PARAM_ID(20, 0));
	inject_list(true,
		    (IS_ENABLED(CONFIG_KFSW_BOTON_TEST) ? 12 : 7) + IS_ENABLED(KFSW_TEST_APPLY),
		    fingerprint);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(listed_status, 1);
	zassert_equal(listed_total,
		      (IS_ENABLED(CONFIG_KFSW_BOTON_TEST) ? 12 : 7) + IS_ENABLED(KFSW_TEST_APPLY));
	zassert_equal(listed_crc, fingerprint);
	push_timeout(8000, true);
	zassert_equal(writable, 8000);
	int before = changes;
	push_timeout(7199, false);
	zassert_equal(writable, 8000);
	zassert_equal(changes, before);
	const uint8_t requests[] = {PARAM_PUSH_REQUEST, PARAM_PUSH_REQUEST_V2,
				    PARAM_PUSH_REQUEST_V2_HWID};
	for (size_t i = 0; i < ARRAY_SIZE(requests); i++) {
		check_encoded(TIMEOUT_ONLY, requests[i], -1, true);
		for (enum push_case kind = UNSIGNED_WIDE; kind <= ARRAY_VALID; kind++) {
			check_encoded(kind, requests[i], -1,
				      kind == FLOAT_VALID || kind == TEXT_VALID ||
					      kind == DATA_VALID || kind == ARRAY_VALID);
		}
		check_encoded(TIMEOUT_ONLY, requests[i], 0, true);
		check_encoded(TEXT_VALID, requests[i], 1, false);
		check_encoded(DATA_VALID, requests[i], 1, false);
	}
#if KFSW_TEST_APPLY
	check_v1_apply();
#endif
	check_cached_response();
#if CONFIG_KFSW_BOTON_TEST
	check_leds();
#endif
	zassert_equal(real, 1.5f);
	zassert_str_equal(text, "good");
	const uint8_t expected[] = {1, 2, 3, 4};
	zassert_mem_equal(bytes, expected, sizeof(bytes));
	inject(CSP_PING);
	zassert_ok(k_sem_take(&ping_reply, K_SECONDS(1)));
	k_sleep(K_MSEC(5));
	zassert_equal(csp_buffer_remaining(), free_before);
}

ZTEST_SUITE(services_param_worker, NULL, NULL, NULL, NULL, NULL);
