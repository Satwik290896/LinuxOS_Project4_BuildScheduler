#include "sched.h"
#include "pelt.h"
#include "linux/list_sort.h"
#include "linux/math64.h"
#include "linux/types.h"

#define MAX_WEIGHT_WFQ	0xFFFFFFFFFFFFFFFF
#define MIN_VFT_INIT	0xFFFFFFFFFFFFFFFF
#define MAX_VALUE	0xFFFFFFFFFFFFFFFF
#define SCALING_FACTOR	0xFFFFFF
atomic_long_t next_balance_counter = ATOMIC_INIT(0);
DEFINE_SPINLOCK(mLock);
bool is_periodic_balance_req = false;
bool is_pick_next_last_pick = false;


DEFINE_SPINLOCK(min_max_lock);
int highest_weight_cpu = 0;
u64 highest_weight_among_cpus = 0;
int s_highest_weight_cpu = 1;
u64 s_highest_weight_among_cpus = 0;
int lowest_weight_cpu = 0;
u64 lowest_weight_among_cpus = 0;
int s_lowest_weight_cpu = 1;
u64 s_lowest_weight_among_cpus = 0;

DEFINE_SPINLOCK(min_max_lock_v2);
int D_lowest_weight_cpu = 0;
u64 D_lowest_weight_among_cpus = 0;
int D_s_lowest_weight_cpu = 1;
u64 D_s_lowest_weight_among_cpus = 0;

void init_wfq_rq(struct wfq_rq *wfq_rq)
{
	wfq_rq->load.weight = 0;
	INIT_LIST_HEAD(&wfq_rq->wfq_rq_list);
	wfq_rq->rq_cpu_runtime = 0;
	wfq_rq->max_weight = MIN_VFT_INIT;
	wfq_rq->nr_running = 0;
	wfq_rq->curr = NULL;
}

static u64 find_vft(struct task_struct *p) {
	u64 p_vft = p->wfq_vruntime + (SCALING_FACTOR/(p->wfq_weight.weight));
	
	return p_vft;
}
static int wfq_cmp(void *priv, const struct list_head *a,
					const struct list_head *b)
{
	struct task_struct *ra = list_entry(a, struct task_struct, wfq);
	struct task_struct *rb = list_entry(b, struct task_struct, wfq);

	u64	param1 = find_vft(ra);
	u64	param2 = find_vft(rb);
	s64	delta = (s64)(param1 - param2);
	if (delta > 0)
		return 1;
	else if (delta < 0)
		return -1;
	return 0;
}

/*Name is confusing, update_max_weight updates the minimum VFT of this wfq_rq*/
static bool 
update_max_weight (struct rq *rq, struct task_struct *p)
{
	u64 p_vft = find_vft(p);
	u64 min_vft = rq->wfq.max_weight;
	bool upd_happened = false;
	
	if (min_vft > p_vft) {
		rq->wfq.max_weight = p_vft;
		upd_happened = true;
	}
	
	return upd_happened;
}

static void D_swap(void) {
	int temp_cpu;
	u64 temp_weight;

	if (D_lowest_weight_among_cpus > D_s_lowest_weight_among_cpus) {
			temp_cpu = D_lowest_weight_cpu;
			temp_weight = D_lowest_weight_among_cpus;
	
			D_lowest_weight_cpu = D_s_lowest_weight_cpu;
			D_lowest_weight_among_cpus = D_s_lowest_weight_among_cpus;
		
			D_s_lowest_weight_cpu = temp_cpu;
			D_s_lowest_weight_among_cpus = temp_weight;
	}
}

