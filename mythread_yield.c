#include <mythread.h>
#include <futex.h>
#include <mythread_q.h>
#include <string.h>
#include <sys/syscall.h>

struct futex gfutex;

char debug[1000];

pid_t gettid()
{
	return (pid_t) syscall(SYS_gettid);
}

int mythread_dispatcher(mythread_t *node)
{
	mythread_t *ptr = node->next;
	while(ptr->state != READY)
		ptr = ptr->next;

	if (ptr == node)
		return -1;
	else {
		sprintf(debug, "Wake-up:%ld Sleep:%ld %d\n", (unsigned long)ptr->tid, (unsigned long)node->tid, ptr->sched_futex.count);
		write(1, debug, strlen(debug));
		futex_up(&ptr->sched_futex);
		sprintf(debug, "Woken up:%ld, to %d\n", (unsigned long)ptr->tid, ptr->sched_futex.count);
		write(1, debug, strlen(debug));
		return 0;
	}
}

/* Basic yield. It will get more complex when we add join */
int mythread_yield()
{
	mythread_t *self;
	int retval;

	self = mythread_q_search(gettid());

	//if (self->next->tid == self->tid) /* Only one thread */
	//	return 0;


	/* sprintf(debug, "Might sleep on gfutex %ld %d\n", (unsigned long)self.tid, gfutex.count);
	write(1, debug, strlen(debug)); */

	futex_down(&gfutex); 

	retval = mythread_dispatcher(self);
	//futex_up(&self->next->sched_futex);
	
	/* Only one thread */
	if (retval == -1) {
		futex_up(&gfutex);
		return 0;
	}

	sprintf(debug, "Might sleep on first down %ld %d\n", (unsigned long)self->tid, self->sched_futex.count);
	write(1, debug, strlen(debug));
	if (self->sched_futex.count > 0)
		futex_down(&self->sched_futex);

	futex_up(&gfutex);

	sprintf(debug, "Might sleep on second down %ld %d\n", (unsigned long)self->tid, self->sched_futex.count);
	write(1, debug, strlen(debug));
	futex_down(&self->sched_futex);

	return 0;
}
