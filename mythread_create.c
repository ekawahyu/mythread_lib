#include <malloc.h>
#include <string.h>
#include <stdlib.h>

#include <signal.h>
#include <errno.h>
#include <sched.h>

#include <mythread.h>
#include <futex.h>
#include <mythread_q.h>

#include <sys/syscall.h>
#include <sys/types.h>

#define CLONE_SIGNAL            (CLONE_SIGHAND | CLONE_THREAD)

int mythread_wrapper(void *);
void * mythread_idle(void *);

mythread_t * mythread_q_head;

mythread_t *idle_tcb;
mythread_t *main_tcb;
extern struct futex gfutex;
struct futex debug_futex;

static void __mythread_add_main_tcb()
{
	DEBUG_PRINTF("add_main_tcb: Creating node for Main thread \n");
	main_tcb = (mythread_t *)malloc(sizeof(mythread_t));
	if (main_tcb == NULL) {
		DEBUG_PRINTF("add_main_tcb: Error allocating memory for main node\n");
		exit(1);
	}

	main_tcb->start_func = NULL;
	main_tcb->args = NULL;
	main_tcb->state = READY;
	main_tcb->returnValue = NULL;
	main_tcb->blockedForJoin = NULL;

	/* Get the main's tid and put it in its corresponding tcb. */
	main_tcb->tid = __mythread_gettid();

	/* Initialize futex to zero*/
	futex_init(&main_tcb->sched_futex, 1);

	/* Put it in the Q of thread blocks */
	mythread_q_add(main_tcb);
}

int mythread_create(mythread_t * new_thread_ID,
		    mythread_attr_t * attr,
		    void *(*start_func) (void *), void *arg)
{

	/* pointer to the stack used by the child process to be created by clone later */
	char *child_stack;
	unsigned long stackSize;
	pid_t tid;

	/* Flags to be passed to clone system call. 
	   This flags variable is picked up from pthread source code. 
	 */
	int clone_flags = (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGNAL
			   | CLONE_PARENT_SETTID
			   | CLONE_CHILD_CLEARTID | CLONE_SYSVSEM);

	if (mythread_q_head == NULL) {
		/* This is the very first mythread_create call. Set up the Q first with tcb nodes for main thread. */
		__mythread_add_main_tcb();

		/* Initialise the global futex */
		futex_init(&gfutex, 1);

	  	/* Now create the node for Idle thread. */
		DEBUG_PRINTF("create: creating node for Idle thread \n");
		idle_tcb = (mythread_t *)malloc(sizeof(mythread_t));
		if(idle_tcb == NULL) {
			DEBUG_PRINTF("create: malloc error while allocating memory to idle node!");
	  		exit(1);
		}
		mythread_create(idle_tcb, NULL, mythread_idle, NULL);
	}

	/*Stack-size argument is not provided */
	if (attr == NULL)
		stackSize = SIGSTKSZ;
	else
		stackSize = attr->stackSize;

	/* posix_memalign aligns the allocated memory at a 64-bit boundry. */
	if (posix_memalign((void **)&child_stack, 8, stackSize)) {
		printf("posix_memalign failed! \n");
		printf("Exiting.....\n");
		return (-ENOMEM);
	}


	/* We leave space for one invocation at the base of the stack */
	child_stack = child_stack + stackSize - sizeof(sigset_t);

	/* Save the thread_fun pointer and the pointer to arguments in the TCB. */
	new_thread_ID->start_func = start_func;
	new_thread_ID->args = arg;
	/* Set the state as READY - READY in Q, waiting to be scheduled. */
	new_thread_ID->state = READY;

	new_thread_ID->returnValue = NULL;
	new_thread_ID->blockedForJoin = NULL;
	/* Initialize the tcb's sched_futex to zero. */
	futex_init(&new_thread_ID->sched_futex, 0);

	/* Put it in the Q of thread blocks */
	mythread_q_add(new_thread_ID);

	/* Call clone with pointer to wrapper function. TCB will be passed as arg to wrapper function. */
	if ((tid =
	     clone(mythread_wrapper, (char *)child_stack, clone_flags,
		   new_thread_ID)) == -1) {
		printf("clone failed! \n");
		printf("ERROR: %s \n", strerror(errno));
		return (-errno);
	}
	/* Save the tid returned by clone system call in the tcb. */
	new_thread_ID->tid = tid;

	DEBUG_PRINTF("create: Finished initialising new thread: %ld\n", (unsigned long)new_thread_ID->tid);
	return 0;
}

int mythread_wrapper(void *thread_tcb)
{
	mythread_t *new_tcb;
	new_tcb = (mythread_t *)thread_tcb;

	DEBUG_PRINTF("Wrapper: will sleep on futex: %ld %d\n", (unsigned long)__mythread_gettid(), new_tcb->sched_futex.count);
	futex_down(&new_tcb->sched_futex);
	DEBUG_PRINTF("Wrapper: futex value: %ld %d\n", (unsigned long)new_tcb->tid, new_tcb->sched_futex.count);
	new_tcb->start_func(new_tcb->args);

	/* Ideally we shouldn't reach here */
	return 0;
}

void * mythread_idle(void *phony)
{
	while(1) {
		DEBUG_PRINTF("I am idle\n"); fflush(stdout);
		mythread_yield();
	}
}

