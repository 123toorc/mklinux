/**
 * Implements task migration and maintains coherent 
 * address spaces across CPU cores.
 *
 * David G. Katz
 */

#include <linux/mcomm.h> // IPC
#include <linux/kthread.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/threads.h> // NR_CPUS
#include <linux/kmod.h>
#include <linux/path.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/slab.h>
#include <linux/process_server.h>
#include <linux/mm.h>
#include <linux/io.h> // ioremap
#include <linux/mman.h> // MAP_ANONYMOUS
#include <linux/pcn_kmsg.h> // Messaging
#include <linux/pcn_perf.h> // performance measurement
#include <linux/string.h>

#include <linux/popcorn_cpuinfo.h>
#include <linux/unistd.h>
#include <linux/tsacct_kern.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#include <asm/pgtable.h>
#include <asm/atomic.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h> // USER_DS
#include <asm/prctl.h> // prctl
#include <asm/proto.h> // do_arch_prctl
#include <asm/msr.h> // wrmsr_safe
#include <asm/mmu_context.h>
#include <asm/processor.h> // load_cr3
#include <asm/i387.h>
#include <asm/atomic.h>
unsigned long get_percpu_old_rsp(void);

#include <linux/futex.h>
#define  NSIG 32

#include<linux/signal.h>
#include <linux/fcntl.h>
#include "futex_remote.h"
#include <linux/net.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <net/inet_sock.h>
//#define FPU_ 1
#undef FPU_

#define MIG_SOCKET 1
#define MIG_BIND 2
#define MIG_LISTEN 3
#define MIG_ACCEPT 4
/**
 * General purpose configuration
 */

// Flag indiciating whether or not to migrate the entire virtual 
// memory space when a migration occurs.  
#define COPY_WHOLE_VM_WITH_MIGRATION 0

// Flag indicating whether or not to migrate file-backed executable
// pages when a fault occurs accessing executable memory.  When this
// flag is 1, those pages will be migrated.  When it is 0, the local
// file-system will be consulted instead.
#define MIGRATE_EXECUTABLE_PAGES_ON_DEMAND 1

// The maximum number of contiguously physical mapped regions to 
// migrate in response to a mapping query.
#define MAX_MAPPINGS 1

// Whether or not to expose a proc entry that we can publish
// information to.
#undef PROCESS_SERVER_HOST_PROC_ENTRY
//#define PROCESS_SERVER_HOST_PROC_ENTRY



/**
 * Use the preprocessor to turn off printk.
 */
#define POPCORN_MAX_PATH 512 
#define PROCESS_SERVER_VERBOSE 0
#if PROCESS_SERVER_VERBOSE
#define PSPRINTK(...) printk(__VA_ARGS__)
#else
#define PSPRINTK(...) ;
#endif

#define PROCESS_SERVER_INSTRUMENT_LOCK 0
#if PROCESS_SERVER_VERBOSE && PROCESS_SERVER_INSTRUMENT_LOCK
#define PS_SPIN_LOCK(x) PSPRINTK("Acquiring spin lock in %s at line %d\n",__func__,__LINE__); \
                       spin_lock(x); \
                       PSPRINTK("Done acquiring spin lock in %s at line %d\n",__func__,__LINE__)
#define PS_SPIN_UNLOCK(x) PSPRINTK("Releasing spin lock in %s at line %d\n",__func__,__LINE__); \
                          spin_unlock(x); \
                          PSPRINTK("Done releasing spin lock in %s at line %d\n",__func__,__LINE__)
#define PS_DOWN_READ(x) PSPRINTK("Acquiring read lock in %s at line %d\n",__func__,__LINE__); \
                        down_read(x); \
                        PSPRINTK("Done acquiring read lock in %s at line %d\n",__func__,__LINE__)
#define PS_UP_READ(x) PSPRINTK("Releasing read lock in %s at line %d\n",__func__,__LINE__); \
                      up_read(x); \
                      PSPRINTK("Done releasing read lock in %s at line %d\n",__func__,__LINE__)
#define PS_DOWN_WRITE(x) PSPRINTK("Acquiring write lock in %s at line %d\n",__func__,__LINE__); \
                         down_write(x); \
                         PSPRINTK("Done acquiring write lock in %s at line %d\n",__func__,__LINE__)
#define PS_UP_WRITE(x) PSPRINTK("Releasing read write in %s at line %d\n",__func__,__LINE__); \
                       up_write(x); \
                       PSPRINTK("Done releasing write lock in %s at line %d\n",__func__,__LINE__)


#else
#define PS_SPIN_LOCK(x) spin_lock(x)
#define PS_SPIN_UNLOCK(x) spin_unlock(x)
#define PS_DOWN_READ(x) down_read(x)
#define PS_UP_READ(x) up_read(x)
#define PS_DOWN_WRITE(x) down_write(x)
#define PS_UP_WRITE(x) up_write(x)
#endif

/**
 * Library data type definitions
 */
#define PROCESS_SERVER_DATA_TYPE_TEST 0
#define PROCESS_SERVER_VMA_DATA_TYPE 1
#define PROCESS_SERVER_PTE_DATA_TYPE 2
#define PROCESS_SERVER_CLONE_DATA_TYPE 3
#define PROCESS_SERVER_MAPPING_REQUEST_DATA_TYPE 4
#define PROCESS_SERVER_MUNMAP_REQUEST_DATA_TYPE 5
#define PROCESS_SERVER_MM_DATA_TYPE 6
#define PROCESS_SERVER_THREAD_COUNT_REQUEST_DATA_TYPE 7
#define PROCESS_SERVER_MPROTECT_DATA_TYPE 8
#define PROCESS_SERVER_LAMPORT_BARRIER_DATA_TYPE 9
#define PROCESS_SERVER_STATS_DATA_TYPE 10

/**
 * Useful macros
 */
#define DO_UNTIL_SUCCESS(x) while(x != 0){}
struct semaphore wake_load_banlancer;
atomic_t load_balancer_req;

void wait_for_balance_req(void)
{
	down(&wake_load_banlancer);
}
EXPORT_SYMBOL(wait_for_balance_req);

void wake_load_banlancer_up(void)
{
	up(&wake_load_banlancer);
}
EXPORT_SYMBOL(wake_load_banlancer_up);

void put_request_to_balance()
{
	atomic_inc(&load_balancer_req);
}
EXPORT_SYMBOL(put_request_to_balance);

void clear_request_to_balance()
{
	atomic_set(&load_balancer_req,0);
}
EXPORT_SYMBOL(clear_request_to_balance);
unsigned int read_request_to_balance()
{
	return atomic_read(&load_balancer_req);
}
EXPORT_SYMBOL(read_request_to_balance);

/**
 * Perf
 */
#define MEASURE_PERF 0
#if MEASURE_PERF
#define PERF_INIT() perf_init()
#define PERF_MEASURE_START(x) perf_measure_start(x)
#define PERF_MEASURE_STOP(x,y,z)  perf_measure_stop(x,y,z)

pcn_perf_context_t perf_count_remote_thread_members;
pcn_perf_context_t perf_process_back_migration;
pcn_perf_context_t perf_process_mapping_request;
pcn_perf_context_t perf_process_mapping_request_search_active_mm;
pcn_perf_context_t perf_process_mapping_request_search_saved_mm;
pcn_perf_context_t perf_process_mapping_request_do_lookup;
pcn_perf_context_t perf_process_mapping_request_transmit;
pcn_perf_context_t perf_process_mapping_response;
pcn_perf_context_t perf_process_tgroup_closed_item;
pcn_perf_context_t perf_process_exit_item;
pcn_perf_context_t perf_process_mprotect_item;
pcn_perf_context_t perf_process_munmap_request;
pcn_perf_context_t perf_process_munmap_response;
pcn_perf_context_t perf_process_server_try_handle_mm_fault;
pcn_perf_context_t perf_process_server_import_address_space;
pcn_perf_context_t perf_process_server_do_exit;
pcn_perf_context_t perf_process_server_do_munmap;
pcn_perf_context_t perf_process_server_do_migration;
pcn_perf_context_t perf_process_server_do_mprotect;
pcn_perf_context_t perf_process_server_notify_delegated_subprocess_starting;
pcn_perf_context_t perf_handle_thread_group_exit_notification;
pcn_perf_context_t perf_handle_remote_thread_count_response;
pcn_perf_context_t perf_handle_remote_thread_count_request;
pcn_perf_context_t perf_handle_munmap_response;
pcn_perf_context_t perf_handle_munmap_request;
pcn_perf_context_t perf_handle_mapping_response;
pcn_perf_context_t perf_handle_mapping_request;
pcn_perf_context_t perf_handle_pte_transfer;
pcn_perf_context_t perf_handle_vma_transfer;
pcn_perf_context_t perf_handle_exiting_process_notification;
pcn_perf_context_t perf_handle_process_pairing_request;
pcn_perf_context_t perf_handle_clone_request;
pcn_perf_context_t perf_handle_mprotect_response;
pcn_perf_context_t perf_handle_mprotect_request;


extern int snull_init_module(void);


/**
 *
 */
static void perf_init(void) {
   perf_init_context(&perf_count_remote_thread_members,
           "count_remote_thread_members");
   perf_init_context(&perf_process_back_migration,
           "process_back_migration");
   perf_init_context(&perf_process_mapping_request,
           "process_mapping_request");
   perf_init_context(&perf_process_mapping_request_search_active_mm,
           "process_mapping_request_search_active_mm");
   perf_init_context(&perf_process_mapping_request_search_saved_mm,
           "process_mapping_request_search_saved_mm");
   perf_init_context(&perf_process_mapping_request_do_lookup,
           "process_mapping_request_do_lookup");
   perf_init_context(&perf_process_mapping_request_transmit,
           "process_mapping_request_transmit");
   perf_init_context(&perf_process_mapping_response,
           "process_mapping_response");
   perf_init_context(&perf_process_tgroup_closed_item,
           "process_tgroup_closed_item");
   perf_init_context(&perf_process_exit_item,
           "process_exit_item");
   perf_init_context(&perf_process_mprotect_item,
           "process_mprotect_item");
   perf_init_context(&perf_process_munmap_request,
           "process_munmap_request");
   perf_init_context(&perf_process_munmap_response,
           "process_munmap_response");
   perf_init_context(&perf_process_server_try_handle_mm_fault,
           "process_server_try_handle_mm_fault");
   perf_init_context(&perf_process_server_import_address_space,
           "process_server_import_address_space");
   perf_init_context(&perf_process_server_do_exit,
           "process_server_do_exit");
   perf_init_context(&perf_process_server_do_munmap,
           "process_server_do_munmap");
   perf_init_context(&perf_process_server_do_migration,
           "process_server_do_migration");
   perf_init_context(&perf_process_server_do_mprotect,
           "process_server_do_mprotect");
   perf_init_context(&perf_process_server_notify_delegated_subprocess_starting,
           "process_server_notify_delegated_subprocess_starting");
   perf_init_context(&perf_handle_thread_group_exit_notification,
           "handle_thread_group_exit_notification");
   perf_init_context(&perf_handle_remote_thread_count_response,
           "handle_remote_thread_count_response");
   perf_init_context(&perf_handle_remote_thread_count_request,
           "handle_remote_thread_count_request");
   perf_init_context(&perf_handle_munmap_response,
           "handle_munmap_response");
   perf_init_context(&perf_handle_munmap_request,
           "handle_munmap_request");
   perf_init_context(&perf_handle_mapping_response,
           "handle_mapping_response");
   perf_init_context(&perf_handle_mapping_request,
           "handle_mapping_request");
   perf_init_context(&perf_handle_pte_transfer,
           "handle_pte_transfer");
   perf_init_context(&perf_handle_vma_transfer,
           "handle_vma_transfer");
   perf_init_context(&perf_handle_exiting_process_notification,
           "handle_exiting_process_notification");
   perf_init_context(&perf_handle_process_pairing_request,
           "handle_process_pairing_request");
   perf_init_context(&perf_handle_clone_request,
           "handle_clone_request");
   perf_init_context(&perf_handle_mprotect_request,
           "handle_mprotect_request");
   perf_init_context(&perf_handle_mprotect_response,
           "handle_mprotect_resonse");

}

#else
#define PERF_INIT() 
#define PERF_MEASURE_START(x) -1
#define PERF_MEASURE_STOP(x, y, z)
#endif

static DECLARE_WAIT_QUEUE_HEAD( countq);
/**
 * Enums
 */
typedef enum _lamport_barrier_state {
    LAMPORT_ENTRY_OWNED,
    LAMPORT_ENTRY_OFF_LIMITS,
    LAMPORT_ENTRY_CONTENDED
} lamport_barrier_state_t;


/**
 * Library
 */
/**
 * Some piping for linking data entries
 * and identifying data entry types.
 */
typedef struct _data_header {
    struct _data_header* next;
    struct _data_header* prev;
    int data_type;
} data_header_t;

/**
 * Hold data about a pte to vma mapping.
 */
typedef struct _pte_data {
    data_header_t header;
    int vma_id;
    int clone_request_id;
    int cpu;
    unsigned long vaddr_start;
    unsigned long paddr_start;
    size_t sz;
} pte_data_t;

/**
 * Hold data about a vma to process
 * mapping.
 */
typedef struct _vma_data {
    data_header_t header;
    spinlock_t lock;
    unsigned long start;
    unsigned long end;
    int clone_request_id;
    int cpu;
    unsigned long flags;
    int vma_id;
    pgprot_t prot;
    unsigned long pgoff;
    pte_data_t* pte_list;
    int mmapping_in_progress;
    char path[256];
} vma_data_t;

typedef struct _contiguous_physical_mapping {
    unsigned char present;
    unsigned long vaddr;
    unsigned long paddr;
    size_t sz;
} contiguous_physical_mapping_t;
#define HAS_FPU_MASK 0x80
/**
 *
 */
typedef struct _clone_data {
    data_header_t header;
    spinlock_t lock;
    int clone_request_id;
    int requesting_cpu;
    char exe_path[512];
    unsigned long clone_flags;
    unsigned long stack_start;
    unsigned long stack_ptr;
    unsigned long env_start;
    unsigned long env_end;
    unsigned long arg_start;
    unsigned long arg_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long data_start;
    unsigned long data_end;
    struct pt_regs regs;
    int placeholder_pid;
    int placeholder_tgid;
    int placeholder_cpu;
    unsigned long thread_fs;
    unsigned long thread_gs;
    unsigned long thread_sp0;
    unsigned long thread_sp;
    unsigned long thread_usersp;
    unsigned short thread_es;
    unsigned short thread_ds;
    unsigned short thread_fsindex;
    unsigned short thread_gsindex;
#ifdef FPU_
    unsigned int  task_flags; //FPU, but should be extended t
    unsigned char task_fpu_counter;
    unsigned char thread_has_fpu;
    union thread_xstate fpu_state; //FPU migration
#endif
    unsigned long def_flags;
    unsigned int personality;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int t_home_cpu;
    int t_home_id;
    int prio, static_prio, normal_prio; //from sched.c
	unsigned int rt_priority; //from sched.c
	int sched_class; //from sched.c but here we are using SCHED_NORMAL, SCHED_FIFO, etc.
    unsigned long previous_cpus;
    vma_data_t* vma_list;
    vma_data_t* pending_vma_list;
    /*mklinux_akshay*/int origin_pid;
    sigset_t remote_blocked, remote_real_blocked;
    sigset_t remote_saved_sigmask;
    struct sigpending remote_pending;
    unsigned long sas_ss_sp;
    size_t sas_ss_size;
    struct k_sigaction action[_NSIG];
  //Socket Information
    int skt_flag;
    int skt_level;
    int skt_type;
    int skt_state;
    int skt_dport;
    int skt_sport;
    __be32 skt_saddr;  
    __be32 skt_daddr;
    int skt_fd;
} clone_data_t;

/**
 * 
 */
typedef struct _mapping_request_data {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long address;
    unsigned long vaddr_start;
    unsigned long vaddr_size;
    contiguous_physical_mapping_t mappings[MAX_MAPPINGS];
    pgprot_t prot;
    unsigned long vm_flags;
    unsigned char present;
    unsigned char complete;
    unsigned char from_saved_mm;
    int responses;
    int expected_responses;
    unsigned long pgoff;
    spinlock_t lock;
    char path[512];
    struct semaphore wait_sem;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long wait_time_concluded;
#endif
} mapping_request_data_t;

/**
 *
 */
typedef struct _munmap_request_data {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long vaddr_start;
    unsigned long vaddr_size;
    int responses;
    int expected_responses;
    spinlock_t lock;
} munmap_request_data_t;

/**
 *
 */
typedef struct _remote_thread_count_request_data {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    int responses;
    int expected_responses;
    int count;
    spinlock_t lock;
} remote_thread_count_request_data_t;

/**
 *
 */
typedef struct _mm_data {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    struct mm_struct* mm;
} mm_data_t;

typedef struct _mprotect_data {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long start;
    int responses;
    int expected_responses;
    spinlock_t lock;
} mprotect_data_t;

typedef struct _get_counter_phys_data {
    data_header_t header;
    int response_received;
    unsigned long resp;
} get_counter_phys_data_t;

typedef struct _lamport_barrier_entry {
    data_header_t header;
    unsigned long long timestamp;
    int responses;
    int expected_responses;
    int allow_responses;
    int is_heavy;
    int cpu;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long lock_acquired;
    unsigned long long lock_released;
#endif
} lamport_barrier_entry_t;

typedef struct _lamport_barrier_queue {
    data_header_t header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int is_heavy;
    unsigned long address;
    unsigned long long active_timestamp;
    lamport_barrier_entry_t* queue;
} lamport_barrier_queue_t;

/**
 * This message is sent to a remote cpu in order to 
 * ask it to spin up a process on behalf of the
 * requesting cpu.  Some of these fields may go
 * away in the near future.
 */
typedef struct _clone_request {
    struct pcn_kmsg_hdr header;
    int clone_request_id;
    unsigned long clone_flags;
    unsigned long stack_start;
    unsigned long stack_ptr;
    unsigned long env_start;
    unsigned long env_end;
    unsigned long arg_start;
    unsigned long arg_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long data_start;
    unsigned long data_end;
    struct pt_regs regs;
    char exe_path[512];
    int placeholder_pid;
    int placeholder_tgid;
    unsigned long thread_fs;
    unsigned long thread_gs;
    unsigned long thread_sp0;
    unsigned long thread_sp;
    unsigned long thread_usersp;
    unsigned short thread_es;
    unsigned short thread_ds;
    unsigned short thread_fsindex;
    unsigned short thread_gsindex;
#ifdef FPU_   
    unsigned int  task_flags; //FPU, but should be extended t
    unsigned char task_fpu_counter; 
    unsigned char thread_has_fpu;   
    union thread_xstate fpu_state; //FPU migration support
#endif
    unsigned long def_flags;
    unsigned int personality;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int t_home_cpu;
    int t_home_id;
    int prio, static_prio, normal_prio; //from sched.c
	unsigned int rt_priority; //from sched.c
	int sched_class; //from sched.c but here we are using SCHED_NORMAL, SCHED_FIFO, etc.
    /*mklinux_akshay*/int origin_pid;
    sigset_t remote_blocked, remote_real_blocked;
    sigset_t remote_saved_sigmask;
    struct sigpending remote_pending;
    unsigned long sas_ss_sp;
    size_t sas_ss_size;
    struct k_sigaction action[_NSIG];
    unsigned long previous_cpus;
    //Socket Information
    int skt_flag;
    int skt_level;
    int skt_type;
    int skt_state;
    int skt_dport;
    int skt_sport;
    __be32 skt_saddr;  
    __be32 skt_daddr;
    int skt_fd;
} clone_request_t;

/**
 * This message is sent in response to a clone request.
 * Its purpose is to notify the requesting cpu that
 * the specified pid is executing on behalf of the
 * requesting cpu.
 */
typedef struct _create_process_pairing {
    struct pcn_kmsg_hdr header;
    int your_pid; // PID of cpu receiving this pairing request
    int my_pid;   // PID of cpu transmitting this pairing request
} create_process_pairing_t;

/**
 * This message informs the remote cpu of delegated
 * process death.  This occurs whether the process
 * is a placeholder or a delegate locally.
 */
struct _exiting_process {
    struct pcn_kmsg_hdr header;
    int t_home_cpu;             // 4
    int t_home_id;              // 4
    int my_pid;                 // 4
    int is_last_tgroup_member;  // 4+
                                // ---
                                // 16 -> 44 bytes of padding needed
    char pad[44];
} __attribute__((packed)) __attribute__((aligned(64)));  
typedef struct _exiting_process exiting_process_t;

/**
 *
 */
struct _exiting_group {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
                                // ---
                                // 8 -> 52 bytes of padding needed
    char pad[52];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _exiting_group exiting_group_t;

/**
 * Inform remote cpu of a vma to process mapping.
 */
typedef struct _vma_transfer {
    struct pcn_kmsg_hdr header;
    int vma_id;
    int clone_request_id;
    unsigned long start;
    unsigned long end;
    pgprot_t prot;
    unsigned long flags;
    unsigned long pgoff;
    char path[256];
} vma_transfer_t;

/**
 * Inform remote cpu of a pte to vma mapping.
 */
struct _pte_transfer {
    struct pcn_kmsg_hdr header;
    int vma_id;                  //  4
    int clone_request_id;        //  4
    unsigned long vaddr_start;   //  8
    unsigned long paddr_start;   //  8
    size_t sz;                   //  4 +
                                 //  ---
                                 //  28 -> 32 bytes of padding needed
    char pad[32];
} __attribute__((packed)) __attribute__((aligned(64)));

typedef struct _pte_transfer pte_transfer_t;

/**
 *
 */
struct _mapping_request {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;          // 4
    unsigned long address;      // 8
    char need_vma;              // 1
                                // ---
                                // 21 -> 39 bytes of padding needed
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long send_time;
    char pad[31];
#else
    char pad[39];
#endif

} __attribute__((packed)) __attribute__((aligned(64)));

typedef struct _mapping_request mapping_request_t;

/*
 * type = PCN_KMSG_TYPE_PROC_SRV_THREAD_GROUP_EXITED_NOTIFICATION
 */
struct _thread_group_exited_notification {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
                                // ---
                                // 8 -> 52 bytes of padding needed
    char pad[52];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _thread_group_exited_notification thread_group_exited_notification_t;


/**
 *
 */
struct _mapping_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;                                    // 4 
    int tgroup_home_id;                                     // 4
    int requester_pid;                                      // 4
    unsigned char present;                                  // 1
    unsigned char from_saved_mm;                            // 1
    unsigned long address;                                  // 8
    unsigned long vaddr_start;                              // 8
    unsigned long vaddr_size;
    contiguous_physical_mapping_t mappings[MAX_MAPPINGS];
    pgprot_t prot;              
    unsigned long vm_flags;     
    unsigned long pgoff;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long send_time;
#endif
    char path[512]; // save to last so we can cut
                    // off data when possible.
};
typedef struct _mapping_response mapping_response_t;

/**
 * This is a hack to eliminate the overhead of sending
 * an entire mapping_response_t when there is no mapping.
 * The overhead is due to the size of the message, which
 * requires the _long pcn_kmsg variant to be used.
 */
struct _nonpresent_mapping_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    int requester_pid;              // 4
    unsigned long address;          // 8
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long send_time;   // 8
                                    // ---
                                    // 28 -> 32 bytes of padding needed
    char pad[32];
#else
    char pad[40];
#endif

} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _nonpresent_mapping_response nonpresent_mapping_response_t;

/**
 *
 */
struct _munmap_request {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;         // 4
    int tgroup_home_id;          // 4
    int requester_pid;           // 4
    unsigned long vaddr_start;   // 8
    unsigned long vaddr_size;    // 8
                                 // ---
                                 // 28 -> 32 bytes of padding needed
    char pad[32];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _munmap_request munmap_request_t;

/**
 *
 */
struct _munmap_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;          // 4
    unsigned long vaddr_start;  // 8
    unsigned long vaddr_size;   // 8+
                                // ---
                                // 28 -> 32 bytes of padding needed
    char pad[32];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _munmap_response munmap_response_t;

/**
 *
 */
struct _remote_thread_count_request {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;          // 4
                                // ---
                                // 12 -> 48 bytes of padding needed
    char pad[48];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _remote_thread_count_request remote_thread_count_request_t;

/**
 *
 */
struct _remote_thread_count_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;        // 4
    int count;                  // 4
                                // ---
                                // 16 -> 44 bytes of padding needed
    char pad[44];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _remote_thread_count_response remote_thread_count_response_t;

/**
 *
 */
struct _mprotect_request {
    struct pcn_kmsg_hdr header; 
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;          // 4
    unsigned long start;        // 8
    size_t len;                 // 4
    unsigned long prot;         // 8
                                // ---
                                // 32 -> 28 bytes of padding needed
    char pad[28];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _mprotect_request mprotect_request_t;

/**
 *
 */
struct _mprotect_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;        // 4
    int tgroup_home_id;         // 4
    int requester_pid;          // 4
    unsigned long start;        // 8
                                // ---
                                // 20 -> 40 bytes of padding needed
    char pad[40];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _mprotect_response mprotect_response_t;

/**
 *
 */
typedef struct _back_migration {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int t_home_cpu;
    int t_home_id;
    unsigned long previous_cpus;
    struct pt_regs regs;
    unsigned long thread_fs;
    unsigned long thread_gs;
    unsigned long thread_usersp;
    unsigned short thread_es;
    unsigned short thread_ds;
    unsigned short thread_fsindex;
    unsigned short thread_gsindex; 
#ifdef FPU_   
    unsigned int  task_flags; //FPU, but should be extended t
    unsigned char task_fpu_counter; 
    unsigned char thread_has_fpu;   
    union thread_xstate fpu_state; //FPU migration support
#endif
} back_migration_t;

/**
 *
 */
struct _lamport_barrier_request{
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;                   // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 36 -> 28 bytes of padding needed
    char pad[28];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_request lamport_barrier_request_t;

/**
 *
 */
struct _lamport_barrier_request_range {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;                    // 4
    size_t sz;                      // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 40 -> 20 bytes of padding needed
    char pad[20];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_request_range lamport_barrier_request_range_t;

/**
 *
 */
struct _lamport_barrier_response {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;         // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 36 -> 24 bytes of padding needed
    char pad[24];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_response lamport_barrier_response_t;

/**
 *
 */
struct _lamport_barrier_response_range {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;                   // 4
    size_t sz;                      // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 40 -> 20 bytes of padding needed
    char pad[20];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_response_range lamport_barrier_response_range_t;

/**
 *
 */
struct _lamport_barrier_release {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;                    // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 36 -> 24 bytes of padding needed
    char pad[24];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_release lamport_barrier_release_t;

/**
 *
 */
struct _lamport_barrier_release_range {
    struct pcn_kmsg_hdr header;
    int tgroup_home_cpu;            // 4
    int tgroup_home_id;             // 4
    unsigned long address;          // 8
    int is_heavy;                    // 4
    size_t sz;                      // 4
    unsigned long long timestamp;   // 16
                                    // ---
                                    // 40 -> 20 bytes of padding needed
    char pad[20];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _lamport_barrier_release_range lamport_barrier_release_range_t;

/**
 *
 */
struct _get_counter_phys_request {
    struct pcn_kmsg_hdr header;
    char pad[60];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _get_counter_phys_request get_counter_phys_request_t;

/**
 *
 */
struct _get_counter_phys_response {
    struct pcn_kmsg_hdr header;
    unsigned long resp;
    char pad[58];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _get_counter_phys_response get_counter_phys_response_t;


/**
 *
 */
typedef struct _deconstruction_data {
    int clone_request_id;
    int vma_id;
    int dst_cpu;
} deconstruction_data_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    struct task_struct *task;
    pid_t pid;
    int t_home_cpu;
    int t_home_id;
    int is_last_tgroup_member;
    struct pt_regs regs;
    unsigned long thread_fs;
    unsigned long thread_gs;
    unsigned long thread_sp0;
    unsigned long thread_sp;
    unsigned long thread_usersp;
    unsigned short thread_es;
    unsigned short thread_ds;
    unsigned short thread_fsindex;
    unsigned short thread_gsindex;

#ifdef FPU_   
    unsigned int  task_flags; //FPU, but should be extended t
    unsigned char task_fpu_counter; 
    unsigned char thread_has_fpu;   
    union thread_xstate fpu_state; //FPU migration support
#endif
} exit_work_t;
/**
 *
 */
typedef struct {
    struct work_struct work;
    clone_data_t* data;
} import_task_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
} group_exit_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long address;
    char need_vma;
    int from_cpu;
} mapping_request_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned char from_saved_mm;
    unsigned long address;      
    unsigned char present;      
    unsigned long vaddr_mapping;
    unsigned long vaddr_start;
    unsigned long vaddr_size;
    unsigned long paddr_mapping;
    size_t paddr_mapping_sz;
    pgprot_t prot;              
    unsigned long vm_flags;     
    char path[512];
    unsigned long pgoff;
    int from_cpu;
} mapping_response_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long address;
    int from_cpu;
} nonpresent_mapping_response_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
} tgroup_closed_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long vaddr_start;
    unsigned long vaddr_size;
    int from_cpu;
} munmap_request_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long vaddr_start;
    unsigned long vaddr_size;
} munmap_response_work_t;

/**
 * 
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    unsigned long start;
    size_t len;
    unsigned long prot;
    int from_cpu;
} mprotect_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int requester_pid;
    int from_cpu;
} remote_thread_count_request_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int t_home_cpu;
    int t_home_id;
    unsigned long previous_cpus;
    struct pt_regs regs;
    unsigned long thread_fs;
    unsigned long thread_gs;
    unsigned long thread_usersp;
    unsigned short thread_es;
    unsigned short thread_ds;
    unsigned short thread_fsindex;
    unsigned short thread_gsindex;
#ifdef FPU_
   unsigned int  task_flags; //FPU, but should be extended t
   unsigned char task_fpu_counter;
   unsigned char thread_has_fpu;
   union thread_xstate fpu_state; // FPU migration support
#endif
} back_migration_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    unsigned long long timestamp;
} lamport_barrier_request_work_t;
/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    unsigned long long timestamp;
} lamport_barrier_response_work_t;

/**
 * 
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    unsigned long long timestamp
} lamport_barrier_release_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    size_t sz;
    unsigned long long timestamp;
} lamport_barrier_request_range_work_t;

/**
 *
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    size_t sz;
    unsigned long long timestamp;
} lamport_barrier_response_range_work_t;

/**
 * 
 */
typedef struct {
    struct work_struct work;
    int tgroup_home_cpu;
    int tgroup_home_id;
    int from_cpu;
    unsigned long address;
    int is_heavy;
    size_t sz;
    unsigned long long timestamp
} lamport_barrier_release_range_work_t;

/**
 * Prototypes
 */
static void process_import_task(struct work_struct* work);
static int handle_clone_request(struct pcn_kmsg_message* msg);
long process_server_clone(unsigned long clone_flags,
                          unsigned long stack_start,                                                                                                                   
                          struct pt_regs *regs,
                          unsigned long stack_size,
                          struct task_struct* task);
static vma_data_t* find_vma_data(clone_data_t* clone_data, unsigned long addr_start);
static clone_data_t* find_clone_data(int cpu, int clone_request_id);
static void dump_mm(struct mm_struct* mm);
static void dump_task(struct task_struct* task,struct pt_regs* regs,unsigned long stack_ptr);
static void dump_thread(struct thread_struct* thread);
static void dump_regs(struct pt_regs* regs);
static void dump_stk(struct thread_struct* thread, unsigned long stack_ptr); 

/**
 * Prototypes from parts of the kernel that I modified or made available to external
 * modules.
 */
// I removed the 'static' modifier in mm/memory.c for do_wp_page so I could use it 
// here.
int do_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
               unsigned long address, pte_t *page_table, pmd_t *pmd,
               spinlock_t *ptl, pte_t orig_pte);
int do_mprotect(struct task_struct* task, struct mm_struct* mm, unsigned long start, size_t len, unsigned long prot, int do_remote);
#ifndef PROCESS_SERVER_USE_KMOD
extern int exec_mmap(struct mm_struct* mm);
extern void start_remote_thread(struct pt_regs* regs);
extern void flush_old_files(struct files_struct * files);
#endif
static unsigned long get_next_ts_value(void);

extern struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed);
/**
 * Module variables
 */
static int _vma_id = 0;
static int _clone_request_id = 0;
static int _cpu = -1;
static unsigned long long perf_a, perf_b, perf_c, perf_d, perf_e;
data_header_t* _saved_mm_head = NULL;             // Saved MM list
DEFINE_SPINLOCK(_saved_mm_head_lock);             // Lock for _saved_mm_head
data_header_t* _mapping_request_data_head = NULL; // Mapping request data head
DEFINE_SPINLOCK(_mapping_request_data_head_lock);  // Lock for above
data_header_t* _count_remote_tmembers_data_head = NULL;
DEFINE_SPINLOCK(_count_remote_tmembers_data_head_lock);
data_header_t* _munmap_data_head = NULL;
DEFINE_SPINLOCK(_munmap_data_head_lock);
data_header_t* _mprotect_data_head = NULL;
DEFINE_SPINLOCK(_mprotect_data_head_lock);
data_header_t* _data_head = NULL;                 // General purpose data store
DEFINE_SPINLOCK(_data_head_lock);                 // Lock for _data_head
DEFINE_SPINLOCK(_vma_id_lock);                    // Lock for _vma_id
DEFINE_SPINLOCK(_clone_request_id_lock);          // Lock for _clone_request_id
struct rw_semaphore _import_sem;
DEFINE_SPINLOCK(_remap_lock);
data_header_t* _lamport_barrier_queue_head = NULL;
DEFINE_SPINLOCK(_lamport_barrier_queue_lock);
unsigned long* ts_counter = NULL;
get_counter_phys_data_t* get_counter_phys_data = NULL;

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
struct proc_dir_entry *_proc_entry = NULL;
struct proc_dir_entry *_lamport_proc_entry = NULL;
static void proc_track_data(int entry, unsigned long long time);//proto
static void proc_data_init();
typedef struct _proc_data {
    int count;
    unsigned long long total;
    unsigned long long min;
    unsigned long long max;
    char name[256];
} proc_data_t;
typedef enum _proc_data_index{
    PS_PROC_DATA_MAPPING_WAIT_TIME=0,
    PS_PROC_DATA_MAPPING_POST_WAIT_TIME_RESUME,
    PS_PROC_DATA_MAPPING_REQUEST_SEND_TIME,
    PS_PROC_DATA_MAPPING_RESPONSE_SEND_TIME,
    PS_PROC_DATA_MAPPING_REQUEST_DELIVERY_TIME,
    PS_PROC_DATA_MAPPING_RESPONSE_DELIVERY_TIME,
    PS_PROC_DATA_MAPPING_REQUEST_PROCESSING_TIME,
    PS_PROC_DATA_BREAK_COW_TIME,
    PS_PROC_DATA_FAULT_PROCESSING_TIME,
    PS_PROC_DATA_ADJUSTED_PERMISSIONS,
    PS_PROC_DATA_NEWVMA_ANONYMOUS_PTE,
    PS_PROC_DATA_NEWVMA_ANONYMOUS_NOPTE,
    PS_PROC_DATA_NEWVMA_FILEBACKED_PTE,
    PS_PROC_DATA_NEWVMA_FILEBACKED_NOPTE,
    PS_PROC_DATA_OLDVMA_ANONYMOUS_PTE,
    PS_PROC_DATA_OLDVMA_ANONYMOUS_NOPTE,
    PS_PROC_DATA_OLDVMA_FILEBACKED_PTE,
    PS_PROC_DATA_OLDVMA_FILEBACKED_NOPTE,
    PS_PROC_DATA_MUNMAP_PROCESSING_TIME,
    PS_PROC_DATA_MUNMAP_REQUEST_PROCESSING_TIME,
    PS_PROC_DATA_MPROTECT_PROCESSING_TIME,
    PS_PROC_DATA_MPROTECT_REQUEST_PROCESSING_TIME,
    PS_PROC_DATA_EXIT_PROCESSING_TIME,
    PS_PROC_DATA_EXIT_NOTIFICATION_PROCESSING_TIME,
    PS_PROC_DATA_GROUP_EXIT_PROCESSING_TIME,
    PS_PROC_DATA_GROUP_EXIT_NOTIFICATION_PROCESSING_TIME,
    PS_PROC_DATA_IMPORT_TASK_TIME,
    PS_PROC_DATA_COUNT_REMOTE_THREADS_PROCESSING_TIME,
    PS_PROC_DATA_MK_PAGE_WRITABLE,
    PS_PROC_DATA_WAITING_FOR_LAMPORT_LOCK,
    PS_PROC_DATA_LAMPORT_LOCK_HELD,
    PS_PROC_DATA_MAX
} proc_data_index_t;
proc_data_t _proc_data[NR_CPUS][PS_PROC_DATA_MAX];

typedef struct proc_xfer {
    unsigned long long total;
    int count;
    unsigned long long min;
    unsigned long long max;
} proc_xfer_t;

struct _stats_clear {
    struct pcn_kmsg_hdr header;
    char pad[60];
} __attribute__((packed)) __attribute__((aligned(64)));;
typedef struct _stats_clear stats_clear_t;

struct _stats_query {
    struct pcn_kmsg_hdr header;
    pid_t pid;
    char pad[56];
} __attribute__((packed)) __attribute__((aligned(64)));
typedef struct _stats_query stats_query_t;

struct _stats_response {
    struct pcn_kmsg_hdr header;
    pid_t pid;
    proc_xfer_t data[PS_PROC_DATA_MAX];
} __attribute__((packed)) __attribute__((aligned(64))); 
typedef struct _stats_response stats_response_t;

typedef struct _stats_query_data {
    data_header_t header;
    int expected_responses;
    int responses;
    pid_t pid;
} stats_query_data_t;

typedef struct {
    struct work_struct work;
    int pid;
    int from_cpu;
} stats_query_work_t;

#define PS_PROC_DATA_TRACK(x,y) proc_track_data(x,y)
#define PS_PROC_DATA_INIT() proc_data_init()

#else
#define PS_PROC_DATA_TRACK(x,y)
#define PS_PROC_DATA_INIT()
#endif

// Work Queues
static struct workqueue_struct *clone_wq;
static struct workqueue_struct *exit_wq;
static struct workqueue_struct *mapping_wq;

/**
 * General helper functions and debugging tools
 */

/**
 * TODO
 */
static bool __user_addr (unsigned long x ) {
    return (x < PAGE_OFFSET);   
}

// TODO the cpu_has_known_tgroup_mm must be reworked, i.e. the map must be pointed by the threads NOT one copy per thread, anti scaling and redudandt information
/**
 *
 */
static int cpu_has_known_tgroup_mm(int cpu)
{
#ifdef SUPPORT_FOR_CLUSTERING
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    struct cpumask *pcpum =0;
    int cpuid =-1;
extern struct list_head rlist_head;
    if (cpumask_test_cpu(cpu, cpu_present_mask))
	return 1;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        cpuid = objPtr->_data._processor;
        pcpum = &(objPtr->_data._cpumask);
        if (cpumask_test_cpu(cpu, pcpum)) {
	    if ( bitmap_intersects(cpumask_bits(pcpum),
			           &(current->known_cpu_with_tgroup_mm),
			           (sizeof(unsigned long) *8)) ) {
	        return 1;
            }
	    return 0;
	}
    }
    printk(KERN_ERR"%s: ERROR the input cpu (%d) is not included in any known cpu cluster\n",
		__func__, cpu);
    return 0;
#else
    if(test_bit(cpu,&current->known_cpu_with_tgroup_mm)) {
        return 1;
    }
    return 0;
#endif
}

