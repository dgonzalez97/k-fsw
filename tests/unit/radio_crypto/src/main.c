#include <errno.h>
#include <string.h>
#include <psa/crypto.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <kfsw/comms/uart_codec.h>
#include <kfsw/modules/radio_uhf.h>
#include "radio_crypto_internal.h"

#define MAC_ALG PSA_ALG_HMAC(PSA_ALG_SHA_256)
static const struct kfsw_uart_codec *codec;
static struct radio_crypto_settings stored;
static csp_packet_t controls[16];
static size_t control_count;
static uint8_t random_byte;
static int save_error, random_error;
static psa_key_id_t test_master;

int __wrap_kfsw_uart_codec_register(const char *name, const struct kfsw_uart_codec *value)
{
	zassert_equal(strcmp(name, "KISS"), 0);
	codec = value;
	return 0;
}

int __wrap_kfsw_uart_codec_control(uint16_t peer, const uint8_t *data, size_t size)
{
	zassert_equal(peer, 16);
	zassert_true(control_count < ARRAY_SIZE(controls));
	csp_packet_t *packet = &controls[control_count++];
	memset(packet, 0, sizeof(*packet));
	packet->id = (csp_id_t){.src = 2, .dst = 16};
	packet->length = (uint16_t)size;
	memcpy(packet->data, data, size);
	return 0;
}

int __wrap_radio_crypto_store_load(struct radio_crypto_settings *settings)
{
	*settings = stored;
	return 0;
}

int __wrap_radio_crypto_store_save(const struct radio_crypto_settings *settings)
{
	if (save_error == 0) {
		stored = *settings;
	}
	return save_error;
}

int __wrap_kfsw_radio_host_random(void *data, size_t size)
{
	if (random_error) {
		return -1;
	}
	uint8_t *bytes = data;
	for (size_t i = 0; i < size; i++) {
		bytes[i] = random_byte++;
	}
	return 0;
}

static void aad_header(uint8_t *data, const csp_id_t *id)
{
	sys_put_be16(id->src, data);
	sys_put_be16(id->dst, data + 2);
	data[4] = id->sport;
	data[5] = id->dport;
	data[6] = id->pri;
	data[7] = id->flags;
}

static void sign(csp_packet_t *packet)
{
	uint8_t input[92];
	size_t size;
	aad_header(input, &packet->id);
	memcpy(input + 8, packet->data, packet->length - 32);
	zassert_equal(psa_mac_compute(test_master, MAC_ALG, input, packet->length - 24,
				      packet->data + packet->length - 32, 32, &size),
		      PSA_SUCCESS);
	zassert_equal(size, 32);
}

static csp_packet_t hello(uint8_t identity)
{
	csp_packet_t packet = {.id = {.src = 16, .dst = 2}, .length = 68};
	memcpy(packet.data, "KR\x01\x01", 4);
	memset(packet.data + 4, identity, 16);
	memset(packet.data + 20, identity + 1, 16);
	sign(&packet);
	return packet;
}

static csp_packet_t *last_control(uint8_t type)
{
	for (size_t i = control_count; i != 0; i--) {
		if (controls[i - 1].data[3] == type) {
			return &controls[i - 1];
		}
	}
	zassert_unreachable("Missing control frame");
	return NULL;
}

static psa_key_id_t peer_session(const csp_packet_t *reply, bool ground_transmits)
{
	static const uint8_t label[] = "KFSW UHF session v1";
	uint8_t input[sizeof(label) + 52], material[32];
	size_t size;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key;
	memcpy(input, label, sizeof(label));
	sys_put_be16(ground_transmits ? 16 : 2, input + sizeof(label));
	sys_put_be16(ground_transmits ? 2 : 16, input + sizeof(label) + 2);
	memcpy(input + sizeof(label) + 4, reply->data + 4, 48);
	zassert_equal(
		psa_mac_compute(test_master, MAC_ALG, input, sizeof(input), material, 32, &size),
		PSA_SUCCESS);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, 256);
	psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	zassert_equal(psa_import_key(&attributes, material, 32, &key), PSA_SUCCESS);
	psa_reset_key_attributes(&attributes);
	return key;
}

