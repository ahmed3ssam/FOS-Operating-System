#include <inc/assert.h>
#include <kern/sched.h>
#include <kern/user_environment.h>
#include <kern/memory_manager.h>
#include <kern/command_prompt.h>
#include <kern/trap.h>
#include <kern/kheap.h>
#include <kern/utilities.h>
extern uint32 isBufferingEnabled();
extern void cleanup_buffers(struct Env* e);
extern inline uint32 pd_is_table_used(struct Env *e, uint32 virtual_address);
extern inline void pd_set_table_unused(struct Env *e, uint32 virtual_address);
extern inline void pd_clear_page_dir_entry(struct Env *e, uint32 virtual_address);
void sched_delete_ready_queues() ;
uint32 isSchedMethodRR(){if(scheduler_method == SCH_RR) return 1; return 0;}
uint32 isSchedMethodMLFQ(){if(scheduler_method == SCH_MLFQ) return 1; return 0;}
void init_queue(struct Env_Queue* queue)
{
	if(queue != NULL)
	{
		LIST_INIT(queue);
	}
}
int queue_size(struct Env_Queue* queue)
{
	if(queue != NULL)
	{
		return LIST_SIZE(queue);
	}
	else
	{
		return 0;
	}
}
void enqueue(struct Env_Queue* queue, struct Env* env)
{
	if(env != NULL)
	{
		LIST_INSERT_HEAD(queue, env);
	}
}
struct Env* dequeue(struct Env_Queue* queue)
{
	struct Env* envItem = LIST_LAST(queue);
	if (envItem != NULL)
	{
		LIST_REMOVE(queue, envItem);
	}
	return envItem;
}
void remove_from_queue(struct Env_Queue* queue, struct Env* e)
{
	if (e != NULL)
	{
		LIST_REMOVE(queue, e);
	}
}
struct Env* find_env_in_queue(struct Env_Queue* queue, uint32 envID)
{
	struct Env * ptr_env=NULL;
	LIST_FOREACH(ptr_env, queue)
	{
		if(ptr_env->env_id == envID)
		{
			return ptr_env;
		}
	}
	return NULL;
}
int cur_env_lev = 0;
void sched_init_MLFQ(uint8 numOfLevels, uint8 *quantumOfEachLevel)
{
    sched_delete_ready_queues();
    scheduler_status = SCH_STOPPED;
    scheduler_method = SCH_MLFQ;
    env_ready_queues = kmalloc(sizeof(struct Env_Queue) * num_of_ready_queues);
    quantums = kmalloc(num_of_ready_queues * sizeof(uint8)) ;
    for (int i = 0; i < num_of_ready_queues; i++) {
        quantums[i] = quantumOfEachLevel[i];
        init_queue(&(env_ready_queues[i]));
    }
    kclock_set_quantum(quantums[0]);
}
struct Env* fos_scheduler_MLFQ()
{
    if (curenv != NULL)
    {
        int next_lev = cur_env_lev + 1;
        if (next_lev == num_of_ready_queues)
            next_lev = num_of_ready_queues - 1;
        enqueue(&(env_ready_queues[next_lev]), curenv);
    }
    for (int i=0; i < num_of_ready_queues; i++) {
        struct Env* env = dequeue(&(env_ready_queues[i]));
        if (env != NULL) {
            cur_env_lev = i;
            kclock_set_quantum(quantums[i]);
            return env;
        }
    }
    return NULL;
}
void fos_scheduler(void)
{
	chk1();
	scheduler_status = SCH_STARTED;
	struct Env* next_env = NULL;
	if (scheduler_method == SCH_RR)
	{
		if (curenv != NULL)
		{
			enqueue(&(env_ready_queues[0]), curenv);
		}
		next_env = dequeue(&(env_ready_queues[0]));
		kclock_set_quantum(quantums[0]);
	}
	else if (scheduler_method == SCH_MLFQ)
	{
		next_env = fos_scheduler_MLFQ();
	}
	struct Env* old_curenv = curenv;
	curenv = next_env ;
	chk2(next_env);
	curenv = old_curenv;
	if(next_env != NULL)
	{
		env_run(next_env);
	}
	else
	{
		curenv = NULL;
		lcr3(phys_page_directory);
		scheduler_status = SCH_STOPPED;
		while (1)
			run_command_prompt(NULL);
	}
}
void sched_init_RR(uint8 quantum)
{
	sched_delete_ready_queues();
	scheduler_status = SCH_STOPPED;
	scheduler_method = SCH_RR;
	num_of_ready_queues = 1;
	env_ready_queues = kmalloc(sizeof(struct Env_Queue));
	quantums = kmalloc(num_of_ready_queues * sizeof(uint8)) ;
	quantums[0] = quantum;
	kclock_set_quantum(quantums[0]);
	init_queue(&(env_ready_queues[0]));
}
void sched_init()
{
	old_pf_counter = 0;
	sched_init_RR(CLOCK_INTERVAL_IN_MS);
	init_queue(&env_new_queue);
	init_queue(&env_exit_queue);
}
void sched_delete_ready_queues()
{
	if (env_ready_queues != NULL)
		kfree(env_ready_queues);
	if (quantums != NULL)
	kfree(quantums);
}
void sched_insert_ready(struct Env* env)
{
	if(env != NULL)
	{
		env->env_status = ENV_READY ;
		enqueue(&(env_ready_queues[0]), env);
	}
}
void sched_remove_ready(struct Env* env)
{
	if(env != NULL)
	{
		for (int i = 0 ; i < num_of_ready_queues ; i++)
		{
			struct Env * ptr_env = find_env_in_queue(&(env_ready_queues[i]), env->env_id);
			if (ptr_env != NULL)
			{
				LIST_REMOVE(&(env_ready_queues[i]), env);
				env->env_status = ENV_UNKNOWN;
				return;
			}
		}
	}
}
void sched_insert_new(struct Env* env)
{
	if(env != NULL)
	{
		env->env_status = ENV_NEW ;
		enqueue(&env_new_queue, env);
	}
}
void sched_remove_new(struct Env* env)
{
	if(env != NULL)
	{
		LIST_REMOVE(&env_new_queue, env) ;
		env->env_status = ENV_UNKNOWN;
	}
}
void sched_insert_exit(struct Env* env)
{
	if(env != NULL)
	{
		if(isBufferingEnabled()) {cleanup_buffers(env);}
		env->env_status = ENV_EXIT ;
		enqueue(&env_exit_queue, env);
	}
}
void sched_remove_exit(struct Env* env)
{
	if(env != NULL)
	{
		LIST_REMOVE(&env_exit_queue, env) ;
		env->env_status = ENV_UNKNOWN;
	}
}
void sched_print_all()
{
	struct Env* ptr_env ;
	if (!LIST_EMPTY(&env_new_queue))
	{
		cprintf("\nThe processes in NEW queue are:\n");
		LIST_FOREACH(ptr_env, &env_new_queue)
		{
			cprintf("	[%d] %s\n", ptr_env->env_id, ptr_env->prog_name);
		}
	}
	else
	{
		cprintf("\nNo processes in NEW queue\n");
	}
	cprintf("================================================\n");
	for (int i = 0 ; i < num_of_ready_queues ; i++)
	{
		if (!LIST_EMPTY(&(env_ready_queues[i])))
		{
			cprintf("The processes in READY queue #%d are:\n", i);
			LIST_FOREACH(ptr_env, &(env_ready_queues[i]))
			{
				cprintf("	[%d] %s\n", ptr_env->env_id, ptr_env->prog_name);
			}
		}
		else
		{
			cprintf("No processes in READY queue #%d\n", i);
		}
		cprintf("================================================\n");
	}
	if (!LIST_EMPTY(&env_exit_queue))
	{
		cprintf("The processes in EXIT queue are:\n");
		LIST_FOREACH(ptr_env, &env_exit_queue)
		{
			cprintf("	[%d] %s\n", ptr_env->env_id, ptr_env->prog_name);
		}
	}
	else
	{
		cprintf("No processes in EXIT queue\n");
	}
}
void sched_run_all()
{
	struct Env* ptr_env=NULL;
	LIST_FOREACH(ptr_env, &env_new_queue)
	{
		sched_remove_new(ptr_env);
		sched_insert_ready(ptr_env);
	}
	if (scheduler_status == SCH_STOPPED)
		fos_scheduler();
}
void sched_kill_all()
{
	struct Env* ptr_env ;
	if (!LIST_EMPTY(&env_new_queue))
	{
		cprintf("\nKILLING the processes in the NEW queue...\n");
		LIST_FOREACH(ptr_env, &env_new_queue)
		{
			cprintf("	killing[%d] %s...", ptr_env->env_id, ptr_env->prog_name);
			sched_remove_new(ptr_env);
			start_env_free(ptr_env);
			cprintf("DONE\n");
		}
	}
	else
	{
		cprintf("No processes in NEW queue\n");
	}
	cprintf("================================================\n");
	for (int i = 0 ; i < num_of_ready_queues ; i++)
	{
		if (!LIST_EMPTY(&(env_ready_queues[i])))
		{
			cprintf("KILLING the processes in the READY queue #%d...\n", i);
			LIST_FOREACH(ptr_env, &(env_ready_queues[i]))
			{
				cprintf("	killing[%d] %s...", ptr_env->env_id, ptr_env->prog_name);
				LIST_REMOVE(&(env_ready_queues[i]), ptr_env);
				start_env_free(ptr_env);
				cprintf("DONE\n");
			}
		}
		else
		{
			cprintf("No processes in READY queue #%d\n",i);
		}
		cprintf("================================================\n");
	}
	if (!LIST_EMPTY(&env_exit_queue))
	{
		cprintf("KILLING the processes in the EXIT queue...\n");
		LIST_FOREACH(ptr_env, &env_exit_queue)
		{
			cprintf("	killing[%d] %s...", ptr_env->env_id, ptr_env->prog_name);
			sched_remove_exit(ptr_env);
			start_env_free(ptr_env);
			cprintf("DONE\n");
		}
	}
	else
	{
		cprintf("No processes in EXIT queue\n");
	}
	curenv = NULL;
	fos_scheduler();
}
void sched_new_env(struct Env* e)
{
	if (e!=NULL)
	{
		sched_insert_new(e);
	}
}
void sched_run_env(uint32 envId)
{
	struct Env* ptr_env=NULL;
	LIST_FOREACH(ptr_env, &env_new_queue)
	{
		if(ptr_env->env_id == envId)
		{
			sched_remove_new(ptr_env);
			sched_insert_ready(ptr_env);
			if (scheduler_status == SCH_STOPPED)
			{
				fos_scheduler();
			}
			break;
		}
	}
}
void sched_exit_env(uint32 envId)
{
	struct Env* ptr_env=NULL;
	int found = 0;
	if (!found)
	{
		LIST_FOREACH(ptr_env, &env_new_queue)
				{
			if(ptr_env->env_id == envId)
			{
				sched_remove_new(ptr_env);
				found = 1;
			}
				}
	}
	if (!found)
	{
		for (int i = 0 ; i < num_of_ready_queues ; i++)
		{
			if (!LIST_EMPTY(&(env_ready_queues[i])))
			{
				ptr_env=NULL;
				LIST_FOREACH(ptr_env, &(env_ready_queues[i]))
				{
					if(ptr_env->env_id == envId)
					{
						LIST_REMOVE(&(env_ready_queues[i]), ptr_env);
						found = 1;
						break;
					}
				}
			}
			if (found)
				break;
		}
	}
	if (!found)
	{
		if (curenv->env_id == envId)
		{
			ptr_env = curenv;
			found = 1;
		}
	}
	if (found)
	{
		sched_insert_exit(ptr_env);
		if (curenv->env_id == envId)
		{
			curenv = NULL;
			fos_scheduler();
		}
	}
}
void sched_exit_all_ready_envs()
{
	struct Env* ptr_env=NULL;
	for (int i = 0 ; i < num_of_ready_queues ; i++)
	{
		if (!LIST_EMPTY(&(env_ready_queues[i])))
		{
			ptr_env=NULL;
			LIST_FOREACH(ptr_env, &(env_ready_queues[i]))
			{
				LIST_REMOVE(&(env_ready_queues[i]), ptr_env);
				sched_insert_exit(ptr_env);
			}
		}
	}
}
void sched_kill_env(uint32 envId)
{
	struct Env* ptr_env=NULL;
	int found = 0;
	if (!found)
	{
		LIST_FOREACH(ptr_env, &env_new_queue)
					{
			if(ptr_env->env_id == envId)
			{
				cprintf("killing[%d] %s from the NEW queue...", ptr_env->env_id, ptr_env->prog_name);
				sched_remove_new(ptr_env);
				start_env_free(ptr_env);
				cprintf("DONE\n");
				found = 1;
			}
					}
	}
	if (!found)
	{
		for (int i = 0 ; i < num_of_ready_queues ; i++)
		{
			if (!LIST_EMPTY(&(env_ready_queues[i])))
			{
				ptr_env=NULL;
				LIST_FOREACH(ptr_env, &(env_ready_queues[i]))
				{
					if(ptr_env->env_id == envId)
					{
						cprintf("killing[%d] %s from the READY queue #%d...", ptr_env->env_id, ptr_env->prog_name, i);
						LIST_REMOVE(&(env_ready_queues[i]), ptr_env);
						start_env_free(ptr_env);
						cprintf("DONE\n");
						found = 1;
						break;
					}
				}
			}
			if (found)
				break;
		}
	}
	if (!found)
	{
		ptr_env=NULL;
		LIST_FOREACH(ptr_env, &env_exit_queue)
		{
			if(ptr_env->env_id == envId)
			{
				cprintf("killing[%d] %s from the EXIT queue...", ptr_env->env_id, ptr_env->prog_name);
				sched_remove_exit(ptr_env);
				start_env_free(ptr_env);
				cprintf("DONE\n");
				found = 1;
			}
		}
	}
	if (!found)
	{
		if (curenv->env_id == envId)
		{
			ptr_env = curenv;
			assert(ptr_env->env_id == ENV_RUNNABLE);
			cprintf("killing a RUNNABLE environment [%d] %s...", ptr_env->env_id, ptr_env->prog_name);
			start_env_free(ptr_env);
			cprintf("DONE\n");
			found = 1;
		}
	}
	if (curenv->env_id == envId)
	{
		lcr3(phys_page_directory);
		curenv = NULL;
		fos_scheduler();
	}
}
void clock_interrupt_handler()
{
	if(isPageReplacmentAlgorithmLRU())
	{
		update_WS_time_stamps();
	}
	fos_scheduler();
}
void update_WS_time_stamps()
{
	struct Env *curr_env_ptr = curenv;
	if(curr_env_ptr != NULL)
	{
		{
			int i ;
			for (i = 0 ; i < (curr_env_ptr->page_WS_max_size); i++)
			{
				if( curr_env_ptr->ptr_pageWorkingSet[i].empty != 1)
				{
					uint32 page_va = curr_env_ptr->ptr_pageWorkingSet[i].virtual_address ;
					uint32 perm = pt_get_page_permissions(curr_env_ptr, page_va) ;
					uint32 oldTimeStamp = curr_env_ptr->ptr_pageWorkingSet[i].time_stamp;
					if (perm & PERM_USED)
					{
						curr_env_ptr->ptr_pageWorkingSet[i].time_stamp = (oldTimeStamp>>2) | 0x80000000;
						pt_set_page_permissions(curr_env_ptr, page_va, 0 , PERM_USED) ;
					}
					else
					{
						curr_env_ptr->ptr_pageWorkingSet[i].time_stamp = (oldTimeStamp>>2);
					}
				}
			}
		}
		{
			int t ;
			for (t = 0 ; t < __TWS_MAX_SIZE; t++)
			{
				if( curr_env_ptr->__ptr_tws[t].empty != 1)
				{
					uint32 table_va = curr_env_ptr->__ptr_tws[t].virtual_address;
					uint32 oldTimeStamp = curr_env_ptr->__ptr_tws[t].time_stamp;
					if (pd_is_table_used(curr_env_ptr, table_va))
					{
						curr_env_ptr->__ptr_tws[t].time_stamp = (oldTimeStamp>>2) | 0x80000000;
						pd_set_table_unused(curr_env_ptr, table_va);
					}
					else
					{
						curr_env_ptr->__ptr_tws[t].time_stamp = (oldTimeStamp>>2);
					}
				}
			}
		}
	}
}
