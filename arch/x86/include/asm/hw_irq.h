#ifndef _ASM_X86_HW_IRQ_H
#define _ASM_X86_HW_IRQ_H

/*
 * (C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 * moved some of the old arch/i386/kernel/irq.h to here. VY
 *
 * IRQ/IPI changes taken from work by Thomas Radke
 * <tomsoft@informatik.tu-chemnitz.de>
 *
 * hacked by Andi Kleen for x86-64.
 * unified by tglx
 */

#include <asm/irq_vectors.h>

#ifndef __ASSEMBLY__

#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/smp.h>

#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/sections.h>

/* Interrupt handlers registered during init_IRQ */
extern void apic_timer_interrupt(void);
extern void x86_platform_ipi(void);
extern void error_interrupt(void);
extern void irq_work_interrupt(void);

extern void spurious_interrupt(void);
extern void thermal_interrupt(void);
extern void reschedule_interrupt(void);

#ifdef CONFIG_POPCORN_SHMTUN
/* POPCORN -- handler definition */
extern void popcorn_net_interrupt(void);
#endif

#ifdef CONFIG_POPCORN_KMSG
extern void popcorn_kmsg_interrupt(void);
extern void popcorn_ipi_latency_interrupt(void);
#endif

extern void invalidate_interrupt(void);
extern void invalidate_interrupt0(void);
extern void invalidate_interrupt1(void);
extern void invalidate_interrupt2(void);
extern void invalidate_interrupt3(void);
extern void invalidate_interrupt4(void);
extern void invalidate_interrupt5(void);
extern void invalidate_interrupt6(void);
extern void invalidate_interrupt7(void);
extern void invalidate_interrupt8(void);
extern void invalidate_interrupt9(void);
extern void invalidate_interrupt10(void);
extern void invalidate_interrupt11(void);
extern void invalidate_interrupt12(void);
extern void invalidate_interrupt13(void);
extern void invalidate_interrupt14(void);
extern void invalidate_interrupt15(void);
extern void invalidate_interrupt16(void);
extern void invalidate_interrupt17(void);
extern void invalidate_interrupt18(void);
extern void invalidate_interrupt19(void);
extern void invalidate_interrupt20(void);
extern void invalidate_interrupt21(void);
extern void invalidate_interrupt22(void);
extern void invalidate_interrupt23(void);
extern void invalidate_interrupt24(void);
extern void invalidate_interrupt25(void);
extern void invalidate_interrupt26(void);
extern void invalidate_interrupt27(void);
extern void invalidate_interrupt28(void);
extern void invalidate_interrupt29(void);
extern void invalidate_interrupt30(void);
extern void invalidate_interrupt31(void);

extern void irq_move_cleanup_interrupt(void);
extern void reboot_interrupt(void);
extern void threshold_interrupt(void);

extern void call_function_interrupt(void);
extern void call_function_single_interrupt(void);

/* IOAPIC */
#define IO_APIC_IRQ(x) (((x) >= NR_IRQS_LEGACY) || ((1<<(x)) & io_apic_irqs))
extern unsigned long io_apic_irqs;

extern void init_VISWS_APIC_irqs(void);
extern void setup_IO_APIC(void);
extern void disable_IO_APIC(void);

struct io_apic_irq_attr {
	int ioapic;
	int ioapic_pin;
	int trigger;
	int polarity;
};

static inline void set_io_apic_irq_attr(struct io_apic_irq_attr *irq_attr,
					int ioapic, int ioapic_pin,
					int trigger, int polarity)
{
	irq_attr->ioapic	= ioapic;
	irq_attr->ioapic_pin	= ioapic_pin;
	irq_attr->trigger	= trigger;
	irq_attr->polarity	= polarity;
}

struct irq_2_iommu {
	struct intel_iommu *iommu;
	u16 irte_index;
	u16 sub_handle;
	u8  irte_mask;
};

/*
 * This is performance-critical, we want to do it O(1)
 *
 * Most irqs are mapped 1:1 with pins.
 */
struct irq_cfg {
	struct irq_pin_list	*irq_2_pin;
	cpumask_var_t		domain;
	cpumask_var_t		old_domain;
	u8			vector;
	u8			move_in_progress : 1;
#ifdef CONFIG_IRQ_REMAP
	struct irq_2_iommu	irq_2_iommu;
#endif
	u8			free : 1;
};

extern int assign_irq_vector(int, struct irq_cfg *, const struct cpumask *);
extern void send_cleanup_vector(struct irq_cfg *);

struct irq_data;
int __ioapic_set_affinity(struct irq_data *, const struct cpumask *,
			  unsigned int *dest_id);
extern int IO_APIC_get_PCI_irq_vector(int bus, int devfn, int pin, struct io_apic_irq_attr *irq_attr);
extern void setup_ioapic_dest(void);

extern void enable_IO_APIC(void);

/* Statistics */
extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

/* EISA */
extern void eisa_set_level_irq(unsigned int irq);

/* SMP */
extern void smp_apic_timer_interrupt(struct pt_regs *);
extern void smp_spurious_interrupt(struct pt_regs *);
extern void smp_x86_platform_ipi(struct pt_regs *);
extern void smp_error_interrupt(struct pt_regs *);
#ifdef CONFIG_X86_IO_APIC
extern asmlinkage void smp_irq_move_cleanup_interrupt(void);
#endif
#ifdef CONFIG_SMP
extern void smp_reschedule_interrupt(struct pt_regs *);
#ifdef CONFIG_POPCORN_SHMTUN
extern void smp_popcorn_net_interrupt(struct pt_regs *);
#endif
extern void smp_call_function_interrupt(struct pt_regs *);
extern void smp_call_function_single_interrupt(struct pt_regs *);
#ifdef CONFIG_X86_32
extern void smp_invalidate_interrupt(struct pt_regs *);
#else
extern asmlinkage void smp_invalidate_interrupt(struct pt_regs *);
#endif
#endif

extern void (*__initconst interrupt[NR_VECTORS-FIRST_EXTERNAL_VECTOR])(void);

typedef int vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);
extern void setup_vector_irq(int cpu);

#ifdef CONFIG_X86_IO_APIC
extern void lock_vector_lock(void);
extern void unlock_vector_lock(void);
extern void __setup_vector_irq(int cpu);
#else
static inline void lock_vector_lock(void) {}
static inline void unlock_vector_lock(void) {}
static inline void __setup_vector_irq(int cpu) {}
#endif

#endif /* !ASSEMBLY_ */

#endif /* _ASM_X86_HW_IRQ_H */
