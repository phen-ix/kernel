// SPDX-License-Identifier: GPL-2.0
/*
 * Utilization clamping for Linux 4.14/CASS.
 *
 * Core task/bucket/rq aggregation is adapted from the upstream uclamp series
 * carried by darkhz/uclamp (uclamp_4.14, bc867bf6c38f) and AOSP kernel_msm
 * android-msm-redbull-4.19-android12.  The cpuset and CPU-controller groups are
 * deliberately separate: this ROM writes both hierarchies, and a cpuset CSS is
 * not a task_group CSS.
 *
 * Coexistence with schedtune: schedtune's margin is applied to the raw util
 * first, then uclamp min/max are applied as bounds.  Boosts are combined by
 * max/clamp, never by adding a uclamp floor on top of an already boosted util.
 */
#include "sched.h"
#include <linux/sched/uclamp.h>
#include <uapi/linux/sched/types.h>
#include <linux/cpuset.h>
#include <linux/sysctl.h>
#include <linux/jump_label.h>

static DEFINE_MUTEX(uclamp_mutex);

unsigned int sysctl_sched_uclamp_util_min;
unsigned int sysctl_sched_uclamp_util_max = SCHED_CAPACITY_SCALE;
unsigned int sysctl_sched_uclamp_util_min_rt_default = SCHED_CAPACITY_SCALE;

static struct uclamp_se uclamp_default[UCLAMP_CNT];
DEFINE_STATIC_KEY_FALSE(sched_uclamp_used);

#define UCLAMP_BUCKET_DELTA \
	DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS)
#define for_each_clamp_id(id) for ((id) = 0; (id) < UCLAMP_CNT; (id)++)

static inline unsigned int uclamp_bucket_id(unsigned int value)
{
	return min_t(unsigned int, value / UCLAMP_BUCKET_DELTA,
		     UCLAMP_BUCKETS - 1);
}

static inline unsigned int uclamp_none(enum uclamp_id id)
{
	return id == UCLAMP_MIN ? 0 : SCHED_CAPACITY_SCALE;
}

static inline void uclamp_se_set(struct uclamp_se *se, unsigned int value,
				 bool user_defined)
{
	se->value = value;
	se->bucket_id = uclamp_bucket_id(value);
	se->user_defined = user_defined;
}

void uclamp_group_init(struct uclamp_group *ug, struct uclamp_group *parent)
{
	enum uclamp_id id;

	for_each_clamp_id(id) {
		uclamp_se_set(&ug->req[id], uclamp_none(id), false);
		if (parent)
			ug->eff[id] = parent->eff[id];
		else
			uclamp_se_set(&ug->eff[id], uclamp_none(id), false);
		ug->pct[id] = id == UCLAMP_MIN ? 0 : 10000;
	}
	ug->latency_sensitive_req = parent ? parent->latency_sensitive_req : 0;
	ug->latency_sensitive = parent ? parent->latency_sensitive : 0;
}

static inline unsigned int uclamp_idle_value(struct rq *rq,
		enum uclamp_id id, unsigned int value)
{
	if (id == UCLAMP_MAX) {
		rq->uclamp_flags |= UCLAMP_FLAG_IDLE;
		return value;
	}
	return uclamp_none(UCLAMP_MIN);
}

static inline void uclamp_idle_reset(struct rq *rq, enum uclamp_id id,
				     unsigned int value)
{
	if (rq->uclamp_flags & UCLAMP_FLAG_IDLE)
		WRITE_ONCE(rq->uclamp[id].value, value);
}

static unsigned int uclamp_rq_max_value(struct rq *rq, enum uclamp_id id,
					unsigned int value)
{
	struct uclamp_bucket *bucket = rq->uclamp[id].bucket;
	int i;

	for (i = UCLAMP_BUCKETS - 1; i >= 0; i--) {
		if (bucket[i].tasks)
			return bucket[i].value;
	}
	return uclamp_idle_value(rq, id, value);
}

static struct uclamp_group *task_cpu_group(struct task_struct *p)
{
#ifdef CONFIG_UCLAMP_TASK_GROUP
	struct task_group *tg = task_group(p);

	if (!tg || tg == &root_task_group || task_group_is_autogroup(tg))
		return NULL;
	return &tg->uclamp;
#else
	return NULL;
#endif
}


