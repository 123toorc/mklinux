/*
 * global_spinlock.c
 *
 *  Created on: 4/7/2014
 *      Author: akshay
 */


union futex_key; //forward decl for futex_remote.h


#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <popcorn/global_spinlock.h>
#include <popcorn/remote_pfn.h>
#include <linux/pcn_kmsg.h>

#define FLAGS_SYSCALL		8
#define FLAGS_REMOTECALL	16
#define FLAGS_ORIGINCALL	32

#define FLAGS_SHARED            0x01

#define WAKE_OPS 1
#define WAIT_OPS 0

DEFINE_SPINLOCK(request_queue_lock);

//hash buckets
_spin_value spin_bucket[1<<_SPIN_HASHBITS];
_global_bkt global_bucket[1<<_SPIN_HASHBITS];

#define GENERAL_SPIN_LOCK(x,f) spin_lock_irqsave(x,f)
#define GENERAL_SPIN_UNLOCK(x,f) spin_unlock_irqrestore(x,f)

#define GSP_VERBOSE 0
#if GSP_VERBOSE
#define GSPRINTK(...) printk(__VA_ARGS__)
#else
#define GSPRINTK(...) ;
#endif


#define is_pfn_in_range(pfn)          ((pfn) < max_pfn && (pfn) > kernel_start_addr)

//extern functions
extern struct vm_area_struct * getVMAfromUaddr(unsigned long uaddr);
extern pte_t *do_page_walk(unsigned long address);
extern  int find_kernel_for_pfn(unsigned long addr, struct list_head *head);

_local_rq_t * add_request_node(int request_id, pid_t pid, struct list_head *head) {

	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	_local_rq_t *Ptr = (_local_rq_t *) kmalloc(
			sizeof(_local_rq_t), GFP_ATOMIC);

	memset(Ptr, 0, sizeof(_local_rq_t));
	Ptr->_request_id = request_id;
	Ptr->status = IDLE;
	Ptr->wake_st = 0;
	Ptr->_pid = pid;
	init_waitqueue_head(&Ptr->_wq);
	INIT_LIST_HEAD(&Ptr->lrq_member);
	list_add(&Ptr->lrq_member, head);
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);

	return Ptr;
}

int find_and_delete_request(int request_id, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_request_id == request_id) {
			list_del(&objPtr->lrq_member);
			kfree(objPtr);
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return 1;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
}


int find_and_delete_pid(int pid, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_pid == pid) {
			list_del(&objPtr->lrq_member);
			kfree(objPtr);
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return 1;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
}

_local_rq_t * find_request(int request_id, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_request_id == request_id) {
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return objPtr;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
	return NULL;
}

_local_rq_t * set_err_request(int request_id, int err, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_request_id == request_id) {
			objPtr->status =DONE;
			objPtr->errno = err;
			wake_up_interruptible(&objPtr->_wq);
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return objPtr;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
	return NULL;
}

_local_rq_t *find_request_by_pid(pid_t pid, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_pid == pid && objPtr->ops == 0) {
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return objPtr;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
	return NULL;
}




_local_rq_t *find_request_by_ops(int ops, unsigned long uaddr,pid_t pid, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->ops == 0 && objPtr->uaddr == uaddr && objPtr->_pid == pid) {
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return objPtr;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
	return NULL;
}

_local_rq_t *set_wake_request_by_pid(pid_t pid, struct list_head *head) {

	struct list_head *iter;
	_local_rq_t *objPtr;
	unsigned long f;
	GENERAL_SPIN_LOCK(&request_queue_lock,f);
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, _local_rq_t, lrq_member);
		if (objPtr->_pid == pid) {
			objPtr->wake_st =1; //Set wake state as ON
			GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
			return objPtr;
		}
	}
	GENERAL_SPIN_UNLOCK(&request_queue_lock,f);
	return NULL;
}
inline void spin_key_init (struct spin_key *st) {
	st->_tgid = 0;
	st->_uaddr = 0;
	st->offset = 0;
}


