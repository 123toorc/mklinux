/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2011 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/pkt_sched.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/prefetch.h>
#include <scsi/fc/fc_fcoe.h>

#include "ixgbe.h"
#include "ixgbe_common.h"
#include "ixgbe_dcb_82599.h"
#include "ixgbe_sriov.h"
#include "snull.h"

#define __pika__ 	0
#define __pop__ 	1

#define s_trace_printk(...) trace_printk(__VA_ARGS__)

static int num_of_hash_buc=0;
static int ideal_num_hash_buc=0;

void inc_num_of_hash_buc()
{
	num_of_hash_buc++;
}

void dec_num_of_hash_buc()
{
	num_of_hash_buc--;
}

int read_num_of_hash_buc()
{
	return num_of_hash_buc;
}

void set_number_ideal(int val)
{
	ideal_num_hash_buc = val;
}
int get_number_ideal()
{
	return ideal_num_hash_buc;
}

#define LOAD_TOLARANCE 1.2

int compare_ideal_curr()
{
	return ((ideal_num_hash_buc) - (LOAD_TOLARANCE*num_of_hash_buc));
}

extern void add_filter(unsigned int src_ip,unsigned int port);
EXPORT_SYMBOL(inc_num_of_hash_buc);
EXPORT_SYMBOL(dec_num_of_hash_buc);
EXPORT_SYMBOL(read_num_of_hash_buc);
EXPORT_SYMBOL(set_number_ideal);
EXPORT_SYMBOL(get_number_ideal);
EXPORT_SYMBOL(compare_ideal_curr);

extern void send_skb_copy(struct sk_buff *skb,int kernel);
extern void wake_load_banlancer_up(void);
extern void put_request_to_balance();
extern void snull_atr_setup_spread_load_balance_one(struct ixgbe_hw *hw, u16 port, u8 queue);
extern flow_info* flow_info_data[MAX_FLOW_COUNT];
EXPORT_SYMBOL(flow_info_data);

void snull_move_flow(struct ixgbe_hw *hw, flow_info* flow_entry, u8 queue)
{
//	printk("%s: q %d port %x\n",__func__,queue,flow_entry->port);
	flow_entry->queue=queue;
	snull_atr_setup_spread_load_balance_one(hw,flow_entry->port,queue);

}
EXPORT_SYMBOL(snull_move_flow);

static u64 syn_count;

void inc_sync_count()
{
	syn_count++;
}

void dec_sync_count()
{
	syn_count--;
}

u64 get_sync_count(get_sync_count)
{
	return syn_count;
}
EXPORT_SYMBOL(get_sync_count);
void clear_sync_count()
{
	syn_count=0;
}
EXPORT_SYMBOL(clear_sync_count);

int is_primary=1;
int max_kernels=1;
int check_if_primary()
{
	int cpu_id = smp_processor_id();
	if(cpu_id==0)
		return 1;
	return 0;
}

EXPORT_SYMBOL(check_if_primary);
static int __init if_primary(char *str)
{
        is_primary = simple_strtoull(str, 0, 10);
        return 0;
}
early_param("is_primary", if_primary);

static int __init get_max_kernels(char *str)
{
        max_kernels = simple_strtoull(str, 0, 10);
        return 0;
}
early_param("max_kernels", get_max_kernels);

int get_max_kernel()
{
	return max_kernels;
}

EXPORT_SYMBOL(get_max_kernel);


extern void snull_atr_setup_spread_load_balance_one(struct ixgbe_hw *hw, u16 port, u8 queue);


static struct ixgbe_adapter *adapter_ptr;
static void *adapter_ptr_phy;

struct ixgbe_adapter * get_ixgbe_adapter()
{
	return adapter_ptr;
}
EXPORT_SYMBOL(get_ixgbe_adapter);

char ixgbe_driver_name[] = "ixgbe";
static const char ixgbe_driver_string[] =
			      "Intel(R) 10 Gigabit PCI Express Network Driver";
#define MAJ 3
#define MIN 6
#define BUILD 7
#define DRV_VERSION __stringify(MAJ) "." __stringify(MIN) "." \
	__stringify(BUILD) "-k"
const char ixgbe_driver_version[] = DRV_VERSION;
static const char ixgbe_copyright[] =
				"Copyright (c) 1999-2011 Intel Corporation.";

static const struct ixgbe_info *ixgbe_info_tbl[] = {
	[board_82598] = &ixgbe_82598_info,
	[board_82599] = &ixgbe_82599_info,
	[board_X540] = &ixgbe_X540_info,
};

/* ixgbe_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static DEFINE_PCI_DEVICE_TABLE(ixgbe_pci_tbl) = {
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AF_DUAL_PORT), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AF_SINGLE_PORT), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AT), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598AT2), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598EB_CX4), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598_CX4_DUAL_PORT), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598_DA_DUAL_PORT), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598EB_XF_LR), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598EB_SFP_LOM), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82598_BX), board_82598 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_KX4), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_XAUI_LOM), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_KR), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_SFP), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_SFP_EM), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_KX4_MEZZ), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_CX4), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_BACKPLANE_FCOE), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_SFP_FCOE), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_T3_LOM), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_COMBO_BACKPLANE), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_X540T), board_X540 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_SFP_SF2), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599_LS), board_82599 },
	{PCI_VDEVICE(INTEL, IXGBE_DEV_ID_82599EN_SFP), board_82599 },
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, ixgbe_pci_tbl);

#define NUM_OF_Q 24

struct ixgbe_ring *tx_ring_from_adapter[NUM_OF_Q];
struct ixgbe_ring *rx_ring_from_adapter[NUM_OF_Q];



#ifdef CONFIG_IXGBE_DCA
static int ixgbe_notify_dca(struct notifier_block *, unsigned long event,
			    void *p);
static struct notifier_block dca_notifier = {
	.notifier_call = ixgbe_notify_dca,
	.next          = NULL,
	.priority      = 0
};
#endif

#ifdef CONFIG_PCI_IOV
static unsigned int max_vfs;
module_param(max_vfs, uint, 0);
MODULE_PARM_DESC(max_vfs,
		 "Maximum number of virtual functions to allocate per physical function");
#endif /* CONFIG_PCI_IOV */

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) 10 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

static void ixgbe_service_event_schedule(struct ixgbe_adapter *adapter)
{
	if (!test_bit(__IXGBE_DOWN, &adapter->state) &&
	    !test_and_set_bit(__IXGBE_SERVICE_SCHED, &adapter->state))
		schedule_work(&adapter->service_task);
}

static void ixgbe_service_event_complete(struct ixgbe_adapter *adapter)
{
	BUG_ON(!test_bit(__IXGBE_SERVICE_SCHED, &adapter->state));

	/* flush memory to make sure state is correct before next watchog */
	smp_mb__before_clear_bit();
	clear_bit(__IXGBE_SERVICE_SCHED, &adapter->state);
}

struct ixgbe_reg_info {
	u32 ofs;
	char *name;
};

static const struct ixgbe_reg_info ixgbe_reg_info_tbl[] = {

	/* General Registers */
	{IXGBE_CTRL, "CTRL"},
	{IXGBE_STATUS, "STATUS"},
	{IXGBE_CTRL_EXT, "CTRL_EXT"},

	/* Interrupt Registers */
	{IXGBE_EICR, "EICR"},

	/* RX Registers */
	{IXGBE_SRRCTL(0), "SRRCTL"},
	{IXGBE_DCA_RXCTRL(0), "DRXCTL"},
	{IXGBE_RDLEN(0), "RDLEN"},
	{IXGBE_RDH(0), "RDH"},
	{IXGBE_RDT(0), "RDT"},
	{IXGBE_RXDCTL(0), "RXDCTL"},
	{IXGBE_RDBAL(0), "RDBAL"},
	{IXGBE_RDBAH(0), "RDBAH"},

	/* TX Registers */
	{IXGBE_TDBAL(0), "TDBAL"},
	{IXGBE_TDBAH(0), "TDBAH"},
	{IXGBE_TDLEN(0), "TDLEN"},
	{IXGBE_TDH(0), "TDH"},
	{IXGBE_TDT(0), "TDT"},
	{IXGBE_TXDCTL(0), "TXDCTL"},

	/* List Terminator */
	{}
};


/*
 * ixgbe_regdump - register printout routine
 */
static void ixgbe_regdump(struct ixgbe_hw *hw, struct ixgbe_reg_info *reginfo)
{
	int i = 0, j = 0;
	char rname[16];
	u32 regs[64];

	switch (reginfo->ofs) {
	case IXGBE_SRRCTL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));
		break;
	case IXGBE_DCA_RXCTRL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(i));
		break;
	case IXGBE_RDLEN(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RDLEN(i));
		break;
	case IXGBE_RDH(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RDH(i));
		break;
	case IXGBE_RDT(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RDT(i));
		break;
	case IXGBE_RXDCTL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
		break;
	case IXGBE_RDBAL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RDBAL(i));
		break;
	case IXGBE_RDBAH(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_RDBAH(i));
		break;
	case IXGBE_TDBAL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TDBAL(i));
		break;
	case IXGBE_TDBAH(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TDBAH(i));
		break;
	case IXGBE_TDLEN(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TDLEN(i));
		break;
	case IXGBE_TDH(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TDH(i));
		break;
	case IXGBE_TDT(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TDT(i));
		break;
	case IXGBE_TXDCTL(0):
		for (i = 0; i < 64; i++)
			regs[i] = IXGBE_READ_REG(hw, IXGBE_TXDCTL(i));
		break;
	default:
		pr_info("%-15s %08x\n", reginfo->name,
			IXGBE_READ_REG(hw, reginfo->ofs));
		return;
	}

	for (i = 0; i < 8; i++) {
		snprintf(rname, 16, "%s[%d-%d]", reginfo->name, i*8, i*8+7);
		pr_err("%-15s", rname);
		for (j = 0; j < 8; j++)
			pr_cont(" %08x", regs[i*8+j]);
		pr_cont("\n");
	}

}

/*
 * ixgbe_dump - Print registers, tx-rings and rx-rings
 */
static void ixgbe_dump(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_reg_info *reginfo;
	int n = 0;
	struct ixgbe_ring *tx_ring;
	struct ixgbe_tx_buffer *tx_buffer_info;
	union ixgbe_adv_tx_desc *tx_desc;
	struct my_u0 { u64 a; u64 b; } *u0;
	struct ixgbe_ring *rx_ring;
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *rx_buffer_info;
	u32 staterr;
	int i = 0;

	if (!netif_msg_hw(adapter))
		return;

	/* Print netdevice Info */
	if (netdev) {
		dev_info(&adapter->pdev->dev, "Net device Info\n");
		pr_info("Device Name     state            "
			"trans_start      last_rx\n");
		pr_info("%-15s %016lX %016lX %016lX\n",
			netdev->name,
			netdev->state,
			netdev->trans_start,
			netdev->last_rx);
	}

	/* Print Registers */
	dev_info(&adapter->pdev->dev, "Register Dump\n");
	pr_info(" Register Name   Value\n");
	for (reginfo = (struct ixgbe_reg_info *)ixgbe_reg_info_tbl;
	     reginfo->name; reginfo++) {
		ixgbe_regdump(hw, reginfo);
	}

	/* Print TX Ring Summary */
	if (!netdev || !netif_running(netdev))
		goto exit;

	dev_info(&adapter->pdev->dev, "TX Rings Summary\n");
	pr_info("Queue [NTU] [NTC] [bi(ntc)->dma  ] leng ntw timestamp\n");
	for (n = 0; n < adapter->num_tx_queues; n++) {
		tx_ring = adapter->tx_ring[n];
		tx_buffer_info =
			&tx_ring->tx_buffer_info[tx_ring->next_to_clean];
		pr_info(" %5d %5X %5X %016llX %04X %p %016llX\n",
			   n, tx_ring->next_to_use, tx_ring->next_to_clean,
			   (u64)tx_buffer_info->dma,
			   tx_buffer_info->length,
			   tx_buffer_info->next_to_watch,
			   (u64)tx_buffer_info->time_stamp);
	}

	/* Print TX Rings */
	if (!netif_msg_tx_done(adapter))
		goto rx_ring_summary;

	dev_info(&adapter->pdev->dev, "TX Rings Dump\n");

	/* Transmit Descriptor Formats
	 *
	 * Advanced Transmit Descriptor
	 *   +--------------------------------------------------------------+
	 * 0 |         Buffer Address [63:0]                                |
	 *   +--------------------------------------------------------------+
	 * 8 |  PAYLEN  | PORTS  | IDX | STA | DCMD  |DTYP |  RSV |  DTALEN |
	 *   +--------------------------------------------------------------+
	 *   63       46 45    40 39 36 35 32 31   24 23 20 19              0
	 */

	for (n = 0; n < adapter->num_tx_queues; n++) {
		tx_ring = adapter->tx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("TX QUEUE INDEX = %d\n", tx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("T [desc]     [address 63:0  ] "
			"[PlPOIdStDDt Ln] [bi->dma       ] "
			"leng  ntw timestamp        bi->skb\n");

		for (i = 0; tx_ring->desc && (i < tx_ring->count); i++) {
			tx_desc = IXGBE_TX_DESC_ADV(tx_ring, i);
			tx_buffer_info = &tx_ring->tx_buffer_info[i];
			u0 = (struct my_u0 *)tx_desc;
			pr_info("T [0x%03X]    %016llX %016llX %016llX"
				" %04X  %p %016llX %p", i,
				le64_to_cpu(u0->a),
				le64_to_cpu(u0->b),
				(u64)tx_buffer_info->dma,
				tx_buffer_info->length,
				tx_buffer_info->next_to_watch,
				(u64)tx_buffer_info->time_stamp,
				tx_buffer_info->skb);
			if (i == tx_ring->next_to_use &&
				i == tx_ring->next_to_clean)
				pr_cont(" NTC/U\n");
			else if (i == tx_ring->next_to_use)
				pr_cont(" NTU\n");
			else if (i == tx_ring->next_to_clean)
				pr_cont(" NTC\n");
			else
				pr_cont("\n");

			if (netif_msg_pktdata(adapter) &&
				tx_buffer_info->dma != 0)
				print_hex_dump(KERN_INFO, "",
					DUMP_PREFIX_ADDRESS, 16, 1,
					phys_to_virt(tx_buffer_info->dma),
					tx_buffer_info->length, true);
		}
	}

	/* Print RX Rings Summary */
rx_ring_summary:
	dev_info(&adapter->pdev->dev, "RX Rings Summary\n");
	pr_info("Queue [NTU] [NTC]\n");
	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info("%5d %5X %5X\n",
			n, rx_ring->next_to_use, rx_ring->next_to_clean);
	}

	/* Print RX Rings */
	if (!netif_msg_rx_status(adapter))
		goto exit;

	dev_info(&adapter->pdev->dev, "RX Rings Dump\n");

	/* Advanced Receive Descriptor (Read) Format
	 *    63                                           1        0
	 *    +-----------------------------------------------------+
	 *  0 |       Packet Buffer Address [63:1]           |A0/NSE|
	 *    +----------------------------------------------+------+
	 *  8 |       Header Buffer Address [63:1]           |  DD  |
	 *    +-----------------------------------------------------+
	 *
	 *
	 * Advanced Receive Descriptor (Write-Back) Format
	 *
	 *   63       48 47    32 31  30      21 20 16 15   4 3     0
	 *   +------------------------------------------------------+
	 * 0 | Packet     IP     |SPH| HDR_LEN   | RSV|Packet|  RSS |
	 *   | Checksum   Ident  |   |           |    | Type | Type |
	 *   +------------------------------------------------------+
	 * 8 | VLAN Tag | Length | Extended Error | Extended Status |
	 *   +------------------------------------------------------+
	 *   63       48 47    32 31            20 19               0
	 */
	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("RX QUEUE INDEX = %d\n", rx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("R  [desc]      [ PktBuf     A0] "
			"[  HeadBuf   DD] [bi->dma       ] [bi->skb] "
			"<-- Adv Rx Read format\n");
		pr_info("RWB[desc]      [PcsmIpSHl PtRs] "
			"[vl er S cks ln] ---------------- [bi->skb] "
			"<-- Adv Rx Write-Back format\n");

		for (i = 0; i < rx_ring->count; i++) {
			rx_buffer_info = &rx_ring->rx_buffer_info[i];
			rx_desc = IXGBE_RX_DESC_ADV(rx_ring, i);
			u0 = (struct my_u0 *)rx_desc;
			staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
			if (staterr & IXGBE_RXD_STAT_DD) {
				/* Descriptor Done */
				pr_info("RWB[0x%03X]     %016llX "
					"%016llX ---------------- %p", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					rx_buffer_info->skb);
			} else {
				pr_info("R  [0x%03X]     %016llX "
					"%016llX %016llX %p", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					(u64)rx_buffer_info->dma,
					rx_buffer_info->skb);

				if (netif_msg_pktdata(adapter)) {
					print_hex_dump(KERN_INFO, "",
					   DUMP_PREFIX_ADDRESS, 16, 1,
					   phys_to_virt(rx_buffer_info->dma),
					   rx_ring->rx_buf_len, true);

					if (rx_ring->rx_buf_len
						< IXGBE_RXBUFFER_2K)
						print_hex_dump(KERN_INFO, "",
						  DUMP_PREFIX_ADDRESS, 16, 1,
						  phys_to_virt(
						    rx_buffer_info->page_dma +
						    rx_buffer_info->page_offset
						  ),
						  PAGE_SIZE/2, true);
				}
			}

			if (i == rx_ring->next_to_use)
				pr_cont(" NTU\n");
			else if (i == rx_ring->next_to_clean)
				pr_cont(" NTC\n");
			else
				pr_cont("\n");

		}
	}

exit:
	return;
}

static void ixgbe_release_hw_control(struct ixgbe_adapter *adapter)
{
	u32 ctrl_ext;

	/* Let firmware take over control of h/w */
	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
			ctrl_ext & ~IXGBE_CTRL_EXT_DRV_LOAD);
}

static void ixgbe_get_hw_control(struct ixgbe_adapter *adapter)
{
	u32 ctrl_ext;

	/* Let firmware know the driver has taken over */
	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
			ctrl_ext | IXGBE_CTRL_EXT_DRV_LOAD);
}

/*
 * ixgbe_set_ivar - set the IVAR registers, mapping interrupt causes to vectors
 * @adapter: pointer to adapter struct
 * @direction: 0 for Rx, 1 for Tx, -1 for other causes
 * @queue: queue to map the corresponding interrupt to
 * @msix_vector: the vector to map to the corresponding queue
 *
 */
static void ixgbe_set_ivar(struct ixgbe_adapter *adapter, s8 direction,
			   u8 queue, u8 msix_vector)
{
	u32 ivar, index;
	struct ixgbe_hw *hw = &adapter->hw;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		msix_vector |= IXGBE_IVAR_ALLOC_VAL;
		if (direction == -1)
			direction = 0;
		index = (((direction * 64) + queue) >> 2) & 0x1F;
		ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(index));
		ivar &= ~(0xFF << (8 * (queue & 0x3)));
		ivar |= (msix_vector << (8 * (queue & 0x3)));
		IXGBE_WRITE_REG(hw, IXGBE_IVAR(index), ivar);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (direction == -1) {
			/* other causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = ((queue & 1) * 8);
			ivar = IXGBE_READ_REG(&adapter->hw, IXGBE_IVAR_MISC);
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_IVAR_MISC, ivar);
			break;
		} else {
			/* tx or rx causes */
			msix_vector |= IXGBE_IVAR_ALLOC_VAL;
			index = ((16 * (queue & 1)) + (8 * direction));
			ivar = IXGBE_READ_REG(hw, IXGBE_IVAR(queue >> 1));
			ivar &= ~(0xFF << index);
			ivar |= (msix_vector << index);
			IXGBE_WRITE_REG(hw, IXGBE_IVAR(queue >> 1), ivar);
			break;
		}
	default:
		break;
	}
}

static inline void ixgbe_irq_rearm_queues(struct ixgbe_adapter *adapter,
					  u64 qmask)
{
	u32 mask;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		mask = (IXGBE_EIMS_RTX_QUEUE & qmask);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, mask);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		mask = (qmask & 0xFFFFFFFF);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS_EX(0), mask);
		mask = (qmask >> 32);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS_EX(1), mask);
		break;
	default:
		break;
	}
}

static inline void ixgbe_unmap_tx_resource(struct ixgbe_ring *ring,
					   struct ixgbe_tx_buffer *tx_buffer)
{
	
	if(is_primary==0)
		goto out_secondary;
	if (tx_buffer->dma) {
		if (tx_buffer->tx_flags & IXGBE_TX_FLAGS_MAPPED_AS_PAGE)
			dma_unmap_page(ring->dev,
			               tx_buffer->dma,
			               tx_buffer->length,
			               DMA_TO_DEVICE);
		else
			dma_unmap_single(ring->dev,
			                 tx_buffer->dma,
			                 tx_buffer->length,
			                 DMA_TO_DEVICE);
	}
out_secondary:						//saif modified	
	tx_buffer->dma = 0;
}

void ixgbe_unmap_and_free_tx_resource(struct ixgbe_ring *tx_ring,
				      struct ixgbe_tx_buffer *tx_buffer_info)
{
	ixgbe_unmap_tx_resource(tx_ring, tx_buffer_info);
	if (tx_buffer_info->skb)
		dev_kfree_skb_any(tx_buffer_info->skb);
	tx_buffer_info->skb = NULL;
	/* tx_buffer_info must be completely set up in the transmit path */
}

static void ixgbe_update_xoff_received(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_hw_stats *hwstats = &adapter->stats;
	u32 data = 0;
	u32 xoff[8] = {0};
	int i;

	if ((hw->fc.current_mode == ixgbe_fc_full) ||
	    (hw->fc.current_mode == ixgbe_fc_rx_pause)) {
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			data = IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
			break;
		default:
			data = IXGBE_READ_REG(hw, IXGBE_LXOFFRXCNT);
		}
		hwstats->lxoffrxc += data;

		/* refill credits (no tx hang) if we received xoff */
		if (!data)
			return;

		for (i = 0; i < adapter->num_tx_queues; i++)
			clear_bit(__IXGBE_HANG_CHECK_ARMED,
				  &adapter->tx_ring[i]->state);
		return;
	} else if (!(adapter->dcb_cfg.pfc_mode_enable))
		return;

	/* update stats for each tc, only valid with PFC enabled */
	for (i = 0; i < MAX_TX_PACKET_BUFFERS; i++) {
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			xoff[i] = IXGBE_READ_REG(hw, IXGBE_PXOFFRXC(i));
			break;
		default:
			xoff[i] = IXGBE_READ_REG(hw, IXGBE_PXOFFRXCNT(i));
		}
		hwstats->pxoffrxc[i] += xoff[i];
	}

	/* disarm tx queues that have received xoff frames */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct ixgbe_ring *tx_ring = adapter->tx_ring[i];
		u8 tc = tx_ring->dcb_tc;

		if (xoff[tc])
			clear_bit(__IXGBE_HANG_CHECK_ARMED, &tx_ring->state);
	}
}

static u64 ixgbe_get_tx_completed(struct ixgbe_ring *ring)
{
	return ring->tx_stats.completed;
}

static u64 ixgbe_get_tx_pending(struct ixgbe_ring *ring)
{
	struct ixgbe_adapter *adapter = netdev_priv(ring->netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	u32 head = IXGBE_READ_REG(hw, IXGBE_TDH(ring->reg_idx));
	u32 tail = IXGBE_READ_REG(hw, IXGBE_TDT(ring->reg_idx));

	if (head != tail)
		return (head < tail) ?
			tail - head : (tail + ring->count - head);

	return 0;
}

static inline bool ixgbe_check_tx_hang(struct ixgbe_ring *tx_ring)
{
	u32 tx_done = ixgbe_get_tx_completed(tx_ring);
	u32 tx_done_old = tx_ring->tx_stats.tx_done_old;
	u32 tx_pending = ixgbe_get_tx_pending(tx_ring);
	bool ret = false;

	clear_check_for_tx_hang(tx_ring);

	/*
	 * Check for a hung queue, but be thorough. This verifies
	 * that a transmit has been completed since the previous
	 * check AND there is at least one packet pending. The
	 * ARMED bit is set to indicate a potential hang. The
	 * bit is cleared if a pause frame is received to remove
	 * false hang detection due to PFC or 802.3x frames. By
	 * requiring this to fail twice we avoid races with
	 * pfc clearing the ARMED bit and conditions where we
	 * run the check_tx_hang logic with a transmit completion
	 * pending but without time to complete it yet.
	 */
	if ((tx_done_old == tx_done) && tx_pending) {
		/* make sure it is true for two checks in a row */
		ret = test_and_set_bit(__IXGBE_HANG_CHECK_ARMED,
				       &tx_ring->state);
	} else {
		/* update completed stats and continue */
		tx_ring->tx_stats.tx_done_old = tx_done;
		/* reset the countdown */
		clear_bit(__IXGBE_HANG_CHECK_ARMED, &tx_ring->state);
	}

	return ret;
}


static void put_iphdr(struct sk_buff *skb, int iphdrlen){
         __skb_push(skb, iphdrlen);
}


/* Note call put_iphdr after using the iphdr in case   * of no errors. */
static int get_iphdr(struct sk_buff *skb, struct iphdr** ip_header,int *iphdrlen){
         int res= -EFAULT;
         struct iphdr* network_header= NULL;
         int len;
 
         skb_reset_network_header(skb);
         skb_reset_transport_header(skb);
         skb_reset_mac_len(skb);
 
 //        if (skb->pkt_type == PACKET_OTHERHOST)
 //                goto out;
 
         if (!pskb_may_pull(skb, sizeof(struct iphdr)))
                 goto out;
 
         if(skb_shared(skb))
                 printk("%s: WARNING skb shared\n", __func__);
 
         network_header= ip_hdr(skb);
 
         if (network_header->ihl < 5 || network_header->version != 4)
                 goto out;
 
         if (!pskb_may_pull(skb, network_header->ihl*4))
                 goto out;
 
         network_header= ip_hdr(skb);
 
         if (unlikely(ip_fast_csum((u8 *)network_header, network_header->ihl)))
                 goto out;
 
         len = ntohs(network_header->tot_len);
         if (skb->len < len || len < network_header->ihl*4)
                 goto out;
 
         if (pskb_trim_rcsum(skb, len))
                 goto out;
 
         /* Remove any debris in the socket control block */
         memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
         skb_orphan(skb);
 
         *iphdrlen= ip_hdrlen(skb);
         __skb_pull(skb, *iphdrlen);
         skb_reset_transport_header(skb);
 
         *ip_header= ip_hdr(skb);
 
         res= 0;
 
out:
         return res;
}


/**
 * ixgbe_tx_timeout_reset - initiate reset due to Tx timeout
 * @adapter: driver private struct
 **/
static void ixgbe_tx_timeout_reset(struct ixgbe_adapter *adapter)
{

	/* Do the reset outside of interrupt context */
	if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
		adapter->flags2 |= IXGBE_FLAG2_RESET_REQUESTED;
		ixgbe_service_event_schedule(adapter);
	}
}

/**
 * ixgbe_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: structure containing interrupt and ring information
 * @tx_ring: tx ring to clean
 **/
static bool ixgbe_clean_tx_irq(struct ixgbe_q_vector *q_vector,
			       struct ixgbe_ring *tx_ring)
{
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct ixgbe_tx_buffer *tx_buffer;
	union ixgbe_adv_tx_desc *tx_desc;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int budget = q_vector->tx.work_limit;
	u16 i = tx_ring->next_to_clean;
	
	tx_buffer = &tx_ring->tx_buffer_info[i];
	tx_desc = IXGBE_TX_DESC_ADV(tx_ring, i);
//	if(check_if_primary()==0)
	{
	//	printk("%s:called q_no %d \n",__func__,tx_ring->reg_idx);
		//return true;
	}
	//dump_stack();
	for (; budget; budget--) {
		union ixgbe_adv_tx_desc *eop_desc = tx_buffer->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* if DD is not set pending work has not been completed */
		if (!(eop_desc->wb.status & cpu_to_le32(IXGBE_TXD_STAT_DD)))
			break;

		/* count the packet as being completed */
		tx_ring->tx_stats.completed++;

		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;

		/* prevent any other reads prior to eop_desc being verified */
		rmb();

		do {
			ixgbe_unmap_tx_resource(tx_ring, tx_buffer);
			tx_desc->wb.status = 0;
			if (likely(tx_desc == eop_desc)) {
				eop_desc = NULL;
				dev_kfree_skb_any(tx_buffer->skb);
				tx_buffer->skb = NULL;

				total_bytes += tx_buffer->bytecount;
				total_packets += tx_buffer->gso_segs;
			}

			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(i == tx_ring->count)) {
				i = 0;

				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = IXGBE_TX_DESC_ADV(tx_ring, 0);
			}

		} while (eop_desc);
	}

	tx_ring->next_to_clean = i;
	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->stats.bytes += total_bytes;
	tx_ring->stats.packets += total_packets;
	u64_stats_update_end(&tx_ring->syncp);
	q_vector->tx.total_bytes += total_bytes;
	q_vector->tx.total_packets += total_packets;

if(check_if_primary()!=0)
{
	if (check_for_tx_hang(tx_ring) && ixgbe_check_tx_hang(tx_ring)) {
		/* schedule immediate reset if we believe we hung */
		struct ixgbe_hw *hw = &adapter->hw;
		tx_desc = IXGBE_TX_DESC_ADV(tx_ring, i);
		
		printk("%s: tx hang \n",__func__);
		
		return true;
		e_err(drv, "Detected Tx Unit Hang\n"
			"  Tx Queue             <%d>\n"
			"  TDH, TDT             <%x>, <%x>\n"
			"  next_to_use          <%x>\n"
			"  next_to_clean        <%x>\n"
			"tx_buffer_info[next_to_clean]\n"
			"  time_stamp           <%lx>\n"
			"  jiffies              <%lx>\n",
			tx_ring->queue_index,
			IXGBE_READ_REG(hw, IXGBE_TDH(tx_ring->reg_idx)),
			IXGBE_READ_REG(hw, IXGBE_TDT(tx_ring->reg_idx)),
			tx_ring->next_to_use, i,
			tx_ring->tx_buffer_info[i].time_stamp, jiffies);
//this we have to change saif
		netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);

		e_info(probe,
		       "tx hang %d detected on queue %d, resetting adapter\n",
			adapter->tx_timeout_count + 1, tx_ring->queue_index);

		/* schedule immediate reset if we believe we hung */
		ixgbe_tx_timeout_reset(adapter);

		/* the adapter is about to reset, no point in enabling stuff */
		return true;
	}
}
#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	if (unlikely(total_packets && netif_carrier_ok(tx_ring->netdev) &&
		     (ixgbe_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev, tx_ring->queue_index) &&
		    !test_bit(__IXGBE_DOWN, &adapter->state)) {
			netif_wake_subqueue(tx_ring->netdev, tx_ring->queue_index);
			++tx_ring->tx_stats.restart_queue;
		}
	}

	return !!budget;
}

EXPORT_SYMBOL(ixgbe_clean_tx_irq);

#ifdef CONFIG_IXGBE_DCA
static void ixgbe_update_rx_dca(struct ixgbe_adapter *adapter,
				struct ixgbe_ring *rx_ring,
				int cpu)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rxctrl;
	u8 reg_idx = rx_ring->reg_idx;

	rxctrl = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(reg_idx));
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		rxctrl &= ~IXGBE_DCA_RXCTRL_CPUID_MASK;
		rxctrl |= dca3_get_tag(rx_ring->dev, cpu);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		rxctrl &= ~IXGBE_DCA_RXCTRL_CPUID_MASK_82599;
		rxctrl |= (dca3_get_tag(rx_ring->dev, cpu) <<
			   IXGBE_DCA_RXCTRL_CPUID_SHIFT_82599);
		break;
	default:
		break;
	}
	rxctrl |= IXGBE_DCA_RXCTRL_DESC_DCA_EN;
	rxctrl |= IXGBE_DCA_RXCTRL_HEAD_DCA_EN;
	rxctrl &= ~(IXGBE_DCA_RXCTRL_DESC_RRO_EN);
	IXGBE_WRITE_REG(hw, IXGBE_DCA_RXCTRL(reg_idx), rxctrl);
}

static void ixgbe_update_tx_dca(struct ixgbe_adapter *adapter,
				struct ixgbe_ring *tx_ring,
				int cpu)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 txctrl;
	u8 reg_idx = tx_ring->reg_idx;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(reg_idx));
		txctrl &= ~IXGBE_DCA_TXCTRL_CPUID_MASK;
		txctrl |= dca3_get_tag(tx_ring->dev, cpu);
		txctrl |= IXGBE_DCA_TXCTRL_DESC_DCA_EN;
		IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL(reg_idx), txctrl);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL_82599(reg_idx));
		txctrl &= ~IXGBE_DCA_TXCTRL_CPUID_MASK_82599;
		txctrl |= (dca3_get_tag(tx_ring->dev, cpu) <<
			   IXGBE_DCA_TXCTRL_CPUID_SHIFT_82599);
		txctrl |= IXGBE_DCA_TXCTRL_DESC_DCA_EN;
		IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL_82599(reg_idx), txctrl);
		break;
	default:
		break;
	}
}