static struct uclamp_se uclamp_eff_get(struct task_struct *p,
				       enum uclamp_id id)
{
	struct uclamp_se se = p->uclamp_req[id];
	struct uclamp_group *cpu_ug, *cs_ug;
	unsigned int value = se.value;

	rcu_read_lock();
	cpu_ug = task_cpu_group(p);
	cs_ug = uclamp_cpuset_group(p);

	if (!se.user_defined) {
		if (id == UCLAMP_MIN) {
			if (cpu_ug)
				value = max_t(unsigned int, value,
					      cpu_ug->eff[id].value);
			if (cs_ug)
				value = max_t(unsigned int, value,
					      cs_ug->eff[id].value);
		} else {
			if (cpu_ug)
				value = min_t(unsigned int, value,
					      cpu_ug->eff[id].value);
			if (cs_ug)
				value = min_t(unsigned int, value,
					      cs_ug->eff[id].value);
		}
	} else {
		if (cpu_ug)
			value = min_t(unsigned int, value,
				      cpu_ug->eff[id].value);
		if (cs_ug)
			value = min_t(unsigned int, value,
				      cs_ug->eff[id].value);
	}

	value = min_t(unsigned int, value, uclamp_default[id].value);
	rcu_read_unlock();
	uclamp_se_set(&se, value, se.user_defined);
	return se;
}

unsigned long uclamp_eff_value(struct task_struct *p, enum uclamp_id id)
{
	struct uclamp_se se;

	if (p->uclamp[id].active)
		return p->uclamp[id].value;
	se = uclamp_eff_get(p, id);
	return se.value;
}

static void uclamp_rq_inc_id(struct rq *rq, struct task_struct *p,
			     enum uclamp_id id)
{
	struct uclamp_rq *uc_rq = &rq->uclamp[id];
	struct uclamp_se *se = &p->uclamp[id];
	struct uclamp_bucket *bucket;

	lockdep_assert_held(&rq->lock);
	*se = uclamp_eff_get(p, id);
	bucket = &uc_rq->bucket[se->bucket_id];
	bucket->tasks++;
	se->active = true;
	uclamp_idle_reset(rq, id, se->value);
	if (bucket->tasks == 1 || se->value > bucket->value)
		bucket->value = se->value;
	if (se->value > READ_ONCE(uc_rq->value))
		WRITE_ONCE(uc_rq->value, se->value);
}

static void uclamp_rq_dec_id(struct rq *rq, struct task_struct *p,
			     enum uclamp_id id)
{
	struct uclamp_rq *uc_rq = &rq->uclamp[id];
	struct uclamp_se *se = &p->uclamp[id];
	struct uclamp_bucket *bucket;
	unsigned int rq_clamp;

	lockdep_assert_held(&rq->lock);
	if (unlikely(!se->active))
		return;
	bucket = &uc_rq->bucket[se->bucket_id];
	SCHED_WARN_ON(!bucket->tasks);
	if (likely(bucket->tasks))
		bucket->tasks--;
	se->active = false;
	if (likely(bucket->tasks))
		return;
	rq_clamp = READ_ONCE(uc_rq->value);
	SCHED_WARN_ON(bucket->value > rq_clamp);
	if (bucket->value >= rq_clamp)
		WRITE_ONCE(uc_rq->value,
			   uclamp_rq_max_value(rq, id, se->value));
}

void uclamp_enqueue(struct rq *rq, struct task_struct *p)
{
	enum uclamp_id id;

	if (!static_branch_unlikely(&sched_uclamp_used))
		return;
	if (unlikely(!p->sched_class->uclamp_enabled))
		return;
	for_each_clamp_id(id)
		uclamp_rq_inc_id(rq, p, id);
	if (rq->uclamp_flags & UCLAMP_FLAG_IDLE)
		rq->uclamp_flags &= ~UCLAMP_FLAG_IDLE;
}

void uclamp_dequeue(struct rq *rq, struct task_struct *p)
{
	enum uclamp_id id;

	if (!static_branch_unlikely(&sched_uclamp_used))
		return;
	if (unlikely(!p->sched_class->uclamp_enabled))
		return;
	for_each_clamp_id(id)
		uclamp_rq_dec_id(rq, p, id);
}

