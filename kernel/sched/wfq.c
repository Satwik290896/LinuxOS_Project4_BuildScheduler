#include "sched.h"

#include "pelt.h"

#define MAX_WEIGHT_WFQ 0xFFFFFFFFFFFFFFFF

void init_wfq_rq(struct wfq_rq *wfq_rq)
{
	wfq_rq->load.weight = 0;
	INIT_LIST_HEAD(&rq->wfq);
}

static s64 wfq_cmp(void *priv, const struct list_head *a,
					const struct list_head *b)
{
	struct le_wfq1 *ra = list_entry(a, struct task_struct, wfq);
	struct le_wfq2 *rb = list_entry(b, struct task_struct, wfq);

	u64	param1 = MAX_WEIGHT_WFQ/(ra->wfq_weight);
	u64	param2 = MAX_WEIGHT_WFQ/(rb->wfq_weight);
	s64	delta = (s64)(param1 - param2);
	return delta;
}
					
/*
 * Adding/removing a task to/from a priority array:
 */
static void
enqueue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	list_add_tail(&p->wfq, &rq->wfq.wfq_rq_list);
	p->wfq_vruntime = 0;
	p->wfq_weight = 10;
	rq->wfq.load.weight += p->wfq_weight;
	list_sort(NULL, &rq->wfq.wfq_rq_list, wfq_cmp);
}

static void dequeue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	list_del(&p->wfq);
	rq->wfq.load.weight -= p->wfq_weight;
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
	/*Ideally it should be (1/task_weight)*/
	curr->wfq_vruntime += 1;
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