static void ixgbe_update_dca(struct ixgbe_q_vector *q_vector)
{
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct ixgbe_ring *ring;
	int cpu = get_cpu();

	if (q_vector->cpu == cpu)
		goto out_no_update;

	for (ring = q_vector->tx.ring; ring != NULL; ring = ring->next)
		ixgbe_update_tx_dca(adapter, ring, cpu);

	for (ring = q_vector->rx.ring; ring != NULL; ring = ring->next)
		ixgbe_update_rx_dca(adapter, ring, cpu);

	q_vector->cpu = cpu;
out_no_update:
	put_cpu();
}

static void ixgbe_setup_dca(struct ixgbe_adapter *adapter)
{
	int num_q_vectors;
	int i;

	if (!(adapter->flags & IXGBE_FLAG_DCA_ENABLED))
		return;

	/* always use CB2 mode, difference is masked in the CB driver */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 2);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		num_q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	else
		num_q_vectors = 1;

	for (i = 0; i < num_q_vectors; i++) {
		adapter->q_vector[i]->cpu = -1;
		ixgbe_update_dca(adapter->q_vector[i]);
	}
}

static int __ixgbe_notify_dca(struct device *dev, void *data)
{
	struct ixgbe_adapter *adapter = dev_get_drvdata(dev);
	unsigned long event = *(unsigned long *)data;

	if (!(adapter->flags & IXGBE_FLAG_DCA_CAPABLE))
		return 0;

	switch (event) {
	case DCA_PROVIDER_ADD:
		/* if we're already enabled, don't do it again */
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
			break;
		if (dca_add_requester(dev) == 0) {
			adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
			ixgbe_setup_dca(adapter);
			break;
		}
		/* Fall Through since DCA is disabled. */
	case DCA_PROVIDER_REMOVE:
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED) {
			dca_remove_requester(dev);
			adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 1);
		}
		break;
	}

	return 0;
}
#endif /* CONFIG_IXGBE_DCA */


static inline void ixgbe_rx_hash(union ixgbe_adv_rx_desc *rx_desc,
				 struct sk_buff *skb)
{
	skb->rxhash = le32_to_cpu(rx_desc->wb.lower.hi_dword.rss);
}

/**
 * ixgbe_rx_is_fcoe - check the rx desc for incoming pkt type
 * @adapter: address of board private structure
 * @rx_desc: advanced rx descriptor
 *
 * Returns : true if it is FCoE pkt
 */
static inline bool ixgbe_rx_is_fcoe(struct ixgbe_adapter *adapter,
				    union ixgbe_adv_rx_desc *rx_desc)
{
	__le16 pkt_info = rx_desc->wb.lower.lo_dword.hs_rss.pkt_info;

	return (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) &&
	       ((pkt_info & cpu_to_le16(IXGBE_RXDADV_PKTTYPE_ETQF_MASK)) ==
		(cpu_to_le16(IXGBE_ETQF_FILTER_FCOE <<
			     IXGBE_RXDADV_PKTTYPE_ETQF_SHIFT)));
}





/**
 * ixgbe_receive_skb - Send a completed packet up the stack
 * @adapter: board private structure
 * @skb: packet to send up
 * @status: hardware indication of status of receive
 * @rx_ring: rx descriptor ring (for a specific queue) to setup
 * @rx_desc: rx descriptor
 **/
static void ixgbe_receive_skb(struct ixgbe_q_vector *q_vector,
			      struct sk_buff *skb, u8 status,
			      struct ixgbe_ring *ring,
			      union ixgbe_adv_rx_desc *rx_desc,struct napi_struct *napi_secondary)
{

	
    struct iphdr *ipp;
    struct tcphdr *hdr;
//    printk("%s:called\n",__func__);
    unsigned int s_port,d_port,s_or_port=0;
	int ip_head_len;
	int res;
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct napi_struct *napi;
    struct ixgbe_hw* hw = &adapter->hw; 
	//s_or_port = (unsigned short int) hdr->source;
	//s_or_port = s_or_port & 0x0FFF;
#if 1	
	res = get_iphdr(skb,&ipp,&ip_head_len);
	if(res==0)
	{
		if (ipp->protocol == IPPROTO_TCP)
		{
			hdr = (struct tcphdr *) tcp_hdr(skb);
			s_port = htons((unsigned short int) hdr->source);
			d_port = htons((unsigned short int) hdr->dest);
			s_or_port = hdr->source;
			s_or_port = s_or_port & 0xFF0F;
			int q;
			int state;
	//		printk("%s: TCP ports: source: %d, dest: %d orig_source: %x \n",__func__,s_port,d_port,s_or_port);
			if(hdr->fin)
			{
				dec_sync_count();
			//	atomic_dec(&flow_info_data[s_or_port]->ref);
				//printk("%s: fin packet closing %x\n",__func__,s_or_port);
			}
			else if(hdr->syn)
			{
				if(flow_info_data[s_or_port]!=NULL)
				{
					state = atomic_read(&flow_info_data[s_or_port]->state);
				/*	if((state == FLOW_TRANSITING)||(state == FLOW_ROUTED))
					{
						put_iphdr(skb, ip_head_len);
						__kfree_skb(skb);
						return ;
					}
				*/
					inc_sync_count();
			//		printk("%s: sync %d num_of_hash_buc %d ideal_num_hash_buc %d\n",__func__,get_sync_count(),read_num_of_hash_buc(),get_number_ideal());
#if __pop__					
					if(get_sync_count()>=LOAD_THRESH &&(compare_ideal_curr()<0))
					{
						;
						//printk("%s:waking balancer\n",__func__);
						put_request_to_balance();
						wake_load_banlancer_up();
					}
#endif /*__pop__*/	
#if __pika__
				//	if(get_sync_count()>=LOAD_THRESH)
					{
						add_filter(ipp->saddr,s_port);	
					}	
#endif /*__pika__*/
				
				}
			//	s_port = s_port & 0x0FFF;
			//	q=((skb->queue_mapping-1) + 1) % get_max_kernel(); 
			//	printk("%s: syn packet flow filter %x actual %x q %d\n",__func__,s_port,hdr->source,(skb->queue_mapping-1));
						
			//	if(flow_info_data[s_or_port]==NULL)
			//	{
			//		printk("%s: flow NULL port %x src %x kernel %d \n",__func__,s_or_port,htons(s_port),smp_processor_id());
					//return;
			//	}
			//	if(atomic_read(&flow_info_data[s_or_port]->state) == FLOW_TRANSITING)
			//		return ;
			//	atomic_inc(&flow_info_data[s_or_port]->ref);
			
			}
		}
		else
		{
		//	printk("%s: not TCP source: %d, dest: %d \n",__func__,s_port,d_port);
		}
		put_iphdr(skb, ip_head_len);
	}
#endif

	bool is_vlan = (status & IXGBE_RXD_STAT_VP);
	u16 tag = le16_to_cpu(rx_desc->wb.upper.vlan);
	unsigned long long start,end;
	if (is_vlan && (tag & VLAN_VID_MASK))
		__vlan_hwaccel_put_tag(skb, tag);
	if(check_if_primary()==0)
	{
		napi = napi_secondary;
	}
	else
	{
		napi = &q_vector->napi;
	}		
	

	if (!(adapter->flags & IXGBE_FLAG_IN_NETPOLL))
	{
		
/*		if (ipp->protocol == IPPROTO_TCP)
		{
			send_skb_copy(skb,1);
			__kfree_skb(skb);
		}
*/ 
//		else	
			napi_gro_receive(napi, skb);
	
		
	}
	else
	{
		netif_rx(skb);
	}
}

/**
 * ixgbe_rx_checksum - indicate in skb if hw indicated a good cksum
 * @adapter: address of board private structure
 * @status_err: hardware indication of status of receive
 * @skb: skb currently being received and modified
 * @status_err: status error value of last descriptor in packet
 **/
static inline void ixgbe_rx_checksum(struct ixgbe_adapter *adapter,
				     union ixgbe_adv_rx_desc *rx_desc,
				     struct sk_buff *skb,
				     u32 status_err)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Rx csum disabled */
	if (!(adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED))
		return;

	/* if IP and error */
	if ((status_err & IXGBE_RXD_STAT_IPCS) &&
	    (status_err & IXGBE_RXDADV_ERR_IPE)) {
		adapter->hw_csum_rx_error++;
		return;
	}

	if (!(status_err & IXGBE_RXD_STAT_L4CS))
		return;

	if (status_err & IXGBE_RXDADV_ERR_TCPE) {
		u16 pkt_info = rx_desc->wb.lower.lo_dword.hs_rss.pkt_info;

		/*
		 * 82599 errata, UDP frames with a 0 checksum can be marked as
		 * checksum errors.
		 */
		if ((pkt_info & IXGBE_RXDADV_PKTTYPE_UDP) &&
		    (adapter->hw.mac.type == ixgbe_mac_82599EB))
			return;

		adapter->hw_csum_rx_error++;
		return;
	}

	/* It must be a TCP or UDP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static inline void ixgbe_release_rx_desc(struct ixgbe_ring *rx_ring, u32 val)
{
	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}

void snull_clean_rx_ring(struct ixgbe_ring *rx_ring,int count)
{
//	u16 i = rx_ring->next_to_use;
	u16 i = 0;
	rx_ring->next_to_use=0;
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *bi;
	while(count--)
	{
		rx_desc = IXGBE_RX_DESC_ADV(rx_ring, i);
		bi = &rx_ring->rx_buffer_info[i];
		bi->skb=NULL;
		bi->dma=NULL;
		bi->page=NULL;
		bi->page_dma=NULL;
		i++;
		if (i == rx_ring->count)
			i = 0;
	}
	
}
EXPORT_SYMBOL(snull_clean_rx_ring);

/**
 * ixgbe_alloc_rx_buffers - Replace used receive buffers; packet split
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 **/
void ixgbe_alloc_rx_buffers(struct ixgbe_ring *rx_ring, u16 cleaned_count)
{
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *bi;
	struct sk_buff *skb;
	u16 i = rx_ring->next_to_use;
	

	/* do nothing if no valid netdev defined */
	if (!rx_ring->netdev)
		return;
//	if(check_if_primary()==0)
	{
	//	printk("%s: called %d\n",__func__,cleaned_count);
	}

	while (cleaned_count--) {
		rx_desc = IXGBE_RX_DESC_ADV(rx_ring, i);
		bi = &rx_ring->rx_buffer_info[i];
		skb = bi->skb;
		//if(check_if_primary()==0)
	//	printk("%s: rx_ring->next_to_use %d \n",__func__,i);
		
		if (!skb) {
			//if(check_if_primary()==0)
				//printk("%s: called new allocation\n",__func__);
			skb = netdev_alloc_skb_ip_align(rx_ring->netdev,
							rx_ring->rx_buf_len);
			if (!skb) {
				rx_ring->rx_stats.alloc_rx_buff_failed++;
				goto no_buffers;
			}
			/* initialize queue mapping */
			skb_record_rx_queue(skb, rx_ring->queue_index);
			bi->skb = skb;
		}

		if (!bi->dma) {
			if(check_if_primary())
			{
				bi->dma = dma_map_single(rx_ring->dev,
						 skb->data,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);
				if (dma_mapping_error(rx_ring->dev, bi->dma)) {
					rx_ring->rx_stats.alloc_rx_buff_failed++;
					bi->dma = 0;
					goto no_buffers;
				}
			}
			else
			{
				bi->dma = virt_to_phys(skb->data);
			}
		}

		if (ring_is_ps_enabled(rx_ring)) {
		//	printk("%s: ring_is_ps_enabled \n",__func__);
			if (!bi->page) {
				bi->page = netdev_alloc_page(rx_ring->netdev);
				if (!bi->page) {
					rx_ring->rx_stats.alloc_rx_page_failed++;
					goto no_buffers;
				}
			}

			if (!bi->page_dma) {
				/* use a half page if we're re-using */
				bi->page_offset ^= PAGE_SIZE / 2;
				bi->page_dma = dma_map_page(rx_ring->dev,
							    bi->page,
							    bi->page_offset,
							    PAGE_SIZE / 2,
							    DMA_FROM_DEVICE);
				if (dma_mapping_error(rx_ring->dev,
						      bi->page_dma)) {
					rx_ring->rx_stats.alloc_rx_page_failed++;
					bi->page_dma = 0;
					goto no_buffers;
				}
			}

			/* Refresh the desc even if buffer_addrs didn't change
			 * because each write-back erases this info. */
			rx_desc->read.pkt_addr = cpu_to_le64(bi->page_dma);
			rx_desc->read.hdr_addr = cpu_to_le64(bi->dma);
		} else {
		//	printk("%s: ring_is_ps_disbled \n",__func__);
			rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);
			rx_desc->read.hdr_addr = 0;
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
	}

no_buffers:
	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;
		ixgbe_release_rx_desc(rx_ring, i);
	}
}
EXPORT_SYMBOL(ixgbe_alloc_rx_buffers);
static inline u16 ixgbe_get_hlen(union ixgbe_adv_rx_desc *rx_desc)
{
	/* HW will not DMA in data larger than the given buffer, even if it
	 * parses the (NFS, of course) header to be larger.  In that case, it
	 * fills the header buffer and spills the rest into the page.
	 */
	u16 hdr_info = le16_to_cpu(rx_desc->wb.lower.lo_dword.hs_rss.hdr_info);
	u16 hlen = (hdr_info &  IXGBE_RXDADV_HDRBUFLEN_MASK) >>
		    IXGBE_RXDADV_HDRBUFLEN_SHIFT;
	if (hlen > IXGBE_RX_HDR_SIZE)
		hlen = IXGBE_RX_HDR_SIZE;
	return hlen;
}

/**
 * ixgbe_transform_rsc_queue - change rsc queue into a full packet
 * @skb: pointer to the last skb in the rsc queue
 *
 * This function changes a queue full of hw rsc buffers into a completed
 * packet.  It uses the ->prev pointers to find the first packet and then
 * turns it into the frag list owner.
 **/
static inline struct sk_buff *ixgbe_transform_rsc_queue(struct sk_buff *skb)
{
	unsigned int frag_list_size = 0;
	unsigned int skb_cnt = 1;

	while (skb->prev) {
		struct sk_buff *prev = skb->prev;
		frag_list_size += skb->len;
		skb->prev = NULL;
		skb = prev;
		skb_cnt++;
	}

	skb_shinfo(skb)->frag_list = skb->next;
	skb->next = NULL;
	skb->len += frag_list_size;
	skb->data_len += frag_list_size;
	skb->truesize += frag_list_size;
	IXGBE_RSC_CB(skb)->skb_cnt = skb_cnt;

	return skb;
}

static inline bool ixgbe_get_rsc_state(union ixgbe_adv_rx_desc *rx_desc)
{
	return !!(le32_to_cpu(rx_desc->wb.lower.lo_dword.data) &
		IXGBE_RXDADV_RSCCNT_MASK);
}

static bool ixgbe_clean_rx_irq(struct ixgbe_q_vector *q_vector,
			       struct ixgbe_ring *rx_ring,
			       int budget,struct napi_struct *napi)
{
	struct ixgbe_adapter *adapter = q_vector->adapter;
	union ixgbe_adv_rx_desc *rx_desc, *next_rxd;
	struct ixgbe_rx_buffer *rx_buffer_info, *next_buffer;
	struct sk_buff *skb;
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	const int current_node = numa_node_id();
#ifdef IXGBE_FCOE
	int ddp_bytes = 0;
#endif /* IXGBE_FCOE */

//	if(check_if_primary()==0)
//		printk("%s:called q_no %d budget %d\n",__func__,rx_ring->reg_idx,budget);

	u32 staterr;
	u16 i;
	u16 cleaned_count = 0;
	bool pkt_is_rsc = false;

	i = rx_ring->next_to_clean;
	rx_desc = IXGBE_RX_DESC_ADV(rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
//	printk("%s: i = %d staterr %x q_no %d\n",__func__,i,staterr,rx_ring->reg_idx);

	while (staterr & IXGBE_RXD_STAT_DD) {
		u32 upper_len = 0;
//		printk("%s: inside loop \n",__func__);
		
		rmb(); /* read descriptor and rx_buffer_info after status DD */

		rx_buffer_info = &rx_ring->rx_buffer_info[i];

		skb = rx_buffer_info->skb;
		rx_buffer_info->skb = NULL;
		prefetch(skb->data);
		//if(check_if_primary()==0)
		//	return;


		if (ring_is_rsc_enabled(rx_ring))
			pkt_is_rsc = ixgbe_get_rsc_state(rx_desc);

		/* linear means we are building an skb from multiple pages */
		if (!skb_is_nonlinear(skb)) {
			u16 hlen;
			if (pkt_is_rsc &&
			    !(staterr & IXGBE_RXD_STAT_EOP) &&
			    !skb->prev) {
				/*
				 * When HWRSC is enabled, delay unmapping
				 * of the first packet. It carries the
				 * header information, HW may still
				 * access the header after the writeback.
				 * Only unmap it when EOP is reached
				 */
				IXGBE_RSC_CB(skb)->delay_unmap = true;
				IXGBE_RSC_CB(skb)->dma = rx_buffer_info->dma;
			} else {
				dma_unmap_single(rx_ring->dev,
						 rx_buffer_info->dma,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);
			}
			rx_buffer_info->dma = 0;

			if (ring_is_ps_enabled(rx_ring)) {
				hlen = ixgbe_get_hlen(rx_desc);
				upper_len = le16_to_cpu(rx_desc->wb.upper.length);
			} else {
				hlen = le16_to_cpu(rx_desc->wb.upper.length);
			}

			skb_put(skb, hlen);
		} else {
			/* assume packet split since header is unmapped */
			upper_len = le16_to_cpu(rx_desc->wb.upper.length);
		}

		if (upper_len) {
			dma_unmap_page(rx_ring->dev,
				       rx_buffer_info->page_dma,
				       PAGE_SIZE / 2,
				       DMA_FROM_DEVICE);
			rx_buffer_info->page_dma = 0;
			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
					   rx_buffer_info->page,
					   rx_buffer_info->page_offset,
					   upper_len);

			if ((page_count(rx_buffer_info->page) == 1) &&
			    (page_to_nid(rx_buffer_info->page) == current_node))
				get_page(rx_buffer_info->page);
			else
				rx_buffer_info->page = NULL;

			skb->len += upper_len;
			skb->data_len += upper_len;
			skb->truesize += PAGE_SIZE / 2;
		}

		i++;
		if (i == rx_ring->count)
			i = 0;

		next_rxd = IXGBE_RX_DESC_ADV(rx_ring, i);
		prefetch(next_rxd);
		cleaned_count++;

		if (pkt_is_rsc) {
			u32 nextp = (staterr & IXGBE_RXDADV_NEXTP_MASK) >>
				     IXGBE_RXDADV_NEXTP_SHIFT;
			next_buffer = &rx_ring->rx_buffer_info[nextp];
		} else {
			next_buffer = &rx_ring->rx_buffer_info[i];
		}

		if (!(staterr & IXGBE_RXD_STAT_EOP)) {
			if (ring_is_ps_enabled(rx_ring)) {
				rx_buffer_info->skb = next_buffer->skb;
				rx_buffer_info->dma = next_buffer->dma;
				next_buffer->skb = skb;
				next_buffer->dma = 0;
			} else {
				skb->next = next_buffer->skb;
				skb->next->prev = skb;
			}
			rx_ring->rx_stats.non_eop_descs++;
			goto next_desc;
		}

		if (skb->prev) {
			skb = ixgbe_transform_rsc_queue(skb);
			/* if we got here without RSC the packet is invalid */
			if (!pkt_is_rsc) {
				__pskb_trim(skb, 0);
				rx_buffer_info->skb = skb;
				goto next_desc;
			}
		}

		if (ring_is_rsc_enabled(rx_ring)) {
			if (IXGBE_RSC_CB(skb)->delay_unmap) {
				dma_unmap_single(rx_ring->dev,
						 IXGBE_RSC_CB(skb)->dma,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);
				IXGBE_RSC_CB(skb)->dma = 0;
				IXGBE_RSC_CB(skb)->delay_unmap = false;
			}
		}
		if (pkt_is_rsc) {
			if (ring_is_ps_enabled(rx_ring))
				rx_ring->rx_stats.rsc_count +=
					skb_shinfo(skb)->nr_frags;
			else
				rx_ring->rx_stats.rsc_count +=
					IXGBE_RSC_CB(skb)->skb_cnt;
			rx_ring->rx_stats.rsc_flush++;
		}

		/* ERR_MASK will only have valid bits if EOP set */
		if (unlikely(staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK)) {
			dev_kfree_skb_any(skb);
			goto next_desc;
		}

		ixgbe_rx_checksum(adapter, rx_desc, skb, staterr);
		if (adapter->netdev->features & NETIF_F_RXHASH)
			ixgbe_rx_hash(rx_desc, skb);

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;
		total_rx_packets++;

		skb->protocol = eth_type_trans(skb, rx_ring->netdev);
#ifdef IXGBE_FCOE
		/* if ddp, not passing to ULD unless for FCP_RSP or error */
		if (ixgbe_rx_is_fcoe(adapter, rx_desc)) {
			ddp_bytes = ixgbe_fcoe_ddp(adapter, rx_desc, skb,
						   staterr);
			if (!ddp_bytes) {
				dev_kfree_skb_any(skb);
				goto next_desc;
			}
		}
#endif /* IXGBE_FCOE */

	//	if(check_if_primary()==1){
			ixgbe_receive_skb(q_vector, skb, staterr, rx_ring, rx_desc,napi);
	//	}
	
		budget--;
next_desc:
		rx_desc->wb.upper.status_error = 0;

		if (!budget)
			break;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IXGBE_RX_BUFFER_WRITE) {
			ixgbe_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	}

	rx_ring->next_to_clean = i;
	cleaned_count = ixgbe_desc_unused(rx_ring);

	if (cleaned_count)
		ixgbe_alloc_rx_buffers(rx_ring, cleaned_count);

#ifdef IXGBE_FCOE
	/* include DDPed FCoE data */
	if (ddp_bytes > 0) {
		unsigned int mss;

		mss = rx_ring->netdev->mtu - sizeof(struct fcoe_hdr) -
			sizeof(struct fc_frame_header) -
			sizeof(struct fcoe_crc_eof);
		if (mss > 512)
			mss &= ~511;
		total_rx_bytes += ddp_bytes;
		total_rx_packets += DIV_ROUND_UP(ddp_bytes, mss);
	}
#endif /* IXGBE_FCOE */

	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->stats.packets += total_rx_packets;
	rx_ring->stats.bytes += total_rx_bytes;
	u64_stats_update_end(&rx_ring->syncp);
	q_vector->rx.total_packets += total_rx_packets;
	q_vector->rx.total_bytes += total_rx_bytes;
//	if(check_if_primary()==0)
	//	printk("%s: going out\n",__func__);
	return !!budget;
}
EXPORT_SYMBOL(ixgbe_clean_rx_irq);

/**
 * ixgbe_configure_msix - Configure MSI-X hardware
 * @adapter: board private structure
 *
 * ixgbe_configure_msix sets up the hardware to properly generate MSI-X
 * interrupts.
 **/
static void ixgbe_configure_msix(struct ixgbe_adapter *adapter)
{
	struct ixgbe_q_vector *q_vector;
	int q_vectors, v_idx;
	u32 mask;

	q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* Populate MSIX to EITR Select */
	if (adapter->num_vfs > 32) {
		u32 eitrsel = (1 << (adapter->num_vfs - 32)) - 1;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITRSEL, eitrsel);
	}

	/*
	 * Populate the IVAR table and set the ITR values to the
	 * corresponding register.
	 */
	for (v_idx = 0; v_idx < q_vectors; v_idx++) {
		struct ixgbe_ring *ring;
		q_vector = adapter->q_vector[v_idx];

		for (ring = q_vector->rx.ring; ring != NULL; ring = ring->next)
			ixgbe_set_ivar(adapter, 0, ring->reg_idx, v_idx);

		for (ring = q_vector->tx.ring; ring != NULL; ring = ring->next)
			ixgbe_set_ivar(adapter, 1, ring->reg_idx, v_idx);

		if (q_vector->tx.ring && !q_vector->rx.ring) {
			/* tx only vector */
			if (adapter->tx_itr_setting == 1)
				q_vector->itr = IXGBE_10K_ITR;
			else
				q_vector->itr = adapter->tx_itr_setting;
		} else {
			/* rx or rx/tx vector */
			if (adapter->rx_itr_setting == 1)
				q_vector->itr = IXGBE_20K_ITR;
			else
				q_vector->itr = adapter->rx_itr_setting;
		}

		ixgbe_write_eitr(q_vector);
	}

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		ixgbe_set_ivar(adapter, -1, IXGBE_IVAR_OTHER_CAUSES_INDEX,
			       v_idx);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		ixgbe_set_ivar(adapter, -1, 1, v_idx);
		break;
	default:
		break;
	}
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITR(v_idx), 1950);

	/* set up to autoclear timer, and the vectors */
	mask = IXGBE_EIMS_ENABLE_MASK;
	mask &= ~(IXGBE_EIMS_OTHER |
		  IXGBE_EIMS_MAILBOX |
		  IXGBE_EIMS_LSC);

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIAC, mask);
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

/**
 * ixgbe_update_itr - update the dynamic ITR value based on statistics
 * @q_vector: structure containing interrupt and ring information
 * @ring_container: structure containing ring performance data
 *
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.
 *      this functionality is controlled by the InterruptThrottleRate module
 *      parameter (see ixgbe_param.c)
 **/
static void ixgbe_update_itr(struct ixgbe_q_vector *q_vector,
			     struct ixgbe_ring_container *ring_container)
{
	u64 bytes_perint;
	struct ixgbe_adapter *adapter = q_vector->adapter;
	int bytes = ring_container->total_bytes;
	int packets = ring_container->total_packets;
	u32 timepassed_us;
	u8 itr_setting = ring_container->itr;

	if (packets == 0)
		return;

	/* simple throttlerate management
	 *    0-20MB/s lowest (100000 ints/s)
	 *   20-100MB/s low   (20000 ints/s)
	 *  100-1249MB/s bulk (8000 ints/s)
	 */
	/* what was last interrupt timeslice? */
	timepassed_us = q_vector->itr >> 2;
	bytes_perint = bytes / timepassed_us; /* bytes/usec */

	switch (itr_setting) {
	case lowest_latency:
		if (bytes_perint > adapter->eitr_low)
			itr_setting = low_latency;
		break;
	case low_latency:
		if (bytes_perint > adapter->eitr_high)
			itr_setting = bulk_latency;
		else if (bytes_perint <= adapter->eitr_low)
			itr_setting = lowest_latency;
		break;
	case bulk_latency:
		if (bytes_perint <= adapter->eitr_high)
			itr_setting = low_latency;
		break;
	}

	/* clear work counters since we have the values we need */
	ring_container->total_bytes = 0;
	ring_container->total_packets = 0;

	/* write updated itr to ring container */
	ring_container->itr = itr_setting;
}

/**
 * ixgbe_write_eitr - write EITR register in hardware specific way
 * @q_vector: structure containing interrupt and ring information
 *
 * This function is made to be called by ethtool and by the driver
 * when it needs to update EITR registers at runtime.  Hardware
 * specific quirks/differences are taken care of here.
 */
void ixgbe_write_eitr(struct ixgbe_q_vector *q_vector)
{
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct ixgbe_hw *hw = &adapter->hw;
	int v_idx = q_vector->v_idx;
	u32 itr_reg = q_vector->itr;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		/* must write high and low 16 bits to reset counter */
		itr_reg |= (itr_reg << 16);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		/*
		 * set the WDIS bit to not clear the timer bits and cause an
		 * immediate assertion of the interrupt
		 */
		itr_reg |= IXGBE_EITR_CNT_WDIS;
		break;
	default:
		break;
	}
	IXGBE_WRITE_REG(hw, IXGBE_EITR(v_idx), itr_reg);
}


void ixgbe_set_itr(struct ixgbe_q_vector *q_vector)
{
	u32 new_itr = q_vector->itr;
	u8 current_itr;

	ixgbe_update_itr(q_vector, &q_vector->tx);
	ixgbe_update_itr(q_vector, &q_vector->rx);

	current_itr = max(q_vector->rx.itr, q_vector->tx.itr);

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = IXGBE_100K_ITR;
		break;
	case low_latency:
		new_itr = IXGBE_20K_ITR;
		break;
	case bulk_latency:
		new_itr = IXGBE_8K_ITR;
		break;
	default:
		break;
	}

	if (new_itr != q_vector->itr) {
		/* do an exponential smoothing */
		new_itr = (10 * new_itr * q_vector->itr) /
			  ((9 * new_itr) + q_vector->itr);

		/* save the algorithm value here */
		q_vector->itr = new_itr & IXGBE_MAX_EITR;

		ixgbe_write_eitr(q_vector);
	}
}

EXPORT_SYMBOL(ixgbe_set_itr);
//EXPORT_SYMBOL(ixgbe_irq_enable_queues);

/**
 * ixgbe_check_overtemp_subtask - check for over tempurature
 * @adapter: pointer to adapter
 **/
static void ixgbe_check_overtemp_subtask(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr = adapter->interrupt_event;

	if (test_bit(__IXGBE_DOWN, &adapter->state))
		return;

	if (!(adapter->flags2 & IXGBE_FLAG2_TEMP_SENSOR_CAPABLE) &&
	    !(adapter->flags2 & IXGBE_FLAG2_TEMP_SENSOR_EVENT))
		return;

	adapter->flags2 &= ~IXGBE_FLAG2_TEMP_SENSOR_EVENT;

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_T3_LOM:
		/*
		 * Since the warning interrupt is for both ports
		 * we don't have to check if:
		 *  - This interrupt wasn't for our port.
		 *  - We may have missed the interrupt so always have to
		 *    check if we  got a LSC
		 */
		if (!(eicr & IXGBE_EICR_GPI_SDP0) &&
		    !(eicr & IXGBE_EICR_LSC))
			return;

		if (!(eicr & IXGBE_EICR_LSC) && hw->mac.ops.check_link) {
			u32 autoneg;
			bool link_up = false;

			hw->mac.ops.check_link(hw, &autoneg, &link_up, false);

			if (link_up)
				return;
		}

		/* Check if this is not due to overtemp */
		if (hw->phy.ops.check_overtemp(hw) != IXGBE_ERR_OVERTEMP)
			return;

		break;
	default:
		if (!(eicr & IXGBE_EICR_GPI_SDP0))
			return;
		break;
	}
	e_crit(drv,
	       "Network adapter has been stopped because it has over heated. "
	       "Restart the computer. If the problem persists, "
	       "power off the system and replace the adapter\n");

	adapter->interrupt_event = 0;
}

static void ixgbe_check_fan_failure(struct ixgbe_adapter *adapter, u32 eicr)
{
	struct ixgbe_hw *hw = &adapter->hw;

	if ((adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) &&
	    (eicr & IXGBE_EICR_GPI_SDP1)) {
		e_crit(probe, "Fan has stopped, replace the adapter\n");
		/* write to clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP1);
	}
}

static void ixgbe_check_overtemp_event(struct ixgbe_adapter *adapter, u32 eicr)
{
	if (!(adapter->flags2 & IXGBE_FLAG2_TEMP_SENSOR_CAPABLE))
		return;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
		/*
		 * Need to check link state so complete overtemp check
		 * on service task
		 */
		if (((eicr & IXGBE_EICR_GPI_SDP0) || (eicr & IXGBE_EICR_LSC)) &&
		    (!test_bit(__IXGBE_DOWN, &adapter->state))) {
			adapter->interrupt_event = eicr;
			adapter->flags2 |= IXGBE_FLAG2_TEMP_SENSOR_EVENT;
			ixgbe_service_event_schedule(adapter);
			return;
		}
		return;
	case ixgbe_mac_X540:
		if (!(eicr & IXGBE_EICR_TS))
			return;
		break;
	default:
		return;
	}

	e_crit(drv,
	       "Network adapter has been stopped because it has over heated. "
	       "Restart the computer. If the problem persists, "
	       "power off the system and replace the adapter\n");
}

static void ixgbe_check_sfp_event(struct ixgbe_adapter *adapter, u32 eicr)
{
	struct ixgbe_hw *hw = &adapter->hw;

	if (eicr & IXGBE_EICR_GPI_SDP2) {
		/* Clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP2);
		if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
			adapter->flags2 |= IXGBE_FLAG2_SFP_NEEDS_RESET;
			ixgbe_service_event_schedule(adapter);
		}
	}

	if (eicr & IXGBE_EICR_GPI_SDP1) {
		/* Clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP1);
		if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
			adapter->flags |= IXGBE_FLAG_NEED_LINK_CONFIG;
			ixgbe_service_event_schedule(adapter);
		}
	}
}

static void ixgbe_check_lsc(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	adapter->lsc_int++;
	adapter->flags |= IXGBE_FLAG_NEED_LINK_UPDATE;
	adapter->link_check_timeout = jiffies;
	if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
		IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EIMC_LSC);
		IXGBE_WRITE_FLUSH(hw);
		ixgbe_service_event_schedule(adapter);
	}
}


static inline void ixgbe_irq_enable_queues(struct ixgbe_adapter *adapter,
					u64 qmask)
{
	u32 mask;
	struct ixgbe_hw *hw = &adapter->hw;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		mask = (IXGBE_EIMS_RTX_QUEUE & qmask);
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, mask);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		mask = (qmask & 0xFFFFFFFF);
		if (mask)
			IXGBE_WRITE_REG(hw, IXGBE_EIMS_EX(0), mask);
		mask = (qmask >> 32);
		if (mask)
			IXGBE_WRITE_REG(hw, IXGBE_EIMS_EX(1), mask);
		break;
	default:
		break;
	}
	/* skip the flush */
}



static inline void ixgbe_irq_disable_queues(struct ixgbe_adapter *adapter,
					    u64 qmask)
{
	u32 mask;
	struct ixgbe_hw *hw = &adapter->hw;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		mask = (IXGBE_EIMS_RTX_QUEUE & qmask);
		IXGBE_WRITE_REG(hw, IXGBE_EIMC, mask);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		mask = (qmask & 0xFFFFFFFF);
		if (mask)
			IXGBE_WRITE_REG(hw, IXGBE_EIMC_EX(0), mask);
		mask = (qmask >> 32);
		if (mask)
			IXGBE_WRITE_REG(hw, IXGBE_EIMC_EX(1), mask);
		break;
	default:
		break;
	}
	/* skip the flush */
}