static void __uclamp_refresh_task(struct task_struct *p)
{
	enum uclamp_id id;

	for_each_clamp_id(id) {
		if (p->uclamp[id].active) {
			uclamp_rq_dec_id(task_rq(p), p, id);
			uclamp_rq_inc_id(task_rq(p), p, id);
		}
	}
}

void uclamp_refresh_task(struct task_struct *p)
{
	struct rq_flags rf;
	struct rq *rq;

	if (!static_branch_likely(&sched_uclamp_used))
		return;
	rq = task_rq_lock(p, &rf);
	__uclamp_refresh_task(p);
	task_rq_unlock(rq, p, &rf);
}

static void uclamp_update_active_tasks(struct cgroup_subsys_state *css)
{
	struct css_task_iter it;
	struct task_struct *p;

	css_task_iter_start(css, 0, &it);
	while ((p = css_task_iter_next(&it)))
		uclamp_refresh_task(p);
	css_task_iter_end(&it);
}

void uclamp_update_css(struct cgroup_subsys_state *top_css,
		struct uclamp_group *(*get_group)(struct cgroup_subsys_state *))
{
	struct cgroup_subsys_state *css;
	enum uclamp_id id;

	lockdep_assert_held(&uclamp_mutex);
	rcu_read_lock();
	css_for_each_descendant_pre(css, top_css) {
		struct uclamp_group *ug = get_group(css);
		struct uclamp_group *parent = css->parent ?
			get_group(css->parent) : NULL;
		unsigned int eff[UCLAMP_CNT];
		bool changed = false;
		unsigned int ls;

		for_each_clamp_id(id) {
			eff[id] = ug->req[id].value;
			if (parent)
				eff[id] = min_t(unsigned int, eff[id],
						parent->eff[id].value);
		}
		eff[UCLAMP_MIN] = min(eff[UCLAMP_MIN], eff[UCLAMP_MAX]);
		ls = ug->latency_sensitive_req | (parent ? parent->latency_sensitive : 0);
		for_each_clamp_id(id) {
			if (ug->eff[id].value != eff[id]) {
				uclamp_se_set(&ug->eff[id], eff[id], false);
				changed = true;
			}
		}
		if (ls != ug->latency_sensitive) {
			ug->latency_sensitive = ls;
			changed = true;
		}
		if (changed)
			uclamp_update_active_tasks(css);
	}
	rcu_read_unlock();
}

static void __uclamp_update_rt_default(struct task_struct *p)
{
	struct uclamp_se *se = &p->uclamp_req[UCLAMP_MIN];

	lockdep_assert_held(&p->pi_lock);
	if (se->user_defined)
		return;
	uclamp_se_set(se, sysctl_sched_uclamp_util_min_rt_default, false);
}

static void uclamp_update_rt_default(struct task_struct *p)
{
	struct rq_flags rf;
	struct rq *rq;

	if (!rt_task(p))
		return;
	rq = task_rq_lock(p, &rf);
	__uclamp_update_rt_default(p);
	__uclamp_refresh_task(p);
	task_rq_unlock(rq, p, &rf);
}

static void uclamp_sync_rt_default(void)
{
	struct task_struct *g, *p;

	rcu_read_lock();
	for_each_process_thread(g, p)
		uclamp_update_rt_default(p);
	rcu_read_unlock();
}

int sysctl_sched_uclamp_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int old_min, old_max, old_rt, ret;
	bool update = false;

	mutex_lock(&uclamp_mutex);
	old_min = sysctl_sched_uclamp_util_min;
	old_max = sysctl_sched_uclamp_util_max;
	old_rt = sysctl_sched_uclamp_util_min_rt_default;
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret || !write)
		goto undo;
	if (sysctl_sched_uclamp_util_min > sysctl_sched_uclamp_util_max ||
	    sysctl_sched_uclamp_util_max > SCHED_CAPACITY_SCALE ||
	    sysctl_sched_uclamp_util_min_rt_default > SCHED_CAPACITY_SCALE) {
		ret = -EINVAL;
		goto undo;
	}
	if (old_min != sysctl_sched_uclamp_util_min) {
		uclamp_se_set(&uclamp_default[UCLAMP_MIN],
			      sysctl_sched_uclamp_util_min, false);
		update = true;
	}
	if (old_max != sysctl_sched_uclamp_util_max) {
		uclamp_se_set(&uclamp_default[UCLAMP_MAX],
			      sysctl_sched_uclamp_util_max, false);
		update = true;
	}
	if (update) {
		struct task_struct *g, *p;

		static_branch_enable(&sched_uclamp_used);
		rcu_read_lock();
		for_each_process_thread(g, p)
			uclamp_refresh_task(p);
		rcu_read_unlock();
	}
	if (old_rt != sysctl_sched_uclamp_util_min_rt_default) {
		static_branch_enable(&sched_uclamp_used);
		uclamp_sync_rt_default();
	}
	mutex_unlock(&uclamp_mutex);
	return 0;