/**
 *
 */
static void set_cpu_has_known_tgroup_mm(struct task_struct *task,int cpu) {
    struct task_struct *me = task;
    struct task_struct *t = me;
    do {
        set_bit(cpu,&t->known_cpu_with_tgroup_mm);
    } while_each_thread(me, t);
}

/**
 * @brief find_vma does not always return the correct vm_area_struct*.
 * If it fails to find a vma for the specified address, it instead
 * returns the closest one in the rb list.  This function looks
 * for this failure, and returns NULL in this error condition.
 * Otherwise, it returns a pointer to the struct vm_area_struct
 * containing the specified address.
 */
static struct vm_area_struct* find_vma_checked(struct mm_struct* mm, unsigned long address) {
    struct vm_area_struct* vma = find_vma(mm,address&PAGE_MASK);
    if( vma == NULL ||
        (vma->vm_start > (address & PAGE_MASK)) ||
        (vma->vm_end <= address) ) {
        
        vma = NULL;
    }

    return vma;
}

/**
 * Note, mm->mmap_sem must already be held!
 */
/*static int is_mapped(struct mm_struct* mm, unsigned vaddr) {
    pte_t* pte = NULL;
    pmd_t* pmd = NULL;
    pud_t* pud = NULL;
    pgd_t* pgd = NULL;
    int ret = 0;

    pgd = pgd_offset(mm, vaddr);
    if(pgd_present(*pgd) && pgd_present(*pgd)) {
        pud = pud_offset(pgd,vaddr); 
        if(pud_present(*pud)) {
            pmd = pmd_offset(pud,vaddr);
            if(pmd_present(*pmd)) {
                pte = pte_offset_map(pmd,vaddr);
                if(pte && !pte_none(*pte)) {
                    // It exists!
                    ret = 1;
                }
            }
        }
    }
    return ret;

}*/
// Antonio's Version
static int is_mapped(struct mm_struct* mm, unsigned vaddr)
{
    pte_t* pte = NULL;
    pmd_t* pmd = NULL;                                                             
    pud_t* pud = NULL;                                                             
    pgd_t* pgd = NULL; 

    pgd = pgd_offset(mm, vaddr);                                                   
    if (pgd && !pgd_none(*pgd) && likely(!pgd_bad(*pgd)) && pgd_present(*pgd)) {
        pud = pud_offset(pgd,vaddr);                                               
        if (pud && !pud_none(*pud) && likely(!pud_bad(*pud)) && pud_present(*pud)) {

            pmd = pmd_offset(pud,vaddr);
            if(pmd && !pmd_none(*pmd) && likely(!pmd_bad(*pmd)) && pmd_present(*pmd)) {             
                pte = pte_offset_map(pmd,vaddr);                                   
                if(pte && !pte_none(*pte) && pte_present(*pte)) { 
                   // It exists!                                                  
                    return 1;
          }                                                                  
        }                                                                      
      }                                                                          
    }
    return 0;
}


/**
 * @brief Find the mm_struct for a given distributed thread.  
 * If one does not exist, then return NULL.
 */
static struct mm_struct* find_thread_mm(
        int tgroup_home_cpu, 
        int tgroup_home_id, 
        mm_data_t **used_saved_mm,
        struct task_struct** task_out)
{

    struct task_struct *task, *g;
    struct mm_struct * mm = NULL;
    data_header_t* data_curr;
    mm_data_t* mm_data;
    unsigned long lockflags;

    *used_saved_mm = NULL;
    *task_out = NULL;

    // First, look through all active processes.
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {
        if(task->tgroup_home_cpu == tgroup_home_cpu &&
           task->tgroup_home_id  == tgroup_home_id) {
            mm = task->mm;
            *task_out = task;
            *used_saved_mm = NULL;
            read_unlock(&tasklist_lock);
            goto out;
        }
    } while_each_thread(g,task);
    read_unlock(&tasklist_lock);

    // Failing that, look through saved mm's.
    spin_lock_irqsave(&_saved_mm_head_lock,lockflags);
    data_curr = _saved_mm_head;
    while(data_curr) {

        mm_data = (mm_data_t*)data_curr;
    
        if((mm_data->tgroup_home_cpu == tgroup_home_cpu) &&
           (mm_data->tgroup_home_id  == tgroup_home_id)) {
            mm = mm_data->mm;
            *used_saved_mm = mm_data;
            break;
        }

        data_curr = data_curr->next;

    } // while

    spin_unlock_irqrestore(&_saved_mm_head_lock,lockflags);


out:
    return mm;
}



/**
 * @brief A best effort at making a page writable
 * @return void
 */
static void mk_page_writable(struct mm_struct* mm,
                             struct vm_area_struct* vma,
                             unsigned long vaddr) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif
    spinlock_t* ptl;
    pte_t *ptep, pte, entry;
     
    // Grab the pte, and lock it     
    ptep = get_locked_pte(mm, vaddr, &ptl);
    if (!ptep)
        goto out;

    // grab the contents of the pte pointer
    pte = *ptep;
    
    if(pte_none(*ptep)) {
        pte_unmap_unlock(pte,ptl);
        goto out;
    }

    arch_enter_lazy_mmu_mode();

    // Make the content copy writable and dirty, then
    // write it back into the page tables.
    entry = pte_mkwrite(pte_mkdirty(pte));
    set_pte_at(mm, vaddr, ptep, entry);

    update_mmu_cache(vma, vaddr, ptep);

    arch_leave_lazy_mmu_mode();

    // Unlock the pte
    pte_unmap_unlock(pte, ptl);
out:
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MK_PAGE_WRITABLE,total_time);
#endif
    return;
}

/**
 *
 */
static void mk_page_writable_lookupvma(struct mm_struct*mm,
                             unsigned long addr) {
    struct vm_area_struct* curr = mm->mmap;
    while(curr) {
        if(curr->vm_start <= addr && curr->vm_end > addr) {
            mk_page_writable(mm,curr,addr);
            break;
        }
        curr = curr->vm_next;
    }
}

/**
 * @brief Check to see if a given page is writable.
 * @return 0 if not writable or error, not zero otherwise
 */
static int is_page_writable(struct mm_struct* mm,
                            struct vm_area_struct* vma,
                            unsigned long addr) {
    spinlock_t* ptl;
    pte_t *ptep, pte;
    int ret = 0;

    ptep = get_locked_pte(mm,addr,&ptl);
    if(!ptep)
        goto out;

    pte = *ptep;
    
    if(pte_none(*ptep)) {
        pte_unmap_unlock(*ptep,ptl);
        ret = -1;
        goto out;
    }

    ret = pte_write(pte);

    pte_unmap_unlock(pte, ptl);

out:
    return ret;
}

/**
 * @brief Get the clone data associated with the current task.
 * @return clone_data_t* or NULL if not present
 */
static clone_data_t* get_current_clone_data(void) {
    clone_data_t* ret = NULL;

    if(!current->clone_data) {
        // Do costly lookup
        ret = find_clone_data(current->prev_cpu,
                                 current->clone_request_id);
        // Store it for easy access next time.
        current->clone_data = ret;
    } else {
        ret = (clone_data_t*)current->clone_data;
    }

    return ret;
}


/**
 * @brief Page walk has encountered a pte while deconstructing
 * the client side processes address space.  Transfer it.
 */
/*static int deconstruction_page_walk_pte_entry_callback(pte_t *pte, 
        unsigned long start, unsigned long end, struct mm_walk *walk) {

    deconstruction_data_t* decon_data = (deconstruction_data_t*)walk->private;
    int vma_id = decon_data->vma_id;
    int dst_cpu = decon_data->dst_cpu;
    int clone_request_id = decon_data->clone_request_id;
    pte_transfer_t pte_xfer;

    if(NULL == pte || !pte_present(*pte)) {
        return 0;
    }

    pte_xfer.header.type = PCN_KMSG_TYPE_PROC_SRV_PTE_TRANSFER;
    pte_xfer.header.prio = PCN_KMSG_PRIO_NORMAL;
    pte_xfer.paddr = (pte_val(*pte) & PHYSICAL_PAGE_MASK) | (start & (PAGE_SIZE-1));
    // NOTE: Found the above pte to paddr conversion here -
    // http://wbsun.blogspot.com/2010/12/convert-userspace-virtual-address-to.html
    pte_xfer.vaddr = start;
    pte_xfer.vma_id = vma_id;
    pte_xfer.clone_request_id = clone_request_id;
    pte_xfer.pfn = pte_pfn(*pte);
    PSPRINTK("Sending PTE\n"); 
    DO_UNTIL_SUCCESS(pcn_kmsg_send(dst_cpu, (struct pcn_kmsg_message *)&pte_xfer));

    return 0;
}*/

/**
 * @brief Callback used when walking a memory map.  It looks to see
 * if the page is present.  If present, it resolves the given
 * address.
 * @return always returns 0
 */
static int vm_search_page_walk_pte_entry_callback(pte_t *pte, unsigned long start, unsigned long end, struct mm_walk *walk) {
 
    unsigned long* resolved_addr = (unsigned long*)walk->private;

    if (pte == NULL || pte_none(*pte) || !pte_present(*pte)) {
        *resolved_addr = 0;
        return 0;
    }

    // Store the resolved address in the address
    // pointed to by the private field of the walk
    // structure.  This is checked by the caller
    // of the walk function when the walk is complete.
    *resolved_addr = (pte_val(*pte) & PHYSICAL_PAGE_MASK) | (start & (PAGE_SIZE-1));
    return 0;
}

/**
 * @brief Retrieve the physical address of the specified virtual address.
 * @return -1 indicates failure.  Otherwise, 0 is returned.
 */
static int get_physical_address(struct mm_struct* mm, 
                                unsigned long vaddr,
                                unsigned long* paddr) {
    unsigned long resolved = 0;
    struct mm_walk walk = {
        .pte_entry = vm_search_page_walk_pte_entry_callback,
        .private = &(resolved),
        .mm = mm
    };

    // Walk the page tables.  The walk handler modifies the
    // resolved variable if it finds the address.
    walk_page_range(vaddr & PAGE_MASK, (vaddr & PAGE_MASK) + PAGE_SIZE, &walk);
    if(resolved == 0) {
        return -1;
    }

    // Set the output
    *paddr = resolved;

    return 0;
}

/**
 * Check to see if the specified virtual address has a 
 * corresponding physical address mapped to it.
 * @return 0 = no mapping, 1 = mapping present
 */
static int is_vaddr_mapped(struct mm_struct* mm, unsigned long vaddr) {
    unsigned long resolved = 0;
    struct mm_walk walk = {
        .pte_entry = vm_search_page_walk_pte_entry_callback,
        .private = &(resolved),
        .mm = mm
    };

    // Walk the page tables.  The walk handler will set the
    // resolved variable if it finds the mapping.  
    walk_page_range(vaddr & PAGE_MASK, ( vaddr & PAGE_MASK ) + PAGE_SIZE, &walk);
    if(resolved != 0) {
        return 1;
    }
    return 0;
}

/**
 * @brief Determine if the specified vma can have cow mapings.
 * @return 1 = yes, 0 = no.
 */
static int is_maybe_cow(struct vm_area_struct* vma) {
    if((vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) != VM_MAYWRITE) {
        // Not a cow vma
        return 0;
    }

    if(!(vma->vm_flags & VM_WRITE)) {
        return 0;
    }

    return 1;
}

/**
 * @brief Break the COW page that contains "address", iff that page
 * is a COW page.
 * @return 1 = handled, 0 = not handled.
 * @prerequisite Caller must grab mm->mmap_sem
 */
static int break_cow(struct mm_struct *mm, struct vm_area_struct* vma, unsigned long address) {
    pgd_t *pgd = NULL;
    pud_t *pud = NULL;
    pmd_t *pmd = NULL;
    pte_t *ptep = NULL;
    pte_t pte;
    spinlock_t* ptl;

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time = 0;
    unsigned long long total_time = 0;
    unsigned long long start_time = native_read_tsc();
#endif
    //PSPRINTK("%s: entered\n",__func__);

    // if it's not a cow mapping, return.
    if((vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) != VM_MAYWRITE) {
        goto not_handled;
    }

    // if it's not writable in vm_flags, return.
    if(!(vma->vm_flags & VM_WRITE)) {
        goto not_handled;
    }

    pgd = pgd_offset(mm, address);
    if(!pgd_present(*pgd)) {
        goto not_handled_unlock;
    }

    pud = pud_offset(pgd,address);
    if(!pud_present(*pud)) {
        goto not_handled_unlock;
    }

    pmd = pmd_offset(pud,address);
    if(!pmd_present(*pmd)) {
        goto not_handled_unlock;
    }

    ptep = pte_offset_map(pmd,address);
    if(!ptep || !pte_present(*ptep) || pte_none(*ptep)) {
        pte_unmap(ptep);
        goto not_handled_unlock;
    }

    pte = *ptep;

    if(pte_write(pte)) {
        goto not_handled_unlock;
    }
    
    // break the cow!
    ptl = pte_lockptr(mm,pmd);
    PS_SPIN_LOCK(ptl);
   
    PSPRINTK("%s: proceeding on address %lx\n",__func__,address);
    do_wp_page(mm,vma,address,ptep,pmd,ptl,pte);


    // NOTE:
    // Do not call pte_unmap_unlock(ptep,ptl), since do_wp_page does that!
    
    goto handled;

not_handled_unlock:
not_handled:
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_BREAK_COW_TIME,total_time);
#endif
    return 0;
handled:
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_BREAK_COW_TIME,total_time);
#endif
    return 1;
}

/**
 *  @brief Find the bounds of a physically consecutive mapped region.
 *  The region must be contained within the specified VMA.
 *
 *  Hypothetical page table mappings for a given VMA:
 *
 *  *********************************
 *  *    Vaddr      *   Paddr       *
 *  *********************************
 *  * 0x10000000    * 0x12341000    *
 *  *********************************
 *  * 0x10001000    * 0x12342000    *
 *  *********************************
 *  * 0x10002000    * 0x12343000    *
 *  *********************************
 *  * 0x10003000    * 0x43214000    *
 *  *********************************
 *  
 *  This function, given a vaddr of 12342xxx will return:
 *  *vaddr_mapping_start = 0x10000000
 *  *paddr_mapping_start = 0x12341000
 *  *paddr_mapping_sz    = 0x3000
 *
 *  Notice 0x10003000 and above is not included in the returned region, as
 *  its paddr is not consecutive with the previous mappings.
 *
 */
int find_consecutive_physically_mapped_region(struct mm_struct* mm,
                                              struct vm_area_struct* vma,
                                              unsigned long vaddr,
                                              unsigned long* vaddr_mapping_start,
                                              unsigned long* paddr_mapping_start,
                                              size_t* paddr_mapping_sz,
                                              int br_cow) {
    unsigned long paddr_curr = NULL;
    unsigned long vaddr_curr = vaddr;
    unsigned long vaddr_next = vaddr;
    unsigned long paddr_next = NULL;
    unsigned long paddr_start = NULL;
    size_t sz = 0;

    
    // Initializes paddr_curr
    if(br_cow) {
        break_cow(mm,vma,vaddr_curr);
    }
    if(get_physical_address(mm,vaddr_curr,&paddr_curr) < 0) {
        return -1;
    }
    paddr_start = paddr_curr;
    *vaddr_mapping_start = vaddr_curr;
    *paddr_mapping_start = paddr_curr;
    
    sz = PAGE_SIZE;

    // seek up in memory
    // This stretches (sz) only while leaving
    // vaddr and paddr the samed
    while(1) {
        vaddr_next += PAGE_SIZE;
        
        // dont' go past the end of the vma
        if(vaddr_next >= vma->vm_end) {
            break;
        }

        if(br_cow) {
            break_cow(mm,vma,vaddr_next);
        }

        if(get_physical_address(mm,vaddr_next,&paddr_next) < 0) {
            break;
        }

        if(paddr_next == paddr_curr + PAGE_SIZE) {
            sz += PAGE_SIZE;
            paddr_curr = paddr_next;
        } else {
            break;
        }
    }

    // seek down in memory
    // This stretches sz, and the paddr and vaddr's
    vaddr_curr = vaddr;
    paddr_curr = paddr_start; 
    vaddr_next = vaddr_curr;
    while(1) {
        vaddr_next -= PAGE_SIZE;

        // don't go past the start of the vma
        if(vaddr_next < vma->vm_start) {
            break;
        }

        if(br_cow) {
            break_cow(mm,vma,vaddr_next);
        }

        if(get_physical_address(mm,vaddr_next,&paddr_next) < 0) {
            break;
        }

        if(paddr_next == (paddr_curr - PAGE_SIZE)) {
            vaddr_curr = vaddr_next;
            paddr_curr = paddr_next;
            sz += PAGE_SIZE;
        } else {
            break;
        }
    }
   
    *vaddr_mapping_start = vaddr_curr;
    *paddr_mapping_start = paddr_curr;
    *paddr_mapping_sz = sz;

    PSPRINTK("%s: found consecutive area- vaddr{%lx}, paddr{%lx}, sz{%d}\n",
                __func__,
                *vaddr_mapping_start,
                *paddr_mapping_start,
                *paddr_mapping_sz);

    return 0;
}

/**
 * @brief Find the preceeding physically consecutive region.  This is a region
 * that starts BEFORE the specified vaddr.  The region must be contained 
 * within the specified VMA.
 */
int find_prev_consecutive_physically_mapped_region(struct mm_struct* mm,
                                              struct vm_area_struct* vma,
                                              unsigned long vaddr,
                                              unsigned long* vaddr_mapping_start,
                                              unsigned long* paddr_mapping_start,
                                              size_t* paddr_mapping_sz,
                                              int break_cow) {
    unsigned long curr_vaddr_mapping_start;
    unsigned long curr_paddr_mapping_start;
    unsigned long curr_paddr_mapping_sz;
    unsigned long curr_vaddr = vaddr;
    int ret = -1;

    if(curr_vaddr < vma->vm_start) return -1;

    do {
        int res = find_consecutive_physically_mapped_region(mm,
                                                     vma,
                                                     curr_vaddr,
                                                     &curr_vaddr_mapping_start,
                                                     &curr_paddr_mapping_start,
                                                     &curr_paddr_mapping_sz,
                                                     break_cow);
        if(0 == res) {

            // this is a match, we can store off results and exit
            ret = 0;
            *vaddr_mapping_start = curr_vaddr_mapping_start;
            *paddr_mapping_start = curr_paddr_mapping_start;
            *paddr_mapping_sz    = curr_paddr_mapping_sz;
            break;
        }

        curr_vaddr -= PAGE_SIZE;
    } while (curr_vaddr >= vma->vm_start);

    return ret;

}
/**
 * @brief Find the next physically consecutive region.  This is a region
 * that starts AFTER the specified vaddr.  The region must be contained
 * within the specified VMA.
 */
int find_next_consecutive_physically_mapped_region(struct mm_struct* mm,
                                              struct vm_area_struct* vma,
                                              unsigned long vaddr,
                                              unsigned long* vaddr_mapping_start,
                                              unsigned long* paddr_mapping_start,
                                              size_t* paddr_mapping_sz,
                                              int break_cow) {
    unsigned long curr_vaddr_mapping_start;
    unsigned long curr_paddr_mapping_start;
    unsigned long curr_paddr_mapping_sz;
    unsigned long curr_vaddr = vaddr;
    int ret = -1;

    if(curr_vaddr >= vma->vm_end) return -1;

    do {
        int res = find_consecutive_physically_mapped_region(mm,
                                                     vma,
                                                     curr_vaddr,
                                                     &curr_vaddr_mapping_start,
                                                     &curr_paddr_mapping_start,
                                                     &curr_paddr_mapping_sz,
                                                     break_cow);
        if(0 == res) {

            // this is a match, we can store off results and exit
            ret = 0;
            *vaddr_mapping_start = curr_vaddr_mapping_start;
            *paddr_mapping_start = curr_paddr_mapping_start;
            *paddr_mapping_sz    = curr_paddr_mapping_sz;
            break;
        }

        curr_vaddr += PAGE_SIZE;
    } while (curr_vaddr < vma->vm_end);

    return ret;

}

/**
 *  @brief Fill the array with as many physically consecutive regions
 *  as are present and will fit (specified by arr_sz).
 */
int fill_physical_mapping_array(struct mm_struct* mm,
        struct vm_area_struct* vma,
        unsigned long address,
        contiguous_physical_mapping_t* mappings, 
        int arr_sz,
        int break_cow) {
    int i;
    unsigned long next_vaddr = address & PAGE_MASK;
    int ret = -1;
    unsigned long smallest_in_first_round = next_vaddr;

    PSPRINTK("%s: entered\n",__func__);

    for(i = 0; i < arr_sz; i++) 
        mappings[i].present = 0;

    for(i = 0; i < arr_sz && next_vaddr < vma->vm_end; i++) {
        int valid_mapping = find_next_consecutive_physically_mapped_region(mm,
                                            vma,
                                            next_vaddr,
                                            &mappings[i].vaddr,
                                            &mappings[i].paddr,
                                            &mappings[i].sz,
                                            break_cow);


        if(valid_mapping == 0) {
            PSPRINTK("%s: supplying a mapping in slot %d\n",__func__,i);
            if(address >= mappings[i].vaddr && 
                    address < mappings[i].vaddr + mappings[i].sz)
                ret = 0;

            if(mappings[i].vaddr < smallest_in_first_round)
                smallest_in_first_round = mappings[i].vaddr;

            mappings[i].present = 1;
            next_vaddr = mappings[i].vaddr + mappings[i].sz;

        } else {
            PSPRINTK("%s: up search ended in failure, resuming down search\n",
                    __func__);
            mappings[i].present = 0;
            mappings[i].vaddr = 0;
            mappings[i].paddr = 0;
            mappings[i].sz = 0;
            break;
        }
    }

    // If we have room left, go in the opposite direction
    if(i <= arr_sz -1) {
        next_vaddr = smallest_in_first_round - PAGE_SIZE;
        for(;i < arr_sz && next_vaddr >= vma->vm_start; i++) {
            int valid_mapping = find_prev_consecutive_physically_mapped_region(mm,
                                            vma,
                                            next_vaddr,
                                            &mappings[i].vaddr,
                                            &mappings[i].paddr,
                                            &mappings[i].sz,
                                            break_cow);
            if(valid_mapping == 0) {
                PSPRINTK("%s: supplying a mapping in slot %d\n",__func__,i);
                mappings[i].present = 1;
                next_vaddr = mappings[i].vaddr - PAGE_SIZE;
            } else {
                mappings[i].present = 0;
                mappings[i].vaddr = 0;
                mappings[i].paddr = 0;
                mappings[i].sz = 0;
                break;
            }
        }
    }

    // Trim any entries that extend beyond the boundaries of the vma
    for(i = 0; i < MAX_MAPPINGS; i++) {
        if(mappings[i].present) {
            if(mappings[i].vaddr < vma->vm_start) {
                unsigned long sz_diff = vma->vm_start - mappings[i].vaddr;
                PSPRINTK("Trimming mapping, since it starts too low in memory\n");
                if(mappings[i].sz > sz_diff) {
                    mappings[i].sz -= sz_diff;
                    mappings[i].vaddr = vma->vm_start;
                } else {
                    mappings[i].present = 0;
                    mappings[i].vaddr = 0;
                    mappings[i].paddr = 0;
                    mappings[i].sz = 0;
                }
            }

            if(mappings[i].vaddr + mappings[i].sz >= vma->vm_end) {
                unsigned long sz_diff = mappings[i].vaddr + 
                                        mappings[i].sz - 
                                        vma->vm_end;
                PSPRINTK("Trimming mapping, since it ends too high in memory\n");
                if(mappings[i].sz > sz_diff) {
                    mappings[i].sz -= sz_diff;
                } else {
                    mappings[i].present = 0;
                    mappings[i].vaddr = 0;
                    mappings[i].paddr = 0;
                    mappings[i].sz = 0;
                }
            }
        }
    }

    // Clear out what we just did
    if(ret == -1) {
        PSPRINTK("%s: zeroing out responses, due to an error\n",__func__);
        for(i = 0; i < arr_sz; i++)
            mappings[i].present = 0;
    }

    PSPRINTK("%s: exiting\n",__func__);

    return ret;
}

/**
 * @brief Call remap_pfn_range on the parts of the specified virtual-physical
 * region that are not already mapped.
 * @precondition mm->mmap_sem must already be held by caller.
 */
int remap_pfn_range_remaining(struct mm_struct* mm,
                                  struct vm_area_struct* vma,
                                  unsigned long vaddr_start,
                                  unsigned long paddr_start,
                                  size_t sz,
                                  pgprot_t prot,
                                  int make_writable) {
    unsigned long vaddr_curr;
    unsigned long paddr_curr = paddr_start;
    int ret = 0, val;
    int err;

    PSPRINTK("%s: entered vaddr_start{%lx}, paddr_start{%lx}, sz{%x}\n",
            __func__,
            vaddr_start,
            paddr_start,
            sz);

    for(vaddr_curr = vaddr_start; 
        vaddr_curr < vaddr_start + sz; 
        vaddr_curr += PAGE_SIZE) {
        //if( !(val = is_vaddr_mapped(mm,vaddr_curr)) ) {
        if(!is_vaddr_mapped(mm,vaddr_curr)) {
            //PSPRINTK("%s: mapping vaddr{%lx} paddr{%lx}\n",__func__,vaddr_curr,paddr_curr);
            // not mapped - map it
            err = remap_pfn_range(vma,
                                  vaddr_curr,
                                  paddr_curr >> PAGE_SHIFT,
                                  PAGE_SIZE,
                                  prot);
            if(err == 0) {
                PSPRINTK("%s: succesfully mapped vaddr{%lx} to paddr{%lx}\n",
                            __func__,vaddr_curr,paddr_curr);
                if(make_writable && vma->vm_flags & VM_WRITE) {
                    mk_page_writable(mm, vma, vaddr_curr);
                }
            } else {
                printk(KERN_ALERT"%s: ERROR mapping %lx to %lx with err{%d}\n",
                            __func__, vaddr_curr, paddr_curr, err);
            }

            if( err != 0 ) ret = err;
        } else {
  	        PSPRINTK("%s: is_vaddr_mapped %d, star:%lx end:%lx\n",
	  	        __func__, val, vma->vm_start, vma->vm_end);
        }

        paddr_curr += PAGE_SIZE;
    }

    PSPRINTK("%s: exiting\n",__func__);

    return ret;
}


/**
 * @brief Map, but only in areas that do not currently have mappings.
 * This should extend vmas that ara adjacent as necessary.
 * NOTE: current->enable_do_mmap_pgoff_hook must be disabled
 *       by client code before calling this.
 * NOTE: mm->mmap_sem must already be held by client code.
 * NOTE: entries in the per-mm list of vm_area_structs are
 *       ordered by starting address.  This is helpful, because
 *       I can exit my check early sometimes.
 */
#define FORCE_NODEBUG
#ifndef FORCE_NODEBUG
#define DBGPSPRINTK(...) { if (dbg ==1) printk(KERN_ALERT __VA_ARGS__); }
#else
#define DBGPSPRINTK(...) ;
#endif
unsigned long do_mmap_remaining(struct file *file, unsigned long addr,
                                unsigned long len, unsigned long prot,
                                unsigned long flags, unsigned long pgoff, int dbg) {
    unsigned long ret = addr;
    unsigned long start = addr;
    unsigned long local_end = start;
    unsigned long end = addr + len;
    struct vm_area_struct* curr;
    unsigned long error;

    // go through ALL vma's, looking for interference with this space.
    curr = current->mm->mmap;
    DBGPSPRINTK("%s: processing {%lx,%lx}\n",__func__,addr,len);

    while(1) {

        if(start >= end) goto done;

        // We've reached the end of the list
        else if(curr == NULL) {
            // map through the end
            DBGPSPRINTK("%s: curr == NULL - mapping {%lx,%lx}\n",
                    __func__,start,end-start);
	    error=do_mmap(file, start, end - start, prot, flags, pgoff); 
	    if (error != start)
        	printk(KERN_ALERT"%s_1: ERROR %lx start: %lx end %lx\n", __func__, error, start, end);
            goto done;
        }

        // the VMA is fully above the region of interest
        else if(end <= curr->vm_start) {
                // mmap through local_end
            DBGPSPRINTK("%s: VMA is fully above the region of interest - mapping {%lx,%lx}\n",
                    __func__,start,end-start);
	    error=do_mmap(file, start, end - start, prot, flags, pgoff);
	    if (error != start)
                printk(KERN_ALERT"%s_2: ERROR %lx start: %lx end %lx\n", __func__, error, start, end);
            goto done;
        }

        // the VMA fully encompases the region of interest
        else if(start >= curr->vm_start && end <= curr->vm_end) {
            // nothing to do
            DBGPSPRINTK("%s: VMA fully encompases the region of interest\n",__func__);
            goto done;
        }

        // the VMA is fully below the region of interest
        else if(curr->vm_end <= start) {
            // move on to the next one
            DBGPSPRINTK("%s: VMA is fully below region of interest\n",__func__);
        }

        // the VMA includes the start of the region of interest 
        // but not the end
        else if (start >= curr->vm_start && 
                 start < curr->vm_end &&
                 end > curr->vm_end) {
            // advance start (no mapping to do) 
            start = curr->vm_end;
            local_end = start;
            DBGPSPRINTK("%s: VMA includes start but not end\n",__func__);
        }

        // the VMA includes the end of the region of interest
        // but not the start
        else if(start < curr->vm_start && 
                end <= curr->vm_end &&
                end > curr->vm_start) {
            local_end = curr->vm_start;
            
            // mmap through local_end
            DBGPSPRINTK("%s: VMA includes end but not start - mapping {%lx,%lx}\n",
                    __func__,start, local_end - start);
            error=do_mmap(file, start, local_end - start, prot, flags, pgoff);
            if (error != start)
                printk(KERN_ALERT"%s_3: ERROR %lx start: %lx end %lx\n", __func__, error, start, end);

            // Then we're done
            goto done;
        }

        // the VMA is fully within the region of interest
        else if(start <= curr->vm_start && end >= curr->vm_end) {
            // advance local end
            local_end = curr->vm_start;

            // map the difference
            DBGPSPRINTK("%s: VMS is fully within the region of interest - mapping {%lx,%lx}\n",
                    __func__,start, local_end - start);
            error=do_mmap(file, start, local_end - start, prot, flags, pgoff);
            if (error != start)
                printk(KERN_ALERT"%s_4: ERROR %lx start: %lx end %lx\n", __func__, error, start, end);

            // Then advance to the end of this vma
            start = curr->vm_end;
            local_end = start;
        }

        curr = curr->vm_next;

    }

done:
    
    DBGPSPRINTK("%s: exiting start:%lx\n",__func__, error);
    return ret;
}

static void send_pte(unsigned long paddr_start,
        unsigned long vaddr_start, 
        size_t sz, 
        int dst,
        int vma_id,
        int clone_request_id) {

    pte_transfer_t pte_xfer;
    pte_xfer.header.type = PCN_KMSG_TYPE_PROC_SRV_PTE_TRANSFER;
    pte_xfer.header.prio = PCN_KMSG_PRIO_NORMAL;
    pte_xfer.paddr_start = paddr_start;
    pte_xfer.vaddr_start = vaddr_start;
    pte_xfer.sz = sz;
    pte_xfer.clone_request_id = clone_request_id;
    pte_xfer.vma_id = vma_id;
    pcn_kmsg_send(dst, (struct pcn_kmsg_message *)&pte_xfer);
}

static void send_vma(struct mm_struct* mm,
        struct vm_area_struct* vma, 
        int dst,
        int clone_request_id) {
    char lpath[256];
    char *plpath;
    vma_transfer_t* vma_xfer = kmalloc(sizeof(vma_transfer_t),GFP_KERNEL);
    vma_xfer->header.type = PCN_KMSG_TYPE_PROC_SRV_VMA_TRANSFER;  
    vma_xfer->header.prio = PCN_KMSG_PRIO_NORMAL;
    
    if(vma->vm_file == NULL) {
        vma_xfer->path[0] = '\0';
    } else {
        plpath = d_path(&vma->vm_file->f_path,
                lpath,256);
        strcpy(vma_xfer->path,plpath);
    }

    //
    // Transfer the vma
    //
    PS_SPIN_LOCK(&_vma_id_lock);
    vma_xfer->vma_id = _vma_id++;
    PS_SPIN_UNLOCK(&_vma_id_lock);
    vma_xfer->start = vma->vm_start;
    vma_xfer->end = vma->vm_end;
    vma_xfer->prot = vma->vm_page_prot;
    vma_xfer->clone_request_id = clone_request_id;
    vma_xfer->flags = vma->vm_flags;
    vma_xfer->pgoff = vma->vm_pgoff;
    pcn_kmsg_send_long(dst, 
                        (struct pcn_kmsg_long_message*)vma_xfer, 
                        sizeof(vma_transfer_t) - sizeof(vma_xfer->header));

    // Send all physical information too
    {
    unsigned long curr = vma->vm_start;
    unsigned long vaddr_resolved = -1;
    unsigned long paddr_resolved = -1;
    size_t sz_resolved = 0;
    
    while(curr < vma->vm_end) {
        if(-1 == find_next_consecutive_physically_mapped_region(mm,
                    vma,
                    curr,
                    &vaddr_resolved,
                    &paddr_resolved,
                    &sz_resolved,
                    0)) {
            // None more, exit
            break;
        } else {
            // send the pte
            send_pte(paddr_resolved,
                     vaddr_resolved,
                     sz_resolved,
                     dst,
                     vma_xfer->vma_id,
                     vma_xfer->clone_request_id
                     );

            // move to the next
            curr = vaddr_resolved + sz_resolved;
        }
    }

    }


    kfree(vma_xfer);
}

/**
 * @brief Display a mapping request data entry.
 */
static void dump_mapping_request_data(mapping_request_data_t* data) {
    int i;
    PSPRINTK("mapping request data dump:\n");
    PSPRINTK("address{%lx}, vaddr_start{%lx}, vaddr_sz{%lx}\n",
                    data->address, data->vaddr_start, data->vaddr_size);
    for(i = 0; i < MAX_MAPPINGS; i++) {
        PSPRINTK("mapping %d - vaddr{%lx}, paddr{%lx}, sz{%lx}\n",
                i,data->mappings[i].vaddr,data->mappings[i].paddr,data->mappings[i].sz);
    }
    PSPRINTK("present{%d}, complete{%d}, from_saved_mm{%d}\n",
            data->present, data->complete, data->from_saved_mm);
    PSPRINTK("responses{%d}, expected_responses{%d}\n",
            data->responses, data->expected_responses);
}

/**
 * @brief Display relevant task information.
 */
void dump_task(struct task_struct* task, struct pt_regs* regs, unsigned long stack_ptr) {
#if PROCESS_SERVER_VERBOSE
    if (!task) return;

    PSPRINTK("DUMP TASK\n");
    PSPRINTK("PID: %d\n",task->pid);
    PSPRINTK("State: %lx\n",task->state);
    PSPRINTK("Flags: %x\n",task->flags);
    PSPRINTK("Prio{%d},Static_Prio{%d},Normal_Prio{%d}\n",
            task->prio,task->static_prio,task->normal_prio);
    PSPRINTK("Represents_remote{%d}\n",task->represents_remote);
    PSPRINTK("Executing_for_remote{%d}\n",task->executing_for_remote);
    PSPRINTK("prev_pid{%d}\n",task->prev_pid);
    PSPRINTK("next_pid{%d}\n",task->next_pid);
    PSPRINTK("prev_cpu{%d}\n",task->prev_cpu);
    PSPRINTK("next_cpu{%d}\n",task->next_cpu);
    PSPRINTK("Clone_request_id{%d}\n",task->clone_request_id);
    dump_regs(regs);
    dump_thread(&task->thread);
    //dump_mm(task->mm);
    dump_stk(&task->thread,stack_ptr);
    PSPRINTK("TASK DUMP COMPLETE\n");
#endif
}

/**
 * @brief Display a task's stack information.
 */
static void dump_stk(struct thread_struct* thread, unsigned long stack_ptr) {
    if(!thread) return;
    PSPRINTK("DUMP STACK\n");
    if(thread->sp) {
        PSPRINTK("sp = %lx\n",thread->sp);
    }
    if(thread->usersp) {
        PSPRINTK("usersp = %lx\n",thread->usersp);
    }
    if(stack_ptr) {
        PSPRINTK("stack_ptr = %lx\n",stack_ptr);
    }
    PSPRINTK("STACK DUMP COMPLETE\n");
}

/**
 * @brief Display a tasks register contents.
 */
static void dump_regs(struct pt_regs* regs) {
    unsigned long fs, gs;
    PSPRINTK("DUMP REGS\n");
    if(NULL != regs) {
        PSPRINTK("r15{%lx}\n",regs->r15);   
        PSPRINTK("r14{%lx}\n",regs->r14);
        PSPRINTK("r13{%lx}\n",regs->r13);
        PSPRINTK("r12{%lx}\n",regs->r12);
        PSPRINTK("r11{%lx}\n",regs->r11);
        PSPRINTK("r10{%lx}\n",regs->r10);
        PSPRINTK("r9{%lx}\n",regs->r9);
        PSPRINTK("r8{%lx}\n",regs->r8);
        PSPRINTK("bp{%lx}\n",regs->bp);
        PSPRINTK("bx{%lx}\n",regs->bx);
        PSPRINTK("ax{%lx}\n",regs->ax);
        PSPRINTK("cx{%lx}\n",regs->cx);
        PSPRINTK("dx{%lx}\n",regs->dx);
        PSPRINTK("di{%lx}\n",regs->di);
        PSPRINTK("orig_ax{%lx}\n",regs->orig_ax);
        PSPRINTK("ip{%lx}\n",regs->ip);
        PSPRINTK("cs{%lx}\n",regs->cs);
        PSPRINTK("flags{%lx}\n",regs->flags);
        PSPRINTK("sp{%lx}\n",regs->sp);
        PSPRINTK("ss{%lx}\n",regs->ss);
    }
    rdmsrl(MSR_FS_BASE, fs);
    rdmsrl(MSR_GS_BASE, gs);
    PSPRINTK("fs{%lx}\n",fs);
    PSPRINTK("gs{%lx}\n",gs);
    PSPRINTK("REGS DUMP COMPLETE\n");
}

