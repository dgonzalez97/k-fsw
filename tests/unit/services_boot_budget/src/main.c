#include <errno.h>
#include <cmdline.h>
#include <posix_native_task.h>
#include <posix_board_if.h>
#include <zephyr/sys/reboot.h>
#include <kfsw/platform/boot_store.h>
#include <kfsw/platform/storage.h>
#include <string.h>
#include <bootutil/image.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/lastwords.h>
#include <kfsw/services/boot.h>
#include <kfsw/services/boot_budget.h>
#include <kfsw/services/fwu.h>
#include <kfsw/services/golden.h>
#include <kfsw/services/parameter.h>

static bool hosted;
static bool bad_note_crc;

static void add_hosted_option(void)
{
	static struct args_struct_t options[] = {
		{.option = "boot_budget_hosted",
		 .type = 'b',
		 .is_switch = true,
		 .dest = &hosted,
		 .descript = "Inspect the budget across process restarts"},
		ARG_TABLE_ENDMARKER,
	};
	native_add_command_line_opts(options);
}
NATIVE_TASK(add_hosted_option, PRE_BOOT_1, 10);

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
	ARG_UNUSED(type);
	printk("BOOT BUDGET: reset into installed Golden\n");
	posix_exit(0);
	CODE_UNREACHABLE;
}

uint32_t __real_crc32_ieee(const uint8_t *data, size_t size);
uint32_t __wrap_crc32_ieee(const uint8_t *data, size_t size)
{
	return __real_crc32_ieee(data, size) ^ (bad_note_crc ? 1U : 0U);
}

static int swap_type;

int mcuboot_swap_type(void)
{
	return swap_type;
}

int boot_request_upgrade(int permanent)
{
	zassert_equal(permanent, BOOT_UPGRADE_TEST);
	swap_type = BOOT_SWAP_TYPE_TEST;
	return 0;
}

static bool fail_write;
static bool fail_read;
static uint8_t image[DT_REG_SIZE(DT_CHOSEN(kfsw_golden_partition))];

ssize_t __real_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t size);
ssize_t __wrap_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t size)
{
	return fail_write ? -EIO : __real_nvs_write(fs, id, data, size);
}

ssize_t __real_nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t size);
ssize_t __wrap_nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t size)
{
	return fail_read ? -EIO : __real_nvs_read(fs, id, data, size);
}

static uint8_t remaining(void)
{
	uint8_t left;
	uint8_t maximum;
	kfsw_boot_budget_get(&left, &maximum);
	return left;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	if (hosted) {
		return;
	}
	fail_write = false;
	fail_read = false;
	swap_type = BOOT_SWAP_TYPE_NONE;
	zassert_ok(kfsw_boot_budget_set_max(3U));
	zassert_ok(kfsw_boot_budget_refill());
}

ZTEST(boot_budget, test_restart_sequence_and_healthy_boot_never_refills)
{
	zassert_equal(remaining(), 3U);
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 2U);
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 1U);
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 0U);
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 0U);
}

ZTEST(boot_budget, test_last_attempt_still_runs_the_application)
{
	zassert_ok(kfsw_boot_budget_set_left(1U));
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 0U);
	zassert_false(kfsw_boot_budget_spent());
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 0U);
	zassert_true(kfsw_boot_budget_spent());
}

ZTEST(boot_budget, test_ground_parameter_refills_and_limits)
{
	const struct kfsw_param_definition_set *sets[] = {&kfsw_boot_param_definitions};
	struct kfsw_param_value value = {.type = KFSW_PARAM_U8, .size = 1U, .scalar.u8 = 3U};
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_equal(remaining(), 2U);
	zassert_ok(kfsw_param_set("boot_attempts_left", &value));
	zassert_equal(remaining(), 3U);
	value.scalar.u8 = 5U;
	zassert_ok(kfsw_param_set("boot_attempts_max", &value));
	zassert_equal(remaining(), 3U);
	value.scalar.u8 = 4U;
	zassert_equal(kfsw_param_set("boot_attempts_max", &value), -EINVAL);
	value.scalar.u8 = 6U;
	zassert_equal(kfsw_param_set("boot_attempts_left", &value), -ERANGE);
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 2U);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_param_persist_save());
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 1U);
	zassert_ok(kfsw_param_persist_load());
	zassert_equal(remaining(), 1U, "snapshot must not refill early consumption");
	zassert_ok(kfsw_param_restore_defaults());
	zassert_equal(remaining(), 1U, "RAM defaults must not refill the flash record");
}