/**
 * ixgbe_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_enable(struct ixgbe_adapter *adapter, bool queues,
				    bool flush)
{
	u32 mask = (IXGBE_EIMS_ENABLE_MASK & ~IXGBE_EIMS_RTX_QUEUE);

	/* don't reenable LSC while waiting for link */
	if (adapter->flags & IXGBE_FLAG_NEED_LINK_UPDATE)
		mask &= ~IXGBE_EIMS_LSC;

	if (adapter->flags2 & IXGBE_FLAG2_TEMP_SENSOR_CAPABLE)
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
			mask |= IXGBE_EIMS_GPI_SDP0;
			break;
		case ixgbe_mac_X540:
			mask |= IXGBE_EIMS_TS;
			break;
		default:
			break;
		}
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE)
		mask |= IXGBE_EIMS_GPI_SDP1;
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
		mask |= IXGBE_EIMS_GPI_SDP1;
		mask |= IXGBE_EIMS_GPI_SDP2;
	case ixgbe_mac_X540:
		mask |= IXGBE_EIMS_ECC;
		mask |= IXGBE_EIMS_MAILBOX;
		break;
	default:
		break;
	}
	if ((adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) &&
	    !(adapter->flags2 & IXGBE_FLAG2_FDIR_REQUIRES_REINIT))
		mask |= IXGBE_EIMS_FLOW_DIR;

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, mask);
	if (queues)
		ixgbe_irq_enable_queues(adapter, ~0);
	if (flush)
		IXGBE_WRITE_FLUSH(&adapter->hw);
}

static irqreturn_t ixgbe_msix_other(int irq, void *data)
{
	struct ixgbe_adapter *adapter = data;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr;

	/*
	 * Workaround for Silicon errata.  Use clear-by-write instead
	 * of clear-by-read.  Reading with EICS will return the
	 * interrupt causes without clearing, which later be done
	 * with the write to EICR.
	 */
	eicr = IXGBE_READ_REG(hw, IXGBE_EICS);
	IXGBE_WRITE_REG(hw, IXGBE_EICR, eicr);

	if (eicr & IXGBE_EICR_LSC)
		ixgbe_check_lsc(adapter);

	if (eicr & IXGBE_EICR_MAILBOX)
		ixgbe_msg_task(adapter);

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (eicr & IXGBE_EICR_ECC)
			e_info(link, "Received unrecoverable ECC Err, please "
			       "reboot\n");
		/* Handle Flow Director Full threshold interrupt */
		if (eicr & IXGBE_EICR_FLOW_DIR) {
			int reinit_count = 0;
			int i;
			for (i = 0; i < adapter->num_tx_queues; i++) {
				struct ixgbe_ring *ring = adapter->tx_ring[i];
				if (test_and_clear_bit(__IXGBE_TX_FDIR_INIT_DONE,
						       &ring->state))
					reinit_count++;
			}
			if (reinit_count) {
				/* no more flow director interrupts until after init */
				IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EIMC_FLOW_DIR);
				adapter->flags2 |= IXGBE_FLAG2_FDIR_REQUIRES_REINIT;
				ixgbe_service_event_schedule(adapter);
			}
		}
		ixgbe_check_sfp_event(adapter, eicr);
		ixgbe_check_overtemp_event(adapter, eicr);
		break;
	default:
		break;
	}

	ixgbe_check_fan_failure(adapter, eicr);

	/* re-enable the original interrupt state, no lsc, no queues */
	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_enable(adapter, false, false);

	return IRQ_HANDLED;
}

static irqreturn_t ixgbe_msix_clean_rings(int irq, void *data)
{
	struct ixgbe_q_vector *q_vector = data;

	/* EIAM disabled interrupts (on this vector) for us */

	if (q_vector->rx.ring || q_vector->tx.ring)
		napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

static inline void map_vector_to_rxq(struct ixgbe_adapter *a, int v_idx,
				     int r_idx)
{
	struct ixgbe_q_vector *q_vector = a->q_vector[v_idx];
	struct ixgbe_ring *rx_ring = a->rx_ring[r_idx];

	rx_ring->q_vector = q_vector;
	rx_ring->next = q_vector->rx.ring;
	q_vector->rx.ring = rx_ring;
	q_vector->rx.count++;
}

static inline void map_vector_to_txq(struct ixgbe_adapter *a, int v_idx,
				     int t_idx)
{
	struct ixgbe_q_vector *q_vector = a->q_vector[v_idx];
	struct ixgbe_ring *tx_ring = a->tx_ring[t_idx];

	tx_ring->q_vector = q_vector;
	tx_ring->next = q_vector->tx.ring;
	q_vector->tx.ring = tx_ring;
	q_vector->tx.count++;
	q_vector->tx.work_limit = a->tx_work_limit;
}

/**
 * ixgbe_map_rings_to_vectors - Maps descriptor rings to vectors
 * @adapter: board private structure to initialize
 *
 * This function maps descriptor rings to the queue-specific vectors
 * we were allotted through the MSI-X enabling code.  Ideally, we'd have
 * one vector per ring/queue, but on a constrained vector budget, we
 * group the rings as "efficiently" as possible.  You would add new
 * mapping configurations in here.
 **/
static void ixgbe_map_rings_to_vectors(struct ixgbe_adapter *adapter)
{
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	int rxr_remaining = adapter->num_rx_queues, rxr_idx = 0;
	int txr_remaining = adapter->num_tx_queues, txr_idx = 0;
	int v_start = 0;

	/* only one q_vector if MSI-X is disabled. */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	/*
	 * If we don't have enough vectors for a 1-to-1 mapping, we'll have to
	 * group them so there are multiple queues per vector.
	 *
	 * Re-adjusting *qpv takes care of the remainder.
	 */
	for (; v_start < q_vectors && rxr_remaining; v_start++) {
		int rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_start);
		for (; rqpv; rqpv--, rxr_idx++, rxr_remaining--)
			map_vector_to_rxq(adapter, v_start, rxr_idx);
	}

	/*
	 * If there are not enough q_vectors for each ring to have it's own
	 * vector then we must pair up Rx/Tx on a each vector
	 */
	if ((v_start + txr_remaining) > q_vectors)
		v_start = 0;

	for (; v_start < q_vectors && txr_remaining; v_start++) {
		int tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_start);
		for (; tqpv; tqpv--, txr_idx++, txr_remaining--)
			map_vector_to_txq(adapter, v_start, txr_idx);
	}
}

/**
 * ixgbe_request_msix_irqs - Initialize MSI-X interrupts
 * @adapter: board private structure
 *
 * ixgbe_request_msix_irqs allocates MSI-X vectors and requests
 * interrupts from the kernel.
 **/
static int ixgbe_request_msix_irqs(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	int vector, err;
	int ri = 0, ti = 0;

	for (vector = 0; vector < q_vectors; vector++) {
		struct ixgbe_q_vector *q_vector = adapter->q_vector[vector];
		struct msix_entry *entry = &adapter->msix_entries[vector];

		if (q_vector->tx.ring && q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", netdev->name, "TxRx", ri++);
			ti++;
		} else if (q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", netdev->name, "rx", ri++);
		} else if (q_vector->tx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", netdev->name, "tx", ti++);
		} else {
			/* skip this unused q_vector */
			continue;
		}
		err = request_irq(entry->vector, &ixgbe_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		printk("%s:entry-vector %d entry->entry %d\n",__func__,entry->vector,entry->entry);
		if (err) {
			e_err(probe, "request_irq failed for MSIX interrupt "
			      "Error: %d\n", err);
			goto free_queue_irqs;
		}
		/* If Flow Director is enabled, set interrupt affinity */
		if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) {
			/* assign the mask for this irq */
			irq_set_affinity_hint(entry->vector,
					      q_vector->affinity_mask);
		}
	}

	err = request_irq(adapter->msix_entries[vector].vector,
			  ixgbe_msix_other, 0, netdev->name, adapter);
	if (err) {
		e_err(probe, "request_irq for msix_lsc failed: %d\n", err);
		goto free_queue_irqs;
	}

	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		irq_set_affinity_hint(adapter->msix_entries[vector].vector,
				      NULL);
		free_irq(adapter->msix_entries[vector].vector,
			 adapter->q_vector[vector]);
	}
	adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
	pci_disable_msix(adapter->pdev);
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
	return err;
}

/**
 * ixgbe_intr - legacy mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t ixgbe_intr(int irq, void *data)
{
	struct ixgbe_adapter *adapter = data;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_q_vector *q_vector = adapter->q_vector[0];
	u32 eicr;

	/*
	 * Workaround for silicon errata on 82598.  Mask the interrupts
	 * before the read of EICR.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_IRQ_CLEAR_MASK);

	/* for NAPI, using EIAM to auto-mask tx/rx interrupt bits on read
	 * therefore no explict interrupt disable is necessary */
	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);
	if (!eicr) {
		/*
		 * shared interrupt alert!
		 * make sure interrupts are enabled because the read will
		 * have disabled interrupts due to EIAM
		 * finish the workaround of silicon errata on 82598.  Unmask
		 * the interrupt that we masked before the EICR read.
		 */
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			ixgbe_irq_enable(adapter, true, true);
		return IRQ_NONE;	/* Not our interrupt */
	}

	if (eicr & IXGBE_EICR_LSC)
		ixgbe_check_lsc(adapter);

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		ixgbe_check_sfp_event(adapter, eicr);
		/* Fall through */
	case ixgbe_mac_X540:
		if (eicr & IXGBE_EICR_ECC)
			e_info(link, "Received unrecoverable ECC err, please "
				     "reboot\n");
		ixgbe_check_overtemp_event(adapter, eicr);
		break;
	default:
		break;
	}

	ixgbe_check_fan_failure(adapter, eicr);

	if (napi_schedule_prep(&(q_vector->napi))) {
		/* would disable interrupts here but EIAM disabled it */
		__napi_schedule(&(q_vector->napi));
	}

	/*
	 * re-enable link(maybe) and non-queue interrupts, no flush.
	 * ixgbe_poll will re-enable the queue interrupts
	 */

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_enable(adapter, false, false);

	return IRQ_HANDLED;
}

static inline void ixgbe_reset_q_vectors(struct ixgbe_adapter *adapter)
{
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	int i;

	/* legacy and MSI only use one vector */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i]->q_vector = NULL;
		adapter->rx_ring[i]->next = NULL;
	}
	for (i = 0; i < adapter->num_tx_queues; i++) {
		adapter->tx_ring[i]->q_vector = NULL;
		adapter->tx_ring[i]->next = NULL;
	}

	for (i = 0; i < q_vectors; i++) {
		struct ixgbe_q_vector *q_vector = adapter->q_vector[i];
		memset(&q_vector->rx, 0, sizeof(struct ixgbe_ring_container));
		memset(&q_vector->tx, 0, sizeof(struct ixgbe_ring_container));
	}
}

/**
 * ixgbe_request_irq - initialize interrupts
 * @adapter: board private structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int ixgbe_request_irq(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	/* map all of the rings to the q_vectors */
	ixgbe_map_rings_to_vectors(adapter);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		err = ixgbe_request_msix_irqs(adapter);
	else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED)
		err = request_irq(adapter->pdev->irq, ixgbe_intr, 0,
				  netdev->name, adapter);
	else
		err = request_irq(adapter->pdev->irq, ixgbe_intr, IRQF_SHARED,
				  netdev->name, adapter);

	if (err) {
		e_err(probe, "request_irq failed, Error %d\n", err);

		/* place q_vectors and rings back into a known good state */
		ixgbe_reset_q_vectors(adapter);
	}

	return err;
}

static void ixgbe_free_irq(struct ixgbe_adapter *adapter)
{
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int i, q_vectors;

		q_vectors = adapter->num_msix_vectors;
		i = q_vectors - 1;
		free_irq(adapter->msix_entries[i].vector, adapter);
		i--;

		for (; i >= 0; i--) {
			/* free only the irqs that were actually requested */
			if (!adapter->q_vector[i]->rx.ring &&
			    !adapter->q_vector[i]->tx.ring)
				continue;

			/* clear the affinity_mask in the IRQ descriptor */
			irq_set_affinity_hint(adapter->msix_entries[i].vector,
					      NULL);

			free_irq(adapter->msix_entries[i].vector,
				 adapter->q_vector[i]);
		}
	} else {
		free_irq(adapter->pdev->irq, adapter);
	}

	/* clear q_vector state information */
	ixgbe_reset_q_vectors(adapter);
}

/**
 * ixgbe_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_disable(struct ixgbe_adapter *adapter)
{
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, ~0);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, 0xFFFF0000);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC_EX(0), ~0);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC_EX(1), ~0);
		break;
	default:
		break;
	}
	IXGBE_WRITE_FLUSH(&adapter->hw);
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int i;
		for (i = 0; i < adapter->num_msix_vectors; i++)
			synchronize_irq(adapter->msix_entries[i].vector);
	} else {
		synchronize_irq(adapter->pdev->irq);
	}
}

/**
 * ixgbe_configure_msi_and_legacy - Initialize PIN (INTA...) and MSI interrupts
 *
 **/
static void ixgbe_configure_msi_and_legacy(struct ixgbe_adapter *adapter)
{
	struct ixgbe_q_vector *q_vector = adapter->q_vector[0];

	/* rx/tx vector */
	if (adapter->rx_itr_setting == 1)
		q_vector->itr = IXGBE_20K_ITR;
	else
		q_vector->itr = adapter->rx_itr_setting;

	ixgbe_write_eitr(q_vector);

	ixgbe_set_ivar(adapter, 0, 0, 0);
	ixgbe_set_ivar(adapter, 1, 0, 0);

	e_info(hw, "Legacy interrupt IVAR setup done\n");
}

/**
 * ixgbe_configure_tx_ring - Configure 8259x Tx ring after Reset
 * @adapter: board private structure
 * @ring: structure containing ring specific data
 *
 * Configure the Tx descriptor ring after a reset.
 **/
void ixgbe_configure_tx_ring(struct ixgbe_adapter *adapter,
			     struct ixgbe_ring *ring,int idx)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u64 tdba = ring->dma;
	int wait_loop = 10;
	u32 txdctl = IXGBE_TXDCTL_ENABLE;
	u8 reg_idx = ring->reg_idx;

	/* disable queue to avoid issues while updating state */
	IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(reg_idx), 0);
	IXGBE_WRITE_FLUSH(hw);

	IXGBE_WRITE_REG(hw, IXGBE_TDBAL(reg_idx),
			(tdba & DMA_BIT_MASK(32)));
	IXGBE_WRITE_REG(hw, IXGBE_TDBAH(reg_idx), (tdba >> 32));
	IXGBE_WRITE_REG(hw, IXGBE_TDLEN(reg_idx),
			ring->count * sizeof(union ixgbe_adv_tx_desc));
	IXGBE_WRITE_REG(hw, IXGBE_TDH(reg_idx), 0);
	IXGBE_WRITE_REG(hw, IXGBE_TDT(reg_idx), 0);
	ring->tail = hw->hw_addr + IXGBE_TDT(reg_idx);
	
	printk("%s: my_idx %d reg_idx %d ring->tail %p hw->hw_addr %p IXGBE_TDT %x\n",__func__,idx,reg_idx,ring->tail,hw->hw_addr,IXGBE_TDT(reg_idx));

	/*
	 * set WTHRESH to encourage burst writeback, it should not be set
	 * higher than 1 when ITR is 0 as it could cause false TX hangs
	 *
	 * In order to avoid issues WTHRESH + PTHRESH should always be equal
	 * to or less than the number of on chip descriptors, which is
	 * currently 40.
	 */
	if (!adapter->tx_itr_setting || !adapter->rx_itr_setting)
		txdctl |= (1 << 16);	/* WTHRESH = 1 */
	else
		txdctl |= (8 << 16);	/* WTHRESH = 8 */

	/* PTHRESH=32 is needed to avoid a Tx hang with DFP enabled. */
	txdctl |= (1 << 8) |	/* HTHRESH = 1 */
		   32;		/* PTHRESH = 32 */

	/* reinitialize flowdirector state */
	if ((adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) &&
	    adapter->atr_sample_rate) {
		ring->atr_sample_rate = adapter->atr_sample_rate;
		ring->atr_count = 0;
		set_bit(__IXGBE_TX_FDIR_INIT_DONE, &ring->state);
	} else {
		ring->atr_sample_rate = 0;
	}

	clear_bit(__IXGBE_HANG_CHECK_ARMED, &ring->state);

	/* enable queue */
	IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(reg_idx), txdctl);

	/* TXDCTL.EN will return 0 on 82598 if link is down, so skip it */
	if (hw->mac.type == ixgbe_mac_82598EB &&
	    !(IXGBE_READ_REG(hw, IXGBE_LINKS) & IXGBE_LINKS_UP))
		return;

	/* poll to verify queue is enabled */
	do {
		usleep_range(1000, 2000);
		txdctl = IXGBE_READ_REG(hw, IXGBE_TXDCTL(reg_idx));
	} while (--wait_loop && !(txdctl & IXGBE_TXDCTL_ENABLE));
	if (!wait_loop)
		e_err(drv, "Could not enable Tx Queue %d\n", reg_idx);
}

static void ixgbe_setup_mtqc(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rttdcs;
	u32 reg;
	u8 tcs = netdev_get_num_tc(adapter->netdev);

	if (hw->mac.type == ixgbe_mac_82598EB)
		return;

	/* disable the arbiter while setting MTQC */
	rttdcs = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
	rttdcs |= IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);

	/* set transmit pool layout */
	switch (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
	case (IXGBE_FLAG_SRIOV_ENABLED):
		IXGBE_WRITE_REG(hw, IXGBE_MTQC,
				(IXGBE_MTQC_VT_ENA | IXGBE_MTQC_64VF));
		break;
	default:
		if (!tcs)
			reg = IXGBE_MTQC_64Q_1PB;
		else if (tcs <= 4)
			reg = IXGBE_MTQC_RT_ENA | IXGBE_MTQC_4TC_4TQ;
		else
			reg = IXGBE_MTQC_RT_ENA | IXGBE_MTQC_8TC_8TQ;

		IXGBE_WRITE_REG(hw, IXGBE_MTQC, reg);

		/* Enable Security TX Buffer IFG for multiple pb */
		if (tcs) {
			reg = IXGBE_READ_REG(hw, IXGBE_SECTXMINIFG);
			reg |= IXGBE_SECTX_DCB;
			IXGBE_WRITE_REG(hw, IXGBE_SECTXMINIFG, reg);
		}
		break;
	}

	/* re-enable the arbiter */
	rttdcs &= ~IXGBE_RTTDCS_ARBDIS;
	IXGBE_WRITE_REG(hw, IXGBE_RTTDCS, rttdcs);
}

/**
 * ixgbe_configure_tx - Configure 8259x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void ixgbe_configure_tx(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 dmatxctl;
	u32 i;

	ixgbe_setup_mtqc(adapter);

	if (hw->mac.type != ixgbe_mac_82598EB) {
		/* DMATXCTL.EN must be before Tx queues are enabled */
		dmatxctl = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
		dmatxctl |= IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, dmatxctl);
	}

	/* Setup the HW Tx Head and Tail descriptor pointers */
	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_configure_tx_ring(adapter, adapter->tx_ring[i],i);
}

#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT 2

static void ixgbe_configure_srrctl(struct ixgbe_adapter *adapter,
				   struct ixgbe_ring *rx_ring)
{
	u32 srrctl;
	u8 reg_idx = rx_ring->reg_idx;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB: {
		struct ixgbe_ring_feature *feature = adapter->ring_feature;
		const int mask = feature[RING_F_RSS].mask;
		reg_idx = reg_idx & mask;
	}
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	default:
		break;
	}

	srrctl = IXGBE_READ_REG(&adapter->hw, IXGBE_SRRCTL(reg_idx));

	srrctl &= ~IXGBE_SRRCTL_BSIZEHDR_MASK;
	srrctl &= ~IXGBE_SRRCTL_BSIZEPKT_MASK;
	if (adapter->num_vfs)
		srrctl |= IXGBE_SRRCTL_DROP_EN;

	srrctl |= (IXGBE_RX_HDR_SIZE << IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT) &
		  IXGBE_SRRCTL_BSIZEHDR_MASK;

	if (ring_is_ps_enabled(rx_ring)) {
#if (PAGE_SIZE / 2) > IXGBE_MAX_RXBUFFER
		srrctl |= IXGBE_MAX_RXBUFFER >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
#else
		srrctl |= (PAGE_SIZE / 2) >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
#endif
		srrctl |= IXGBE_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
	} else {
		srrctl |= ALIGN(rx_ring->rx_buf_len, 1024) >>
			  IXGBE_SRRCTL_BSIZEPKT_SHIFT;
		srrctl |= IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
	}

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_SRRCTL(reg_idx), srrctl);
}

static void ixgbe_setup_mrqc(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	static const u32 seed[10] = { 0xE291D73D, 0x1805EC6C, 0x2A94B30D,
			  0xA54F2BEC, 0xEA49AF7C, 0xE214AD3D, 0xB855AABE,
			  0x6A3E67EA, 0x14364D17, 0x3BED200D};
	u32 mrqc = 0, reta = 0;
	u32 rxcsum;
	int i, j;
	u8 tcs = netdev_get_num_tc(adapter->netdev);
	int maxq = adapter->ring_feature[RING_F_RSS].indices;

	if (tcs)
		maxq = min(maxq, adapter->num_tx_queues / tcs);

	/* Fill out hash function seeds */
	for (i = 0; i < 10; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), seed[i]);

	/* Fill out redirection table */
	for (i = 0, j = 0; i < 128; i++, j++) {
		if (j == maxq)
			j = 0;
		/* reta = 4-byte sliding window of
		 * 0x00..(indices-1)(indices-1)00..etc. */
		reta = (reta << 8) | (j * 0x11);
		if ((i & 3) == 3)
			IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
	}

	/* Disable indicating checksum in descriptor, enables RSS hash */
	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	rxcsum |= IXGBE_RXCSUM_PCSD;
	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);

	if (adapter->hw.mac.type == ixgbe_mac_82598EB &&
	    (adapter->flags & IXGBE_FLAG_RSS_ENABLED)) {
		mrqc = IXGBE_MRQC_RSSEN;
	} else {
		int mask = adapter->flags & (IXGBE_FLAG_RSS_ENABLED
					     | IXGBE_FLAG_SRIOV_ENABLED);

		switch (mask) {
		case (IXGBE_FLAG_RSS_ENABLED):
			if (!tcs)
				mrqc = IXGBE_MRQC_RSSEN;
			else if (tcs <= 4)
				mrqc = IXGBE_MRQC_RTRSS4TCEN;
			else
				mrqc = IXGBE_MRQC_RTRSS8TCEN;
			break;
		case (IXGBE_FLAG_SRIOV_ENABLED):
			mrqc = IXGBE_MRQC_VMDQEN;
			break;
		default:
			break;
		}
	}

	/* Perform hash on these packet types */
	mrqc |= IXGBE_MRQC_RSS_FIELD_IPV4
	      | IXGBE_MRQC_RSS_FIELD_IPV4_TCP
	      | IXGBE_MRQC_RSS_FIELD_IPV6
	      | IXGBE_MRQC_RSS_FIELD_IPV6_TCP;

	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
}

/**
 * ixgbe_configure_rscctl - enable RSC for the indicated ring
 * @adapter:    address of board private structure
 * @index:      index of ring to set
 **/
static void ixgbe_configure_rscctl(struct ixgbe_adapter *adapter,
				   struct ixgbe_ring *ring)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rscctrl;
	int rx_buf_len;
	u8 reg_idx = ring->reg_idx;

	if (!ring_is_rsc_enabled(ring))
		return;

	rx_buf_len = ring->rx_buf_len;
	rscctrl = IXGBE_READ_REG(hw, IXGBE_RSCCTL(reg_idx));
	rscctrl |= IXGBE_RSCCTL_RSCEN;
	/*
	 * we must limit the number of descriptors so that the
	 * total size of max desc * buf_len is not greater
	 * than 65535
	 */
	if (ring_is_ps_enabled(ring)) {
#if (MAX_SKB_FRAGS > 16)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_16;
#elif (MAX_SKB_FRAGS > 8)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_8;
#elif (MAX_SKB_FRAGS > 4)
		rscctrl |= IXGBE_RSCCTL_MAXDESC_4;
#else
		rscctrl |= IXGBE_RSCCTL_MAXDESC_1;
#endif
	} else {
		if (rx_buf_len < IXGBE_RXBUFFER_4K)
			rscctrl |= IXGBE_RSCCTL_MAXDESC_16;
		else if (rx_buf_len < IXGBE_RXBUFFER_8K)
			rscctrl |= IXGBE_RSCCTL_MAXDESC_8;
		else
			rscctrl |= IXGBE_RSCCTL_MAXDESC_4;
	}
	IXGBE_WRITE_REG(hw, IXGBE_RSCCTL(reg_idx), rscctrl);
}

/**
 *  ixgbe_set_uta - Set unicast filter table address
 *  @adapter: board private structure
 *
 *  The unicast table address is a register array of 32-bit registers.
 *  The table is meant to be used in a way similar to how the MTA is used
 *  however due to certain limitations in the hardware it is necessary to
 *  set all the hash bits to 1 and use the VMOLR ROPE bit as a promiscuous
 *  enable bit to allow vlan tag stripping when promiscuous mode is enabled
 **/
static void ixgbe_set_uta(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int i;

	/* The UTA table only exists on 82599 hardware and newer */
	if (hw->mac.type < ixgbe_mac_82599EB)
		return;

	/* we only need to do this if VMDq is enabled */
	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return;

	for (i = 0; i < 128; i++)
		IXGBE_WRITE_REG(hw, IXGBE_UTA(i), ~0);
}

#define IXGBE_MAX_RX_DESC_POLL 10
static void ixgbe_rx_desc_queue_enable(struct ixgbe_adapter *adapter,
				       struct ixgbe_ring *ring)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int wait_loop = IXGBE_MAX_RX_DESC_POLL;
	u32 rxdctl;
	u8 reg_idx = ring->reg_idx;

	/* RXDCTL.EN will return 0 on 82598 if link is down, so skip it */
	if (hw->mac.type == ixgbe_mac_82598EB &&
	    !(IXGBE_READ_REG(hw, IXGBE_LINKS) & IXGBE_LINKS_UP))
		return;

	do {
		usleep_range(1000, 2000);
		rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(reg_idx));
	} while (--wait_loop && !(rxdctl & IXGBE_RXDCTL_ENABLE));

	if (!wait_loop) {
		e_err(drv, "RXDCTL.ENABLE on Rx queue %d not set within "
		      "the polling period\n", reg_idx);
	}
}

void ixgbe_disable_rx_queue(struct ixgbe_adapter *adapter,
			    struct ixgbe_ring *ring)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int wait_loop = IXGBE_MAX_RX_DESC_POLL;
	u32 rxdctl;
	u8 reg_idx = ring->reg_idx;

	rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(reg_idx));
	rxdctl &= ~IXGBE_RXDCTL_ENABLE;

	/* write value back with RXDCTL.ENABLE bit cleared */
	IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(reg_idx), rxdctl);

	if (hw->mac.type == ixgbe_mac_82598EB &&
	    !(IXGBE_READ_REG(hw, IXGBE_LINKS) & IXGBE_LINKS_UP))
		return;

	/* the hardware may take up to 100us to really disable the rx queue */
	do {
		udelay(10);
		rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(reg_idx));
	} while (--wait_loop && (rxdctl & IXGBE_RXDCTL_ENABLE));

	if (!wait_loop) {
		e_err(drv, "RXDCTL.ENABLE on Rx queue %d not cleared within "
		      "the polling period\n", reg_idx);
	}
}

void ixgbe_configure_rx_ring(struct ixgbe_adapter *adapter,
			     struct ixgbe_ring *ring)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u64 rdba = ring->dma;
	u32 rxdctl;
	u8 reg_idx = ring->reg_idx;
	printk("%s: ring->dma %x ring->reg_idx %d\n",__func__,ring->dma,ring->reg_idx);

	/* disable queue to avoid issues while updating state */
	rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(reg_idx));
	ixgbe_disable_rx_queue(adapter, ring);

	IXGBE_WRITE_REG(hw, IXGBE_RDBAL(reg_idx), (rdba & DMA_BIT_MASK(32)));
	IXGBE_WRITE_REG(hw, IXGBE_RDBAH(reg_idx), (rdba >> 32));
	IXGBE_WRITE_REG(hw, IXGBE_RDLEN(reg_idx),
			ring->count * sizeof(union ixgbe_adv_rx_desc));
	IXGBE_WRITE_REG(hw, IXGBE_RDH(reg_idx), 0);
	IXGBE_WRITE_REG(hw, IXGBE_RDT(reg_idx), 0);
	ring->tail = hw->hw_addr + IXGBE_RDT(reg_idx);

	ixgbe_configure_srrctl(adapter, ring);
	ixgbe_configure_rscctl(adapter, ring);

	/* If operating in IOV mode set RLPML for X540 */
	if ((adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) &&
	    hw->mac.type == ixgbe_mac_X540) {
		rxdctl &= ~IXGBE_RXDCTL_RLPMLMASK;
		rxdctl |= ((ring->netdev->mtu + ETH_HLEN +
			    ETH_FCS_LEN + VLAN_HLEN) | IXGBE_RXDCTL_RLPML_EN);
	}

	if (hw->mac.type == ixgbe_mac_82598EB) {
		/*
		 * enable cache line friendly hardware writes:
		 * PTHRESH=32 descriptors (half the internal cache),
		 * this also removes ugly rx_no_buffer_count increment
		 * HTHRESH=4 descriptors (to minimize latency on fetch)
		 * WTHRESH=8 burst writeback up to two cache lines
		 */
		rxdctl &= ~0x3FFFFF;
		rxdctl |=  0x080420;
	}

	/* enable receive descriptor ring */
	rxdctl |= IXGBE_RXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(reg_idx), rxdctl);

	ixgbe_rx_desc_queue_enable(adapter, ring);
	ixgbe_alloc_rx_buffers(ring, ixgbe_desc_unused(ring));
}
EXPORT_SYMBOL(ixgbe_configure_rx_ring);


static void ixgbe_setup_psrtype(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int p;

	/* PSRTYPE must be initialized in non 82598 adapters */
	u32 psrtype = IXGBE_PSRTYPE_TCPHDR |
		      IXGBE_PSRTYPE_UDPHDR |
		      IXGBE_PSRTYPE_IPV4HDR |
		      IXGBE_PSRTYPE_L2HDR |
		      IXGBE_PSRTYPE_IPV6HDR;

	if (hw->mac.type == ixgbe_mac_82598EB)
		return;

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED)
		psrtype |= (adapter->num_rx_queues_per_pool << 29);

	for (p = 0; p < adapter->num_rx_pools; p++)
		IXGBE_WRITE_REG(hw, IXGBE_PSRTYPE(adapter->num_vfs + p),
				psrtype);
}

static void ixgbe_configure_virtualization(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 gcr_ext;
	u32 vt_reg_bits;
	u32 reg_offset, vf_shift;
	u32 vmdctl;
	int i;

	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return;

	vmdctl = IXGBE_READ_REG(hw, IXGBE_VT_CTL);
	vt_reg_bits = IXGBE_VMD_CTL_VMDQ_EN | IXGBE_VT_CTL_REPLEN;
	vt_reg_bits |= (adapter->num_vfs << IXGBE_VT_CTL_POOL_SHIFT);
	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, vmdctl | vt_reg_bits);

	vf_shift = adapter->num_vfs % 32;
	reg_offset = (adapter->num_vfs > 32) ? 1 : 0;

	/* Enable only the PF's pool for Tx/Rx */
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_offset), (1 << vf_shift));
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_offset ^ 1), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_offset), (1 << vf_shift));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_offset ^ 1), 0);
	IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);

	/* Map PF MAC address in RAR Entry 0 to first pool following VFs */
	hw->mac.ops.set_vmdq(hw, 0, adapter->num_vfs);

	/*
	 * Set up VF register offsets for selected VT Mode,
	 * i.e. 32 or 64 VFs for SR-IOV
	 */
	gcr_ext = IXGBE_READ_REG(hw, IXGBE_GCR_EXT);
	gcr_ext |= IXGBE_GCR_EXT_MSIX_EN;
	gcr_ext |= IXGBE_GCR_EXT_VT_MODE_64;
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT, gcr_ext);

	/* enable Tx loopback for VF/PF communication */
	IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);
	/* Enable MAC Anti-Spoofing */
	hw->mac.ops.set_mac_anti_spoofing(hw,
					   (adapter->num_vfs != 0),
					  adapter->num_vfs);
	/* For VFs that have spoof checking turned off */
	for (i = 0; i < adapter->num_vfs; i++) {
		if (!adapter->vfinfo[i].spoofchk_enabled)
			ixgbe_ndo_set_vf_spoofchk(adapter->netdev, i, false);
	}
}

