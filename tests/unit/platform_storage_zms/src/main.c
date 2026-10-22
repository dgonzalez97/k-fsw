#include <errno.h>
#include <string.h>

#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kvss/zms.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include "storage_zms_disk.h"
#include "flash_read.h"
#undef flash_read

#define NAME KFSW_STORAGE_ZMS_DISK_NAME
#define SIZE KFSW_STORAGE_ZMS_SECTOR_SIZE
#define COUNT CONFIG_KFSW_STORAGE_ZMS_SECTOR_COUNT
#define PHYSICAL_SIZE 0x40000U
#define PARTITION DT_CHOSEN(kfsw_storage_partition)

static const struct flash_area *area;
static uint8_t *memory;
static uint8_t snapshot[2U * PHYSICAL_SIZE];
static uint8_t data[2U * SIZE];
static uint8_t readback[2U * SIZE];
static int64_t fail_after = -1;
static size_t operations;
static size_t erases;
static bool fail_read;
static bool fail_close_once;
static size_t fail_zms_read_length;
static off_t fail_zms_read_offset = -1;
static size_t failed_zms_reads;

static bool tracing;
static struct {
	size_t start;
	size_t length;
	off_t offset;
	bool erase;
} trace[512];
static size_t trace_count;
static bool cuts[4U * PHYSICAL_SIZE];

int __real_flash_area_read(const struct flash_area *fa, off_t off, void *dst, size_t len);

int __wrap_flash_area_read(const struct flash_area *fa, off_t off, void *dst, size_t len)
{
	return fail_read ? -EIO : __real_flash_area_read(fa, off, dst, len);
}

int storage_zms_test_flash_read(const struct device *dev, off_t offset, void *dst, size_t len)
{
	if (fail_zms_read_length == len &&
	    (fail_zms_read_offset < 0 || fail_zms_read_offset == offset)) {
		failed_zms_reads++;
		return -EIO;
	}
	return flash_read(dev, offset, dst, len);
}

static void trace_write(off_t offset)
{
	if (!tracing) {
		return;
	}
	if (trace_count == 0U || trace[trace_count - 1U].erase ||
	    offset != trace[trace_count - 1U].offset + trace[trace_count - 1U].length) {
		zassert_true(trace_count < ARRAY_SIZE(trace), "trace overflow at %zu", operations);
		trace[trace_count].start = operations;
		trace[trace_count].length = 0U;
		trace[trace_count].offset = offset;
		trace[trace_count++].erase = false;
	}
	trace[trace_count - 1U].length++;
}

static int write_byte(const struct device *device, off_t offset, uint8_t value)
{
	ARG_UNUSED(device);
	zassert_true(offset >= area->fa_off && offset < area->fa_off + area->fa_size);
	if (fail_close_once && offset == area->fa_off + PHYSICAL_SIZE - 32U) {
		fail_close_once = false;
		return -EIO;
	}
	if (fail_after == 0) {
		return -EIO;
	}
	if (fail_after > 0) {
		fail_after--;
	}
	trace_write(offset);
	operations++;
	return value;
}

static int erase_unit(const struct device *device, off_t offset)
{
	ARG_UNUSED(device);
	zassert_true(offset >= area->fa_off &&
		     offset + PHYSICAL_SIZE <= area->fa_off + area->fa_size);
	erases++;
	if (tracing) {
		zassert_true(trace_count < ARRAY_SIZE(trace), "trace overflow at %zu", operations);
		trace[trace_count].start = operations;
		trace[trace_count].length = PHYSICAL_SIZE;
		trace[trace_count].offset = offset;
		trace[trace_count++].erase = true;
	}
	/* Model an interrupted erase at any byte, including damaged headers. */
	for (size_t index = 0U; index < PHYSICAL_SIZE; index++) {
		if (fail_after == 0) {
			return -EIO;
		}
		if (fail_after > 0) {
			fail_after--;
		}
		memory[offset + index] = 0xff;
		operations++;
	}
	return 0;
}

static const struct flash_simulator_cb callbacks = {
	.write_byte = write_byte,
	.erase_unit = erase_unit,
};

