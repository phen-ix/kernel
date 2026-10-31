/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_UCLAMP_H
#define _LINUX_SCHED_UCLAMP_H

#include <linux/sched.h>
#include <linux/cgroup.h>

struct rq;

#ifdef CONFIG_UCLAMP_TASK

struct uclamp_group {
	struct uclamp_se req[UCLAMP_CNT];
	struct uclamp_se eff[UCLAMP_CNT];
	s64 pct[UCLAMP_CNT];
	unsigned int latency_sensitive_req;
	unsigned int latency_sensitive;
};

struct uclamp_cftype {
	struct uclamp_group *(*get_group)(struct cgroup_subsys_state *css);
	enum uclamp_id clamp_id;
	bool latency_sensitive;
};

#ifdef CONFIG_UCLAMP_TASK_GROUP
struct uclamp_group *uclamp_cpuset_group(struct task_struct *p);
#else
static inline struct uclamp_group *uclamp_cpuset_group(struct task_struct *p)
{ return NULL; }
#endif

void uclamp_init(void);
void uclamp_fork(struct task_struct *p);
void uclamp_post_fork(struct task_struct *p);
void uclamp_enqueue(struct rq *rq, struct task_struct *p);
void uclamp_dequeue(struct rq *rq, struct task_struct *p);
void uclamp_refresh_task(struct task_struct *p);
int uclamp_validate(struct task_struct *p, const struct sched_attr *attr);
void uclamp_setattr(struct task_struct *p, const struct sched_attr *attr);
unsigned long uclamp_eff_value(struct task_struct *p, enum uclamp_id clamp_id);
unsigned long uclamp_task_util(struct task_struct *p, unsigned long util);
bool uclamp_latency_sensitive(struct task_struct *p);
int sysctl_sched_uclamp_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos);

void uclamp_group_init(struct uclamp_group *ug, struct uclamp_group *parent);
void uclamp_group_online(struct cgroup_subsys_state *css,
		struct uclamp_group *(*get_group)(struct cgroup_subsys_state *));
void uclamp_update_css(struct cgroup_subsys_state *css,
		struct uclamp_group *(*get_group)(struct cgroup_subsys_state *));
ssize_t uclamp_css_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off);
int uclamp_css_show(struct seq_file *sf, void *v);
int uclamp_css_ls_write(struct cgroup_subsys_state *css,
		struct cftype *cft, u64 val);
u64 uclamp_css_ls_read(struct cgroup_subsys_state *css, struct cftype *cft);

DECLARE_STATIC_KEY_FALSE(sched_uclamp_used);
extern unsigned int sysctl_sched_uclamp_util_min;
extern unsigned int sysctl_sched_uclamp_util_max;
extern unsigned int sysctl_sched_uclamp_util_min_rt_default;

static inline bool uclamp_is_used(void)
{
	return static_branch_likely(&sched_uclamp_used);
}



#else /* !CONFIG_UCLAMP_TASK */

static inline void uclamp_init(void) { }
static inline void uclamp_fork(struct task_struct *p) { }
static inline void uclamp_post_fork(struct task_struct *p) { }
static inline void uclamp_enqueue(struct rq *rq, struct task_struct *p) { }
static inline void uclamp_dequeue(struct rq *rq, struct task_struct *p) { }
static inline void uclamp_refresh_task(struct task_struct *p) { }
static inline int uclamp_validate(struct task_struct *p,
		const struct sched_attr *attr) { return 0; }
static inline void uclamp_setattr(struct task_struct *p,
		const struct sched_attr *attr) { }
static inline unsigned long uclamp_task_util(struct task_struct *p,
		unsigned long util) { return util; }
static inline bool uclamp_latency_sensitive(struct task_struct *p)
{ return false; }
static inline bool uclamp_is_used(void) { return false; }

#endif /* CONFIG_UCLAMP_TASK */

#endif /* _LINUX_SCHED_UCLAMP_H */