undo:
	sysctl_sched_uclamp_util_min = old_min;
	sysctl_sched_uclamp_util_max = old_max;
	sysctl_sched_uclamp_util_min_rt_default = old_rt;
	mutex_unlock(&uclamp_mutex);
	return ret;
}

int uclamp_validate(struct task_struct *p, const struct sched_attr *attr)
{
	unsigned int lower = p->uclamp_req[UCLAMP_MIN].value;
	unsigned int upper = p->uclamp_req[UCLAMP_MAX].value;

	if (attr->sched_flags & SCHED_FLAG_UTIL_CLAMP_MIN)
		lower = attr->sched_util_min;
	if (attr->sched_flags & SCHED_FLAG_UTIL_CLAMP_MAX)
		upper = attr->sched_util_max;
	if (lower > upper || upper > SCHED_CAPACITY_SCALE)
		return -EINVAL;
	static_branch_enable(&sched_uclamp_used);
	return 0;
}

void uclamp_setattr(struct task_struct *p, const struct sched_attr *attr)
{
	enum uclamp_id id;

	for_each_clamp_id(id) {
		struct uclamp_se *se = &p->uclamp_req[id];

		if (se->user_defined)
			continue;
		if (unlikely(rt_task(p) && id == UCLAMP_MIN))
			__uclamp_update_rt_default(p);
		else
			uclamp_se_set(se, uclamp_none(id), false);
	}
	if (attr->sched_flags & SCHED_FLAG_UTIL_CLAMP_MIN)
		uclamp_se_set(&p->uclamp_req[UCLAMP_MIN],
			      attr->sched_util_min, true);
	if (attr->sched_flags & SCHED_FLAG_UTIL_CLAMP_MAX)
		uclamp_se_set(&p->uclamp_req[UCLAMP_MAX],
			      attr->sched_util_max, true);
}

void uclamp_fork(struct task_struct *p)
{
	enum uclamp_id id;

	for_each_clamp_id(id)
		p->uclamp[id].active = false;
	if (likely(!p->sched_reset_on_fork))
		return;
	for_each_clamp_id(id)
		uclamp_se_set(&p->uclamp_req[id], uclamp_none(id), false);
}

void uclamp_post_fork(struct task_struct *p)
{
	uclamp_update_rt_default(p);
}

unsigned long uclamp_task_util(struct task_struct *p, unsigned long util)
{
	unsigned int min_util, max_util;

	if (!uclamp_is_used())
		return util;
	min_util = uclamp_eff_value(p, UCLAMP_MIN);
	max_util = uclamp_eff_value(p, UCLAMP_MAX);
	if (min_util >= max_util)
		return min_util;
	return clamp_t(unsigned long, util, min_util, max_util);
}

bool uclamp_latency_sensitive(struct task_struct *p)
{
	struct uclamp_group *cpu_ug, *cs_ug;
	bool ls = false;

	if (!uclamp_is_used())
		return false;
	rcu_read_lock();
	cpu_ug = task_cpu_group(p);
	cs_ug = uclamp_cpuset_group(p);
	if (cpu_ug)
		ls |= !!cpu_ug->latency_sensitive;
	if (cs_ug)
		ls |= !!cs_ug->latency_sensitive;
	rcu_read_unlock();
	return ls;
}

void __init uclamp_init(void)
{
	enum uclamp_id id;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		for_each_clamp_id(id) {
			rq->uclamp[id].value = uclamp_none(id);
			memset(rq->uclamp[id].bucket, 0,
			       sizeof(rq->uclamp[id].bucket));
		}
		rq->uclamp_flags = 0;
	}
	for_each_clamp_id(id) {
		uclamp_se_set(&init_task.uclamp_req[id], uclamp_none(id), false);
		uclamp_se_set(&uclamp_default[id], SCHED_CAPACITY_SCALE, false);
	}