static void ixgbe_set_rx_buffer_len(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	int max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	int rx_buf_len;
	struct ixgbe_ring *rx_ring;
	int i;
	u32 mhadd, hlreg0;

	/* Decide whether to use packet split mode or not */
	/* On by default */
	adapter->flags |= IXGBE_FLAG_RX_PS_ENABLED;

	/* Do not use packet split if we're in SR-IOV Mode */
	if (adapter->num_vfs)
		adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;

	/* Disable packet split due to 82599 erratum #45 */
	if (hw->mac.type == ixgbe_mac_82599EB)
		adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;

#ifdef IXGBE_FCOE
	/* adjust max frame to be able to do baby jumbo for FCoE */
	if ((adapter->flags & IXGBE_FLAG_FCOE_ENABLED) &&
	    (max_frame < IXGBE_FCOE_JUMBO_FRAME_SIZE))
		max_frame = IXGBE_FCOE_JUMBO_FRAME_SIZE;

#endif /* IXGBE_FCOE */
	mhadd = IXGBE_READ_REG(hw, IXGBE_MHADD);
	if (max_frame != (mhadd >> IXGBE_MHADD_MFS_SHIFT)) {
		mhadd &= ~IXGBE_MHADD_MFS_MASK;
		mhadd |= max_frame << IXGBE_MHADD_MFS_SHIFT;

		IXGBE_WRITE_REG(hw, IXGBE_MHADD, mhadd);
	}

	/* MHADD will allow an extra 4 bytes past for vlan tagged frames */
	max_frame += VLAN_HLEN;

	/* Set the RX buffer length according to the mode */
	if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
		rx_buf_len = IXGBE_RX_HDR_SIZE;
	} else {
		if (!(adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED) &&
		    (netdev->mtu <= ETH_DATA_LEN))
			rx_buf_len = MAXIMUM_ETHERNET_VLAN_SIZE;
		/*
		 * Make best use of allocation by using all but 1K of a
		 * power of 2 allocation that will be used for skb->head.
		 */
		else if (max_frame <= IXGBE_RXBUFFER_3K)
			rx_buf_len = IXGBE_RXBUFFER_3K;
		else if (max_frame <= IXGBE_RXBUFFER_7K)
			rx_buf_len = IXGBE_RXBUFFER_7K;
		else if (max_frame <= IXGBE_RXBUFFER_15K)
			rx_buf_len = IXGBE_RXBUFFER_15K;
		else
			rx_buf_len = IXGBE_MAX_RXBUFFER;
	}

	hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	/* set jumbo enable since MHADD.MFS is keeping size locked at max_frame */
	hlreg0 |= IXGBE_HLREG0_JUMBOEN;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);

	/*
	 * Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		rx_ring = adapter->rx_ring[i];
		rx_ring->rx_buf_len = rx_buf_len;

		if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED)
			set_ring_ps_enabled(rx_ring);
		else
			clear_ring_ps_enabled(rx_ring);

		if (adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED)
			set_ring_rsc_enabled(rx_ring);
		else
			clear_ring_rsc_enabled(rx_ring);

#ifdef IXGBE_FCOE
		if (netdev->features & NETIF_F_FCOE_MTU) {
			struct ixgbe_ring_feature *f;
			f = &adapter->ring_feature[RING_F_FCOE];
			if ((i >= f->mask) && (i < f->mask + f->indices)) {
				clear_ring_ps_enabled(rx_ring);
				if (rx_buf_len < IXGBE_FCOE_JUMBO_FRAME_SIZE)
					rx_ring->rx_buf_len =
						IXGBE_FCOE_JUMBO_FRAME_SIZE;
			} else if (!ring_is_rsc_enabled(rx_ring) &&
				   !ring_is_ps_enabled(rx_ring)) {
				rx_ring->rx_buf_len =
						IXGBE_FCOE_JUMBO_FRAME_SIZE;
			}
		}
#endif /* IXGBE_FCOE */
	}
}

static void ixgbe_setup_rdrxctl(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rdrxctl = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		/*
		 * For VMDq support of different descriptor types or
		 * buffer sizes through the use of multiple SRRCTL
		 * registers, RDRXCTL.MVMEN must be set to 1
		 *
		 * also, the manual doesn't mention it clearly but DCA hints
		 * will only use queue 0's tags unless this bit is set.  Side
		 * effects of setting this bit are only that SRRCTL must be
		 * fully programmed [0..15]
		 */
		rdrxctl |= IXGBE_RDRXCTL_MVMEN;
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		/* Disable RSC for ACK packets */
		IXGBE_WRITE_REG(hw, IXGBE_RSCDBU,
		   (IXGBE_RSCDBU_RSCACKDIS | IXGBE_READ_REG(hw, IXGBE_RSCDBU)));
		rdrxctl &= ~IXGBE_RDRXCTL_RSCFRSTSIZE;
		/* hardware requires some bits to be set by default */
		rdrxctl |= (IXGBE_RDRXCTL_RSCACKC | IXGBE_RDRXCTL_FCOE_WRFIX);
		rdrxctl |= IXGBE_RDRXCTL_CRCSTRIP;
		break;
	default:
		/* We should do nothing since we don't know this hardware */
		return;
	}

	IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, rdrxctl);
}

/**
 * ixgbe_configure_rx - Configure 8259x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void ixgbe_configure_rx(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int i;
	u32 rxctrl;

	/* disable receives while setting up the descriptors */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl & ~IXGBE_RXCTRL_RXEN);

	ixgbe_setup_psrtype(adapter);
	ixgbe_setup_rdrxctl(adapter);

	/* Program registers for the distribution of queues */
	ixgbe_setup_mrqc(adapter);

	ixgbe_set_uta(adapter);

	/* set_rx_buffer_len must be called before ring initialization */
	ixgbe_set_rx_buffer_len(adapter);

	/*
	 * Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_configure_rx_ring(adapter, adapter->rx_ring[i]);

	/* disable drop enable for 82598 parts */
	if (hw->mac.type == ixgbe_mac_82598EB)
		rxctrl |= IXGBE_RXCTRL_DMBYPS;

	/* enable all receives */
	rxctrl |= IXGBE_RXCTRL_RXEN;
	hw->mac.ops.enable_rx_dma(hw, rxctrl);
}

static void ixgbe_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	int pool_ndx = adapter->num_vfs;

	/* add VID to filter table */
	hw->mac.ops.set_vfta(&adapter->hw, vid, pool_ndx, true);
	set_bit(vid, adapter->active_vlans);
}

static void ixgbe_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	int pool_ndx = adapter->num_vfs;

	/* remove VID from filter table */
	hw->mac.ops.set_vfta(&adapter->hw, vid, pool_ndx, false);
	clear_bit(vid, adapter->active_vlans);
}

/**
 * ixgbe_vlan_filter_disable - helper to disable hw vlan filtering
 * @adapter: driver data
 */
static void ixgbe_vlan_filter_disable(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vlnctrl;

	vlnctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
	vlnctrl &= ~(IXGBE_VLNCTRL_VFE | IXGBE_VLNCTRL_CFIEN);
	IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, vlnctrl);
}

/**
 * ixgbe_vlan_filter_enable - helper to enable hw vlan filtering
 * @adapter: driver data
 */
static void ixgbe_vlan_filter_enable(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vlnctrl;

	vlnctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
	vlnctrl |= IXGBE_VLNCTRL_VFE;
	vlnctrl &= ~IXGBE_VLNCTRL_CFIEN;
	IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, vlnctrl);
}

/**
 * ixgbe_vlan_strip_disable - helper to disable hw vlan stripping
 * @adapter: driver data
 */
static void ixgbe_vlan_strip_disable(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vlnctrl;
	int i, j;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		vlnctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
		vlnctrl &= ~IXGBE_VLNCTRL_VME;
		IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, vlnctrl);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		for (i = 0; i < adapter->num_rx_queues; i++) {
			j = adapter->rx_ring[i]->reg_idx;
			vlnctrl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(j));
			vlnctrl &= ~IXGBE_RXDCTL_VME;
			IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(j), vlnctrl);
		}
		break;
	default:
		break;
	}
}

/**
 * ixgbe_vlan_strip_enable - helper to enable hw vlan stripping
 * @adapter: driver data
 */
static void ixgbe_vlan_strip_enable(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vlnctrl;
	int i, j;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		vlnctrl = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
		vlnctrl |= IXGBE_VLNCTRL_VME;
		IXGBE_WRITE_REG(hw, IXGBE_VLNCTRL, vlnctrl);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		for (i = 0; i < adapter->num_rx_queues; i++) {
			j = adapter->rx_ring[i]->reg_idx;
			vlnctrl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(j));
			vlnctrl |= IXGBE_RXDCTL_VME;
			IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(j), vlnctrl);
		}
		break;
	default:
		break;
	}
}

static void ixgbe_restore_vlan(struct ixgbe_adapter *adapter)
{
	u16 vid;

	ixgbe_vlan_rx_add_vid(adapter->netdev, 0);

	for_each_set_bit(vid, adapter->active_vlans, VLAN_N_VID)
		ixgbe_vlan_rx_add_vid(adapter->netdev, vid);
}

/**
 * ixgbe_write_uc_addr_list - write unicast addresses to RAR table
 * @netdev: network interface device structure
 *
 * Writes unicast address list to the RAR table.
 * Returns: -ENOMEM on failure/insufficient address space
 *                0 on no addresses written
 *                X on writing X addresses to the RAR table
 **/
static int ixgbe_write_uc_addr_list(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	unsigned int vfn = adapter->num_vfs;
	unsigned int rar_entries = IXGBE_MAX_PF_MACVLANS;
	int count = 0;

	/* return ENOMEM indicating insufficient memory for addresses */
	if (netdev_uc_count(netdev) > rar_entries)
		return -ENOMEM;

	if (!netdev_uc_empty(netdev) && rar_entries) {
		struct netdev_hw_addr *ha;
		/* return error if we do not support writing to RAR table */
		if (!hw->mac.ops.set_rar)
			return -ENOMEM;

		netdev_for_each_uc_addr(ha, netdev) {
			if (!rar_entries)
				break;
			hw->mac.ops.set_rar(hw, rar_entries--, ha->addr,
					    vfn, IXGBE_RAH_AV);
			count++;
		}
	}
	/* write the addresses in reverse order to avoid write combining */
	for (; rar_entries > 0 ; rar_entries--)
		hw->mac.ops.clear_rar(hw, rar_entries);

	return count;
}

/**
 * ixgbe_set_rx_mode - Unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_rx_method entry point is called whenever the unicast/multicast
 * address list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast and
 * promiscuous mode.
 **/
void ixgbe_set_rx_mode(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 fctrl, vmolr = IXGBE_VMOLR_BAM | IXGBE_VMOLR_AUPE;
	int count;

	/* Check for Promiscuous and All Multicast modes */

	fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);

	/* set all bits that we expect to always be set */
	fctrl |= IXGBE_FCTRL_BAM;
	fctrl |= IXGBE_FCTRL_DPF; /* discard pause frames when FC enabled */
	fctrl |= IXGBE_FCTRL_PMCF;

	/* clear the bits we are changing the status of */
	fctrl &= ~(IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);

	if (netdev->flags & IFF_PROMISC) {
		hw->addr_ctrl.user_set_promisc = true;
		fctrl |= (IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
		vmolr |= (IXGBE_VMOLR_ROPE | IXGBE_VMOLR_MPE);
		/* don't hardware filter vlans in promisc mode */
		ixgbe_vlan_filter_disable(adapter);
	} else {
		if (netdev->flags & IFF_ALLMULTI) {
			fctrl |= IXGBE_FCTRL_MPE;
			vmolr |= IXGBE_VMOLR_MPE;
		} else {
			/*
			 * Write addresses to the MTA, if the attempt fails
			 * then we should just turn on promiscuous mode so
			 * that we can at least receive multicast traffic
			 */
			hw->mac.ops.update_mc_addr_list(hw, netdev);
			vmolr |= IXGBE_VMOLR_ROMPE;
		}
		ixgbe_vlan_filter_enable(adapter);
		hw->addr_ctrl.user_set_promisc = false;
		/*
		 * Write addresses to available RAR registers, if there is not
		 * sufficient space to store all the addresses then enable
		 * unicast promiscuous mode
		 */
		count = ixgbe_write_uc_addr_list(netdev);
		if (count < 0) {
			fctrl |= IXGBE_FCTRL_UPE;
			vmolr |= IXGBE_VMOLR_ROPE;
		}
	}

	if (adapter->num_vfs) {
		ixgbe_restore_vf_multicasts(adapter);
		vmolr |= IXGBE_READ_REG(hw, IXGBE_VMOLR(adapter->num_vfs)) &
			 ~(IXGBE_VMOLR_MPE | IXGBE_VMOLR_ROMPE |
			   IXGBE_VMOLR_ROPE);
		IXGBE_WRITE_REG(hw, IXGBE_VMOLR(adapter->num_vfs), vmolr);
	}

	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);

	if (netdev->features & NETIF_F_HW_VLAN_RX)
		ixgbe_vlan_strip_enable(adapter);
	else
		ixgbe_vlan_strip_disable(adapter);
}

static void ixgbe_napi_enable_all(struct ixgbe_adapter *adapter)
{
	int q_idx;
	struct ixgbe_q_vector *q_vector;
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* legacy and MSI only use one vector */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		q_vector = adapter->q_vector[q_idx];
		napi_enable(&q_vector->napi);
	}
}

static void ixgbe_napi_disable_all(struct ixgbe_adapter *adapter)
{
	int q_idx;
	struct ixgbe_q_vector *q_vector;
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* legacy and MSI only use one vector */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		q_vector = adapter->q_vector[q_idx];
		napi_disable(&q_vector->napi);
	}
}

#ifdef CONFIG_IXGBE_DCB
/*
 * ixgbe_configure_dcb - Configure DCB hardware
 * @adapter: ixgbe adapter struct
 *
 * This is called by the driver on open to configure the DCB hardware.
 * This is also called by the gennetlink interface when reconfiguring
 * the DCB state.
 */
static void ixgbe_configure_dcb(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int max_frame = adapter->netdev->mtu + ETH_HLEN + ETH_FCS_LEN;

	if (!(adapter->flags & IXGBE_FLAG_DCB_ENABLED)) {
		if (hw->mac.type == ixgbe_mac_82598EB)
			netif_set_gso_max_size(adapter->netdev, 65536);
		return;
	}

	if (hw->mac.type == ixgbe_mac_82598EB)
		netif_set_gso_max_size(adapter->netdev, 32768);


	/* Enable VLAN tag insert/strip */
	adapter->netdev->features |= NETIF_F_HW_VLAN_RX;

	hw->mac.ops.set_vfta(&adapter->hw, 0, 0, true);

#ifdef IXGBE_FCOE
	if (adapter->netdev->features & NETIF_F_FCOE_MTU)
		max_frame = max(max_frame, IXGBE_FCOE_JUMBO_FRAME_SIZE);
#endif

	/* reconfigure the hardware */
	if (adapter->dcbx_cap & DCB_CAP_DCBX_VER_CEE) {
		ixgbe_dcb_calculate_tc_credits(hw, &adapter->dcb_cfg, max_frame,
						DCB_TX_CONFIG);
		ixgbe_dcb_calculate_tc_credits(hw, &adapter->dcb_cfg, max_frame,
						DCB_RX_CONFIG);
		ixgbe_dcb_hw_config(hw, &adapter->dcb_cfg);
	} else if (adapter->ixgbe_ieee_ets && adapter->ixgbe_ieee_pfc) {
		ixgbe_dcb_hw_ets(&adapter->hw,
				 adapter->ixgbe_ieee_ets,
				 max_frame);
		ixgbe_dcb_hw_pfc_config(&adapter->hw,
					adapter->ixgbe_ieee_pfc->pfc_en,
					adapter->ixgbe_ieee_ets->prio_tc);
	}

	/* Enable RSS Hash per TC */
	if (hw->mac.type != ixgbe_mac_82598EB) {
		int i;
		u32 reg = 0;

		for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
			u8 msb = 0;
			u8 cnt = adapter->netdev->tc_to_txq[i].count;

			while (cnt >>= 1)
				msb++;

			reg |= msb << IXGBE_RQTC_SHIFT_TC(i);
		}
		IXGBE_WRITE_REG(hw, IXGBE_RQTC, reg);
	}
}
#endif

/* Additional bittime to account for IXGBE framing */
#define IXGBE_ETH_FRAMING 20

/*
 * ixgbe_hpbthresh - calculate high water mark for flow control
 *
 * @adapter: board private structure to calculate for
 * @pb - packet buffer to calculate
 */
static int ixgbe_hpbthresh(struct ixgbe_adapter *adapter, int pb)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *dev = adapter->netdev;
	int link, tc, kb, marker;
	u32 dv_id, rx_pba;

	/* Calculate max LAN frame size */
	tc = link = dev->mtu + ETH_HLEN + ETH_FCS_LEN + IXGBE_ETH_FRAMING;

#ifdef IXGBE_FCOE
	/* FCoE traffic class uses FCOE jumbo frames */
	if (dev->features & NETIF_F_FCOE_MTU) {
		int fcoe_pb = 0;

#ifdef CONFIG_IXGBE_DCB
		fcoe_pb = netdev_get_prio_tc_map(dev, adapter->fcoe.up);

#endif
		if (fcoe_pb == pb && tc < IXGBE_FCOE_JUMBO_FRAME_SIZE)
			tc = IXGBE_FCOE_JUMBO_FRAME_SIZE;
	}
#endif

	/* Calculate delay value for device */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		dv_id = IXGBE_DV_X540(link, tc);
		break;
	default:
		dv_id = IXGBE_DV(link, tc);
		break;
	}

	/* Loopback switch introduces additional latency */
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		dv_id += IXGBE_B2BT(tc);

	/* Delay value is calculated in bit times convert to KB */
	kb = IXGBE_BT2KB(dv_id);
	rx_pba = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(pb)) >> 10;

	marker = rx_pba - kb;

	/* It is possible that the packet buffer is not large enough
	 * to provide required headroom. In this case throw an error
	 * to user and a do the best we can.
	 */
	if (marker < 0) {
		e_warn(drv, "Packet Buffer(%i) can not provide enough"
			    "headroom to support flow control."
			    "Decrease MTU or number of traffic classes\n", pb);
		marker = tc + 1;
	}

	return marker;
}

/*
 * ixgbe_lpbthresh - calculate low water mark for for flow control
 *
 * @adapter: board private structure to calculate for
 * @pb - packet buffer to calculate
 */
static int ixgbe_lpbthresh(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *dev = adapter->netdev;
	int tc;
	u32 dv_id;

	/* Calculate max LAN frame size */
	tc = dev->mtu + ETH_HLEN + ETH_FCS_LEN;

	/* Calculate delay value for device */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		dv_id = IXGBE_LOW_DV_X540(tc);
		break;
	default:
		dv_id = IXGBE_LOW_DV(tc);
		break;
	}

	/* Delay value is calculated in bit times convert to KB */
	return IXGBE_BT2KB(dv_id);
}

/*
 * ixgbe_pbthresh_setup - calculate and setup high low water marks
 */
static void ixgbe_pbthresh_setup(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int num_tc = netdev_get_num_tc(adapter->netdev);
	int i;

	if (!num_tc)
		num_tc = 1;

	hw->fc.low_water = ixgbe_lpbthresh(adapter);

	for (i = 0; i < num_tc; i++) {
		hw->fc.high_water[i] = ixgbe_hpbthresh(adapter, i);

		/* Low water marks must not be larger than high water marks */
		if (hw->fc.low_water > hw->fc.high_water[i])
			hw->fc.low_water = 0;
	}
}

static void ixgbe_configure_pb(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int hdrm;
	u8 tc = netdev_get_num_tc(adapter->netdev);

	if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE ||
	    adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE)
		hdrm = 32 << adapter->fdir_pballoc;
	else
		hdrm = 0;

	hw->mac.ops.set_rxpba(hw, tc, hdrm, PBA_STRATEGY_EQUAL);
	ixgbe_pbthresh_setup(adapter);
}

static void ixgbe_fdir_filter_restore(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct hlist_node *node, *node2;
	struct ixgbe_fdir_filter *filter;

	spin_lock(&adapter->fdir_perfect_lock);

	if (!hlist_empty(&adapter->fdir_filter_list))
		ixgbe_fdir_set_input_mask_82599(hw, &adapter->fdir_mask);

	hlist_for_each_entry_safe(filter, node, node2,
				  &adapter->fdir_filter_list, fdir_node) {
		ixgbe_fdir_write_perfect_filter_82599(hw,
				&filter->filter,
				filter->sw_idx,
				(filter->action == IXGBE_FDIR_DROP_QUEUE) ?
				IXGBE_FDIR_DROP_QUEUE :
				adapter->rx_ring[filter->action]->reg_idx);
	}

	spin_unlock(&adapter->fdir_perfect_lock);
}

static void ixgbe_configure(struct ixgbe_adapter *adapter)
{
	ixgbe_configure_pb(adapter);
#ifdef CONFIG_IXGBE_DCB
	ixgbe_configure_dcb(adapter);
#endif

	ixgbe_set_rx_mode(adapter->netdev);
	ixgbe_restore_vlan(adapter);

#ifdef IXGBE_FCOE
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED)
		ixgbe_configure_fcoe(adapter);

#endif /* IXGBE_FCOE */
	if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) {
		ixgbe_init_fdir_signature_82599(&adapter->hw,
						adapter->fdir_pballoc);
	} else if (adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE) {
		ixgbe_init_fdir_perfect_82599(&adapter->hw,
					      adapter->fdir_pballoc);
		ixgbe_fdir_filter_restore(adapter);
	}

	ixgbe_configure_virtualization(adapter);

	ixgbe_configure_tx(adapter);
	ixgbe_configure_rx(adapter);
}

static inline bool ixgbe_is_sfp(struct ixgbe_hw *hw)
{
	switch (hw->phy.type) {
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
	case ixgbe_phy_sfp_passive_tyco:
	case ixgbe_phy_sfp_passive_unknown:
	case ixgbe_phy_sfp_active_unknown:
	case ixgbe_phy_sfp_ftl_active:
		return true;
	case ixgbe_phy_nl:
		if (hw->mac.type == ixgbe_mac_82598EB)
			return true;
	default:
		return false;
	}
}

/**
 * ixgbe_sfp_link_config - set up SFP+ link
 * @adapter: pointer to private adapter struct
 **/
static void ixgbe_sfp_link_config(struct ixgbe_adapter *adapter)
{
	/*
	 * We are assuming the worst case scenerio here, and that
	 * is that an SFP was inserted/removed after the reset
	 * but before SFP detection was enabled.  As such the best
	 * solution is to just start searching as soon as we start
	 */
	if (adapter->hw.mac.type == ixgbe_mac_82598EB)
		adapter->flags2 |= IXGBE_FLAG2_SEARCH_FOR_SFP;

	adapter->flags2 |= IXGBE_FLAG2_SFP_NEEDS_RESET;
}

/**
 * ixgbe_non_sfp_link_config - set up non-SFP+ link
 * @hw: pointer to private hardware struct
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_non_sfp_link_config(struct ixgbe_hw *hw)
{
	u32 autoneg;
	bool negotiation, link_up = false;
	u32 ret = IXGBE_ERR_LINK_SETUP;

	if (hw->mac.ops.check_link)
		ret = hw->mac.ops.check_link(hw, &autoneg, &link_up, false);

	if (ret)
		goto link_cfg_out;

	autoneg = hw->phy.autoneg_advertised;
	if ((!autoneg) && (hw->mac.ops.get_link_capabilities))
		ret = hw->mac.ops.get_link_capabilities(hw, &autoneg,
							&negotiation);
	if (ret)
		goto link_cfg_out;

	if (hw->mac.ops.setup_link)
		ret = hw->mac.ops.setup_link(hw, autoneg, negotiation, link_up);
link_cfg_out:
	return ret;
}

static void ixgbe_setup_gpie(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 gpie = 0;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		gpie = IXGBE_GPIE_MSIX_MODE | IXGBE_GPIE_PBA_SUPPORT |
		       IXGBE_GPIE_OCD;
		gpie |= IXGBE_GPIE_EIAME;
		/*
		 * use EIAM to auto-mask when MSI-X interrupt is asserted
		 * this saves a register write for every interrupt
		 */
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			IXGBE_WRITE_REG(hw, IXGBE_EIAM, IXGBE_EICS_RTX_QUEUE);
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
		default:
			IXGBE_WRITE_REG(hw, IXGBE_EIAM_EX(0), 0xFFFFFFFF);
			IXGBE_WRITE_REG(hw, IXGBE_EIAM_EX(1), 0xFFFFFFFF);
			break;
		}
	} else {
		/* legacy interrupts, use EIAM to auto-mask when reading EICR,
		 * specifically only auto mask tx and rx interrupts */
		IXGBE_WRITE_REG(hw, IXGBE_EIAM, IXGBE_EICS_RTX_QUEUE);
	}

	/* XXX: to interrupt immediately for EICS writes, enable this */
	/* gpie |= IXGBE_GPIE_EIMEN; */

	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
		gpie &= ~IXGBE_GPIE_VTMODE_MASK;
		gpie |= IXGBE_GPIE_VTMODE_64;
	}

	/* Enable Thermal over heat sensor interrupt */
	if (adapter->flags2 & IXGBE_FLAG2_TEMP_SENSOR_CAPABLE) {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
			gpie |= IXGBE_SDP0_GPIEN;
			break;
		case ixgbe_mac_X540:
			gpie |= IXGBE_EIMS_TS;
			break;
		default:
			break;
		}
	}

	/* Enable fan failure interrupt */
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE)
		gpie |= IXGBE_SDP1_GPIEN;

	if (hw->mac.type == ixgbe_mac_82599EB) {
		gpie |= IXGBE_SDP1_GPIEN;
		gpie |= IXGBE_SDP2_GPIEN;
	}

	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
}

static void ixgbe_up_complete(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int err;
	u32 ctrl_ext;

	ixgbe_get_hw_control(adapter);
	ixgbe_setup_gpie(adapter);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		ixgbe_configure_msix(adapter);
	else
		ixgbe_configure_msi_and_legacy(adapter);

	/* enable the optics for both mult-speed fiber and 82599 SFP+ fiber */
	if (hw->mac.ops.enable_tx_laser &&
	    ((hw->phy.multispeed_fiber) ||
	     ((hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber) &&
	      (hw->mac.type == ixgbe_mac_82599EB))))
		hw->mac.ops.enable_tx_laser(hw);

	clear_bit(__IXGBE_DOWN, &adapter->state);
	ixgbe_napi_enable_all(adapter);

	if (ixgbe_is_sfp(hw)) {
		ixgbe_sfp_link_config(adapter);
	} else {
		err = ixgbe_non_sfp_link_config(hw);
		if (err)
			e_err(probe, "link_config FAILED %d\n", err);
	}

	/* clear any pending interrupts, may auto mask */
	IXGBE_READ_REG(hw, IXGBE_EICR);
	ixgbe_irq_enable(adapter, true, true);

	/*
	 * If this adapter has a fan, check to see if we had a failure
	 * before we enabled the interrupt.
	 */
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) {
		u32 esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		if (esdp & IXGBE_ESDP_SDP1)
			e_crit(drv, "Fan has stopped, replace the adapter\n");
	}

	/* enable transmits */
	netif_tx_start_all_queues(adapter->netdev);

	/* bring the link up in the watchdog, this could race with our first
	 * link up interrupt but shouldn't be a problem */
	adapter->flags |= IXGBE_FLAG_NEED_LINK_UPDATE;
	adapter->link_check_timeout = jiffies;
	mod_timer(&adapter->service_timer, jiffies);

	/* Set PF Reset Done bit so PF/VF Mail Ops can work */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
}

void ixgbe_reinit_locked(struct ixgbe_adapter *adapter)
{
	WARN_ON(in_interrupt());
	/* put off any impending NetWatchDogTimeout */
	adapter->netdev->trans_start = jiffies;

	while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
		usleep_range(1000, 2000);
	ixgbe_down(adapter);
	/*
	 * If SR-IOV enabled then wait a bit before bringing the adapter
	 * back up to give the VFs time to respond to the reset.  The
	 * two second wait is based upon the watchdog timer cycle in
	 * the VF driver.
	 */
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		msleep(2000);
	ixgbe_up(adapter);
	clear_bit(__IXGBE_RESETTING, &adapter->state);
}

void ixgbe_up(struct ixgbe_adapter *adapter)
{
	/* hardware has been reset, we need to reload some things */
	ixgbe_configure(adapter);

	ixgbe_up_complete(adapter);
}

void ixgbe_reset(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	/* lock SFP init bit to prevent race conditions with the watchdog */
	while (test_and_set_bit(__IXGBE_IN_SFP_INIT, &adapter->state))
		usleep_range(1000, 2000);

	/* clear all SFP and link config related flags while holding SFP_INIT */
	adapter->flags2 &= ~(IXGBE_FLAG2_SEARCH_FOR_SFP |
			     IXGBE_FLAG2_SFP_NEEDS_RESET);
	adapter->flags &= ~IXGBE_FLAG_NEED_LINK_CONFIG;

	err = hw->mac.ops.init_hw(hw);
	switch (err) {
	case 0:
	case IXGBE_ERR_SFP_NOT_PRESENT:
	case IXGBE_ERR_SFP_NOT_SUPPORTED:
		break;
	case IXGBE_ERR_MASTER_REQUESTS_PENDING:
		e_dev_err("master disable timed out\n");
		break;
	case IXGBE_ERR_EEPROM_VERSION:
		/* We are running on a pre-production device, log a warning */
		e_dev_warn("This device is a pre-production adapter/LOM. "
			   "Please be aware there may be issuesassociated with "
			   "your hardware.  If you are experiencing problems "
			   "please contact your Intel or hardware "
			   "representative who provided you with this "
			   "hardware.\n");
		break;
	default:
		e_dev_err("Hardware Error: %d\n", err);
	}

	clear_bit(__IXGBE_IN_SFP_INIT, &adapter->state);

	/* reprogram the RAR[0] in case user changed it. */
	hw->mac.ops.set_rar(hw, 0, hw->mac.addr, adapter->num_vfs,
			    IXGBE_RAH_AV);
}

/**
 * ixgbe_clean_rx_ring - Free Rx Buffers per Queue
 * @rx_ring: ring to free buffers from
 **/
static void ixgbe_clean_rx_ring(struct ixgbe_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	unsigned long size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!rx_ring->rx_buffer_info)
		return;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct ixgbe_rx_buffer *rx_buffer_info;

		rx_buffer_info = &rx_ring->rx_buffer_info[i];
		if (rx_buffer_info->dma) {
			dma_unmap_single(rx_ring->dev, rx_buffer_info->dma,
					 rx_ring->rx_buf_len,
					 DMA_FROM_DEVICE);
			rx_buffer_info->dma = 0;
		}
		if (rx_buffer_info->skb) {
			struct sk_buff *skb = rx_buffer_info->skb;
			rx_buffer_info->skb = NULL;
			do {
				struct sk_buff *this = skb;
				if (IXGBE_RSC_CB(this)->delay_unmap) {
					dma_unmap_single(dev,
							 IXGBE_RSC_CB(this)->dma,
							 rx_ring->rx_buf_len,
							 DMA_FROM_DEVICE);
					IXGBE_RSC_CB(this)->dma = 0;
					IXGBE_RSC_CB(skb)->delay_unmap = false;
				}
				skb = skb->prev;
				dev_kfree_skb(this);
			} while (skb);
		}
		if (!rx_buffer_info->page)
			continue;
		if (rx_buffer_info->page_dma) {
			dma_unmap_page(dev, rx_buffer_info->page_dma,
				       PAGE_SIZE / 2, DMA_FROM_DEVICE);
			rx_buffer_info->page_dma = 0;
		}
		put_page(rx_buffer_info->page);
		rx_buffer_info->page = NULL;
		rx_buffer_info->page_offset = 0;
	}

	size = sizeof(struct ixgbe_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * ixgbe_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 **/
static void ixgbe_clean_tx_ring(struct ixgbe_ring *tx_ring)
{
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned long size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_buffer_info)
		return;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		ixgbe_unmap_and_free_tx_resource(tx_ring, tx_buffer_info);
	}

	size = sizeof(struct ixgbe_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * ixgbe_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_rx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_clean_rx_ring(adapter->rx_ring[i]);
}