static void deinitialize(void)
{
	bool force = true;

	zassert_ok(disk_access_ioctl(NAME, DISK_IOCTL_CTRL_DEINIT, &force));
}

static void remount(void)
{
	deinitialize();
	zassert_ok(disk_access_init(NAME));
}

static void reset_media(void)
{
	fail_after = -1;
	fail_read = false;
	fail_close_once = false;
	fail_zms_read_length = 0U;
	fail_zms_read_offset = -1;
	failed_zms_reads = 0U;
	tracing = false;
	zassert_ok(kfsw_storage_unmount());
	deinitialize();
	memset(memory + area->fa_off, 0xff, area->fa_size);
	operations = 0U;
	erases = 0U;
}

static void *setup(void)
{
	size_t size;

	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(PARTITION), &area));
	memory = flash_simulator_get_memory(area->fa_dev, &size);
	zassert_not_null(memory);
	zassert_true(size >= area->fa_off + area->fa_size);
	flash_simulator_set_callbacks(area->fa_dev, &callbacks);
	return NULL;
}

ZTEST(storage_zms, test_blank_bounds_same_value_and_remount)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	zassert_ok(disk_access_read(NAME, readback, COUNT - 1U, 1));
	for (size_t index = 0; index < SIZE; index++) {
		zassert_equal(readback[index], 0xff);
	}
	memset(data, 0x5a, sizeof(data));
	zassert_equal(disk_access_write(NAME, data, COUNT - 1U, 2), -EINVAL);
	zassert_equal(disk_access_write(NAME, data, UINT32_MAX, 2), -EINVAL);
	zassert_equal(disk_access_write(NAME, NULL, 0, 1), -EINVAL);
	zassert_equal(disk_access_write(NAME, data, 0, 0), -EINVAL);
	zassert_ok(disk_access_write(NAME, data, 0, 2));
	size_t before = operations;

	zassert_ok(disk_access_write(NAME, data, 0, 2));
	zassert_equal(operations, before);
	remount();
	zassert_ok(disk_access_read(NAME, readback, 0, 2));
	zassert_mem_equal(data, readback, sizeof(data));
}

ZTEST(storage_zms, test_unknown_and_destroyed_headers_preserved)
{
	reset_media();
	memory[area->fa_off + 77U] = 0x17;
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(disk_access_init(NAME), -EFAULT);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));

	reset_media();
	zassert_ok(disk_access_init(NAME));
	memset(data, 0x66, SIZE);
	zassert_ok(disk_access_write(NAME, data, 0, 1));
	deinitialize();
	for (size_t sector = 0; sector < 2; sector++) {
		memset(memory + area->fa_off + (sector + 1) * PHYSICAL_SIZE - 16, 0, 16);
	}
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(disk_access_init(NAME), -EFAULT);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
}

ZTEST(storage_zms, test_unsupported_version_preserved)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	deinitialize();
	uint8_t *header = memory + area->fa_off + PHYSICAL_SIZE - 16U;

	/* Initialization may choose either sector. */
	if (sys_get_le32(header + 12) != 0x4201) {
		header += PHYSICAL_SIZE;
	}
	sys_put_le32(0x4202, header + 12);
	header[0] = crc8_ccitt(0xff, header + 1, 15);
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(disk_access_init(NAME), -EPROTONOSUPPORT);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
}

ZTEST(storage_zms, test_lengths_and_crc)
{
	struct zms_fs raw = {
		.flash_device = area->fa_dev,
		.offset = area->fa_off,
		.sector_size = PHYSICAL_SIZE,
		.sector_count = 2,
	};

	reset_media();
	zassert_ok(zms_mount(&raw));
	memset(data, 0x55, sizeof(data));
	zassert_equal(zms_write(&raw, 1, data, SIZE - 1), SIZE - 1);
	zassert_equal(zms_write(&raw, 2, data, SIZE + 1), SIZE + 1);
	zassert_ok(disk_access_init(NAME));
	zassert_equal(disk_access_read(NAME, readback, 0, 1), -EIO);
	zassert_equal(disk_access_read(NAME, readback, 1, 1), -EIO);

	reset_media();
	zassert_ok(disk_access_init(NAME));
	zassert_ok(disk_access_write(NAME, data, 0, 1));
	/* First large record begins at offset zero in the initialized sector. */
	memory[area->fa_off] ^= 1;
	zassert_equal(disk_access_read(NAME, readback, 0, 1), -EIO);
}

