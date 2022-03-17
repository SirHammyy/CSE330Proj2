#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Hamlin");
MODULE_DESCRIPTION("Producer and Consumer");

int uid;
int buff_size;
int p;
int c;

module_param(uid, int, 0);
module_param(buff_size, int, 0);
module_param(p, int, 0);
module_param(c, int, 0);

struct task_struct_list {
	struct task_struct* task;
	struct task_struct_list* next;
};

struct buffer {
	int capacity;
	struct task_struct_list* list;
};


//Global Variables:
struct buffer *buf = NULL;
struct semaphore mutex;
struct semaphore full;
struct semaphore empty;
u64 total_seconds;

//Global task structs for kernel threads
struct task_struct *pThread;
int pThreadPID;
struct task_struct_list *cThreads = NULL; 

static int producer(void *arg) {
	struct task_struct *ptr;
	int process_counter = 0;


	//Adds processes into buffer
	for_each_process(ptr) {
		if (ptr->cred->uid.val == uid) {
			process_counter++;
		
			if (down_interruptible(&empty))
				break;
			if (down_interruptible(&mutex))
				break;	

			//Critical section		
			if (buf->list == NULL) {
				struct task_struct_list *new;
				new = kmalloc(sizeof(struct task_struct_list), GFP_KERNEL);
				new->task = ptr;
				buf->list = new;
				buf->capacity++;
				printk(KERN_INFO "[%s] Produced Item#-%d at buffer index:%d for PID:%d", current->comm, process_counter, buf->capacity, new->task->pid);
			}
			else {
				struct task_struct_list *temp = buf->list;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				struct task_struct_list *new;
				new = kmalloc(sizeof(struct task_struct_list), GFP_KERNEL);
				new->task = ptr;
				temp->next = new;
				buf->capacity++;
				printk(KERN_INFO "[%s] Produced Item#-%d at buffer index:%d for PID:%d\n", current->comm, process_counter, buf->capacity, new->task->pid);
			}


			up(&mutex);
			up(&full);

		
		}
		
	}

	return 0;
}

static int consumer(void *arg) {
	int consumedCount = 0;

	//Start infinite loop until stop
	while (!kthread_should_stop()) {
		if (down_interruptible(&full))
			break;
		if (down_interruptible(&mutex))
			break;


		//Check to skip critical section
		if (kthread_should_stop()) {
			return 0;
		}


		if (buf != NULL && buf->list != NULL && buf->capacity > 0) {
			//Critical section
			struct task_struct_list *temp = buf->list;
			buf->list = buf->list->next;
			u64 time_elapsed = ktime_get_ns() - temp->task->start_time;
			u64 elapsed_seconds = time_elapsed / 1000000000;
			int minutes = elapsed_seconds / 60;
			int hours = minutes / 60;
			int remaining_seconds = elapsed_seconds % 60;
			total_seconds = total_seconds + elapsed_seconds;

			consumedCount++;
			printk(KERN_INFO "[%s] Consumed Item#-%d on buffer index:0 PID:%d Elapsed Time- %d:%d:%d\n", current->comm, buf->capacity, temp->task->pid, hours, minutes, remaining_seconds);
			buf->capacity--;
			up(&mutex);
			up(&empty);
		}
	}

	return 0;
}

static int __init init_func(void) {
	int err;
	
	printk(KERN_INFO "Starting producer_consumer module\n");

	//Initialize Semaphores
	sema_init(&mutex, 1);
	sema_init(&full, 0);
	sema_init(&empty, buff_size);

	buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);

	//Start the threads

	if (p == 1) {
		pThread = kthread_run(producer, NULL, "Producer-1");
		pThreadPID = pThread->pid;
		if (IS_ERR(pThread)) {
			printk(KERN_INFO "ERROR: Cannot create producer thread\n");
			err = PTR_ERR(pThread);
			pThread = NULL;
			return err;
		}
	}
	
	int i = 0;
	while (i < c) {
		struct task_struct_list *cThread = kmalloc(sizeof(struct task_struct_list), GFP_KERNEL);
		cThread->task = kthread_run(consumer, NULL, "Consumer-%d", i);
		if (IS_ERR(cThread->task)) {
			printk(KERN_INFO "ERROR: Cannot create consumer thread\n");
			err = PTR_ERR(cThread->task);
			cThread->task = NULL;
			return err;
		}
		cThread->next = cThreads;
		cThreads = cThread;
		

		i++;
	}

	struct task_struct_list *cThr = cThreads;
	while (cThr != NULL) {
		//printk(KERN_INFO "consumer thread PID %d\n", cThr->task->pid);
		cThr = cThr->next;
	}

	return 0;
}


static void __exit exit_func(void) {

	struct task_struct_list *temp = buf->list;
	while (temp != NULL) {
		struct task_struct_list *tbr = temp;
		temp = temp->next;
		kfree(tbr);
	}
	kfree(buf);

	printk(KERN_INFO "Stopping producer thread PID-%d\n", pThreadPID);

	int i = c;
	struct task_struct_list *cThread = cThreads;
	while (cThread != NULL) {
		int j = i;
		while (j >= 0) {
			up(&full);
			up(&mutex);
			up(&full);
			up(&mutex);
			j--;
		}
		printk(KERN_INFO "Stopping consumer thread PID-%d\n", cThread->task->pid);
		up(&full);
		up(&mutex);
		kthread_stop(cThread->task);
		up(&full);
		up(&mutex);
		cThread = cThread->next;
		
	}


	int minutes = total_seconds / 60;
	int hours = minutes / 60;
	int remain_seconds = total_seconds % 60;
	minutes = minutes % 60;
	printk(KERN_INFO "The total elapsed time of all processes for UID %d is %d:%d:%d\n", uid, hours, minutes, remain_seconds);

	printk(KERN_INFO "Exiting producer_consumer module\n");
}


module_init(init_func);
module_exit(exit_func);