/**
 * ixgbe_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_tx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_clean_tx_ring(adapter->tx_ring[i]);
}

static void ixgbe_fdir_filter_exit(struct ixgbe_adapter *adapter)
{
	struct hlist_node *node, *node2;
	struct ixgbe_fdir_filter *filter;

	spin_lock(&adapter->fdir_perfect_lock);

	hlist_for_each_entry_safe(filter, node, node2,
				  &adapter->fdir_filter_list, fdir_node) {
		hlist_del(&filter->fdir_node);
		kfree(filter);
	}
	adapter->fdir_filter_count = 0;

	spin_unlock(&adapter->fdir_perfect_lock);
}

void ixgbe_down(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rxctrl;
	int i;

	/* signal that we are down to the interrupt handler */
	set_bit(__IXGBE_DOWN, &adapter->state);

	/* disable receives */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl & ~IXGBE_RXCTRL_RXEN);

	/* disable all enabled rx queues */
	for (i = 0; i < adapter->num_rx_queues; i++)
		/* this call also flushes the previous write */
		ixgbe_disable_rx_queue(adapter, adapter->rx_ring[i]);

	usleep_range(10000, 20000);

	netif_tx_stop_all_queues(netdev);

	/* call carrier off first to avoid false dev_watchdog timeouts */
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	ixgbe_irq_disable(adapter);

	ixgbe_napi_disable_all(adapter);

	adapter->flags2 &= ~(IXGBE_FLAG2_FDIR_REQUIRES_REINIT |
			     IXGBE_FLAG2_RESET_REQUESTED);
	adapter->flags &= ~IXGBE_FLAG_NEED_LINK_UPDATE;

	del_timer_sync(&adapter->service_timer);

	if (adapter->num_vfs) {
		/* Clear EITR Select mapping */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITRSEL, 0);

		/* Mark all the VFs as inactive */
		for (i = 0 ; i < adapter->num_vfs; i++)
			adapter->vfinfo[i].clear_to_send = 0;

		/* ping all the active vfs to let them know we are going down */
		ixgbe_ping_all_vfs(adapter);

		/* Disable all VFTE/VFRE TX/RX */
		ixgbe_disable_tx_rx(adapter);
	}

	/* disable transmits in the hardware now that interrupts are off */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		u8 reg_idx = adapter->tx_ring[i]->reg_idx;
		IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(reg_idx), IXGBE_TXDCTL_SWFLSH);
	}

	/* Disable the Tx DMA engine on 82599 and X540 */
	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL,
				(IXGBE_READ_REG(hw, IXGBE_DMATXCTL) &
				 ~IXGBE_DMATXCTL_TE));
		break;
	default:
		break;
	}

	if (!pci_channel_offline(adapter->pdev))
		ixgbe_reset(adapter);

	/* power down the optics for multispeed fiber and 82599 SFP+ fiber */
	if (hw->mac.ops.disable_tx_laser &&
	    ((hw->phy.multispeed_fiber) ||
	     ((hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber) &&
	      (hw->mac.type == ixgbe_mac_82599EB))))
		hw->mac.ops.disable_tx_laser(hw);

	ixgbe_clean_all_tx_rings(adapter);
	ixgbe_clean_all_rx_rings(adapter);

#ifdef CONFIG_IXGBE_DCA
	/* since we reset the hardware DCA settings were cleared */
	ixgbe_setup_dca(adapter);
#endif
}

/**
 * ixgbe_poll - NAPI Rx polling callback
 * @napi: structure for representing this polling device
 * @budget: how many packets driver is allowed to clean
 *
 * This function is used for legacy and MSI, NAPI mode
 **/
static int ixgbe_poll(struct napi_struct *napi, int budget)
{
	struct ixgbe_q_vector *q_vector =
				container_of(napi, struct ixgbe_q_vector, napi);
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct ixgbe_ring *ring;
	int per_ring_budget;
	bool clean_complete = true;

#ifdef CONFIG_IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
		ixgbe_update_dca(q_vector);
#endif

	for (ring = q_vector->tx.ring; ring != NULL; ring = ring->next)
		clean_complete &= !!ixgbe_clean_tx_irq(q_vector, ring);

	/* attempt to distribute budget to each queue fairly, but don't allow
	 * the budget to go below 1 because we'll exit polling */
	if (q_vector->rx.count > 1)
		per_ring_budget = max(budget/q_vector->rx.count, 1);
	else
		per_ring_budget = budget;

	for (ring = q_vector->rx.ring; ring != NULL; ring = ring->next)
		clean_complete &= ixgbe_clean_rx_irq(q_vector, ring,
						     per_ring_budget,napi);

	/* If all work not completed, return budget and keep polling */
	if (!clean_complete)
		return budget;

	/* all work done, exit the polling mode */
	napi_complete(napi);
	if (adapter->rx_itr_setting & 1)
		ixgbe_set_itr(q_vector);
	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_enable_queues(adapter, ((u64)1 << q_vector->v_idx));

	return 0;
}

/**
 * ixgbe_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void ixgbe_tx_timeout(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	ixgbe_tx_timeout_reset(adapter);
}

/**
 * ixgbe_set_rss_queues: Allocate queues for RSS
 * @adapter: board private structure to initialize
 *
 * This is our "base" multiqueue mode.  RSS (Receive Side Scaling) will try
 * to allocate one Rx queue per CPU, and if available, one Tx queue per CPU.
 *
 **/
static inline bool ixgbe_set_rss_queues(struct ixgbe_adapter *adapter)
{
	bool ret = false;
	struct ixgbe_ring_feature *f = &adapter->ring_feature[RING_F_RSS];

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED) {
		f->mask = 0xF;
		adapter->num_rx_queues = f->indices;
		adapter->num_tx_queues = f->indices;
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}

/**
 * ixgbe_set_fdir_queues: Allocate queues for Flow Director
 * @adapter: board private structure to initialize
 *
 * Flow Director is an advanced Rx filter, attempting to get Rx flows back
 * to the original CPU that initiated the Tx session.  This runs in addition
 * to RSS, so if a packet doesn't match an FDIR filter, we can still spread the
 * Rx load across CPUs using RSS.
 *
 **/
static inline bool ixgbe_set_fdir_queues(struct ixgbe_adapter *adapter)
{
	bool ret = false;
	struct ixgbe_ring_feature *f_fdir = &adapter->ring_feature[RING_F_FDIR];

	f_fdir->indices = min((int)num_online_cpus(), f_fdir->indices);
	f_fdir->mask = 0;
	
	f_fdir->indices = 24;
	
	printk("%s:called\n",__func__);
	/* Flow Director must have RSS enabled */
	if ((adapter->flags & IXGBE_FLAG_RSS_ENABLED) &&
	    (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE)) {
		adapter->num_tx_queues = f_fdir->indices;
		adapter->num_rx_queues = f_fdir->indices;
		ret = true;
		printk("%s: num_tx_q %d num_rx %d\n",__func__,adapter->num_tx_queues,adapter->num_rx_queues);
	} else {
		adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;
	}
	return ret;
}

#ifdef IXGBE_FCOE
/**
 * ixgbe_set_fcoe_queues: Allocate queues for Fiber Channel over Ethernet (FCoE)
 * @adapter: board private structure to initialize
 *
 * FCoE RX FCRETA can use up to 8 rx queues for up to 8 different exchanges.
 * The ring feature mask is not used as a mask for FCoE, as it can take any 8
 * rx queues out of the max number of rx queues, instead, it is used as the
 * index of the first rx queue used by FCoE.
 *
 **/
static inline bool ixgbe_set_fcoe_queues(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring_feature *f = &adapter->ring_feature[RING_F_FCOE];

	if (!(adapter->flags & IXGBE_FLAG_FCOE_ENABLED))
		return false;

	f->indices = min((int)num_online_cpus(), f->indices);

	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED) {
		e_info(probe, "FCoE enabled with RSS\n");
		if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE)
			ixgbe_set_fdir_queues(adapter);
		else
			ixgbe_set_rss_queues(adapter);
	}

	/* adding FCoE rx rings to the end */
	f->mask = adapter->num_rx_queues;
	adapter->num_rx_queues += f->indices;
	adapter->num_tx_queues += f->indices;

	return true;
}
#endif /* IXGBE_FCOE */

/* Artificial max queue cap per traffic class in DCB mode */
#define DCB_QUEUE_CAP 8

#ifdef CONFIG_IXGBE_DCB
static inline bool ixgbe_set_dcb_queues(struct ixgbe_adapter *adapter)
{
	int per_tc_q, q, i, offset = 0;
	struct net_device *dev = adapter->netdev;
	int tcs = netdev_get_num_tc(dev);

	if (!tcs)
		return false;

	/* Map queue offset and counts onto allocated tx queues */
	per_tc_q = min(dev->num_tx_queues / tcs, (unsigned int)DCB_QUEUE_CAP);
	q = min((int)num_online_cpus(), per_tc_q);

	for (i = 0; i < tcs; i++) {
		netdev_set_tc_queue(dev, i, q, offset);
		offset += q;
	}

	adapter->num_tx_queues = q * tcs;
	adapter->num_rx_queues = q * tcs;

#ifdef IXGBE_FCOE
	/* FCoE enabled queues require special configuration indexed
	 * by feature specific indices and mask. Here we map FCoE
	 * indices onto the DCB queue pairs allowing FCoE to own
	 * configuration later.
	 */
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) {
		int tc;
		struct ixgbe_ring_feature *f =
					&adapter->ring_feature[RING_F_FCOE];

		tc = netdev_get_prio_tc_map(dev, adapter->fcoe.up);
		f->indices = dev->tc_to_txq[tc].count;
		f->mask = dev->tc_to_txq[tc].offset;
	}
#endif

	return true;
}
#endif

/**
 * ixgbe_set_sriov_queues: Allocate queues for IOV use
 * @adapter: board private structure to initialize
 *
 * IOV doesn't actually use anything, so just NAK the
 * request for now and let the other queue routines
 * figure out what to do.
 */
static inline bool ixgbe_set_sriov_queues(struct ixgbe_adapter *adapter)
{
	return false;
}

/*
 * ixgbe_set_num_queues: Allocate queues for device, feature dependent
 * @adapter: board private structure to initialize
 *
 * This is the top level queue allocation routine.  The order here is very
 * important, starting with the "most" number of features turned on at once,
 * and ending with the smallest set of features.  This way large combinations
 * can be allocated if they're turned on, and smaller combinations are the
 * fallthrough conditions.
 *
 **/
static int ixgbe_set_num_queues(struct ixgbe_adapter *adapter)
{
	/* Start with base case */
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;
	adapter->num_rx_pools = adapter->num_rx_queues;
	adapter->num_rx_queues_per_pool = 1;

	if (ixgbe_set_sriov_queues(adapter))
		goto done;

#ifdef CONFIG_IXGBE_DCB
	if (ixgbe_set_dcb_queues(adapter))
		goto done;

#endif
#ifdef IXGBE_FCOE
	if (ixgbe_set_fcoe_queues(adapter))
		goto done;

#endif /* IXGBE_FCOE */
	if (ixgbe_set_fdir_queues(adapter))
		goto done;

	if (ixgbe_set_rss_queues(adapter))
		goto done;

	/* fallback to base case */
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;

done:
	/* Notify the stack of the (possibly) reduced queue counts. */
	netif_set_real_num_tx_queues(adapter->netdev, adapter->num_tx_queues);
	return netif_set_real_num_rx_queues(adapter->netdev,
					    adapter->num_rx_queues);
}

static void ixgbe_acquire_msix_vectors(struct ixgbe_adapter *adapter,
				       int vectors)
{
	int err, vector_threshold;

	/* We'll want at least 3 (vector_threshold):
	 * 1) TxQ[0] Cleanup
	 * 2) RxQ[0] Cleanup
	 * 3) Other (Link Status Change, etc.)
	 * 4) TCP Timer (optional)
	 */
	vector_threshold = MIN_MSIX_COUNT;

	/* The more we get, the more we will assign to Tx/Rx Cleanup
	 * for the separate queues...where Rx Cleanup >= Tx Cleanup.
	 * Right now, we simply care about how many we'll get; we'll
	 * set them up later while requesting irq's.
	 */
	while (vectors >= vector_threshold) {
		err = pci_enable_msix(adapter->pdev, adapter->msix_entries,
				      vectors);
		if (!err) /* Success in acquiring all requested vectors. */
			break;
		else if (err < 0)
			vectors = 0; /* Nasty failure, quit now */
		else /* err == number of vectors we should try again with */
			vectors = err;
	}

	if (vectors < vector_threshold) {
		/* Can't allocate enough MSI-X interrupts?  Oh well.
		 * This just means we'll go with either a single MSI
		 * vector or fall back to legacy interrupts.
		 */
		netif_printk(adapter, hw, KERN_DEBUG, adapter->netdev,
			     "Unable to allocate MSI-X interrupts\n");
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else {
		adapter->flags |= IXGBE_FLAG_MSIX_ENABLED; /* Woot! */
		/*
		 * Adjust for only the vectors we'll use, which is minimum
		 * of max_msix_q_vectors + NON_Q_VECTORS, or the number of
		 * vectors we were allocated.
		 */
		adapter->num_msix_vectors = min(vectors,
				   adapter->max_msix_q_vectors + NON_Q_VECTORS);
	}
}

/**
 * ixgbe_cache_ring_rss - Descriptor ring to register mapping for RSS
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for RSS to the assigned rings.
 *
 **/
static inline bool ixgbe_cache_ring_rss(struct ixgbe_adapter *adapter)
{
	int i;

	if (!(adapter->flags & IXGBE_FLAG_RSS_ENABLED))
		return false;

	for (i = 0; i < adapter->num_rx_queues; i++)
		adapter->rx_ring[i]->reg_idx = i;
	for (i = 0; i < adapter->num_tx_queues; i++)
		adapter->tx_ring[i]->reg_idx = i;

	return true;
}

#ifdef CONFIG_IXGBE_DCB

/* ixgbe_get_first_reg_idx - Return first register index associated with ring */
static void ixgbe_get_first_reg_idx(struct ixgbe_adapter *adapter, u8 tc,
				    unsigned int *tx, unsigned int *rx)
{
	struct net_device *dev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u8 num_tcs = netdev_get_num_tc(dev);

	*tx = 0;
	*rx = 0;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		*tx = tc << 2;
		*rx = tc << 3;
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		if (num_tcs > 4) {
			if (tc < 3) {
				*tx = tc << 5;
				*rx = tc << 4;
			} else if (tc <  5) {
				*tx = ((tc + 2) << 4);
				*rx = tc << 4;
			} else if (tc < num_tcs) {
				*tx = ((tc + 8) << 3);
				*rx = tc << 4;
			}
		} else {
			*rx =  tc << 5;
			switch (tc) {
			case 0:
				*tx =  0;
				break;
			case 1:
				*tx = 64;
				break;
			case 2:
				*tx = 96;
				break;
			case 3:
				*tx = 112;
				break;
			default:
				break;
			}
		}
		break;
	default:
		break;
	}
}

/**
 * ixgbe_cache_ring_dcb - Descriptor ring to register mapping for DCB
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for DCB to the assigned rings.
 *
 **/
static inline bool ixgbe_cache_ring_dcb(struct ixgbe_adapter *adapter)
{
	struct net_device *dev = adapter->netdev;
	int i, j, k;
	u8 num_tcs = netdev_get_num_tc(dev);

	if (!num_tcs)
		return false;

	for (i = 0, k = 0; i < num_tcs; i++) {
		unsigned int tx_s, rx_s;
		u16 count = dev->tc_to_txq[i].count;

		ixgbe_get_first_reg_idx(adapter, i, &tx_s, &rx_s);
		for (j = 0; j < count; j++, k++) {
			adapter->tx_ring[k]->reg_idx = tx_s + j;
			adapter->rx_ring[k]->reg_idx = rx_s + j;
			adapter->tx_ring[k]->dcb_tc = i;
			adapter->rx_ring[k]->dcb_tc = i;
		}
	}

	return true;
}
#endif

/**
 * ixgbe_cache_ring_fdir - Descriptor ring to register mapping for Flow Director
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for Flow Director to the assigned rings.
 *
 **/
static inline bool ixgbe_cache_ring_fdir(struct ixgbe_adapter *adapter)
{
	int i;
	bool ret = false;

	if ((adapter->flags & IXGBE_FLAG_RSS_ENABLED) &&
	    (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE)) {
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->reg_idx = i;
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->reg_idx = i;
		ret = true;
	}

	return ret;
}

#ifdef IXGBE_FCOE
/**
 * ixgbe_cache_ring_fcoe - Descriptor ring to register mapping for the FCoE
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for FCoE mode to the assigned rings.
 *
 */
static inline bool ixgbe_cache_ring_fcoe(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring_feature *f = &adapter->ring_feature[RING_F_FCOE];
	int i;
	u8 fcoe_rx_i = 0, fcoe_tx_i = 0;

	if (!(adapter->flags & IXGBE_FLAG_FCOE_ENABLED))
		return false;

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED) {
		if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE)
			ixgbe_cache_ring_fdir(adapter);
		else
			ixgbe_cache_ring_rss(adapter);

		fcoe_rx_i = f->mask;
		fcoe_tx_i = f->mask;
	}
	for (i = 0; i < f->indices; i++, fcoe_rx_i++, fcoe_tx_i++) {
		adapter->rx_ring[f->mask + i]->reg_idx = fcoe_rx_i;
		adapter->tx_ring[f->mask + i]->reg_idx = fcoe_tx_i;
	}
	return true;
}

#endif /* IXGBE_FCOE */
/**
 * ixgbe_cache_ring_sriov - Descriptor ring to register mapping for sriov
 * @adapter: board private structure to initialize
 *
 * SR-IOV doesn't use any descriptor rings but changes the default if
 * no other mapping is used.
 *
 */
static inline bool ixgbe_cache_ring_sriov(struct ixgbe_adapter *adapter)
{
	adapter->rx_ring[0]->reg_idx = adapter->num_vfs * 2;
	adapter->tx_ring[0]->reg_idx = adapter->num_vfs * 2;
	if (adapter->num_vfs)
		return true;
	else
		return false;
}

/**
 * ixgbe_cache_ring_register - Descriptor ring to register mapping
 * @adapter: board private structure to initialize
 *
 * Once we know the feature-set enabled for the device, we'll cache
 * the register offset the descriptor ring is assigned to.
 *
 * Note, the order the various feature calls is important.  It must start with
 * the "most" features enabled at the same time, then trickle down to the
 * least amount of features turned on at once.
 **/
static void ixgbe_cache_ring_register(struct ixgbe_adapter *adapter)
{
	/* start with default case */
	adapter->rx_ring[0]->reg_idx = 0;
	adapter->tx_ring[0]->reg_idx = 0;

	if (ixgbe_cache_ring_sriov(adapter))
		return;

#ifdef CONFIG_IXGBE_DCB
	if (ixgbe_cache_ring_dcb(adapter))
		return;
#endif

#ifdef IXGBE_FCOE
	if (ixgbe_cache_ring_fcoe(adapter))
		return;
#endif /* IXGBE_FCOE */

	if (ixgbe_cache_ring_fdir(adapter))
		return;

	if (ixgbe_cache_ring_rss(adapter))
		return;
}

/**
 * ixgbe_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.  The polling_netdev array is
 * intended for Multiqueue, but should work fine with a single queue.
 **/
static int ixgbe_alloc_queues(struct ixgbe_adapter *adapter)
{
	int rx = 0, tx = 0, nid = adapter->node;

	if (nid < 0 || !node_online(nid))
		nid = first_online_node;

	for (; tx < adapter->num_tx_queues; tx++) {
		struct ixgbe_ring *ring;

		ring = kzalloc_node(sizeof(*ring), GFP_KERNEL, nid);
		if (!ring)
			ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring)
			goto err_allocation;
		ring->count = adapter->tx_ring_count;
		ring->queue_index = tx;
		ring->numa_node = nid;
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		adapter->tx_ring[tx] = ring;
	}

	for (; rx < adapter->num_rx_queues; rx++) {
		struct ixgbe_ring *ring;

		ring = kzalloc_node(sizeof(*ring), GFP_KERNEL, nid);
		if (!ring)
			ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring)
			goto err_allocation;
		ring->count = adapter->rx_ring_count;
		ring->queue_index = rx;
		ring->numa_node = nid;
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		adapter->rx_ring[rx] = ring;
	}

	ixgbe_cache_ring_register(adapter);

	return 0;

err_allocation:
	while (tx)
		kfree(adapter->tx_ring[--tx]);

	while (rx)
		kfree(adapter->rx_ring[--rx]);
	return -ENOMEM;
}

/**
 * ixgbe_set_interrupt_capability - set MSI-X or MSI if supported
 * @adapter: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int ixgbe_set_interrupt_capability(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int err = 0;
	int vector, v_budget;

	/*
	 * It's easy to be greedy for MSI-X vectors, but it really
	 * doesn't do us much good if we have a lot more vectors
	 * than CPU's.  So let's be conservative and only ask for
	 * (roughly) the same number of vectors as there are CPU's.
	 */
//	v_budget = min(adapter->num_rx_queues + adapter->num_tx_queues,
//		       (int)num_online_cpus()) + NON_Q_VECTORS;

		v_budget = min(adapter->num_rx_queues + adapter->num_tx_queues,
		       24) + NON_Q_VECTORS;


	/*
	 * At the same time, hardware can only support a maximum of
	 * hw.mac->max_msix_vectors vectors.  With features
	 * such as RSS and VMDq, we can easily surpass the number of Rx and Tx
	 * descriptor queues supported by our device.  Thus, we cap it off in
	 * those rare cases where the cpu count also exceeds our vector limit.
	 */
	v_budget = min(v_budget, (int)hw->mac.max_msix_vectors);

	/* A failure in MSI-X entry allocation isn't fatal, but it does
	 * mean we disable MSI-X capabilities of the adapter. */
	adapter->msix_entries = kcalloc(v_budget,
					sizeof(struct msix_entry), GFP_KERNEL);
	if (adapter->msix_entries) {
		for (vector = 0; vector < v_budget; vector++)
			adapter->msix_entries[vector].entry = vector;

		ixgbe_acquire_msix_vectors(adapter, v_budget);

		if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
			goto out;
	}

	adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;
	adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
	if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) {
		e_err(probe,
		      "ATR is not supported while multiple "
		      "queues are disabled.  Disabling Flow Director\n");
	}
	adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;
	adapter->atr_sample_rate = 0;
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		ixgbe_disable_sriov(adapter);

	err = ixgbe_set_num_queues(adapter);
	if (err)
		return err;

	err = pci_enable_msi(adapter->pdev);
	if (!err) {
		adapter->flags |= IXGBE_FLAG_MSI_ENABLED;
	} else {
		netif_printk(adapter, hw, KERN_DEBUG, adapter->netdev,
			     "Unable to allocate MSI interrupt, "
			     "falling back to legacy.  Error: %d\n", err);
		/* reset err */
		err = 0;
	}

out:
	return err;
}

/**
 * ixgbe_alloc_q_vectors - Allocate memory for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int ixgbe_alloc_q_vectors(struct ixgbe_adapter *adapter)
{
	int v_idx, num_q_vectors;
	struct ixgbe_q_vector *q_vector;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		num_q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	else
		num_q_vectors = 1;

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		q_vector = kzalloc_node(sizeof(struct ixgbe_q_vector),
					GFP_KERNEL, adapter->node);
		if (!q_vector)
			q_vector = kzalloc(sizeof(struct ixgbe_q_vector),
					   GFP_KERNEL);
		if (!q_vector)
			goto err_out;

		q_vector->adapter = adapter;
		q_vector->v_idx = v_idx;

		/* Allocate the affinity_hint cpumask, configure the mask */
		if (!alloc_cpumask_var(&q_vector->affinity_mask, GFP_KERNEL))
			goto err_out;
		cpumask_set_cpu(v_idx, q_vector->affinity_mask);
		netif_napi_add(adapter->netdev, &q_vector->napi,
			       ixgbe_poll, 64);
		adapter->q_vector[v_idx] = q_vector;
	}

	return 0;

err_out:
	while (v_idx) {
		v_idx--;
		q_vector = adapter->q_vector[v_idx];
		netif_napi_del(&q_vector->napi);
		free_cpumask_var(q_vector->affinity_mask);
		kfree(q_vector);
		adapter->q_vector[v_idx] = NULL;
	}
	return -ENOMEM;
}

/**
 * ixgbe_free_q_vectors - Free memory allocated for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void ixgbe_free_q_vectors(struct ixgbe_adapter *adapter)
{
	int v_idx, num_q_vectors;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		num_q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	else
		num_q_vectors = 1;

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		struct ixgbe_q_vector *q_vector = adapter->q_vector[v_idx];
		adapter->q_vector[v_idx] = NULL;
		netif_napi_del(&q_vector->napi);
		free_cpumask_var(q_vector->affinity_mask);
		kfree(q_vector);
	}
}

static void ixgbe_reset_interrupt_capability(struct ixgbe_adapter *adapter)
{
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSI_ENABLED;
		pci_disable_msi(adapter->pdev);
	}
}

/**
 * ixgbe_init_interrupt_scheme - Determine proper interrupt scheme
 * @adapter: board private structure to initialize
 *
 * We determine which interrupt scheme to use based on...
 * - Kernel support (MSI, MSI-X)
 *   - which can be user-defined (via MODULE_PARAM)
 * - Hardware queue count (num_*_queues)
 *   - defined by miscellaneous hardware support/features (RSS, etc.)
 **/
int ixgbe_init_interrupt_scheme(struct ixgbe_adapter *adapter)
{
	int err;

	printk("\n%s:called\n",__func__);

	/* Number of supported queues */
	err = ixgbe_set_num_queues(adapter);
	if (err)
		return err;

	err = ixgbe_set_interrupt_capability(adapter);
	if (err) {
		e_dev_err("Unable to setup interrupt capabilities\n");
		goto err_set_interrupt;
	}

	err = ixgbe_alloc_q_vectors(adapter);
	if (err) {
		e_dev_err("Unable to allocate memory for queue vectors\n");
		goto err_alloc_q_vectors;
	}

	err = ixgbe_alloc_queues(adapter);
	if (err) {
		e_dev_err("Unable to allocate memory for queues\n");
		goto err_alloc_queues;
	}

	e_dev_info("Multiqueue %s: Rx Queue count = %u, Tx Queue count = %u\n",
		   (adapter->num_rx_queues > 1) ? "Enabled" : "Disabled",
		   adapter->num_rx_queues, adapter->num_tx_queues);

	set_bit(__IXGBE_DOWN, &adapter->state);

	return 0;

err_alloc_queues:
	ixgbe_free_q_vectors(adapter);
err_alloc_q_vectors:
	ixgbe_reset_interrupt_capability(adapter);
err_set_interrupt:
	return err;
}

/**
 * ixgbe_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @adapter: board private structure to clear interrupt scheme on
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
void ixgbe_clear_interrupt_scheme(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		kfree(adapter->tx_ring[i]);
		adapter->tx_ring[i] = NULL;
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct ixgbe_ring *ring = adapter->rx_ring[i];

		/* ixgbe_get_stats64() might access this ring, we must wait
		 * a grace period before freeing it.
		 */
		kfree_rcu(ring, rcu);
		adapter->rx_ring[i] = NULL;
	}

	adapter->num_tx_queues = 0;
	adapter->num_rx_queues = 0;

	ixgbe_free_q_vectors(adapter);
	ixgbe_reset_interrupt_capability(adapter);
}

/**
 * ixgbe_sw_init - Initialize general software structures (struct ixgbe_adapter)
 * @adapter: board private structure to initialize
 *
 * ixgbe_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int __devinit ixgbe_sw_init(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	unsigned int rss;
#ifdef CONFIG_IXGBE_DCB
	int j;
	struct tc_configuration *tc;
#endif

	/* PCI config space info */

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* Set capability flags */
	rss = min(IXGBE_MAX_RSS_INDICES, (int)num_online_cpus());
	adapter->ring_feature[RING_F_RSS].indices = rss;
	adapter->flags |= IXGBE_FLAG_RSS_ENABLED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		if (hw->device_id == IXGBE_DEV_ID_82598AT)
			adapter->flags |= IXGBE_FLAG_FAN_FAIL_CAPABLE;
		adapter->max_msix_q_vectors = MAX_MSIX_Q_VECTORS_82598;
		break;
	case ixgbe_mac_X540:
		adapter->flags2 |= IXGBE_FLAG2_TEMP_SENSOR_CAPABLE;
	case ixgbe_mac_82599EB:
		adapter->max_msix_q_vectors = MAX_MSIX_Q_VECTORS_82599;
		adapter->flags2 |= IXGBE_FLAG2_RSC_CAPABLE;
		adapter->flags2 |= IXGBE_FLAG2_RSC_ENABLED;
		if (hw->device_id == IXGBE_DEV_ID_82599_T3_LOM)
			adapter->flags2 |= IXGBE_FLAG2_TEMP_SENSOR_CAPABLE;
		/* Flow Director hash filters enabled */
		adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;
		adapter->atr_sample_rate = 20;
		adapter->ring_feature[RING_F_FDIR].indices =
							 IXGBE_MAX_FDIR_INDICES;
		adapter->fdir_pballoc = IXGBE_FDIR_PBALLOC_64K;
#ifdef IXGBE_FCOE
		adapter->flags |= IXGBE_FLAG_FCOE_CAPABLE;
		adapter->flags &= ~IXGBE_FLAG_FCOE_ENABLED;
		adapter->ring_feature[RING_F_FCOE].indices = 0;
#ifdef CONFIG_IXGBE_DCB
		/* Default traffic class to use for FCoE */
		adapter->fcoe.up = IXGBE_FCOE_DEFTC;
#endif
#endif /* IXGBE_FCOE */
		break;
	default:
		break;
	}

	/* n-tuple support exists, always init our spinlock */
	spin_lock_init(&adapter->fdir_perfect_lock);

#ifdef CONFIG_IXGBE_DCB
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		adapter->dcb_cfg.num_tcs.pg_tcs = X540_TRAFFIC_CLASS;
		adapter->dcb_cfg.num_tcs.pfc_tcs = X540_TRAFFIC_CLASS;
		break;
	default:
		adapter->dcb_cfg.num_tcs.pg_tcs = MAX_TRAFFIC_CLASS;
		adapter->dcb_cfg.num_tcs.pfc_tcs = MAX_TRAFFIC_CLASS;
		break;
	}

	/* Configure DCB traffic classes */
	for (j = 0; j < MAX_TRAFFIC_CLASS; j++) {
		tc = &adapter->dcb_cfg.tc_config[j];
		tc->path[DCB_TX_CONFIG].bwg_id = 0;
		tc->path[DCB_TX_CONFIG].bwg_percent = 12 + (j & 1);
		tc->path[DCB_RX_CONFIG].bwg_id = 0;
		tc->path[DCB_RX_CONFIG].bwg_percent = 12 + (j & 1);
		tc->dcb_pfc = pfc_disabled;
	}

	/* Initialize default user to priority mapping, UPx->TC0 */
	tc = &adapter->dcb_cfg.tc_config[0];
	tc->path[DCB_TX_CONFIG].up_to_tc_bitmap = 0xFF;
	tc->path[DCB_RX_CONFIG].up_to_tc_bitmap = 0xFF;

	adapter->dcb_cfg.bw_percentage[DCB_TX_CONFIG][0] = 100;
	adapter->dcb_cfg.bw_percentage[DCB_RX_CONFIG][0] = 100;
	adapter->dcb_cfg.pfc_mode_enable = false;
	adapter->dcb_set_bitmap = 0x00;
	adapter->dcbx_cap = DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_CEE;
	ixgbe_copy_dcb_cfg(&adapter->dcb_cfg, &adapter->temp_dcb_cfg,
			   MAX_TRAFFIC_CLASS);

#endif

	/* default flow control settings */
	hw->fc.requested_mode = ixgbe_fc_full;
	hw->fc.current_mode = ixgbe_fc_full;	/* init for ethtool output */
#ifdef CONFIG_DCB
	adapter->last_lfc_mode = hw->fc.current_mode;
#endif
	ixgbe_pbthresh_setup(adapter);
	hw->fc.pause_time = IXGBE_DEFAULT_FCPAUSE;
	hw->fc.send_xon = true;
	hw->fc.disable_fc_autoneg = false;

	/* enable itr by default in dynamic mode */
	adapter->rx_itr_setting = 1;
	adapter->tx_itr_setting = 1;

	/* set defaults for eitr in MegaBytes */
	adapter->eitr_low = 10;
	adapter->eitr_high = 20;

	/* set default ring sizes */
	adapter->tx_ring_count = IXGBE_DEFAULT_TXD;
	adapter->rx_ring_count = IXGBE_DEFAULT_RXD;

	/* set default work limits */
	adapter->tx_work_limit = IXGBE_DEFAULT_TX_WORK;

	/* initialize eeprom parameters */
	if (ixgbe_init_eeprom_params_generic(hw)) {
		e_dev_err("EEPROM initialization failed\n");
		return -EIO;
	}

	/* enable rx csum by default */
	adapter->flags |= IXGBE_FLAG_RX_CSUM_ENABLED;

	/* get assigned NUMA node */
	adapter->node = dev_to_node(&pdev->dev);

	set_bit(__IXGBE_DOWN, &adapter->state);

	return 0;
}

