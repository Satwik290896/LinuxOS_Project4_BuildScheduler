
#include "sched.h"
#include "pelt.h"
#include "linux/list_sort.h"
#include "linux/math64.h"

#define MAX_WEIGHT_WFQ 0xFFFFFFFFFFFFFFFF
unsigned long next_balance_counter;

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
	s64	delta = (s64)(param1 - param2);
	if (delta > 0)
		return 1;
	else if (delta < 0)
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
	struct rq_flags rf;
	u64 min_weight = MAX_WEIGHT_WFQ;
	int min_weight_cpu;
	struct rq *rq_min_cpu;
		
	if (p->sched_class != &wfq_sched_class)
		return;	

	if (flags & ENQUEUE_WFQ_ADD_EXACT) {
		/* add p to this rq, rather than the rq with lowest total weight */
		rq_min_cpu = rq;

		list_add_tail(&p->wfq, &rq_min_cpu->wfq.wfq_rq_list);
		(rq_min_cpu->wfq.nr_running)++;
		add_nr_running(rq_min_cpu, 1);

		rq_min_cpu->wfq.load.weight += p->wfq_weight.weight;
	} else if (flags & ENQUEUE_WFQ_WEIGHT_UPD) {
		rq_min_cpu = rq;
		rq->wfq.load.weight += p->wfq_weight_change;
	} else {
		for_each_possible_cpu(i) {
			struct rq *rq_cpu;
			rq_cpu = cpu_rq(i);
			
			if (rq_cpu != rq)
				rq_lock(rq_cpu, &rf);
	
			if (min_weight > rq_cpu->wfq.load.weight) {
				min_weight_cpu = i;
				min_weight = rq_cpu->wfq.load.weight;
			}
			
			if (rq_cpu != rq)
				rq_unlock(rq_cpu, &rf);
		}
	
		rq_min_cpu = cpu_rq(min_weight_cpu);
	
		if (rq_min_cpu != rq)
			rq_lock(rq_min_cpu, &rf);
			
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
	
	if (rq_min_cpu != rq)
		rq_unlock(rq_min_cpu, &rf);
}




static void dequeue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_struct *first;
	
		
	if (p->sched_class != &wfq_sched_class)
		return;	

	list_del(&p->wfq);
	(rq->wfq.nr_running)--;
	sub_nr_running(rq, 1);
	
	rq->wfq.load.weight -= p->wfq_weight.weight;
	
	
	first = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	rq->wfq.max_weight = first->wfq_weight.weight;
}

static void yield_task_wfq(struct rq *rq)
{
	return;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	if (p->sched_class != &wfq_sched_class)
		return;	
	if (rq->wfq.max_weight > p->wfq_weight.weight)
	{
		resched_curr(rq);
	}
}


static struct task_struct *pick_next_task_wfq(struct rq *rq)
{
	struct task_struct *p;
	
		
	if (rq->wfq.nr_running < 1)
		return NULL;

	p = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	return p;
}

static void update_curr_wfq(struct rq *rq)
{
	return;	
}

static void put_prev_task_wfq(struct rq *rq, struct task_struct *prev)
{
	return;
}

static void set_next_task_wfq(struct rq *rq, struct task_struct *next, bool first)
{
	return;	
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
	return;
}

static void switched_to_wfq(struct rq *rq, struct task_struct *p)
{
	/*Switched from other run queue to here*/
	return;
}

static unsigned int get_rr_interval_wfq(struct rq *rq, struct task_struct *task)
{
	return 0;
}