ZTEST(storage_zms, test_partial_request_failure)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	memset(data, 0x1a, sizeof(data));
	fail_after = SIZE + 16U + 5U;
	zassert_equal(disk_access_write(NAME, data, 0, 2), -EIO);
	fail_after = -1;
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(disk_access_read(NAME, readback, 0, 1), -EACCES);
	zassert_equal(disk_access_write(NAME, data, 0, 1), -EACCES);
	zassert_equal(disk_access_ioctl(NAME, DISK_IOCTL_CTRL_SYNC, NULL), -EACCES);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
	remount();
	zassert_ok(disk_access_ioctl(NAME, DISK_IOCTL_CTRL_SYNC, NULL));
	zassert_ok(disk_access_read(NAME, readback, 0, 1));
	zassert_mem_equal(data, readback, SIZE);
	zassert_ok(disk_access_read(NAME, readback, 1, 1));
	for (size_t index = 0; index < SIZE; index++) {
		zassert_equal(readback[index], 0xff);
	}
}

ZTEST(storage_zms, test_transient_close_failure_preserves_acknowledged_data)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	for (uint32_t index = 0U; index < 63U; index++) {
		memset(data, index, SIZE);
		zassert_ok(disk_access_write(NAME, data, index % COUNT, 1));
	}
	memset(data, 0xa5, SIZE);
	fail_close_once = true;
	int result = disk_access_write(NAME, data, 0, 1);

	zassert_false(fail_close_once, "Close-ATE failure was not injected");
	remount();
	uint32_t lost = 0U;

	for (uint32_t sector = 0U; sector < COUNT; sector++) {
		memset(readback + SIZE, sector < 15U ? sector + COUNT : sector, SIZE);
		int read_result = disk_access_read(NAME, readback, sector, 1);

		if (read_result != 0 || memcmp(readback, readback + SIZE, SIZE) != 0) {
			lost++;
		}
	}
	printk("Transient close failure: write result %d, lost acknowledged sectors %u\n", result,
	       lost);
	zassert_equal(result, -EIO);
	zassert_equal(lost, 0U);
}

ZTEST(storage_zms, test_full_live_set_repeated_gc)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	for (uint32_t cycle = 0U; cycle < 5U; cycle++) {
		for (uint32_t sector = 0U; sector < COUNT; sector++) {
			memset(data, cycle * COUNT + sector, SIZE);
			zassert_ok(disk_access_write(NAME, data, sector, 1));
		}
		remount();
		for (uint32_t sector = 0U; sector < COUNT; sector++) {
			memset(data, cycle * COUNT + sector, SIZE);
			zassert_ok(disk_access_read(NAME, readback, sector, 1));
			zassert_mem_equal(data, readback, SIZE);
		}
	}
	zassert_true(erases >= 5U, "Expected repeated compaction");
	printk("ZMS geometry: %u physical bytes, %u logical bytes; GC erases %zu\n",
	       2U * PHYSICAL_SIZE, COUNT * SIZE, erases);
}