static csp_packet_t protected_packet(const csp_packet_t *reply, uint64_t sequence)
{
	static const uint8_t text[] = "command payload";
	csp_packet_t packet = {.id = {.src = 16, .dst = 2, .sport = 21, .dport = 11}};
	uint8_t nonce[12], aad[20];
	size_t written;
	psa_key_id_t key = peer_session(reply, true);
	memcpy(packet.data, "KR\x01\x00", 4);
	sys_put_be64(sequence, packet.data + 4);
	sys_put_be32(16, nonce);
	sys_put_be64(sequence, nonce + 4);
	aad_header(aad, &packet.id);
	memcpy(aad + 8, packet.data, 12);
	zassert_equal(psa_aead_encrypt(key, PSA_ALG_GCM, nonce, 12, aad, 20, text, sizeof(text),
				       packet.data + 12, sizeof(packet.data) - 12, &written),
		      PSA_SUCCESS);
	packet.length = (uint16_t)(written + 12);
	psa_destroy_key(key);
	return packet;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	static bool parameters_ready;
	if (!parameters_ready) {
		const struct kfsw_param_definition_set *sets[] = {
			&kfsw_radio_uhf_param_definitions};
		zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
		parameters_ready = true;
	}
	stored = (struct radio_crypto_settings){
		.key_set = true, .enabled = true, .tx = true, .rx = true};
	for (size_t i = 0; i < sizeof(stored.key); i++) {
		stored.key[i] = (uint8_t)i;
	}
	control_count = 0;
	random_byte = 0;
	random_error = save_error = 0;
	zassert_ok(kfsw_radio_uhf_crypto_init());
	psa_destroy_key(test_master);
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attributes, 256);
	psa_set_key_algorithm(&attributes, MAC_ALG);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	zassert_equal(psa_import_key(&attributes, stored.key, 32, &test_master), PSA_SUCCESS);
	psa_reset_key_attributes(&attributes);
}

ZTEST(radio_crypto, test_plaintext_and_unauthenticated_handshake_are_refused)
{
	csp_packet_t packet = {.length = 12};
	zassert_true(codec->decode(&packet) < 0);
	packet = hello(4);
	packet.data[50] ^= 1;
	zassert_true(codec->decode(&packet) < 0);
	struct kfsw_radio_crypto_info info;
	kfsw_radio_uhf_crypto_get(&info);
	zassert_false(info.rx_ready);
}

ZTEST(radio_crypto, test_payload_header_tag_and_duplicates)
{
	csp_packet_t request = hello(4);
	zassert_equal(codec->decode(&request), 1);
	csp_packet_t reply = *last_control(2);
	csp_packet_t original = protected_packet(&reply, 1);
	csp_packet_t changed = original;
	changed.id.dport ^= 1;
	zassert_true(codec->decode(&changed) < 0);
	changed = original;
	changed.data[15] ^= 1;
	zassert_true(codec->decode(&changed) < 0);
	changed = original;
	changed.data[changed.length - 1] ^= 1;
	zassert_true(codec->decode(&changed) < 0);
	changed = original;
	zassert_ok(codec->decode(&changed));
	zassert_mem_equal(changed.data, "command payload", sizeof("command payload"));
	zassert_equal(codec->decode(&original), -EALREADY);
	/* Retried HELLO must not clear the replay counter. */
	zassert_equal(codec->decode(&request), 1);
	original = protected_packet(&reply, 1);
	zassert_equal(codec->decode(&original), -EALREADY);
}