/**
 * @brief Display a tasks thread information.
 */
static void dump_thread(struct thread_struct* thread) {
    PSPRINTK("DUMP THREAD\n");
    PSPRINTK("sp0{%lx}, sp{%lx}\n",thread->sp0,thread->sp);
    PSPRINTK("usersp{%lx}\n",thread->usersp);
    PSPRINTK("es{%x}\n",thread->es);
    PSPRINTK("ds{%x}\n",thread->ds);
    PSPRINTK("fsindex{%x}\n",thread->fsindex);
    PSPRINTK("gsindex{%x}\n",thread->gsindex);
    PSPRINTK("gs{%lx}\n",thread->gs);
    PSPRINTK("THREAD DUMP COMPLETE\n");
}

/**
 * @brief Display a pte_data_t data structure.
 */
static void dump_pte_data(pte_data_t* p) {
    PSPRINTK("PTE_DATA\n");
    PSPRINTK("vma_id{%x}\n",p->vma_id);
    PSPRINTK("clone_request_id{%x}\n",p->clone_request_id);
    PSPRINTK("cpu{%x}\n",p->cpu);
    PSPRINTK("vaddr_start{%lx}\n",p->vaddr_start);
    PSPRINTK("paddr_start{%lx}\n",p->paddr_start);
    PSPRINTK("sz{%d}\n",p->sz);
}

/**
 * @brief Display a vma_data_t data structure.
 */
static void dump_vma_data(vma_data_t* v) {
    pte_data_t* p;
    PSPRINTK("VMA_DATA\n");
    PSPRINTK("start{%lx}\n",v->start);
    PSPRINTK("end{%lx}\n",v->end);
    PSPRINTK("clone_request_id{%x}\n",v->clone_request_id);
    PSPRINTK("cpu{%x}\n",v->cpu);
    PSPRINTK("flags{%lx}\n",v->flags);
    PSPRINTK("vma_id{%x}\n",v->vma_id);
    PSPRINTK("path{%s}\n",v->path);

    p = v->pte_list;
    while(p) {
        dump_pte_data(p);
        p = (pte_data_t*)p->header.next;
    }
}

/**
 * @brief Display a clone_data_t.
 */
static void dump_clone_data(clone_data_t* r) {
    vma_data_t* v;
    PSPRINTK("CLONE REQUEST\n");
    PSPRINTK("clone_request_id{%x}\n",r->clone_request_id);
    PSPRINTK("clone_flags{%lx}\n",r->clone_flags);
    PSPRINTK("stack_start{%lx}\n",r->stack_start);
    PSPRINTK("stack_ptr{%lx}\n",r->stack_ptr);
    PSPRINTK("env_start{%lx}\n",r->env_start);
    PSPRINTK("env_end{%lx}\n",r->env_end);
    PSPRINTK("arg_start{%lx}\n",r->arg_start);
    PSPRINTK("arg_end{%lx}\n",r->arg_end);
    PSPRINTK("heap_start{%lx}\n",r->heap_start);
    PSPRINTK("heap_end{%lx}\n",r->heap_end);
    PSPRINTK("data_start{%lx}\n",r->data_start);
    PSPRINTK("data_end{%lx}\n",r->data_end);
    dump_regs(&r->regs);
    PSPRINTK("placeholder_pid{%x}\n",r->placeholder_pid);
    PSPRINTK("placeholder_tgid{%x}\n",r->placeholder_tgid);
    PSPRINTK("thread_fs{%lx}\n",r->thread_fs);
    PSPRINTK("thread_gs{%lx}\n",r->thread_gs);
    PSPRINTK("thread_sp0{%lx}\n",r->thread_sp0);
    PSPRINTK("thread_sp{%lx}\n",r->thread_sp);
    PSPRINTK("thread_usersp{%lx}\n",r->thread_usersp);

#ifdef FPU_   
    PSPRINTK("task_flags{%x}\n",r->task_flags);
    PSPRINTK("task_fpu_counter{%x}\n",(unsigned int)r->task_fpu_counter);
    PSPRINTK("thread_has_fpu{%x}\n",(unsigned int)r->thread_has_fpu);
#endif
    v = r->vma_list;
    while(v) {
        dump_vma_data(v);
        v = (vma_data_t*)v->header.next;
    }
}

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
/**
 * @brief Finds a stats_query data entry.
 * @return Either a stats entry or NULL if one is not found
 * that satisfies the parameter requirements.
 */
static stats_query_data_t* find_stats_query_data(pid_t pid) {
    data_header_t* curr = NULL;
    stats_query_data_t* query = NULL;
    stats_query_data_t* ret = NULL;
    PS_SPIN_LOCK(&_data_head_lock);
    
    curr = _data_head;
    while(curr) {
        if(curr->data_type == PROCESS_SERVER_STATS_DATA_TYPE) {
            query = (stats_query_data_t*)curr;
            if(query->pid == pid) {
                ret = query;
                break;
            }
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_data_head_lock);

    return ret;
}
#endif

/**
 * Queue lock must already be held.
 */
static void add_fault_entry_to_queue(lamport_barrier_entry_t* entry,
                                     lamport_barrier_queue_t* queue)
{
    lamport_barrier_entry_t* curr = queue->queue;
    lamport_barrier_entry_t* last = NULL;

    entry->header.next = NULL;
    entry->header.prev = NULL;

    // Take care of the "empty" scenario first because it's easy.
    if(!queue->queue) {
        queue->queue = entry;
        return;
    }

    // Next take care of the scenario where we have to replace
    // the first entry
    if(queue->queue->timestamp > entry->timestamp) {
        queue->queue->header.prev = (data_header_t*)entry;
        entry->header.next = (data_header_t*)queue->queue;
        queue->queue = entry;
        return;
    }

    // Now we have to iterate, but we know that we don't
    // have to change the value of queue->queue.
    while(curr) {
        if(curr->timestamp > entry->timestamp) {
            curr->header.prev->next = (data_header_t*)entry;
            entry->header.prev = curr->header.prev;
            curr->header.prev = (data_header_t*)entry;
            entry->header.next = (data_header_t*)curr;
            return;
        }
        last = curr;
        curr = (lamport_barrier_entry_t*)curr->header.next;
    }

    // It must be the last entry then
    if(last) {
        last->header.next = (data_header_t*)entry;
        entry->header.prev = (data_header_t*)last;
    }

}


/**
 * @brief Find a fault barrier data entry.
 * @return Either a data entry, or NULL if one does 
 * not exist that satisfies the parameter requirements.
 * If is_heavy, address is ignored.
 */
static lamport_barrier_queue_t* find_lamport_barrier_queue(int tgroup_home_cpu, 
        int tgroup_home_id, 
        unsigned long address,
        int is_heavy) {

    data_header_t* curr = NULL;
    lamport_barrier_queue_t* entry = NULL;
    lamport_barrier_queue_t* ret = NULL;

    curr = (data_header_t*)_lamport_barrier_queue_head;
    while(curr) {
        entry = (lamport_barrier_queue_t*)curr;
        if(entry->tgroup_home_cpu == tgroup_home_cpu &&
           entry->tgroup_home_id == tgroup_home_id) {
           
            if(is_heavy && entry->is_heavy) {
                ret = entry;
                break;
            } else if(!is_heavy && entry->address == address) {
                ret = entry;
                break;
            }
        }
        curr = curr->next;
    }

    return ret;
}

/**
 * @brief Find a thread count data entry.
 * @return Either a thread count request data entry, or NULL if one does 
 * not exist that satisfies the parameter requirements.
 */
static remote_thread_count_request_data_t* find_remote_thread_count_data(int cpu, 
        int id, int requester_pid) {

    data_header_t* curr = NULL;
    remote_thread_count_request_data_t* request = NULL;
    remote_thread_count_request_data_t* ret = NULL;
    unsigned long lockflags;

    spin_lock_irqsave(&_count_remote_tmembers_data_head_lock,lockflags);

    curr = _count_remote_tmembers_data_head;
    while(curr) {
        request = (remote_thread_count_request_data_t*)curr;
        if(request->tgroup_home_cpu == cpu &&
           request->tgroup_home_id == id &&
           request->requester_pid == requester_pid) {
            ret = request;
            break;
        }
        curr = curr->next;
    }

    spin_unlock_irqrestore(&_count_remote_tmembers_data_head_lock,lockflags);

    return ret;
}

/**
 * @brief Finds a munmap request data entry.
 * @return Either a munmap request data entry, or NULL if one is not
 * found that satisfies the parameter requirements.
 */
static munmap_request_data_t* find_munmap_request_data(int cpu, int id, 
        int requester_pid, unsigned long address) {

    data_header_t* curr = NULL;
    munmap_request_data_t* request = NULL;
    munmap_request_data_t* ret = NULL;
    PS_SPIN_LOCK(&_munmap_data_head_lock);
    
    curr = _munmap_data_head;
    while(curr) {
        request = (munmap_request_data_t*)curr;
        if(request->tgroup_home_cpu == cpu && 
                request->tgroup_home_id == id &&
                request->requester_pid == requester_pid &&
                request->vaddr_start == address) {
            ret = request;
            break;
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_munmap_data_head_lock);

    return ret;

}

/**
 * @brief Finds an mprotect request data entry.
 * @return Either a mprotect request data entry, or NULL if one is
 * not found that satisfies the parameter requirements.
 */
static mprotect_data_t* find_mprotect_request_data(int cpu, int id, 
        int requester_pid, unsigned long start) {

    data_header_t* curr = NULL;
    mprotect_data_t* request = NULL;
    mprotect_data_t* ret = NULL;
    PS_SPIN_LOCK(&_mprotect_data_head_lock);
    
    curr = _mprotect_data_head;
    while(curr) {
        request = (mprotect_data_t*)curr;
        if(request->tgroup_home_cpu == cpu && 
                request->tgroup_home_id == id &&
                request->requester_pid == requester_pid &&
                request->start == start) {
            ret = request;
            break;
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_mprotect_data_head_lock);

    return ret;

}

/**
 * @brief Finds a mapping request data entry.
 * @return Either a mapping request data entry, or NULL if an entry
 * is not found that satisfies the parameter requirements.
 */
static mapping_request_data_t* find_mapping_request_data(int cpu, int id, 
        int pid, unsigned long address) {

    data_header_t* curr = NULL;
    mapping_request_data_t* request = NULL;
    mapping_request_data_t* ret = NULL;
    
    curr = _mapping_request_data_head;
    while(curr) {
        request = (mapping_request_data_t*)curr;
        if(request->tgroup_home_cpu == cpu && 
                request->tgroup_home_id == id &&
                request->requester_pid == pid &&
                request->address == address) {
            ret = request;
            break;
        }
        curr = curr->next;
    }


    return ret;
}

/**
 * @brief Finds a clone data entry.
 * @return Either a clone entry or NULL if one is not found
 * that satisfies the parameter requirements.
 */
static clone_data_t* find_clone_data(int cpu, int clone_request_id) {
    data_header_t* curr = NULL;
    clone_data_t* clone = NULL;
    clone_data_t* ret = NULL;
    PS_SPIN_LOCK(&_data_head_lock);
    
    curr = _data_head;
    while(curr) {
        if(curr->data_type == PROCESS_SERVER_CLONE_DATA_TYPE) {
            clone = (clone_data_t*)curr;
            if(clone->placeholder_cpu == cpu && clone->clone_request_id == clone_request_id) {
                ret = clone;
                break;
            }
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_data_head_lock);

    return ret;
}

/**
 * @brief Destroys the specified clone data.  It also destroys lists
 * that are nested within it.
 */
static void destroy_clone_data(clone_data_t* data) {
    vma_data_t* vma_data;
    pte_data_t* pte_data;
    vma_data = data->vma_list;
    while(vma_data) {
        
        // Destroy this VMA's PTE's
        pte_data = vma_data->pte_list;
        while(pte_data) {

            // Remove pte from list
            vma_data->pte_list = (pte_data_t*)pte_data->header.next;
            if(vma_data->pte_list) {
                vma_data->pte_list->header.prev = NULL;
            }

            // Destroy pte
            kfree(pte_data);

            // Next is the new list head
            pte_data = vma_data->pte_list;
        }
        
        // Remove vma from list
        data->vma_list = (vma_data_t*)vma_data->header.next;
        if(data->vma_list) {
            data->vma_list->header.prev = NULL;
        }

        // Destroy vma
        kfree(vma_data);

        // Next is the new list head
        vma_data = data->vma_list;
    }

    // Destroy clone data
    kfree(data);
}

/**
 * @brief Finds a vma_data_t entry.
 */
static vma_data_t* find_vma_data(clone_data_t* clone_data, unsigned long addr_start) {

    vma_data_t* curr = clone_data->vma_list;
    vma_data_t* ret = NULL;

    while(curr) {
        
        if(curr->start == addr_start) {
            ret = curr;
            break;
        }

        curr = (vma_data_t*)curr->header.next;
    }

    return ret;
}

/**
 * @brief Callback for page walk that displays the contents of the walk.
 */
static int dump_page_walk_pte_entry_callback(pte_t *pte, unsigned long start, 
        unsigned long end, struct mm_walk *walk) {

    int nx;
    int rw;
    int user;
    int pwt;
    int pcd;
    int accessed;
    int dirty;

    if(NULL == pte || !pte_present(*pte)) {                                                                                                                             
        return 0;
    }

    nx       = pte_flags(*pte) & _PAGE_NX       ? 1 : 0;
    rw       = pte_flags(*pte) & _PAGE_RW       ? 1 : 0;
    user     = pte_flags(*pte) & _PAGE_USER     ? 1 : 0;
    pwt      = pte_flags(*pte) & _PAGE_PWT      ? 1 : 0;
    pcd      = pte_flags(*pte) & _PAGE_PCD      ? 1 : 0;
    accessed = pte_flags(*pte) & _PAGE_ACCESSED ? 1 : 0;
    dirty    = pte_flags(*pte) & _PAGE_DIRTY    ? 1 : 0;

    PSPRINTK("pte_entry start{%lx}, end{%lx}, phy{%lx}\n",
            start,
            end,
            (unsigned long)(pte_val(*pte) & PHYSICAL_PAGE_MASK) | (start & (PAGE_SIZE-1)));

    PSPRINTK("\tnx{%d}, ",nx);
    PSPRINTK("rw{%d}, ",rw);
    PSPRINTK("user{%d}, ",user);
    PSPRINTK("pwt{%d}, ",pwt);
    PSPRINTK("pcd{%d}, ",pcd);
    PSPRINTK("accessed{%d}, ",accessed);
    PSPRINTK("dirty{%d}\n",dirty);

    return 0;
}

/**
 * @brief Displays relevant data within a mm.
 */
static void dump_mm(struct mm_struct* mm) {
    struct vm_area_struct * curr;
    struct mm_walk walk = {
        .pte_entry = dump_page_walk_pte_entry_callback,
        .mm = mm,
        .private = NULL
        };
    char buf[256];

    if(NULL == mm) {
        PSPRINTK("MM IS NULL!\n");
        return;
    }

    PS_DOWN_READ(&mm->mmap_sem);

    curr = mm->mmap;

    PSPRINTK("MM DUMP\n");
    PSPRINTK("Stack Growth{%lx}\n",mm->stack_vm);
    PSPRINTK("Code{%lx - %lx}\n",mm->start_code,mm->end_code);
    PSPRINTK("Brk{%lx - %lx}\n",mm->start_brk,mm->brk);
    PSPRINTK("Stack{%lx}\n",mm->start_stack);
    PSPRINTK("Arg{%lx - %lx}\n",mm->arg_start,mm->arg_end);
    PSPRINTK("Env{%lx - %lx}\n",mm->env_start,mm->env_end);

    while(curr) {
        if(!curr->vm_file) {
            PSPRINTK("Anonymous VM Entry: start{%lx}, end{%lx}, pgoff{%lx}, flags{%lx}\n",
                    curr->vm_start, 
                    curr->vm_end,
                    curr->vm_pgoff,
                    curr->vm_flags);
            // walk    
            walk_page_range(curr->vm_start,curr->vm_end,&walk);
        } else {
            PSPRINTK("Page VM Entry: start{%lx}, end{%lx}, pgoff{%lx}, path{%s}, flags{%lx}\n",
                    curr->vm_start,
                    curr->vm_end,
                    curr->vm_pgoff,
                    d_path(&curr->vm_file->f_path,buf, 256),
                    curr->vm_flags);
            walk_page_range(curr->vm_start,curr->vm_end,&walk);
        }
        curr = curr->vm_next;
    }

    PS_UP_READ(&mm->mmap_sem);
}

/**
 * Data library
 */

/**
 * @brief Add data entry.
 */
static void add_data_entry_to(void* entry, spinlock_t* lock, data_header_t** head) {
    data_header_t* hdr = (data_header_t*)entry;
    data_header_t* curr = NULL;

    if(!entry) {
        return;
    }

    // Always clear out the link information
    hdr->next = NULL;
    hdr->prev = NULL;

    if(lock)PS_SPIN_LOCK(lock);
    
    if (!*head) {
        *head = hdr;
        hdr->next = NULL;
        hdr->prev = NULL;
    } else {
        curr = *head;
        while(curr->next != NULL) {
            if(curr == entry) {
                return;// It's already in the list!
            }
            curr = curr->next;
        }
        // Now curr should be the last entry.
        // Append the new entry to curr.
        curr->next = hdr;
        hdr->next = NULL;
        hdr->prev = curr;
    }

    if(lock)PS_SPIN_UNLOCK(lock);
}

/**
 * @brief Remove a data entry
 * @prerequisite Requires user to hold lock
 */
static void remove_data_entry_from(void* entry, data_header_t** head) {
    data_header_t* hdr = entry;

    if(!entry) {
        return;
    }

    if(*head == hdr) {
        *head = hdr->next;
    }

    if(hdr->next) {
        hdr->next->prev = hdr->prev;
    }

    if(hdr->prev) {
        hdr->prev->next = hdr->next;
    }

    hdr->prev = NULL;
    hdr->next = NULL;

}

/**
 * @brief Add data entry
 */
static void add_data_entry(void* entry) {
    data_header_t* hdr = (data_header_t*)entry;
    data_header_t* curr = NULL;
    unsigned long lockflags;

    if(!entry) {
        return;
    }

    // Always clear out the link information
    hdr->next = NULL;
    hdr->prev = NULL;

    spin_lock_irqsave(&_data_head_lock,lockflags);
    
    if (!_data_head) {
        _data_head = hdr;
        hdr->next = NULL;
        hdr->prev = NULL;
    } else {
        curr = _data_head;
        while(curr->next != NULL) {
            if(curr == entry) {
                return;// It's already in the list!
            }
            curr = curr->next;
        }
        // Now curr should be the last entry.
        // Append the new entry to curr.
        curr->next = hdr;
        hdr->next = NULL;
        hdr->prev = curr;
    }

    spin_unlock_irqrestore(&_data_head_lock,lockflags);
}

/**
 * @brief Remove a data entry.
 * @prerequisite Requires user to hold _data_head_lock.
 */
static void remove_data_entry(void* entry) {
    data_header_t* hdr = entry;

    if(!entry) {
        return;
    }

    if(_data_head == hdr) {
        _data_head = hdr->next;
    }

    if(hdr->next) {
        hdr->next->prev = hdr->prev;
    }

    if(hdr->prev) {
        hdr->prev->next = hdr->next;
    }

    hdr->prev = NULL;
    hdr->next = NULL;

}

/**
 *
 */
static void dump_lamport_queue(lamport_barrier_queue_t* queue) {
    lamport_barrier_entry_t* curr = queue->queue;
    int queue_pos = 0;
    PSPRINTK("Queue:\n",__func__);
    PSPRINTK("  tgroup_home_cpu: %d\n",queue->tgroup_home_cpu);
    PSPRINTK("  tgroup_home_id: %d\n",queue->tgroup_home_id);
    PSPRINTK("  Addr: %lx\n",queue->address);
    PSPRINTK("  is_heavy: %d\n",queue->is_heavy);
    PSPRINTK("  active_timestamp: %llx\n",queue->active_timestamp);
    PSPRINTK("  Entries:\n");
    while(curr) {
        PSPRINTK("    Entry, Queue position %d\n",queue_pos++);
        PSPRINTK("\t   timestamp: %llx\n",curr->timestamp);
        PSPRINTK("\t   is_heavy: %d\n",curr->is_heavy);
        PSPRINTK("\t   cpu: %d\n",curr->cpu);
        curr = (lamport_barrier_entry_t*)curr->header.next;
    }
}

static void dump_lamport_queue_alwaysprint(lamport_barrier_queue_t* queue) {
     lamport_barrier_entry_t* curr = queue->queue;
     int queue_pos = 0;
     printk("Queue %p:\n",__func__,queue);
     printk("  tgroup_home_cpu: %d\n",queue->tgroup_home_cpu);
     printk("  tgroup_home_id: %d\n",queue->tgroup_home_id);
     printk("  Addr: %lx\n",queue->address);
     printk("  is_heavy: %d\n",queue->is_heavy);
     printk("  active_timestamp: %llx\n",queue->active_timestamp);
     printk("  Entries:\n");
     while(curr) {
         printk("    Entry, Queue position %d\n",queue_pos++);
         printk("\t   timestamp: %llx\n",curr->timestamp);
         printk("\t   is_heavy: %d\n",curr->is_heavy);
         printk("\t   cpu: %d\n",curr->cpu);
         curr = (lamport_barrier_entry_t*)curr->header.next;
     }
}

static void dump_lamport_queue_alert(lamport_barrier_queue_t* queue) {
     lamport_barrier_entry_t* curr = queue->queue;
     int queue_pos = 0;
     printk(KERN_ALERT"Queue %p:\n",__func__,queue);
     printk(KERN_ALERT"  tgroup_home_cpu: %d\n",queue->tgroup_home_cpu);
     printk(KERN_ALERT"  tgroup_home_id: %d\n",queue->tgroup_home_id);
     printk(KERN_ALERT"  Addr: %lx\n",queue->address);
     printk(KERN_ALERT"  is_heavy: %d\n",queue->is_heavy);
     printk(KERN_ALERT"  active_timestamp: %llx\n",queue->active_timestamp);
     printk(KERN_ALERT"  Entries:\n");
     while(curr) {
         printk(KERN_ALERT"    Entry, Queue position %d\n",queue_pos++);
         printk(KERN_ALERT"\t   timestamp: %llx\n",curr->timestamp);
         printk(KERN_ALERT"\t   is_heavy: %d\n",curr->is_heavy);
         printk(KERN_ALERT"\t   cpu: %d\n",curr->cpu);
         curr = (lamport_barrier_entry_t*)curr->header.next;
     }
}

static void dump_all_lamport_queues() {
    lamport_barrier_queue_t* curr = _lamport_barrier_queue_head;
    while(curr) {
        dump_lamport_queue(curr);
        curr = (lamport_barrier_queue_t*)curr->header.next;
    }
}

static void dump_all_lamport_queues_alert() {
     lamport_barrier_queue_t* curr = _lamport_barrier_queue_head;
     while(curr) {
         dump_lamport_queue_alert(curr);
         curr = (lamport_barrier_queue_t*)curr->header.next;
     }
}

static void dump_all_lamport_queues_alwaysprint() {
     lamport_barrier_queue_t* curr = _lamport_barrier_queue_head;
     while(curr) {
        dump_lamport_queue_alwaysprint(curr);
         curr = (lamport_barrier_queue_t*)curr->header.next;
     }
}

/**
 * @brief Print information about the list.
 */
static void dump_data_list(void) {
    data_header_t* curr = NULL;
    pte_data_t* pte_data = NULL;
    vma_data_t* vma_data = NULL;
    clone_data_t* clone_data = NULL;

    PS_SPIN_LOCK(&_data_head_lock);

    curr = _data_head;

    PSPRINTK("DATA LIST:\n");
    while(curr) {
        switch(curr->data_type) {
        case PROCESS_SERVER_VMA_DATA_TYPE:
            vma_data = (vma_data_t*)curr;
            PSPRINTK("VMA DATA: start{%lx}, end{%lx}, crid{%d}, vmaid{%d}, cpu{%d}, pgoff{%lx}\n",
                    vma_data->start,
                    vma_data->end,
                    vma_data->clone_request_id,
                    vma_data->vma_id, 
                    vma_data->cpu, 
                    vma_data->pgoff);
            break;
        case PROCESS_SERVER_PTE_DATA_TYPE:
            pte_data = (pte_data_t*)curr;
            PSPRINTK("PTE DATA: vaddr_start{%lx}, paddr_start{%lx}, sz{%d}, vmaid{%d}, cpu{%d}\n",
                    pte_data->vaddr_start,
                    pte_data->paddr_start,
                    pte_data->sz,
                    pte_data->vma_id,
                    pte_data->cpu);
            break;
        case PROCESS_SERVER_CLONE_DATA_TYPE:
            clone_data = (clone_data_t*)curr;
            PSPRINTK("CLONE DATA: flags{%lx}, stack_start{%lx}, heap_start{%lx}, heap_end{%lx}, ip{%lx}, crid{%d}\n",
                    clone_data->clone_flags,
                    clone_data->stack_start,
                    clone_data->heap_start,
                    clone_data->heap_end,
                    clone_data->regs.ip,
                    clone_data->clone_request_id);
            break;
        default:
            break;
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_data_head_lock);
}

/**
 * @brief Counts remote thread group members.
 * @return The number of remote thread group members in the
 * specified distributed thread group.
 * <MEASURE perf_count_remote_thread_members>
 */
static int count_remote_thread_members(int exclude_t_home_cpu,
                                       int exclude_t_home_id) {

    int tgroup_home_cpu = current->tgroup_home_cpu;
    int tgroup_home_id  = current->tgroup_home_id;
    remote_thread_count_request_data_t* data;
    remote_thread_count_request_t request;
    int i;
    int s;
    int ret = -1;
    int perf = -1;
    unsigned long lockflags;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif

    perf = PERF_MEASURE_START(&perf_count_remote_thread_members);

    PSPRINTK("%s: entered\n",__func__);

    data = kmalloc(sizeof(remote_thread_count_request_data_t),GFP_KERNEL);
    if(!data) goto exit;

    data->header.data_type = PROCESS_SERVER_THREAD_COUNT_REQUEST_DATA_TYPE;
    data->responses = 0;
    data->expected_responses = 0;
    data->tgroup_home_cpu = tgroup_home_cpu;
    data->tgroup_home_id = tgroup_home_id;
    data->requester_pid = current->pid;
    data->count = 0;
    spin_lock_init(&data->lock);

    add_data_entry_to(data,
                      &_count_remote_tmembers_data_head_lock,
                      &_count_remote_tmembers_data_head);

    request.header.type = PCN_KMSG_TYPE_PROC_SRV_THREAD_COUNT_REQUEST;
    request.header.prio = PCN_KMSG_PRIO_NORMAL;
    request.tgroup_home_cpu = current->tgroup_home_cpu; //TODO why not tgroup_home_cpu?!?!
    request.tgroup_home_id  = current->tgroup_home_id; //TODO why not tgroup_home_id?!?!
    request.requester_pid = data->requester_pid;

#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        // Skip the current cpu
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;
#endif
        // Send the request to this cpu.
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&request));
        if(!s) {
            // A successful send operation, increase the number
            // of expected responses.
            data->expected_responses++;
        }
    }

    PSPRINTK("%s: waiting on %d responses\n",__func__,data->expected_responses);

    // Wait for all cpus to respond.
    while(data->expected_responses != data->responses) {
        schedule();
    }

    // OK, all responses are in, we can proceed.
    ret = data->count;

    PSPRINTK("%s: found a total of %d remote threads in group\n",__func__,
            data->count);

    spin_lock_irqsave(&_count_remote_tmembers_data_head_lock,lockflags);
    remove_data_entry_from(data,
                           &_count_remote_tmembers_data_head);
    spin_unlock_irqrestore(&_count_remote_tmembers_data_head_lock,lockflags);

    kfree(data);

exit:

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_COUNT_REMOTE_THREADS_PROCESSING_TIME,total_time);
#endif

    PERF_MEASURE_STOP(&perf_count_remote_thread_members," ",perf);
    return ret;
}

/**
 * @brief Counts the number of local thread group members for the specified
 * distributed thread group.
 */
static int count_local_thread_members(int tgroup_home_cpu, 
        int tgroup_home_id, int exclude_pid) {

    struct task_struct *task, *g;
    int count = 0;
    PSPRINTK("%s: entered\n",__func__);
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {
        if(task->tgroup_home_id == tgroup_home_id &&
           task->tgroup_home_cpu == tgroup_home_cpu &&
           task->t_home_cpu == _cpu &&
           task->pid != exclude_pid &&
           task->exit_state != EXIT_ZOMBIE &&
           task->exit_state != EXIT_DEAD &&
           !(task->flags & PF_EXITING)) {

                count++;
            
        }
    } while_each_thread(g,task);
    read_unlock(&tasklist_lock);
    PSPRINTK("%s: exited\n",__func__);

    return count;

}

/**
 * @brief Counts the number of local and remote thread group members for the
 * thread group in which the "current" task resides.
 * @return The number of threads.
 */
static int count_thread_members() {
     
    int count = 0;
    PSPRINTK("%s: entered\n",__func__);
    count += count_local_thread_members(current->tgroup_home_cpu, current->tgroup_home_id,current->pid);
    count += count_remote_thread_members(current->tgroup_home_cpu, current->tgroup_home_id);
    PSPRINTK("%s: exited\n",__func__);
    return count;
}


/*
 * @brief Process notification of a thread group closing.
 * This function will wait for any locally executing thread group
 * members to exit.  It will then clean up all local resources
 * dedicated to the thread group that has exited.
 *
 * <MEASURE perf_process_tgroup_closed_item>
 */

void process_tgroup_closed_item(struct work_struct* work) {

    tgroup_closed_work_t* w = (tgroup_closed_work_t*) work;
    data_header_t *curr;
    mm_data_t* mm_data = NULL;
    struct task_struct *g, *task;
    unsigned char tgroup_closed = 0;
    int perf = -1;
    mm_data_t* to_remove = NULL;

    perf = PERF_MEASURE_START(&perf_process_tgroup_closed_item);

    PSPRINTK("%s: entered\n",__func__);
    PSPRINTK("%s: received group exit notification\n",__func__);

    PSPRINTK("%s: waiting for all members of this distributed thread group to finish\n",__func__);
    while(!tgroup_closed) {
        unsigned char pass = 0;
        read_lock(&tasklist_lock);
        do_each_thread(g,task) {
            if(task->tgroup_home_cpu == w->tgroup_home_cpu &&
               task->tgroup_home_id  == w->tgroup_home_id) {
                // there are still living tasks within this distributed thread group
                // wait a bit
                pass = 1;
                goto pass_complete;
            }
        } while_each_thread(g,task);
pass_complete:
        read_unlock(&tasklist_lock);
        if(!pass) {
            tgroup_closed = 1;
        } else {
            PSPRINTK("%s: waiting for tgroup close out\n",__func__);
            schedule();
        }
    }

loop:
    spin_lock(&_saved_mm_head_lock);
    // Remove all saved mm's for this thread group.
    curr = _saved_mm_head;
    while(curr) {
        mm_data = (mm_data_t*)curr;
        if(mm_data->tgroup_home_cpu == w->tgroup_home_cpu &&
           mm_data->tgroup_home_id  == w->tgroup_home_id) {
            remove_data_entry_from(curr,&_saved_mm_head);
            to_remove = mm_data;
            goto found;
        }
        curr = curr->next;
    }
found:
    spin_unlock(&_saved_mm_head_lock);

    if(to_remove != NULL) {
        PSPRINTK("%s: removing a mm from cpu{%d} id{%d}\n",
                __func__,
                w->tgroup_home_cpu,
                w->tgroup_home_id);
        
        BUG_ON(to_remove->mm == NULL);
        mmput(to_remove->mm);
        kfree(to_remove);
        to_remove = NULL;
        goto loop;
    }

    kfree(work);

    PERF_MEASURE_STOP(&perf_process_tgroup_closed_item," ",perf);
}


/**
 * @brief Process a request made by a remote CPU for a mapping.  This function
 * will search for mm's for the specified distributed thread group, and if found,
 * will search that mm for entries that contain the address that was asked for.
 * Prefetch is implemented in this function, so not only will the page that
 * is asked for be communicated, but the entire contiguous range of virtual to
 * physical addresses that the specified address lives in will be communicated.
 * Other contiguous regions may also be communicated if they exist.  This is
 * prefetch.
 *
 * <MEASURED perf_process_mapping_request>
 */
void process_mapping_request(struct work_struct* work) {
    mapping_request_work_t* w = (mapping_request_work_t*) work;
    mapping_response_t* response;
    data_header_t* data_curr = NULL;
    mm_data_t* mm_data = NULL;
    struct task_struct* task = NULL;
    struct task_struct* g;
    struct vm_area_struct* vma = NULL;
    struct mm_struct* mm = NULL;
    unsigned long address = w->address;
    unsigned long resolved = 0;
    struct mm_walk walk = {
        .pte_entry = vm_search_page_walk_pte_entry_callback,
        .private = &(resolved)
    };
    char *plpath = NULL, *lpath = NULL;
    int used_saved_mm = 0, found_vma = 1, found_pte = 1; 
    int i;
    
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long mapping_response_send_time_start = 0;
    unsigned long long mapping_response_send_time_end = 0;
    unsigned long long mapping_request_processing_time_start = native_read_tsc();
    unsigned long long mapping_request_processing_time_end = 0;
#endif
    
    // Perf start
    int perf = PERF_MEASURE_START(&perf_process_mapping_request);

    current->enable_distributed_munmap = 0;
    current->enable_do_mmap_pgoff_hook = 0;

    //PSPRINTK("%s: entered\n",__func__);
    PSPRINTK("received mapping request from {%d} address{%lx}, cpu{%d}, id{%d}\n",
            w->from_cpu,
            w->address,
            w->tgroup_home_cpu,
            w->tgroup_home_id);

    // First, search through existing processes
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {
        if((task->tgroup_home_cpu == w->tgroup_home_cpu) &&
           (task->tgroup_home_id  == w->tgroup_home_id )) {
            //PSPRINTK("mapping request found common thread group here\n");
            mm = task->mm;

            // Take note of the fact that an mm exists on the remote kernel
            set_cpu_has_known_tgroup_mm(task, w->from_cpu);

            goto task_mm_search_exit;
        }
    } while_each_thread(g,task);
task_mm_search_exit:
    read_unlock(&tasklist_lock);

    // Failing the process search, look through saved mm's.
    if(!mm) {
        PS_SPIN_LOCK(&_saved_mm_head_lock);
        data_curr = _saved_mm_head;
        while(data_curr) {

            mm_data = (mm_data_t*)data_curr;
            
            if((mm_data->tgroup_home_cpu == w->tgroup_home_cpu) &&
               (mm_data->tgroup_home_id  == w->tgroup_home_id)) {
                PSPRINTK("%s: Using saved mm to resolve mapping\n",__func__);
                mm = mm_data->mm;
                used_saved_mm = 1;
                break;
            }

            data_curr = data_curr->next;

        } // while

        PS_SPIN_UNLOCK(&_saved_mm_head_lock);
    }
     response = kmalloc(sizeof(mapping_response_t), GFP_ATOMIC); //TODO convert to alloc_cache
    if (!response) {
      printk(KERN_ALERT"can not kmalloc mapping_response_t area from{%d} address{%lx} cpu{%d} id{%d}\n",
	      w->from_cpu, w->address, w->tgroup_home_cpu, w->tgroup_home_id);
      goto err_work;
    }
    lpath = kmalloc(POPCORN_MAX_PATH, GFP_ATOMIC); //TODO convert to alloc_cache
    if (!lpath) {
      printk(KERN_ALERT"can not kmalloc lpath area from{%d} address{%lx} cpu{%d} id{%d}\n",
	      w->from_cpu, w->address, w->tgroup_home_cpu, w->tgroup_home_id);
      goto err_response;
    }
    
    // OK, if mm was found, look up the mapping.
    if(mm) {

        // The purpose of this code block is to determine
        // if we need to use a read or write lock, and safely.  
        // implement whatever lock type we decided we needed.  We
        // prefer to use read locks, since then we can service
        // more than one mapping request at the same time.  However,
        // if we are going to do any cow break operations, we 
        // must lock for write.
        int can_be_cow = 0;
        int first = 1;
changed_can_be_cow:
        if(can_be_cow)
            PS_DOWN_WRITE(&mm->mmap_sem);
        else 
            PS_DOWN_READ(&mm->mmap_sem);
        vma = find_vma_checked(mm, address);
        if(vma && first) {
            first = 0;
            if(is_maybe_cow(vma)) {
                can_be_cow = 1;
                PS_UP_READ(&mm->mmap_sem);
                goto changed_can_be_cow;
            }
        }

        walk.mm = mm;
        walk_page_range(address & PAGE_MASK, 
                (address & PAGE_MASK) + PAGE_SIZE, &walk);

        if(vma && resolved != 0) {
            PSPRINTK("mapping found! %lx for vaddr %lx\n",resolved,
                    address & PAGE_MASK);
            /*
             * Find regions of consecutive physical memory
             * in this vma, including the faulting address
             * if possible.
             */
            {

            // Now grab all the mappings that we can stuff into the response.
         if (0 != fill_physical_mapping_array(mm, vma, address,
                                                &(response->mappings[0]),
						MAX_MAPPINGS,can_be_cow)) {
                // If the fill process fails, clear out all
                // results.  Otherwise, we might trick the
                // receiving cpu into thinking the target
                // mapping was found when it was not.
                for(i = 0; i < MAX_MAPPINGS; i++) {
                    response->mappings[i].present = 0;
                    response->mappings[i].vaddr = 0;
                    response->mappings[i].paddr = 0;
                    response->mappings[i].sz = 0;
                }                    
            }

            if(can_be_cow) {
                downgrade_write(&mm->mmap_sem);
            }

            }

            response->header.type = PCN_KMSG_TYPE_PROC_SRV_MAPPING_RESPONSE;
            response->header.prio = PCN_KMSG_PRIO_NORMAL;
            response->tgroup_home_cpu = w->tgroup_home_cpu;
            response->tgroup_home_id = w->tgroup_home_id;
            response->requester_pid = w->requester_pid;
            response->address = address;
            response->present = 1;
            response->vaddr_start = vma->vm_start;
            response->vaddr_size = vma->vm_end - vma->vm_start;
            response->prot = vma->vm_page_prot;
            response->vm_flags = vma->vm_flags;
            if(vma->vm_file == NULL || !w->need_vma) {
                 response->path[0] = '\0';
            } else {    
                plpath = d_path(&vma->vm_file->f_path,lpath,512);
                strcpy(response->path,plpath);
                response->pgoff = vma->vm_pgoff;
            }

            // We modified this lock to be read-mode above so now
            // we can do a read-unlock instead of a write-unlock
            PS_UP_READ(&mm->mmap_sem);
       
        } else {

            if(can_be_cow)
                PS_UP_WRITE(&mm->mmap_sem);
            else
                PS_UP_READ(&mm->mmap_sem);
            // Zero out mappings
            for(i = 0; i < MAX_MAPPINGS; i++) {
               response->mappings[i].present = 0;
               response->mappings[i].vaddr = 0;
               response->mappings[i].paddr = 0;
               response->mappings[i].sz = 0;
            }
        }
    }

    // Not found, respond accordingly
    if(resolved == 0) {
        found_vma = 0;
        found_pte = 0;
        //PSPRINTK("Mapping not found\n");
        response->header.type = PCN_KMSG_TYPE_PROC_SRV_MAPPING_RESPONSE;
        response->header.prio = PCN_KMSG_PRIO_NORMAL;
        response->tgroup_home_cpu = w->tgroup_home_cpu;
        response->tgroup_home_id = w->tgroup_home_id;
        response->requester_pid = w->requester_pid;
        response->address = address;
        response->present = 0;
        response->vaddr_start = 0;
        response->vaddr_size = 0;
        response->path[0] = '\0';

        // Handle case where vma was present but no pte.
        // Optimization, if no pte, and it is specified not to
        // send the path, we can instead report that the mapping
        // was not found at all.  This will result in sending a 
        // nonpresent_mapping_response_t, which is much smaller
        // than a mapping_response_t.
        if(vma && w->need_vma) {
            //PSPRINTK("But vma present\n");
            found_vma = 1;
            response->present = 1;
            response->vaddr_start = vma->vm_start;
            response->vaddr_size = vma->vm_end - vma->vm_start;
            response->prot = vma->vm_page_prot;
            response->vm_flags = vma->vm_flags;
             if(vma->vm_file == NULL || !w->need_vma) {
               response->path[0] = '\0';
             } else {    
                 plpath = d_path(&vma->vm_file->f_path,lpath,512);
                 strcpy(response->path,plpath);
                 response->pgoff = vma->vm_pgoff;
             }
        }
    }

    // Send response
    if(response->present) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        mapping_response_send_time_start = native_read_tsc();
        response->send_time = mapping_response_send_time_start;
#endif
        DO_UNTIL_SUCCESS(pcn_kmsg_send_long(w->from_cpu,
                            (struct pcn_kmsg_long_message*)(response),
                            sizeof(mapping_response_t) - 
                            sizeof(struct pcn_kmsg_hdr) -   //
                            sizeof(response->path) +         // Chop off the end of the path
                            strlen(response->path) + 1));    // variable to save bandwidth.
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        mapping_response_send_time_end = native_read_tsc();
#endif
    } else {
        // This is an optimization to get rid of the _long send 
        // which is a time sink.
        nonpresent_mapping_response_t nonpresent_response;
        nonpresent_response.header.type = PCN_KMSG_TYPE_PROC_SRV_MAPPING_RESPONSE_NONPRESENT;
        nonpresent_response.header.prio = PCN_KMSG_PRIO_NORMAL;
        nonpresent_response.tgroup_home_cpu = w->tgroup_home_cpu;
        nonpresent_response.tgroup_home_id  = w->tgroup_home_id;
        nonpresent_response.requester_pid = w->requester_pid;
        nonpresent_response.address = w->address;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        mapping_response_send_time_start = native_read_tsc();
        nonpresent_response.send_time = mapping_response_send_time_start;
#endif

        DO_UNTIL_SUCCESS(pcn_kmsg_send(w->from_cpu,(struct pcn_kmsg_message*)(&nonpresent_response)));

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        mapping_response_send_time_end = native_read_tsc();
#endif

    }
    
    kfree(lpath);
err_response:
    kfree(response);
err_work:
    // proc
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_RESPONSE_SEND_TIME,
            mapping_response_send_time_end - mapping_response_send_time_start);
