#include "sched.h"

#include "pelt.h"


/*
 * Adding/removing a task to/from a priority array:
 */
static void
enqueue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	list_add_tail(&p->wfq, &rq->wfq.wfq_rq_list);
}

static void dequeue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	list_del(&p->wfq);
}

static void yield_task_wfq(struct rq *rq)
{

}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_wfq(struct rq *rq, struct task_struct *p, int flags)
{

}


static struct task_struct *pick_next_task_wfq(struct rq *rq)
{
	struct task_struct *p;
	p = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	return p;
}

static void put_prev_task_wfq(struct rq *rq, struct task_struct *prev)
{
}

static void set_next_task_wfq(struct rq *rq, struct task_struct *next, bool first)
{

}


static void task_tick_wfq(struct rq *rq, struct task_struct *curr, int queued)
{
}

static void
prio_changed_wfq(struct rq *rq, struct task_struct *p, int oldprio)
{

}

static void switched_to_wfq(struct rq *rq, struct task_struct *p)
{

}

static void update_curr_wfq(struct rq *rq)
{
}



const struct sched_class wfq_sched_class
	__section("__wfq_sched_class") = {
	.enqueue_task		= enqueue_task_wfq,
	.dequeue_task		= dequeue_task_wfq,
	.yield_task		= yield_task_wfq,

	.check_preempt_curr	= check_preempt_curr_wfq,

	.pick_next_task		= pick_next_task_wfq,
	.put_prev_task		= put_prev_task_wfq,
	.set_next_task          = set_next_task_wfq,

/*
#ifdef CONFIG_SMP
	.balance		= balance_wfq,
	.select_task_rq		= select_task_rq_wfq,
	.set_cpus_allowed       = set_cpus_allowed_common,
	.rq_online              = rq_online_wfq,
	.rq_offline             = rq_offline_wfq,
	.task_woken		= task_woken_wfq,
	.switched_from		= switched_from_wfq,
	
#endif
*/

	.task_tick		= task_tick_wfq,

	/*.get_rr_interval	= get_rr_interval_wfq,
	*/
	.prio_changed		= prio_changed_wfq,
	.switched_to		= switched_to_wfq,

	.update_curr		= update_curr_wfq,

/*
#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
	
#endif
*/
};