/**
 * ixgbe_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
int ixgbe_setup_tx_resources(struct ixgbe_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int size;

	size = sizeof(struct ixgbe_tx_buffer) * tx_ring->count;
//	tx_ring->tx_buffer_info = vzalloc_node(size, tx_ring->numa_node);
	tx_ring->tx_buffer_info = kzalloc_node(size,GFP_KERNEL,tx_ring->numa_node);
	
	if (!tx_ring->tx_buffer_info)
		tx_ring->tx_buffer_info = kzalloc(size,GFP_KERNEL);
	if (!tx_ring->tx_buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(union ixgbe_adv_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
					   
	tx_ring->tx_rx_buffer_info_phys=virt_to_phys(tx_ring->tx_buffer_info);
	printk("%s: tx_ring->size %d tx_ring->desc %p tx_ring->tx_buffer_info_phy %p tx_ring->tx_buffer_info\n",__func__,tx_ring->size,tx_ring->desc,tx_ring->tx_rx_buffer_info_phys,tx_ring->tx_buffer_info);
	
	
	if (!tx_ring->desc)
		goto err;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	
	
	
	
	return 0;

err:
//	vfree(tx_ring->tx_buffer_info);
	kfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Tx descriptor ring\n");
	return -ENOMEM;
}

/**
 * ixgbe_setup_all_tx_resources - allocate all queues Tx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int ixgbe_setup_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = ixgbe_setup_tx_resources(adapter->tx_ring[i]);
		if (!err)
		{
			tx_ring_from_adapter[i] = adapter->tx_ring[i];
			continue;
		}
		e_err(probe, "Allocation for Tx Queue %u failed\n", i);
		break;
	}

	return err;
}
struct ixgbe_ring * get_tx_ring(int q_no)
{
	return tx_ring_from_adapter[q_no];
}
EXPORT_SYMBOL(get_tx_ring);


struct ixgbe_ring * get_rx_ring(int q_no)
{
	return rx_ring_from_adapter[q_no];
}
EXPORT_SYMBOL(get_rx_ring);

void print_tx_ring(struct ixgbe_ring *tx_ring)
{
	printk("%s: Ring id : Desc %p Device %p ixgbe_tx_buffer %p state %x tail %p \n count %d rx_buf_len %d queue_index %d reg_idx %d \n atr_sample_rate %d atr_count %d ring_size %d \n"
	,__func__,tx_ring->desc,tx_ring->netdev,tx_ring->tx_buffer_info,tx_ring->state,tx_ring->tail,tx_ring->count,tx_ring->rx_buf_len,tx_ring->queue_index,tx_ring->reg_idx,tx_ring->atr_sample_rate,tx_ring->atr_count,tx_ring->size);
	
	printk("%s:Phy Desc %p Device %p ixgbe_tx_buffer %p tail %p dma %x\n",__func__,virt_to_phys(tx_ring->desc),virt_to_phys(tx_ring->netdev),virt_to_phys(tx_ring->tx_buffer_info),virt_to_phys(tx_ring->tail),tx_ring->dma);

}


void print_rx_ring(struct ixgbe_ring *tx_ring)
{
	printk("%s: Ring id : Desc %p Device %p ixgbe_tx_buffer %p state %x tail %p \n count %d rx_buf_len %d queue_index %d reg_idx %d \n atr_sample_rate %d atr_count %d ring_size %d \n"
	,__func__,tx_ring->desc,tx_ring->netdev,tx_ring->tx_buffer_info,tx_ring->state,tx_ring->tail,tx_ring->count,tx_ring->rx_buf_len,tx_ring->queue_index,tx_ring->reg_idx,tx_ring->atr_sample_rate,tx_ring->atr_count,tx_ring->size);
	
	printk("%s:Phy Desc %p Device %p ixgbe_tx_buffer %p tail %p dma %x\n",__func__,virt_to_phys(tx_ring->desc),virt_to_phys(tx_ring->netdev),virt_to_phys(tx_ring->tx_buffer_info),virt_to_phys(tx_ring->tail),tx_ring->dma);

}

EXPORT_SYMBOL(print_tx_ring);

void print_tx_ring_desc(int q_no)
{
	printk("%s: tx_ring_add %p physical %p ixgbe_ring_size %d\n",__func__,tx_ring_from_adapter[q_no],virt_to_phys(tx_ring_from_adapter[q_no]),sizeof(struct ixgbe_ring));
	print_tx_ring(tx_ring_from_adapter[q_no]);
}
EXPORT_SYMBOL(print_tx_ring_desc);




/**
 * ixgbe_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int ixgbe_setup_rx_resources(struct ixgbe_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int size;

	size = sizeof(struct ixgbe_rx_buffer) * rx_ring->count;
//	rx_ring->rx_buffer_info = vzalloc_node(size, rx_ring->numa_node);
	
	rx_ring->rx_buffer_info = kzalloc_node(size,GFP_KERNEL,rx_ring->numa_node);
	if (!rx_ring->rx_buffer_info)
		rx_ring->rx_buffer_info = kzalloc(size,GFP_KERNEL);
	if (!rx_ring->rx_buffer_info)
		goto err;

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union ixgbe_adv_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	printk("%s: rx_ring->size %d rx_ring->desc %p\n",__func__,rx_ring->size,rx_ring->desc);
	rx_ring->tx_rx_buffer_info_phys=virt_to_phys(rx_ring->rx_buffer_info);
	if (!rx_ring->desc)
		goto err;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	return 0;
err:
//	vfree(rx_ring->rx_buffer_info);
	kfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Rx descriptor ring\n");
	return -ENOMEM;
}

EXPORT_SYMBOL(ixgbe_setup_rx_resources);


/**
 * ixgbe_setup_all_rx_resources - allocate all queues Rx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int ixgbe_setup_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = ixgbe_setup_rx_resources(adapter->rx_ring[i]);
		if (!err)
		{
			rx_ring_from_adapter[i] = adapter->rx_ring[i];
			continue;
		}
		e_err(probe, "Allocation for Rx Queue %u failed\n", i);
		break;
	}

	return err;
}

/**
 * ixgbe_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
void ixgbe_free_tx_resources(struct ixgbe_ring *tx_ring)
{
	ixgbe_clean_tx_ring(tx_ring);

//	vfree(tx_ring->tx_buffer_info);
	kfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
static void ixgbe_free_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		if (adapter->tx_ring[i]->desc)
			ixgbe_free_tx_resources(adapter->tx_ring[i]);
}

/**
 * ixgbe_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
void ixgbe_free_rx_resources(struct ixgbe_ring *rx_ring)
{
	ixgbe_clean_rx_ring(rx_ring);

//	vfree(rx_ring->rx_buffer_info);
	kfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/
static void ixgbe_free_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		if (adapter->rx_ring[i]->desc)
			ixgbe_free_rx_resources(adapter->rx_ring[i]);
}

/**
 * ixgbe_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;

	/* MTU < 68 is an error and causes problems on some kernels */
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED &&
	    hw->mac.type != ixgbe_mac_X540) {
		if ((new_mtu < 68) || (max_frame > MAXIMUM_ETHERNET_VLAN_SIZE))
			return -EINVAL;
	} else {
		if ((new_mtu < 68) || (max_frame > IXGBE_MAX_JUMBO_FRAME_SIZE))
			return -EINVAL;
	}

	e_info(probe, "changing MTU from %d to %d\n", netdev->mtu, new_mtu);
	/* must set new MTU before calling down or up */
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);

	return 0;
}

/**
 * ixgbe_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int ixgbe_open(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	
	adapter_ptr = adapter;
	
	int err;

	/* disallow open during test */
	if (test_bit(__IXGBE_TESTING, &adapter->state))
		return -EBUSY;

	netif_carrier_off(netdev);

	/* allocate transmit descriptors */
	err = ixgbe_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = ixgbe_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	ixgbe_configure(adapter);

	err = ixgbe_request_irq(adapter);
	if (err)
		goto err_req_irq;

	ixgbe_up_complete(adapter);
	
	adapter_ptr = adapter;
	adapter_ptr_phy = virt_to_phys(adapter);
	printk("%s: adapter_ptr_phys %p\n",__func__,adapter_ptr_phy);
	

	return 0;

err_req_irq:
err_setup_rx:
	ixgbe_free_all_rx_resources(adapter);
err_setup_tx:
	ixgbe_free_all_tx_resources(adapter);
	ixgbe_reset(adapter);

	return err;
}

/**
 * ixgbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int ixgbe_close(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	ixgbe_down(adapter);
	ixgbe_free_irq(adapter);

	ixgbe_fdir_filter_exit(adapter);

	ixgbe_free_all_tx_resources(adapter);
	ixgbe_free_all_rx_resources(adapter);

	ixgbe_release_hw_control(adapter);

	return 0;
}

#ifdef CONFIG_PM
static int ixgbe_resume(struct pci_dev *pdev)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	/*
	 * pci_restore_state clears dev->state_saved so call
	 * pci_save_state to restore it.
	 */
	pci_save_state(pdev);

	err = pci_enable_device_mem(pdev);
	if (err) {
		e_dev_err("Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_wake_from_d3(pdev, false);

	err = ixgbe_init_interrupt_scheme(adapter);
	if (err) {
		e_dev_err("Cannot initialize interrupts for device\n");
		return err;
	}

	ixgbe_reset(adapter);

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_WUS, ~0);

	if (netif_running(netdev)) {
		err = ixgbe_open(netdev);
		if (err)
			return err;
	}

	netif_device_attach(netdev);

	return 0;
}
#endif /* CONFIG_PM */

static int __ixgbe_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 ctrl, fctrl;
	u32 wufc = adapter->wol;
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		ixgbe_down(adapter);
		ixgbe_free_irq(adapter);
		ixgbe_free_all_tx_resources(adapter);
		ixgbe_free_all_rx_resources(adapter);
	}

	ixgbe_clear_interrupt_scheme(adapter);
#ifdef CONFIG_DCB
	kfree(adapter->ixgbe_ieee_pfc);
	kfree(adapter->ixgbe_ieee_ets);
#endif

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;

#endif
	if (wufc) {
		ixgbe_set_rx_mode(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & IXGBE_WUFC_MC) {
			fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
			fctrl |= IXGBE_FCTRL_MPE;
			IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);
		}

		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		ctrl |= IXGBE_CTRL_GIO_DIS;
		IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);

		IXGBE_WRITE_REG(hw, IXGBE_WUFC, wufc);
	} else {
		IXGBE_WRITE_REG(hw, IXGBE_WUC, 0);
		IXGBE_WRITE_REG(hw, IXGBE_WUFC, 0);
	}

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		pci_wake_from_d3(pdev, false);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		pci_wake_from_d3(pdev, !!wufc);
		break;
	default:
		break;
	}

	*enable_wake = !!wufc;

	ixgbe_release_hw_control(adapter);

	pci_disable_device(pdev);

	return 0;
}

#ifdef CONFIG_PM
static int ixgbe_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int retval;
	bool wake;

	retval = __ixgbe_shutdown(pdev, &wake);
	if (retval)
		return retval;

	if (wake) {
		pci_prepare_to_sleep(pdev);
	} else {
		pci_wake_from_d3(pdev, false);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;
}
#endif /* CONFIG_PM */

static void ixgbe_shutdown(struct pci_dev *pdev)
{
	bool wake;

	__ixgbe_shutdown(pdev, &wake);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * ixgbe_update_stats - Update the board statistics counters.
 * @adapter: board private structure
 **/
void ixgbe_update_stats(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_hw_stats *hwstats = &adapter->stats;
	u64 total_mpc = 0;
	u32 i, missed_rx = 0, mpc, bprc, lxon, lxoff, xon_off_tot;
	u64 non_eop_descs = 0, restart_queue = 0, tx_busy = 0;
	u64 alloc_rx_page_failed = 0, alloc_rx_buff_failed = 0;
	u64 bytes = 0, packets = 0;
#ifdef IXGBE_FCOE
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;
	unsigned int cpu;
	u64 fcoe_noddp_counts_sum = 0, fcoe_noddp_ext_buff_counts_sum = 0;
#endif /* IXGBE_FCOE */

	if (test_bit(__IXGBE_DOWN, &adapter->state) ||
	    test_bit(__IXGBE_RESETTING, &adapter->state))
		return;

	if (adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED) {
		u64 rsc_count = 0;
		u64 rsc_flush = 0;
		for (i = 0; i < 16; i++)
			adapter->hw_rx_no_dma_resources +=
				IXGBE_READ_REG(hw, IXGBE_QPRDC(i));
		for (i = 0; i < adapter->num_rx_queues; i++) {
			rsc_count += adapter->rx_ring[i]->rx_stats.rsc_count;
			rsc_flush += adapter->rx_ring[i]->rx_stats.rsc_flush;
		}
		adapter->rsc_total_count = rsc_count;
		adapter->rsc_total_flush = rsc_flush;
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct ixgbe_ring *rx_ring = adapter->rx_ring[i];
		non_eop_descs += rx_ring->rx_stats.non_eop_descs;
		alloc_rx_page_failed += rx_ring->rx_stats.alloc_rx_page_failed;
		alloc_rx_buff_failed += rx_ring->rx_stats.alloc_rx_buff_failed;
		bytes += rx_ring->stats.bytes;
		packets += rx_ring->stats.packets;
	}
	adapter->non_eop_descs = non_eop_descs;
	adapter->alloc_rx_page_failed = alloc_rx_page_failed;
	adapter->alloc_rx_buff_failed = alloc_rx_buff_failed;
	netdev->stats.rx_bytes = bytes;
	netdev->stats.rx_packets = packets;

	bytes = 0;
	packets = 0;
	/* gather some stats to the adapter struct that are per queue */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct ixgbe_ring *tx_ring = adapter->tx_ring[i];
		restart_queue += tx_ring->tx_stats.restart_queue;
		tx_busy += tx_ring->tx_stats.tx_busy;
		bytes += tx_ring->stats.bytes;
		packets += tx_ring->stats.packets;
	}
	adapter->restart_queue = restart_queue;
	adapter->tx_busy = tx_busy;
	netdev->stats.tx_bytes = bytes;
	netdev->stats.tx_packets = packets;

	hwstats->crcerrs += IXGBE_READ_REG(hw, IXGBE_CRCERRS);

	/* 8 register reads */
	for (i = 0; i < 8; i++) {
		/* for packet buffers not used, the register should read 0 */
		mpc = IXGBE_READ_REG(hw, IXGBE_MPC(i));
		missed_rx += mpc;
		hwstats->mpc[i] += mpc;
		total_mpc += hwstats->mpc[i];
		hwstats->pxontxc[i] += IXGBE_READ_REG(hw, IXGBE_PXONTXC(i));
		hwstats->pxofftxc[i] += IXGBE_READ_REG(hw, IXGBE_PXOFFTXC(i));
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			hwstats->rnbc[i] += IXGBE_READ_REG(hw, IXGBE_RNBC(i));
			hwstats->qbtc[i] += IXGBE_READ_REG(hw, IXGBE_QBTC(i));
			hwstats->qbrc[i] += IXGBE_READ_REG(hw, IXGBE_QBRC(i));
			hwstats->pxonrxc[i] +=
				IXGBE_READ_REG(hw, IXGBE_PXONRXC(i));
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			hwstats->pxonrxc[i] +=
				IXGBE_READ_REG(hw, IXGBE_PXONRXCNT(i));
			break;
		default:
			break;
		}
	}

	/*16 register reads */
	for (i = 0; i < 16; i++) {
		hwstats->qptc[i] += IXGBE_READ_REG(hw, IXGBE_QPTC(i));
		hwstats->qprc[i] += IXGBE_READ_REG(hw, IXGBE_QPRC(i));
		if ((hw->mac.type == ixgbe_mac_82599EB) ||
		    (hw->mac.type == ixgbe_mac_X540)) {
			hwstats->qbtc[i] += IXGBE_READ_REG(hw, IXGBE_QBTC_L(i));
			IXGBE_READ_REG(hw, IXGBE_QBTC_H(i)); /* to clear */
			hwstats->qbrc[i] += IXGBE_READ_REG(hw, IXGBE_QBRC_L(i));
			IXGBE_READ_REG(hw, IXGBE_QBRC_H(i)); /* to clear */
		}
	}

	hwstats->gprc += IXGBE_READ_REG(hw, IXGBE_GPRC);
	/* work around hardware counting issue */
	hwstats->gprc -= missed_rx;

	ixgbe_update_xoff_received(adapter);

	/* 82598 hardware only has a 32 bit counter in the high register */
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		hwstats->lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXC);
		hwstats->gorc += IXGBE_READ_REG(hw, IXGBE_GORCH);
		hwstats->gotc += IXGBE_READ_REG(hw, IXGBE_GOTCH);
		hwstats->tor += IXGBE_READ_REG(hw, IXGBE_TORH);
		break;
	case ixgbe_mac_X540:
		/* OS2BMC stats are X540 only*/
		hwstats->o2bgptc += IXGBE_READ_REG(hw, IXGBE_O2BGPTC);
		hwstats->o2bspc += IXGBE_READ_REG(hw, IXGBE_O2BSPC);
		hwstats->b2ospc += IXGBE_READ_REG(hw, IXGBE_B2OSPC);
		hwstats->b2ogprc += IXGBE_READ_REG(hw, IXGBE_B2OGPRC);
	case ixgbe_mac_82599EB:
		hwstats->gorc += IXGBE_READ_REG(hw, IXGBE_GORCL);
		IXGBE_READ_REG(hw, IXGBE_GORCH); /* to clear */
		hwstats->gotc += IXGBE_READ_REG(hw, IXGBE_GOTCL);
		IXGBE_READ_REG(hw, IXGBE_GOTCH); /* to clear */
		hwstats->tor += IXGBE_READ_REG(hw, IXGBE_TORL);
		IXGBE_READ_REG(hw, IXGBE_TORH); /* to clear */
		hwstats->lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXCNT);
		hwstats->fdirmatch += IXGBE_READ_REG(hw, IXGBE_FDIRMATCH);
		hwstats->fdirmiss += IXGBE_READ_REG(hw, IXGBE_FDIRMISS);
#ifdef IXGBE_FCOE
		hwstats->fccrc += IXGBE_READ_REG(hw, IXGBE_FCCRC);
		hwstats->fcoerpdc += IXGBE_READ_REG(hw, IXGBE_FCOERPDC);
		hwstats->fcoeprc += IXGBE_READ_REG(hw, IXGBE_FCOEPRC);
		hwstats->fcoeptc += IXGBE_READ_REG(hw, IXGBE_FCOEPTC);
		hwstats->fcoedwrc += IXGBE_READ_REG(hw, IXGBE_FCOEDWRC);
		hwstats->fcoedwtc += IXGBE_READ_REG(hw, IXGBE_FCOEDWTC);
		/* Add up per cpu counters for total ddp aloc fail */
		if (fcoe->pcpu_noddp && fcoe->pcpu_noddp_ext_buff) {
			for_each_possible_cpu(cpu) {
				fcoe_noddp_counts_sum +=
					*per_cpu_ptr(fcoe->pcpu_noddp, cpu);
				fcoe_noddp_ext_buff_counts_sum +=
					*per_cpu_ptr(fcoe->
						pcpu_noddp_ext_buff, cpu);
			}
		}
		hwstats->fcoe_noddp = fcoe_noddp_counts_sum;
		hwstats->fcoe_noddp_ext_buff = fcoe_noddp_ext_buff_counts_sum;
#endif /* IXGBE_FCOE */
		break;
	default:
		break;
	}
	bprc = IXGBE_READ_REG(hw, IXGBE_BPRC);
	hwstats->bprc += bprc;
	hwstats->mprc += IXGBE_READ_REG(hw, IXGBE_MPRC);
	if (hw->mac.type == ixgbe_mac_82598EB)
		hwstats->mprc -= bprc;
	hwstats->roc += IXGBE_READ_REG(hw, IXGBE_ROC);
	hwstats->prc64 += IXGBE_READ_REG(hw, IXGBE_PRC64);
	hwstats->prc127 += IXGBE_READ_REG(hw, IXGBE_PRC127);
	hwstats->prc255 += IXGBE_READ_REG(hw, IXGBE_PRC255);
	hwstats->prc511 += IXGBE_READ_REG(hw, IXGBE_PRC511);
	hwstats->prc1023 += IXGBE_READ_REG(hw, IXGBE_PRC1023);
	hwstats->prc1522 += IXGBE_READ_REG(hw, IXGBE_PRC1522);
	hwstats->rlec += IXGBE_READ_REG(hw, IXGBE_RLEC);
	lxon = IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	hwstats->lxontxc += lxon;
	lxoff = IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	hwstats->lxofftxc += lxoff;
	hwstats->gptc += IXGBE_READ_REG(hw, IXGBE_GPTC);
	hwstats->mptc += IXGBE_READ_REG(hw, IXGBE_MPTC);
	/*
	 * 82598 errata - tx of flow control packets is included in tx counters
	 */
	xon_off_tot = lxon + lxoff;
	hwstats->gptc -= xon_off_tot;
	hwstats->mptc -= xon_off_tot;
	hwstats->gotc -= (xon_off_tot * (ETH_ZLEN + ETH_FCS_LEN));
	hwstats->ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	hwstats->rfc += IXGBE_READ_REG(hw, IXGBE_RFC);
	hwstats->rjc += IXGBE_READ_REG(hw, IXGBE_RJC);
	hwstats->tpr += IXGBE_READ_REG(hw, IXGBE_TPR);
	hwstats->ptc64 += IXGBE_READ_REG(hw, IXGBE_PTC64);
	hwstats->ptc64 -= xon_off_tot;
	hwstats->ptc127 += IXGBE_READ_REG(hw, IXGBE_PTC127);
	hwstats->ptc255 += IXGBE_READ_REG(hw, IXGBE_PTC255);
	hwstats->ptc511 += IXGBE_READ_REG(hw, IXGBE_PTC511);
	hwstats->ptc1023 += IXGBE_READ_REG(hw, IXGBE_PTC1023);
	hwstats->ptc1522 += IXGBE_READ_REG(hw, IXGBE_PTC1522);
	hwstats->bptc += IXGBE_READ_REG(hw, IXGBE_BPTC);

	/* Fill out the OS statistics structure */
	netdev->stats.multicast = hwstats->mprc;

	/* Rx Errors */
	netdev->stats.rx_errors = hwstats->crcerrs + hwstats->rlec;
	netdev->stats.rx_dropped = 0;
	netdev->stats.rx_length_errors = hwstats->rlec;
	netdev->stats.rx_crc_errors = hwstats->crcerrs;
	netdev->stats.rx_missed_errors = total_mpc;
}

/**
 * ixgbe_fdir_reinit_subtask - worker thread to reinit FDIR filter table
 * @adapter - pointer to the device adapter structure
 **/
static void ixgbe_fdir_reinit_subtask(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int i;

	if (!(adapter->flags2 & IXGBE_FLAG2_FDIR_REQUIRES_REINIT))
		return;

	adapter->flags2 &= ~IXGBE_FLAG2_FDIR_REQUIRES_REINIT;

	/* if interface is down do nothing */
	if (test_bit(__IXGBE_DOWN, &adapter->state))
		return;

	/* do nothing if we are not using signature filters */
	if (!(adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE))
		return;

	adapter->fdir_overflow++;

	if (ixgbe_reinit_fdir_tables_82599(hw) == 0) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			set_bit(__IXGBE_TX_FDIR_INIT_DONE,
			        &(adapter->tx_ring[i]->state));
		/* re-enable flow director interrupts */
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMS_FLOW_DIR);
	} else {
		e_err(probe, "failed to finish FDIR re-initialization, "
		      "ignored adding FDIR ATR filters\n");
	}
}

/**
 * ixgbe_check_hang_subtask - check for hung queues and dropped interrupts
 * @adapter - pointer to the device adapter structure
 *
 * This function serves two purposes.  First it strobes the interrupt lines
 * in order to make certain interrupts are occuring.  Secondly it sets the
 * bits needed to check for TX hangs.  As a result we should immediately
 * determine if a hang has occured.
 */
static void ixgbe_check_hang_subtask(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u64 eics = 0;
	int i;

	/* If we're down or resetting, just bail */
	if (test_bit(__IXGBE_DOWN, &adapter->state) ||
	    test_bit(__IXGBE_RESETTING, &adapter->state))
		return;

	/* Force detection of hung controller */
	if (netif_carrier_ok(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			set_check_for_tx_hang(adapter->tx_ring[i]);
	}

	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
		/*
		 * for legacy and MSI interrupts don't set any bits
		 * that are enabled for EIAM, because this operation
		 * would set *both* EIMS and EICS for any bit in EIAM
		 */
		IXGBE_WRITE_REG(hw, IXGBE_EICS,
			(IXGBE_EICS_TCP_TIMER | IXGBE_EICS_OTHER));
	} else {
		/* get one bit for every active tx/rx interrupt vector */
		for (i = 0; i < adapter->num_msix_vectors - NON_Q_VECTORS; i++) {
			struct ixgbe_q_vector *qv = adapter->q_vector[i];
			if (qv->rx.ring || qv->tx.ring)
				eics |= ((u64)1 << i);
		}
	}

	/* Cause software interrupt to ensure rings are cleaned */
	ixgbe_irq_rearm_queues(adapter, eics);

}

/**
 * ixgbe_watchdog_update_link - update the link status
 * @adapter - pointer to the device adapter structure
 * @link_speed - pointer to a u32 to store the link_speed
 **/
static void ixgbe_watchdog_update_link(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 link_speed = adapter->link_speed;
	bool link_up = adapter->link_up;
	int i;

	if (!(adapter->flags & IXGBE_FLAG_NEED_LINK_UPDATE))
		return;

	if (hw->mac.ops.check_link) {
		hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
	} else {
		/* always assume link is up, if no check link function */
		link_speed = IXGBE_LINK_SPEED_10GB_FULL;
		link_up = true;
	}
	if (link_up) {
		if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
			for (i = 0; i < MAX_TRAFFIC_CLASS; i++)
				hw->mac.ops.fc_enable(hw, i);
		} else {
			hw->mac.ops.fc_enable(hw, 0);
		}
	}

	if (link_up ||
	    time_after(jiffies, (adapter->link_check_timeout +
				 IXGBE_TRY_LINK_TIMEOUT))) {
		adapter->flags &= ~IXGBE_FLAG_NEED_LINK_UPDATE;
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMC_LSC);
		IXGBE_WRITE_FLUSH(hw);
	}

	adapter->link_up = link_up;
	adapter->link_speed = link_speed;
}

/**
 * ixgbe_watchdog_link_is_up - update netif_carrier status and
 *                             print link up message
 * @adapter - pointer to the device adapter structure
 **/
static void ixgbe_watchdog_link_is_up(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 link_speed = adapter->link_speed;
	bool flow_rx, flow_tx;

	/* only continue if link was previously down */
	if (netif_carrier_ok(netdev))
		return;

	adapter->flags2 &= ~IXGBE_FLAG2_SEARCH_FOR_SFP;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB: {
		u32 frctl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
		u32 rmcs = IXGBE_READ_REG(hw, IXGBE_RMCS);
		flow_rx = !!(frctl & IXGBE_FCTRL_RFCE);
		flow_tx = !!(rmcs & IXGBE_RMCS_TFCE_802_3X);
	}
		break;
	case ixgbe_mac_X540:
	case ixgbe_mac_82599EB: {
		u32 mflcn = IXGBE_READ_REG(hw, IXGBE_MFLCN);
		u32 fccfg = IXGBE_READ_REG(hw, IXGBE_FCCFG);
		flow_rx = !!(mflcn & IXGBE_MFLCN_RFCE);
		flow_tx = !!(fccfg & IXGBE_FCCFG_TFCE_802_3X);
	}
		break;
	default:
		flow_tx = false;
		flow_rx = false;
		break;
	}
	e_info(drv, "NIC Link is Up %s, Flow Control: %s\n",
	       (link_speed == IXGBE_LINK_SPEED_10GB_FULL ?
	       "10 Gbps" :
	       (link_speed == IXGBE_LINK_SPEED_1GB_FULL ?
	       "1 Gbps" :
	       (link_speed == IXGBE_LINK_SPEED_100_FULL ?
	       "100 Mbps" :
	       "unknown speed"))),
	       ((flow_rx && flow_tx) ? "RX/TX" :
	       (flow_rx ? "RX" :
	       (flow_tx ? "TX" : "None"))));

	netif_carrier_on(netdev);
	ixgbe_check_vf_rate_limit(adapter);
}

/**
 * ixgbe_watchdog_link_is_down - update netif_carrier status and
 *                               print link down message
 * @adapter - pointer to the adapter structure
 **/
static void ixgbe_watchdog_link_is_down(struct ixgbe_adapter* adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;

	adapter->link_up = false;
	adapter->link_speed = 0;

	/* only continue if link was up previously */
	if (!netif_carrier_ok(netdev))
		return;

	/* poll for SFP+ cable when link is down */
	if (ixgbe_is_sfp(hw) && hw->mac.type == ixgbe_mac_82598EB)
		adapter->flags2 |= IXGBE_FLAG2_SEARCH_FOR_SFP;

	e_info(drv, "NIC Link is Down\n");
	netif_carrier_off(netdev);
}

/**
 * ixgbe_watchdog_flush_tx - flush queues on link down
 * @adapter - pointer to the device adapter structure
 **/
static void ixgbe_watchdog_flush_tx(struct ixgbe_adapter *adapter)
{
	int i;
	int some_tx_pending = 0;

	if (!netif_carrier_ok(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			struct ixgbe_ring *tx_ring = adapter->tx_ring[i];
			if (tx_ring->next_to_use != tx_ring->next_to_clean) {
				some_tx_pending = 1;
				break;
			}
		}

		if (some_tx_pending) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context).
			 */
			adapter->flags2 |= IXGBE_FLAG2_RESET_REQUESTED;
		}
	}
}

static void ixgbe_spoof_check(struct ixgbe_adapter *adapter)
{
	u32 ssvpc;

	/* Do not perform spoof check for 82598 */
	if (adapter->hw.mac.type == ixgbe_mac_82598EB)
		return;

	ssvpc = IXGBE_READ_REG(&adapter->hw, IXGBE_SSVPC);

	/*
	 * ssvpc register is cleared on read, if zero then no
	 * spoofed packets in the last interval.
	 */
	if (!ssvpc)
		return;

	e_warn(drv, "%d Spoofed packets detected\n", ssvpc);
}

/**
 * ixgbe_watchdog_subtask - check and bring link up
 * @adapter - pointer to the device adapter structure
 **/
static void ixgbe_watchdog_subtask(struct ixgbe_adapter *adapter)
{
	/* if interface is down do nothing */
	if (test_bit(__IXGBE_DOWN, &adapter->state) ||
	    test_bit(__IXGBE_RESETTING, &adapter->state))
		return;

	ixgbe_watchdog_update_link(adapter);

	if (adapter->link_up)
		ixgbe_watchdog_link_is_up(adapter);
	else
		ixgbe_watchdog_link_is_down(adapter);

	ixgbe_spoof_check(adapter);
	ixgbe_update_stats(adapter);

	ixgbe_watchdog_flush_tx(adapter);
}

/**
 * ixgbe_sfp_detection_subtask - poll for SFP+ cable
 * @adapter - the ixgbe adapter structure
 **/