#endif

    kfree(work);

    // Perf stop
    if(used_saved_mm && found_vma && found_pte) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "Saved MM + VMA + PTE",
                perf);
    } else if (used_saved_mm && found_vma && !found_pte) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "Saved MM + VMA + no PTE",
                perf);
    } else if (used_saved_mm && !found_vma) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "Saved MM + no VMA",
                perf);
    } else if (!used_saved_mm && found_vma && found_pte) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "VMA + PTE",
                perf);
    } else if (!used_saved_mm && found_vma && !found_pte) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "VMA + no PTE",
                perf);
    } else if (!used_saved_mm && !found_vma) {
        PERF_MEASURE_STOP(&perf_process_mapping_request,
                "no VMA",
                perf);
    } else {
        PERF_MEASURE_STOP(&perf_process_mapping_request,"ERR",perf);
    }

    current->enable_distributed_munmap = 1;
    current->enable_do_mmap_pgoff_hook = 1;

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    {
    unsigned long long mapping_request_processing_time;
    mapping_request_processing_time_end = native_read_tsc();
    mapping_request_processing_time = mapping_request_processing_time_end - 
                                        mapping_request_processing_time_start;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_REQUEST_PROCESSING_TIME,
            mapping_request_processing_time);
    }
#endif

    return;
}

unsigned long long perf_aa, perf_bb, perf_cc, perf_dd, perf_ee;

/**
 * @brief Process notification that a task has exited.  This function
 * sets the "return disposition" of the task, then wakes the task.
 * In this case, the "return disposition" specifies that the task
 * is exiting.  When the task resumes execution, it consults its
 * return disposition and acts accordingly - and invokes do_exit.
 *
 * <MEASURE perf_process_exit_item>
 */
void process_exit_item(struct work_struct* work) {
    exit_work_t* w = (exit_work_t*) work;
    pid_t pid = w->pid;
    struct task_struct *task = w->task;

    int perf = PERF_MEASURE_START(&perf_process_exit_item);
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif

    if(unlikely(!task)) {
        printk("%s: ERROR - empty task\n",__func__);
        kfree(work);
        PERF_MEASURE_STOP(&perf_process_exit_item,"ERROR",perf);
        return;
    }

    if(unlikely(task->pid != pid)) {
        printk("%s: ERROR - wrong task picked\n",__func__);
        kfree(work);
        PERF_MEASURE_STOP(&perf_process_exit_item,"ERROR",perf);
        return;
    }
    
    PSPRINTK("%s: process to kill %ld\n", __func__, (long)pid);
    PSPRINTK("%s: for_each_process Found task to kill, killing\n", __func__);
    PSPRINTK("%s: killing task - is_last_tgroup_member{%d}\n",
            __func__,
            w->is_last_tgroup_member);

    // Now we're executing locally, so update our records
    //if(task->t_home_cpu == _cpu && task->t_home_id == task->pid)
    //    task->represents_remote = 0;

    // Set the return disposition
    task->return_disposition = RETURN_DISPOSITION_EXIT;

    wake_up_process(task);

    kfree(work);

    PERF_MEASURE_STOP(&perf_process_exit_item," ",perf);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_EXIT_NOTIFICATION_PROCESSING_TIME,total_time);
#endif
}

/**
 * @brief Process a group exit request.  This function
 * issues SIGKILL to all locally executing members of the specified
 * distributed thread group.  Only tasks that are actively
 * executing on this CPU will receive the SIGKILL.  Shadow tasks
 * will not be sent SIGKILL.  Group exit requests are sent to
 * all CPUs, so for shadow tasks, another CPU will issue the
 * SIGKILL.  When that occurs, the normal exit process will be
 * initiated for that task, and eventually, all of its shadow
 * tasks will be killed.
 */
void process_group_exit_item(struct work_struct* work) {
    group_exit_work_t* w = (group_exit_work_t*) work;
    struct task_struct *task = NULL;
    struct task_struct *g;
    unsigned long flags;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif

    //int perf = PERF_MEASURE_START(&perf_process_group_exit_item);
    PSPRINTK("%s: entered\n",__func__);
    PSPRINTK("exit group target id{%d}, cpu{%d}\n",
            w->tgroup_home_id, w->tgroup_home_cpu);

    do_each_thread(g,task) {
        if(task->tgroup_home_id == w->tgroup_home_id &&
           task->tgroup_home_cpu == w->tgroup_home_cpu) {
            
            if (!task->represents_remote) { //similar to zap_other_threads
				exit_robust_list(task);
				task->robust_list = NULL;
				// active, send sigkill
				lock_task_sighand(task, &flags);

				task_clear_jobctl_pending(task, JOBCTL_PENDING_MASK);
				sigaddset(&task->pending.signal, SIGKILL);
				signal_wake_up(task, 1);
				clear_ti_thread_flag(task, _TIF_USER_RETURN_NOTIFY);

				unlock_task_sighand(task, &flags);

			}

            // If it is a shadow task, it will eventually
            // get killed when its corresponding active task
            // is killed.

        }
    } while_each_thread(g,task);
    
    kfree(work);

    PSPRINTK("%s: exiting\n",__func__);
    //PERF_MEASURE_STOP(&perf_process_group_exit_item," ",perf);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_GROUP_EXIT_NOTIFICATION_PROCESSING_TIME,total_time);
#endif

}


/**
 * @brief Process request to unmap a region of memory from a distributed
 * thread group.  Look for local thread group members and carry out the
 * requested action.
 *
 * <MEASURE perf_process_munmap_request>
 */
void process_munmap_request(struct work_struct* work) {
    munmap_request_work_t* w = (munmap_request_work_t*)work;
    munmap_response_t response;
    struct task_struct *task, *g;
    data_header_t *curr = NULL;
    mm_data_t* mm_data = NULL;
    mm_data_t* to_munmap = NULL;
    struct mm_struct* mm_to_munmap = NULL;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif
    int perf = PERF_MEASURE_START(&perf_process_munmap_request);

    PSPRINTK("%s: entered\n",__func__);

    current->enable_distributed_munmap = 0;
    current->enable_do_mmap_pgoff_hook = 0;

    // munmap the specified region in the specified thread group
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {

        // Look for the thread group
        if(task->tgroup_home_cpu == w->tgroup_home_cpu &&
           task->tgroup_home_id  == w->tgroup_home_id &&
           !(task->flags & PF_EXITING)) {

            // Take note of the fact that an mm exists on the remote kernel
            set_cpu_has_known_tgroup_mm(task,w->from_cpu);
	 if (task && task->mm ) {
	    mm_to_munmap =task->mm;
	}
	else
		printk("%s: pirla\n", __func__);

	goto done; // thread grouping - threads all share a common mm.
        }
    } while_each_thread(g,task);
done:
    read_unlock(&tasklist_lock);

      if(mm_to_munmap) {
	 PS_DOWN_WRITE(&mm_to_munmap->mmap_sem);
	 do_munmap(mm_to_munmap, w->vaddr_start, w->vaddr_size);
	 PS_UP_WRITE(&mm_to_munmap->mmap_sem);
	 }
	else{
	printk("%s: unexpected error task %p pid %d comm %s task->mm %p\n", 
        	 __func__, task,task->pid,task->comm, (task ? task->mm : 0) );
	}
    // munmap the specified region in any saved mm's as well.
    // This keeps old mappings saved in the mm of dead thread
    // group members from being resolved accidentally after
    // being munmap()ped, as that would cause security/coherency
    // problems.
    PS_SPIN_LOCK(&_saved_mm_head_lock);
    curr = _saved_mm_head;
    while(curr) {
        mm_data = (mm_data_t*)curr;
        if(mm_data->tgroup_home_cpu == w->tgroup_home_cpu &&
           mm_data->tgroup_home_id  == w->tgroup_home_id) {
           
            to_munmap = mm_data;
            goto found;

        }
        curr = curr->next;
    }
found:
    PS_SPIN_UNLOCK(&_saved_mm_head_lock);

    if (to_munmap && to_munmap->mm) {
        PS_DOWN_WRITE(&to_munmap->mm->mmap_sem);
        do_munmap(to_munmap->mm, w->vaddr_start, w->vaddr_size);
        if (to_munmap && to_munmap->mm)
            PS_UP_WRITE(&to_munmap->mm->mmap_sem);
        else{
            printk(KERN_ALERT"%s: ERROR2: to_munmap %p mm %p\n", __func__, to_munmap, to_munmap?to_munmap->mm:0);
    }}
    else if (to_munmap){ // It is OK for to_munmap to be null, but not to_munmap->mm
        printk(KERN_ALERT"%s: ERROR1: to_munmap %p mm %p\n", __func__, to_munmap, to_munmap?to_munmap->mm:0);
	}
    // Construct response
    response.header.type = PCN_KMSG_TYPE_PROC_SRV_MUNMAP_RESPONSE;
    response.header.prio = PCN_KMSG_PRIO_NORMAL;
    response.tgroup_home_cpu = w->tgroup_home_cpu;
    response.tgroup_home_id = w->tgroup_home_id;
    response.requester_pid = w->requester_pid;
    response.vaddr_start = w->vaddr_start;
    response.vaddr_size = w->vaddr_size;
    
    // Send response
    DO_UNTIL_SUCCESS(pcn_kmsg_send(w->from_cpu,
                        (struct pcn_kmsg_message*)(&response)));

    current->enable_distributed_munmap = 1;
    current->enable_do_mmap_pgoff_hook = 1;
    
    kfree(work);
    
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MUNMAP_REQUEST_PROCESSING_TIME,total_time);
#endif

    PERF_MEASURE_STOP(&perf_process_munmap_request," ",perf);
}

/**
 * @brief Process request to change protection of a region of memory in
 * a distributed thread group.  Look for local thread group members and
 * carry out the requested action.
 *
 * <MEASRURE perf_process_mprotect_item>
 */
void process_mprotect_item(struct work_struct* work) {
    mprotect_response_t response;
    mprotect_work_t* w = (mprotect_work_t*)work;
    int tgroup_home_cpu = w->tgroup_home_cpu;
    int tgroup_home_id  = w->tgroup_home_id;
    unsigned long start = w->start;
    size_t len = w->len;
    unsigned long prot = w->prot;
    struct task_struct* task, *g;
    data_header_t* curr = NULL;
    mm_data_t* mm_data = NULL;
    mm_data_t* to_munmap = NULL;
    struct mm_struct* mm_to_munmap = NULL;

    int perf = PERF_MEASURE_START(&perf_process_mprotect_item);
    

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
#endif
   
    current->enable_distributed_munmap = 0;
    current->enable_do_mmap_pgoff_hook = 0;

    // Find the task
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {

        // Look for the thread group
        if (task->tgroup_home_cpu == tgroup_home_cpu &&
            task->tgroup_home_id  == tgroup_home_id &&
            !(task->flags & PF_EXITING)) {
          
           // Take note of the fact that an mm exists on the remote kernel
            set_cpu_has_known_tgroup_mm(task,w->from_cpu);
            
	     if (task && task->mm ) {
	             mm_to_munmap = task->mm;
	     }
	     else
                printk("%s: pirla\n",__func__);
          // then quit
         goto done;
        }
    } while_each_thread(g,task);
done:
    read_unlock(&tasklist_lock);

      if(mm_to_munmap) {
        do_mprotect(task,mm_to_munmap,start,len,prot,0);
        goto early_exit;
	}


    // munmap the specified region in any saved mm's as well.
    // This keeps old mappings saved in the mm of dead thread
    // group members from being resolved accidentally after
    // being munmap()ped, as that would cause security/coherency
    // problems.
    PS_SPIN_LOCK(&_saved_mm_head_lock);
    curr = _saved_mm_head;
    while(curr) {
        mm_data = (mm_data_t*)curr;
        if(mm_data->tgroup_home_cpu == w->tgroup_home_cpu &&
           mm_data->tgroup_home_id  == w->tgroup_home_id) {
           
            to_munmap = mm_data;
            goto found;

        }
        curr = curr->next;
    }
found:
    PS_SPIN_UNLOCK(&_saved_mm_head_lock);

    if(to_munmap != NULL) {
      do_mprotect(NULL,to_munmap->mm,start,len,prot,0);
    }

early_exit:
    // Construct response
    response.header.type = PCN_KMSG_TYPE_PROC_SRV_MPROTECT_RESPONSE;
    response.header.prio = PCN_KMSG_PRIO_NORMAL;
    response.tgroup_home_cpu = tgroup_home_cpu;
    response.tgroup_home_id = tgroup_home_id;
    response.requester_pid = w->requester_pid;
    response.start = start;
    
    // Send response
    DO_UNTIL_SUCCESS(pcn_kmsg_send(w->from_cpu,
                        (struct pcn_kmsg_message*)(&response)));

    current->enable_distributed_munmap = 0;
    current->enable_do_mmap_pgoff_hook = 0;
    
    kfree(work);

    PERF_MEASURE_STOP(&perf_process_mprotect_item," ",perf);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MPROTECT_REQUEST_PROCESSING_TIME,total_time);
#endif
}

/**
 * @brief Process a request from a remote CPU for the number of
 * thread group members that are executing on this CPU.
 */
void process_remote_thread_count_request(struct work_struct* work) {
    remote_thread_count_request_work_t* w = (remote_thread_count_request_work_t*)work;
    remote_thread_count_response_t response;

    PSPRINTK("%s: entered - cpu{%d}, id{%d}\n",
            __func__,
            w->tgroup_home_cpu,
            w->tgroup_home_id);


    // Finish constructing response
    response.header.type = PCN_KMSG_TYPE_PROC_SRV_THREAD_COUNT_RESPONSE;
    response.header.prio = PCN_KMSG_PRIO_NORMAL;
    response.tgroup_home_cpu = w->tgroup_home_cpu;
    response.tgroup_home_id = w->tgroup_home_id;
    response.requester_pid = w->requester_pid;
    response.count = count_local_thread_members(w->tgroup_home_cpu,w->tgroup_home_id,-1);

    PSPRINTK("%s: responding to thread count request with %d\n",__func__,
            response.count);

    // Send response
    DO_UNTIL_SUCCESS(pcn_kmsg_send(w->from_cpu,
                            (struct pcn_kmsg_message*)(&response)));

    kfree(work);

    return;
}

/**
 * @brief Process a request to migrate a task back to this CPU.
 * This function imports the task state, then sets the "return disposition"
 * of that task to specify that it is returning due to a back-migration.
 * This function then wakes the task back up (changes a shadow task back
 * into an active task).
 *
 * <MEASURE perf_process_back_migration>
 */
void process_back_migration(struct work_struct* work) {
    back_migration_work_t* w = (back_migration_work_t*)work;
    struct task_struct* task, *g;
    unsigned char found = 0;
    int perf = -1;
    struct pt_regs* regs = NULL;

    perf = PERF_MEASURE_START(&perf_process_back_migration);

    PSPRINTK("%s\n",__func__);

    // Find the task
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {
        if(task->tgroup_home_id  == w->tgroup_home_id &&
           task->tgroup_home_cpu == w->tgroup_home_cpu &&
           task->t_home_id       == w->t_home_id &&
           task->t_home_cpu      == w->t_home_cpu) {
            found = 1;
            goto search_exit;
        }
    } while_each_thread(g,task);
search_exit:
    read_unlock(&tasklist_lock);
    if(!found) {
        goto exit;
    }

    regs = task_pt_regs(task);

    // Now, transplant the state into the shadow process
    memcpy(regs, &w->regs, sizeof(struct pt_regs));

    task->previous_cpus = w->previous_cpus;
    task->thread.fs = w->thread_fs;
    task->thread.gs = w->thread_gs;
    task->thread.usersp = w->thread_usersp;
    task->thread.es = w->thread_es;
    task->thread.ds = w->thread_ds;
    task->thread.fsindex = w->thread_fsindex;
    task->thread.gsindex = w->thread_gsindex;


#ifdef FPU_   
      //FPU migration --- server (back migration)
          if (w->task_flags & PF_USED_MATH)
               set_used_math();
           current->fpu_counter = w->task_fpu_counter;
        if (w->task_flags & PF_USED_MATH) {
               if (fpu_alloc(&current->thread.fpu) == -ENOMEM)
                   printk(KERN_ERR "%s: ERROR fpu_alloc returned -ENOMEM, remote fpu not copied.\n", __func__);
               else {
                   struct fpu temp; temp.state = &w->fpu_state;
                   fpu_copy(&current->thread.fpu, &temp);
               }
           }
       printk(KERN_ALERT"%s: task flags %x fpu_counter %x has_fpu %x [%d:%d]\n",
           __func__, current->flags, (int)current->fpu_counter,
           (int)current->thread.has_fpu, (int)__thread_has_fpu(current), (int)fpu_allocated(&current->thread.fpu));
           //FPU migration code --- id the following optional?
           if (tsk_used_math(current) && current->fpu_counter >5) //fpu.preload
               __math_state_restore(current);
       
#endif
    // Update local state
    task->represents_remote = 0;
    task->executing_for_remote = 1;
    task->t_distributed = 1;

    // Set the return disposition
    task->return_disposition = RETURN_DISPOSITION_MIGRATE;

    // Release the task
    wake_up_process(task);
    
exit:
    kfree(work);

    PERF_MEASURE_STOP(&perf_process_back_migration," ",perf);
}

void register_lamport_barrier_request_light(int tgroup_home_cpu, 
                                            int tgroup_home_id,
                                            unsigned long address,
                                            unsigned long long timestamp,
                                            int from_cpu) {
    lamport_barrier_entry_t* entry = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
    lamport_barrier_queue_t* queue = NULL;
    lamport_barrier_queue_t* heavy_queue = NULL;
    entry->timestamp = timestamp;
    entry->responses = 0;
    entry->is_heavy = 0;
    entry->expected_responses = 0;
    entry->allow_responses = 0;
    entry->cpu = from_cpu;

    PSPRINTK("%s: addr{%lx},ts{%llx},cpu{%d}\n",__func__,address,timestamp,from_cpu);
    // Find queue, if it exists
    queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                       tgroup_home_id,
                                       address,
                                       0);

    // If we cannot find one, make one
    if(!queue) {
        PSPRINTK("%s: Queue not found, creating one\n",__func__);
        queue = kmalloc(sizeof(lamport_barrier_queue_t),GFP_ATOMIC);
        queue->tgroup_home_cpu = tgroup_home_cpu;
        queue->tgroup_home_id  = tgroup_home_id;
        queue->address = address;
        queue->queue = NULL;
        queue->is_heavy = 0;
        PSPRINTK("%s: Setting active_timestamp to 0\n",__func__);
        queue->active_timestamp = 0;
        add_data_entry_to(queue,NULL,&_lamport_barrier_queue_head);
    
        // Add all heavy entries to this queue
        heavy_queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                                 tgroup_home_id,
                                                 0,
                                                 1);
        if(heavy_queue) {
            lamport_barrier_entry_t* curr = heavy_queue->queue;
            PSPRINTK("%s: found heavy queue\n",__func__);
            while(curr) {
                lamport_barrier_entry_t* e = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
                PSPRINTK("%s: adding entry from heavy queue to queue(addr{%lx}) ts{%llx}\n",
                        __func__,address,curr->timestamp);
                e->timestamp = curr->timestamp;
                e->responses = 0;
                e->expected_responses = 0;
                e->allow_responses = 0;
                e->is_heavy = 1;
                e->cpu = curr->cpu;
                
                add_fault_entry_to_queue(e,queue);

                if(queue->queue == e) {
                    PSPRINTK("%s: new entry is not at the front of the queue\n",
                            __func__);
                    PSPRINTK("%s: setting active timestamp to %llx\n",
                            __func__,e->timestamp);
                    queue->active_timestamp = e->timestamp;
                }
               

                curr = (lamport_barrier_entry_t*)curr->header.next;
            }
        }
    }

    // Add entry to queue
    add_fault_entry_to_queue(entry,queue);

    PSPRINTK("%s: exiting\n",__func__);
}

void register_lamport_barrier_request_heavy(int tgroup_home_cpu, 
                                            int tgroup_home_id,
                                            unsigned long long timestamp,
                                            int from_cpu) {
    lamport_barrier_queue_t* curr_queue = NULL; 
    data_header_t* curr = NULL;
    
    PSPRINTK("%s: ts{%llx},cpu{%d}\n",__func__,timestamp,from_cpu);

    lamport_barrier_entry_t* entry = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
    lamport_barrier_queue_t* queue = NULL;
    entry->timestamp = timestamp;
    entry->responses = 0;
    entry->is_heavy = 1;
    entry->expected_responses = 0;
    entry->allow_responses = 0;
    entry->cpu = from_cpu;

    // Find queue, if it exists
    queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                       tgroup_home_id,
                                       NULL,
                                       1);

    // If we cannot find one, make one
    if(!queue) {
        PSPRINTK("%s: Adding a heavy queue\n",__func__);
        queue = kmalloc(sizeof(lamport_barrier_queue_t),GFP_ATOMIC);
        queue->tgroup_home_cpu = tgroup_home_cpu;
        queue->tgroup_home_id  = tgroup_home_id;
        queue->address = 0;
        queue->queue = NULL;
        queue->is_heavy = 1;
        PSPRINTK("%s: Setting active_timestamp to 0\n",__func__);
        queue->active_timestamp = 0;
        add_data_entry_to(queue,NULL,&_lamport_barrier_queue_head);
    }


    // Add entry to queue
    add_fault_entry_to_queue(entry,queue);

    // Add heavy entry to all non-heavy queues
    curr = _lamport_barrier_queue_head;
    while(curr) {
        lamport_barrier_queue_t* queue_curr = (lamport_barrier_queue_t*)curr;
        if(queue_curr->tgroup_home_cpu == tgroup_home_cpu &&
           queue_curr->tgroup_home_id  == tgroup_home_id &&
           !queue_curr->is_heavy) {
            PSPRINTK("%s: adding heavy entry to addr{%lx}\n",
                    __func__,queue_curr->address);

            lamport_barrier_entry_t* e = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
            e->timestamp = entry->timestamp;
            e->responses = entry->responses;
            e->expected_responses = entry->expected_responses;
            e->is_heavy = 1;
            e->allow_responses = 0;
            e->cpu = entry->cpu;

            add_fault_entry_to_queue(e,queue_curr);

            if(queue_curr->queue == e) {
                PSPRINTK("%s: new entry is not at the front of the queue\n",
                        __func__);
                PSPRINTK("%s: setting active timestamp to %llx\n",
                        __func__,e->timestamp);

                queue_curr->active_timestamp = e->timestamp;
            }

            PSPRINTK("Modified non-heavy queue-\n");
            dump_lamport_queue(queue_curr);
        }
        curr = curr->next;
    }

    PSPRINTK("HEAVY QUEUE-\n");
    dump_lamport_queue(queue);
    
    PSPRINTK("%s: exiting\n",__func__);

}

/**
 * _lamport_barrier_queue_lock must already be held.
 */
void register_lamport_barrier_request(int tgroup_home_cpu,
                                      int tgroup_home_id,
                                      unsigned long address,
                                      unsigned long long timestamp,
                                      int from_cpu,
                                      int is_heavy) {
    if(is_heavy) {
        register_lamport_barrier_request_heavy(tgroup_home_cpu,
                                               tgroup_home_id,
                                               timestamp,
                                               from_cpu);
    } else {
        register_lamport_barrier_request_light(tgroup_home_cpu,
                                               tgroup_home_id,
                                               address,
                                               timestamp,
                                               from_cpu);
    }
}

/**
 *
 */
void process_lamport_barrier_request(struct work_struct* work) {
    lamport_barrier_request_work_t* w = (lamport_barrier_request_work_t*)work;
    lamport_barrier_response_t* response = NULL;

    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);

    register_lamport_barrier_request(w->tgroup_home_cpu,
                                     w->tgroup_home_id,
                                     w->address,
                                     w->timestamp,
                                     w->from_cpu,
                                     w->is_heavy);

    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    // Reply
    response = kmalloc(sizeof(lamport_barrier_response_t),GFP_KERNEL);
    response->header.type = PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RESPONSE;
    response->header.prio = PCN_KMSG_PRIO_NORMAL;
    response->tgroup_home_cpu = w->tgroup_home_cpu;
    response->tgroup_home_id  = w->tgroup_home_id;
    response->address = w->address;
    response->is_heavy = w->is_heavy;
    response->timestamp = w->timestamp;
    pcn_kmsg_send(w->from_cpu,(struct pcn_kmsg_message*)response);
    kfree(response);
    
    kfree(work);
}

/**
 *
 */
void process_lamport_barrier_request_range(struct work_struct* work) {
    lamport_barrier_request_range_work_t* w = (lamport_barrier_request_range_work_t*)work;
    lamport_barrier_response_range_t* response = NULL;
    int i;

    PSPRINTK("%s: timestamp{%llx},cpu{%d},is_heavy{%d}\n",__func__,
                        w->timestamp,w->from_cpu,w->is_heavy);

    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    if(w->is_heavy) {
        register_lamport_barrier_request(w->tgroup_home_cpu,
                                         w->tgroup_home_id,
                                         0,
                                         w->timestamp,
                                         w->from_cpu,
                                         1);
    } else {
        for(i = 0; i < (w->sz / PAGE_SIZE); i++) {
            register_lamport_barrier_request(w->tgroup_home_cpu,
                                             w->tgroup_home_id,
                                             w->address + (i*PAGE_SIZE),
                                             w->timestamp,
                                             w->from_cpu,
                                             w->is_heavy);
        }
    }
    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    // Reply
    response = kmalloc(sizeof(lamport_barrier_response_range_t),GFP_KERNEL);
    response->header.type = PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RESPONSE_RANGE;
    response->header.prio = PCN_KMSG_PRIO_NORMAL;
    response->tgroup_home_cpu = w->tgroup_home_cpu;
    response->tgroup_home_id  = w->tgroup_home_id;
    response->address = w->address;
    response->is_heavy = w->is_heavy;
    response->sz = w->sz;
    response->timestamp = w->timestamp;
    pcn_kmsg_send(w->from_cpu,(struct pcn_kmsg_message*)response);
    kfree(response);
   
    PSPRINTK("%s: exiting\n",__func__);

    kfree(work);
}
/**
 * 
 */
void register_lamport_barrier_response_light(int tgroup_home_cpu,
                                             int tgroup_home_id,
                                             unsigned long address,
                                             unsigned long long timestamp) {
    lamport_barrier_queue_t* queue = NULL;
    lamport_barrier_entry_t* curr = NULL;

    PSPRINTK("%s\n",__func__);

    queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                       tgroup_home_id,
                                       address,
                                       0);

    BUG_ON(!queue);

    if(queue) {
        curr = queue->queue;
        while(curr) {
            if(curr->cpu == _cpu &&
               curr->timestamp == timestamp) {
                curr->responses++;
                goto accounted_for;
            }
            curr = curr->header.next;
        }
    }
accounted_for:
    PSPRINTK("%s: exiting\n",__func__);
    return;
}

/**
 * 
 */
void register_lamport_barrier_response_heavy(int tgroup_home_cpu,
                                             int tgroup_home_id,
                                             unsigned long long timestamp) {
    lamport_barrier_queue_t* queue = NULL;
    lamport_barrier_entry_t* curr = NULL;

    PSPRINTK("%s\n",__func__);

    queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                       tgroup_home_id,
                                       0,
                                       1);

    //BUG_ON(!queue);
    if(!queue) PSPRINTK("%s: ERROR, no queue found\n",__func__);

    if(queue) {
        curr = queue->queue;
        while(curr) {
            if(curr->cpu == _cpu &&
               curr->timestamp == timestamp) {
                curr->responses++;
                goto accounted_for;
            }
            curr = curr->header.next;
        }
    }
accounted_for:
    PSPRINTK("%s: exiting\n",__func__);
    return;

}

/**
 * _lamport_barrier_queue_lock must already be held.
 */
void register_lamport_barrier_response(int tgroup_home_cpu,
                                       int tgroup_home_id,
                                       unsigned long address,
                                       unsigned long long timestamp,
                                       int is_heavy) {
    if(is_heavy) {
        register_lamport_barrier_response_heavy(tgroup_home_cpu,
                                                tgroup_home_id,
                                                timestamp);
    } else {
        register_lamport_barrier_response_light(tgroup_home_cpu,
                                                tgroup_home_id,
                                                address,
                                                timestamp);
    }
}

/**
 *
 */
void process_lamport_barrier_response(struct work_struct* work) {
    lamport_barrier_response_work_t* w = (lamport_barrier_response_work_t*)work;

    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    register_lamport_barrier_response(w->tgroup_home_cpu,
                                      w->tgroup_home_id,
                                      w->address,
                                      w->timestamp,
                                      w->is_heavy);
    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);


    kfree(work);
}

/**
 *
 */
void process_lamport_barrier_response_range(struct work_struct* work) {
    lamport_barrier_response_range_work_t* w = (lamport_barrier_response_range_work_t*)work;
    int i;

    PSPRINTK("%s: timestamp{%llx},cpu{%d},is_heavy{%d}\n",__func__,
                        w->timestamp,w->from_cpu,w->is_heavy);
    
    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    if(w->is_heavy) {
        register_lamport_barrier_response(w->tgroup_home_cpu,
                                          w->tgroup_home_id,
                                          0,
                                          w->timestamp,
                                          1);
    } else {
        for(i = 0; i < (w->sz / PAGE_SIZE); i++) {
            register_lamport_barrier_response(w->tgroup_home_cpu,
                                              w->tgroup_home_id,
                                              w->address + (i*PAGE_SIZE),
                                              w->timestamp,
                                              w->is_heavy);
        }
    }
    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    PSPRINTK("%s: exiting\n",__func__);
    
    kfree(work);
}

/**
 * 
 */
void register_lamport_barrier_release_light(int tgroup_home_cpu,
                                           int tgroup_home_id,
                                           unsigned long address,
                                           unsigned long long timestamp,
                                           int from_cpu) {
    lamport_barrier_queue_t* queue = NULL;
    lamport_barrier_entry_t* curr = NULL;
    PSPRINTK("%s: addr{%lx},ts{%llx},cpu{%d}\n",__func__,address,timestamp,from_cpu);
    queue = find_lamport_barrier_queue(tgroup_home_cpu,
                                       tgroup_home_id,
                                       address,
                                       0);

    if(queue) {
        PSPRINTK("%s: queue found for %lx\n",__func__,queue->address);
        // find the specific entry
        curr = queue->queue;
        while(curr) {
            if(curr->cpu == from_cpu &&
               curr->timestamp == timestamp) {
                PSPRINTK("%s: entry found, ts{%llx}\n",
                        __func__,curr->timestamp);
                remove_data_entry_from(curr,(data_header_t**)&queue->queue);
                kfree(curr);
                break;
            }
            curr = curr->header.next;
        }
        if(!queue->queue) {
            PSPRINTK("%s: queue empty, removing\n",__func__);
            remove_data_entry_from(queue,&_lamport_barrier_queue_head);
            kfree(queue);
        }
    }
    PSPRINTK("%s: exiting\n",__func__);
}

/**
 *
 */
void register_lamport_barrier_release_heavy(int tgroup_home_cpu,
                                           int tgroup_home_id,
                                           unsigned long long timestamp,
                                           int from_cpu) {
    data_header_t* curr = NULL;
    lamport_barrier_queue_t* queue = NULL;
    curr = (data_header_t*)_lamport_barrier_queue_head;

    PSPRINTK("%s: ts{%llx},cpu{%d}\n",__func__,timestamp,from_cpu);

    while(curr) {
        data_header_t* next_queue = curr->next;


        queue = (lamport_barrier_queue_t*)curr;
        if(queue->tgroup_home_cpu == tgroup_home_cpu &&
           queue->tgroup_home_id  == tgroup_home_id) {
            
            lamport_barrier_entry_t* entry_curr = NULL;

            PSPRINTK("%s: examining queue addr{%lx}\n",__func__,queue->address);

            entry_curr = queue->queue;

            while(entry_curr) {

                lamport_barrier_entry_t* next_entry = entry_curr->header.next;
                if(entry_curr->cpu == from_cpu &&
                   entry_curr->timestamp == timestamp &&
                   entry_curr->is_heavy) {

                    PSPRINTK("%s: removing heavy entry ts{%llx}\n",
                            __func__,entry_curr->timestamp);
                    remove_data_entry_from(entry_curr,(data_header_t**)&queue->queue); 
                    kfree(entry_curr);
                }
                entry_curr = next_entry;

            }

            if(!queue->queue) {
                PSPRINTK("%s: queue is now empty, freeing it\n",__func__);
                remove_data_entry_from(queue,&_lamport_barrier_queue_head);
                kfree(queue);
            }

        }
        curr = next_queue;
    }
    PSPRINTK("%s: exiting\n",__func__);
}

/**
 * _lamport_barrier_queue_lock must already be held.
 */
void register_lamport_barrier_release(int tgroup_home_cpu,
                                      int tgroup_home_id,
                                      unsigned long address,
                                      unsigned long long timestamp,
                                      int from_cpu,
                                      int is_heavy) {
    if(is_heavy) {
        register_lamport_barrier_release_heavy(tgroup_home_cpu,
                                               tgroup_home_id,
                                               timestamp,
                                               from_cpu);
    } else {
        register_lamport_barrier_release_light(tgroup_home_cpu,
                                               tgroup_home_id,
                                               address,
                                               timestamp,
                                               from_cpu);
    }
}

/**
 *
 */
void process_lamport_barrier_release(struct work_struct* work) {
    lamport_barrier_release_work_t* w = (lamport_barrier_release_work_t*)work;

    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    register_lamport_barrier_release(w->tgroup_home_cpu,
                                     w->tgroup_home_id,
                                     w->address,
                                     w->timestamp,
                                     w->from_cpu,
                                     w->is_heavy);
    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    kfree(work);
}

/**
 *
 */
void process_lamport_barrier_release_range(struct work_struct* work) {
    lamport_barrier_release_range_work_t* w = (lamport_barrier_release_range_work_t*)work;
    int i;
    int page_count = w->sz / PAGE_SIZE;

    PSPRINTK("%s: timestamp{%llx},cpu{%d},is_heavy{%d}\n",__func__,
                        w->timestamp,w->from_cpu,w->is_heavy);
    
    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    if(w->is_heavy) {
        register_lamport_barrier_release(w->tgroup_home_cpu,
                                         w->tgroup_home_id,
                                         0,
                                         w->timestamp,
                                         w->from_cpu,
                                         1);
    } else {
        for(i = 0; i < page_count; i++) {
            register_lamport_barrier_release(w->tgroup_home_cpu,
                                             w->tgroup_home_id,
                                             w->address + (i * PAGE_SIZE),
                                             w->timestamp,
                                             w->from_cpu,
                                             w->is_heavy);
        }
    }
    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    PSPRINTK("%s: exiting\n",__func__);

    kfree(work);
}

/**
 * Message handlers
 */

/**
 * @brief Message handler for when a distributed thread exits.
 *
 * <MEASURE perf_handle_thread_group_exit_notification>
 */
