
#include "sched.h"
#include "pelt.h"
#include "linux/list_sort.h"

#define MAX_WEIGHT_WFQ 0xFFFFFFFFFFFFFFFF

void init_wfq_rq(struct wfq_rq *wfq_rq)
{
	wfq_rq->load.weight = 0;
	INIT_LIST_HEAD(&wfq_rq->wfq_rq_list);
	wfq_rq->rq_cpu_runtime = 0;
	wfq_rq->max_weight = 0;
	wfq_rq->nr_running = 0;
}

static int wfq_cmp(void *priv, const struct list_head *a,
					const struct list_head *b)
{
	struct task_struct *ra = list_entry(a, struct task_struct, wfq);
	struct task_struct *rb = list_entry(b, struct task_struct, wfq);

	u64	param1 = MAX_WEIGHT_WFQ/(ra->wfq_weight.weight);
	u64	param2 = MAX_WEIGHT_WFQ/(rb->wfq_weight.weight);
	/*s64	delta = (s64)(param1 - param2);*/
	if (param1 > param2)
		return 1;
	else if (param1 < param2)
		return -1;
	return 0;
}
					
/*
 * Adding/removing a task to/from a priority array:
 */
static void
enqueue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	
	int i;
	u64 min_weight = MAX_WEIGHT_WFQ;
	int min_weight_cpu;
	struct rq *rq_min_cpu;
	
	if (flags & ENQUEUE_WFQ_WEIGHT_UPD) {
		rq_min_cpu = rq;
		rq->wfq.load.weight += p->wfq_weight_change;
	}
	else {
		for_each_possible_cpu(i) {
			struct rq *rq_cpu;
			rq_cpu = cpu_rq(i);
		
			if (min_weight > rq_cpu->wfq.load.weight) {
				min_weight_cpu = i;
				min_weight = rq_cpu->wfq.load.weight;
			}
		}
	
		rq_min_cpu = cpu_rq(min_weight_cpu);
	
		list_add_tail(&p->wfq, &rq_min_cpu->wfq.wfq_rq_list);
		(rq_min_cpu->wfq.nr_running)++;
		add_nr_running(rq_min_cpu, 1);

		rq_min_cpu->wfq.load.weight += p->wfq_weight.weight;
	}
	
	list_sort(NULL, &rq_min_cpu->wfq.wfq_rq_list, wfq_cmp);
	
	if (rq_min_cpu->wfq.max_weight < p->wfq_weight.weight) {
		struct task_struct *first;
		first = list_first_entry(&rq_min_cpu->wfq.wfq_rq_list, struct task_struct, wfq);
		rq_min_cpu->wfq.max_weight = first->wfq_weight.weight;
	}
}

static void dequeue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_struct *first;
	
	list_del(&p->wfq);
	(rq->wfq.nr_running)--;
	sub_nr_running(rq, 1);
	
	rq->wfq.load.weight -= p->wfq_weight.weight;
	
	
	first = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	rq->wfq.max_weight = first->wfq_weight.weight;
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

static void update_curr_wfq(struct rq *rq)
{
	
}

static void put_prev_task_wfq(struct rq *rq, struct task_struct *prev)
{
}

static void set_next_task_wfq(struct rq *rq, struct task_struct *next, bool first)
{
	
}


static void task_tick_wfq(struct rq *rq, struct task_struct *curr, int queued)
{
	if (curr->sched_class != &wfq_sched_class)
		return;
		
	/*Ideally it should be (1/task_weight)*/
	curr->wfq_vruntime += 1;
	
	/*Ideally it should be (1/total_task_weight). 
	 * We count it and wherever required divide */
	rq->wfq.rq_cpu_runtime += 1;
	
	if (rq->wfq.max_weight > curr->wfq_weight.weight)
	{
		resched_curr(rq);
	}
	
}

static void
prio_changed_wfq(struct rq *rq, struct task_struct *p, int oldprio)
{

}

static void switched_to_wfq(struct rq *rq, struct task_struct *p)
{

}

static unsigned int get_rr_interval_wfq(struct rq *rq, struct task_struct *task)
{
	return 0;
}

#ifdef CONFIG_SMP
static int balance_wfq(struct rq *rq, struct task_struct *p, struct rq_flags *rf)
{
	return 0;
}

static int
select_task_rq_wfq(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	return 0;
}

static void rq_online_wfq(struct rq *rq)
{

}

static void rq_offline_wfq(struct rq *rq)
{

}

static void task_woken_wfq(struct rq *rq, struct task_struct *p)
{

}

static void switched_from_wfq(struct rq *rq, struct task_struct *p)
{

}

#endif

const struct sched_class wfq_sched_class
	__section("__wfq_sched_class") = {
	.enqueue_task		= enqueue_task_wfq,
	.dequeue_task		= dequeue_task_wfq,
	.yield_task		= yield_task_wfq,

	.check_preempt_curr	= check_preempt_curr_wfq,

	.pick_next_task		= pick_next_task_wfq,
	.put_prev_task		= put_prev_task_wfq,
	.set_next_task          = set_next_task_wfq,


#ifdef CONFIG_SMP
	.balance		= balance_wfq,
	.select_task_rq		= select_task_rq_wfq,
	.set_cpus_allowed       = set_cpus_allowed_common,
	.rq_online              = rq_online_wfq,
	.rq_offline             = rq_offline_wfq,
	.task_woken		= task_woken_wfq,
	.switched_from		= switched_from_wfq,
	
#endif


	.task_tick		= task_tick_wfq,

	.get_rr_interval	= get_rr_interval_wfq,
	
	.prio_changed		= prio_changed_wfq,
	.switched_to		= switched_to_wfq,

	.update_curr		= update_curr_wfq,

#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
	
#endif

};