static void ixgbe_sfp_detection_subtask(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	s32 err;

	/* not searching for SFP so there is nothing to do here */
	if (!(adapter->flags2 & IXGBE_FLAG2_SEARCH_FOR_SFP) &&
	    !(adapter->flags2 & IXGBE_FLAG2_SFP_NEEDS_RESET))
		return;

	/* someone else is in init, wait until next service event */
	if (test_and_set_bit(__IXGBE_IN_SFP_INIT, &adapter->state))
		return;

	err = hw->phy.ops.identify_sfp(hw);
	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto sfp_out;

	if (err == IXGBE_ERR_SFP_NOT_PRESENT) {
		/* If no cable is present, then we need to reset
		 * the next time we find a good cable. */
		adapter->flags2 |= IXGBE_FLAG2_SFP_NEEDS_RESET;
	}

	/* exit on error */
	if (err)
		goto sfp_out;

	/* exit if reset not needed */
	if (!(adapter->flags2 & IXGBE_FLAG2_SFP_NEEDS_RESET))
		goto sfp_out;

	adapter->flags2 &= ~IXGBE_FLAG2_SFP_NEEDS_RESET;

	/*
	 * A module may be identified correctly, but the EEPROM may not have
	 * support for that module.  setup_sfp() will fail in that case, so
	 * we should not allow that module to load.
	 */
	if (hw->mac.type == ixgbe_mac_82598EB)
		err = hw->phy.ops.reset(hw);
	else
		err = hw->mac.ops.setup_sfp(hw);

	if (err == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto sfp_out;

	adapter->flags |= IXGBE_FLAG_NEED_LINK_CONFIG;
	e_info(probe, "detected SFP+: %d\n", hw->phy.sfp_type);

sfp_out:
	clear_bit(__IXGBE_IN_SFP_INIT, &adapter->state);

	if ((err == IXGBE_ERR_SFP_NOT_SUPPORTED) &&
	    (adapter->netdev->reg_state == NETREG_REGISTERED)) {
		e_dev_err("failed to initialize because an unsupported "
			  "SFP+ module type was detected.\n");
		e_dev_err("Reload the driver after installing a "
			  "supported module.\n");
		unregister_netdev(adapter->netdev);
	}
}

/**
 * ixgbe_sfp_link_config_subtask - set up link SFP after module install
 * @adapter - the ixgbe adapter structure
 **/
static void ixgbe_sfp_link_config_subtask(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 autoneg;
	bool negotiation;

	if (!(adapter->flags & IXGBE_FLAG_NEED_LINK_CONFIG))
		return;

	/* someone else is in init, wait until next service event */
	if (test_and_set_bit(__IXGBE_IN_SFP_INIT, &adapter->state))
		return;

	adapter->flags &= ~IXGBE_FLAG_NEED_LINK_CONFIG;

	autoneg = hw->phy.autoneg_advertised;
	if ((!autoneg) && (hw->mac.ops.get_link_capabilities))
		hw->mac.ops.get_link_capabilities(hw, &autoneg, &negotiation);
	if (hw->mac.ops.setup_link)
		hw->mac.ops.setup_link(hw, autoneg, negotiation, true);

	adapter->flags |= IXGBE_FLAG_NEED_LINK_UPDATE;
	adapter->link_check_timeout = jiffies;
	clear_bit(__IXGBE_IN_SFP_INIT, &adapter->state);
}

#ifdef CONFIG_PCI_IOV
static void ixgbe_check_for_bad_vf(struct ixgbe_adapter *adapter)
{
	int vf;
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 gpc;
	u32 ciaa, ciad;

	gpc = IXGBE_READ_REG(hw, IXGBE_TXDGPC);
	if (gpc) /* If incrementing then no need for the check below */
		return;
	/*
	 * Check to see if a bad DMA write target from an errant or
	 * malicious VF has caused a PCIe error.  If so then we can
	 * issue a VFLR to the offending VF(s) and then resume without
	 * requesting a full slot reset.
	 */

	for (vf = 0; vf < adapter->num_vfs; vf++) {
		ciaa = (vf << 16) | 0x80000000;
		/* 32 bit read so align, we really want status at offset 6 */
		ciaa |= PCI_COMMAND;
		IXGBE_WRITE_REG(hw, IXGBE_CIAA_82599, ciaa);
		ciad = IXGBE_READ_REG(hw, IXGBE_CIAD_82599);
		ciaa &= 0x7FFFFFFF;
		/* disable debug mode asap after reading data */
		IXGBE_WRITE_REG(hw, IXGBE_CIAA_82599, ciaa);
		/* Get the upper 16 bits which will be the PCI status reg */
		ciad >>= 16;
		if (ciad & PCI_STATUS_REC_MASTER_ABORT) {
			netdev_err(netdev, "VF %d Hung DMA\n", vf);
			/* Issue VFLR */
			ciaa = (vf << 16) | 0x80000000;
			ciaa |= 0xA8;
			IXGBE_WRITE_REG(hw, IXGBE_CIAA_82599, ciaa);
			ciad = 0x00008000;  /* VFLR */
			IXGBE_WRITE_REG(hw, IXGBE_CIAD_82599, ciad);
			ciaa &= 0x7FFFFFFF;
			IXGBE_WRITE_REG(hw, IXGBE_CIAA_82599, ciaa);
		}
	}
}

#endif
/**
 * ixgbe_service_timer - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void ixgbe_service_timer(unsigned long data)
{
	struct ixgbe_adapter *adapter = (struct ixgbe_adapter *)data;
	unsigned long next_event_offset;
	bool ready = true;

#ifdef CONFIG_PCI_IOV
	ready = false;

	/*
	 * don't bother with SR-IOV VF DMA hang check if there are
	 * no VFs or the link is down
	 */
	if (!adapter->num_vfs ||
	    (adapter->flags & IXGBE_FLAG_NEED_LINK_UPDATE)) {
		ready = true;
		goto normal_timer_service;
	}

	/* If we have VFs allocated then we must check for DMA hangs */
	ixgbe_check_for_bad_vf(adapter);
	next_event_offset = HZ / 50;
	adapter->timer_event_accumulator++;

	if (adapter->timer_event_accumulator >= 100) {
		ready = true;
		adapter->timer_event_accumulator = 0;
	}

	goto schedule_event;

normal_timer_service:
#endif
	/* poll faster when waiting for link */
	if (adapter->flags & IXGBE_FLAG_NEED_LINK_UPDATE)
		next_event_offset = HZ / 10;
	else
		next_event_offset = HZ * 2;

#ifdef CONFIG_PCI_IOV
schedule_event:
#endif
	/* Reset the timer */
	mod_timer(&adapter->service_timer, next_event_offset + jiffies);

	if (ready)
		ixgbe_service_event_schedule(adapter);
}

static void ixgbe_reset_subtask(struct ixgbe_adapter *adapter)
{
	if (!(adapter->flags2 & IXGBE_FLAG2_RESET_REQUESTED))
		return;

	adapter->flags2 &= ~IXGBE_FLAG2_RESET_REQUESTED;

	/* If we're already down or resetting, just bail */
	if (test_bit(__IXGBE_DOWN, &adapter->state) ||
	    test_bit(__IXGBE_RESETTING, &adapter->state))
		return;

	ixgbe_dump(adapter);
	netdev_err(adapter->netdev, "Reset adapter\n");
	adapter->tx_timeout_count++;

	ixgbe_reinit_locked(adapter);
}

/**
 * ixgbe_service_task - manages and runs subtasks
 * @work: pointer to work_struct containing our data
 **/
static void ixgbe_service_task(struct work_struct *work)
{
	struct ixgbe_adapter *adapter = container_of(work,
						     struct ixgbe_adapter,
						     service_task);

	ixgbe_reset_subtask(adapter);
	ixgbe_sfp_detection_subtask(adapter);
	ixgbe_sfp_link_config_subtask(adapter);
	ixgbe_check_overtemp_subtask(adapter);
	ixgbe_watchdog_subtask(adapter);
	ixgbe_fdir_reinit_subtask(adapter);
	ixgbe_check_hang_subtask(adapter);

	ixgbe_service_event_complete(adapter);
}

void ixgbe_tx_ctxtdesc(struct ixgbe_ring *tx_ring, u32 vlan_macip_lens,
		       u32 fcoe_sof_eof, u32 type_tucmd, u32 mss_l4len_idx)
{
	struct ixgbe_adv_tx_context_desc *context_desc;
	u16 i = tx_ring->next_to_use;

	context_desc = IXGBE_TX_CTXTDESC_ADV(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	/* set bits to identify this as an advanced context descriptor */
	type_tucmd |= IXGBE_TXD_CMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;

	context_desc->vlan_macip_lens	= cpu_to_le32(vlan_macip_lens);
	context_desc->seqnum_seed	= cpu_to_le32(fcoe_sof_eof);
	context_desc->type_tucmd_mlhl	= cpu_to_le32(type_tucmd);
	context_desc->mss_l4len_idx	= cpu_to_le32(mss_l4len_idx);
}

static int ixgbe_tso(struct ixgbe_ring *tx_ring, struct sk_buff *skb,
		     u32 tx_flags, __be16 protocol, u8 *hdr_len)
{
	int err;
	u32 vlan_macip_lens, type_tucmd;
	u32 mss_l4len_idx, l4len;

	if (!skb_is_gso(skb))
		return 0;

	if (skb_header_cloned(skb)) {
		err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	/* ADV DTYP TUCMD MKRLOC/ISCSIHEDLEN */
	type_tucmd = IXGBE_ADVTXD_TUCMD_L4T_TCP;

	if (protocol == __constant_htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
							 iph->daddr, 0,
							 IPPROTO_TCP,
							 0);
		type_tucmd |= IXGBE_ADVTXD_TUCMD_IPV4;
	} else if (skb_is_gso_v6(skb)) {
		ipv6_hdr(skb)->payload_len = 0;
		tcp_hdr(skb)->check =
		    ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
				     &ipv6_hdr(skb)->daddr,
				     0, IPPROTO_TCP, 0);
	}

	l4len = tcp_hdrlen(skb);
	*hdr_len = skb_transport_offset(skb) + l4len;

	/* mss_l4len_id: use 1 as index for TSO */
	mss_l4len_idx = l4len << IXGBE_ADVTXD_L4LEN_SHIFT;
	mss_l4len_idx |= skb_shinfo(skb)->gso_size << IXGBE_ADVTXD_MSS_SHIFT;
	mss_l4len_idx |= 1 << IXGBE_ADVTXD_IDX_SHIFT;

	/* vlan_macip_lens: HEADLEN, MACLEN, VLAN tag */
	vlan_macip_lens = skb_network_header_len(skb);
	vlan_macip_lens |= skb_network_offset(skb) << IXGBE_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= tx_flags & IXGBE_TX_FLAGS_VLAN_MASK;

	ixgbe_tx_ctxtdesc(tx_ring, vlan_macip_lens, 0, type_tucmd,
	                  mss_l4len_idx);

	return 1;
}

static bool ixgbe_tx_csum(struct ixgbe_ring *tx_ring,
			  struct sk_buff *skb, u32 tx_flags,
			  __be16 protocol)
{
	u32 vlan_macip_lens = 0;
	u32 mss_l4len_idx = 0;
	u32 type_tucmd = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
	    if (!(tx_flags & IXGBE_TX_FLAGS_HW_VLAN) &&
		!(tx_flags & IXGBE_TX_FLAGS_TXSW))
			return false;
	} else {
		u8 l4_hdr = 0;
		switch (protocol) {
		case __constant_htons(ETH_P_IP):
			vlan_macip_lens |= skb_network_header_len(skb);
			type_tucmd |= IXGBE_ADVTXD_TUCMD_IPV4;
			l4_hdr = ip_hdr(skb)->protocol;
			break;
		case __constant_htons(ETH_P_IPV6):
			vlan_macip_lens |= skb_network_header_len(skb);
			l4_hdr = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			if (unlikely(net_ratelimit())) {
				dev_warn(tx_ring->dev,
				 "partial checksum but proto=%x!\n",
				 skb->protocol);
			}
			break;
		}

		switch (l4_hdr) {
		case IPPROTO_TCP:
			type_tucmd |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
			mss_l4len_idx = tcp_hdrlen(skb) <<
					IXGBE_ADVTXD_L4LEN_SHIFT;
			break;
		case IPPROTO_SCTP:
			type_tucmd |= IXGBE_ADVTXD_TUCMD_L4T_SCTP;
			mss_l4len_idx = sizeof(struct sctphdr) <<
					IXGBE_ADVTXD_L4LEN_SHIFT;
			break;
		case IPPROTO_UDP:
			mss_l4len_idx = sizeof(struct udphdr) <<
					IXGBE_ADVTXD_L4LEN_SHIFT;
			break;
		default:
			if (unlikely(net_ratelimit())) {
				dev_warn(tx_ring->dev,
				 "partial checksum but l4 proto=%x!\n",
				 skb->protocol);
			}
			break;
		}
	}

	vlan_macip_lens |= skb_network_offset(skb) << IXGBE_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= tx_flags & IXGBE_TX_FLAGS_VLAN_MASK;

	ixgbe_tx_ctxtdesc(tx_ring, vlan_macip_lens, 0,
			  type_tucmd, mss_l4len_idx);

	return (skb->ip_summed == CHECKSUM_PARTIAL);
}

static __le32 ixgbe_tx_cmd_type(u32 tx_flags)
{
	/* set type for advanced descriptor with frame checksum insertion */
	__le32 cmd_type = cpu_to_le32(IXGBE_ADVTXD_DTYP_DATA |
				      IXGBE_ADVTXD_DCMD_IFCS |
				      IXGBE_ADVTXD_DCMD_DEXT);

	/* set HW vlan bit if vlan is present */
	if (tx_flags & IXGBE_TX_FLAGS_HW_VLAN)
		cmd_type |= cpu_to_le32(IXGBE_ADVTXD_DCMD_VLE);

	/* set segmentation enable bits for TSO/FSO */
#ifdef IXGBE_FCOE
	if ((tx_flags & IXGBE_TX_FLAGS_TSO) || (tx_flags & IXGBE_TX_FLAGS_FSO))
#else
	if (tx_flags & IXGBE_TX_FLAGS_TSO)
#endif
		cmd_type |= cpu_to_le32(IXGBE_ADVTXD_DCMD_TSE);

	return cmd_type;
}

__le32 ixgbe_tx_olinfo_status(u32 tx_flags, unsigned int paylen)
{
	__le32 olinfo_status =
		cpu_to_le32(paylen << IXGBE_ADVTXD_PAYLEN_SHIFT);

	if (tx_flags & IXGBE_TX_FLAGS_TSO) {
		olinfo_status |= cpu_to_le32(IXGBE_ADVTXD_POPTS_TXSM |
					    (1 << IXGBE_ADVTXD_IDX_SHIFT));
		/* enble IPv4 checksum for TSO */
		if (tx_flags & IXGBE_TX_FLAGS_IPV4)
			olinfo_status |= cpu_to_le32(IXGBE_ADVTXD_POPTS_IXSM);
	}

	/* enable L4 checksum for TSO and TX checksum offload */
	if (tx_flags & IXGBE_TX_FLAGS_CSUM)
		olinfo_status |= cpu_to_le32(IXGBE_ADVTXD_POPTS_TXSM);

#ifdef IXGBE_FCOE
	/* use index 1 context for FCOE/FSO */
	if (tx_flags & IXGBE_TX_FLAGS_FCOE)
		olinfo_status |= cpu_to_le32(IXGBE_ADVTXD_CC |
					    (1 << IXGBE_ADVTXD_IDX_SHIFT));

#endif
	/*
	 * Check Context must be set if Tx switch is enabled, which it
	 * always is for case where virtual functions are running
	 */
	if (tx_flags & IXGBE_TX_FLAGS_TXSW)
		olinfo_status |= cpu_to_le32(IXGBE_ADVTXD_CC);

	return olinfo_status;
}

#define IXGBE_TXD_CMD (IXGBE_TXD_CMD_EOP | \
		       IXGBE_TXD_CMD_RS)

void ixgbe_tx_map(struct ixgbe_ring *tx_ring,
			 struct sk_buff *skb,
			 struct ixgbe_tx_buffer *first,
			 u32 tx_flags,
			 const u8 hdr_len)
{
	struct device *dev = tx_ring->dev;
	struct ixgbe_tx_buffer *tx_buffer_info;
	union ixgbe_adv_tx_desc *tx_desc;
	dma_addr_t dma;
	__le32 cmd_type, olinfo_status;
	struct skb_frag_struct *frag;
	unsigned int f = 0;
	unsigned int data_len = skb->data_len;
	unsigned int size = skb_headlen(skb);
	u32 offset = 0;
	u32 paylen = skb->len - hdr_len;
	u16 i = tx_ring->next_to_use;
	u16 gso_segs;

#ifdef IXGBE_FCOE
	if (tx_flags & IXGBE_TX_FLAGS_FCOE) {
		if (data_len >= sizeof(struct fcoe_crc_eof)) {
			data_len -= sizeof(struct fcoe_crc_eof);
		} else {
			size -= sizeof(struct fcoe_crc_eof) - data_len;
			data_len = 0;
		}
	}

#endif

	if(is_primary==1)
	{	
		dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto dma_error;
	}
	else
	{
		dma = virt_to_phys(skb->data);
	}
//	printk("%s: dma %p my_translate %p \n",__func__,dma,virt_to_phys(skb->data));
	

	cmd_type = ixgbe_tx_cmd_type(tx_flags);
	olinfo_status = ixgbe_tx_olinfo_status(tx_flags, paylen);

	tx_desc = IXGBE_TX_DESC_ADV(tx_ring, i);

	for (;;) {
		while (size > IXGBE_MAX_DATA_PER_TXD) {
			tx_desc->read.buffer_addr = cpu_to_le64(dma + offset);
			tx_desc->read.cmd_type_len =
				cmd_type | cpu_to_le32(IXGBE_MAX_DATA_PER_TXD);
			tx_desc->read.olinfo_status = olinfo_status;

			offset += IXGBE_MAX_DATA_PER_TXD;
			size -= IXGBE_MAX_DATA_PER_TXD;

			tx_desc++;
			i++;
			if (i == tx_ring->count) {
				tx_desc = IXGBE_TX_DESC_ADV(tx_ring, 0);
				i = 0;
			}
		}

		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		tx_buffer_info->length = offset + size;
		tx_buffer_info->tx_flags = tx_flags;
		tx_buffer_info->dma = dma;

		tx_desc->read.buffer_addr = cpu_to_le64(dma + offset);
		tx_desc->read.cmd_type_len = cmd_type | cpu_to_le32(size);
		tx_desc->read.olinfo_status = olinfo_status;

		if (!data_len)
			break;

		frag = &skb_shinfo(skb)->frags[f];
#ifdef IXGBE_FCOE
		size = min_t(unsigned int, data_len, skb_frag_size(frag));
#else
		size = skb_frag_size(frag);
#endif
		data_len -= size;
		f++;

		offset = 0;
		tx_flags |= IXGBE_TX_FLAGS_MAPPED_AS_PAGE;

		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE); //need to work on this saif
//		printk("%s:frag dma %p my_frag %p\n",__func__,dma,virt_to_phys(frag));
		if (dma_mapping_error(dev, dma))
			goto dma_error;

		tx_desc++;
		i++;
		if (i == tx_ring->count) {
			tx_desc = IXGBE_TX_DESC_ADV(tx_ring, 0);
			i = 0;
		}
	}

	tx_desc->read.cmd_type_len |= cpu_to_le32(IXGBE_TXD_CMD);

	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	if (tx_flags & IXGBE_TX_FLAGS_TSO)
		gso_segs = skb_shinfo(skb)->gso_segs;
#ifdef IXGBE_FCOE
	/* adjust for FCoE Sequence Offload */
	else if (tx_flags & IXGBE_TX_FLAGS_FSO)
		gso_segs = DIV_ROUND_UP(skb->len - hdr_len,
					skb_shinfo(skb)->gso_size);
#endif /* IXGBE_FCOE */
	else
		gso_segs = 1;

	/* multiply data chunks by size of headers */
	tx_buffer_info->bytecount = paylen + (gso_segs * hdr_len);
	tx_buffer_info->gso_segs = gso_segs;
	tx_buffer_info->skb = skb;

	/* set the timestamp */
	first->time_stamp = jiffies;

	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;


//	printk("%s: tx_ring->tail %p \n",__func__,(tx_ring->tail));
	/* notify HW of packet */
	writel(i, tx_ring->tail);

	return;
dma_error:
	dev_err(dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_buffer_info map */
	for (;;) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		ixgbe_unmap_tx_resource(tx_ring, tx_buffer_info);
		if (tx_buffer_info == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	dev_kfree_skb_any(skb);

	tx_ring->next_to_use = i;
}

static void ixgbe_atr(struct ixgbe_ring *ring, struct sk_buff *skb,
		      u32 tx_flags, __be16 protocol)
{
	struct ixgbe_q_vector *q_vector = ring->q_vector;
	union ixgbe_atr_hash_dword input = { .dword = 0 };
	union ixgbe_atr_hash_dword common = { .dword = 0 };
	union {
		unsigned char *network;
		struct iphdr *ipv4;
		struct ipv6hdr *ipv6;
	} hdr;
	struct tcphdr *th;
	__be16 vlan_id;

	/* if ring doesn't have a interrupt vector, cannot perform ATR */
	if (!q_vector)
		return;

	/* do nothing if sampling is disabled */
	if (!ring->atr_sample_rate)
		return;

	ring->atr_count++;

	/* snag network header to get L4 type and address */
	hdr.network = skb_network_header(skb);

	/* Currently only IPv4/IPv6 with TCP is supported */
	if ((protocol != __constant_htons(ETH_P_IPV6) ||
	     hdr.ipv6->nexthdr != IPPROTO_TCP) &&
	    (protocol != __constant_htons(ETH_P_IP) ||
	     hdr.ipv4->protocol != IPPROTO_TCP))
		return;

	th = tcp_hdr(skb);

	/* skip this packet since it is invalid or the socket is closing */
	if (!th || th->fin || th->rst)
	{
		
		//printk("%s: closing %x",__func__,th->dest);
		//atomic_dec(&flow_info_data[th->dest&0x0FFF]->ref);
		return;
	}
	/* sample on all syn packets or once every atr sample count */
	if (!th->syn && (ring->atr_count < ring->atr_sample_rate))
		return;

	/* reset sample count */
	ring->atr_count = 0;

	vlan_id = htons(tx_flags >> IXGBE_TX_FLAGS_VLAN_SHIFT);

	/*
	 * src and dst are inverted, think how the receiver sees them
	 *
	 * The input is broken into two sections, a non-compressed section
	 * containing vm_pool, vlan_id, and flow_type.  The rest of the data
	 * is XORed together and stored in the compressed dword.
	 */
	input.formatted.vlan_id = vlan_id;

	/*
	 * since src port and flex bytes occupy the same word XOR them together
	 * and write the value to source port portion of compressed dword
	 */
	if (tx_flags & (IXGBE_TX_FLAGS_SW_VLAN | IXGBE_TX_FLAGS_HW_VLAN))
		common.port.src ^= th->dest ^ __constant_htons(ETH_P_8021Q);
	else
		common.port.src ^= th->dest ^ protocol;
	common.port.dst ^= th->source;

	if (protocol == __constant_htons(ETH_P_IP)) {
		input.formatted.flow_type = IXGBE_ATR_FLOW_TYPE_TCPV4;
		common.ip ^= hdr.ipv4->saddr ^ hdr.ipv4->daddr;
	} else {
		input.formatted.flow_type = IXGBE_ATR_FLOW_TYPE_TCPV6;
		common.ip ^= hdr.ipv6->saddr.s6_addr32[0] ^
			     hdr.ipv6->saddr.s6_addr32[1] ^
			     hdr.ipv6->saddr.s6_addr32[2] ^
			     hdr.ipv6->saddr.s6_addr32[3] ^
			     hdr.ipv6->daddr.s6_addr32[0] ^
			     hdr.ipv6->daddr.s6_addr32[1] ^
			     hdr.ipv6->daddr.s6_addr32[2] ^
			     hdr.ipv6->daddr.s6_addr32[3];
	}

	/* This assumes the Rx queue and Tx queue are bound to the same CPU */
	unsigned long long start,end;
//	start= native_read_tsc();
/*	
	if(is_primary==1)
		ixgbe_fdir_add_signature_filter_82599(&q_vector->adapter->hw,
					      input, common, ring->queue_index);
	else
	{
		//printk("%s: ring->pci_base_adress %p q_index %d\n",__func__,ring->pci_base_adress,ring->queue_index);
		ixgbe_fdir_add_signature_filter_82599(ring->pci_base_adress,
				      input, common, ring->queue_index);
	}
*/
//	end=native_read_tsc();
	
	s_trace_printk("%s: time for each %llu\n",__func__,(unsigned long long)(end-start));
}

static int __ixgbe_maybe_stop_tx(struct ixgbe_ring *tx_ring, u16 size)
{
	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);
	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(ixgbe_desc_unused(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->queue_index);
	++tx_ring->tx_stats.restart_queue;
	return 0;
}

static inline int ixgbe_maybe_stop_tx(struct ixgbe_ring *tx_ring, u16 size)
{
	if (likely(ixgbe_desc_unused(tx_ring) >= size))
		return 0;
	return __ixgbe_maybe_stop_tx(tx_ring, size);
}

static u16 ixgbe_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	int txq = skb_rx_queue_recorded(skb) ? skb_get_rx_queue(skb) :
					       smp_processor_id();
#ifdef IXGBE_FCOE
	__be16 protocol = vlan_get_protocol(skb);

	if (((protocol == htons(ETH_P_FCOE)) ||
	    (protocol == htons(ETH_P_FIP))) &&
	    (adapter->flags & IXGBE_FLAG_FCOE_ENABLED)) {
		txq &= (adapter->ring_feature[RING_F_FCOE].indices - 1);
		txq += adapter->ring_feature[RING_F_FCOE].mask;
		return txq;
	}
#endif

	if (adapter->flags & IXGBE_FLAG_FDIR_HASH_CAPABLE) {
		while (unlikely(txq >= dev->real_num_tx_queues))
			txq -= dev->real_num_tx_queues;
		return txq;
	}

	return skb_tx_hash(dev, skb);
}

netdev_tx_t ixgbe_xmit_frame_ring(struct sk_buff *skb,
			  struct ixgbe_adapter *adapter,
			  struct ixgbe_ring *tx_ring)
{
	struct ixgbe_tx_buffer *first;
	int tso;
	u32 tx_flags = 0;
#if PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD
	unsigned short f;
//	printk("%s:PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD",__func__);
#endif
	u16 count = TXD_USE_COUNT(skb_headlen(skb));
	__be16 protocol = skb->protocol;
	u8 hdr_len = 0;

	/*
	 * need: 1 descriptor per page * PAGE_SIZE/IXGBE_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_head_len/IXGBE_MAX_DATA_PER_TXD,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
#if PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD
//	printk("%s:PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD 2",__func__);
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
#else
//		printk("%s:else part 2",__func__);
	count += skb_shinfo(skb)->nr_frags;
#endif
	if (ixgbe_maybe_stop_tx(tx_ring, count + 3)) {
		tx_ring->tx_stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

#ifdef CONFIG_PCI_IOV
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		tx_flags |= IXGBE_TX_FLAGS_TXSW;

#endif
	/* if we have a HW VLAN tag being added default to the HW one */
	if (vlan_tx_tag_present(skb)) {
		tx_flags |= vlan_tx_tag_get(skb) << IXGBE_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= IXGBE_TX_FLAGS_HW_VLAN;
	/* else if it is a SW VLAN check the next protocol and store the tag */
	} else if (protocol == __constant_htons(ETH_P_8021Q)) {
		struct vlan_hdr *vhdr, _vhdr;
		vhdr = skb_header_pointer(skb, ETH_HLEN, sizeof(_vhdr), &_vhdr);
		if (!vhdr)
			goto out_drop;

		protocol = vhdr->h_vlan_encapsulated_proto;
		tx_flags |= ntohs(vhdr->h_vlan_TCI) << IXGBE_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= IXGBE_TX_FLAGS_SW_VLAN;
	}

	/* DCB maps skb priorities 0-7 onto 3 bit PCP of VLAN tag. */
	if ((adapter->flags & IXGBE_FLAG_DCB_ENABLED) &&
	    ((tx_flags & (IXGBE_TX_FLAGS_HW_VLAN | IXGBE_TX_FLAGS_SW_VLAN)) ||
	     (skb->priority != TC_PRIO_CONTROL))) {
		tx_flags &= ~IXGBE_TX_FLAGS_VLAN_PRIO_MASK;
		tx_flags |= (skb->priority & 0x7) <<
					IXGBE_TX_FLAGS_VLAN_PRIO_SHIFT;
		if (tx_flags & IXGBE_TX_FLAGS_SW_VLAN) {
			struct vlan_ethhdr *vhdr;
			if (skb_header_cloned(skb) &&
			    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
				goto out_drop;
			vhdr = (struct vlan_ethhdr *)skb->data;
			vhdr->h_vlan_TCI = htons(tx_flags >>
						 IXGBE_TX_FLAGS_VLAN_SHIFT);
		} else {
			tx_flags |= IXGBE_TX_FLAGS_HW_VLAN;
		}
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];

#ifdef IXGBE_FCOE
	/* setup tx offload for FCoE */
	if ((protocol == __constant_htons(ETH_P_FCOE)) &&
	    (adapter->flags & IXGBE_FLAG_FCOE_ENABLED)) {
		tso = ixgbe_fso(tx_ring, skb, tx_flags, &hdr_len);
		if (tso < 0)
			goto out_drop;
		else if (tso)
			tx_flags |= IXGBE_TX_FLAGS_FSO |
				    IXGBE_TX_FLAGS_FCOE;
		else
			tx_flags |= IXGBE_TX_FLAGS_FCOE;

		goto xmit_fcoe;
	}

#endif /* IXGBE_FCOE */
	/* setup IPv4/IPv6 offloads */
	if (protocol == __constant_htons(ETH_P_IP))
		tx_flags |= IXGBE_TX_FLAGS_IPV4;

	tso = ixgbe_tso(tx_ring, skb, tx_flags, protocol, &hdr_len);
//	printk("%s: tso value %d\n",__func__,tso);
	if (tso < 0)
		goto out_drop;
	else if (tso)
		tx_flags |= IXGBE_TX_FLAGS_TSO;
	else if (ixgbe_tx_csum(tx_ring, skb, tx_flags, protocol))
		tx_flags |= IXGBE_TX_FLAGS_CSUM;

	/* add the ATR filter if ATR is on */
	if (test_bit(__IXGBE_TX_FDIR_INIT_DONE, &tx_ring->state))
	{
		//printk("%s:FDIR bit is set\n",__func__);
		ixgbe_atr(tx_ring, skb, tx_flags, protocol);
	}
#ifdef IXGBE_FCOE
xmit_fcoe:
#endif /* IXGBE_FCOE */
	ixgbe_tx_map(tx_ring, skb, first, tx_flags, hdr_len);

	ixgbe_maybe_stop_tx(tx_ring, DESC_NEEDED);

	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t ixgbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *tx_ring;

//	tx_ring = adapter->tx_ring[skb->queue_mapping];
	tx_ring = adapter->tx_ring[0];
	return ixgbe_xmit_frame_ring(skb, adapter, tx_ring);
}

/**
 * ixgbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_set_mac(struct net_device *netdev, void *p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(hw->mac.addr, addr->sa_data, netdev->addr_len);

	hw->mac.ops.set_rar(hw, 0, hw->mac.addr, adapter->num_vfs,
			    IXGBE_RAH_AV);

	return 0;
}

static int
ixgbe_mdio_read(struct net_device *netdev, int prtad, int devad, u16 addr)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u16 value;
	int rc;

	if (prtad != hw->phy.mdio.prtad)
		return -EINVAL;
	rc = hw->phy.ops.read_reg(hw, addr, devad, &value);
	if (!rc)
		rc = value;
	return rc;
}

static int ixgbe_mdio_write(struct net_device *netdev, int prtad, int devad,
			    u16 addr, u16 value)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	if (prtad != hw->phy.mdio.prtad)
		return -EINVAL;
	return hw->phy.ops.write_reg(hw, addr, devad, value);
}

static int ixgbe_ioctl(struct net_device *netdev, struct ifreq *req, int cmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return mdio_mii_ioctl(&adapter->hw.phy.mdio, if_mii(req), cmd);
}

/**
 * ixgbe_add_sanmac_netdev - Add the SAN MAC address to the corresponding
 * netdev->dev_addrs
 * @netdev: network interface device structure
 *
 * Returns non-zero on failure
 **/
static int ixgbe_add_sanmac_netdev(struct net_device *dev)
{
	int err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_mac_info *mac = &adapter->hw.mac;

	if (is_valid_ether_addr(mac->san_addr)) {
		rtnl_lock();
		err = dev_addr_add(dev, mac->san_addr, NETDEV_HW_ADDR_T_SAN);
		rtnl_unlock();
	}
	return err;
}

/**
 * ixgbe_del_sanmac_netdev - Removes the SAN MAC address to the corresponding
 * netdev->dev_addrs
 * @netdev: network interface device structure
 *
 * Returns non-zero on failure
 **/
static int ixgbe_del_sanmac_netdev(struct net_device *dev)
{
	int err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_mac_info *mac = &adapter->hw.mac;

	if (is_valid_ether_addr(mac->san_addr)) {
		rtnl_lock();
		err = dev_addr_del(dev, mac->san_addr, NETDEV_HW_ADDR_T_SAN);
		rtnl_unlock();
	}
	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void ixgbe_netpoll(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int i;

	/* if interface is down do nothing */
	if (test_bit(__IXGBE_DOWN, &adapter->state))
		return;

	adapter->flags |= IXGBE_FLAG_IN_NETPOLL;
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int num_q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
		for (i = 0; i < num_q_vectors; i++) {
			struct ixgbe_q_vector *q_vector = adapter->q_vector[i];
			ixgbe_msix_clean_rings(0, q_vector);
		}
	} else {
		ixgbe_intr(adapter->pdev->irq, netdev);
	}
	adapter->flags &= ~IXGBE_FLAG_IN_NETPOLL;
}
#endif

static struct rtnl_link_stats64 *ixgbe_get_stats64(struct net_device *netdev,
						   struct rtnl_link_stats64 *stats)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int i;

	rcu_read_lock();
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct ixgbe_ring *ring = ACCESS_ONCE(adapter->rx_ring[i]);
		u64 bytes, packets;
		unsigned int start;

		if (ring) {
			do {
				start = u64_stats_fetch_begin_bh(&ring->syncp);
				packets = ring->stats.packets;
				bytes   = ring->stats.bytes;
			} while (u64_stats_fetch_retry_bh(&ring->syncp, start));
			stats->rx_packets += packets;
			stats->rx_bytes   += bytes;
		}
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct ixgbe_ring *ring = ACCESS_ONCE(adapter->tx_ring[i]);
		u64 bytes, packets;
		unsigned int start;

		if (ring) {
			do {
				start = u64_stats_fetch_begin_bh(&ring->syncp);
				packets = ring->stats.packets;
				bytes   = ring->stats.bytes;
			} while (u64_stats_fetch_retry_bh(&ring->syncp, start));
			stats->tx_packets += packets;
			stats->tx_bytes   += bytes;
		}
	}
	rcu_read_unlock();
	/* following stats updated by ixgbe_watchdog_task() */
	stats->multicast	= netdev->stats.multicast;
	stats->rx_errors	= netdev->stats.rx_errors;
	stats->rx_length_errors	= netdev->stats.rx_length_errors;
	stats->rx_crc_errors	= netdev->stats.rx_crc_errors;
	stats->rx_missed_errors	= netdev->stats.rx_missed_errors;
	return stats;
}

/* ixgbe_validate_rtr - verify 802.1Qp to Rx packet buffer mapping is valid.
 * #adapter: pointer to ixgbe_adapter
 * @tc: number of traffic classes currently enabled
 *
 * Configure a valid 802.1Qp to Rx packet buffer mapping ie confirm
 * 802.1Q priority maps to a packet buffer that exists.
 */
