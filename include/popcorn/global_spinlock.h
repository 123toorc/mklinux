#ifndef __GLOBAL_SPINLOCK_H
#define __GLOBAL_SPINLOCK_H

#include <linux/workqueue.h>
#include	<linux/hash.h>
#include <linux/spinlock.h>
#include "request_data.h"

#define _SPIN_HASHBITS 8
#define _BITS_FOR_STATUS 3

#define INITIAL_STATE 0
#define HAS_TICKET 3

#define NORMAL_Q_PRIORITY 100

#define LOCK_STAT 
//#undef LOCK_STAT
#define sp_hashfn(uaddr, pid)      \
	hash_long((unsigned long)uaddr + (unsigned long)pid, _SPIN_HASHBITS)


struct futex_common_data{
	int nr_wake;
	int nr_requeue;
	u32 bitset;
	int cmpval;
	int val;
	int rflag;
	int fn_flag;
	int rw;
	unsigned int flags;
	unsigned long uaddr;
	unsigned long uaddr2;
	unsigned int ops :1;
} __attribute__((packed));

typedef struct futex_common_data  futex_common_data_t;




typedef struct spin_key {
	pid_t _tgid;
	unsigned long _uaddr;
	int offset;
} _spin_key;

struct local_request_queue {
	pid_t _pid;
	unsigned long uaddr;
	unsigned long _request_id;//ticket number _pid has acquired
	unsigned int wake_st; //token status
	wait_queue_head_t _wq; //to wait until the server responds
	enum {
		DONE, IDLE, INPROG, SLEEP
	} status;
	int errno;
	int ops;
	int _st;
	struct list_head lrq_member;
} __attribute__((packed));
typedef struct local_request_queue _local_rq_t;

struct global_request_queue {
	//	volatile struct plist_node list;
	_remote_wakeup_request_t wakeup;
	_remote_key_request_t wait;
	int cnt;
	unsigned int ops:1; //0-wait 1-wake
}__attribute__((packed));
typedef struct global_request_queue _global_rq;





struct global_request_work  {
	struct work_struct work;
	spinlock_t * lock;
	_global_rq * gq;
	//	struct plist_head * _grq_head;
	//	volatile unsigned int _is_alive;
	//	pid_t _worker_pid;
	//	wait_queue_head_t *flush;
	//	unsigned int * free_work;
	//	int ops ; //0-wait 1-wake
};

typedef struct global_request_work global_request_work_t;
struct migration_value{
	pid_t pid;
	unsigned long uaddr;
	int ops;
	struct timespec request_time;
	volatile unsigned long served;
	struct timespec response_time;
};
typedef struct migration_value  _mig_value;

struct spin_value {
	spinlock_t _sp;
	volatile unsigned int _st; //token status//TBR
	volatile unsigned long _ticket;
	_mig_value *mig_st;
	struct list_head _lrq_head; //stores the status of ticket and wait queues
};
typedef struct spin_value  _spin_value;

struct global_value {
	//	volatile struct plist_head _grq_head; // TODO for storing mutiple wq
	struct workqueue_struct *global_wq;
	struct task_struct *thread_group_leader;
	pid_t thread_group_pid;
	global_request_work_t *worker_task;
	unsigned int free :1;
	char name[32];
	struct list_head _link_head;
};
typedef struct global_value _global_value;

struct global_value_bag{
	spinlock_t lock;
	struct list_head link;
};
typedef struct global_value_bag _global_bkt;
	

_spin_value *hashspinkey(_spin_key *sk);
_global_bkt *hashgroup(struct task_struct *group_pid);


//Populate spin key from uaddr
int getKey(unsigned long uaddr, _spin_key *sk, pid_t tgid);

int global_spinlock(unsigned long uaddr,futex_common_data_t *_data,_spin_value * value,_local_rq_t *rq_ptr,int localticket_value);
int global_spinunlock(unsigned long uaddr, unsigned int fn_flag);


_local_rq_t * add_request_node(int request_id,pid_t pid, struct list_head *head);
int find_and_delete_request(int request_id, struct list_head *head);
_local_rq_t * find_request(int request_id, struct list_head *head) ;
_local_rq_t * find_request_by_pid(pid_t pid, struct list_head *head) ;
_local_rq_t * set_err_request(int request_id,int err, struct list_head *head) ;
int find_and_delete_pid(int pid, struct list_head *head);
_local_rq_t *set_wake_request_by_pid(pid_t pid, struct list_head *head);
_local_rq_t *find_request_by_ops(int ops, unsigned long uaddr, pid_t pid, struct list_head *head);

extern _spin_value spin_bucket[1 << _SPIN_HASHBITS];
extern _global_bkt global_bucket[1 << _SPIN_HASHBITS];

#endif
