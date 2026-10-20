/* SPDX-License-Identifier: GPL-2.0 */


__BEGIN_CDECLS

struct device;
static inline void pm_runtime_disable(struct device *dev)
{
}

static inline void pm_runtime_get_noresume(struct device *dev)
{
}

static inline void pm_runtime_put_noidle(struct device *dev)
{
}

static inline int pm_runtime_get_sync(struct device *dev)
{
	return 0;
}

static inline void pm_runtime_dont_use_autosuspend(struct device *dev) {
}

static inline void pm_runtime_mark_last_busy(struct device *dev) {
}

static void pm_runtime_set_suspended(struct device *dev) {
}

static int pm_runtime_put_autosuspend(struct device *dev) {
	return 0;
}

static void pm_runtime_enable(struct device *dev) {
}

static void pm_runtime_set_active(struct device *dev) {
}

static void pm_runtime_use_autosuspend(struct device *dev) {

}

static void pm_runtime_set_autosuspend_delay(struct device *dev, unsigned int delay) {

}


struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

#define RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	.runtime_suspend = suspend_fn, \
	.runtime_resume = resume_fn, \
	.runtime_idle = idle_fn,

__END_CDECLS