static void D_reorder_l(int r, int this_cpu, u64 this_weight) {
	int temp_cpu;
	u64 temp_weight;
	
	if (r == 1) {
		temp_cpu = D_lowest_weight_cpu;
		temp_weight = D_lowest_weight_among_cpus;
		
		D_lowest_weight_cpu = this_cpu;
		D_lowest_weight_among_cpus = this_weight;

		D_s_lowest_weight_cpu = temp_cpu;
		D_s_lowest_weight_among_cpus = temp_weight;		
		
	} else if (r == 2) {
		D_s_lowest_weight_cpu = this_cpu;
		D_s_lowest_weight_among_cpus = this_weight;		
	}
}
		
static void update_dMax(struct rq *rq) {
	int this_cpu = rq->cpu;
	u64 this_cpu_wfq_weight = rq->wfq.load.weight;

	/*Lowest weight logic*/
	if ((D_lowest_weight_cpu == this_cpu) || (D_s_lowest_weight_cpu == this_cpu)) {
		if (D_lowest_weight_cpu == this_cpu) {
			D_lowest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		if (D_s_lowest_weight_cpu == this_cpu) {
			D_s_lowest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		D_swap();
	} else {
		if (D_lowest_weight_among_cpus < this_cpu_wfq_weight) {
			D_reorder_l(1, this_cpu, this_cpu_wfq_weight);
		} else {
			if (D_s_lowest_weight_among_cpus < this_cpu_wfq_weight) {
				D_reorder_l(2, this_cpu, this_cpu_wfq_weight);
			}
		}	
	}

}

static void swap_hl(int s) {
	int temp_cpu;
	u64 temp_weight;

	if (s == 1) {
		if (highest_weight_among_cpus < s_highest_weight_among_cpus) {
			temp_cpu = highest_weight_cpu;
			temp_weight = highest_weight_among_cpus;
	
			highest_weight_cpu = s_highest_weight_cpu;
			highest_weight_among_cpus = s_highest_weight_among_cpus;
		
			s_highest_weight_cpu = temp_cpu;
			s_highest_weight_among_cpus = temp_weight;
		}
	} else if (s == 0) {
		if (lowest_weight_among_cpus > s_lowest_weight_among_cpus) {
			temp_cpu = lowest_weight_cpu;
			temp_weight = lowest_weight_among_cpus;
	
			lowest_weight_cpu = s_lowest_weight_cpu;
			lowest_weight_among_cpus = s_lowest_weight_among_cpus;
		
			s_lowest_weight_cpu = temp_cpu;
			s_lowest_weight_among_cpus = temp_weight;
		}	
	}
}


static void reorder_h(int r, int this_cpu, u64 this_weight) {
	int temp_cpu;
	u64 temp_weight;
	
	if (r == 1) {
		temp_cpu = highest_weight_cpu;
		temp_weight = highest_weight_among_cpus;
		
		highest_weight_cpu = this_cpu;
		highest_weight_among_cpus = this_weight;

		s_highest_weight_cpu = temp_cpu;
		s_highest_weight_among_cpus = temp_weight;		
		
	} else if (r == 2) {
		s_highest_weight_cpu = this_cpu;
		s_highest_weight_among_cpus = this_weight;		
	}
}

static void reorder_l(int r, int this_cpu, u64 this_weight) {
	int temp_cpu;
	u64 temp_weight;
	
	if (r == 1) {
		temp_cpu = lowest_weight_cpu;
		temp_weight = lowest_weight_among_cpus;
		
		lowest_weight_cpu = this_cpu;
		lowest_weight_among_cpus = this_weight;

		s_lowest_weight_cpu = temp_cpu;
		s_lowest_weight_among_cpus = temp_weight;		
		
	} else if (r == 2) {
		s_lowest_weight_cpu = this_cpu;
		s_lowest_weight_among_cpus = this_weight;		
	}
}

	
static void update_min_max_cpu_buf(struct rq *rq)
{
	int this_cpu = rq->cpu;
	u64 this_cpu_wfq_weight = rq->wfq.load.weight;

	/*Highest weight logic*/
	if ((highest_weight_cpu == this_cpu) || (s_highest_weight_cpu == this_cpu)) {
		if (highest_weight_cpu == this_cpu) {
			highest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		if (s_highest_weight_cpu == this_cpu) {
			s_highest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		swap_hl(1);
	} else {
		if (highest_weight_among_cpus < this_cpu_wfq_weight) {
			reorder_h(1, this_cpu, this_cpu_wfq_weight);
		} else {
			if (s_highest_weight_among_cpus < this_cpu_wfq_weight) {
				reorder_h(2, this_cpu, this_cpu_wfq_weight);
			}
		}	
	}
	
	/*Lowest weight logic*/
	if ((lowest_weight_cpu == this_cpu) || (s_lowest_weight_cpu == this_cpu)) {
		if (lowest_weight_cpu == this_cpu) {
			lowest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		if (s_lowest_weight_cpu == this_cpu) {
			s_lowest_weight_among_cpus = this_cpu_wfq_weight;
		}
		
		swap_hl(0);
	} else {
		if (lowest_weight_among_cpus > this_cpu_wfq_weight) {
			reorder_l(1, this_cpu, this_cpu_wfq_weight);
		} else {
			if (s_lowest_weight_among_cpus > this_cpu_wfq_weight) {
				reorder_l(2, this_cpu, this_cpu_wfq_weight);
			}
		}	
	}	
	
	
	
	
}
/*
 * Adding/removing a task to/from a priority array:
 */
static void
enqueue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	
	bool upd_happened = false;
	unsigned long flags_2 = 0;
		
	if (p->sched_class != &wfq_sched_class)
		return;	

	is_periodic_balance_req = true;
	if (flags & ENQUEUE_WFQ_ADD_EXACT) {
		/* add p to this rq, rather than the rq with lowest total weight */

		list_add_tail(&p->wfq, &rq->wfq.wfq_rq_list);
		
		p->wfq_vruntime = rq->wfq.rq_cpu_runtime;
		
		/*Doing this before incrementing*/
		if (rq->wfq.nr_running >= 1)
			upd_happened = update_max_weight(rq, p);
		else {
			rq->wfq.max_weight = find_vft(p);
			rq->wfq.curr = p;
		}

		(rq->wfq.nr_running)++;
		add_nr_running(rq, 1);

		rq->wfq.load.weight += p->wfq_weight.weight;
		
	} else if (flags & ENQUEUE_WFQ_WEIGHT_UPD) {
	
		rq->wfq.load.weight += p->wfq_weight_change;
		
		/*=1 means only *p is in queue; =0 will not happen*/
		if (rq->wfq.nr_running == 1) {
			rq->wfq.max_weight = find_vft(p);
			rq->wfq.curr = p;
		}
		else if (rq->wfq.nr_running > 1)
			upd_happened = update_max_weight(rq, p);
			
		
	} else {

		list_add_tail(&p->wfq, &rq->wfq.wfq_rq_list);
		p->wfq_vruntime = rq->wfq.rq_cpu_runtime;
		
		/*Doing this before incrementing*/
		if (rq->wfq.nr_running >= 1)
			upd_happened = update_max_weight(rq, p);
		else {
			rq->wfq.max_weight = find_vft(p);
			rq->wfq.curr = p;
		}

		(rq->wfq.nr_running)++;
		add_nr_running(rq, 1);

		rq->wfq.load.weight += p->wfq_weight.weight;
	}
	
	//list_sort(NULL, &rq->wfq.wfq_rq_list, wfq_cmp);
	if (upd_happened) {
		rq->wfq.curr = p;
	}
	
	/* required
	 * list_sort(NULL, &rq->wfq.wfq_rq_list, wfq_cmp);*/
	
	spin_lock_irqsave_nested(&min_max_lock, flags_2, SINGLE_DEPTH_NESTING);
	if (rq->wfq.nr_running >= 2) {
		update_min_max_cpu_buf(rq);
		update_dMax(rq);
	} else {
		update_dMax(rq);
	}
	spin_unlock_irqrestore(&min_max_lock, flags_2);
}




static void dequeue_task_wfq(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_struct *first;
	unsigned long flags_2 = 0;
	
		
	if (p->sched_class != &wfq_sched_class)
		return;	

	if (rq->wfq.nr_running == 0)
		return;

	is_periodic_balance_req = true;
	list_del(&p->wfq);
	(rq->wfq.nr_running)--;
	sub_nr_running(rq, 1);
	
	rq->wfq.load.weight -= p->wfq_weight.weight;
	
	if (rq->wfq.nr_running >= 1) {
		if ((p == rq->wfq.curr) || (rq->wfq.max_weight == find_vft(p))) {
			list_sort(NULL, &rq->wfq.wfq_rq_list, wfq_cmp);
			first = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
			rq->wfq.max_weight = find_vft(first);
			rq->wfq.curr = first;
		}
	} else {
		rq->wfq.max_weight = MIN_VFT_INIT;
		rq->wfq.curr = NULL;
	}
	
	spin_lock_irqsave_nested(&min_max_lock, flags_2, SINGLE_DEPTH_NESTING);
	if (rq->wfq.nr_running >= 2) {
		update_min_max_cpu_buf(rq);
		update_dMax(rq);
	} else {
		update_dMax(rq);
	}
	spin_unlock_irqrestore(&min_max_lock, flags_2);
	
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
	if ((rq->wfq.max_weight < find_vft(p))  && (rq->wfq.curr != p))
	{
		resched_curr(rq);
	}
}


static struct task_struct *pick_next_task_wfq(struct rq *rq)
{
	struct task_struct *p;
	
		
	if (rq->wfq.nr_running < 1)
		return NULL;

	list_sort(NULL, &rq->wfq.wfq_rq_list, wfq_cmp);
	
	if (!is_pick_next_last_pick) {
		p = list_first_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	
		/*safety*/
		rq->wfq.curr = p;
		rq->wfq.max_weight = find_vft(p);
	} else {
		p = list_last_entry(&rq->wfq.wfq_rq_list, struct task_struct, wfq);
	}
	
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
	u64 new_vruntime_val;
	u64 new_cpuruntime_val;
	u64 diff;
	u64 temp;
	if (curr->sched_class != &wfq_sched_class)
		return;

	if (rq->wfq.nr_running < 1)
		return;

	/*Ideally it should be (1/task_weight)*/
	new_vruntime_val = (SCALING_FACTOR)/(curr->wfq_weight.weight);
	diff = MAX_VALUE - curr->wfq_vruntime;
	
	if (diff > new_vruntime_val)
		curr->wfq_vruntime += new_vruntime_val;
	else {
		temp = new_vruntime_val - diff;
		curr->wfq_vruntime = temp;
	}
	
	/*Ideally it should be (1/total_task_weight). 
	 * We count it and wherever required divide */
	new_cpuruntime_val = (SCALING_FACTOR)/(rq->wfq.load.weight);
	diff = MAX_VALUE - rq->wfq.rq_cpu_runtime;
	
	if (diff > new_cpuruntime_val)
		rq->wfq.rq_cpu_runtime += new_cpuruntime_val;
	else {
		temp = new_cpuruntime_val - diff;
		rq->wfq.rq_cpu_runtime = temp;	
	}	
	
	
	if ((rq->wfq.max_weight < find_vft(curr)) && (rq->wfq.curr != curr))
	{
		resched_curr(rq);
	}
	else {
		/*Curent has minimum VFT, needs to update for every task_tick*/
		rq->wfq.max_weight = find_vft(curr);
		/*safety*/
		rq->wfq.curr = curr;
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

bool is_cpu_allowed_wfq(struct task_struct *p, int cpu)
{
	if (!cpumask_test_cpu(cpu, p->cpus_ptr))
		return false;

	if (is_per_cpu_kthread(p))
		return cpu_online(cpu);

	return cpu_active(cpu);
}

struct rq *move_queued_task_wfq(struct rq *rq, struct rq_flags *rf,
				   struct task_struct *p, int new_cpu)
{
	lockdep_assert_held(&rq->lock);

	deactivate_task(rq, p, DEQUEUE_NOCLOCK);
	set_task_cpu(p, new_cpu);
	rq_unlock(rq, rf);

	rq = cpu_rq(new_cpu);

	rq_lock(rq, rf);
	BUG_ON(task_cpu(p) != new_cpu);
	activate_task(rq, p, 0);
	check_preempt_curr(rq, p, 0);

	return rq;
}

struct rq *migrate_task_wfq(struct rq *rq, struct rq_flags *rf,
				 struct task_struct *p, int dest_cpu)
{
	/* Affinity changed (again). */
	if (!is_cpu_allowed_wfq(p, dest_cpu))
		return rq;

	update_rq_clock(rq);
	rq = move_queued_task_wfq(rq, rf, p, dest_cpu);

	return rq;
}

static __latent_entropy void load_balance_wfq(struct softirq_action *h)
{
	struct rq_flags *rf;
	struct rq *rq, *max_rq, *min_rq, *temp_rq;
	int i;
	unsigned long max_weight = 0, min_weight =  MAX_WEIGHT_WFQ;
	struct task_struct *curr, *stolen_task;
	int found_eligible = 0, this_cpu_idx = 0;
	int min_cpu, max_cpu;
	unsigned long flags = 0;
	
	if (!is_periodic_balance_req)
		return;
	

	//spin_lock_irqsave(&min_max_lock, flags);
	min_cpu = lowest_weight_cpu;
	max_cpu = highest_weight_cpu;
	max_rq = cpu_rq(max_cpu);
	min_rq = cpu_rq(min_cpu);
	
	/* no valid cpu found */
	if (max_cpu == min_cpu) {
		return;
	}

	rq_lock(max_rq, rf);
	is_pick_next_last_pick = true;
	stolen_task = pick_next_task_wfq(max_rq);
	is_pick_next_last_pick = false;
	
	rq_unlock(max_rq, rf);
	raw_spin_lock(&stolen_task->pi_lock);
	rq_relock(max_rq, rf);
	max_weight = max_rq->wfq.load.weight;
	
	if ((max_weight == 0) || (!task_on_rq_queued(stolen_task))) {
		rq_unlock(max_rq, rf);
		raw_spin_unlock(&stolen_task->pi_lock);
		return;
	}
	
	temp_rq = migrate_task_wfq(max_rq, rf, stolen_task, min_cpu);
	
	rq_unlock(temp_rq, rf);
	raw_spin_unlock(&stolen_task->pi_lock);

	is_periodic_balance_req = false;
}

/*
 * Trigger the SCHED_WFQ_SOFTIRQ if it is time to do periodic load balancing.
 */
void trigger_load_balance_wfq(struct rq *rq)
{
	unsigned long next_balance = jiffies + msecs_to_jiffies(500); 
	unsigned long flags = 0;

	if(!atomic_long_read(&next_balance_counter))
		atomic_long_set(&next_balance_counter, next_balance);
	/* 	grab a lock and check condition
		if true update next_balance, release lock
		then do load balance */
	spin_lock_irqsave(&mLock, flags);
	if (time_after_eq(jiffies, (unsigned long)atomic_long_read(&next_balance_counter))){
		atomic_long_set(&next_balance_counter, next_balance);
		spin_unlock_irqrestore(&mLock, flags);
		raise_softirq(SCHED_WFQ_SOFTIRQ);
	}else{
		spin_unlock_irqrestore(&mLock, flags);
	}
}

#ifdef CONFIG_SMP
/* idle load balancing implementation */
static int balance_wfq(struct rq *rq, struct task_struct *p, struct rq_flags *rf)
{
	/*unsigned long max_weight = 0;
	unsigned long max_weight_2 = 0;
	int i;
	int this_cpu_idx = 0;
	int max_cpu_idx = 0;
	int max_cpu_idx_2 = 0;
	int found_swappable_rq = 0;
	struct rq *max_rq;
	int found_eligible = 0;
	struct task_struct *curr;
	struct task_struct *stolen_task;
	int this_cpu = rq->cpu;
	unsigned long flags;
		
	if (!cpu_active(this_cpu))
		return 0;
		
	if (p->sched_class != &wfq_sched_class)
		return 0;

	if (rq->wfq.nr_running != 0)
		return 1;
	*/
	/*for_each_online_cpu(i) {
		struct rq *rq_cpu = cpu_rq(i);
		if (rq_cpu == rq) {
			this_cpu_idx = i;
			continue;
		}

		double_lock_balance(rq, rq_cpu);
		if ((rq_cpu->wfq.load.weight > max_weight) && (rq_cpu->wfq.nr_running >= 2)) {
			found_swappable_rq = 1;
			max_cpu_idx = i;
			max_weight = rq_cpu->wfq.load.weight;
			max_rq = rq_cpu;
		}
		double_unlock_balance(rq, rq_cpu);
	}*/


	/*spin_lock_irqsave(&min_max_lock, flags);
	max_cpu_idx = highest_weight_cpu;
	max_weight = highest_weight_among_cpus;
	max_cpu_idx_2 = highest_weight_cpu;
	max_weight_2 = highest_weight_among_cpus;
	
	
	if ((max_weight == 0) || (max_cpu_idx == rq->cpu) ) {
		if ((max_weight_2 == 0) || (max_cpu_idx_2 == rq->cpu) )
			found_swappable_rq = 0;
		else {
			max_weight = max_weight_2;
			max_cpu_idx = max_cpu_idx_2;
			found_swappable_rq = 1;
		}
	}
		
	if (found_swappable_rq == 0) {
		spin_unlock_irqrestore(&min_max_lock, flags);
		return 0;
	}
	else {
		max_rq = cpu_rq(max_cpu_idx);
	}
	spin_unlock_irqrestore(&min_max_lock, flags);
		
	double_lock_balance(rq, max_rq);
	*/
	/* no valid cpu found */
	//rcu_read_lock();

	/*if (max_rq->wfq.nr_running < 2) {
		double_unlock_balance(rq, max_rq);
		rcu_read_unlock();
		return 0;
	}

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
		double_unlock_balance(rq, max_rq);
		rcu_read_unlock();
		return 0;
	}

	deactivate_task(max_rq, stolen_task, 0);
	set_task_cpu(stolen_task, this_cpu_idx);
	activate_task(rq, stolen_task, 0);
	resched_curr(rq);
	rcu_read_unlock();
	double_unlock_balance(rq, max_rq);
	*/

	return 0;
}

static int
select_task_rq_wfq(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	/*int i;
	struct rq_flags rf;
	u64 min_weight = MAX_WEIGHT_WFQ;
	int min_weight_cpu = cpu;
	struct rq *rq_cpu;
	
	for_each_online_cpu(i) {
		
		rq_cpu = cpu_rq(i);
		rq_lock(rq_cpu, &rf);

		if (min_weight > rq_cpu->wfq.load.weight) {
			min_weight_cpu = i;
			min_weight = rq_cpu->wfq.load.weight;
		}

		rq_unlock(rq_cpu, &rf);
	}*/
	int min_cpu;
	unsigned long flags_2 = 0;
	spin_lock_irqsave(&min_max_lock, flags_2);
	min_cpu = D_lowest_weight_cpu;
	spin_unlock_irqrestore(&min_max_lock, flags_2);
	printk("[select_task] cpu: %d\n", min_cpu);
	return min_cpu;
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

__init void init_sched_wfq_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_WFQ_SOFTIRQ, load_balance_wfq);
#endif /* SMP */
}