ZTEST(boot_budget, test_nucleo_commanded_evidence_and_watchdog)
{
	struct kfsw_lastwords note;
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 0U, 1U, 1U);
	zassert_true(kfsw_lastwords_take(&note));
	/* STM32's software reset can also latch PIN; neither alone is evidence. */
	zassert_true(kfsw_boot_budget_commanded(RESET_SOFTWARE | RESET_PIN,
						note.reason == KFSW_LASTWORDS_COMMANDED));
	zassert_ok(kfsw_boot_budget_start(true));
	zassert_equal(remaining(), 3U);
	zassert_false(kfsw_lastwords_take(&note));
	zassert_false(kfsw_boot_budget_commanded(RESET_SOFTWARE, false));
	zassert_false(kfsw_boot_budget_commanded(RESET_WATCHDOG | RESET_SOFTWARE, true));
	zassert_false(kfsw_boot_budget_commanded(RESET_BROWNOUT | RESET_SOFTWARE, true));
	zassert_false(kfsw_boot_budget_commanded(RESET_POR, true));
	zassert_false(kfsw_boot_budget_commanded(0U, true));
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 2U);
}

ZTEST(boot_budget, test_write_failure_does_not_cancel_consumption)
{
	fail_write = true;
	zassert_equal(kfsw_boot_budget_start(false), -EIO);
	zassert_equal(remaining(), 2U);
	zassert_equal(kfsw_boot_budget_set_left(3U), -EIO);
	zassert_equal(remaining(), 2U);
}

ZTEST(boot_budget, test_fwu_install_refills)
{
	struct kfsw_fwu_status status;
	memset(image, 0x55, sizeof(image));
	zassert_ok(kfsw_boot_budget_set_left(1U));
	zassert_ok(kfsw_fwu_begin(sizeof(image), crc32_ieee(image, sizeof(image))));
	zassert_ok(kfsw_fwu_write(0U, image, sizeof(image)));
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(remaining(), 1U);
	zassert_ok(kfsw_fwu_session_verify(&status.handle, &status));
	zassert_ok(kfsw_fwu_session_activate(&status.handle, &status));
	zassert_equal(remaining(), 3U);
	zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
}

ZTEST(boot_budget, test_mcuboot_revert_precedes_golden_and_does_not_refill)
{
	const struct flash_area *area;
	enum kfsw_golden_outcome outcome;
	struct kfsw_fwu_status status;
	uint8_t actual[32];

	memset(image, 0x77, sizeof(image));
	sys_put_le32(IMAGE_MAGIC, image);
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_golden_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0U, area->fa_size));
	zassert_ok(flash_area_write(area, 0U, image, sizeof(image)));
	flash_area_close(area);
	zassert_ok(kfsw_boot_budget_set_left(1U));
	zassert_ok(kfsw_boot_budget_start(false));
	swap_type = BOOT_SWAP_TYPE_REVERT;
	zassert_equal(kfsw_golden_check(&outcome), -EBUSY);
	zassert_equal(outcome, KFSW_GOLDEN_NOT_NEEDED);
	zassert_equal(remaining(), 0U);
	/* MCUboot has reverted before the old application next executes. */
	swap_type = BOOT_SWAP_TYPE_NONE;
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 0U);
	zassert_ok(kfsw_golden_check(&outcome));
	zassert_equal(outcome, KFSW_GOLDEN_INSTALLED);
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_fwu_partition)), &area));
	zassert_ok(flash_area_read(area, kfsw_fwu_slot_write_offset(), actual, sizeof(actual)));
	zassert_mem_equal(actual, image, sizeof(actual));
	flash_area_close(area);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
}