//Populate spin key from uaddr
int getKey(unsigned long uaddr, _spin_key *sk, pid_t tgid)
{
	unsigned long address = (unsigned long)uaddr;
	sk->offset = address;
	sk->_uaddr  = uaddr;
	sk->_tgid	= tgid;

	return 0;
}


// hash spin key to find the spin bucket
_spin_value *hashspinkey(_spin_key *sk)
{
	pagefault_disable();
	u32 hash = sp_hashfn(sk->_uaddr,sk->_tgid);
	pagefault_enable();
	return &spin_bucket[hash];
}

//to get the global worker and global request queue
_global_bkt *hashgroup(struct task_struct *group_pid)
{
	struct task_struct *tsk =NULL;
	tsk= group_pid;
	u32 hash = sp_hashfn(tsk->pid,0);
	return &global_bucket[hash];
}
// Perform global spin lock
int global_spinlock(unsigned long uaddr,futex_common_data_t *_data,_spin_value * value,_local_rq_t *rq_ptr,int localticket_value)
__releases(&value->_sp)
{
	int res = 0;
	int cpu=0;
	unsigned int wait_flag = 0;

	_remote_key_request_t* wait_req= (_remote_key_request_t*) kmalloc(sizeof(_remote_key_request_t),
			GFP_ATOMIC);
	_remote_wakeup_request_t *wake_req = (_remote_wakeup_request_t*) kmalloc(sizeof(_remote_wakeup_request_t),
			GFP_ATOMIC);

	//Prepare request
	if(_data->ops==WAIT_OPS){

		GSPRINTK(KERN_ALERT"%s:  uaddr {%lx}  pid{%d} current->tgroup_home_id{%d}\n",__func__,uaddr,current->pid,current->tgroup_home_id);

		// Finish constructing response
		wait_req->header.type = PCN_KMSG_TYPE_REMOTE_IPC_FUTEX_KEY_REQUEST;
		wait_req->header.prio = PCN_KMSG_PRIO_NORMAL;

		wait_req->ops = WAIT_OPS;
		wait_req->rw = _data->rw;
		wait_req->val = _data->val;

		wait_req->uaddr = (unsigned long) uaddr;
		wait_req->tghid = current->tgroup_home_id;
		wait_req->bitset = _data->bitset;
		wait_req->pid = current->pid;
		wait_req->fn_flags = _data->fn_flag;
		wait_req->flags = _data->flags;

		wait_req->ticket = localticket_value;//GET_TOKEN; //set the request has no ticket
	}
	else{
		wake_req->header.type = PCN_KMSG_TYPE_REMOTE_IPC_FUTEX_WAKE_REQUEST;
		wake_req->header.prio = PCN_KMSG_PRIO_NORMAL;

		wake_req->ops = WAKE_OPS;
		wake_req->uaddr2 = (unsigned long) _data->uaddr2;
		wake_req->nr_wake2 = _data->nr_requeue;
		wake_req->cmpval = _data->cmpval;
		wake_req->rflag = _data->rflag;
		wake_req->nr_wake =_data->nr_wake;

		wake_req->uaddr = (unsigned long) uaddr;
		wake_req->tghid = current->tgroup_home_id;
		wake_req->bitset = _data->bitset;
		wake_req->pid = current->pid;
		wake_req->fn_flag = _data->fn_flag;
		wake_req->flags = _data->flags;

		wake_req->ticket = localticket_value;//GET_TOKEN; //set the request has no ticket
		GSPRINTK(KERN_ALERT"%s: wake uaddr2{%lx} data{%lx} \n",__func__,wake_req->uaddr2,_data->uaddr2);
	}




	unsigned long pfn;
	pte_t pte;
	pte = *((pte_t *) do_page_walk((unsigned long)uaddr));
	pfn = pte_pfn(pte);

	struct vm_area_struct *vma;
	vma = getVMAfromUaddr(uaddr);
	if (vma != NULL) {
		//Remote kernel. So pages will be special or invalid
		if( current->executing_for_remote && ((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) || !pfn_valid(pfn) || !is_pfn_in_range(pfn))) {
			if(_data->ops==WAIT_OPS){
				wait_req->fn_flags |= FLAGS_REMOTECALL;
			}
			else{
				wake_req->fn_flag |= FLAGS_REMOTECALL;
			}
			GSPRINTK(KERN_ALERT"%s: sending to origin remote callpfn cpu: 0x{%d} request->ticket{%d} \n",__func__,cpu,localticket_value);
			if ((cpu = find_kernel_for_pfn(pfn, &pfn_list_head)) != -1){
				spin_unlock(&value->_sp);

				res = pcn_kmsg_send(cpu, (struct pcn_kmsg_message*)  ((_data->ops==WAKE_OPS)? (wake_req):(wait_req)));
				wait_flag = 1;
			}
		}
		//Origin kernel. check for normal pages or COW'd pages (in this case PFNMAP/MIXEDMAP is normal) 
		else if (!current->executing_for_remote && ( !(vma->vm_flags & VM_PFNMAP)  || ((vma->vm_flags & VM_PFNMAP) && ((vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE)))) {
			if(_data->ops==WAIT_OPS){
				wait_req->fn_flags |= FLAGS_ORIGINCALL;
			}
			else{
				wake_req->fn_flag |= FLAGS_ORIGINCALL;
				wake_req->rflag = current->pid;
			}
			GSPRINTK(KERN_ALERT"%s: sending to origin origin call cpu: 0x{%d} request->ticket{%d} \n",__func__,cpu,localticket_value);
			if ((cpu = find_kernel_for_pfn(pfn, &pfn_list_head)) != -1){
				spin_unlock(&value->_sp);

				res = pcn_kmsg_send(cpu, (struct pcn_kmsg_message*) ((_data->ops==WAKE_OPS)? (wake_req):(wait_req)));
				wait_flag = 1;
			}
		}
		else{
			//Check mm/memmory.c for more on pte_special rule check
			GSPRINTK(KERN_ALERT" VMA FLAG RULE CHECK exec{%d} pfn{0x%lx} valid{%d}  vma{0x%lx} 1:{%d} 3:{%d} 4:{%d}\n",current->executing_for_remote,pfn,pfn_valid(pfn),vma->vm_flags,!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)), ((vma->vm_flags & VM_PFNMAP) && ((vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE)), (vma->vm_flags & VM_MIXEDMAP) && pfn_valid(pfn) );
			spin_unlock(&value->_sp);
	                wait_flag = 0;
		}
	}
	else{
		printk(KERN_ALERT" VMA is NULL \n");		
		spin_unlock(&value->_sp);
		wait_flag = 0;

	}
	if(wait_flag == 1){
		wait_event_interruptible(rq_ptr->_wq, (rq_ptr->status == DONE));
		GSPRINTK(KERN_ALERT"%s:after wake up process: task woken{%d}\n",__func__,current->pid);
		wait_flag = 0;
	}
	else
		printk(KERN_ALERT"%s:task {%d} sleep not required\n",__func__,current->pid);

out:
	kfree(wake_req);
	kfree(wait_req);
	return 0;

}

static int __init global_spinlock_init(void)
{
	int i=0;


	for (i = 0; i < ARRAY_SIZE(spin_bucket); i++) {
		spin_lock_init(&spin_bucket[i]._sp);
		spin_bucket[i]._st = 0;//TBR
		spin_bucket[i]._ticket = 0;
		spin_bucket[i].mig_st = NULL;

		INIT_LIST_HEAD(&spin_bucket[i]._lrq_head);
	}

	for (i = 0; i < ARRAY_SIZE(global_bucket); i++) {
		spin_lock_init(&global_bucket[i].lock);
		INIT_LIST_HEAD(&global_bucket[i].link);
		//		global_bucket[i].worker_task=NULL;
		//		global_bucket[i].global_wq = NULL;
		//		global_bucket[i].free = 0;
	}


	return 0;
}
static void __exit global_spinlock_exit(void)
{

	int i=0;

}
__initcall(global_spinlock_init);

module_exit(global_spinlock_exit);
