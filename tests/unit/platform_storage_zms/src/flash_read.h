#include <zephyr/drivers/flash.h>

int storage_zms_test_flash_read(const struct device *dev, off_t offset, void *dst, size_t len);

/* Only the ZMS translation unit substitutes its mounted reads. */
#define flash_read storage_zms_test_flash_read
