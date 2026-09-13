#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>
#include <zephyr/sys/reboot.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define THREADS 40U
#define SECONDARY PARTITION_ID(slot1_partition)

struct dispatch_sample {
	struct k_thread *thread;
	uint64_t ready;
	uint64_t maximum;
	uint32_t count;
	bool pending;
};

struct duration_sample {
	uint64_t maximum;
	uint32_t count;
};

static struct dispatch_sample dispatch[THREADS];
static struct duration_sample collection, local_read, sample_all;
static bool measuring;
static uint32_t overflow;
static uint32_t cut_erase_pages, cut_write_bytes;

static struct dispatch_sample *find_thread(struct k_thread *thread)
{
	for (size_t i = 0; i < ARRAY_SIZE(dispatch); i++) {
		if (dispatch[i].thread == thread) {
			return &dispatch[i];
		}
		if (dispatch[i].thread == NULL) {
			dispatch[i].thread = thread;
			return &dispatch[i];
		}
	}
	overflow++;
	return NULL;
}

void sys_trace_thread_sched_ready_user(struct k_thread *thread)
{
	unsigned int key = irq_lock();

	if (measuring) {
		struct dispatch_sample *sample = find_thread(thread);

		if (sample != NULL && !sample->pending) {
			sample->ready = k_cycle_get_64();
			sample->pending = true;
		}
	}
	irq_unlock(key);
}

void sys_trace_thread_switched_in_user(void)
{
	unsigned int key = irq_lock();

	if (measuring) {
		struct dispatch_sample *sample = find_thread(k_current_get());

		if (sample != NULL && sample->pending) {
			sample->maximum = MAX(sample->maximum, k_cycle_get_64() - sample->ready);
			sample->count++;
			sample->pending = false;
		}
	}
	irq_unlock(key);
}

static void duration(struct duration_sample *sample, uint64_t start)
{
	uint64_t elapsed = k_cycle_get_64() - start;
	unsigned int key = irq_lock();

	if (measuring) {
		sample->maximum = MAX(sample->maximum, elapsed);
		sample->count++;
	}
	irq_unlock(key);
}

int __real_kfsw_hk_collect(uint8_t report);
int __wrap_kfsw_hk_collect(uint8_t report)
{
	uint64_t start = k_cycle_get_64();
	int result = __real_kfsw_hk_collect(report);

	duration(&collection, start);
	return result;
}

int __real_kfsw_param_get_by_id(uint16_t id, struct kfsw_param_value *value);
int __wrap_kfsw_param_get_by_id(uint16_t id, struct kfsw_param_value *value)
{
	uint64_t start = k_cycle_get_64();
	int result = __real_kfsw_param_get_by_id(id, value);

	duration(&local_read, start);
	return result;
}

void __real_kfsw_param_sample_all(void);
void __wrap_kfsw_param_sample_all(void)
{
	uint64_t start = k_cycle_get_64();

	__real_kfsw_param_sample_all();
	duration(&sample_all, start);
}

int __real_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size);
int __wrap_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size)
{
	if (area->fa_id != SECONDARY || cut_erase_pages == 0U) {
		return __real_flash_area_flatten(area, offset, size);
	}
	while (size != 0U) {
		struct flash_pages_info page;
		int result =
			flash_get_page_info_by_offs(area->fa_dev, area->fa_off + offset, &page);

		if (result != 0) {
			return result;
		}
		if (page.start_offset != area->fa_off + offset || size < page.size) {
			return -EINVAL;
		}
		result = __real_flash_area_flatten(area, offset, page.size);
		if (result != 0) {
			return result;
		}
		offset += (off_t)page.size;
		size -= page.size;
		if (--cut_erase_pages == 0U) {
			printk("@HIL_CUT erase offset=%ld\n", (long)offset);
			sys_reboot(SYS_REBOOT_COLD);
		}
	}
	return 0;
}

int __real_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush);
int __wrap_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush)
{
	size_t before = ctx->bytes_written;
	int result = __real_stream_flash_buffered_write(ctx, data, size, flush);
	size_t written = ctx->bytes_written - before;

	if (result == 0 && ctx->offset >= DT_REG_ADDR(DT_NODELABEL(slot1_partition)) &&
	    ctx->offset < DT_REG_ADDR(DT_NODELABEL(slot1_partition)) +
				  DT_REG_SIZE(DT_NODELABEL(slot1_partition)) &&
	    cut_write_bytes != 0U) {
		if (written >= cut_write_bytes) {
			cut_write_bytes = 0U;
			printk("@HIL_CUT write offset=%lu\n", (unsigned long)ctx->bytes_written);
			sys_reboot(SYS_REBOOT_COLD);
		}
		cut_write_bytes -= (uint32_t)written;
	}
	return result;
}

static int start(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key = irq_lock();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	memset(dispatch, 0, sizeof(dispatch));
	collection = (struct duration_sample){0};
	local_read = (struct duration_sample){0};
	sample_all = (struct duration_sample){0};
	overflow = 0;
	measuring = true;
	irq_unlock(key);
	shell_print(sh, "Timing started");
	return 0;
}

static void print_duration(const struct shell *sh, const char *name,
			   const struct duration_sample *sample)
{
	shell_print(sh, "%s count=%u max_us=%llu", name, sample->count,
		    (unsigned long long)k_cyc_to_us_ceil64(sample->maximum));
}

static int report(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key = irq_lock();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	measuring = false;
	irq_unlock(key);
	for (size_t i = 0; i < ARRAY_SIZE(dispatch); i++) {
		const struct dispatch_sample *sample = &dispatch[i];
		size_t unused = 0;

		if (sample->thread == NULL) {
			continue;
		}
		int result = k_thread_stack_space_get(sample->thread, &unused);

		shell_print(sh, "thread=%s count=%u dispatch_max_us=%llu unused=%zu stack_rc=%d",
			    k_thread_name_get(sample->thread), sample->count,
			    (unsigned long long)k_cyc_to_us_ceil64(sample->maximum), unused,
			    result);
	}
	print_duration(sh, "hk_collect", &collection);
	print_duration(sh, "param_local", &local_read);
	print_duration(sh, "param_sample_all", &sample_all);
	shell_print(sh, "untracked=%u", overflow);
	return 0;
}

static int cut(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long count = strtoul(argv[2], &end, 10);

	ARG_UNUSED(argc);
	if (*end != '\0' || count == 0U || count > UINT32_MAX || argv[2][0] == '-') {
		return -EINVAL;
	}
	if (strcmp(argv[1], "erase") == 0) {
		cut_erase_pages = (uint32_t)count;
	} else if (strcmp(argv[1], "write") == 0) {
		cut_write_bytes = (uint32_t)count;
	} else {
		return -EINVAL;
	}
	shell_print(sh, "Reset armed: %s after %lu", argv[1], count);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(hil_commands,
	SHELL_CMD_ARG(start, NULL, "Start timing window.", start, 1, 0),
	SHELL_CMD_ARG(report, NULL, "Stop timing; print maxima and stacks.", report, 1, 0),
	SHELL_CMD_ARG(cut, NULL, "erase <pages> or write <bytes> before reset.", cut, 3, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(hil, &hil_commands, "Bench probes.", NULL);