static int handle_thread_group_exited_notification(struct pcn_kmsg_message* inc_msg) {
    thread_group_exited_notification_t* msg = (thread_group_exited_notification_t*) inc_msg;
    tgroup_closed_work_t* exit_work = NULL;

    int perf = PERF_MEASURE_START(&perf_handle_thread_group_exit_notification);

    // Spin up bottom half to process this event
    exit_work = kmalloc(sizeof(tgroup_closed_work_t),GFP_ATOMIC);
    if(exit_work) {
        INIT_WORK( (struct work_struct*)exit_work, process_tgroup_closed_item);
        exit_work->tgroup_home_cpu = msg->tgroup_home_cpu;
        exit_work->tgroup_home_id  = msg->tgroup_home_id;
        queue_work(exit_wq, (struct work_struct*)exit_work);
    }

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_thread_group_exit_notification," ",perf);

    return 0;
}

/**
 * @brief Message handler for when a CPU responds to a request
 * made by this CPU to count distributed thread group members.
 *
 * <MEASURE perf_handle_remote_thread_count_response>
 */
static int handle_remote_thread_count_response(struct pcn_kmsg_message* inc_msg) {
    remote_thread_count_response_t* msg = (remote_thread_count_response_t*) inc_msg;
    remote_thread_count_request_data_t* data = NULL;
    unsigned long lockflags;
    
    int perf = PERF_MEASURE_START(&perf_handle_remote_thread_count_response);
    
    data = find_remote_thread_count_data(msg->tgroup_home_cpu,
                                         msg->tgroup_home_id,
                                         msg->requester_pid);

    PSPRINTK("%s: entered - cpu{%d}, id{%d}, count{%d}\n",
            __func__,
            msg->tgroup_home_cpu,
            msg->tgroup_home_id,
            msg->count);

    if(data == NULL) {
        PSPRINTK("unable to find remote thread count data\n");
        goto error_exit;
    }

    // Register this response.
    spin_lock_irqsave(&data->lock,lockflags);
    data->count += msg->count;
    data->responses++;
    spin_unlock_irqrestore(&data->lock,lockflags);

error_exit:
    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_remote_thread_count_response," ",perf);

    return 0;
}

/**
 * @brief Message handler invoked when a request has been received from
 * another CUP to count the number of threads in a distributed thread
 * group.
 *
 * <MEASURE perf_handle_remote_thread_count_request>
 */
static int handle_remote_thread_count_request(struct pcn_kmsg_message* inc_msg) {
    remote_thread_count_request_t* msg = (remote_thread_count_request_t*)inc_msg;
    remote_thread_count_request_work_t* work = NULL;

    int perf = PERF_MEASURE_START(&perf_handle_remote_thread_count_request);
    
    work = kmalloc(sizeof(remote_thread_count_request_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_remote_thread_count_request );
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->requester_pid = msg->requester_pid;
        work->from_cpu = msg->header.from_cpu;
        queue_work(mapping_wq, (struct work_struct*)work);
    }

    PERF_MEASURE_STOP(&perf_handle_remote_thread_count_request," ",perf);

    pcn_kmsg_free_msg(inc_msg);
    
    return 0;
}

/**
 * @brief Message handler invoked when another CPU is acknowledging
 * a request to unmap a memory region.
 *
 * <MEASURE perf_handle_munmap_response>
 */
static int handle_munmap_response(struct pcn_kmsg_message* inc_msg) {
    munmap_response_t* msg = (munmap_response_t*)inc_msg;
    munmap_request_data_t* data = NULL;
    unsigned long lockflags;
    int perf = PERF_MEASURE_START(&perf_handle_munmap_response);
   
    data = find_munmap_request_data(
                                   msg->tgroup_home_cpu,
                                   msg->tgroup_home_id,
                                   msg->requester_pid,
                                   msg->vaddr_start);

    if(data == NULL) {
        PSPRINTK("unable to find munmap data\n");
        goto exit_error;;
    }

    // Register this response.
    spin_lock_irqsave(&data->lock,lockflags);
    data->responses++;
    spin_unlock_irqrestore(&data->lock,lockflags);

exit_error:

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_munmap_response," ",perf);

    return 0;
}

/**
 * @brief Message handler for when another CPU asks to unmap memory
 * regions in a distributed thread group.
 *
 * <MEASURE perf_handle_munmap_request>
 */
static int handle_munmap_request(struct pcn_kmsg_message* inc_msg) {
    munmap_request_t* msg = (munmap_request_t*)inc_msg;
    munmap_request_work_t* work = NULL;
    
    int perf = PERF_MEASURE_START(&perf_handle_munmap_request);

    work = kmalloc(sizeof(munmap_request_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_munmap_request  );
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id =  msg->tgroup_home_id;
        work->requester_pid = msg->requester_pid;
        work->from_cpu = msg->header.from_cpu;
        work->vaddr_start = msg->vaddr_start;
        work->vaddr_size = msg->vaddr_size;
        queue_work(mapping_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_munmap_request," ",perf);

    return 0;
}

/**
 * @brief Message handler for when another CPU responds acknowledging
 * that it has handled a distributed mprotect request.
 *
 * <MEASURE perf_handle_mprotect_response>
 */
static int handle_mprotect_response(struct pcn_kmsg_message* inc_msg) {
    mprotect_response_t* msg = (mprotect_response_t*)inc_msg;
    mprotect_data_t* data;
  
    int perf = PERF_MEASURE_START(&perf_handle_mprotect_response);

    data = find_mprotect_request_data(
                                   msg->tgroup_home_cpu,
                                   msg->tgroup_home_id,
                                   msg->requester_pid,
                                   msg->start);

    if(data == NULL) {
        PSPRINTK("unable to find mprotect data\n");
        pcn_kmsg_free_msg(inc_msg);
        PERF_MEASURE_STOP(&perf_handle_mprotect_response,"ERROR",perf);
        return -1;
    }

    // Register this response.
    PS_SPIN_LOCK(&data->lock);
    data->responses++;
    PS_SPIN_UNLOCK(&data->lock);

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_mprotect_response," ",perf);

    return 0;
}

/**
 * @brief Message handler for when a CPU is requesting that this
 * CPU changes the protection on a given page.
 *
 * <MEASURE perf_handle_mprotect_request>
 */
static int handle_mprotect_request(struct pcn_kmsg_message* inc_msg) {
    mprotect_request_t* msg = (mprotect_request_t*)inc_msg;
    mprotect_work_t* work;
    unsigned long start = msg->start;
    size_t len = msg->len;
    unsigned long prot = msg->prot;
    int tgroup_home_cpu = msg->tgroup_home_cpu;
    int tgroup_home_id = msg->tgroup_home_id;


    int perf = PERF_MEASURE_START(&perf_handle_mprotect_request);

    // Schedule work
    work = kmalloc(sizeof(mprotect_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_mprotect_item  );
        work->tgroup_home_id = tgroup_home_id;
        work->tgroup_home_cpu = tgroup_home_cpu;
        work->requester_pid = msg->requester_pid;
        work->start = start;
        work->len = len;
        work->prot = prot;
        work->from_cpu = msg->header.from_cpu;
        queue_work(mapping_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_mprotect_request," ",perf);

    return 0;
}

/**
 * @brief Message handler for when responses to mapping requests come in.  This
 * handler is invoked when a CPU wants to say that it found no mapping.  A separate
 * handler was created for this response as an optimization that keeps us from
 * having to transport tons of data unnecessarily.
 */
static int handle_nonpresent_mapping_response(struct pcn_kmsg_message* inc_msg) {
    nonpresent_mapping_response_t* msg = (nonpresent_mapping_response_t*)inc_msg;
    mapping_request_data_t* data;
    unsigned long lockflags1,lockflags2;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long received_time = native_read_tsc();
#endif

    //PSPRINTK("%s: entered\n",__func__);

    spin_lock_irqsave(&_mapping_request_data_head_lock,lockflags2);

    data = find_mapping_request_data(
                                     msg->tgroup_home_cpu,
                                     msg->tgroup_home_id,
                                     msg->requester_pid,
                                     msg->address);

    if(data == NULL) {
        //printk("%s: ERROR null mapping request data\n",__func__);
        goto exit;
    }

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_RESPONSE_DELIVERY_TIME,
                        received_time - msg->send_time);
#endif

    PSPRINTK("Nonpresent mapping response received for %lx from %d\n",
            msg->address,
            msg->header.from_cpu);

    spin_lock_irqsave(&data->lock,lockflags1);

 #ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    if (!data->wait_time_concluded && (data->responses+1) == data->expected_responses) {
        data->wait_time_concluded = native_read_tsc();
    }
    mb();
#endif

    data->responses++;
    spin_unlock_irqrestore(&data->lock,lockflags1);
exit:

    spin_unlock_irqrestore(&_mapping_request_data_head_lock,lockflags2);
    
    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

/**
 * @brief Message handler for responses to mapping requests.  This function
 * takes in responses and applies precedence rules to arrive at a single
 * response, though different responses may have been given.
 *
 *  <MEASURE perf_handle_mapping_response>
 */
static int handle_mapping_response(struct pcn_kmsg_message* inc_msg) {
    mapping_response_t* msg = (mapping_response_t*)inc_msg;
    mapping_request_data_t* data = NULL;
    unsigned long lockflags, lockflags2;
    unsigned char data_paddr_present = 0;
    unsigned char response_paddr_present = 0;
    int i = 0;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long received_time = native_read_tsc();
#endif

    PSPRINTK("%s: entered\n",__func__);

    spin_lock_irqsave(&_mapping_request_data_head_lock,lockflags2);
    
    data = find_mapping_request_data(
                                     msg->tgroup_home_cpu,
                                     msg->tgroup_home_id,
                                     msg->requester_pid,
                                     msg->address);


    PSPRINTK("%s: received mapping response: addr{%lx},requester{%d},sender{%d}\n",
             __func__,
             msg->address,
             msg->requester_pid,
             msg->header.from_cpu);

    if(data == NULL) {
        goto out_err;
    }
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_RESPONSE_DELIVERY_TIME,
                        received_time - msg->send_time);
#endif

    spin_lock_irqsave(&data->lock,lockflags);

    PSPRINTK("Before changing data\n");
    dump_mapping_request_data(data);

    // If this data entry is completely filled out,
    // there is no reason to go through any more of
    // this logic.  We do still need to account for
    // the response though, which is done after the
    // out label.
    if(data->complete) {
        PSPRINTK("%s: data already complete, exiting\n",__func__);
        goto out;
    }

    if(!msg->present) {
        PSPRINTK("this is a \"not present\" response\n");
    }

    if(msg->present) {
        PSPRINTK("received positive search result from cpu %d\n",
                msg->header.from_cpu);

        // figure out if the current data has a paddr in it
        for(i = 0; i < MAX_MAPPINGS; i++) {
            if(data->mappings[i].present) {
                PSPRINTK("%s: data paddr present\n",__func__);
                data_paddr_present = 1;
                break;
            }
        } 

        // Sanitize data in mappings in cases where they are not marked as present
        for(i = 0; i < MAX_MAPPINGS; i++) {
            if(!msg->mappings[i].present) {
                PSPRINTK("Had to sanitize mapping entry due to the mapping being marked as non present\n");
                msg->mappings[i].vaddr = 0;
                msg->mappings[i].paddr = 0;
                msg->mappings[i].sz = 0;
            }
        }

        // Santize data in mappings in cases where they exceed the vma boundaries
        for(i = 0; i < MAX_MAPPINGS; i++) {
            unsigned long vaddr_start = msg->vaddr_start;
            unsigned long vaddr_end   = msg->vaddr_start + msg->vaddr_size;
            if(msg->mappings[i].present) {
                if(msg->mappings[i].vaddr < vaddr_start ||
                   msg->mappings[i].vaddr + msg->mappings[i].sz > vaddr_end) {
                    PSPRINTK("Had to sanitize mapping entry due to the mapping extending beyond the vma\n");
                    msg->mappings[i].present = 0;
                    msg->mappings[i].vaddr = 0;
                    msg->mappings[i].paddr = 0;
                    msg->mappings[i].sz = 0;
                }
            }
        }

        // figure out if the response has a paddr in it that is
        // relevant to this specific fault address
        for(i = 0; i < MAX_MAPPINGS; i++) {
            if(msg->mappings[i].present &&
               msg->mappings[i].vaddr <= data->address &&
               msg->mappings[i].vaddr + msg->mappings[i].sz > data->address) {
                PSPRINTK("%s: response paddr present\n",__func__);
                response_paddr_present = 1;
                break;
            } 
        }

        if(!data_paddr_present) {
            PSPRINTK("%s: data paddr not present\n",__func__);
        } 
        if(!response_paddr_present) {
            PSPRINTK("%s: response paddr not present\n",__func__);
        }
        
        // Enforce precedence rules.  Responses from saved mm's
        // are always ignored when a response from a live thread
        // can satisfy the mapping request.  The purpose of this
        // is to ensure that the mapping is not stale, since
        // mmap() operations will not effect saved mm's.  Saved
        // mm's are only useful for cases where a thread mmap()ed
        // some space then died before any other thread was able
        // to acquire the new mapping.
        //
        // A note on a case where multiple cpu's have mappings, but
        // of different sizes.  It is possible that two cpu's have
        // partially overlapping mappings.  This is possible because
        // new mapping's are merged when their regions are contiguous.  
        // This is not a problem here because if part of a mapping is 
        // accessed that is not part of an existig mapping, that new 
        // part will be merged in the resulting mapping request.
        //
        // Also, prefer responses that provide values for paddr.
        if(data->present == 1) {

            // Ensure that we keep physical mappings around.
            if(data_paddr_present && !response_paddr_present) {
                PSPRINTK("%s: prevented mapping resolver from downgrading from mapping with paddr to one without\n",__func__);
                goto out;
            }
        }
       
        data->from_saved_mm = msg->from_saved_mm;
        data->vaddr_start = msg->vaddr_start;
        data->vaddr_size = msg->vaddr_size;
        data->prot = msg->prot;
        data->vm_flags = msg->vm_flags;
        data->present = 1;
        if(response_paddr_present) {
            for(i = 0; i < MAX_MAPPINGS; i++) {
                if(msg->mappings[i].present) {
                    PSPRINTK("%s: Found valid mapping in slot %d\n",__func__,i);
                    data->complete = 1;
                }
                data->mappings[i].vaddr  = msg->mappings[i].vaddr;
                data->mappings[i].paddr  = msg->mappings[i].paddr;
                data->mappings[i].sz     = msg->mappings[i].sz;
                data->mappings[i].present = msg->mappings[i].present;
            }
        }
        strcpy(data->path,msg->path);
        data->pgoff = msg->pgoff;

    } else {
        PSPRINTK("received negative search result\n");
    }



out:

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    if (!data->wait_time_concluded && ((data->responses+1) == data->expected_responses) || data->complete) {
        data->wait_time_concluded = native_read_tsc();
    }
    mb();
#endif
    // Account for this cpu's response.
    data->responses++;

    PSPRINTK("After changing data\n");
    dump_mapping_request_data(data);


    spin_unlock_irqrestore(&data->lock,lockflags);

out_err:

    spin_unlock_irqrestore(&_mapping_request_data_head_lock,lockflags2);

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

/**
 * @brief Message handler for when a remote CPU is asking for a
 * page mapping.
 * <MEASRE perf_handle_mapping_request>
 */
static int handle_mapping_request(struct pcn_kmsg_message* inc_msg) {
    mapping_request_t* msg = (mapping_request_t*)inc_msg;
    mapping_request_work_t* work;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long receive_time = native_read_tsc();
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_REQUEST_DELIVERY_TIME,
                        receive_time - msg->send_time);
#endif

    int perf = PERF_MEASURE_START(&perf_handle_mapping_request);

    work = kmalloc(sizeof(mapping_request_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_mapping_request );
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->address = msg->address;
        work->requester_pid = msg->requester_pid;
        work->need_vma = msg->need_vma;
        work->from_cpu = msg->header.from_cpu;
        queue_work(mapping_wq, (struct work_struct*)work);
    }

    // Clean up incoming message
    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_mapping_request," ",perf);

    return 0;
}

/**
 * @brief Message handler for when pte information arrives.  This message
 * type is only used when on-demand address space migration is disabled.
 *
 * <MEASURE perf_handle_pte_transfer>
 */
static int handle_pte_transfer(struct pcn_kmsg_message* inc_msg) {
    pte_transfer_t* msg = (pte_transfer_t*)inc_msg;
    unsigned int source_cpu = msg->header.from_cpu;
    data_header_t* curr = NULL;
    vma_data_t* vma = NULL;
    pte_data_t* pte_data;
    
    int perf = PERF_MEASURE_START(&perf_handle_pte_transfer);

    pte_data = kmalloc(sizeof(pte_data_t),GFP_ATOMIC);
    
    PSPRINTK("%s: entered\n",__func__);
    if(!pte_data) {
        PSPRINTK("Failed to allocate pte_data_t\n");
        PERF_MEASURE_STOP(&perf_handle_pte_transfer,"kmalloc failure",perf);
        return 0;
    }

    PSPRINTK("pte transfer: src{%d}, vaddr_start{%lx}, paddr_start{%lx}, sz{%d}, vma_id{%d}\n",
            source_cpu,
            msg->vaddr_start, msg->paddr_start, msg->sz,  msg->vma_id);

    pte_data->header.data_type = PROCESS_SERVER_PTE_DATA_TYPE;
    pte_data->header.next = NULL;
    pte_data->header.prev = NULL;

    // Copy data into new data item.
    pte_data->cpu = source_cpu;
    pte_data->vma_id = msg->vma_id;
    pte_data->vaddr_start = msg->vaddr_start;
    pte_data->paddr_start = msg->paddr_start;
    pte_data->sz = msg->sz;
    pte_data->clone_request_id = msg->clone_request_id;

    // Look through data store for matching vma_data_t entries.
    PS_SPIN_LOCK(&_data_head_lock);

    curr = _data_head;
    while(curr) {
        if(curr->data_type == PROCESS_SERVER_VMA_DATA_TYPE) {
            vma = (vma_data_t*)curr;
            if(vma->cpu == pte_data->cpu &&
               vma->vma_id == pte_data->vma_id &&
               vma->clone_request_id == pte_data->clone_request_id) {
                // Add to vma data
                PS_SPIN_LOCK(&vma->lock);
                if(vma->pte_list) {
                    pte_data->header.next = (data_header_t*)vma->pte_list;
                    vma->pte_list->header.prev = (data_header_t*)pte_data;
                    vma->pte_list = pte_data;
                } else {
                    vma->pte_list = pte_data;
                }
                PSPRINTK("PTE added to vma\n");
                PS_SPIN_UNLOCK(&vma->lock);
                break;
            }
        }
        curr = curr->next;
    }

    PS_SPIN_UNLOCK(&_data_head_lock);

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_pte_transfer," ",perf);
    
    return 0;
}

/**
 * @brief Message handler for when a vma is transfered.  This message type is
 * only used when on-demand is not, in other words, when an entire
 * address space is migrated up-front.  This code still exists for
 * legacy reasons and should eventually be removed.
 *
 * <MEASURE perf_handle_vma_transfer>
 */
static int handle_vma_transfer(struct pcn_kmsg_message* inc_msg) {
    vma_transfer_t* msg = (vma_transfer_t*)inc_msg;
    unsigned int source_cpu = msg->header.from_cpu;
    vma_data_t* vma_data;
    
    int perf = PERF_MEASURE_START(&perf_handle_vma_transfer);
    
    vma_data = kmalloc(sizeof(vma_data_t),GFP_ATOMIC);
    
    PSPRINTK("%s: entered\n",__func__);
    PSPRINTK("handle_vma_transfer %d\n",msg->vma_id);
    
    if(!vma_data) {
        PSPRINTK("Failed to allocate vma_data_t\n");
        PERF_MEASURE_STOP(&perf_handle_vma_transfer,"kmalloc failure",perf);
        return 0;
    }

    vma_data->header.data_type = PROCESS_SERVER_VMA_DATA_TYPE;

    // Copy data into new data item.
    vma_data->cpu = source_cpu;
    vma_data->start = msg->start;
    vma_data->end = msg->end;
    vma_data->clone_request_id = msg->clone_request_id;
    vma_data->flags = msg->flags;
    vma_data->prot = msg->prot;
    vma_data->vma_id = msg->vma_id;
    vma_data->pgoff = msg->pgoff;
    vma_data->pte_list = NULL;
    vma_data->lock = __SPIN_LOCK_UNLOCKED(&vma_data->lock);
    strcpy(vma_data->path,msg->path);

    add_data_entry(vma_data); 
   
    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_vma_transfer," ",perf);

    return 0;
}

/**
 * @brief Message handler for when a distributed task exits.
 *
 * <MEASURE perf_handle_exiting_process_notification>
 */
static int handle_exiting_process_notification(struct pcn_kmsg_message* inc_msg) {
    exiting_process_t* msg = (exiting_process_t*)inc_msg;
    struct task_struct *task, *g;
    exit_work_t* exit_work = NULL;

    int perf = PERF_MEASURE_START(&perf_handle_exiting_process_notification);

    PSPRINTK("%s: cpu: %d msg: (pid: %d from_cpu: %d [%d])\n", 
	   __func__, smp_processor_id(), msg->my_pid,  inc_msg->hdr.from_cpu, msg->header.from_cpu);
   
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {
        if(task->t_home_id == msg->t_home_id &&
           task->t_home_cpu == msg->t_home_cpu) {

            PSPRINTK("kmkprocsrv: killing local task pid{%d}\n",task->pid);

            read_unlock(&tasklist_lock);

            // Now we're executing locally, so update our records
            // Should I be doing this here, or in the bottom-half handler?
            task->represents_remote = 0;
            
            exit_work = kmalloc(sizeof(exit_work_t),GFP_ATOMIC);
            if(exit_work) {
                INIT_WORK( (struct work_struct*)exit_work, process_exit_item);
                exit_work->task = task;
                exit_work->pid = task->pid;
                exit_work->t_home_id = task->t_home_id;
                exit_work->t_home_cpu = task->t_home_cpu;
                exit_work->is_last_tgroup_member = msg->is_last_tgroup_member;
                queue_work(exit_wq, (struct work_struct*)exit_work);
            }

          
            goto done; // No need to continue;
        }
    } while_each_thread(g,task);
    read_unlock(&tasklist_lock);
done:

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_exiting_process_notification," ",perf);

    return 0;
}

/**
 * @brief Message handler for when exit_group message is received
 * from a remote CPU.
 */