static void sweep_replacement(bool gc)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	uint32_t writes = gc ? 63U : 1U;

	for (uint32_t index = 0U; index < writes; index++) {
		memset(data, index, SIZE);
		zassert_ok(disk_access_write(NAME, data, index % COUNT, 1));
	}
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	memset(data, 0xa5, SIZE);
	operations = 0U;
	trace_count = 0U;
	tracing = true;
	zassert_ok(disk_access_write(NAME, data, 0, 1));
	tracing = false;
	size_t total = operations;
	size_t tested = 0U;
	size_t metadata_bytes = 0U;

	zassert_true(total < ARRAY_SIZE(cuts));
	zassert_equal(total > PHYSICAL_SIZE, gc);
	memset(cuts, 0, sizeof(cuts));
	for (size_t index = 0U; index < trace_count; index++) {
		size_t start = trace[index].start;
		size_t length = trace[index].length;

		if (!trace[index].erase && length <= 32U) {
			metadata_bytes += length;
		}
		for (size_t byte = 0U; byte <= length; byte++) {
			if (!gc || length <= 32U || byte <= 1U || byte + 1U >= length ||
			    (trace[index].erase &&
			     (byte < 64U || byte + 64U >= length || byte % 4096U == 0U))) {
				cuts[start + byte] = true;
			}
		}
	}
	for (size_t cut = 0U; cut <= total; cut++) {
		if (!cuts[cut]) {
			continue;
		}
		tested++;
		deinitialize();
		memcpy(memory + area->fa_off, snapshot, sizeof(snapshot));
		zassert_ok(disk_access_init(NAME));
		fail_after = cut;
		int result = disk_access_write(NAME, data, 0, 1);

		fail_after = -1;
		remount();
		for (uint32_t sector = 0U; sector < (gc ? COUNT : 1U); sector++) {
			uint8_t expected = gc && sector < 15U ? sector + COUNT : sector;

			zassert_ok(disk_access_read(NAME, readback, sector, 1), "cut %zu", cut);
			if (sector == 0U && readback[0] == 0xa5) {
				expected = 0xa5;
			}
			memset(readback + SIZE, expected, SIZE);
			zassert_mem_equal(readback, readback + SIZE, SIZE, "cut %zu sector %u", cut,
					  sector);
		}
		if (result == 0) {
			zassert_ok(disk_access_read(NAME, readback, 0, 1));
			zassert_mem_equal(data, readback, SIZE);
		} else {
			zassert_equal(result, -EIO);
		}
	}
	printk("%s: %zu cuts, span 0..%zu; %zu traced runs, %zu metadata bytes\n",
	       gc ? "GC" : "Append", tested, total, trace_count, metadata_bytes);
}

ZTEST(storage_zms, test_interrupted_append)
{
	sweep_replacement(false);
}

ZTEST(storage_zms, test_interrupted_gc)
{
	sweep_replacement(true);
}

ZTEST(storage_zms, test_read_failure_preserves_media)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	memset(data, 0x37, SIZE);
	zassert_ok(disk_access_write(NAME, data, 0, 1));
	deinitialize();
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	fail_read = true;
	zassert_equal(disk_access_init(NAME), -EIO);
	fail_read = false;
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_ok(disk_access_init(NAME));
	zassert_ok(disk_access_read(NAME, readback, 0, 1));
	zassert_mem_equal(data, readback, SIZE);
}

ZTEST(storage_zms, test_mounted_metadata_and_payload_read_errors)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	memset(data, 0x37, SIZE);
	zassert_ok(disk_access_write(NAME, data, 0, 1));
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));

	fail_zms_read_length = 16U;
	zassert_equal(disk_access_read(NAME, readback, 0, 1), -EIO);
	zassert_true(failed_zms_reads > 0U);
	fail_zms_read_length = SIZE;
	failed_zms_reads = 0U;
	zassert_equal(disk_access_read(NAME, readback, 0, 1), -EIO);
	zassert_true(failed_zms_reads > 0U);
	fail_zms_read_length = 0U;
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_ok(disk_access_read(NAME, readback, 0, 1));
	zassert_mem_equal(data, readback, SIZE);
}

ZTEST(storage_zms, test_gc_copy_read_error)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	for (uint32_t index = 0U; index < 63U; index++) {
		memset(data, index, SIZE);
		zassert_ok(disk_access_write(NAME, data, index % COUNT, 1));
	}
	/* The newest live record is copied first, in ZMS_BLOCK_SIZE chunks.
	 * Its payload starts at 62 * 4096; the CRC is stored in the ATE.
	 */
	fail_zms_read_offset = area->fa_off + 62U * SIZE;
	fail_zms_read_length = 32U;
	memset(data, 0xa5, SIZE);
	zassert_equal(disk_access_write(NAME, data, 0, 1), -EIO);
	zassert_true(failed_zms_reads > 0U, "GC copy failure was not injected");
	fail_zms_read_length = 0U;
	remount();
	for (uint32_t sector = 0U; sector < COUNT; sector++) {
		memset(data, sector < 15U ? sector + COUNT : sector, SIZE);
		zassert_ok(disk_access_read(NAME, readback, sector, 1));
		zassert_mem_equal(data, readback, SIZE);
	}
}