ZTEST(radio_crypto, test_both_directions_and_replayed_reply)
{
	csp_packet_t request = hello(8);
	zassert_equal(codec->decode(&request), 1);
	csp_packet_t reply = *last_control(1);
	reply.id = (csp_id_t){.src = 16, .dst = 2};
	reply.data[3] = 2;
	reply.length = 84;
	memset(reply.data + 36, 0xac, 16);
	sign(&reply);
	zassert_equal(codec->decode(&reply), 1);
	zassert_true(codec->decode(&reply) < 0);
	csp_packet_t packet = {.id = {.src = 2, .dst = 16, .dport = 10}, .length = 4};
	memcpy(packet.data, "test", 4);
	zassert_ok(codec->encode(&packet));
	uint8_t nonce[12], aad[20], plain[32];
	size_t size;
	psa_key_id_t key = peer_session(&reply, false);
	sys_put_be32(2, nonce);
	memcpy(nonce + 4, packet.data + 4, 8);
	aad_header(aad, &packet.id);
	memcpy(aad + 8, packet.data, 12);
	zassert_equal(psa_aead_decrypt(key, PSA_ALG_GCM, nonce, 12, aad, 20, packet.data + 12,
				       packet.length - 12, plain, sizeof(plain), &size),
		      PSA_SUCCESS);
	zassert_equal(size, 4);
	zassert_mem_equal(plain, "test", 4);
	psa_destroy_key(key);
	packet.length = CSP_BUFFER_SIZE;
	zassert_equal(codec->encode(&packet), -EMSGSIZE);
}

ZTEST(radio_crypto, test_replay_after_receiver_restart)
{
	csp_packet_t request = hello(7);
	zassert_equal(codec->decode(&request), 1);
	csp_packet_t old = protected_packet(last_control(2), 1);
	zassert_ok(kfsw_radio_uhf_crypto_init());
	zassert_true(codec->decode(&old) < 0);
	/* Replaying the old HELLO gets a fresh receiver challenge. */
	zassert_equal(codec->decode(&request), 1);
	zassert_true(codec->decode(&old) < 0);
	csp_packet_t fresh = protected_packet(last_control(2), 1);
	zassert_ok(codec->decode(&fresh));
}

ZTEST(radio_crypto, test_key_redaction_and_storage_failure)
{
	struct kfsw_param_value value = {.type = KFSW_PARAM_STRING};
	strcpy(value.text, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	value.size = strlen(value.text) + 1;
	zassert_ok(kfsw_param_set("uhf_key_hex", &value));
	zassert_ok(kfsw_param_get("uhf_key_hex", &value));
	zassert_equal(value.text[0], '\0');
	zassert_true(radio_crypto_validate_key("short") < 0);
	zassert_true(radio_crypto_validate_key(
			     "z00102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f") <
		     0);
	save_error = -ENOSPC;
	union kfsw_param_scalar disabled = {.u8 = 0};
	radio_crypto_set_enable(&disabled);
	struct kfsw_radio_crypto_info info;
	kfsw_radio_uhf_crypto_get(&info);
	zassert_true(info.enabled);
	zassert_equal(info.last_error, -ENOSPC);
}

ZTEST(radio_crypto, test_disable_and_one_way_policy)
{
	union kfsw_param_scalar disabled = {.u8 = 0};
	radio_crypto_set_tx(&disabled);
	csp_packet_t packet = {.length = 4};
	memcpy(packet.data, "test", 4);
	zassert_ok(codec->encode(&packet));
	zassert_mem_equal(packet.data, "test", 4);
	zassert_true(codec->decode(&packet) < 0);
	radio_crypto_set_enable(&disabled);
	zassert_ok(codec->decode(&packet));
}

ZTEST(radio_crypto, test_entropy_failure_does_not_start_session)
{
	random_error = 1;
	zassert_true(kfsw_radio_uhf_crypto_connect() < 0);
	struct kfsw_radio_crypto_info info;
	kfsw_radio_uhf_crypto_get(&info);
	zassert_false(info.tx_ready);
	zassert_equal(control_count, 0);
}

ZTEST_SUITE(radio_crypto, NULL, NULL, before, NULL, NULL);