static int handle_exit_group(struct pcn_kmsg_message* inc_msg) {
    exiting_group_t* msg = (exiting_group_t*)inc_msg;
    group_exit_work_t* work;

    work = kmalloc(sizeof(group_exit_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_group_exit_item);
        work->tgroup_home_id = msg->tgroup_home_id;
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        queue_work(exit_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

/**
 * @brief Handler function for when another processor informs the current cpu
 * of a pid pairing.
 *
 * <MEASURE perf_handle_process_pairing_request>
 */
static int handle_process_pairing_request(struct pcn_kmsg_message* inc_msg) {
    create_process_pairing_t* msg = (create_process_pairing_t*)inc_msg;
    unsigned int source_cpu = msg->header.from_cpu;
    struct task_struct *task, *g;

    int perf = PERF_MEASURE_START(&perf_handle_process_pairing_request);

    PSPRINTK("%s entered\n",__func__);

    if(msg == NULL) {
        PSPRINTK("%s msg == null - ERROR\n",__func__);
        PERF_MEASURE_STOP(&perf_handle_process_pairing_request,"ERROR, msg == null",perf);
        return 0;
    }

    PSPRINTK("%s: remote_pid{%d}, local_pid{%d}, remote_cpu{%d}\n",
            __func__,
            msg->my_pid,
            msg->your_pid,
            source_cpu);
    /*
     * Go through all the processes looking for the one with the right pid.
     * Once that task is found, do the bookkeeping necessary to remember
     * the remote cpu and pid information.
     */
    read_lock(&tasklist_lock);
    do_each_thread(g,task) {

        if(task->pid == msg->your_pid && task->represents_remote ) {
            task->next_cpu = source_cpu;
            task->next_pid = msg->my_pid;
            task->executing_for_remote = 0;
 
            PSPRINTK("kmkprocsrv: Added paring at request remote_pid{%d}, local_pid{%d}, remote_cpu{%d}",
                    task->next_pid,
                    task->pid,
                    task->next_cpu);

            goto done; // No need to continue;
        }
    } while_each_thread(g,task);

done:
    read_unlock(&tasklist_lock);

    pcn_kmsg_free_msg(inc_msg);

    PERF_MEASURE_STOP(&perf_handle_process_pairing_request," ",perf);

    return 0;
}

/**
 * @brief Message handler for migration requests.
 * TODO: refactor "clone_request" to "migraton_request".
 */
static int handle_clone_request(struct pcn_kmsg_message* inc_msg) {
    clone_request_t* request = (clone_request_t*)inc_msg;
    unsigned int source_cpu = request->header.from_cpu;
    clone_data_t* clone_data = NULL;
    data_header_t* curr = NULL;
    data_header_t* next = NULL;
    vma_data_t* vma = NULL;
    unsigned long lockflags;
    printk(KERN_ALERT"%s:called\n",__func__);
    int perf = PERF_MEASURE_START(&perf_handle_clone_request);

    perf_cc = native_read_tsc();

    printk(KERN_ALERT"%s: entered\n",__func__);
    
    /*
     * Remember this request
     */
    clone_data = kmalloc(sizeof(clone_data_t),GFP_ATOMIC);
    
    clone_data->header.data_type = PROCESS_SERVER_CLONE_DATA_TYPE;

    clone_data->clone_request_id = request->clone_request_id;
    clone_data->requesting_cpu = source_cpu;
    clone_data->clone_flags = request->clone_flags;
    clone_data->stack_start = request->stack_start;
    clone_data->stack_ptr = request->stack_ptr;
    clone_data->arg_start = request->arg_start;
    clone_data->arg_end = request->arg_end;
    clone_data->env_start = request->env_start;
    clone_data->env_end = request->env_end;
    clone_data->heap_start = request->heap_start;
    clone_data->heap_end = request->heap_end;
    clone_data->data_start = request->data_start;
    clone_data->data_end = request->data_end;
    memcpy(&clone_data->regs, &request->regs, sizeof(struct pt_regs) );
    memcpy(&clone_data->exe_path, &request->exe_path, sizeof(request->exe_path));
    clone_data->placeholder_pid = request->placeholder_pid;
    clone_data->placeholder_tgid = request->placeholder_tgid;
    clone_data->placeholder_cpu = source_cpu;
    clone_data->thread_fs = request->thread_fs;
    clone_data->thread_gs = request->thread_gs;
    clone_data->thread_sp0 = request->thread_sp0;
    clone_data->thread_sp = request->thread_sp;
    clone_data->thread_usersp = request->thread_usersp;
    clone_data->thread_es = request->thread_es;
    clone_data->thread_ds = request->thread_ds;
    clone_data->thread_fsindex = request->thread_fsindex;
    clone_data->thread_gsindex = request->thread_gsindex;
    //TODO this part of the code requires refactoring, it is ugly and can not be worst. Copy each element of a data structure in another without data transformation (ok in the het. case) is a waste of resources.
#ifdef FPU_   
         clone_data->task_flags = request->task_flags;
         clone_data->task_fpu_counter = request->task_fpu_counter;
         clone_data->thread_has_fpu = request->thread_has_fpu;
         clone_data->fpu_state = request->fpu_state;
     //end FPU code
#endif
    clone_data->def_flags = request->def_flags;
    clone_data->personality = request->personality;
    clone_data->vma_list = NULL;
    clone_data->tgroup_home_cpu = request->tgroup_home_cpu;
    clone_data->tgroup_home_id = request->tgroup_home_id;
    clone_data->t_home_cpu = request->t_home_cpu;
    clone_data->t_home_id = request->t_home_id;
    clone_data->previous_cpus = request->previous_cpus;
    clone_data->prio = request->prio;
    clone_data->static_prio = request->static_prio;
    clone_data->normal_prio = request->normal_prio;
    clone_data->rt_priority = request->rt_priority;
    clone_data->sched_class = request->sched_class;
    clone_data->lock = __SPIN_LOCK_UNLOCKED(&clone_data->lock);

    /*mklinux_akshay*/
    clone_data->origin_pid =request->origin_pid;
    clone_data->remote_blocked = request->remote_blocked ;
    clone_data->remote_real_blocked = request->remote_real_blocked;
    clone_data->remote_saved_sigmask = request->remote_saved_sigmask ;
    //clone_data->remote_pending = request->remote_pending;

    clone_data->sas_ss_sp = request->sas_ss_sp;
    clone_data->sas_ss_size = request->sas_ss_size;
    int cnt=0;
    for(cnt=0;cnt<_NSIG;cnt++)
    	clone_data->action[cnt] = request->action[cnt];

    clone_data->skt_flag = request->skt_flag;
    clone_data->skt_level = request->skt_level;

    clone_data->skt_type = request->skt_type;
    clone_data->skt_state = request->skt_state;
    clone_data->skt_dport = request->skt_dport;
    clone_data->skt_sport = request->skt_sport;
    clone_data->skt_saddr = request->skt_saddr;
    clone_data->skt_daddr = request->skt_daddr;
    clone_data->skt_fd = request->skt_fd;
     /*
     * Pull in vma data
     */
#if COPY_WHOLE_VM_WITH_MIGRATION 
    spin_lock_irqsave(&_data_head_lock,lockflags);

    curr = _data_head;
    while(curr) {
        next = curr->next;

        if(curr->data_type == PROCESS_SERVER_VMA_DATA_TYPE) {
            vma = (vma_data_t*)curr;
            if(vma->clone_request_id == clone_data->clone_request_id &&
               vma->cpu == source_cpu ) {

                // Remove the data entry from the general data store
                remove_data_entry(vma);

                // Place data entry in this clone request's vma list
                PS_SPIN_LOCK(&clone_data->lock);
                if(clone_data->vma_list) {
                    clone_data->vma_list->header.prev = (data_header_t*)vma;
                    vma->header.next = (data_header_t*)clone_data->vma_list;
                } 
                clone_data->vma_list = vma;
                PS_SPIN_UNLOCK(&clone_data->lock);
            }
        }

        curr = next;
    }

    spin_unlock_irqrestore(&_data_head_lock,lockflags);
#endif

perf_dd = native_read_tsc();

    {
#ifdef PROCESS_SERVER_USE_KMOD
    struct subprocess_info* sub_info;
    char* argv[] = {clone_data->exe_path,NULL};
    static char *envp[] = { 
        "HOME=/",
        "TERM=linux",
        "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL
    };
    
    add_data_entry(clone_data);
    
    perf_aa = native_read_tsc();
    sub_info = call_usermodehelper_setup( clone_data->exe_path /*argv[0]*/, 
            argv, envp, 
            GFP_ATOMIC );

    /*
     * This information is passed into kmod in order to
     * act as closure information for when the process
     * is spun up.  Once that occurs, this cpu must
     * notify the requesting cpu of the local pid of the
     * delegate process so that it can maintain its records.
     * That information will be used to maintain the link
     * between the placeholder process on the requesting cpu
     * and the delegate process on the executing cpu.
     */
    sub_info->delegated = 1;
    sub_info->remote_pid = clone_data->placeholder_pid;
    sub_info->remote_cpu = clone_data->requesting_cpu;
    sub_info->clone_request_id = clone_data->clone_request_id;
    memcpy(&sub_info->remote_regs, &clone_data->regs, sizeof(struct pt_regs) );
    
    //dump_regs(&sub_info->remote_regs);

    /*
     * Spin up the new process.
     */
    call_usermodehelper_exec(sub_info, UMH_NO_WAIT);
    perf_bb = native_read_tsc();
#else
    import_task_work_t* work;
    work = kmalloc(sizeof(import_task_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_import_task );
        work->data = clone_data;
        queue_work(clone_wq, (struct work_struct*)work);

    }
#endif
    }

    pcn_kmsg_free_msg(inc_msg);

    perf_ee = native_read_tsc();

    PERF_MEASURE_STOP(&perf_handle_clone_request," ",perf);
    return 0;
}

/**
 * @brief Message handler for back migration message.
 */
static int handle_back_migration(struct pcn_kmsg_message* inc_msg) {
    back_migration_t* msg = (back_migration_t*)inc_msg;
    back_migration_work_t* work;

    work = kmalloc(sizeof(back_migration_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_back_migration);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->t_home_cpu      = msg->t_home_cpu;
        work->t_home_id       = msg->t_home_id;
        work->previous_cpus   = msg->previous_cpus;
        work->thread_fs       = msg->thread_fs;
        work->thread_gs       = msg->thread_gs;
        work->thread_usersp   = msg->thread_usersp;
        work->thread_es       = msg->thread_es;
        work->thread_ds       = msg->thread_ds;
        work->thread_fsindex  = msg->thread_fsindex;
        work->thread_gsindex  = msg->thread_gsindex;
	//TODO this function (part of the code) requires refactoring (switch to memcpy or continue like this if there is data transformation (het. support)
	        //FPU migration
#ifdef FPU_   
	        work->task_flags      = msg->task_flags;
	        work->task_fpu_counter = msg->task_fpu_counter;
	        work->thread_has_fpu  = msg->thread_has_fpu;
	        work->fpu_state       = msg->fpu_state;
	        // end FPU code
#endif        
		memcpy(&work->regs, &msg->regs, sizeof(struct pt_regs));
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_request(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_request_t* msg = (lamport_barrier_request_t*)inc_msg;
    lamport_barrier_request_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_request_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_request);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_response(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_response_t* msg = (lamport_barrier_response_t*)inc_msg;
    lamport_barrier_response_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_response_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_response);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_release(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_release_t* msg = (lamport_barrier_release_t*)inc_msg;
    lamport_barrier_release_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_release_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_release);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_request_range(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_request_range_t* msg = (lamport_barrier_request_range_t*)inc_msg;
    lamport_barrier_request_range_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_request_range_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_request_range);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->sz = msg->sz;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_response_range(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_response_range_t* msg = (lamport_barrier_response_range_t*)inc_msg;
    lamport_barrier_response_range_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_response_range_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_response_range);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->sz = msg->sz;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

static int handle_lamport_barrier_release_range(struct pcn_kmsg_message* inc_msg) {
    lamport_barrier_release_range_t* msg = (lamport_barrier_release_range_t*)inc_msg;
    lamport_barrier_release_range_work_t* work;

    work = kmalloc(sizeof(lamport_barrier_release_range_work_t),GFP_ATOMIC);
    if(work) {
        INIT_WORK( (struct work_struct*)work, process_lamport_barrier_release_range);
        work->tgroup_home_cpu = msg->tgroup_home_cpu;
        work->tgroup_home_id  = msg->tgroup_home_id;
        work->from_cpu = msg->header.from_cpu;
        work->address = msg->address;
        work->is_heavy = msg->is_heavy;
        work->sz = msg->sz;
        work->timestamp = msg->timestamp;
        queue_work(clone_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);

    return 0;
}

/**
 *
 */
static int handle_get_counter_phys_request(struct pcn_kmsg_message* inc_msg) {
    get_counter_phys_response_t resp;
    resp.header.type = PCN_KMSG_TYPE_PROC_SRV_GET_COUNTER_PHYS_RESPONSE;
    resp.header.prio = PCN_KMSG_PRIO_NORMAL;
    resp.resp = virt_to_phys(ts_counter);
    pcn_kmsg_send(inc_msg->hdr.from_cpu,(struct pcn_kmsg_message*)&resp);
    pcn_kmsg_free_msg(inc_msg);
    return 0;
}

/**
 *
 */
static int handle_get_counter_phys_response(struct pcn_kmsg_message* inc_msg) {
    get_counter_phys_response_t* msg = (get_counter_phys_response_t*)inc_msg;

    if(get_counter_phys_data) {
        get_counter_phys_data->resp = msg->resp;
        get_counter_phys_data->response_received = 1;
    }

    pcn_kmsg_free_msg(inc_msg);
    
    return 0;
}

/**
 *
 * Public API
 */



/**
 * @brief This function morphs a newly created task into
 * a migrated task.
 *
 * <MEASURED perf_process_server_import_address_space>
 */
int process_server_import_address_space(unsigned long* ip, 
        unsigned long* sp, 
        struct pt_regs* regs) {
    clone_data_t* clone_data = NULL;
    struct file* f = NULL;
#ifdef PROCESS_SERVER_USE_KMOD
    struct vm_area_struct* vma = NULL;
    int munmap_ret = 0;
#endif
    struct mm_struct* thread_mm = NULL;
    struct task_struct* thread_task = NULL;
    mm_data_t* used_saved_mm = NULL;
    int perf = -1;
#ifndef PROCESS_SERVER_USE_KMOD
    struct cred* new_cred = NULL;
#endif
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    int do_time_measurement = 0;
    unsigned long long start_time = native_read_tsc();
#endif

    perf_a = native_read_tsc();
    
    printk(KERN_ALERT"import address space\n");
    
    // Verify that we're a delegated task // deadlock.
#ifdef PROCESS_SERVER_USE_KMOD
    if (!current->executing_for_remote) {
        PSPRINTK("ERROR - not executing for remote\n");
        return -1;
    }
#endif

    perf = PERF_MEASURE_START(&perf_process_server_import_address_space);

    clone_data = current->clone_data;
    if(!clone_data)
        clone_data = find_clone_data(current->prev_cpu,current->clone_request_id);
    if(!clone_data) {
        PERF_MEASURE_STOP(&perf_process_server_import_address_space,"Clone data missing, early exit",perf);
        return -1;
    }

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    do_time_measurement = 1;
#endif

    perf_b = native_read_tsc();    
    
    // Search for existing thread members to share an mm with.
    // Immediately set tgroup_home_<foo> under lock to keep 
    // from allowing multiple tgroups to be created when there 
    // are multiple tasks being migrated at the same time in
    // the same thread group.
    PS_DOWN_WRITE(&_import_sem);

    thread_mm = find_thread_mm(clone_data->tgroup_home_cpu,
                               clone_data->tgroup_home_id,
                               &used_saved_mm,
                               &thread_task);


    current->prev_cpu = clone_data->placeholder_cpu;
    current->prev_pid = clone_data->placeholder_pid;
    current->tgroup_home_cpu = clone_data->tgroup_home_cpu;
    current->tgroup_home_id = clone_data->tgroup_home_id;
    current->t_home_cpu = clone_data->t_home_cpu;
    current->t_home_id = clone_data->t_home_id;
    current->previous_cpus = clone_data->previous_cpus; // This has already
                                                        // been updated by the
                                                        // sending cpu.
                                                        //
    current->executing_for_remote = 1;
    current->tgroup_distributed = 1;
    current->t_distributed = 1;

#ifndef PROCESS_SERVER_USE_KMOD
    spin_lock_irq(&current->sighand->siglock);
    flush_signal_handlers(current,1);
    spin_unlock_irq(&current->sighand->siglock);

    set_cpus_allowed_ptr(current,cpu_all_mask);

    set_user_nice(current,0);

    new_cred = prepare_kernel_cred(current);
    new_cred->cap_bset = CAP_FULL_SET;
    new_cred->cap_inheritable = CAP_FULL_SET;
    commit_creds(new_cred);
#endif

    PSPRINTK("%s: previous_cpus{%lx}\n",__func__,current->previous_cpus);
    PSPRINTK("%s: t_home_cpu{%d}\n",__func__,current->t_home_cpu);
    PSPRINTK("%s: t_home_id{%d}\n",__func__,current->t_home_id);
  
    if(!thread_mm) {
        
       
#ifdef PROCESS_SERVER_USE_KMOD
        PS_DOWN_WRITE(&current->mm->mmap_sem);

        // Gut existing mappings
        current->enable_distributed_munmap = 0;
        vma = current->mm->mmap;
        while(vma) {
            PSPRINTK("Unmapping vma at %lx\n",vma->vm_start);
            munmap_ret = do_munmap(current->mm, vma->vm_start, vma->vm_end - vma->vm_start);
            vma = current->mm->mmap;
        }
        current->enable_distributed_munmap = 1;

        // Clean out cache and tlb
        flush_tlb_mm(current->mm);
        flush_cache_mm(current->mm);
        PS_UP_WRITE(&current->mm->mmap_sem);
        
        // import exe_file
        f = filp_open(clone_data->exe_path,O_RDONLY | O_LARGEFILE, 0);
        if(!IS_ERR(f)) {
            get_file(f);
            current->mm->exe_file = f;
            filp_close(f,NULL);
        } else {
            printk("%s: Error opening file %s\n",__func__,clone_data->exe_path);
        }
       
#else
        struct mm_struct* mm = mm_alloc();
        if(mm) {
            init_new_context(current,mm);

            // import exe_file
            f = filp_open(clone_data->exe_path,O_RDONLY | O_LARGEFILE , 0);
            if(!IS_ERR(f)) {
                //get_file(f);
                //mm->exe_file = f;
                set_mm_exe_file(mm,f);
                filp_close(f,NULL);
            } else {
                printk("%s: Error opening executable file\n",__func__);
            }
            mm->task_size = TASK_SIZE;
            mm->token_priority = 0;
            mm->last_interval = 0;

            arch_pick_mmap_layout(mm);

            atomic_inc(&mm->mm_users);
            exec_mmap(mm);
        }
#endif

        perf_c = native_read_tsc();    

        // Import address space
#if !(COPY_WHOLE_VM_WITH_MIGRATION)
        {
        struct vm_area_struct* vma_out = NULL;
        // fetch stack
        process_server_pull_remote_mappings(current->mm,
                                           NULL,
                                           clone_data->stack_start,
                                           NULL,
                                           &vma_out,
                                           NULL);

        }
#else // Copying address space with migration
        {
        pte_data_t* pte_curr = NULL;
        vma_data_t* vma_curr = NULL;
        int mmap_flags = 0;
        int vmas_installed = 0;
        int ptes_installed = 0;
        unsigned long err = 0;

        vma_curr = clone_data->vma_list;
        while(vma_curr) {
            PSPRINTK("do_mmap() at %lx\n",vma_curr->start);
            if(vma_curr->path[0] != '\0') {
                mmap_flags = MAP_FIXED|MAP_PRIVATE;
                f = filp_open(vma_curr->path,
                                O_RDONLY | O_LARGEFILE,
                                0);
                if(!IS_ERR(f)) {
                    PS_DOWN_WRITE(&current->mm->mmap_sem);
                    vma_curr->mmapping_in_progress = 1;
                    current->enable_do_mmap_pgoff_hook = 0;
                    err = do_mmap(f, 
                            vma_curr->start, 
                            vma_curr->end - vma_curr->start,
                            PROT_READ|PROT_WRITE|PROT_EXEC, 
                            mmap_flags, 
                            vma_curr->pgoff << PAGE_SHIFT);
                    vmas_installed++;
                    vma_curr->mmapping_in_progress = 0;
                    current->enable_do_mmap_pgoff_hook = 1;
                    PS_UP_WRITE(&current->mm->mmap_sem);
                    filp_close(f,NULL);
                    if(err != vma_curr->start) {
                        PSPRINTK("Fault - do_mmap failed to map %lx with error %lx\n",
                                vma_curr->start,err);
                    }
                } else {
                    printk("%s: error opening file %s\n",__func__,vma_curr->path);
                }
            } else {
                mmap_flags = MAP_UNINITIALIZED|MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE;
                PS_DOWN_WRITE(&current->mm->mmap_sem);
                current->enable_do_mmap_pgoff_hook = 0;
                err = do_mmap(NULL, 
                    vma_curr->start, 
                    vma_curr->end - vma_curr->start,
                    PROT_READ|PROT_WRITE, 
                    mmap_flags, 
                    0);
                current->enable_do_mmap_pgoff_hook = 1;
                vmas_installed++;
                //PSPRINTK("mmap error for %lx = %lx\n",vma_curr->start,err);
                PS_UP_WRITE(&current->mm->mmap_sem);
                if(err != vma_curr->start) {
                    PSPRINTK("Fault - do_mmap failed to map %lx with error %lx\n",
                            vma_curr->start,err);
                }
            }
           
            if(err > 0) {
                // mmap_region succeeded
                PS_DOWN_READ(&current->mm->mmap_sem);
                vma = find_vma_checked(current->mm, vma_curr->start);
                PS_UP_READ(&current->mm->mmap_sem);
                PSPRINTK("vma mmapped, pulling in pte's\n");
                if(vma) {
                    pte_curr = vma_curr->pte_list;
                    if(pte_curr == NULL) {
                        PSPRINTK("vma->pte_curr == null\n");
                    }
                    while(pte_curr) {
                        PS_DOWN_WRITE(&current->mm->mmap_sem);
                        err = remap_pfn_range_remaining(current->mm,
                                                        vma,
                                                        pte_curr->vaddr_start,
                                                        pte_curr->paddr_start,
                                                        pte_curr->sz,
                                                        vma->vm_page_prot,
                                                        1);

                        PS_UP_WRITE(&current->mm->mmap_sem);
                        
                        pte_curr = (pte_data_t*)pte_curr->header.next;
                    }
                }
            }
            vma_curr = (vma_data_t*)vma_curr->header.next;
        }
        }
#endif
    } else {
        struct mm_struct* oldmm = current->mm;
        
        //PS_DOWN_WRITE(&thread_mm->mmap_sem); // deadlock was here... is it still?

        // Flush the tlb and cache, removing any entries for the
        // old memory map.
        if(oldmm && oldmm != thread_mm) {
            PS_DOWN_WRITE(&oldmm->mmap_sem);
            flush_tlb_mm(oldmm);
            flush_cache_mm(oldmm);
            PS_UP_WRITE(&oldmm->mmap_sem);

            // Update the owner for the old memory map
            mm_update_next_owner(oldmm);

            // Exit use of current memory map
            mmput(oldmm);
        }

        // Switch the memory map for this task so we're using
        // the memory map that was found for this distributed
        // thread group
        current->mm = thread_mm;
        current->active_mm = current->mm;
        percpu_write(cpu_tlbstate.active_mm, thread_mm);
        load_cr3(thread_mm->pgd);

        // Do mm accounting
        if(NULL == used_saved_mm) {
            // Did not use a saved MM.  Saved MM's have artificially
            // incremented mm_users fields to keep them from being
            // destroyed when the last tgroup member exits.  So we can
            // just use the current value of mm_users.  Since in this case
            // we are not using a saved mm, we must increment mm_users.
            atomic_inc(&current->mm->mm_users);
        } else {
            // Used a saved MM.  Must delete the saved mm entry.
            // It is safe to do so now, since we have ingested
            // its mm at this point.
            unsigned long lockflags;
            spin_lock_irqsave(&_saved_mm_head_lock,lockflags);
            remove_data_entry_from(used_saved_mm,&_saved_mm_head);
            spin_unlock_irqrestore(&_saved_mm_head_lock,lockflags);
            kfree(used_saved_mm);
        }

        //PS_UP_WRITE(&thread_mm->mmap_sem);

        // Transplant thread group information
        // if there are other thread group members
        // on this cpu.
        if(thread_task) {

            write_lock_irq(&tasklist_lock);
            PS_SPIN_LOCK(&thread_task->sighand->siglock);

            // Copy grouping info
            current->group_leader = thread_task->group_leader;
            current->tgid = thread_task->tgid;
            current->real_parent = thread_task->real_parent;
            current->parent_exec_id = thread_task->parent_exec_id;

            // Unhash sibling 
            list_del_init(&current->sibling);
            INIT_LIST_HEAD(&current->sibling);

             // Remove from tasks list, since this is not group leader.
             // We know that by virtue of the fact that we found another
             // thread group member.
            list_del_rcu(&current->tasks);

            // Signal related stuff
            current->signal = thread_task->signal;     
            atomic_inc(&thread_task->signal->live);
            atomic_inc(&thread_task->signal->sigcnt);
            thread_task->signal->nr_threads++;
            current->exit_signal = -1;
            
            // Sighand related stuff
            current->sighand = thread_task->sighand;   
            atomic_inc(&thread_task->sighand->count);

            // Rehash thread_group
            list_del_rcu(&current->thread_group);
            list_add_tail_rcu(&current->thread_group,
                              &current->group_leader->thread_group);

            // Reduce process count
             __this_cpu_dec(process_counts);

            PS_SPIN_UNLOCK(&thread_task->sighand->siglock);
            write_unlock_irq(&tasklist_lock);
           
            // copy fs
            // TODO: This should probably only happen when CLONE_FS is used...
            task_lock(current);
            PS_SPIN_LOCK(&thread_task->fs->lock);
            current->fs = thread_task->fs;
            current->fs->users++;
            PS_SPIN_UNLOCK(&thread_task->fs->lock);
            task_unlock(current);

            // copy files
            // TODO: This should probably only happen when CLONE_FILES is used...
            task_lock(current);
            current->files = thread_task->files;
            task_unlock(current);
            atomic_inc(&current->files->count);
        }
    }


    perf_d = native_read_tsc();

    // install memory information
    current->mm->start_stack = clone_data->stack_start;
    current->mm->start_brk = clone_data->heap_start;
    current->mm->brk = clone_data->heap_end;
    current->mm->env_start = clone_data->env_start;
    current->mm->env_end = clone_data->env_end;
    current->mm->arg_start = clone_data->arg_start;
    current->mm->arg_end = clone_data->arg_end;
    current->mm->start_data = clone_data->data_start;
    current->mm->end_data = clone_data->data_end;
    current->mm->def_flags = clone_data->def_flags;

    // install thread information
    // TODO: Move to arch
    current->thread.es = clone_data->thread_es;
    current->thread.ds = clone_data->thread_ds;
    current->thread.usersp = clone_data->thread_usersp;
    current->thread.fsindex = clone_data->thread_fsindex;
    current->thread.fs = clone_data->thread_fs;
    current->thread.gs = clone_data->thread_gs;    
    current->thread.gsindex = clone_data->thread_gsindex;
   

    //mklinux_akshay
    current->origin_pid = clone_data->origin_pid;
    sigorsets(&current->blocked,&current->blocked,&clone_data->remote_blocked) ;
    sigorsets(&current->real_blocked,&current->real_blocked,&clone_data->remote_real_blocked);
    sigorsets(&current->saved_sigmask,&current->saved_sigmask,&clone_data->remote_saved_sigmask);
    ///current->pending = clone_data->remote_pending;
    current->sas_ss_sp = clone_data->sas_ss_sp;
    current->sas_ss_size = clone_data->sas_ss_size;

    ///printk(KERN_ALERT "origin pid {%d}-{%d} \n",current->origin_pid,clone_data->origin_pid);

    int cnt=0;
     for(cnt=0;cnt<_NSIG;cnt++)
    	 current->sighand->action[cnt] = clone_data->action[cnt];

    // Set output variables.
    *sp = clone_data->thread_usersp;
    *ip = clone_data->regs.ip;
    
    // adjust registers as necessary
    memcpy(regs,&clone_data->regs,sizeof(struct pt_regs)); 
    regs->ax = 0; // Fake success for the "sched_setaffinity" syscall
                  // that this process just "returned from"

    current->prio = clone_data->prio;
    current->static_prio = clone_data->static_prio;
    current->normal_prio = clone_data->normal_prio;
    current->rt_priority = clone_data->rt_priority;
    current->policy = clone_data->sched_class;
    current->personality = clone_data->personality;

    // We assume that an exec is going on and the current process is the one is executing
    // (a switch will occur if it is not the one that must execute)
    { // FS/GS update --- start
#ifdef PROCESS_SERVER_USE_KMOD
    unsigned long fs, gs;
    unsigned int fsindex, gsindex;
    unsigned short es, ds;
                    
    savesegment(fs, fsindex);
    if ( !(clone_data->thread_fs) || !(__user_addr(clone_data->thread_fs)) ) {
      printk(KERN_ERR "%s: ERROR corrupted fs base address %p\n", __func__, clone_data->thread_fs);
    }    
    if (unlikely(fsindex | current->thread.fsindex))
      loadsegment(fs, current->thread.fsindex);
    else
      loadsegment(fs, 0);
    if (current->thread.fs)
      checking_wrmsrl(MSR_FS_BASE, current->thread.fs);    
                             
    savesegment(gs, gsindex); //read the gs register in gsindex variable
    if ( !(clone_data->thread_gs) && !(__user_addr(clone_data->thread_gs)) ) {
      printk(KERN_ERR "%s: ERROR corrupted gs base address %p\n", __func__, clone_data->thread_gs);      
    }
    if (unlikely(gsindex | current->thread.gsindex))
      load_gs_index(current->thread.gsindex);
    else
      load_gs_index(0);
    if (current->thread.gs)
      checking_wrmsrl(MSR_KERNEL_GS_BASE, current->thread.gs);
#else
    {
    int i, ch;
    const char* name = NULL;
    char tcomm[sizeof(current->comm)];

    flush_thread();
    set_fs(USER_DS);
    current->flags &= ~(PF_RANDOMIZE | PF_KTHREAD);
    current->sas_ss_sp = current->sas_ss_size = 0;

    // Copy exe name
    name = clone_data->exe_path;
    for(i = 0; (ch = *(name++)) != '\0';) {
        if(ch == '/')
            i = 0;
        else if (i < (sizeof(tcomm) - 1)) 
            tcomm[i++] = ch;
    }
    tcomm[i] = '\0';
    set_task_comm(current,tcomm);

    current->self_exec_id++;
        
    flush_signal_handlers(current,0);
   
    int cnt=0,flags;
    lock_task_sighand(current, &flags);
    current->origin_pid = clone_data->origin_pid;
    sigorsets(&current->blocked,&current->blocked,&clone_data->remote_blocked) ;
    sigorsets(&current->real_blocked,&current->real_blocked,&clone_data->remote_real_blocked);
    sigorsets(&current->saved_sigmask,&current->saved_sigmask,&clone_data->remote_saved_sigmask);


    current->sas_ss_sp = clone_data->sas_ss_sp;
    current->sas_ss_size = clone_data->sas_ss_size;

    for(cnt=0;cnt<_NSIG;cnt++)
         current->sighand->action[cnt] = clone_data->action[cnt];
    unlock_task_sighand(current, &flags);


    flush_old_files(current->files);
    }
    start_remote_thread(regs);
#endif

    } // FS/GS update --- end

    // Save off clone data, replacing any that may
#ifdef FPU_   
     //FPU migration code --- server
          /* PF_USED_MATH is set if the task used the FPU before
           * fpu_counter is incremented every time you go in __switch_to while owning the FPU
           * has_fpu is true if the task is the owner of the FPU, thus the FPU contains its data
           * fpu.preload (see arch/x86/include/asm.i387.h:switch_fpu_prepare()) is a heuristic
           */
          if (clone_data->task_flags & PF_USED_MATH)
              set_used_math();
          current->fpu_counter = clone_data->task_fpu_counter;
          if (clone_data->thread_has_fpu & HAS_FPU_MASK) {    
	  if (fpu_alloc(&current->thread.fpu) == -ENOMEM)
                  printk(KERN_ALERT "%s: ERROR fpu_alloc returned -ENOMEM, remote fpu not copied.\n", __func__);
              else {
                  struct fpu temp; temp.state = &clone_data->fpu_state;
                  fpu_copy(&current->thread.fpu, &temp);
              }
         }
/*     printk(KERN_ALERT"%s: task flags %x fpu_counter %x has_fpu %x [%d:%d]\n",
         __func__, current->flags, (int)current->fpu_counter,
          (int)current->thread.has_fpu, (int)__thread_has_fpu(current), (int)fpu_allocated(&current->thread.fpu));
  */        //FPU migration code --- is the following optional?
          if (tsk_used_math(current) && current->fpu_counter >5) //fpu.preload
              __math_state_restore(current);
#endif    
     // Save off clone data, replacing any that may
    // already exist.
#ifdef PROCESS_SERVER_USE_KMOD
    if(current->clone_data) {
        unsigned long lockflags;
        spin_lock_irqsave(&_data_head_lock,lockflags);
        remove_data_entry(current->clone_data);
        spin_unlock_irqrestore(&_data_head_lock,lockflags);
        destroy_clone_data(current->clone_data);
    }
    current->clone_data = clone_data;
#endif

    // Socket Related Operations
    int tempfd=0,err=0;
    struct sockaddr_in serv_addr, cli_addr;
 
    if(clone_data->skt_flag == 1){
	   
	    current->migrated_socket = 1;
	    
	 switch(1){
	    case MIG_SOCKET:
		  tempfd = sys_socket(AF_INET,clone_data->skt_type,IPPROTO_IP);
		  current->surrogate_fd = tempfd;
		  if(clone_data->skt_level == MIG_SOCKET) break;
	    case MIG_BIND:
		  serv_addr.sin_family = AF_INET;
		  serv_addr.sin_addr.s_addr =  clone_data->skt_saddr;
		  serv_addr.sin_port = clone_data->skt_sport;
 		  char *add;		  
    		  add = &serv_addr.sin_addr.s_addr;
                  printk(KERN_ALERT"%d.%d.%d.%d \n",add[0],add[1],add[2],add[3]);
	
		  err = sys_bind(tempfd,&serv_addr,sizeof(struct sockaddr_in));
		  printk(KERN_ALERT"BIND err %d\n",err);
		  if(clone_data->skt_level == MIG_BIND) break;
            case MIG_LISTEN:
		  break;
            case MIG_ACCEPT:
                  break; 
	    default:
		  printk("Problem in the level index\n");
	    }
	    

	    struct fdtable *files_table;
	    unsigned int i =0;
	    struct files_struct *current_files = current->files;
	    struct socket skt;
	    files_table = files_fdtable(current_files);
	    int err, fput_needed;
	    struct socket *sock = kmalloc(sizeof(struct socket),GFP_KERNEL);
	    while(files_table->fd[i] != NULL) {

	    printk(KERN_ALERT "fd is {%d}",i);
	    sock = sockfd_lookup_light(i, &err, &fput_needed);
	    if(sock){
		skt =(struct socket) *sock;
		struct sock *sk = skt.sk;
		struct inet_sock *in_ = inet_sk(sk);
		printk(KERN_ALERT"fd{%d} sock ptr {%p} type{%d} state{%d} d %d s%d saddr %d mc %d - %d \n",i,(struct socket *) sock,(int) skt.type,(int) skt.state,((struct inet_sock *)in_)->inet_dport,((struct inet_sock *)in_)->inet_sport,((struct inet_sock*)in_)->inet_saddr,((struct inet_sock*)in_)->inet_daddr,((struct inet_sock *)in_)->mc_addr);
	    }
		i++;
	    }
	}
	    PS_UP_WRITE(&_import_sem);

	    process_server_notify_delegated_subprocess_starting(current->pid,
		    clone_data->placeholder_pid,
		    clone_data->requesting_cpu);

	    //dump_task(current,NULL,0);

	    PERF_MEASURE_STOP(&perf_process_server_import_address_space, " ",perf);


	    perf_e = native_read_tsc();

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    kt_fdskt_fdskt_fdnd_time = native_read_tsc();
	    total_time = end_time - start_time;
	    PS_PROC_DATA_TRACK(PS_PROC_DATA_IMPORT_TASK_TIME,total_time);
	#endif

	    printk("%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu (%d)\n",
		    __func__,
		    perf_aa, perf_bb, perf_cc, perf_dd, perf_ee,
		    perf_a, perf_b, perf_c, perf_d, perf_e, current->t_home_id);

	    return 0;
	}

	static int call_import_task(void* data) {
	    kernel_import_task(data);
	    return -1;
	}

	static void process_import_task(struct work_struct* work) {
	    import_task_work_t* w = (import_task_work_t*)work;
	    clone_data_t* data = w->data;
	    kfree(work); 
	    kernel_thread(call_import_task, data, SIGCHLD);
	}

	long sys_process_server_import_task(void *info /*name*/,
		const char* argv,
		const char* envp,
		struct pt_regs* regs) {
	    clone_data_t* clone_data = (clone_data_t*)info;
	    unsigned long ip, sp;
	    current->clone_data = clone_data;
	    //printk("in sys_process_server_import_task pid{%d}, clone_data{%lx}\n",current->pid,(unsigned long)clone_data);
	    process_server_import_address_space(&ip,&sp,regs);
	    return 0;
	}

	/**
	 * @brief Distributes local calls to do_group_exit.  This function
	 * sends a request to all other CPUs to do a group exit on this
	 * distributed thread group.
	 */
	int process_server_do_group_exit(void) {
	    exiting_group_t msg;
	    int i;
	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    unsigned long long end_time;
	    unsigned long long total_time;
	    int do_time_measurement = 0;
	    unsigned long long start_time = native_read_tsc();
	#endif

	     // Select only relevant tasks to operate on
	    if(!(current->t_distributed || current->tgroup_distributed)/* || 
		    !current->enable_distributed_exit*/) {
		return -1;
	    }

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    do_time_measurement = 1;
	#endif

	    printk(KERN_ALERT"%s: doing distributed group exit\n",__func__);

	    // Build message
	    msg.header.type = PCN_KMSG_TYPE_PROC_SRV_EXIT_GROUP;
	    msg.header.prio = PCN_KMSG_PRIO_NORMAL;
	    msg.tgroup_home_id = current->tgroup_home_id;
	    msg.tgroup_home_cpu = current->tgroup_home_cpu;

	#ifndef SUPPORT_FOR_CLUSTERING
	    for(i = 0; i < NR_CPUS; i++) {
		// Skip the current cpu
		if(i == _cpu) continue;
	#else
	    // the list does not include the current processor group descirptor (TODO)
	    struct list_head *iter;
	    _remote_cpu_info_list_t *objPtr;
	    extern struct list_head rlist_head;
	    list_for_each(iter, &rlist_head) {
		objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
		i = objPtr->_data._processor;
	#endif
		// Send
		pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&msg));
	    }

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    if(do_time_measurement) {
		end_time = native_read_tsc();
		total_time = end_time - start_time;
		PS_PROC_DATA_TRACK(PS_PROC_DATA_GROUP_EXIT_PROCESSING_TIME,total_time);
	    }
	#endif

	    return 0;
	}

	/**
	 * @brief Notify of the fact that either a delegate or placeholder has died locally.  
	 * In this case, the remote cpu housing its counterpart must be notified, so
	 * that it can kill that counterpart.
	 *
	 * <MEASURE perf_process_server_do_exit>
	 */
	int process_server_do_exit(int exit_code) {

	    exiting_process_t msg;
	    int is_last_thread_in_local_group = 1;
	    int is_last_thread_in_group = 1;
	    struct task_struct *task, *g;
	    mm_data_t* mm_data = NULL;
	    int i;
	    thread_group_exited_notification_t exit_notification;
	    clone_data_t* clone_data = NULL;
	    int perf = -1;

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    unsigned long long end_time;
	    unsigned long long total_time;
	    int do_time_measurement = 0;
	    unsigned long long start_time = native_read_tsc();
	#endif

	    // Select only relevant tasks to operate on
	    if(!(current->t_distributed || current->tgroup_distributed)/* || 
		    !current->enable_distributed_exit*/) {
		return -1;
	    }

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    do_time_measurement = 1;
	#endif

	/*     printk("%s: CHANGED? prio: %d static: %d normal: %d rt: %u class: %d rt_prio %d\n",
			__func__,
			 current->prio, current->static_prio, current->normal_prio, current->rt_priority,
			 current->policy, rt_prio (current->prio));
	*/
	    perf = PERF_MEASURE_START(&perf_process_server_do_exit);

	    // Let's not do an exit while we're importing a new task.
	    // This could cause bad things to happen when looking
	    // for mm's.
	    PS_DOWN_WRITE(&_import_sem);

	    PSPRINTK("%s - pid{%d}, prev_cpu{%d}, prev_pid{%d}\n",__func__,
		    current->pid,
		    current->prev_cpu,
		    current->prev_pid);
	    
	    // Determine if this is the last _active_ thread in the 
	    // local group.  We have to count shadow tasks because
	    // otherwise we risk missing tasks when they are exiting
	    // and migrating back.
	    read_lock(&tasklist_lock);
	    do_each_thread(g,task) {
		if(task->tgid == current->tgid &&           // <--- narrow search to current thread group only 
			task->pid != current->pid &&        // <--- don't include current in the search
			task->exit_state != EXIT_ZOMBIE &&  // <-,
			task->exit_state != EXIT_DEAD &&    // <-|- check to see if it's in a runnable state
			!(task->flags & PF_EXITING)) {      // <-'
		    is_last_thread_in_local_group = 0;
		    goto finished_membership_search;
		}
	    } while_each_thread(g,task);
	finished_membership_search:
	    read_unlock(&tasklist_lock);

	    // Count the number of threads in this distributed thread group
	    // this will be useful for determining what to do with the mm.
	    if(!is_last_thread_in_local_group) {
		// Not the last local thread, which means we're not the
		// last in the distributed thread group either.
		is_last_thread_in_group = 0;
	#ifndef SUPPORT_FOR_CLUSTERING
	    } else if (!(task->t_home_cpu == _cpu &&
	#else
	    } else if (!(task->t_home_cpu == cpumask_first(cpu_present_mask) &&
	#endif
		      task->t_home_id == task->pid)) {
		// OPTIMIZATION: only bother to count threads if we are not home base for
		// this thread.
		is_last_thread_in_group = 0;
	    } else {
		// Last local thread, which means we MIGHT be the last
		// in the distributed thread group, but we have to check.
		int count = count_thread_members();
		if (count == 0) {
		    // Distributed thread count yielded no thread group members
		    // so the current <exiting> task is the last group member.
		    is_last_thread_in_group = 1;
		} else {
		    // There are more thread group members.
		    is_last_thread_in_group = 0;
		}
	    }
	    
	    // Find the clone data, we are going to destroy this very soon.
	    clone_data = get_current_clone_data();
	    //clone_data = find_clone_data(current->prev_cpu, current->clone_request_id);

	    // Build the message that is going to migrate this task back 
	    // from whence it came.
	    msg.header.type = PCN_KMSG_TYPE_PROC_SRV_EXIT_PROCESS;
	    msg.header.prio = PCN_KMSG_PRIO_NORMAL;
	    msg.my_pid = current->pid;
	    msg.t_home_id = current->t_home_id;
	    msg.t_home_cpu = current->t_home_cpu;
	    msg.is_last_tgroup_member = is_last_thread_in_group;

	    if(current->executing_for_remote) {
		int i;
		// this task is dying. If this is a migrated task, the shadow will soon
		// take over, so do not mark this as executing for remote
		current->executing_for_remote = 0;

		// Migrate back - you just had an out of body experience, you will wake in
		//                a familiar place (a place you've been before), but unfortunately, 
		//                your life is over.
		//                Note: comments like this must == I am tired.
	#ifndef SUPPORT_FOR_CLUSTERING
		for(i = 0; i < NR_CPUS; i++) {
		  // Skip the current cpu
		  if(i == _cpu)
		    continue;
		  if (test_bit(i,&current->previous_cpus))
	#else
		// the list does not include the current processor group descirptor (TODO)
		struct list_head *iter;
		_remote_cpu_info_list_t *objPtr;
		struct cpumask *pcpum =0;
		extern struct list_head rlist_head;
		list_for_each(iter, &rlist_head) {
		  objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
		  i = objPtr->_data._processor;
		  pcpum  = &(objPtr->_data._cpumask);
		  if ( bitmap_intersects(cpumask_bits(pcpum),  
					&(current->previous_cpus),
					(sizeof(unsigned long) *8)) )
	#endif
		    pcn_kmsg_send(i, (struct pcn_kmsg_message*)&msg);
		}
	    } 
		
	    //already sent group exit signals. wait for each one to respond.
	    if(current->tgid == current->pid && is_last_thread_in_local_group &&  (exit_code & SIGNAL_GROUP_EXIT)){

		//printk(KERN_ALERT"should wait for others\n"); 
		
	    }
	    // If this was the last thread in the local work, we take one of two 
	    // courses of action, either we:
	    //
	    // 1) determine that this is the last thread globally, and issue a 
	    //    notification to that effect.
	    //
	    //    or.
	    //
	    // 2) we determine that this is NOT the last thread globally, in which
	    //    case we save the mm to use to resolve mappings with.
	    if(is_last_thread_in_local_group) {
		// Check to see if this is the last member of the distributed
		// thread group.
		if(is_last_thread_in_group) {

		  //  printk("%s: This is the last thread member!\n",__func__);

		    // Notify all cpus
		    exit_notification.header.type = PCN_KMSG_TYPE_PROC_SRV_THREAD_GROUP_EXITED_NOTIFICATION;
		    exit_notification.header.prio = PCN_KMSG_PRIO_NORMAL;
		    exit_notification.tgroup_home_cpu = current->tgroup_home_cpu;
		    exit_notification.tgroup_home_id = current->tgroup_home_id;

	#ifndef SUPPORT_FOR_CLUSTERING
		    for(i = 0; i < NR_CPUS; i++) {
		      // Skip the current cpu
		      if(i == _cpu) continue;
	#else
		    // the list does not include the current processor group descirptor (TODO)
			struct list_head *iter;
			_remote_cpu_info_list_t *objPtr;
		    extern struct list_head rlist_head;
		    list_for_each(iter, &rlist_head) {
		      objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
		      i = objPtr->_data._processor;
	#endif
		      pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&exit_notification));
		    }

		} else {
		    // This is NOT the last distributed thread group member.  Grab
		    // a reference to the mm, and increase the number of users to keep 
		    // it from being destroyed
		    //printk("%s: This is not the last thread member, saving mm\n",
		      //      __func__);
		    if (current && current->mm)
			atomic_inc(&current->mm->mm_users);
		    else
			printk("%s: ERROR current %p, current->mm %p\n", __func__, current, current->mm);

		    // Remember the mm
		    mm_data = kmalloc(sizeof(mm_data_t),GFP_KERNEL);
		    mm_data->header.data_type = PROCESS_SERVER_MM_DATA_TYPE;
		    mm_data->mm = current->mm;
		    mm_data->tgroup_home_cpu = current->tgroup_home_cpu;
		    mm_data->tgroup_home_id  = current->tgroup_home_id;

		    // Add the data entry
		    add_data_entry_to(mm_data,
				      &_saved_mm_head_lock,
				      &_saved_mm_head);

		}

	    } else {
		//printk("%s: This is not the last local thread member\n",__func__);
	    }

	    // We know that this task is exiting, and we will never have to work
	    // with it again, so remove its clone_data from the linked list, and
	    // nuke it.
	    if(clone_data) {
	#ifdef PROCESS_SERVER_USE_KMOD
		unsigned long lockflags;
		spin_lock_irqsave(&_data_head_lock,lockflags);
		remove_data_entry(clone_data);
		spin_unlock_irqrestore(&_data_head_lock,lockflags);
	#endif
		destroy_clone_data(clone_data);
	    }

	    PS_UP_WRITE(&_import_sem);
	    
	    PERF_MEASURE_STOP(&perf_process_server_do_exit," ",perf);

	#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
	    if(do_time_measurement) {
		end_time = native_read_tsc();
		total_time = end_time - start_time;
		PS_PROC_DATA_TRACK(PS_PROC_DATA_EXIT_PROCESSING_TIME,total_time);
	    }
	#endif

	    return 0;
	}

	/**
	 * @brief Create a pairing between a newly created delegate process and the
	 * remote placeholder process.  This function creates the local
	 * pairing first, then sends a message to the originating cpu
 * so that it can do the same.
 */
int process_server_notify_delegated_subprocess_starting(pid_t pid, 
        pid_t remote_pid, int remote_cpu) {

    create_process_pairing_t msg;
    int perf = PERF_MEASURE_START(&perf_process_server_notify_delegated_subprocess_starting);

    PSPRINTK("kmkprocsrv: notify_subprocess_starting: pid{%d}, remote_pid{%d}, remote_cpu{%d}\n",
            pid,remote_pid,remote_cpu);
    
    // Notify remote cpu of pairing between current task and remote
    // representative task.
    msg.header.type = PCN_KMSG_TYPE_PROC_SRV_CREATE_PROCESS_PAIRING;
    msg.header.prio = PCN_KMSG_PRIO_NORMAL;
    msg.your_pid = remote_pid; 
    msg.my_pid = pid;
    
    if(0 != pcn_kmsg_send(remote_cpu, (struct pcn_kmsg_message*)(&msg))) {
        printk("%s: ERROR sending message pairing message to cpu %d\n",
                __func__,
                remote_cpu);
    }

    PERF_MEASURE_STOP(&perf_process_server_notify_delegated_subprocess_starting,
            " ",
            perf);

    return 0;

}

/**
 * @brief If the current process is distributed, we want to make sure that all members
 * of this distributed thread group carry out the same munmap operation.  Furthermore,
 * we want to make sure they do so _before_ this syscall returns.  So, synchronously
 * command every cpu to carry out the munmap for the specified thread group.
 *
 * <MEASURE perf_process_server_do_munmap>
 */
int process_server_do_munmap(struct mm_struct* mm, 
            unsigned long start, 
            unsigned long len) {

    munmap_request_data_t* data;
    munmap_request_t request;
    int i;
    int s;
    int perf = -1;
    unsigned long lockflags;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time = 0;
    unsigned long long total_time = 0;
    unsigned long long start_time = native_read_tsc();
    int do_time_measurement = 0;
#endif

     // Nothing to do for a thread group that's not distributed.
    if(!current->tgroup_distributed || !current->enable_distributed_munmap) {
        goto exit;
    } 

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    do_time_measurement = 1;
#endif

    perf = PERF_MEASURE_START(&perf_process_server_do_munmap);

    data = kmalloc(sizeof(munmap_request_data_t),GFP_KERNEL);
    if(!data) goto exit;

    data->header.data_type = PROCESS_SERVER_MUNMAP_REQUEST_DATA_TYPE;
    data->vaddr_start = start;
    data->vaddr_size = len;
    data->responses = 0;
    data->expected_responses = 0;
    data->tgroup_home_cpu = current->tgroup_home_cpu;
    data->tgroup_home_id = current->tgroup_home_id;
    data->requester_pid = current->pid;
    spin_lock_init(&data->lock);

    add_data_entry_to(data,
                      &_munmap_data_head_lock,
                      &_munmap_data_head);

    request.header.type = PCN_KMSG_TYPE_PROC_SRV_MUNMAP_REQUEST;
    request.header.prio = PCN_KMSG_PRIO_NORMAL;
    request.vaddr_start = start;
    request.vaddr_size  = len;
    request.tgroup_home_cpu = current->tgroup_home_cpu;
    request.tgroup_home_id  = current->tgroup_home_id;
    request.requester_pid = current->pid;

    // This function is always called with mm->mmap_sem held.
    // We have to release it to avoid deadlocks.  If this
    // lock is held and another kernel is also munmapping,
    // then if we hold the lock here, the munmap message
    // handler fails to acquire the mm->mmap_sem, and deadlock
    // ensues.
    up_write(&mm->mmap_sem);

#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        // Skip the current cpu
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;
#endif
        // Send the request to this cpu.
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&request));
        if(!s) {
            // A successful send operation, increase the number
            // of expected responses.
            data->expected_responses++;
        }
    }

    // Wait for all cpus to respond.
    while(data->expected_responses != data->responses) {
        schedule();
    }

    down_write(&mm->mmap_sem);

    // OK, all responses are in, we can proceed.

    spin_lock_irqsave(&_munmap_data_head_lock,lockflags);
    remove_data_entry_from(data,
                           &_munmap_data_head);
    spin_unlock_irqrestore(&_munmap_data_head_lock,lockflags);

    kfree(data);

exit:

    PERF_MEASURE_STOP(&perf_process_server_do_munmap,"Exit success",perf);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    if(do_time_measurement) {
        end_time = native_read_tsc();
        total_time = end_time - start_time;
        PS_PROC_DATA_TRACK(PS_PROC_DATA_MUNMAP_PROCESSING_TIME,total_time);
    }
#endif

    return 0;
}

/**
 * @brief Hooks do_mprotect.  Local protection changes must invalidate
 * the corresponding remote page mappings to force other CPUs to re-acquire
 * the modified mappings.
 */
void process_server_do_mprotect(struct task_struct* task,
                                unsigned long start,
                                size_t len,
                                unsigned long prot) {
    mprotect_data_t* data;
    mprotect_request_t request;
    int i;
    int s;
    int perf = -1;
    unsigned lockflags;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time;
    unsigned long long total_time;
    unsigned long long start_time = native_read_tsc();
    int do_time_measurement = 0;
#endif

     // Nothing to do for a thread group that's not distributed.
    if(!current->tgroup_distributed) {
        goto exit;
    }

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    do_time_measurement = 1;
#endif

    PSPRINTK("%s entered\n",__func__);

    perf = PERF_MEASURE_START(&perf_process_server_do_mprotect);

    data = kmalloc(sizeof(mprotect_data_t),GFP_KERNEL);
    if(!data) goto exit;

    data->header.data_type = PROCESS_SERVER_MPROTECT_DATA_TYPE;
    data->responses = 0;
    data->expected_responses = 0;
    data->tgroup_home_cpu = task->tgroup_home_cpu;
    data->tgroup_home_id = task->tgroup_home_id;
    data->requester_pid = task->pid;
    data->start = start;
    spin_lock_init(&data->lock);

    add_data_entry_to(data,
                      &_mprotect_data_head_lock,
                      &_mprotect_data_head);

    request.header.type = PCN_KMSG_TYPE_PROC_SRV_MPROTECT_REQUEST;
    request.header.prio = PCN_KMSG_PRIO_NORMAL;
    request.start = start;
    request.len  = len;
    request.prot = prot;
    request.tgroup_home_cpu = task->tgroup_home_cpu;
    request.tgroup_home_id  = task->tgroup_home_id;
    request.requester_pid = task->pid;

    PSPRINTK("Sending mprotect request to all other kernels... ");
#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        // Skip the current cpu
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;
#endif
        // Send the request to this cpu.
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&request));
        if(!s) {
            // A successful send operation, increase the number
            // of expected responses.
            data->expected_responses++;
        }
    }

    PSPRINTK("done\nWaiting for responses... ");

    // Wait for all cpus to respond.
    while(data->expected_responses != data->responses) {
        schedule();
    }

    PSPRINTK("done\n");

    // OK, all responses are in, we can proceed.

    spin_lock_irqsave(&_mprotect_data_head_lock,lockflags);
    remove_data_entry_from(data,
                           &_mprotect_data_head);
    spin_unlock_irqrestore(&_mprotect_data_head_lock,lockflags);

    kfree(data);

exit:

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    if(do_time_measurement) {
        end_time = native_read_tsc();
        total_time = end_time - start_time;
        PS_PROC_DATA_TRACK(PS_PROC_DATA_MPROTECT_PROCESSING_TIME,total_time);
    }
#endif

    PERF_MEASURE_STOP(&perf_process_server_do_mprotect," ",perf);

}