ZTEST(boot_budget, test_invalid_note_spends_attempt)
{
	struct kfsw_lastwords note;
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 0U, 1U, 1U);
	bad_note_crc = true;
	zassert_false(kfsw_lastwords_take(&note));
	bad_note_crc = false;
	zassert_false(kfsw_boot_budget_commanded(RESET_SOFTWARE,
						 note.reason == KFSW_LASTWORDS_COMMANDED));
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 2U);
}

ZTEST(boot_budget, test_hosted_prepare)
{
	const struct flash_area *area;
	if (!hosted) {
		ztest_test_skip();
	}
	memset(image, 0x77, sizeof(image));
	sys_put_le32(IMAGE_MAGIC, image);
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_golden_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0U, area->fa_size));
	zassert_ok(flash_area_write(area, 0U, image, sizeof(image)));
	flash_area_close(area);
	zassert_ok(kfsw_boot_budget_refill());
	printk("BOOT BUDGET: prepared left=%u\n", remaining());
}

ZTEST(boot_budget, test_hosted_inspect)
{
	if (!hosted) {
		ztest_test_skip();
	}
	printk("BOOT BUDGET: application left=%u\n", remaining());
}

ZTEST(boot_budget, test_hosted_golden)
{
	uint8_t left;
	uint8_t maximum;
	if (!hosted) {
		ztest_test_skip();
	}
	zassert_true(IS_ENABLED(CONFIG_KFSW_GOLDEN_IMAGE));
	zassert_false(IS_ENABLED(CONFIG_KFSW_BOOT_BUDGET));
	zassert_equal(kfsw_golden_install(), -ENODEV);
	zassert_ok(kfsw_boot_store_open());
	zassert_ok(kfsw_boot_store_read(&left, &maximum));
	zassert_equal(left, maximum);
	/* Recovery runs the lite front end and no shell, so the only way to see
	   the budget of the image it is about to replace is over CSP. */
	const struct kfsw_param_definition_set *sets[] = {&kfsw_boot_param_definitions};
	struct kfsw_param_value value = {.type = KFSW_PARAM_U8, .size = 1U};
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_param_get("boot_attempts_left", &value));
	zassert_equal(value.scalar.u8, left);
	value.scalar.u8 = 1U;
	zassert_ok(kfsw_param_set("boot_attempts_left", &value));
	zassert_ok(kfsw_boot_store_read(&left, &maximum));
	zassert_equal(left, 1U);
	zassert_ok(kfsw_boot_budget_refill());
	printk("BOOT BUDGET: Golden up; no budget consumed; self-install refused\n");
}

ZTEST(boot_budget, test_read_failure_leaves_budget_unavailable)
{
	fail_read = true;
	zassert_equal(kfsw_boot_budget_start(false), -EIO);
	zassert_equal(remaining(), UINT8_MAX);
	fail_read = false;
	zassert_ok(kfsw_boot_budget_start(false));
	zassert_equal(remaining(), 2U);
}

ZTEST(boot_budget, test_empty_golden_keeps_application_behavior)
{
	const struct flash_area *area;
	enum kfsw_golden_outcome outcome;
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_golden_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0U, area->fa_size));
	flash_area_close(area);
	zassert_ok(kfsw_boot_budget_set_left(0U));
	zassert_equal(kfsw_golden_check(&outcome), -ENOENT);
	zassert_equal(outcome, KFSW_GOLDEN_NOT_NEEDED);
}

ZTEST(boot_budget, test_maximum_changes_never_refill)
{
	zassert_ok(kfsw_boot_budget_set_max(10U));
	zassert_equal(remaining(), 3U);
	zassert_ok(kfsw_boot_budget_set_left(10U));
	zassert_ok(kfsw_boot_budget_set_max(5U));
	zassert_equal(remaining(), 5U);
	zassert_ok(kfsw_boot_budget_set_max(3U));
	zassert_equal(remaining(), 3U);
	zassert_equal(kfsw_boot_budget_set_max(0U), -EINVAL);
	zassert_equal(kfsw_boot_budget_set_max(7U), -EINVAL);
}

ZTEST_SUITE(boot_budget, NULL, NULL, before, NULL, NULL);
