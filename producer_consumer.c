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

//Global task structs for kernel threads
struct task_struct *pThread;
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
			printk(KERN_INFO "%s is force stopping\n", current->comm);
			return 0;
		}


		if (buf != NULL && buf->list != NULL && buf->capacity > 0) {
			//Critical section
			struct task_struct_list *temp = buf->list;
			buf->list = buf->list->next;
			unsigned long int time_elapsed = temp->task->start_time;
			unsigned long int total_seconds = time_elapsed/(10^9);
			consumedCount++;
			buf->capacity--;
			printk(KERN_INFO "[%s] Consumed Item#-%d on buffer index:0 PID:%d Elapsed Time- %ld\n", current->comm, buf->capacity, temp->task->pid, total_seconds);
		
			up(&mutex);
			up(&empty);
		}
	}

	printk(KERN_INFO "%s is stopping\n", current->comm);

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
		printk(KERN_INFO "Starting consumer thread PID:%d\n", cThread->task->pid);
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
		printk(KERN_INFO "consumer thread PID %d\n", cThr->task->pid);
		cThr = cThr->next;
	}

	return 0;
}


static void __exit exit_func(void) {

	//struct task_struct_list *temp = buf->list;
	//while (temp != NULL) {
	//	struct task_struct_list *tbr = temp;
	//	temp = temp->next;
	//	kfree(tbr);
	//}
	//kfree(buf);

	int i = c;
	struct task_struct_list *cThread = cThreads;
	printk(KERN_INFO "beginning stop consumer at %d\n", cThread->task->pid);
	while (cThread != NULL) {
		int j = i;
		while (j >= 0) {
			up(&full);
			up(&mutex);
			up(&full);
			up(&mutex);
			j--;
		}
		printk(KERN_INFO "stopping cThread: %d\n", cThread->task->pid);
		up(&full);
		up(&mutex);
		kthread_stop(cThread->task);
		printk(KERN_INFO "stopped cThread\n");
		up(&full);
		up(&mutex);
		cThread = cThread->next;
		
	}

	cThread = cThreads;
	while (cThread != NULL) {
		up(&full);
		up(&mutex);
		up(&full);
		up(&mutex);
		cThread = cThread->next;
	}

	printk(KERN_INFO "Exiting producer_consumer module\n");
}


module_init(init_func);
module_exit(exit_func);