/**
 * @brief Hooks do_mmap_pgoff.  This is necessary in order to maintain
 * address space coherency, since do_mmap_pgoff can modify existing
 * mappings.  If do_mmap_pgoff were allowed to modify existing mappings
 * without first unmapping those mappings remotely, multiple different
 * mappings for the same address would then exist between CPUs.  This
 * function forces remote munmap prior to doing the local do_mmap_pgoff.
 */
unsigned long process_server_do_mmap_pgoff(struct file *file, unsigned long addr,
                                           unsigned long len, unsigned long prot,
                                           unsigned long flags, unsigned long pgoff) {

    // Nothing to do for a thread group that's not distributed.
    // Also, skip if the mmap hook is turned off.  This should
    // only happen when memory mapping within process_server
    // fault handler and import address space.
    if(!current->tgroup_distributed || !current->enable_do_mmap_pgoff_hook) {
        goto not_handled_no_perf;
    }

    // Do a distributed munmap on the entire range of addresses that
    // are about to be remapped.  This will ensure that the range
    // is cleared out remotely, as well as locally (handled by the
    // do_mmap_pgoff implementation) to keep from having differing
    // vm address spaces on different cpus.
    process_server_do_munmap(current->mm,
                             addr,
                             len);

not_handled_no_perf:
    return 0;
}

/**
 * @brief Implements on-demand page migration.  As this CPU faults,
 * this fault handler is invoked.  Its job is to pull in any mappings
 * that may exist for the faulting address from other CPUs.
 * @return 0 = not handled, 1 = handled.
 *
 * <MEASURED perf_process_server_try_handle_mm_fault>
 */
int process_server_pull_remote_mappings(struct mm_struct *mm, 
                                       struct vm_area_struct *vma,
                                       unsigned long address, 
                                       unsigned int flags, 
                                       struct vm_area_struct **vma_out,
                                       unsigned long error_code) {

    mapping_request_data_t *data = NULL;
    unsigned long err = 0;
    int ret = 0;
    mapping_request_t request;
    int i;
    int s;
    int j;
    struct file* f = NULL;
    unsigned long prot = 0;
    unsigned char started_outside_vma = 0;
    unsigned char did_early_removal = 0;
    char path[512];
    char* ppath;
    // for perf
    unsigned char pte_provided = 0;
    unsigned char is_anonymous = 0;
    unsigned char vma_not_found = 1;
    unsigned char adjusted_permissions = 0;
    unsigned char is_new_vma = 0;
    unsigned char paddr_present = 0;
    int perf = -1;
    int original_enable_distributed_munmap = current->enable_distributed_munmap;
    int original_enable_do_mmap_pgoff_hook = current->enable_do_mmap_pgoff_hook;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long mapping_wait_start = 0;
    unsigned long long mapping_wait_end = 0;
    unsigned long long mapping_request_send_start = 0;
    unsigned long long mapping_request_send_end = 0;
    unsigned long long fault_processing_time_start = 0;
    unsigned long long fault_processing_time_end = 0;
    unsigned long long fault_processing_time = 0;
#endif


    // Nothing to do for a thread group that's not distributed.
    if(!current->tgroup_distributed) {
        goto not_handled_no_perf;
    }

    current->enable_distributed_munmap = 0;
    current->enable_do_mmap_pgoff_hook = 0;
    
    perf = PERF_MEASURE_START(&perf_process_server_try_handle_mm_fault);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    fault_processing_time_start = native_read_tsc();
#endif

    PSPRINTK("Fault caught on address{%lx}, cpu{%d}, id{%d}, pid{%d}, tgid{%d}, error_code{%lx}\n",
            address,
            current->tgroup_home_cpu,
            current->tgroup_home_id,
            current->pid,
            current->tgid,
            error_code);

    if(is_vaddr_mapped(mm,address)) {
        PSPRINTK("exiting mk fault handler because vaddr %lx is already mapped- cpu{%d}, id{%d}\n",
                address,current->tgroup_home_cpu,current->tgroup_home_id);

        // should this thing be writable?  if so, set it and exit
        // This is a security hole, and is VERY bad.
        // It will also probably cause problems for genuine COW mappings..
        if(!vma) {
            vma = find_vma_checked(mm, address & PAGE_MASK);
            if(!vma)
                PSPRINTK("VMA failed to resolve\n");
        }
        if(vma && 
                vma->vm_flags & VM_WRITE /*&& 
                0 == is_page_writable(mm, vma, address & PAGE_MASK)*/) {
            PSPRINTK("Touching up write setting\n");
            mk_page_writable(mm,vma,address & PAGE_MASK);
            adjusted_permissions = 1;
            ret = 1;
        } else {
            PSPRINTK("Did not touch up write settings\n");
        }

        goto not_handled;
    }

    // Is this an optimization?  Will it work?
    // The idea is to not bother pulling in physical pages
    // for mappings that are executable and file backed, since
    // those pages will never change.  Might as well map
    // them locally.
#if !(MIGRATE_EXECUTABLE_PAGES_ON_DEMAND)
    if(vma && 
            (vma->vm_start <= address) &&
            (vma->vm_end > address) &&
            (vma->vm_flags & VM_EXEC) && 
            vma->vm_file) {
        ret = 0;
        PSPRINTK("Skipping distributed mapping pull because page is executable\n");

        goto not_handled;
    }
#endif
    
    // The vma that's passed in might not always be correct.  find_vma fails by returning the wrong
    // vma when the vma is not present.  How ugly...
    if(vma && (vma->vm_start > address || vma->vm_end <= address)) {
        started_outside_vma = 1;
        PSPRINTK("set vma = NULL, since the vma does not hold the faulting address, for whatever reason...\n");
        vma = NULL;
    } else if (vma) {
        //PSPRINTK("vma found and valid\n");
    } else {
        //PSPRINTK("no vma present\n");
    }

    data = kmalloc(sizeof(mapping_request_data_t),GFP_KERNEL); 
   
    // Set up data entry to share with response handler.
    // This data entry will be modified by the response handler,
    // and we will check it periodically to see if our request
    // has been responded to by all active cpus.
    data->header.data_type = PROCESS_SERVER_MAPPING_REQUEST_DATA_TYPE;
    data->address = address;
    data->present = 0;
    data->complete = 0;
    spin_lock_init(&data->lock);
    data->responses = 0;
    data->expected_responses = 0;
    data->tgroup_home_cpu = current->tgroup_home_cpu;
    data->tgroup_home_id = current->tgroup_home_id;
    data->requester_pid = current->pid;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    data->wait_time_concluded = 0;
#endif
    for(j = 0; j < MAX_MAPPINGS; j++) {
        data->mappings[j].present = 0;
        data->mappings[j].vaddr = 0;
        data->mappings[j].paddr = 0;
        data->mappings[j].sz = 0;
    }


    // Make data entry visible to handler.
    add_data_entry_to(data,
                      &_mapping_request_data_head_lock,
                      &_mapping_request_data_head);

    // Send out requests, tracking the number of successful
    // send operations.  That number is the number of requests
    // we will expect back.
    request.header.type = PCN_KMSG_TYPE_PROC_SRV_MAPPING_REQUEST;
    request.header.prio = PCN_KMSG_PRIO_NORMAL;
    request.address = address;
    request.tgroup_home_cpu = current->tgroup_home_cpu;
    request.tgroup_home_id  = current->tgroup_home_id;
    request.requester_pid = current->pid;
    request.need_vma = vma? 0 : 1; // Optimization, do not bother
                                    // sending the vma path if a local
                                    // vma is already installed, since
                                    // we know that a do_mmap_pgoff will
                                    // not be needed in this case.
    // Part of need_vma optimization.  Just record the path in the
    // data structure since we know it in advance, and since the
    // resolving kernel instance is no longer responsible for providing
    // it in this case.
    if(!request.need_vma && vma->vm_file) {
        d_path(&vma->vm_file->f_path,data->path,sizeof(data->path));
    }

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    mapping_request_send_start = native_read_tsc();
#endif

#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        // Skip the current cpu
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) { 
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;
#endif
        // Send the request to this cpu.
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        request.send_time = native_read_tsc();
#endif
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&request));
        if(!s) {
            // A successful send operation, increase the number
            // of expected responses.
            data->expected_responses++;
        }
    }
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    mapping_request_send_end = native_read_tsc();
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_REQUEST_SEND_TIME,
                        mapping_request_send_end - mapping_request_send_start);
#endif

    // Wait for all cpus to respond, or a mapping that is complete
    // with a physical mapping.  Mapping results that do not include
    // a physical mapping cause this to wait until all mapping responses
    // have arrived from remote cpus.
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    mapping_wait_start = native_read_tsc();
#endif
    while(1) {
        unsigned char done = 0;
        unsigned long lockflags;
        spin_lock_irqsave(&data->lock,lockflags);
        if(data->expected_responses == data->responses || data->complete)
            done = 1;
        spin_unlock_irqrestore(&data->lock,lockflags);
        if (done) {
            unsigned long lockflags;
            spin_lock_irqsave(&_mapping_request_data_head_lock,lockflags);
            remove_data_entry_from(data,
                                  &_mapping_request_data_head);
            spin_unlock_irqrestore(&_mapping_request_data_head_lock,lockflags);
            did_early_removal = 1;
            break;
        }
        schedule();
    }
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    mapping_wait_end = native_read_tsc();
    PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_WAIT_TIME,
                        mapping_wait_end - mapping_wait_start);
    if(data->wait_time_concluded) {
        PS_PROC_DATA_TRACK(PS_PROC_DATA_MAPPING_POST_WAIT_TIME_RESUME,
                            mapping_wait_end - data->wait_time_concluded);
    }
#endif
    
    // Handle successful response.
    if(data->present) {
        PSPRINTK(/*KERN_ALERT*/"Mapping(%d): %lx v:%lx p:%lx vaddr{%lx-%lx} prot{%lx} vm_flags{%lx} pgoff{%lx} \"%s\"\n",
		smp_processor_id(), (unsigned long)vma, data->address, data->mappings[0].paddr,
                data->vaddr_start, (data->vaddr_start + data->vaddr_size),
                data->prot, data->vm_flags, data->pgoff, data->path);
        vma_not_found = 0;

        // Figure out how to protect this region.
        prot |= (data->vm_flags & VM_READ)?  PROT_READ  : 0;
        prot |= (data->vm_flags & VM_WRITE)? PROT_WRITE : 0;
        prot |= (data->vm_flags & VM_EXEC)?  PROT_EXEC  : 0;

        //PS_DOWN_WRITE(&current->mm->mmap_sem);

        // If there was not previously a vma, create one.
        if(!vma || vma->vm_start != data->vaddr_start || vma->vm_end != (data->vaddr_start + data->vaddr_size)) {
            PSPRINTK("vma not present\n");
            is_new_vma = 1;
            if(data->path[0] == '\0') {       
                PSPRINTK("mapping anonymous\n");
                is_anonymous = 1;
                // mmap parts that are missing, while leaving the existing
                // parts untouched.
                PS_DOWN_WRITE(&current->mm->mmap_sem);
                err = do_mmap_remaining(NULL,
                        data->vaddr_start,
                        data->vaddr_size,
                        prot,
                        MAP_FIXED|
                        MAP_ANONYMOUS|
                        ((data->vm_flags & VM_SHARED)?MAP_SHARED:MAP_PRIVATE),
                        0, (data->vm_flags & VM_NORESERVE) ?1:0);
                PS_UP_WRITE(&current->mm->mmap_sem);

/*		// NOTE we use the following to catch the megabug
		if ( data->vm_flags & VM_NORESERVE )
			printk(KERN_ALERT"MAPPING ANONYMOUS %p %p data: %lx vma: %lx {%lx-%lx} ret%lx\n",
				__func__, data->mappings[i].vaddr, data->mappings[i].paddr, 
				data->vm_flags, vma?vma->vm_flags:0, vma?vma->vm_start:0, vma?vma->vm_end:0, err);*/
            } else {
                //unsigned char used_existing;
                PSPRINTK("opening file to map\n");
                is_anonymous = 0;

                // Temporary, check to see if the path is /dev/null (deleted), it should just
                // be /dev/null in that case.  TODO: Add logic to detect and remove the 
                // " (deleted)" from any path here.  This is important, because anonymous mappings
                // are sometimes, depending on how glibc is compiled, mapped instead to the /dev/zero
                // file, and without this check, the filp_open call will fail because the "(deleted)"
                // string at the end of the path results in the file not being found.
                if( !strncmp( "/dev/zero (deleted)", data->path, strlen("/dev/zero (deleted)")+1 )) {
                    data->path[9] = '\0';
                }
               
                //if(vma && vma->vm_file) {
                //    used_existing = 0;
                //    f = fget(fileno(vma->vm_file));
                //} else {
                //    used_existing = 1;
                    f = filp_open(data->path, (data->vm_flags & VM_SHARED)? O_RDWR:O_RDONLY, 0);
                //}

                if(!IS_ERR(f)) {
                    PSPRINTK("mapping file %s, %lx, %lx, %lx\n",data->path,
                            data->vaddr_start, 
                            data->vaddr_size,
                            (unsigned long)f);
                    // mmap parts that are missing, while leaving the existing
                    // parts untouched.
                    PS_DOWN_WRITE(&current->mm->mmap_sem);
                    err = do_mmap_remaining(f,
                            data->vaddr_start,
                            data->vaddr_size,
                            prot,
                            MAP_FIXED |
                            ((data->vm_flags & VM_DENYWRITE)?MAP_DENYWRITE:0) |
                            ((data->vm_flags & VM_EXECUTABLE)?MAP_EXECUTABLE:0) |
                            ((data->vm_flags & VM_SHARED)?MAP_SHARED:MAP_PRIVATE),
                            data->pgoff << PAGE_SHIFT, (data->vm_flags & VM_NORESERVE) ?1:0);
                    PS_UP_WRITE(&current->mm->mmap_sem);

                    //if(used_existing) {
                    //    fput(f);
                    //} else {
                        filp_close(f,NULL);
                    //}
                } else {
                    printk("Error opening file %s\n",data->path);
                }
            }
            if(err != data->vaddr_start) {
                PSPRINTK("ERROR: Failed to do_mmap %lx\n",err);
                //PS_UP_WRITE(&current->mm->mmap_sem);
                goto exit_remove_data;
            }
            PS_DOWN_READ(&current->mm->mmap_sem); 
            vma = find_vma_checked(current->mm, data->address); //data->vaddr_start);
            PS_UP_READ(&current->mm->mmap_sem);
            if (data->address < vma->vm_start || vma->vm_end <= data->address)
		printk(KERN_ALERT"%s: ERROR %lx is not mapped in current vma {%lx-%lx} remote vma {%lx-%lx}\n",
			__func__, data->address, vma->vm_start, vma->vm_end,
			data->vaddr_start, (data->vaddr_start + data->vaddr_size));
        } else {
            PSPRINTK("vma is present, using existing\n");
        }

        if(vma) {
            vma->vm_flags |= VM_MIXEDMAP;
        }

        // We should have a vma now, so map physical memory into it.
        // Check to see if we have mappings
        for(i = 0; i < MAX_MAPPINGS; i++) {
            if(data->mappings[i].present) {
                paddr_present = 1;
                break;
            }
        }
        if(vma && paddr_present) { 
            int remap_pfn_range_err = 0;
            pte_provided = 1;
            unsigned long cow_addr;


            for(i = 0; i < MAX_MAPPINGS; i++) {
                if(data->mappings[i].present) {
                    int tmp_err;
                    PS_DOWN_WRITE(&current->mm->mmap_sem);
                    tmp_err = remap_pfn_range_remaining(current->mm,
                                                       vma,
                                                       data->mappings[i].vaddr,
                                                       data->mappings[i].paddr,
                                                       data->mappings[i].sz,
                                                       vm_get_page_prot(vma->vm_flags),
                                                       1);
                    PS_UP_WRITE(&current->mm->mmap_sem);
                    if(tmp_err) remap_pfn_range_err = tmp_err;
                }
            }

            // Check remap_pfn_range success
            if(remap_pfn_range_err) {
                printk(KERN_ALERT"ERROR: Failed to remap_pfn_range %d\n",err);
            } else {
                PSPRINTK("remap_pfn_range succeeded\n");
                ret = 1;
            }
        } 

        //PS_UP_WRITE(&current->mm->mmap_sem);

        if(vma) {
            *vma_out = vma;
        }
    }

exit_remove_data:

    if(!did_early_removal) {
        unsigned long lockflags;
        spin_lock_irqsave(&_mapping_request_data_head_lock,lockflags);
        remove_data_entry_from(data,
                              &_mapping_request_data_head);
        spin_unlock_irqrestore(&_mapping_request_data_head_lock,lockflags);
    }
    kfree(data);

    PSPRINTK("exiting fault handler\n");

not_handled:

    if (adjusted_permissions) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_ADJUSTED_PERMISSIONS,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,"Adjusted Permissions",perf);
    } else if (is_new_vma && is_anonymous && pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_NEWVMA_ANONYMOUS_PTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "New Anonymous VMA + PTE",
                perf);
    } else if (is_new_vma && is_anonymous && !pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_NEWVMA_ANONYMOUS_NOPTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "New Anonymous VMA + No PTE",
                perf);
    } else if (is_new_vma && !is_anonymous && pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_NEWVMA_FILEBACKED_PTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "New File Backed VMA + PTE",
                perf);
    } else if (is_new_vma && !is_anonymous && !pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_NEWVMA_FILEBACKED_NOPTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "New File Backed VMA + No PTE",
                perf);
    } else if (!is_new_vma && is_anonymous && pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_OLDVMA_ANONYMOUS_PTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "Existing Anonymous VMA + PTE",
                perf);
    } else if (!is_new_vma && is_anonymous && !pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_OLDVMA_ANONYMOUS_NOPTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "Existing Anonymous VMA + No PTE",
                perf);
    } else if (!is_new_vma && !is_anonymous && pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_OLDVMA_FILEBACKED_PTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "Existing File Backed VMA + PTE",
                perf);
    } else if (!is_new_vma && !is_anonymous && !pte_provided) {
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        PS_PROC_DATA_TRACK(PS_PROC_DATA_OLDVMA_FILEBACKED_NOPTE,0);
#endif
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,
                "Existing File Backed VMA + No PTE",
                perf);
    } else {
        PERF_MEASURE_STOP(&perf_process_server_try_handle_mm_fault,"test",perf);
    }

    current->enable_distributed_munmap = original_enable_distributed_munmap;
    current->enable_do_mmap_pgoff_hook = original_enable_do_mmap_pgoff_hook;

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    fault_processing_time_end = native_read_tsc();
    fault_processing_time = fault_processing_time_end - fault_processing_time_start;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_FAULT_PROCESSING_TIME,
                        fault_processing_time);
#endif

    return ret;

not_handled_no_perf:
    
    return 0;
}

/**
 *  
 */
void break_all_cow_pages(struct task_struct* task, struct task_struct* orig) {
    struct mm_struct* mm = task->mm;
    struct vm_area_struct* curr = mm->mmap;
    unsigned long i,start, end;
    while(curr) {
        if(is_maybe_cow(curr)) {
            start = curr->vm_start;
            end = curr->vm_end;
            for(i = start; i < end; i += PAGE_SIZE) {
                if(break_cow(mm,curr,i)) {
                    mk_page_writable_lookupvma(orig->mm,i);
                }
            }
        }
        curr = curr->vm_next;
    }
}

/**
 * @brief Propagate origin thread group to children, and initialize other 
 * task members.  If the parent was member of a remote thread group,
 * store the thread group info.
 *
 * Note: In addition to in this function, distribution information is
 * maintained when a thread or process is migrated.  That ensures
 * that all members of a thread group are kept up-to-date when
 * one member migrates.
 */
int process_server_dup_task(struct task_struct* orig, struct task_struct* task)
{
    // TODO more work to support kernel clustering is still required
    int home_kernel =
#ifndef SUPPORT_FOR_CLUSTERING
    _cpu;
#else
    cpumask_first(cpu_present_mask);
#endif

    task->executing_for_remote = 0;
    task->represents_remote = 0;
    task->enable_do_mmap_pgoff_hook = 1;
    task->prev_cpu = -1;
    task->next_cpu = -1;
    task->prev_pid = -1;
    task->next_pid = -1;
    task->clone_data = NULL;
    task->t_home_cpu = home_kernel;
    task->t_home_id  = task->pid;
    task->t_distributed = 0;
    task->previous_cpus = 0;
    task->known_cpu_with_tgroup_mm = 0;
    task->return_disposition = RETURN_DISPOSITION_NONE;
    task->surrogate = -1;
    task->uaddr = 0;
    task->futex_state = 0;
    task->migration_state = 0;
    task->migrated_socket = 0;
    task->surrogate_fd = -1;
    spin_lock_init(&(task->mig_lock));
    task->signal_state = 0;
    task->dest_cpu = smp_processor_id();
    task->remote_hb = NULL;
    
// If this is pid 1 or 2, the parent cannot have been migrated
    // so it is safe to take on all local thread info.
    if(unlikely(orig->pid == 1 || orig->pid == 2)) {
        task->tgroup_home_cpu = home_kernel;
        task->tgroup_home_id = orig->tgid;
        task->tgroup_distributed = 0;
        return 1;
    }
    // If the new task is not in the same thread group as the parent,
    // then we do not need to propagate the old thread info.
    if(orig->tgid != task->tgid) {
        task->tgroup_home_cpu = home_kernel;
        task->tgroup_home_id = task->tgid;
        task->tgroup_distributed = 0;

        // COW problem fix, necessary for coherency.
        if(orig->tgroup_distributed) {
            break_all_cow_pages(task,orig);
        }

        return 1;
    }

    // Inherit the list of known cpus with mms for this thread group 
    // once we know that the task is int he same tgid.
    task->known_cpu_with_tgroup_mm = orig->known_cpu_with_tgroup_mm;

    // This is important.  We want to make sure to keep an accurate record
    // of which cpu and thread group the new thread is a part of.
    if(orig->executing_for_remote == 1 || orig->tgroup_home_cpu != home_kernel) {
        task->tgroup_home_cpu = orig->tgroup_home_cpu;
        task->tgroup_home_id = orig->tgroup_home_id;
        task->tgroup_distributed = 1;
       // task->origin_pid = orig->origin_pid;
    } else {
        task->tgroup_home_cpu = home_kernel;
        task->tgroup_home_id = orig->tgid;
        task->tgroup_distributed = 0;
    }
//printk(KERN_ALERT"TGID {%d} \n",task->tgid);
    return 1;
}


/**
 * @brief Migrate the specified task <task> to a CPU on which
 * it has not yet executed.
 *
 * <MEASURE perf_process_server_do_migration>
 */
int do_migration_to_new_cpu(struct task_struct* task, int cpu) {
    struct pt_regs *regs = task_pt_regs(task);
    // TODO: THIS IS WRONG, task flags is not what I want here.
    unsigned long clone_flags = task->clone_flags;
    unsigned long stack_start = task->mm->start_stack;
    clone_request_t* request = kmalloc(sizeof(clone_request_t),GFP_KERNEL);
    struct task_struct* tgroup_iterator = NULL;
    struct task_struct* g;
    int dst_cpu = cpu;
    char path[256] = {0};
    char* rpath = d_path(&task->mm->exe_file->f_path,path,256);
    int lclone_request_id;
    int perf = -1;
    int sock_fd;
    unsigned long flags=0;
    //printk("process_server_do_migration pid{%d} cpu {%d}\n",task->pid,cpu);

    // Nothing to do if we're migrating to the current cpu
    if(dst_cpu == _cpu) {
        return PROCESS_SERVER_CLONE_FAIL;
    }
    spin_lock_irq(&(task->mig_lock));
         task->migration_state = 1;
    spin_unlock_irq(&(task->mig_lock));
    smp_mb();

    perf = PERF_MEASURE_START(&perf_process_server_do_migration);

    // This will be a placeholder process for the remote
    // process that is subsequently going to be started.
    // Block its execution.
    __set_task_state(task,TASK_UNINTERRUPTIBLE);


    int sig;
    struct task_struct *t=current;
    // Book keeping for previous cpu bitmask.
    set_bit(smp_processor_id(),&task->previous_cpus);

    // Book keeping for placeholder process.
    spin_lock_irq(&(task->mig_lock));
    task->represents_remote = 1;
    task->tgroup_distributed = 1;
    task->t_distributed = 1;
    spin_unlock_irq(&(task->mig_lock));

    struct fdtable *files_table;
    struct sock *sk = NULL;
    struct inet_sock *in_ = NULL;
    unsigned int i =0;
    struct files_struct *current_files = current->files;
    struct socket skt;
    files_table = files_fdtable(current_files);
    int err, fput_needed;
    struct socket *sock = kmalloc(sizeof(struct socket),GFP_KERNEL);
    while(files_table->fd[i] != NULL) {

    printk(KERN_ALERT "fd is {%d}",i);
    sock = sockfd_lookup_light(i, &err, &fput_needed);
    if(sock){
	sock_fd = i;
	char * add;
        request->skt_flag = 1;
        skt =(struct socket) *sock;
	sk = skt.sk;
	in_ = inet_sk(sk);
	add = &in_->inet_saddr;
	printk(KERN_ALERT"fd{%d} sock ptr {%p} type{%d} state{%d} d %d s%d saddr %ld (%d.%d.%d.%d) mc %d - %d \n",i,(struct socket *) sock,(int) skt.type,(int) skt.state,((struct inet_sock *)in_)->inet_dport,((struct inet_sock *)in_)->inet_sport,((struct inet_sock*)in_)->inet_saddr,add[0],add[1],add[2],add[3],((struct inet_sock*)in_)->inet_daddr,((struct inet_sock *)in_)->mc_addr);
    }
	i++;
    }



    /*mklinux_akshay*/
    if(task->prev_pid==-1)
    	task->origin_pid=task->pid;
    else
    	task->origin_pid=task->origin_pid;

   struct task_struct *par = task->parent;


    // Book keeping for distributed threads.

    read_lock(&tasklist_lock);
    do_each_thread(g,tgroup_iterator) {
        if(tgroup_iterator != task) {
            if(tgroup_iterator->tgid == task->tgid) {
                tgroup_iterator->tgroup_distributed = 1;
                tgroup_iterator->tgroup_home_id = task->tgroup_home_id;
                tgroup_iterator->tgroup_home_cpu = task->tgroup_home_cpu;
            }
        }
    } while_each_thread(g,tgroup_iterator);
    read_unlock(&tasklist_lock);

    // Pick an id for this remote process request
    PS_SPIN_LOCK(&_clone_request_id_lock);
    lclone_request_id = _clone_request_id++;
    PS_SPIN_UNLOCK(&_clone_request_id_lock);

#if COPY_WHOLE_VM_WITH_MIGRATION
    {
    int dst_has_mm = cpu_has_known_tgroup_mm(dst_cpu);
    if(!dst_has_mm) {
        struct vm_area_struct* curr = NULL;
        // We have to break every cow page before migrating if we're
        // about to move the whole thing.
    restart_break_cow_all:
        curr = task->mm->mmap;
        while(curr) {
            unsigned long addr;
            //unsigned char broken = 0;
            if(is_maybe_cow(curr)) {
                PS_DOWN_WRITE(&task->mm->mmap_sem);
                for(addr = curr->vm_start; addr < curr->vm_end; addr += PAGE_SIZE) {
                    if(break_cow(task->mm,curr,addr)) {
                        //broken = 1;
                    }
                }
                PS_UP_WRITE(&task->mm->mmap_sem);
            }
            //if(broken) 
            //    goto restart_break_cow_all;
            curr = curr->vm_next;
        }
        
        PS_DOWN_READ(&task->mm->mmap_sem);
        curr = task->mm->mmap;

        while(curr) {
            // Only send the vma is either we don't think the 
            // remote cpu has a mm already set up, or if this
            // vma represents the task's stack.
            //if(!dst_has_mm ) {
                send_vma(task->mm,
                         curr,
                         dst_cpu,
                        lclone_request_id);
            //} 
            curr = curr->vm_next;
        }


        PS_UP_READ(&task->mm->mmap_sem);
    }
    }
#endif

    // Build request
    request->header.type = PCN_KMSG_TYPE_PROC_SRV_CLONE_REQUEST;
    request->header.prio = PCN_KMSG_PRIO_NORMAL;
    request->clone_flags = clone_flags;
    request->clone_request_id = lclone_request_id;
    memcpy( &request->regs, regs, sizeof(struct pt_regs) );
    strncpy( request->exe_path, rpath, 512 );
    
    // struct mm_struct -----------------------------------------------------------
    request->stack_start = task->mm->start_stack;
    request->heap_start = task->mm->start_brk;
    request->heap_end = task->mm->brk;
    request->env_start = task->mm->env_start;
    request->env_end = task->mm->env_end;
    request->arg_start = task->mm->arg_start;
    request->arg_end = task->mm->arg_end;
    request->data_start = task->mm->start_data;
    request->data_end = task->mm->end_data;
    request->def_flags = task->mm->def_flags;
    
    // struct task_struct ---------------------------------------------------------    
    request->stack_ptr = stack_start;
    request->placeholder_pid = task->pid;
    request->placeholder_tgid = task->tgid;
    request->tgroup_home_cpu = task->tgroup_home_cpu;
    request->tgroup_home_id = task->tgroup_home_id;
    request->t_home_cpu = task->t_home_cpu;
    request->t_home_id = task->t_home_id;
    request->previous_cpus = task->previous_cpus;
    request->prio = task->prio;
    request->static_prio = task->static_prio;
    request->normal_prio = task->normal_prio;
    request->rt_priority = task->rt_priority;
    request->sched_class = task->policy;
    request->personality = task->personality;
    

    /*mklinux_akshay*/
    if (task->prev_pid == -1)
    	request->origin_pid = task->pid;
    else
    	request->origin_pid = task->origin_pid;
    
    lock_task_sighand(task, &flags);

    request->remote_blocked = task->blocked;
    request->remote_real_blocked = task->real_blocked;
    request->remote_saved_sigmask = task->saved_sigmask;
   // request->remote_pending = task->pending;
    request->sas_ss_sp = task->sas_ss_sp;
    request->sas_ss_size = task->sas_ss_size;
    int cnt = 0;
    for (cnt = 0; cnt < _NSIG; cnt++)
    	request->action[cnt] = task->sighand->action[cnt];

     unlock_task_sighand(task, &flags);


    // socket informationi
    if(request->skt_flag == 1){
    request->skt_level = MIG_BIND;
 
    request->skt_type = skt.type;
    request->skt_state = skt.state;
    request->skt_dport = ((struct inet_sock *)in_)->inet_dport;
    request->skt_sport = ((struct inet_sock *)in_)->inet_sport;
    request->skt_saddr = ((struct inet_sock *)in_)->inet_saddr;
    request->skt_daddr = ((struct inet_sock *)in_)->inet_daddr;
    request->skt_fd = sock_fd;
    }
    // struct thread_struct -------------------------------------------------------
    // have a look at: copy_thread() arch/x86/kernel/process_64.c 
    // have a look at: struct thread_struct arch/x86/include/asm/processor.h
    {
      	unsigned long fs, gs;
	unsigned int fsindex, gsindex;
	unsigned int ds, es;
    unsigned long _usersp;

	if (current != task)
	    PSPRINTK("DAVEK current is different from task!\n");

    request->thread_sp0 = task->thread.sp0;
    request->thread_sp = task->thread.sp;
    //printk("%s: usersp percpu %lx thread %lx fs where it migrated {%lx}\n", __func__, get_percpu_old_rsp(), task->thread.usersp,task->thread.fs);
    // if (percpu_read(old_rsp), task->thread.usersp) set to 0 otherwise copy
    _usersp = get_percpu_old_rsp();
    if (task->thread.usersp != _usersp) {
       // printk("%s: USERSP %lx %lx\n",
         //       __func__, task->thread.usersp, _usersp);
        request->thread_usersp = _usersp;
    } else {
        request->thread_usersp = task->thread.usersp;
    }
    
    request->thread_es = task->thread.es;
    savesegment(es, es);          
    if ((current == task) && (es != request->thread_es))
      PSPRINTK("%s: DAVEK: es %x thread %x\n", __func__, es, request->thread_es);
    request->thread_ds = task->thread.ds;
    savesegment(ds, ds);
    if (ds != request->thread_ds)
      PSPRINTK("%s: DAVEK: ds %x thread %x\n", __func__, ds, request->thread_ds);
    
    // fs register (TLS in user space)    
    request->thread_fsindex = task->thread.fsindex;
    savesegment(fs, fsindex);
    if (fsindex != request->thread_fsindex)
        //printk(KERN_ALERT"%s: fsindex %x (TLS_SEL:%x) thread %x\n", __func__, fsindex, FS_TLS_SEL, request->thread_fsindex);
    request->thread_fs = task->thread.fs;
    rdmsrl(MSR_FS_BASE, fs);
    if (fs != request->thread_fs) {
        request->thread_fs = fs;
        PSPRINTK("%s: DAVEK: fs %lx thread %lx\n", __func__, fs, request->thread_fs);
    }
    
    // gs register (percpu in kernel space)
    request->thread_gsindex = task->thread.gsindex;
    savesegment(gs, gsindex);
    if (gsindex != request->thread_gsindex)
        printk(KERN_WARNING"%s: gsindex %x (TLS_SEL:%x) thread %x\n", __func__, gsindex, GS_TLS_SEL, request->thread_gsindex);
    request->thread_gs = task->thread.gs;
    rdmsrl(MSR_KERNEL_GS_BASE, gs); //NOTE there are two gs base registers in Kernel the used one is MSR_GS_BASE, so MSR_KERNEL_GS_BASE is user space in kernel
    if (gs != request->thread_gs) {
        request->thread_gs = gs;
        PSPRINTK("%s: DAVEK: gs %lx thread %lx\n", __func__, fs, request->thread_gs);
    }

#ifdef FPU_   
 //FPU migration code --- initiator
PSPRINTK(KERN_ERR"%s: task flags %x fpu_counter %x has_fpu %x [%d:%d] %d:%d %x\n",
		        __func__, task->flags, (int)task->fpu_counter, (int)task->thread.has_fpu,
		         (int)__thread_has_fpu(task), (int)fpu_allocated(&task->thread.fpu),
		         (int)use_xsave(), (int)use_fxsr(), (int) PF_USED_MATH);
     request->task_flags = task->flags;
     request->task_fpu_counter = task->fpu_counter;
    request->thread_has_fpu = task->thread.has_fpu;
     if (!fpu_allocated(&task->thread.fpu)) {  
	         printk("%s: !fpu_allocated\n", __func__);
	         request->thread_has_fpu &= (unsigned char)~HAS_FPU_MASK;
	     }
	     else {
		         struct fpu temp; temp.state = &request->fpu_state;      
		         fpu_save_init(&task->thread.fpu);
		         fpu_copy(&temp, &task->thread.fpu);
		         request->thread_has_fpu |= HAS_FPU_MASK;
		     }
//		 printk(KERN_ALERT"%s: flags %x fpu_counter %x has_fpu %x [%d:%d]\n",
//				         __func__, request->task_flags, (int)request->task_fpu_counter,
//				         (int)request->thread_has_fpu, (int)__thread_has_fpu(task), (int)fpu_allocated(&task->thread.fpu));
#endif
    
	// ptrace, debug, dr7: struct perf_event *ptrace_bps[HBP_NUM]; unsigned long debugreg6; unsigned long ptrace_dr7;
    // Fault info: unsigned long cr2; unsigned long trap_no; unsigned long error_code;
    // floating point: struct fpu fpu; THIS IS NEEDED
    // IO permissions: unsigned long *io_bitmap_ptr; unsigned long iopl; unsigned io_bitmap_max;
    }

    // Remember that now, that cpu has a mm for this tgroup
    //set_cpu_has_known_tgroup_mm(dst_cpu);
    spin_lock_irq(&(task->mig_lock));

    // Send request
    DO_UNTIL_SUCCESS(pcn_kmsg_send_long(dst_cpu, 
                        (struct pcn_kmsg_long_message*)request, 
                        sizeof(clone_request_t) - sizeof(request->header)));
    
    task->migration_state = 0;
    task->dest_cpu = dst_cpu;
    spin_unlock_irq(&(task->mig_lock));
    kfree(request);

  //  printk(KERN_ALERT"Migration done\n");
    //dump_task(task,regs,request->stack_ptr);
    
    PERF_MEASURE_STOP(&perf_process_server_do_migration,"migration to new cpu",perf);

    
    __set_task_state(task,TASK_UNINTERRUPTIBLE);
    return PROCESS_SERVER_CLONE_SUCCESS;

}

/**
 * @brief Migrate the specified task to a CPU on which it has previously
 * executed.
 *
 * <MEASURE perf_process_server_do_migration>
 */
