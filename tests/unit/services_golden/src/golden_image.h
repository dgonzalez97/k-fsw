#ifndef KFSW_TESTS_GOLDEN_IMAGE_H
#define KFSW_TESTS_GOLDEN_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bootutil/image.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define GOLDEN_IMAGE_SHA256_SIZE 32U
#define GOLDEN_IMAGE_TLV_SIZE                                                                      \
	(sizeof(struct image_tlv_info) + sizeof(struct image_tlv) + GOLDEN_IMAGE_SHA256_SIZE)

/* The layout of a signed image without its signature: header, body and the
 * SHA-256 TLV the installer checks, filling the whole buffer.
 */
static inline void golden_image_build(uint8_t *image, size_t size, uint8_t fill)
{
	const size_t hashed = size - GOLDEN_IMAGE_TLV_SIZE;
	const struct image_header header = {
		.ih_magic = sys_cpu_to_le32(IMAGE_MAGIC),
		.ih_hdr_size = sys_cpu_to_le16(IMAGE_HEADER_SIZE),
		.ih_img_size = sys_cpu_to_le32(hashed - IMAGE_HEADER_SIZE),
	};
	const struct image_tlv_info info = {
		.it_magic = sys_cpu_to_le16(IMAGE_TLV_INFO_MAGIC),
		.it_tlv_tot = sys_cpu_to_le16(GOLDEN_IMAGE_TLV_SIZE),
	};
	const struct image_tlv tlv = {
		.it_type = sys_cpu_to_le16(IMAGE_TLV_SHA256),
		.it_len = sys_cpu_to_le16(GOLDEN_IMAGE_SHA256_SIZE),
	};
	size_t length;

	for (size_t i = 0U; i < hashed; i++) {
		image[i] = (uint8_t)(fill + i * 7U);
	}
	memcpy(image, &header, sizeof(header));
	memcpy(&image[hashed], &info, sizeof(info));
	memcpy(&image[hashed + sizeof(info)], &tlv, sizeof(tlv));
	zassert_equal(psa_crypto_init(), PSA_SUCCESS);
	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, image, hashed,
				       &image[hashed + sizeof(info) + sizeof(tlv)],
				       GOLDEN_IMAGE_SHA256_SIZE, &length),
		      PSA_SUCCESS);
}

#endif
