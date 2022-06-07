/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/memlayout.h>

#include <kern/command_prompt.h>
#include <kern/console.h>
#include <kern/memory_manager.h>
#include <kern/helpers.h>
#include <kern/kclock.h>
#include <kern/user_environment.h>
#include <kern/trap.h>
#include <kern/picirq.h>
#include <kern/sched.h>
#include <kern/utilities.h>
#include <inc/timerreg.h>

//Functions Declaration
//======================
void print_welcome_message();
//=======================================

extern uint32 enableBuffering();
extern uint32 isBufferingEnabled();


//First ever function called in FOS kernel
void FOS_initialize()
{
	//get actual addresses after code linking
	extern char start_of_uninitialized_data_section[], end_of_kernel[];

	// Before doing anything else,
	// clear the uninitialized global data (BSS) section of our program, from start_of_uninitialized_data_section to end_of_kernel
	// This ensures that all static/global variables start with zero value.
	memset(start_of_uninitialized_data_section, 0, end_of_kernel - start_of_uninitialized_data_section);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	console_initialize();

	//print welcome message
	print_welcome_message();

	// Lab 2 memory management initialization functions
	detect_memory();
	initialize_kernel_VM();
	initialize_paging();
//	page_check();


	// Lab 3 user environment initialization functions
	env_init();
	idt_init();

	setPageReplacmentAlgorithmModifiedCLOCK();
	setUHeapPlacementStrategyNEXTFIT();
	setKHeapPlacementStrategyNEXTFIT();
	enableBuffering(0);
	//enableModifiedBuffer(1) ;
	enableModifiedBuffer(0) ;
	//TODODONE: remove this line
	chksch(1);

	// Lab 4 multitasking initialization functions
	pic_init();

	kclock_start(CLOCK_INTERVAL_IN_MS);
	sched_init() ;

	// start the kernel command prompt.
	while (1==1)
	{
		cprintf("\nWelcome to the FOS kernel command prompt!\n");
		cprintf("Type 'help' for a list of commands.\n");
		run_command_prompt();
	}
}


void print_welcome_message()
{
	cprintf("\n\n\n");
	cprintf("\t\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	cprintf("\t\t!!                                                             !!\n");
	cprintf("\t\t!!                   !! FCIS says HELLO !!                     !!\n");
	cprintf("\t\t!!                                                             !!\n");
	cprintf("\t\t!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	cprintf("\n\n\n\n");
}


/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
static const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", exit the curenv and schedule the next environment.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	//if (panicstr)
	//	goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("\nkernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the fos scheduler */
	//2013: Check if the panic occur when running an environment
	if (curenv != NULL && curenv->env_status == ENV_RUNNABLE)
	{
		//2015
		env_exit();
		//env_run_cmd_prmpt() ;
	}

	//2015
	fos_scheduler();
	//while (1==1)
	//	run_command_prompt();

}

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", exit all ready env's and then enters the kernel command prompt.
 */
void _panic_all(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	//if (panicstr)
	//	goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("\nkernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the fos scheduler */

	//exit all ready env's
	sched_exit_all_ready_envs();
	if (curenv != NULL)
	{
		//cprintf("exit curenv...........\n");
		sched_exit_env(curenv->env_id);
		//env_run_cmd_prmpt() ;
	}

	fos_scheduler();
}

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", exit the curenv (if any) and break into the command prompt.
 */
void _panic_into_prompt(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	//if (panicstr)
	//	goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("\nkernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the fos scheduler */
	//2013: Check if the panic occur when running an environment
	if (curenv != NULL && curenv->env_status == ENV_RUNNABLE)
	{
		sched_insert_exit(curenv);
		curenv = NULL;
	}

	lcr3(phys_page_directory);

	scheduler_status = SCH_STOPPED;
	while (1)
		run_command_prompt(NULL);
}

/* like panic, but don't enters the kernel command prompt*/
void _warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("\nkernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