int do_migration_back_to_previous_cpu(struct task_struct* task, int cpu) {
    back_migration_t *mig =NULL;
    struct pt_regs* regs = task_pt_regs(task);

    unsigned long _usersp;
    int perf = -1;

    perf = PERF_MEASURE_START(&perf_process_server_do_migration);

    mig = kmalloc(sizeof(back_migration_t), GFP_ATOMIC);
    if (!mig) {
         printk("%s: ERROR kmalloc ret %p\n", __func__, mig);
         return -1;
     }
    // Set up response header

    mig->header.type = PCN_KMSG_TYPE_PROC_SRV_BACK_MIGRATION;
    mig->header.prio = PCN_KMSG_PRIO_NORMAL;

    // Make mark on the list of previous cpus
    set_bit(smp_processor_id(),&task->previous_cpus);

    // Knock this task out.
    __set_task_state(task,TASK_UNINTERRUPTIBLE);
    
    // Update local state
    task->executing_for_remote = 0;
    task->represents_remote = 1;
    task->t_distributed = 1; // This should already be the case
    
    // Build message
    mig->tgroup_home_cpu = task->tgroup_home_cpu;
    mig->tgroup_home_id  = task->tgroup_home_id;
    mig->t_home_cpu      = task->t_home_cpu;
    mig->t_home_id       = task->t_home_id;
    mig->previous_cpus   = task->previous_cpus;
    mig->thread_fs       = task->thread.fs;
    mig->thread_gs       = task->thread.gs;

    _usersp = get_percpu_old_rsp();
if (task->thread.usersp != _usersp) { 
  printk("%s: USERSP %lx %lx\n",
    __func__, task->thread.usersp, _usersp);
  mig->thread_usersp = _usersp;
}else
  mig->thread_usersp = task->thread.usersp;


    mig->thread_es       = task->thread.es;
    mig->thread_ds       = task->thread.ds;
    mig->thread_fsindex  = task->thread.fsindex;
    mig->thread_gsindex  = task->thread.gsindex;

    //FPU support --- initiator (back migration?)

#ifdef FPU_   
  mig->task_flags      = task->flags;
       mig->task_fpu_counter = task->fpu_counter;
       mig->thread_has_fpu   = task->thread.has_fpu;
       if (!fpu_allocated(&task->thread.fpu)) {  
	           printk("%s: !fpu_allocated\n", __func__);
	           mig->thread_has_fpu &= (unsigned char)~HAS_FPU_MASK;
	       }
	       else {
	           struct fpu temp; temp.state = &mig->fpu_state;      
		          fpu_save_init(&task->thread.fpu);
		         fpu_copy(&temp, &task->thread.fpu);
		          mig->thread_has_fpu |= HAS_FPU_MASK;
		      }
		 /*  printk(KERN_ALERT"%s: flags %x fpu_counter %x has_fpu %x [%d:%d]\n",
				           __func__, mig->task_flags, (int)mig->task_fpu_counter,
				           (int)mig->thread_has_fpu, (int)__thread_has_fpu(task), (int)fpu_allocated(&task->thread.fpu));
*/
#endif

    memcpy(&mig->regs, regs, sizeof(struct pt_regs));

    // Send migration request to destination.
    pcn_kmsg_send_long(cpu,
                       (struct pcn_kmsg_long_message*)mig,
                       sizeof(back_migration_t) - sizeof(struct pcn_kmsg_hdr));

    pcn_kmsg_free_msg(mig);
    PERF_MEASURE_STOP(&perf_process_server_do_migration,"back migration",perf);
 

    return PROCESS_SERVER_CLONE_SUCCESS;
}

/**
 * @brief Migrate the specified task <task> to cpu <cpu>
 * This function will put the specified task to 
 * sleep, and push its info over to the remote cpu.  The
 * remote cpu will then create a new process and import that
 * info into its new context.  
 *
 * Note: There are now two different migration implementations.
 *       One is for the case where we are migrating to a cpu that
 *       has never before hosted this thread.  The other is where
 *       we are migrating to a cpu that has hosted this thread
 *       before.  There's a lot of stuff that we do not need
 *       to do when the thread has been there before, and the
 *       messaging data requirements are much less for that case.
 *
 */
int process_server_do_migration(struct task_struct* task, int cpu) {
   
    int ret = 0;

#ifndef SUPPORT_FOR_CLUSTERING
    printk(KERN_ALERT"%s: normal migration {%d}\n",__func__,cpu);
    if(test_bit(cpu,&task->previous_cpus)) {
        ret = do_migration_back_to_previous_cpu(task,cpu);
    } else {
        ret = do_migration_to_new_cpu(task,cpu);
    }
#else
    if (cpumask_test_cpu(cpu, cpu_present_mask)) {
        printk(KERN_ERR"%s: called but task %p does not require inter-kernel migration"
		       "(cpu: %d present_mask)\n", __func__, task, cpu);
        return -EBUSY;
    }
    //printk(KERN_ALERT"%s: clustering activated\n",__func__);
    // TODO seems like that David is using previous_cpus as a bitmask.. 
    // TODO well this must be upgraded to a cpumask, declared as usigned long in task_struct
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    struct cpumask *pcpum =0;
    int cpuid=-1;
extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        cpuid = objPtr->_data._processor;
        pcpum = &(objPtr->_data._cpumask);
	if (cpumask_test_cpu(cpu, pcpum)) {
		if ( bitmap_intersects(cpumask_bits(pcpum),
				       &(task->previous_cpus),
				       (sizeof(unsigned long)*8)) )
	            ret = do_migration_back_to_previous_cpu(task,cpuid);
		else
		    ret = do_migration_to_new_cpu(task,cpuid);
	}
    }
#endif
    return ret;
}

/**
 * @brief Handles a task's return disposition.  When a task is re-awoken
 * when it either migrates back to this CPU, or exits, this function
 * implements the actions that must be made immediately after
 * the newly awoken task resumes execution.
 */
void process_server_do_return_disposition(void) {

    int return_disposition = current->return_disposition;

    //printk("%s disp{%d} \n",__func__,return_disposition);
    // Reset the return disposition
    current->return_disposition = RETURN_DISPOSITION_NONE;

    switch(return_disposition) {
    case RETURN_DISPOSITION_NONE:
        printk("%s: ERROR, return disposition is none!\n",__func__);
        break;   
    case RETURN_DISPOSITION_MIGRATE:
        // Nothing to do, already back-imported the
        // state in process_back_migration.  This will
        // remain a stub until something needs to occur
        // here.
        PSPRINTK("%s: return disposition migrate\n",__func__);
        break;
    case RETURN_DISPOSITION_EXIT:
        PSPRINTK("%s: return disposition exit\n",__func__);
    default:
        do_exit(0);
        break;
    }

    return;
}

/**
 *
 */
void wait_for_all_lamport_lock_acquisition(lamport_barrier_queue_t* queue,
                                           lamport_barrier_entry_t* entry) {
    data_header_t* curr = NULL;
    lamport_barrier_queue_t* queue_curr = NULL;
    int done = 0;
    PSPRINTK("%s: ts{%lx}\n",__func__,entry->timestamp);
    PSPRINTK("%s: Starting queues-\n",__func__);
    dump_all_lamport_queues();
    while(!done) {
        done = 1;
        PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
        // look through every queue for this thread group
        curr = (data_header_t*)_lamport_barrier_queue_head;
        while(curr) {
            queue_curr = (lamport_barrier_queue_t*) curr;
            if(queue_curr->tgroup_home_cpu == queue->tgroup_home_cpu &&
               queue_curr->tgroup_home_id  == queue->tgroup_home_id) {
                
                // if we don't have the lock, spin again.
                if(queue_curr->queue) {
                    if(queue_curr->queue->timestamp != entry->timestamp) {
                        done = 0;
                        break;
                    }
                }

            }
            curr = curr->next;
        }

        PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);
        if(!done)
            schedule();
    }
    PSPRINTK("%s: Ending queues-\n",__func__);
    dump_all_lamport_queues();
    PSPRINTK("%s: exiting ts{%llx}\n",__func__,entry->timestamp);
}

/**
 * _lamport_barrier_queue_lock must NOT already be held.
 */
void wait_for_lamport_lock_acquisition(lamport_barrier_queue_t* queue,
                                       lamport_barrier_entry_t* entry) {
    // Wait until "entry" is at the front of the queue
    PSPRINTK("%s: ts{%llx}\n",__func__,entry->timestamp);
    while(1) {
        PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
        if(entry == queue->queue) {
            queue->active_timestamp = entry->timestamp;
            PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);
            goto lock_acquired;
        }
        PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);
        schedule();
    } 
lock_acquired:

    if(queue->is_heavy) {
        wait_for_all_lamport_lock_acquisition(queue,entry);
    }

    PSPRINTK("%s: exiting ts{%llx}\n",__func__,entry->timestamp);
    return;
}

/**
 * _lamport_barrier_queue_lock must NOT already be held.
 */
void wait_for_all_lamport_request_responses(lamport_barrier_entry_t* entry) {
    PSPRINTK("%s: ts{%llx}\n",__func__,entry->timestamp);
    while(1) {
        PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
        if(entry->expected_responses == entry->responses) {
            PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);
            goto responses_acquired;
        }
        PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);
        schedule();
    }
responses_acquired:
    PSPRINTK("%s: exiting ts{%llx}\n",__func__,entry->timestamp);
    return;
}

/**
 * 
 */
void add_entry_to_lamport_queue_light(unsigned long address, 
                                      unsigned long long ts,
                                      lamport_barrier_entry_t** entry,
                                      lamport_barrier_queue_t** queue) {
    lamport_barrier_queue_t* heavy_queue = NULL;

    PSPRINTK("%s: addr{%lx},ts{%llx}\n",__func__,address,ts);

    *entry = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);

    // form record and place in queue
    (*entry)->timestamp = ts;
    (*entry)->responses = 0;
    (*entry)->expected_responses = 0;
    (*entry)->allow_responses = 0;
    (*entry)->is_heavy = 0;
    (*entry)->cpu = _cpu;

    // find queue if it exists
    *queue = find_lamport_barrier_queue(current->tgroup_home_cpu,
                                     current->tgroup_home_id,
                                     address,
                                     0);
    // If no queue exists, create one
    if(!*queue) {
        *queue = kmalloc(sizeof(lamport_barrier_queue_t),GFP_ATOMIC);
        (*queue)->tgroup_home_cpu = current->tgroup_home_cpu;
        (*queue)->tgroup_home_id  = current->tgroup_home_id;
        (*queue)->address = address;
        (*queue)->is_heavy = 0;
        PSPRINTK("%s: Setting active_timestamp to 0\n",__func__);
        (*queue)->active_timestamp = 0;
        (*queue)->queue = NULL;
        add_data_entry_to(*queue,NULL,&_lamport_barrier_queue_head);

        // Add all heavy entries to this queue
        heavy_queue = find_lamport_barrier_queue(current->tgroup_home_cpu,
                                                 current->tgroup_home_id,
                                                 0,
                                                 1);
        if(heavy_queue) {
            lamport_barrier_entry_t* curr = heavy_queue->queue;
            PSPRINTK("%s: found heavy queue\n",__func__);
            while(curr) {
                lamport_barrier_entry_t* e = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
                PSPRINTK("%s: adding entry from heavy queue to queue(addr{%lx}) ts{%llx}\n",
                        __func__,address,curr->timestamp);
                e->timestamp = curr->timestamp;
                e->responses = 0;
                e->expected_responses = 0;
                e->allow_responses = 0;
                e->is_heavy = 1;
                e->cpu = curr->cpu;
                
                add_fault_entry_to_queue(e,*queue);

                if((*queue)->queue == e) {
                    PSPRINTK("%s: new entry is not at the front of the queue\n",
                            __func__);
                    PSPRINTK("%s: setting active timestamp to %llx\n",
                            __func__,e->timestamp);
                    (*queue)->active_timestamp = e->timestamp;
                }
               

                curr = (lamport_barrier_entry_t*)curr->header.next;
            }
        }
    } 

    // Add entry to queue
    add_fault_entry_to_queue(*entry,*queue);

    dump_lamport_queue(*queue);

}

static void add_entry_to_lamport_queue_heavy(unsigned long long ts,
                                      lamport_barrier_entry_t** entry,
                                      lamport_barrier_queue_t** queue) {

    data_header_t* curr = NULL;
    lamport_barrier_queue_t* queue_curr = NULL;

    PSPRINTK("%s: ts{%llx}\n",__func__,ts);

    *entry = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);

    // form record and place in queue
    (*entry)->timestamp = ts;
    (*entry)->responses = 0;
    (*entry)->expected_responses = 0;
    (*entry)->allow_responses = 0;
    (*entry)->is_heavy = 1;
    (*entry)->cpu = _cpu;

    // find queue if it exists
    *queue = find_lamport_barrier_queue(current->tgroup_home_cpu,
                                        current->tgroup_home_id,
                                        0,
                                        1);
    // If no queue exists, create one
    if(!*queue) {
        PSPRINTK("%s: adding heavy queue\n",__func__);
        *queue = kmalloc(sizeof(lamport_barrier_queue_t),GFP_ATOMIC);
        (*queue)->tgroup_home_cpu = current->tgroup_home_cpu;
        (*queue)->tgroup_home_id  = current->tgroup_home_id;
        (*queue)->address = 0;
        (*queue)->is_heavy = 1;
        PSPRINTK("%s: Setting active_timestamp to 0\n",__func__);
        (*queue)->active_timestamp = 0;
        (*queue)->queue = NULL;
        add_data_entry_to(*queue,NULL,&_lamport_barrier_queue_head);
    } 

    // Add entry to queue
    add_fault_entry_to_queue(*entry,*queue);

    // Add entry to all existing non-heavy queues for this thread group
    curr = (data_header_t*)_lamport_barrier_queue_head; 
    while(curr) {
        queue_curr = (lamport_barrier_queue_t*) curr;
        if(queue_curr->tgroup_home_cpu == current->tgroup_home_cpu &&
           queue_curr->tgroup_home_id  == current->tgroup_home_id) {

            if(!queue_curr->is_heavy) {
                lamport_barrier_entry_t* e = kmalloc(sizeof(lamport_barrier_entry_t),GFP_ATOMIC);
                PSPRINTK("%s: adding entry to non heavy queue addr{%lx}\n",
                        __func__,queue_curr->address);
                e->timestamp = ts;
                e->responses = 0;
                e->expected_responses = 0;
                e->allow_responses = 0;
                e->is_heavy = 1;
                e->cpu = _cpu;

                add_fault_entry_to_queue(e,queue_curr);

                if(queue_curr->queue == e) {
                    PSPRINTK("%s: new entry is not at the front of the queue\n",
                            __func__);
                    PSPRINTK("%s: setting active timestamp to %llx\n",
                            __func__,e->timestamp);
                    queue_curr->active_timestamp = e->timestamp;
                }

                PSPRINTK("Modified non heavy queue-\n");
                dump_lamport_queue(queue_curr);
            }
            
        }
        curr = curr->next;
    }
    PSPRINTK("%s: exiting\n",__func__);
}

/**
 * _lamport_barrier_queue_lock must already be held.
 */
static void add_entry_to_lamport_queue(unsigned long address, 
                                unsigned long long ts,
                                lamport_barrier_entry_t** entry,
                                lamport_barrier_queue_t** queue,
                                int is_heavy) {
    if(is_heavy) {
        add_entry_to_lamport_queue_heavy(ts,entry,queue);
    } else {
        add_entry_to_lamport_queue_light(address,ts,entry,queue);
    }
}


/**
 *
 */
static int process_server_acquire_page_lock_range_maybeheavy(unsigned long address,size_t sz, int is_heavy) {
    lamport_barrier_request_range_t* request = NULL;
    lamport_barrier_entry_t** entry_list = NULL;
    lamport_barrier_queue_t** queue_list = NULL;
    int i,s;
    unsigned long addr;
    int index;
    int page_count = sz / PAGE_SIZE;
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    unsigned long long end_time = 0;
    unsigned long long total_time = 0;
    unsigned long long start_time = native_read_tsc();
#endif

    if(!current->tgroup_distributed) return 0;

    PSPRINTK("%s: addr{%lx},sz{%d},is_heavy{%d}\n",__func__,address,sz,is_heavy);

    BUG_ON(is_heavy && (sz > PAGE_SIZE));

    entry_list = kmalloc(sizeof(lamport_barrier_entry_t*)*page_count,GFP_KERNEL);
    queue_list = kmalloc(sizeof(lamport_barrier_queue_t*)*page_count,GFP_KERNEL);
    request = kmalloc(sizeof(lamport_barrier_request_range_t), GFP_KERNEL);
  
    BUG_ON(!request);
    BUG_ON(!entry_list);
    BUG_ON(!queue_list);

    address &= PAGE_MASK;
    request->header.type = PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_REQUEST_RANGE;
    request->header.prio = PCN_KMSG_PRIO_NORMAL;
    request->address = address;
    request->is_heavy = is_heavy;
    request->sz = sz;
    request->tgroup_home_cpu = current->tgroup_home_cpu;
    request->tgroup_home_id =  current->tgroup_home_id;

    // Grab the fault barrier queue lock
    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    
    // create timestamp
    request->timestamp = get_next_ts_value(); /*native_read_tsc();*/

    if(!is_heavy) {
        index = 0;
        for(addr = address; addr < address + sz; addr += PAGE_SIZE) {
            add_entry_to_lamport_queue(addr,
                                       request->timestamp,
                                       &(entry_list[index]),
                                       &(queue_list[index]),
                                       0);
            index++;
        }
    } else {
        add_entry_to_lamport_queue(0,
                                   request->timestamp,
                                   &(entry_list[0]),
                                   &(queue_list[0]),
                                   1);
    }

    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    // Send out request to everybody
    for(i = 0; i < NR_CPUS; i++) {
        if(i == _cpu) continue;
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)request);
        if(!s) {
            for(index = 0; index < page_count; index++) 
                entry_list[index]->expected_responses++;
        }
    }

    mb();

    kfree(request);

    for(index = 0; index < page_count; index++)
        wait_for_all_lamport_request_responses(entry_list[index]);

    for(index = 0; index < page_count; index++)
        wait_for_lamport_lock_acquisition(queue_list[index],entry_list[index]);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    end_time = native_read_tsc();
    for(index = 0; index < page_count; index++)
        entry_list[index]->lock_acquired = end_time;
    total_time = end_time - start_time;
    PS_PROC_DATA_TRACK(PS_PROC_DATA_WAITING_FOR_LAMPORT_LOCK,total_time);
#endif

    kfree(entry_list);
    kfree(queue_list);

    PSPRINTK("%s: exiting\n",__func__);

    return 0;
}


int process_server_acquire_page_lock_range(unsigned long address,size_t sz) {
    return process_server_acquire_page_lock_range_maybeheavy(address,sz,0);
}

/**
 *
 */
int process_server_acquire_page_lock(unsigned long address) {
    return process_server_acquire_page_lock_range(address,PAGE_SIZE);
}

/**
 *
 */
int process_server_acquire_heavy_lock() {
    return process_server_acquire_page_lock_range_maybeheavy(0,PAGE_SIZE,1);
}

/**
 *
 */
int process_server_acquire_distributed_mm_lock() {
    return process_server_acquire_page_lock_range(0,PAGE_SIZE);
}

/**
 *
 */
static void release_local_lamport_lock_light(unsigned long address,
                                      unsigned long long* timestamp) {
    lamport_barrier_queue_t* queue = NULL;
    lamport_barrier_entry_t* entry = NULL;
    *timestamp = 0;
    // find queue
    queue = find_lamport_barrier_queue(current->tgroup_home_cpu,
                                     current->tgroup_home_id,
                                     address,
                                     0);

    //BUG_ON(!queue);

    if(queue) {

        BUG_ON(!queue->queue);
        BUG_ON(queue->queue->cpu != _cpu);
        
        entry = queue->queue;
        
        //BUG_ON(entry->timestamp != queue->active_timestamp);
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        entry->lock_released = native_read_tsc();
        PS_PROC_DATA_TRACK(PS_PROC_DATA_LAMPORT_LOCK_HELD,
                                entry->lock_released - entry->lock_acquired);
#endif
        *timestamp = entry->timestamp;
        PSPRINTK("%s: Setting active_timestamp to 0\n",__func__);
        queue->active_timestamp = 0;
        
        // remove entry from queue
        remove_data_entry_from((data_header_t*)entry,(data_header_t**)&queue->queue);

        kfree(entry); // this is OK, because kfree never sleeps

        // garbage collect the queue if necessary
        if(!queue->queue) {
            remove_data_entry_from(queue,&_lamport_barrier_queue_head);
            kfree(queue);
        }
    
    }

}

/**
 *
 */
static void release_local_lamport_lock_heavy(unsigned long long* timestamp) {
    data_header_t* curr = _lamport_barrier_queue_head;
    data_header_t* next = NULL;

    PSPRINTK("%s\n",__func__);

    while(curr) {
        lamport_barrier_queue_t* queue = (lamport_barrier_queue_t*)curr;
        lamport_barrier_entry_t* entry = NULL;
        next = curr->next;
        
        if(queue->tgroup_home_cpu != current->tgroup_home_cpu ||
           queue->tgroup_home_id  != current->tgroup_home_id) {
            curr = next;
            continue;
        }


        BUG_ON(!queue->queue);
        
        entry = queue->queue;

        BUG_ON(!entry);
        BUG_ON(!entry->is_heavy);

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
        entry->lock_released = native_read_tsc();
        PS_PROC_DATA_TRACK(PS_PROC_DATA_LAMPORT_LOCK_HELD,
                                entry->lock_released - entry->lock_acquired);
#endif

        *timestamp = entry->timestamp;
        PSPRINTK("%s: Setting active timestamp to 0\n",__func__);
        queue->active_timestamp = 0;
      
        PSPRINTK("%s: Removing heavy entry ts{%llx},cpu{%d},heavy{%d},addr{%ls}\n",
                __func__,
                entry->timestamp,
                entry->cpu,
                entry->is_heavy,
                queue->address);

        // remove entry from queue
        remove_data_entry_from((data_header_t*)entry,(data_header_t**)&queue->queue);

        kfree(entry); // this is OK, because kfree never sleeps

        // garbage collect the queue if necessary
        if(!queue->queue) {
            PSPRINTK("%s: Removing queue is_heavy{%d}\n",__func__,queue->is_heavy);
            remove_data_entry_from(queue,&_lamport_barrier_queue_head);
            kfree(queue);
        }
        
        curr = next;
    }
}

/**
 *
 */
static void release_local_lamport_lock(unsigned long address,
                                unsigned long long* timestamp,
                                int is_heavy) {
    if(0 != is_heavy) {
        release_local_lamport_lock_heavy(timestamp);
    } else {
        release_local_lamport_lock_light(address,timestamp);
    }
}


/**
 *
 */
void process_server_release_page_lock_range_maybeheavy(unsigned long address,size_t sz, int is_heavy) {
    lamport_barrier_release_range_t* release = NULL;
    int i;
    int index;
    unsigned long long timestamp = 0;
    unsigned long long tmp_ts = 0;
    int page_count = sz / PAGE_SIZE;

    BUG_ON(is_heavy && (sz > PAGE_SIZE));

    if(!current->tgroup_distributed) return;

    PSPRINTK("%s: addr{%lx},sz{%d},is_heavy{%d}\n",__func__,address,sz,is_heavy);

    address &= PAGE_MASK;
    release = kmalloc(sizeof(lamport_barrier_release_range_t),
                        GFP_KERNEL);

    PS_SPIN_LOCK(&_lamport_barrier_queue_lock);
    
    if(is_heavy) {
        release_local_lamport_lock(0,&tmp_ts,1);
        if(!timestamp && tmp_ts) timestamp = tmp_ts;
    } else {
        for(index = 0; index < page_count; index++) {
            release_local_lamport_lock(address + (index*PAGE_SIZE),
                                       &tmp_ts,
                                       0);
            if(!timestamp && tmp_ts) timestamp = tmp_ts;
        }
    }

    PS_SPIN_UNLOCK(&_lamport_barrier_queue_lock);

    // Send release
    release->header.type = PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RELEASE_RANGE;
    release->header.prio = PCN_KMSG_PRIO_NORMAL;
    release->tgroup_home_cpu = current->tgroup_home_cpu;
    release->tgroup_home_id  = current->tgroup_home_id;
    release->is_heavy = is_heavy;
    release->timestamp = timestamp;
    release->address = address;
    release->sz = sz;
    for(i = 0; i < NR_CPUS; i++) {
        if(i == _cpu) continue;
        pcn_kmsg_send(i,(struct pcn_kmsg_message*)release);
    }

    kfree(release);

    PSPRINTK("%s: exiting\n",__func__);
}

/**
 *
 */
void process_server_release_page_lock_range(unsigned long address,size_t sz) {
    process_server_release_page_lock_range_maybeheavy(address,sz,0);
}

/**
 *
 */
void process_server_release_page_lock(unsigned long address) {
    process_server_release_page_lock_range(address,PAGE_SIZE);
}

/**
 *
 */
void process_server_release_heavy_lock() {
    process_server_release_page_lock_range_maybeheavy(0,PAGE_SIZE,1);
}

/**
 *  
 */
void process_server_release_distributed_mm_lock() {
    process_server_release_page_lock_range(0,PAGE_SIZE);
}

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static void proc_data_reset(int cpu,int entry) {
    if(entry >= PS_PROC_DATA_MAX) {
        printk("Invalid proc_data_reset entry %d\n",entry);
        return;
    }
    _proc_data[cpu][entry].total = 0;
    _proc_data[cpu][entry].count = 0;
    _proc_data[cpu][entry].min = 0;
    _proc_data[cpu][entry].max = 0;
   
}
#endif

/**
 *
 */
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static int proc_read(char* buf, char**start, off_t off, int count,
                        int *eof, void*d) {
    char* p = buf;
    int i,j,s;
    stats_query_t query;
    stats_query_data_t data;

    sprintf(buf,"See dmesg\n");

    query.header.prio = PCN_KMSG_PRIO_NORMAL;
    query.header.type = PCN_KMSG_TYPE_PROC_SRV_STATS_QUERY;
    query.pid = current->pid;
    data.pid = current->pid;
    data.header.data_type = PROCESS_SERVER_STATS_DATA_TYPE;
    data.expected_responses = 0;
    data.responses = 0;

    add_data_entry(&data);

    // Update all the data
#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;

#endif
        s = pcn_kmsg_send(i,(struct pcn_kmsg_message*)(&query));
        if(!s) {
            data.expected_responses++;
        }
    }

    while(data.expected_responses != data.responses) {
        schedule();
    }

    spin_lock(&_data_head_lock);
    remove_data_entry(&data);
    spin_unlock(&_data_head_lock);

    printk("Process Server Data\n");
    for(i = 0; i < PS_PROC_DATA_MAX; i++) {
        printk("%s[Tot,Cnt,Max,Min,Avg]:\n",_proc_data[_cpu][i].name);
        for(j = 0; j < NR_CPUS; j++) {
            if(_proc_data[j][i].count) {
                unsigned long long avg = 0;
                if(_proc_data[j][i].count)
                    avg = _proc_data[j][i].total / _proc_data[j][i].count;
                printk("\tcpu{%d}[%llx,%d,%llx,%llx,%llx]\n",
                                j,
                                _proc_data[j][i].total,
                                _proc_data[j][i].count,
                                _proc_data[j][i].max,
                                _proc_data[j][i].min,
                                avg);
            }
        }
    }
    return strlen(buf);
}           
#endif


#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static void proc_track_data(int entry, unsigned long long time) {
    if(entry >= PS_PROC_DATA_MAX) {
        printk("Invalid proc_track_data entry %d\n",entry);
        return;
    }
    _proc_data[_cpu][entry].total += time;
    _proc_data[_cpu][entry].count++;
    if(_proc_data[_cpu][entry].min == 0 || time < _proc_data[_cpu][entry].min)
        _proc_data[_cpu][entry].min = time;
    if(time > _proc_data[_cpu][entry].max)
        _proc_data[_cpu][entry].max = time;
}
#endif



/**
 *
 */      
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static int proc_write(struct file* file,
                        const char* buffer,
                        unsigned long count,
                        void* data) {
    int i;
    int j;
    stats_clear_t msg;
    msg.header.type = PCN_KMSG_TYPE_PROC_SRV_STATS_CLEAR;
    msg.header.prio = PCN_KMSG_PRIO_NORMAL;

    for(j = 0; j < NR_CPUS; j++)
        for(i = 0; i < PS_PROC_DATA_MAX; i++)
            proc_data_reset(j,i);

#ifndef SUPPORT_FOR_CLUSTERING
    for(i = 0; i < NR_CPUS; i++) {
        if(i == _cpu) continue;
#else
    // the list does not include the current processor group descirptor (TODO)
    struct list_head *iter;
    _remote_cpu_info_list_t *objPtr;
    extern struct list_head rlist_head;
    list_for_each(iter, &rlist_head) {
        objPtr = list_entry(iter, _remote_cpu_info_list_t, cpu_list_member);
        i = objPtr->_data._processor;

#endif
        pcn_kmsg_send(i,(struct pcn_kmsg_message*)&msg);
    }


    return count;
} 
#endif

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static void proc_data_init(void) {
    int i;
    int j;
    _proc_entry = create_proc_entry("procsrv",666,NULL);
    _proc_entry->read_proc = proc_read;
    _proc_entry->write_proc = proc_write;

    for(j = 0; j < NR_CPUS; j++)
        for(i = 0; i < PS_PROC_DATA_MAX; i++)
            proc_data_reset(j,i);

    for(j = 0; j < NR_CPUS; j++) {
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_WAIT_TIME].name,
                "Mapping wait time");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_POST_WAIT_TIME_RESUME].name,
                "Time after all mapping responses are in and when the fault handler resumes");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_REQUEST_SEND_TIME].name,
                "Mapping request send time");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_RESPONSE_SEND_TIME].name,
                "Mapping response send time");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_REQUEST_DELIVERY_TIME].name,
                "Mapping request delivery time");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_RESPONSE_DELIVERY_TIME].name,
                "Mapping response delivery time");
        sprintf(_proc_data[j][PS_PROC_DATA_BREAK_COW_TIME].name,
                "Break cow time");
        sprintf(_proc_data[j][PS_PROC_DATA_MAPPING_REQUEST_PROCESSING_TIME].name,
                "Mapping request processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_FAULT_PROCESSING_TIME].name,
                "Fault processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_ADJUSTED_PERMISSIONS].name,
                "Adjusted permissions fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_NEWVMA_ANONYMOUS_PTE].name,
                "Newvma anonymous pte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_NEWVMA_ANONYMOUS_NOPTE].name,
                "Newvma anonymous nopte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_NEWVMA_FILEBACKED_PTE].name,
                "Newvma filebacked pte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_NEWVMA_FILEBACKED_NOPTE].name,
                "Newvma filebacked nopte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_OLDVMA_ANONYMOUS_PTE].name,
                "Oldvma anonymous pte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_OLDVMA_ANONYMOUS_NOPTE].name,
                "Oldvma anonymous nopte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_OLDVMA_FILEBACKED_PTE].name,
                "Oldvma filebacked pte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_OLDVMA_FILEBACKED_NOPTE].name,
                "Oldvma filebacked nopte fault time");
        sprintf(_proc_data[j][PS_PROC_DATA_MUNMAP_PROCESSING_TIME].name,
                "Munmap processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_MUNMAP_REQUEST_PROCESSING_TIME].name,
                "Munmap request processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_MPROTECT_PROCESSING_TIME].name,
                "Mprotect processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_MPROTECT_REQUEST_PROCESSING_TIME].name,
                "Mprotect request processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_EXIT_PROCESSING_TIME].name,
                "Exit processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_EXIT_NOTIFICATION_PROCESSING_TIME].name,
                "Exit notification processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_GROUP_EXIT_PROCESSING_TIME].name,
                "Group exit processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_GROUP_EXIT_NOTIFICATION_PROCESSING_TIME].name,
                "Group exit notification processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_IMPORT_TASK_TIME].name,
                "Import migrated task information time");
        sprintf(_proc_data[j][PS_PROC_DATA_COUNT_REMOTE_THREADS_PROCESSING_TIME].name,
                "Count remote threads processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_MK_PAGE_WRITABLE].name,
                "Make page writable processing time");
        sprintf(_proc_data[j][PS_PROC_DATA_WAITING_FOR_LAMPORT_LOCK].name,
                "Waiting for Lamport lock on virtual page");
        sprintf(_proc_data[j][PS_PROC_DATA_LAMPORT_LOCK_HELD].name,
                "Lamport lock held");
    }
}
#endif

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static int handle_stats_clear(struct pcn_kmsg_message* inc_msg) {

    int i,j;
    for(j = 0; j < NR_CPUS; j++)
        for(i = 0; i < PS_PROC_DATA_MAX; i++)
            proc_data_reset(j,i);
    pcn_kmsg_free_msg(inc_msg);
    return 0;
}
#endif

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static void process_stats_query(struct work_struct* w) {
    stats_response_t* response = kmalloc(sizeof(stats_response_t),GFP_KERNEL);
    int i;
    stats_query_work_t* work = (stats_query_work_t*)w;
    response->header.type = PCN_KMSG_TYPE_PROC_SRV_STATS_RESPONSE;
    response->header.prio = PCN_KMSG_PRIO_NORMAL;
    response->pid = work->pid;
    for(i = 0; i < PS_PROC_DATA_MAX; i++) { 
        response->data[i].count = _proc_data[_cpu][i].count;
        response->data[i].total = _proc_data[_cpu][i].total;
        response->data[i].min   = _proc_data[_cpu][i].min;
        response->data[i].max   = _proc_data[_cpu][i].max;
    }
    pcn_kmsg_send_long(work->from_cpu,
                        (struct pcn_kmsg_long_message*)response,
                        sizeof(stats_response_t) - sizeof(response->header));
    kfree(response);
    kfree(w);
}
#endif

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static int handle_stats_query(struct pcn_kmsg_message* inc_msg) {
    stats_query_t* query = (stats_query_t*)inc_msg;
    stats_query_work_t* work = kmalloc(sizeof(stats_query_work_t),GFP_ATOMIC);

    if(work) {
        INIT_WORK( (struct work_struct*)work, process_stats_query);
        work->pid = query->pid;
        work->from_cpu = query->header.from_cpu;
        queue_work(exit_wq, (struct work_struct*)work);
    }

    pcn_kmsg_free_msg(inc_msg);
    return 0;
}
#endif

#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
static int handle_stats_response(struct pcn_kmsg_message* inc_msg) {
    stats_response_t* response = (stats_response_t*)inc_msg;
    stats_query_data_t* data = find_stats_query_data(response->pid);
    int from_cpu = response->header.from_cpu;
    if(data) {
        int i;
        for(i = 0; i < PS_PROC_DATA_MAX; i++) {
            _proc_data[from_cpu][i].count = response->data[i].count;
            _proc_data[from_cpu][i].total = response->data[i].total;
            _proc_data[from_cpu][i].min   = response->data[i].min;
            _proc_data[from_cpu][i].max   = response->data[i].max;
        }

        data->responses++;
    }
    pcn_kmsg_free_msg(inc_msg);
    return 0;
}
#endif

/* From Wikipedia page "Fetch and add", modified to work for u64 */
/**
 *
 */
static inline unsigned long fetch_and_add(volatile unsigned long * variable, 
                      unsigned long value) {
    asm volatile( 
             "lock; xaddq %%rax, %2;"
             :"=a" (value)                   //Output
             : "a" (value), "m" (*variable)  //Input
             :"memory" );
    return value;
}

/**
 *
 */
static unsigned long get_next_ts_value() {
    return fetch_and_add(ts_counter,1);
}

/**
 *
 */
static unsigned long long* get_master_ts_counter_address() {
    get_counter_phys_request_t request;
    request.header.type = PCN_KMSG_TYPE_PROC_SRV_GET_COUNTER_PHYS_REQUEST;
    request.header.prio = PCN_KMSG_PRIO_NORMAL;

    if(!get_counter_phys_data)
        get_counter_phys_data = kmalloc(sizeof(get_counter_phys_data_t),GFP_KERNEL);

    get_counter_phys_data->resp = 0;
    get_counter_phys_data->response_received = 0;

    pcn_kmsg_send(0,(struct pcn_kmsg_message*)&request);

    while(!get_counter_phys_data->response_received)
        schedule();
     
    return (unsigned long long*)get_counter_phys_data->resp;
}

/**
 *
 */
static void init_shared_counter(void) {
    if(!_cpu) {
        // Master allocs space, then shares it
        void* pg = kmalloc(PAGE_SIZE,GFP_KERNEL);
        ts_counter = pg;
        *ts_counter = 0;
        get_next_ts_value();
        printk("%s: ts_counter{%lx},*ts_counter{%lx}\n",__func__,
                ts_counter,
                get_next_ts_value());
    } else {
        // ask for physical address of master's ts_counter
        ts_counter = ioremap_cache(get_master_ts_counter_address(), PAGE_SIZE);
        printk("%s: ts_counter{%lx},*ts_counter{%lx}\n",__func__,
                ts_counter,
                get_next_ts_value());
    }
}

/**
 * @brief Initialize this module
 */
static int __init process_server_init(void) {

    /*
     * Cache some local information.
     */
//#ifndef SUPPORT_FOR_CLUSTERING

//           _cpu= smp_processor_id();
//#else
	   _cpu = cpumask_first(cpu_present_mask);
//#endif
		sema_init(&wake_load_banlancer,0);		

	//snull_init_module();

    /*
     * Init global semaphores
     */
    init_rwsem(&_import_sem);

    /*
     * Create work queues so that we can do bottom side
     * processing on data that was brought in by the
     * communications module interrupt handlers.
     */
    clone_wq   = create_workqueue("clone_wq");
    exit_wq    = create_workqueue("exit_wq");
    mapping_wq = create_workqueue("mapping_wq");

    /*
     * Proc entry to publish information
     */
    PS_PROC_DATA_INIT();

    /*
     * Register to receive relevant incomming messages.
     */
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_PTE_TRANSFER, 
            handle_pte_transfer);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_VMA_TRANSFER, 
            handle_vma_transfer);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_EXIT_PROCESS, 
            handle_exiting_process_notification);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_EXIT_GROUP,
            handle_exit_group);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_CREATE_PROCESS_PAIRING, 
            handle_process_pairing_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_CLONE_REQUEST, 
            handle_clone_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MAPPING_REQUEST,
            handle_mapping_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MAPPING_RESPONSE,
            handle_mapping_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MAPPING_RESPONSE_NONPRESENT,
            handle_nonpresent_mapping_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MUNMAP_REQUEST,
            handle_munmap_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MUNMAP_RESPONSE,
            handle_munmap_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_THREAD_COUNT_REQUEST,
            handle_remote_thread_count_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_THREAD_COUNT_RESPONSE,
            handle_remote_thread_count_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_THREAD_GROUP_EXITED_NOTIFICATION,
            handle_thread_group_exited_notification);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MPROTECT_REQUEST,
            handle_mprotect_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_MPROTECT_RESPONSE,
            handle_mprotect_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_BACK_MIGRATION,
            handle_back_migration);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_REQUEST,
            handle_lamport_barrier_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RESPONSE,
            handle_lamport_barrier_response);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RELEASE,
            handle_lamport_barrier_release);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_REQUEST_RANGE,
            handle_lamport_barrier_request_range);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RESPONSE_RANGE,
            handle_lamport_barrier_response_range);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_LAMPORT_BARRIER_RELEASE_RANGE,
            handle_lamport_barrier_release_range);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_GET_COUNTER_PHYS_REQUEST,
            handle_get_counter_phys_request);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_GET_COUNTER_PHYS_RESPONSE,
            handle_get_counter_phys_response);

    // stats messages
#ifdef PROCESS_SERVER_HOST_PROC_ENTRY
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_STATS_CLEAR,
            handle_stats_clear);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_STATS_QUERY,
            handle_stats_query);
    pcn_kmsg_register_callback(PCN_KMSG_TYPE_PROC_SRV_STATS_RESPONSE,
            handle_stats_response);
#endif

    /*
     *  
     */
   init_shared_counter(); 

    PERF_INIT(); 
    return 0;
}

/**
 * Register process server init function as
 * module initialization function.
 */
late_initcall(process_server_init);