#ifdef CONFIG_UCLAMP_TASK_GROUP
	uclamp_group_init(&root_task_group.uclamp, NULL);
#endif
}

/* Percentage parsing: upstream a5e112e6424a, with negative whole rejected. */
static u64 power_of_ten(int power)
{
	u64 v = 1;

	while (power--)
		v *= 10;
	return v;
}

static int cgroup_parse_float_local(const char *input, unsigned int dec_shift,
				    s64 *v)
{
	s64 whole, frac = 0;
	int fstart = 0, fend = 0, flen;

	if (!sscanf(input, "%lld.%n%lld%n", &whole, &fstart, &frac, &fend))
		return -EINVAL;
	if (whole < 0 || frac < 0)
		return -EINVAL;
	flen = fend > fstart ? fend - fstart : 0;
	if (flen < dec_shift)
		frac *= power_of_ten(dec_shift - flen);
	else
		frac = DIV_ROUND_CLOSEST_ULL(frac, power_of_ten(flen - dec_shift));
	*v = whole * power_of_ten(dec_shift) + frac;
	return 0;
}

#define UCLAMP_PERCENT_SHIFT 2
#define UCLAMP_PERCENT_SCALE (100 * 100)

ssize_t uclamp_css_write(struct kernfs_open_file *of, char *buf,
			 size_t nbytes, loff_t off)
{
	const struct uclamp_cftype *ucft = (void *)of_cft(of)->private;
	struct uclamp_group *ug;
	s64 percent = UCLAMP_PERCENT_SCALE;
	u64 util = SCHED_CAPACITY_SCALE;
	int ret;

	buf = strim(buf);
	if (strcmp(buf, "max")) {
		ret = cgroup_parse_float_local(buf, UCLAMP_PERCENT_SHIFT,
					       &percent);
		if (ret)
			return ret;
		if (percent > UCLAMP_PERCENT_SCALE)
			return -ERANGE;
		util = DIV_ROUND_CLOSEST_ULL(percent << SCHED_CAPACITY_SHIFT,
					     UCLAMP_PERCENT_SCALE);
	}
	static_branch_enable(&sched_uclamp_used);
	mutex_lock(&uclamp_mutex);
	ug = ucft->get_group(of_css(of));
	uclamp_se_set(&ug->req[ucft->clamp_id], util, false);
	ug->pct[ucft->clamp_id] = percent;
	uclamp_update_css(of_css(of), ucft->get_group);
	mutex_unlock(&uclamp_mutex);
	return nbytes;
}

int uclamp_css_show(struct seq_file *sf, void *v)
{
	const struct uclamp_cftype *ucft = (void *)seq_cft(sf)->private;
	struct uclamp_group *ug;
	u64 percent;
	u32 rem;
	rcu_read_lock();
	ug = ucft->get_group(seq_css(sf));
	percent = ug->pct[ucft->clamp_id];
	rcu_read_unlock();
	if (percent == UCLAMP_PERCENT_SCALE) {
		seq_puts(sf, "max\n");
		return 0;
	}
	percent = div_u64_rem(percent, 100, &rem);
	seq_printf(sf, "%llu.%02u\n", percent, rem);
	return 0;
}

int uclamp_css_ls_write(struct cgroup_subsys_state *css, struct cftype *cft,
			u64 val)
{
	const struct uclamp_cftype *ucft = (void *)cft->private;
	struct uclamp_group *ug;

	if (val > 1)
		return -EINVAL;
	static_branch_enable(&sched_uclamp_used);
	mutex_lock(&uclamp_mutex);
	ug = ucft->get_group(css);
	ug->latency_sensitive_req = val;
	uclamp_update_css(css, ucft->get_group);
	mutex_unlock(&uclamp_mutex);
	return 0;
}

u64 uclamp_css_ls_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	const struct uclamp_cftype *ucft = (void *)cft->private;

	return ucft->get_group(css)->latency_sensitive_req;
}

void uclamp_group_online(struct cgroup_subsys_state *css,
		struct uclamp_group *(*get_group)(struct cgroup_subsys_state *))
{
	mutex_lock(&uclamp_mutex);
	uclamp_update_css(css, get_group);
	mutex_unlock(&uclamp_mutex);
}