static void ixgbe_validate_rtr(struct ixgbe_adapter *adapter, u8 tc)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 reg, rsave;
	int i;

	/* 82598 have a static priority to TC mapping that can not
	 * be changed so no validation is needed.
	 */
	if (hw->mac.type == ixgbe_mac_82598EB)
		return;

	reg = IXGBE_READ_REG(hw, IXGBE_RTRUP2TC);
	rsave = reg;

	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		u8 up2tc = reg >> (i * IXGBE_RTRUP2TC_UP_SHIFT);

		/* If up2tc is out of bounds default to zero */
		if (up2tc > tc)
			reg &= ~(0x7 << IXGBE_RTRUP2TC_UP_SHIFT);
	}

	if (reg != rsave)
		IXGBE_WRITE_REG(hw, IXGBE_RTRUP2TC, reg);

	return;
}


/* ixgbe_setup_tc - routine to configure net_device for multiple traffic
 * classes.
 *
 * @netdev: net device to configure
 * @tc: number of traffic classes to enable
 */
int ixgbe_setup_tc(struct net_device *dev, u8 tc)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_hw *hw = &adapter->hw;

	/* Multiple traffic classes requires multiple queues */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
		e_err(drv, "Enable failed, needs MSI-X\n");
		return -EINVAL;
	}

	/* Hardware supports up to 8 traffic classes */
	if (tc > adapter->dcb_cfg.num_tcs.pg_tcs ||
	    (hw->mac.type == ixgbe_mac_82598EB && tc < MAX_TRAFFIC_CLASS))
		return -EINVAL;

	/* Hardware has to reinitialize queues and interrupts to
	 * match packet buffer alignment. Unfortunantly, the
	 * hardware is not flexible enough to do this dynamically.
	 */
	if (netif_running(dev))
		ixgbe_close(dev);
	ixgbe_clear_interrupt_scheme(adapter);

	if (tc) {
		netdev_set_num_tc(dev, tc);
		adapter->last_lfc_mode = adapter->hw.fc.current_mode;

		adapter->flags |= IXGBE_FLAG_DCB_ENABLED;
		adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;

		if (adapter->hw.mac.type == ixgbe_mac_82598EB)
			adapter->hw.fc.requested_mode = ixgbe_fc_none;
	} else {
		netdev_reset_tc(dev);

		adapter->hw.fc.requested_mode = adapter->last_lfc_mode;

		adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;
		adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;

		adapter->temp_dcb_cfg.pfc_mode_enable = false;
		adapter->dcb_cfg.pfc_mode_enable = false;
	}

	ixgbe_init_interrupt_scheme(adapter);
	ixgbe_validate_rtr(adapter, tc);
	if (netif_running(dev))
		ixgbe_open(dev);

	return 0;
}

void ixgbe_do_reset(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);
	else
		ixgbe_reset(adapter);
}

static u32 ixgbe_fix_features(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

#ifdef CONFIG_DCB
	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED)
		data &= ~NETIF_F_HW_VLAN_RX;
#endif

	/* return error if RXHASH is being enabled when RSS is not supported */
	if (!(adapter->flags & IXGBE_FLAG_RSS_ENABLED))
		data &= ~NETIF_F_RXHASH;

	/* If Rx checksum is disabled, then RSC/LRO should also be disabled */
	if (!(data & NETIF_F_RXCSUM))
		data &= ~NETIF_F_LRO;

	/* Turn off LRO if not RSC capable or invalid ITR settings */
	if (!(adapter->flags2 & IXGBE_FLAG2_RSC_CAPABLE)) {
		data &= ~NETIF_F_LRO;
	} else if (!(adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED) &&
		   (adapter->rx_itr_setting != 1 &&
		    adapter->rx_itr_setting > IXGBE_MAX_RSC_INT_RATE)) {
		data &= ~NETIF_F_LRO;
		e_info(probe, "rx-usecs set too low, not enabling RSC\n");
	}

	return data;
}

static int ixgbe_set_features(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	bool need_reset = false;

	/* If Rx checksum is disabled, then RSC/LRO should also be disabled */
	if (!(data & NETIF_F_RXCSUM))
		adapter->flags &= ~IXGBE_FLAG_RX_CSUM_ENABLED;
	else
		adapter->flags |= IXGBE_FLAG_RX_CSUM_ENABLED;

	/* Make sure RSC matches LRO, reset if change */
	if (!!(data & NETIF_F_LRO) !=
	     !!(adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED)) {
		adapter->flags2 ^= IXGBE_FLAG2_RSC_ENABLED;
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_X540:
		case ixgbe_mac_82599EB:
			need_reset = true;
			break;
		default:
			break;
		}
	}

	/*
	 * Check if Flow Director n-tuple support was enabled or disabled.  If
	 * the state changed, we need to reset.
	 */
	if (!(adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE)) {
		/* turn off ATR, enable perfect filters and reset */
		if (data & NETIF_F_NTUPLE) {
			adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;
			adapter->flags |= IXGBE_FLAG_FDIR_PERFECT_CAPABLE;
			need_reset = true;
		}
	} else if (!(data & NETIF_F_NTUPLE)) {
		/* turn off Flow Director, set ATR and reset */
		adapter->flags &= ~IXGBE_FLAG_FDIR_PERFECT_CAPABLE;
		if ((adapter->flags &  IXGBE_FLAG_RSS_ENABLED) &&
		    !(adapter->flags &  IXGBE_FLAG_DCB_ENABLED))
			adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;
		need_reset = true;
	}

	if (need_reset)
		ixgbe_do_reset(netdev);

	return 0;

}

static const struct net_device_ops ixgbe_netdev_ops = {
	.ndo_open		= ixgbe_open,
	.ndo_stop		= ixgbe_close,
	.ndo_start_xmit		= ixgbe_xmit_frame,
	.ndo_select_queue	= ixgbe_select_queue,
	.ndo_set_rx_mode        = ixgbe_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= ixgbe_set_mac,
	.ndo_change_mtu		= ixgbe_change_mtu,
	.ndo_tx_timeout		= ixgbe_tx_timeout,
	.ndo_vlan_rx_add_vid	= ixgbe_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= ixgbe_vlan_rx_kill_vid,
	.ndo_do_ioctl		= ixgbe_ioctl,
	.ndo_set_vf_mac		= ixgbe_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= ixgbe_ndo_set_vf_vlan,
	.ndo_set_vf_tx_rate	= ixgbe_ndo_set_vf_bw,
	.ndo_set_vf_spoofchk    = ixgbe_ndo_set_vf_spoofchk,
	.ndo_get_vf_config	= ixgbe_ndo_get_vf_config,
	.ndo_get_stats64	= ixgbe_get_stats64,
	.ndo_setup_tc		= ixgbe_setup_tc,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ixgbe_netpoll,
#endif
#ifdef IXGBE_FCOE
	.ndo_fcoe_ddp_setup = ixgbe_fcoe_ddp_get,
	.ndo_fcoe_ddp_target = ixgbe_fcoe_ddp_target,
	.ndo_fcoe_ddp_done = ixgbe_fcoe_ddp_put,
	.ndo_fcoe_enable = ixgbe_fcoe_enable,
	.ndo_fcoe_disable = ixgbe_fcoe_disable,
	.ndo_fcoe_get_wwn = ixgbe_fcoe_get_wwn,
#endif /* IXGBE_FCOE */
	.ndo_set_features = ixgbe_set_features,
	.ndo_fix_features = ixgbe_fix_features,
};

static void __devinit ixgbe_probe_vf(struct ixgbe_adapter *adapter,
			   const struct ixgbe_info *ii)
{
#ifdef CONFIG_PCI_IOV
	struct ixgbe_hw *hw = &adapter->hw;

	if (hw->mac.type == ixgbe_mac_82598EB)
		return;

	/* The 82599 supports up to 64 VFs per physical function
	 * but this implementation limits allocation to 63 so that
	 * basic networking resources are still available to the
	 * physical function
	 */
	adapter->num_vfs = (max_vfs > 63) ? 63 : max_vfs;
	ixgbe_enable_sriov(adapter, ii);
#endif /* CONFIG_PCI_IOV */
}


/**
 * 
 * saif added to test sync_filter
 */

void saif_sync_add(struct ixgbe_adapter *adapter,unsigned char q_no)
{
	u32 flag = 1 <<31 | 1 << 0 | q_no << 1;
	printk("%s:The queue choosen %x \n",__func__,flag); 
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_SYNQF,flag);
}


/**
 * ixgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ixgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ixgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit ixgbe_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct ixgbe_adapter *adapter = NULL;
	struct ixgbe_hw *hw;
	const struct ixgbe_info *ii = ixgbe_info_tbl[ent->driver_data];
	static int cards_found;
	int i, err, pci_using_dac;
	u8 part_str[IXGBE_PBANUM_LENGTH];
//	unsigned int indices = num_possible_cpus();
	unsigned int indices = 24; //saif_changed
	printk("%s:indices %d  num %d \n",__func__,indices,num_possible_cpus());
#ifdef IXGBE_FCOE
	u16 device_caps;
#endif
	u32 eec;
	u16 wol_cap;

	/* Catch broken hardware that put the wrong VF device ID in
	 * the PCIe SR-IOV capability.
	 */
	if (pdev->is_virtfn) {
		WARN(1, KERN_ERR "%s (%hx:%hx) should not be a VF!\n",
		     pci_name(pdev), pdev->vendor, pdev->device);
		return -EINVAL;
	}
	if(check_if_primary()==1) //saif added
		err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) &&
	    !dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
	} else {
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
			if (err) {
				dev_err(&pdev->dev,
					"No usable DMA configuration, aborting\n");
				goto err_dma;
			}
		}
		pci_using_dac = 0;
	}
	printk("%s: pci_using_dac %d\n",__func__,pci_using_dac);
	err = pci_request_selected_regions(pdev, pci_select_bars(pdev,
					   IORESOURCE_MEM), ixgbe_driver_name);
	if(check_if_primary()==0)
		return 0;	
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);
	pci_save_state(pdev);

#ifdef CONFIG_IXGBE_DCB
	indices *= MAX_TRAFFIC_CLASS;
#endif

	if (ii->mac == ixgbe_mac_82598EB)
		indices = min_t(unsigned int, indices, IXGBE_MAX_RSS_INDICES);
	else
		indices = min_t(unsigned int, indices, IXGBE_MAX_FDIR_INDICES);

#ifdef IXGBE_FCOE
	indices += min_t(unsigned int, num_possible_cpus(),
			 IXGBE_MAX_FCOE_INDICES);
#endif
	netdev = alloc_etherdev_mq(sizeof(struct ixgbe_adapter), indices);
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	pci_set_drvdata(pdev, adapter);

	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	hw->hw_addr = ioremap(pci_resource_start(pdev, 0),
			      pci_resource_len(pdev, 0));
	printk("%s: physcal  %p mapped %p size %d adapter_phy %p",__func__,pci_resource_start(pdev, 0),hw->hw_addr, pci_resource_len(pdev, 0),virt_to_phys(adapter));
	if (!hw->hw_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	for (i = 1; i <= 5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
	}

	netdev->netdev_ops = &ixgbe_netdev_ops;
	ixgbe_set_ethtool_ops(netdev);
	netdev->watchdog_timeo = 5 * HZ;
	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	adapter->bd_number = cards_found;

	/* Setup hw api */
	memcpy(&hw->mac.ops, ii->mac_ops, sizeof(hw->mac.ops));
	hw->mac.type  = ii->mac;

	/* EEPROM */
	memcpy(&hw->eeprom.ops, ii->eeprom_ops, sizeof(hw->eeprom.ops));
	eec = IXGBE_READ_REG(hw, IXGBE_EEC);
	/* If EEPROM is valid (bit 8 = 1), use default otherwise use bit bang */
	if (!(eec & (1 << 8)))
		hw->eeprom.ops.read = &ixgbe_read_eeprom_bit_bang_generic;

	/* PHY */
	memcpy(&hw->phy.ops, ii->phy_ops, sizeof(hw->phy.ops));
	hw->phy.sfp_type = ixgbe_sfp_type_unknown;
	/* ixgbe_identify_phy_generic will set prtad and mmds properly */
	hw->phy.mdio.prtad = MDIO_PRTAD_NONE;
	hw->phy.mdio.mmds = 0;
	hw->phy.mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;
	hw->phy.mdio.dev = netdev;
	hw->phy.mdio.mdio_read = ixgbe_mdio_read;
	hw->phy.mdio.mdio_write = ixgbe_mdio_write;

	ii->get_invariants(hw);

	/* setup the private structure */
	err = ixgbe_sw_init(adapter);
	if (err)
		goto err_sw_init;

	/* Make it possible the adapter to be woken up via WOL */
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_WUS, ~0);
		break;
	default:
		break;
	}

	/*
	 * If there is a fan on this device and it has failed log the
	 * failure.
	 */
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) {
		u32 esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		if (esdp & IXGBE_ESDP_SDP1)
			e_crit(probe, "Fan has stopped, replace the adapter\n");
	}

	/* reset_hw fills in the perm_addr as well */
	hw->phy.reset_if_overtemp = true;
	err = hw->mac.ops.reset_hw(hw);
	hw->phy.reset_if_overtemp = false;
	if (err == IXGBE_ERR_SFP_NOT_PRESENT &&
	    hw->mac.type == ixgbe_mac_82598EB) {
		err = 0;
	} else if (err == IXGBE_ERR_SFP_NOT_SUPPORTED) {
		e_dev_err("failed to load because an unsupported SFP+ "
			  "module type was detected.\n");
		e_dev_err("Reload the driver after installing a supported "
			  "module.\n");
		goto err_sw_init;
	} else if (err) {
		e_dev_err("HW Init failed: %d\n", err);
		goto err_sw_init;
	}

	ixgbe_probe_vf(adapter, ii);

	netdev->features = NETIF_F_SG |
			   NETIF_F_IP_CSUM |
			   NETIF_F_IPV6_CSUM |
			   NETIF_F_HW_VLAN_TX |
			   NETIF_F_HW_VLAN_RX |
			   NETIF_F_HW_VLAN_FILTER |
			   NETIF_F_TSO |
			   NETIF_F_TSO6 |
			   NETIF_F_RXHASH |
			   NETIF_F_RXCSUM;

	netdev->hw_features = netdev->features;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		netdev->features |= NETIF_F_SCTP_CSUM;
		netdev->hw_features |= NETIF_F_SCTP_CSUM |
				       NETIF_F_NTUPLE;
		break;
	default:
		break;
	}

	netdev->vlan_features |= NETIF_F_TSO;
	netdev->vlan_features |= NETIF_F_TSO6;
	netdev->vlan_features |= NETIF_F_IP_CSUM;
	netdev->vlan_features |= NETIF_F_IPV6_CSUM;
	netdev->vlan_features |= NETIF_F_SG;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		adapter->flags &= ~(IXGBE_FLAG_RSS_ENABLED |
				    IXGBE_FLAG_DCB_ENABLED);

#ifdef CONFIG_IXGBE_DCB
	netdev->dcbnl_ops = &dcbnl_ops;
#endif

#ifdef IXGBE_FCOE
	if (adapter->flags & IXGBE_FLAG_FCOE_CAPABLE) {
		if (hw->mac.ops.get_device_caps) {
			hw->mac.ops.get_device_caps(hw, &device_caps);
			if (device_caps & IXGBE_DEVICE_CAPS_FCOE_OFFLOADS)
				adapter->flags &= ~IXGBE_FLAG_FCOE_CAPABLE;
		}
	}
	if (adapter->flags & IXGBE_FLAG_FCOE_CAPABLE) {
		netdev->vlan_features |= NETIF_F_FCOE_CRC;
		netdev->vlan_features |= NETIF_F_FSO;
		netdev->vlan_features |= NETIF_F_FCOE_MTU;
	}
#endif /* IXGBE_FCOE */
	if (pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (adapter->flags2 & IXGBE_FLAG2_RSC_CAPABLE)
		netdev->hw_features |= NETIF_F_LRO;
	if (adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED)
		netdev->features |= NETIF_F_LRO;

	/* make sure the EEPROM is good */
	if (hw->eeprom.ops.validate_checksum(hw, NULL) < 0) {
		e_dev_err("The EEPROM Checksum Is Not Valid\n");
		err = -EIO;
		goto err_eeprom;
	}

	memcpy(netdev->dev_addr, hw->mac.perm_addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac.perm_addr, netdev->addr_len);

	if (ixgbe_validate_mac_addr(netdev->perm_addr)) {
		e_dev_err("invalid MAC address\n");
		err = -EIO;
		goto err_eeprom;
	}

	setup_timer(&adapter->service_timer, &ixgbe_service_timer,
	            (unsigned long) adapter);

	INIT_WORK(&adapter->service_task, ixgbe_service_task);
	clear_bit(__IXGBE_SERVICE_SCHED, &adapter->state);

	err = ixgbe_init_interrupt_scheme(adapter);
	if (err)
		goto err_sw_init;

	if (!(adapter->flags & IXGBE_FLAG_RSS_ENABLED)) {
		netdev->hw_features &= ~NETIF_F_RXHASH;
		netdev->features &= ~NETIF_F_RXHASH;
	}

	/* WOL not supported for all but the following */
	adapter->wol = 0;
	switch (pdev->device) {
	case IXGBE_DEV_ID_82599_SFP:
		/* Only this subdevice supports WOL */
		if (pdev->subsystem_device == IXGBE_SUBDEV_ID_82599_SFP)
			adapter->wol = IXGBE_WUFC_MAG;
		break;
	case IXGBE_DEV_ID_82599_COMBO_BACKPLANE:
		/* All except this subdevice support WOL */
		if (pdev->subsystem_device != IXGBE_SUBDEV_ID_82599_KX4_KR_MEZZ)
			adapter->wol = IXGBE_WUFC_MAG;
		break;
	case IXGBE_DEV_ID_82599_KX4:
		adapter->wol = IXGBE_WUFC_MAG;
		break;
	case IXGBE_DEV_ID_X540T:
		/* Check eeprom to see if it is enabled */
		hw->eeprom.ops.read(hw, 0x2c, &adapter->eeprom_cap);
		wol_cap = adapter->eeprom_cap & IXGBE_DEVICE_CAPS_WOL_MASK;

		if ((wol_cap == IXGBE_DEVICE_CAPS_WOL_PORT0_1) ||
		    ((wol_cap == IXGBE_DEVICE_CAPS_WOL_PORT0) &&
		     (hw->bus.func == 0)))
			adapter->wol = IXGBE_WUFC_MAG;
		break;
	}
	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	/* save off EEPROM version number */
	hw->eeprom.ops.read(hw, 0x2e, &adapter->eeprom_verh);
	hw->eeprom.ops.read(hw, 0x2d, &adapter->eeprom_verl);

	/* pick up the PCI bus settings for reporting later */
	hw->mac.ops.get_bus_info(hw);

	/* print bus type/speed/width info */
	e_dev_info("(PCI Express:%s:%s) %pM\n",
		   (hw->bus.speed == ixgbe_bus_speed_5000 ? "5.0GT/s" :
		    hw->bus.speed == ixgbe_bus_speed_2500 ? "2.5GT/s" :
		    "Unknown"),
		   (hw->bus.width == ixgbe_bus_width_pcie_x8 ? "Width x8" :
		    hw->bus.width == ixgbe_bus_width_pcie_x4 ? "Width x4" :
		    hw->bus.width == ixgbe_bus_width_pcie_x1 ? "Width x1" :
		    "Unknown"),
		   netdev->dev_addr);

	err = ixgbe_read_pba_string_generic(hw, part_str, IXGBE_PBANUM_LENGTH);
	if (err)
		strncpy(part_str, "Unknown", IXGBE_PBANUM_LENGTH);
	if (ixgbe_is_sfp(hw) && hw->phy.sfp_type != ixgbe_sfp_type_not_present)
		e_dev_info("MAC: %d, PHY: %d, SFP+: %d, PBA No: %s\n",
			   hw->mac.type, hw->phy.type, hw->phy.sfp_type,
		           part_str);
	else
		e_dev_info("MAC: %d, PHY: %d, PBA No: %s\n",
			   hw->mac.type, hw->phy.type, part_str);

	if (hw->bus.width <= ixgbe_bus_width_pcie_x4) {
		e_dev_warn("PCI-Express bandwidth available for this card is "
			   "not sufficient for optimal performance.\n");
		e_dev_warn("For optimal performance a x8 PCI-Express slot "
			   "is required.\n");
	}

	/* reset the hardware with the new settings */
	err = hw->mac.ops.start_hw(hw);

	if (err == IXGBE_ERR_EEPROM_VERSION) {
		/* We are running on a pre-production device, log a warning */
		e_dev_warn("This device is a pre-production adapter/LOM. "
			   "Please be aware there may be issues associated "
			   "with your hardware.  If you are experiencing "
			   "problems please contact your Intel or hardware "
			   "representative who provided you with this "
			   "hardware.\n");
	}
	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	/* power down the optics for multispeed fiber and 82599 SFP+ fiber */
	if (hw->mac.ops.disable_tx_laser &&
	    ((hw->phy.multispeed_fiber) ||
	     ((hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber) &&
	      (hw->mac.type == ixgbe_mac_82599EB))))
		hw->mac.ops.disable_tx_laser(hw);

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

#ifdef CONFIG_IXGBE_DCA
	if (dca_add_requester(&pdev->dev) == 0) {
		adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
		ixgbe_setup_dca(adapter);
	}
#endif
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
		e_info(probe, "IOV is enabled with %d VFs\n", adapter->num_vfs);
		for (i = 0; i < adapter->num_vfs; i++)
			ixgbe_vf_configuration(pdev, (i | 0x10000000));
	}

	/* firmware requires driver version to be 0xFFFFFFFF
	 * since os does not support feature
	 */
	if (hw->mac.ops.set_fw_drv_ver)
		hw->mac.ops.set_fw_drv_ver(hw, 0xFF, 0xFF, 0xFF,
					   0xFF);

	/* add san mac addr to netdev */
	ixgbe_add_sanmac_netdev(netdev);

//	saif_sync_add(adapter,1);

	e_dev_info("Intel(R) 10 Gigabit Network Connection\n");
	cards_found++;
	return 0;

err_register:
	ixgbe_release_hw_control(adapter);
	ixgbe_clear_interrupt_scheme(adapter);
err_sw_init:
err_eeprom:
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		ixgbe_disable_sriov(adapter);
	adapter->flags2 &= ~IXGBE_FLAG2_SEARCH_FOR_SFP;
	iounmap(hw->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * ixgbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ixgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void __devexit ixgbe_remove(struct pci_dev *pdev)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	set_bit(__IXGBE_DOWN, &adapter->state);
	cancel_work_sync(&adapter->service_task);

#ifdef CONFIG_IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
		dca_remove_requester(&pdev->dev);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 1);
	}

#endif
#ifdef IXGBE_FCOE
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED)
		ixgbe_cleanup_fcoe(adapter);

#endif /* IXGBE_FCOE */

	/* remove the added san mac */
	ixgbe_del_sanmac_netdev(netdev);

	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
		if (!(ixgbe_check_vf_assignment(adapter)))
			ixgbe_disable_sriov(adapter);
		else
			e_dev_warn("Unloading driver while VFs are assigned "
				   "- VFs will not be deallocated\n");
	}

	ixgbe_clear_interrupt_scheme(adapter);

	ixgbe_release_hw_control(adapter);

	iounmap(adapter->hw.hw_addr);
	pci_release_selected_regions(pdev, pci_select_bars(pdev,
				     IORESOURCE_MEM));

	e_dev_info("complete\n");

	free_netdev(netdev);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

/**
 * ixgbe_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t ixgbe_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

#ifdef CONFIG_PCI_IOV
	struct pci_dev *bdev, *vfdev;
	u32 dw0, dw1, dw2, dw3;
	int vf, pos;
	u16 req_id, pf_func;

	if (adapter->hw.mac.type == ixgbe_mac_82598EB ||
	    adapter->num_vfs == 0)
		goto skip_bad_vf_detection;

	bdev = pdev->bus->self;
	while (bdev && (bdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT))
		bdev = bdev->bus->self;

	if (!bdev)
		goto skip_bad_vf_detection;

	pos = pci_find_ext_capability(bdev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		goto skip_bad_vf_detection;

	pci_read_config_dword(bdev, pos + PCI_ERR_HEADER_LOG, &dw0);
	pci_read_config_dword(bdev, pos + PCI_ERR_HEADER_LOG + 4, &dw1);
	pci_read_config_dword(bdev, pos + PCI_ERR_HEADER_LOG + 8, &dw2);
	pci_read_config_dword(bdev, pos + PCI_ERR_HEADER_LOG + 12, &dw3);

	req_id = dw1 >> 16;
	/* On the 82599 if bit 7 of the requestor ID is set then it's a VF */
	if (!(req_id & 0x0080))
		goto skip_bad_vf_detection;

	pf_func = req_id & 0x01;
	if ((pf_func & 1) == (pdev->devfn & 1)) {
		unsigned int device_id;

		vf = (req_id & 0x7F) >> 1;
		e_dev_err("VF %d has caused a PCIe error\n", vf);
		e_dev_err("TLP: dw0: %8.8x\tdw1: %8.8x\tdw2: "
				"%8.8x\tdw3: %8.8x\n",
		dw0, dw1, dw2, dw3);
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
			device_id = IXGBE_82599_VF_DEVICE_ID;
			break;
		case ixgbe_mac_X540:
			device_id = IXGBE_X540_VF_DEVICE_ID;
			break;
		default:
			device_id = 0;
			break;
		}

		/* Find the pci device of the offending VF */
		vfdev = pci_get_device(IXGBE_INTEL_VENDOR_ID, device_id, NULL);
		while (vfdev) {
			if (vfdev->devfn == (req_id & 0xFF))
				break;
			vfdev = pci_get_device(IXGBE_INTEL_VENDOR_ID,
					       device_id, vfdev);
		}
		/*
		 * There's a slim chance the VF could have been hot plugged,
		 * so if it is no longer present we don't need to issue the
		 * VFLR.  Just clean up the AER in that case.
		 */
		if (vfdev) {
			e_dev_err("Issuing VFLR to VF %d\n", vf);
			pci_write_config_dword(vfdev, 0xA8, 0x00008000);
		}

		pci_cleanup_aer_uncorrect_error_status(pdev);
	}

	/*
	 * Even though the error may have occurred on the other port
	 * we still need to increment the vf error reference count for
	 * both ports because the I/O resume function will be called
	 * for both of them.
	 */
	adapter->vferr_refcount++;

	return PCI_ERS_RESULT_RECOVERED;

skip_bad_vf_detection:
#endif /* CONFIG_PCI_IOV */
	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev))
		ixgbe_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ixgbe_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t ixgbe_io_slot_reset(struct pci_dev *pdev)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	pci_ers_result_t result;
	int err;

	if (pci_enable_device_mem(pdev)) {
		e_err(probe, "Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);

		pci_wake_from_d3(pdev, false);

		ixgbe_reset(adapter);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_WUS, ~0);
		result = PCI_ERS_RESULT_RECOVERED;
	}

	err = pci_cleanup_aer_uncorrect_error_status(pdev);
	if (err) {
		e_dev_err("pci_cleanup_aer_uncorrect_error_status "
			  "failed 0x%0x\n", err);
		/* non-fatal, continue */
	}

	return result;
}

/**
 * ixgbe_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void ixgbe_io_resume(struct pci_dev *pdev)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

#ifdef CONFIG_PCI_IOV
	if (adapter->vferr_refcount) {
		e_info(drv, "Resuming after VF err\n");
		adapter->vferr_refcount--;
		return;
	}

#endif
	if (netif_running(netdev))
		ixgbe_up(adapter);

	netif_device_attach(netdev);
}

static struct pci_error_handlers ixgbe_err_handler = {
	.error_detected = ixgbe_io_error_detected,
	.slot_reset = ixgbe_io_slot_reset,
	.resume = ixgbe_io_resume,
};

static struct pci_driver ixgbe_driver = {
	.name     = ixgbe_driver_name,
	.id_table = ixgbe_pci_tbl,
	.probe    = ixgbe_probe,
	.remove   = __devexit_p(ixgbe_remove),
#ifdef CONFIG_PM
	.suspend  = ixgbe_suspend,
	.resume   = ixgbe_resume,
#endif
	.shutdown = ixgbe_shutdown,
	.err_handler = &ixgbe_err_handler
};

/**
 * ixgbe_init_module - Driver Registration Routine
 *
 * ixgbe_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init ixgbe_init_module(void)
{
	int ret;
	pr_info("%s - version %s\n", ixgbe_driver_string, ixgbe_driver_version);
	pr_info("%s\n", ixgbe_copyright);
	if(!is_primary)
	{
		printk("%s: not primary going out\n",__func__);
		return 0;
	}
	else
	{
		printk("%s: primary \n",__func__);
	}

#ifdef CONFIG_IXGBE_DCA
	dca_register_notify(&dca_notifier);
#endif

	ret = pci_register_driver(&ixgbe_driver);
	return ret;
}

module_init(ixgbe_init_module);

/**
 * ixgbe_exit_module - Driver Exit Cleanup Routine
 *
 * ixgbe_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit ixgbe_exit_module(void)
{
#ifdef CONFIG_IXGBE_DCA
	dca_unregister_notify(&dca_notifier);
#endif
	pci_unregister_driver(&ixgbe_driver);
	rcu_barrier(); /* Wait for completion of call_rcu()'s */
}

#ifdef CONFIG_IXGBE_DCA
static int ixgbe_notify_dca(struct notifier_block *nb, unsigned long event,
			    void *p)
{
	int ret_val;

	ret_val = driver_for_each_device(&ixgbe_driver.driver, NULL, &event,
					 __ixgbe_notify_dca);

	return ret_val ? NOTIFY_BAD : NOTIFY_DONE;
}

#endif /* CONFIG_IXGBE_DCA */

module_exit(ixgbe_exit_module);


netdev_tx_t snull_xmit_frame_ring(struct sk_buff *skb,
			  struct ixgbe_adapter *adapter,
			  struct ixgbe_ring *tx_ring)
{
	struct ixgbe_tx_buffer *first;
	int tso;
	u32 tx_flags = 0;
#if PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD
	unsigned short f;
	//printk("%s:PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD",__func__);
#endif
	u16 count = TXD_USE_COUNT(skb_headlen(skb));
	__be16 protocol = skb->protocol;
	u8 hdr_len = 0;

	/*
	 * need: 1 descriptor per page * PAGE_SIZE/IXGBE_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_head_len/IXGBE_MAX_DATA_PER_TXD,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
#if PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD
	printk("%s:PAGE_SIZE > IXGBE_MAX_DATA_PER_TXD 2",__func__);
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
#else
	//	printk("%s:else part 2\n",__func__);
	count += skb_shinfo(skb)->nr_frags;
#endif
	if (ixgbe_maybe_stop_tx(tx_ring, count + 3)) {
		tx_ring->tx_stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];

	/* setup IPv4/IPv6 offloads */
	if (protocol == __constant_htons(ETH_P_IP))
		tx_flags |= IXGBE_TX_FLAGS_IPV4;

	tso = ixgbe_tso(tx_ring, skb, tx_flags, protocol, &hdr_len);
	//printk("%s: tso value %d\n",__func__,tso);
	if (tso < 0)
		goto out_drop;
	else if (tso)
		tx_flags |= IXGBE_TX_FLAGS_TSO;
	else if (ixgbe_tx_csum(tx_ring, skb, tx_flags, protocol))
		tx_flags |= IXGBE_TX_FLAGS_CSUM;

	/* add the ATR filter if ATR is on */
	if (test_bit(__IXGBE_TX_FDIR_INIT_DONE, &tx_ring->state))
	{
		//printk("%s:FDIR bit is set\n",__func__);
		ixgbe_atr(tx_ring, skb, tx_flags, protocol);
	}
	else
	{
		//printk("%s:FDIR bit is not set\n",__func__);
	}
	ixgbe_tx_map(tx_ring, skb, first, tx_flags, hdr_len);

	ixgbe_maybe_stop_tx(tx_ring, DESC_NEEDED);

	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

EXPORT_SYMBOL(snull_xmit_frame_ring);





/*popcorn stuff*/


netdev_tx_t snull_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *tx_ring;
	tx_ring = adapter->tx_ring[skb->queue_mapping];
	return snull_xmit_frame_ring(skb, adapter, tx_ring);
}



struct ixgbe_adapter * get_mapping_info_ixgbe_dev()
{
	return adapter_ptr;
}
EXPORT_SYMBOL(get_mapping_info_ixgbe_dev);
void * get_adapter_phy()
{
	return adapter_ptr_phy;
}
EXPORT_SYMBOL(get_adapter_phy);

int get_irq(int q_no)
{
	return adapter_ptr->msix_entries[q_no].vector;
}
EXPORT_SYMBOL(get_irq);

void test_conti()
{
	int size = sizeof(struct ixgbe_adapter);
	int i;
	unsigned char * adapter =(unsigned char *) get_mapping_info_ixgbe_dev();
	void * adapter_phy = get_adapter_phy();
	unsigned char * adap_phy_remap= (unsigned char *)phys_to_virt(adapter_phy);
	
	printk("%s: adapter %p adapter_phy %p adap_phy_remap %p \n",__func__,adapter,adapter_phy,adap_phy_remap);
	
	
	for(i=0;i<size;i++)
	{
		if(adapter[i]!=adap_phy_remap[i])
		{
			printk("%s:Mismatch\n",__func__);
		}
	}
	printk("%s: same\n",__func__);

}

EXPORT_SYMBOL(test_conti);












/* ixgbe_main.c */