ZTEST(storage_zms, test_mount_refuses_unknown_media_without_changes)
{
	reset_media();
	memory[area->fa_off + 77U] = 0x17;
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	/* Recognition refuses the media; the subsequent erased check sees an
	 * uninitialized disk and returns EACCES through the mount API.
	 */
	zassert_equal(kfsw_storage_mount(), -EACCES);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(operations, 0U);
	zassert_equal(erases, 0U);
}

ZTEST(storage_zms, test_mount_refuses_corrupt_littlefs_without_changes)
{
	reset_media();
	zassert_ok(disk_access_init(NAME));
	for (uint32_t sector = 0U; sector < COUNT; sector++) {
		memset(data, sector, SIZE);
		zassert_ok(disk_access_write(NAME, data, sector, 1));
	}
	deinitialize();
	memcpy(snapshot, memory + area->fa_off, sizeof(snapshot));
	size_t before = operations;
	size_t erases_before = erases;

	zassert_equal(kfsw_storage_mount(), -EFAULT);
	zassert_mem_equal(snapshot, memory + area->fa_off, sizeof(snapshot));
	zassert_equal(operations, before);
	zassert_equal(erases, erases_before);
	zassert_ok(disk_access_init(NAME));
	for (uint32_t sector = 0U; sector < COUNT; sector++) {
		memset(data, sector, SIZE);
		zassert_ok(disk_access_read(NAME, readback, sector, 1));
		zassert_mem_equal(data, readback, SIZE);
	}
}

ZTEST(storage_zms, test_littlefs_tree_full_and_remount)
{
	static const char *const directories[] = {
		"/kfsw/params", "/kfsw/hk", "/kfsw/ftp", "/kfsw/ftp/procedures", "/kfsw/boot",
	};
	struct fs_file_t file;
	struct fs_statvfs statistics;
	size_t stored = 0U;
	bool refused = false;

	reset_media();
	zassert_ok(kfsw_storage_mount());
	for (size_t index = 0U; index < ARRAY_SIZE(directories); index++) {
		zassert_ok(fs_mkdir(directories[index]));
	}
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, "/kfsw/journal.bin", FS_O_CREATE | FS_O_WRITE));
	zassert_ok(fs_close(&file));
	zassert_ok(fs_statvfs("/kfsw", &statistics));
	printk("LittleFS after tree: total %lu free %lu bytes\n",
	       statistics.f_blocks * statistics.f_frsize, statistics.f_bfree * statistics.f_frsize);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, "/kfsw/params/full", FS_O_CREATE | FS_O_WRITE));
	memset(data, 0x3c, SIZE);
	while (stored < COUNT * SIZE) {
		ssize_t result = fs_write(&file, data, SIZE);

		if (result < 0) {
			zassert_equal(result, -ENOSPC);
			refused = true;
			break;
		}
		int synced = fs_sync(&file);

		if (synced != 0) {
			zassert_equal(synced, -ENOSPC);
			refused = true;
			break;
		}
		stored += result;
		if (result != SIZE) {
			break;
		}
	}
	zassert_true(refused, "Expected full-volume refusal");
	zassert_true(stored >= SIZE && stored < COUNT * SIZE);
	zassert_ok(fs_close(&file));
	zassert_ok(kfsw_storage_unmount());
	zassert_ok(kfsw_storage_mount());
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, "/kfsw/params/full", FS_O_READ));
	for (size_t offset = 0U; offset < stored; offset += SIZE) {
		size_t length = MIN(SIZE, stored - offset);

		zassert_equal(fs_read(&file, readback, length), length);
		zassert_mem_equal(data, readback, length);
	}
	zassert_ok(fs_close(&file));
	zassert_ok(kfsw_storage_unmount());
	printk("LittleFS full-volume payload: %zu bytes\n", stored);
}

ZTEST_SUITE(storage_zms, NULL, setup, NULL, NULL, NULL);