static int load_balance_wfq(void)
{
	struct rq_flags rf;
	struct rq *rq, *max_rq, *min_rq;
	int i;
	unsigned long max_weight = 0, min_weight =  MAX_WEIGHT_WFQ;
	struct task_struct *curr, *stolen_task;
	int found_eligible = 0, this_cpu_idx = 0;

	/* find the CPU with greatest total weight */
	for_each_possible_cpu(i) {
		rq = cpu_rq(i);
		
		rq_lock_irq(rq, &rf);
		if ((rq->wfq.load.weight > max_weight) && (rq->wfq.nr_running >= 2)) {
			max_weight = rq->wfq.load.weight;
			max_rq = rq;
		}
		if ((rq->wfq.load.weight < min_weight) && (rq->wfq.nr_running >= 2)) {
			min_weight = rq->wfq.load.weight;
			min_rq = rq;
		}
		rq_unlock_irq(rq, &rf);

		if (!max_weight)
			return 1;

		rq_lock_irq(max_rq, &rf);
		/* iterate over max_rq to get an eligible task */
		list_for_each_entry(curr, &(max_rq->wfq.wfq_rq_list), wfq) {
			if (curr->sched_class != &wfq_sched_class)
				continue;
			if (kthread_is_per_cpu(curr))
				continue;
			if (!cpumask_test_cpu(this_cpu_idx, curr->cpus_ptr))
				continue;
			if (task_running(max_rq, curr))
				continue;

			stolen_task = curr;
			found_eligible = 1;
			break;
		}
		if(!found_eligible){
			rq_unlock_irq(max_rq, &rf);
			return 1;
		}
		/* add the stolen_task to rq with the lowest weight */
		dequeue_task_wfq(max_rq, stolen_task, 0);
		rq_unlock(max_rq, &rf);
		enqueue_task_wfq(min_rq, stolen_task, ENQUEUE_WFQ_ADD_EXACT);
	}
	return 0;
}

/*
 * Trigger the SCHED_SOFTIRQ if it is time to do periodic load balancing.
 */
void trigger_load_balance_wfq(void)
{
	unsigned long interval = msecs_to_jiffies(500);
	if(!next_balance_counter)
		next_balance_counter = jiffies;
	// printk(KERN_WARNING "next_balance_counter: %lu\n", next_balance_counter);
	if(time_after_eq(jiffies, next_balance_counter))
		raise_softirq(SCHED_SOFTIRQ);
	load_balance_wfq();
	// nohz_balancer_kick(rq);
	next_balance_counter += interval;
}

#ifdef CONFIG_SMP
/* idle load balancing implementation */
static int balance_wfq(struct rq *rq, struct task_struct *p, struct rq_flags *rf)
{
	unsigned long max_weight = 0;
	int i;
	int this_cpu_idx = 0;
	int max_cpu_idx = 0;
	int found_swappable_rq = 0;
	struct rq *max_rq;
	struct rq_flags rf_max;
	int found_eligible = 0;
	struct task_struct *curr;
	struct task_struct *stolen_task;

	if (p->sched_class != &wfq_sched_class)
		return 1;

	if (rq->wfq.nr_running != 0)
		return 1;

	/* find the CPU with greatest total weight */
	for_each_possible_cpu(i) {
		/* struct rq_flags rf_tmp; */
		struct rq *rq_cpu = cpu_rq(i);
		if (rq_cpu == rq) {
			this_cpu_idx = i;
			continue;
		}

		/* since the spec says that the weights used here can
		 * be an estimate, and we're not modifying the RQ in
		 * this step, we can do this without using a lock. */
		/* rq_lock(rq_cpu, &rf_tmp); */
		if ((rq_cpu->wfq.load.weight > max_weight) && (rq_cpu->wfq.nr_running >= 2)) {
			found_swappable_rq = 1;
			max_cpu_idx = i;
			max_weight = rq_cpu->wfq.load.weight;
			max_rq = rq_cpu;
		}
		/* rq_unlock(rq_cpu, &rf_tmp); */
	}

	/* no CPUs with greater weight and at least 2 tasks */
	if (found_swappable_rq == 0)
		return 1;

	rq_lock(max_rq, &rf_max);
	/* iterate over max_rq to get an eligible task */
	list_for_each_entry(curr, &(max_rq->wfq.wfq_rq_list), wfq) {
		if (curr->sched_class != &wfq_sched_class)
			continue;
		if (kthread_is_per_cpu(curr))
			continue;
		if (!cpumask_test_cpu(this_cpu_idx, curr->cpus_ptr))
			continue;
		if (task_running(max_rq, curr))
			continue;

		stolen_task = curr;
		found_eligible = 1;
		break;
	}

	if (found_eligible == 0) {
		rq_unlock(max_rq, &rf_max);
		return 1;
	}

	dequeue_task_wfq(max_rq, stolen_task, 0);
	rq_unlock(max_rq, &rf_max);
	enqueue_task_wfq(rq, stolen_task, ENQUEUE_WFQ_ADD_EXACT);

	return 0;
}

static int
select_task_rq_wfq(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	return cpu;
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
	/*Switched to other run queue from here*/
	return;
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
