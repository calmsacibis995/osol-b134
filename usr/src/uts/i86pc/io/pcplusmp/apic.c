/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * PSMI 1.1 extensions are supported only in 2.6 and later versions.
 * PSMI 1.2 extensions are supported only in 2.7 and later versions.
 * PSMI 1.3 and 1.4 extensions are supported in Solaris 10.
 * PSMI 1.5 extensions are supported in Solaris Nevada.
 * PSMI 1.6 extensions are supported in Solaris Nevada.
 */
#define	PSMI_1_6

#include <sys/processor.h>
#include <sys/time.h>
#include <sys/psm.h>
#include <sys/smp_impldefs.h>
#include <sys/cram.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/psm_common.h>
#include <sys/apic.h>
#include <sys/pit.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/pci.h>
#include <sys/promif.h>
#include <sys/x86_archext.h>
#include <sys/cpc_impl.h>
#include <sys/uadmin.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include <sys/archsystm.h>
#include <sys/trap.h>
#include <sys/machsystm.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/rm_platter.h>
#include <sys/privregs.h>
#include <sys/note.h>
#include <sys/pci_intr_lib.h>
#include <sys/spl.h>
#include <sys/clock.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/x_call.h>
#include <sys/reboot.h>
#include <sys/hpet.h>

/*
 *	Local Function Prototypes
 */
static void apic_init_intr();
static void apic_nmi_intr(caddr_t arg, struct regs *rp);

/*
 *	standard MP entries
 */
static int	apic_probe();
static int	apic_clkinit();
static int	apic_getclkirq(int ipl);
static uint_t	apic_calibrate(volatile uint32_t *addr,
    uint16_t *pit_ticks_adj);
static hrtime_t apic_gettime();
static hrtime_t apic_gethrtime();
static void	apic_init();
static void	apic_picinit(void);
static int	apic_cpu_start(processorid_t, caddr_t);
static int	apic_post_cpu_start(void);
static void	apic_send_ipi(int cpun, int ipl);
static void	apic_set_idlecpu(processorid_t cpun);
static void	apic_unset_idlecpu(processorid_t cpun);
static int	apic_intr_enter(int ipl, int *vect);
static void	apic_setspl(int ipl);
static void	x2apic_setspl(int ipl);
static int	apic_addspl(int ipl, int vector, int min_ipl, int max_ipl);
static int	apic_delspl(int ipl, int vector, int min_ipl, int max_ipl);
static void	apic_shutdown(int cmd, int fcn);
static void	apic_preshutdown(int cmd, int fcn);
static int	apic_disable_intr(processorid_t cpun);
static void	apic_enable_intr(processorid_t cpun);
static processorid_t	apic_get_next_processorid(processorid_t cpun);
static int		apic_get_ipivect(int ipl, int type);
static void	apic_timer_reprogram(hrtime_t time);
static void	apic_timer_enable(void);
static void	apic_timer_disable(void);
static void	apic_post_cyclic_setup(void *arg);
static void	apic_intrmap_init(int apic_mode);
static void	apic_record_ioapic_rdt(apic_irq_t *irq_ptr, ioapic_rdt_t *irdt);
static void	apic_record_msi(apic_irq_t *irq_ptr, msi_regs_t *mregs);

static int	apic_oneshot = 0;
int	apic_oneshot_enable = 1; /* to allow disabling one-shot capability */

/* Now the ones for Dynamic Interrupt distribution */
int	apic_enable_dynamic_migration = 0;

extern int apic_have_32bit_cr8;

/*
 * These variables are frequently accessed in apic_intr_enter(),
 * apic_intr_exit and apic_setspl, so group them together
 */
volatile uint32_t *apicadr =  NULL;	/* virtual addr of local APIC	*/
int apic_setspl_delay = 1;		/* apic_setspl - delay enable	*/
int apic_clkvect;

/* vector at which error interrupts come in */
int apic_errvect;
int apic_enable_error_intr = 1;
int apic_error_display_delay = 100;

/* vector at which performance counter overflow interrupts come in */
int apic_cpcovf_vect;
int apic_enable_cpcovf_intr = 1;

/* vector at which CMCI interrupts come in */
int apic_cmci_vect;
extern int cmi_enable_cmci;
extern void cmi_cmci_trap(void);

static kmutex_t cmci_cpu_setup_lock;	/* protects cmci_cpu_setup_registered */
static int cmci_cpu_setup_registered;

/*
 * The following vector assignments influence the value of ipltopri and
 * vectortoipl. Note that vectors 0 - 0x1f are not used. We can program
 * idle to 0 and IPL 0 to 0xf to differentiate idle in case
 * we care to do so in future. Note some IPLs which are rarely used
 * will share the vector ranges and heavily used IPLs (5 and 6) have
 * a wide range.
 *
 * This array is used to initialize apic_ipls[] (in apic_init()).
 *
 *	IPL		Vector range.		as passed to intr_enter
 *	0		none.
 *	1,2,3		0x20-0x2f		0x0-0xf
 *	4		0x30-0x3f		0x10-0x1f
 *	5		0x40-0x5f		0x20-0x3f
 *	6		0x60-0x7f		0x40-0x5f
 *	7,8,9		0x80-0x8f		0x60-0x6f
 *	10		0x90-0x9f		0x70-0x7f
 *	11		0xa0-0xaf		0x80-0x8f
 *	...		...
 *	15		0xe0-0xef		0xc0-0xcf
 *	15		0xf0-0xff		0xd0-0xdf
 */
uchar_t apic_vectortoipl[APIC_AVAIL_VECTOR / APIC_VECTOR_PER_IPL] = {
	3, 4, 5, 5, 6, 6, 9, 10, 11, 12, 13, 14, 15, 15
};
	/*
	 * The ipl of an ISR at vector X is apic_vectortoipl[X>>4]
	 * NOTE that this is vector as passed into intr_enter which is
	 * programmed vector - 0x20 (APIC_BASE_VECT)
	 */

uchar_t	apic_ipltopri[MAXIPL + 1];	/* unix ipl to apic pri	*/
	/* The taskpri to be programmed into apic to mask given ipl */

#if defined(__amd64)
uchar_t	apic_cr8pri[MAXIPL + 1];	/* unix ipl to cr8 pri	*/
#endif

/*
 * Correlation of the hardware vector to the IPL in use, initialized
 * from apic_vectortoipl[] in apic_init().  The final IPLs may not correlate
 * to the IPLs in apic_vectortoipl on some systems that share interrupt lines
 * connected to errata-stricken IOAPICs
 */
uchar_t apic_ipls[APIC_AVAIL_VECTOR];

/*
 * Patchable global variables.
 */
int	apic_forceload = 0;

int	apic_coarse_hrtime = 1;		/* 0 - use accurate slow gethrtime() */
					/* 1 - use gettime() for performance */
int	apic_flat_model = 0;		/* 0 - clustered. 1 - flat */
int	apic_enable_hwsoftint = 0;	/* 0 - disable, 1 - enable	*/
int	apic_enable_bind_log = 1;	/* 1 - display interrupt binding log */
int	apic_panic_on_nmi = 0;
int	apic_panic_on_apic_error = 0;

int	apic_verbose = 0;

/* minimum number of timer ticks to program to */
int apic_min_timer_ticks = 1;
/*
 *	Local static data
 */
static struct	psm_ops apic_ops = {
	apic_probe,

	apic_init,
	apic_picinit,
	apic_intr_enter,
	apic_intr_exit,
	apic_setspl,
	apic_addspl,
	apic_delspl,
	apic_disable_intr,
	apic_enable_intr,
	(int (*)(int))NULL,		/* psm_softlvl_to_irq */
	(void (*)(int))NULL,		/* psm_set_softintr */

	apic_set_idlecpu,
	apic_unset_idlecpu,

	apic_clkinit,
	apic_getclkirq,
	(void (*)(void))NULL,		/* psm_hrtimeinit */
	apic_gethrtime,

	apic_get_next_processorid,
	apic_cpu_start,
	apic_post_cpu_start,
	apic_shutdown,
	apic_get_ipivect,
	apic_send_ipi,

	(int (*)(dev_info_t *, int))NULL,	/* psm_translate_irq */
	(void (*)(int, char *))NULL,	/* psm_notify_error */
	(void (*)(int))NULL,		/* psm_notify_func */
	apic_timer_reprogram,
	apic_timer_enable,
	apic_timer_disable,
	apic_post_cyclic_setup,
	apic_preshutdown,
	apic_intr_ops,			/* Advanced DDI Interrupt framework */
	apic_state,			/* save, restore apic state for S3 */
};


static struct	psm_info apic_psm_info = {
	PSM_INFO_VER01_6,			/* version */
	PSM_OWN_EXCLUSIVE,			/* ownership */
	(struct psm_ops *)&apic_ops,		/* operation */
	APIC_PCPLUSMP_NAME,			/* machine name */
	"pcplusmp v1.4 compatible",
};

static void *apic_hdlp;

#ifdef DEBUG
int	apic_debug = 0;
int	apic_restrict_vector = 0;

int	apic_debug_msgbuf[APIC_DEBUG_MSGBUFSIZE];
int	apic_debug_msgbufindex = 0;

#endif /* DEBUG */

apic_cpus_info_t	*apic_cpus;

cpuset_t	apic_cpumask;
uint_t	apic_picinit_called;

/* Flag to indicate that we need to shut down all processors */
static uint_t	apic_shutdown_processors;

uint_t apic_nsec_per_intr = 0;

/*
 * apic_let_idle_redistribute can have the following values:
 * 0 - If clock decremented it from 1 to 0, clock has to call redistribute.
 * apic_redistribute_lock prevents multiple idle cpus from redistributing
 */
int	apic_num_idle_redistributions = 0;
static	int apic_let_idle_redistribute = 0;
static	uint_t apic_nticks = 0;
static	uint_t apic_skipped_redistribute = 0;

/* to gather intr data and redistribute */
static void apic_redistribute_compute(void);

static	uint_t last_count_read = 0;
static	lock_t	apic_gethrtime_lock;
volatile int	apic_hrtime_stamp = 0;
volatile hrtime_t apic_nsec_since_boot = 0;
static uint_t apic_hertz_count;

uint64_t apic_ticks_per_SFnsecs;	/* # of ticks in SF nsecs */

static hrtime_t apic_nsec_max;

static	hrtime_t	apic_last_hrtime = 0;
int		apic_hrtime_error = 0;
int		apic_remote_hrterr = 0;
int		apic_num_nmis = 0;
int		apic_apic_error = 0;
int		apic_num_apic_errors = 0;
int		apic_num_cksum_errors = 0;

int	apic_error = 0;
static	int	apic_cmos_ssb_set = 0;

/* use to make sure only one cpu handles the nmi */
static	lock_t	apic_nmi_lock;
/* use to make sure only one cpu handles the error interrupt */
static	lock_t	apic_error_lock;

static	struct {
	uchar_t	cntl;
	uchar_t	data;
} aspen_bmc[] = {
	{ CC_SMS_WR_START,	0x18 },		/* NetFn/LUN */
	{ CC_SMS_WR_NEXT,	0x24 },		/* Cmd SET_WATCHDOG_TIMER */
	{ CC_SMS_WR_NEXT,	0x84 },		/* DataByte 1: SMS/OS no log */
	{ CC_SMS_WR_NEXT,	0x2 },		/* DataByte 2: Power Down */
	{ CC_SMS_WR_NEXT,	0x0 },		/* DataByte 3: no pre-timeout */
	{ CC_SMS_WR_NEXT,	0x0 },		/* DataByte 4: timer expir. */
	{ CC_SMS_WR_NEXT,	0xa },		/* DataByte 5: init countdown */
	{ CC_SMS_WR_END,	0x0 },		/* DataByte 6: init countdown */

	{ CC_SMS_WR_START,	0x18 },		/* NetFn/LUN */
	{ CC_SMS_WR_END,	0x22 }		/* Cmd RESET_WATCHDOG_TIMER */
};

static	struct {
	int	port;
	uchar_t	data;
} sitka_bmc[] = {
	{ SMS_COMMAND_REGISTER,	SMS_WRITE_START },
	{ SMS_DATA_REGISTER,	0x18 },		/* NetFn/LUN */
	{ SMS_DATA_REGISTER,	0x24 },		/* Cmd SET_WATCHDOG_TIMER */
	{ SMS_DATA_REGISTER,	0x84 },		/* DataByte 1: SMS/OS no log */
	{ SMS_DATA_REGISTER,	0x2 },		/* DataByte 2: Power Down */
	{ SMS_DATA_REGISTER,	0x0 },		/* DataByte 3: no pre-timeout */
	{ SMS_DATA_REGISTER,	0x0 },		/* DataByte 4: timer expir. */
	{ SMS_DATA_REGISTER,	0xa },		/* DataByte 5: init countdown */
	{ SMS_COMMAND_REGISTER,	SMS_WRITE_END },
	{ SMS_DATA_REGISTER,	0x0 },		/* DataByte 6: init countdown */

	{ SMS_COMMAND_REGISTER,	SMS_WRITE_START },
	{ SMS_DATA_REGISTER,	0x18 },		/* NetFn/LUN */
	{ SMS_COMMAND_REGISTER,	SMS_WRITE_END },
	{ SMS_DATA_REGISTER,	0x22 }		/* Cmd RESET_WATCHDOG_TIMER */
};

/* Patchable global variables. */
int		apic_kmdb_on_nmi = 0;		/* 0 - no, 1 - yes enter kmdb */
uint32_t	apic_divide_reg_init = 0;	/* 0 - divide by 2 */

/* default apic ops without interrupt remapping */
static apic_intrmap_ops_t apic_nointrmap_ops = {
	(int (*)(int))return_instr,
	(void (*)(int))return_instr,
	(void (*)(apic_irq_t *))return_instr,
	(void (*)(apic_irq_t *, void *))return_instr,
	(void (*)(apic_irq_t *))return_instr,
	apic_record_ioapic_rdt,
	apic_record_msi,
};

apic_intrmap_ops_t *apic_vt_ops = &apic_nointrmap_ops;

/*
 *	This is the loadable module wrapper
 */

int
_init(void)
{
	if (apic_coarse_hrtime)
		apic_ops.psm_gethrtime = &apic_gettime;
	return (psm_mod_init(&apic_hdlp, &apic_psm_info));
}

int
_fini(void)
{
	return (psm_mod_fini(&apic_hdlp, &apic_psm_info));
}

int
_info(struct modinfo *modinfop)
{
	return (psm_mod_info(&apic_hdlp, &apic_psm_info, modinfop));
}


static int
apic_probe()
{
	return (apic_probe_common(apic_psm_info.p_mach_idstring));
}

void
apic_init()
{
	int i;
	int	j = 1;

	apic_ipltopri[0] = APIC_VECTOR_PER_IPL; /* leave 0 for idle */
	for (i = 0; i < (APIC_AVAIL_VECTOR / APIC_VECTOR_PER_IPL); i++) {
		if ((i < ((APIC_AVAIL_VECTOR / APIC_VECTOR_PER_IPL) - 1)) &&
		    (apic_vectortoipl[i + 1] == apic_vectortoipl[i]))
			/* get to highest vector at the same ipl */
			continue;
		for (; j <= apic_vectortoipl[i]; j++) {
			apic_ipltopri[j] = (i << APIC_IPL_SHIFT) +
			    APIC_BASE_VECT;
		}
	}
	for (; j < MAXIPL + 1; j++)
		/* fill up any empty ipltopri slots */
		apic_ipltopri[j] = (i << APIC_IPL_SHIFT) + APIC_BASE_VECT;
	apic_init_common();
#if defined(__amd64)
	/*
	 * Make cpu-specific interrupt info point to cr8pri vector
	 */
	for (i = 0; i <= MAXIPL; i++)
		apic_cr8pri[i] = apic_ipltopri[i] >> APIC_IPL_SHIFT;
	CPU->cpu_pri_data = apic_cr8pri;
#else
	if (cpuid_have_cr8access(CPU))
		apic_have_32bit_cr8 = 1;
#endif	/* __amd64 */
}

/*
 * handler for APIC Error interrupt. Just print a warning and continue
 */
static int
apic_error_intr()
{
	uint_t	error0, error1, error;
	uint_t	i;

	/*
	 * We need to write before read as per 7.4.17 of system prog manual.
	 * We do both and or the results to be safe
	 */
	error0 = apic_reg_ops->apic_read(APIC_ERROR_STATUS);
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	error1 = apic_reg_ops->apic_read(APIC_ERROR_STATUS);
	error = error0 | error1;

	/*
	 * Clear the APIC error status (do this on all cpus that enter here)
	 * (two writes are required due to the semantics of accessing the
	 * error status register.)
	 */
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);

	/*
	 * Prevent more than 1 CPU from handling error interrupt causing
	 * double printing (interleave of characters from multiple
	 * CPU's when using prom_printf)
	 */
	if (lock_try(&apic_error_lock) == 0)
		return (error ? DDI_INTR_CLAIMED : DDI_INTR_UNCLAIMED);
	if (error) {
#if	DEBUG
		if (apic_debug)
			debug_enter("pcplusmp: APIC Error interrupt received");
#endif /* DEBUG */
		if (apic_panic_on_apic_error)
			cmn_err(CE_PANIC,
			    "APIC Error interrupt on CPU %d. Status = %x\n",
			    psm_get_cpu_id(), error);
		else {
			if ((error & ~APIC_CS_ERRORS) == 0) {
				/* cksum error only */
				apic_error |= APIC_ERR_APIC_ERROR;
				apic_apic_error |= error;
				apic_num_apic_errors++;
				apic_num_cksum_errors++;
			} else {
				/*
				 * prom_printf is the best shot we have of
				 * something which is problem free from
				 * high level/NMI type of interrupts
				 */
				prom_printf("APIC Error interrupt on CPU %d. "
				    "Status 0 = %x, Status 1 = %x\n",
				    psm_get_cpu_id(), error0, error1);
				apic_error |= APIC_ERR_APIC_ERROR;
				apic_apic_error |= error;
				apic_num_apic_errors++;
				for (i = 0; i < apic_error_display_delay; i++) {
					tenmicrosec();
				}
				/*
				 * provide more delay next time limited to
				 * roughly 1 clock tick time
				 */
				if (apic_error_display_delay < 500)
					apic_error_display_delay *= 2;
			}
		}
		lock_clear(&apic_error_lock);
		return (DDI_INTR_CLAIMED);
	} else {
		lock_clear(&apic_error_lock);
		return (DDI_INTR_UNCLAIMED);
	}
	/* NOTREACHED */
}

/*
 * Turn off the mask bit in the performance counter Local Vector Table entry.
 */
static void
apic_cpcovf_mask_clear(void)
{
	apic_reg_ops->apic_write(APIC_PCINT_VECT,
	    (apic_reg_ops->apic_read(APIC_PCINT_VECT) & ~APIC_LVT_MASK));
}

/*ARGSUSED*/
static int
apic_cmci_enable(xc_arg_t arg1, xc_arg_t arg2, xc_arg_t arg3)
{
	apic_reg_ops->apic_write(APIC_CMCI_VECT, apic_cmci_vect);
	return (0);
}

/*ARGSUSED*/
static int
apic_cmci_disable(xc_arg_t arg1, xc_arg_t arg2, xc_arg_t arg3)
{
	apic_reg_ops->apic_write(APIC_CMCI_VECT, apic_cmci_vect | AV_MASK);
	return (0);
}

/*ARGSUSED*/
static int
cmci_cpu_setup(cpu_setup_t what, int cpuid, void *arg)
{
	cpuset_t	cpu_set;

	CPUSET_ONLY(cpu_set, cpuid);

	switch (what) {
		case CPU_ON:
			xc_call(NULL, NULL, NULL, CPUSET2BV(cpu_set),
			    (xc_func_t)apic_cmci_enable);
			break;

		case CPU_OFF:
			xc_call(NULL, NULL, NULL, CPUSET2BV(cpu_set),
			    (xc_func_t)apic_cmci_disable);
			break;

		default:
			break;
	}

	return (0);
}

static void
apic_init_intr()
{
	processorid_t	cpun = psm_get_cpu_id();
	uint_t nlvt;
	uint32_t svr = AV_UNIT_ENABLE | APIC_SPUR_INTR;

	apic_reg_ops->apic_write_task_reg(APIC_MASK_ALL);

	if (apic_mode == LOCAL_APIC) {
		/*
		 * We are running APIC in MMIO mode.
		 */
		if (apic_flat_model) {
			apic_reg_ops->apic_write(APIC_FORMAT_REG,
			    APIC_FLAT_MODEL);
		} else {
			apic_reg_ops->apic_write(APIC_FORMAT_REG,
			    APIC_CLUSTER_MODEL);
		}

		apic_reg_ops->apic_write(APIC_DEST_REG,
		    AV_HIGH_ORDER >> cpun);
	}

	if (apic_directed_EOI_supported()) {
		/*
		 * Setting the 12th bit in the Spurious Interrupt Vector
		 * Register suppresses broadcast EOIs generated by the local
		 * APIC. The suppression of broadcast EOIs happens only when
		 * interrupts are level-triggered.
		 */
		svr |= APIC_SVR_SUPPRESS_BROADCAST_EOI;
	}

	/* need to enable APIC before unmasking NMI */
	apic_reg_ops->apic_write(APIC_SPUR_INT_REG, svr);

	/*
	 * Presence of an invalid vector with delivery mode AV_FIXED can
	 * cause an error interrupt, even if the entry is masked...so
	 * write a valid vector to LVT entries along with the mask bit
	 */

	/* All APICs have timer and LINT0/1 */
	apic_reg_ops->apic_write(APIC_LOCAL_TIMER, AV_MASK|APIC_RESV_IRQ);
	apic_reg_ops->apic_write(APIC_INT_VECT0, AV_MASK|APIC_RESV_IRQ);
	apic_reg_ops->apic_write(APIC_INT_VECT1, AV_NMI);	/* enable NMI */

	/*
	 * On integrated APICs, the number of LVT entries is
	 * 'Max LVT entry' + 1; on 82489DX's (non-integrated
	 * APICs), nlvt is "3" (LINT0, LINT1, and timer)
	 */

	if (apic_cpus[cpun].aci_local_ver < APIC_INTEGRATED_VERS) {
		nlvt = 3;
	} else {
		nlvt = ((apic_reg_ops->apic_read(APIC_VERS_REG) >> 16) &
		    0xFF) + 1;
	}

	if (nlvt >= 5) {
		/* Enable performance counter overflow interrupt */

		if ((x86_feature & X86_MSR) != X86_MSR)
			apic_enable_cpcovf_intr = 0;
		if (apic_enable_cpcovf_intr) {
			if (apic_cpcovf_vect == 0) {
				int ipl = APIC_PCINT_IPL;
				int irq = apic_get_ipivect(ipl, -1);

				ASSERT(irq != -1);
				apic_cpcovf_vect =
				    apic_irq_table[irq]->airq_vector;
				ASSERT(apic_cpcovf_vect);
				(void) add_avintr(NULL, ipl,
				    (avfunc)kcpc_hw_overflow_intr,
				    "apic pcint", irq, NULL, NULL, NULL, NULL);
				kcpc_hw_overflow_intr_installed = 1;
				kcpc_hw_enable_cpc_intr =
				    apic_cpcovf_mask_clear;
			}
			apic_reg_ops->apic_write(APIC_PCINT_VECT,
			    apic_cpcovf_vect);
		}
	}

	if (nlvt >= 6) {
		/* Only mask TM intr if the BIOS apparently doesn't use it */

		uint32_t lvtval;

		lvtval = apic_reg_ops->apic_read(APIC_THERM_VECT);
		if (((lvtval & AV_MASK) == AV_MASK) ||
		    ((lvtval & AV_DELIV_MODE) != AV_SMI)) {
			apic_reg_ops->apic_write(APIC_THERM_VECT,
			    AV_MASK|APIC_RESV_IRQ);
		}
	}

	/* Enable error interrupt */

	if (nlvt >= 4 && apic_enable_error_intr) {
		if (apic_errvect == 0) {
			int ipl = 0xf;	/* get highest priority intr */
			int irq = apic_get_ipivect(ipl, -1);

			ASSERT(irq != -1);
			apic_errvect = apic_irq_table[irq]->airq_vector;
			ASSERT(apic_errvect);
			/*
			 * Not PSMI compliant, but we are going to merge
			 * with ON anyway
			 */
			(void) add_avintr((void *)NULL, ipl,
			    (avfunc)apic_error_intr, "apic error intr",
			    irq, NULL, NULL, NULL, NULL);
		}
		apic_reg_ops->apic_write(APIC_ERR_VECT, apic_errvect);
		apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
		apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	}

	/* Enable CMCI interrupt */
	if (cmi_enable_cmci) {

		mutex_enter(&cmci_cpu_setup_lock);
		if (cmci_cpu_setup_registered == 0) {
			mutex_enter(&cpu_lock);
			register_cpu_setup_func(cmci_cpu_setup, NULL);
			mutex_exit(&cpu_lock);
			cmci_cpu_setup_registered = 1;
		}
		mutex_exit(&cmci_cpu_setup_lock);

		if (apic_cmci_vect == 0) {
			int ipl = 0x2;
			int irq = apic_get_ipivect(ipl, -1);

			ASSERT(irq != -1);
			apic_cmci_vect = apic_irq_table[irq]->airq_vector;
			ASSERT(apic_cmci_vect);

			(void) add_avintr(NULL, ipl,
			    (avfunc)cmi_cmci_trap,
			    "apic cmci intr", irq, NULL, NULL, NULL, NULL);
		}
		apic_reg_ops->apic_write(APIC_CMCI_VECT, apic_cmci_vect);
	}
}

static void
apic_disable_local_apic()
{
	apic_reg_ops->apic_write_task_reg(APIC_MASK_ALL);
	apic_reg_ops->apic_write(APIC_LOCAL_TIMER, AV_MASK);

	/* local intr reg 0 */
	apic_reg_ops->apic_write(APIC_INT_VECT0, AV_MASK);

	/* disable NMI */
	apic_reg_ops->apic_write(APIC_INT_VECT1, AV_MASK);

	/* and error interrupt */
	apic_reg_ops->apic_write(APIC_ERR_VECT, AV_MASK);

	/* and perf counter intr */
	apic_reg_ops->apic_write(APIC_PCINT_VECT, AV_MASK);

	apic_reg_ops->apic_write(APIC_SPUR_INT_REG, APIC_SPUR_INTR);
}

static void
apic_picinit(void)
{
	int i, j;
	uint_t isr;

	/*
	 * Initialize and enable interrupt remapping before apic
	 * hardware initialization
	 */
	apic_intrmap_init(apic_mode);

	/*
	 * On UniSys Model 6520, the BIOS leaves vector 0x20 isr
	 * bit on without clearing it with EOI.  Since softint
	 * uses vector 0x20 to interrupt itself, so softint will
	 * not work on this machine.  In order to fix this problem
	 * a check is made to verify all the isr bits are clear.
	 * If not, EOIs are issued to clear the bits.
	 */
	for (i = 7; i >= 1; i--) {
		isr = apic_reg_ops->apic_read(APIC_ISR_REG + (i * 4));
		if (isr != 0)
			for (j = 0; ((j < 32) && (isr != 0)); j++)
				if (isr & (1 << j)) {
					apic_reg_ops->apic_write(
					    APIC_EOI_REG, 0);
					isr &= ~(1 << j);
					apic_error |= APIC_ERR_BOOT_EOI;
				}
	}

	/* set a flag so we know we have run apic_picinit() */
	apic_picinit_called = 1;
	LOCK_INIT_CLEAR(&apic_gethrtime_lock);
	LOCK_INIT_CLEAR(&apic_ioapic_lock);
	LOCK_INIT_CLEAR(&apic_error_lock);

	picsetup();	 /* initialise the 8259 */

	/* add nmi handler - least priority nmi handler */
	LOCK_INIT_CLEAR(&apic_nmi_lock);

	if (!psm_add_nmintr(0, (avfunc) apic_nmi_intr,
	    "pcplusmp NMI handler", (caddr_t)NULL))
		cmn_err(CE_WARN, "pcplusmp: Unable to add nmi handler");

	/*
	 * Check for directed-EOI capability in the local APIC.
	 */
	if (apic_directed_EOI_supported() == 1) {
		apic_set_directed_EOI_handler();
	}

	apic_init_intr();

	/* enable apic mode if imcr present */
	if (apic_imcrp) {
		outb(APIC_IMCR_P1, (uchar_t)APIC_IMCR_SELECT);
		outb(APIC_IMCR_P2, (uchar_t)APIC_IMCR_APIC);
	}

	ioapic_init_intr(IOAPIC_MASK);
}


/*ARGSUSED1*/
static int
apic_cpu_start(processorid_t cpun, caddr_t arg)
{
	int		loop_count;
	uint32_t	vector;
	uint_t		cpu_id;
	ulong_t		iflag;

	cpu_id =  apic_cpus[cpun].aci_local_id;

	apic_cmos_ssb_set = 1;

	/*
	 * Interrupts on BSP cpu will be disabled during these startup
	 * steps in order to avoid unwanted side effects from
	 * executing interrupt handlers on a problematic BIOS.
	 */

	iflag = intr_clear();
	outb(CMOS_ADDR, SSB);
	outb(CMOS_DATA, BIOS_SHUTDOWN);

	/*
	 * According to X2APIC specification in section '2.3.5.1' of
	 * Interrupt Command Register Semantics, the semantics of
	 * programming the Interrupt Command Register to dispatch an interrupt
	 * is simplified. A single MSR write to the 64-bit ICR is required
	 * for dispatching an interrupt. Specifically, with the 64-bit MSR
	 * interface to ICR, system software is not required to check the
	 * status of the delivery status bit prior to writing to the ICR
	 * to send an IPI. With the removal of the Delivery Status bit,
	 * system software no longer has a reason to read the ICR. It remains
	 * readable only to aid in debugging.
	 */
#ifdef	DEBUG
	APIC_AV_PENDING_SET();
#else
	if (apic_mode == LOCAL_APIC) {
		APIC_AV_PENDING_SET();
	}
#endif /* DEBUG */

	/* for integrated - make sure there is one INIT IPI in buffer */
	/* for external - it will wake up the cpu */
	apic_reg_ops->apic_write_int_cmd(cpu_id, AV_ASSERT | AV_RESET);

	/* If only 1 CPU is installed, PENDING bit will not go low */
	for (loop_count = 0x1000; loop_count; loop_count--) {
		if (apic_mode == LOCAL_APIC &&
		    apic_reg_ops->apic_read(APIC_INT_CMD1) & AV_PENDING)
			apic_ret();
		else
			break;
	}

	apic_reg_ops->apic_write_int_cmd(cpu_id, AV_DEASSERT | AV_RESET);

	drv_usecwait(20000);		/* 20 milli sec */

	if (apic_cpus[cpun].aci_local_ver >= APIC_INTEGRATED_VERS) {
		/* integrated apic */

		vector = (rm_platter_pa >> MMU_PAGESHIFT) &
		    (APIC_VECTOR_MASK | APIC_IPL_MASK);

		/* to offset the INIT IPI queue up in the buffer */
		apic_reg_ops->apic_write_int_cmd(cpu_id, vector | AV_STARTUP);

		drv_usecwait(200);		/* 20 micro sec */

		apic_reg_ops->apic_write_int_cmd(cpu_id, vector | AV_STARTUP);

		drv_usecwait(200);		/* 20 micro sec */
	}
	intr_restore(iflag);
	return (0);
}


#ifdef	DEBUG
int	apic_break_on_cpu = 9;
int	apic_stretch_interrupts = 0;
int	apic_stretch_ISR = 1 << 3;	/* IPL of 3 matches nothing now */

void
apic_break()
{
}
#endif /* DEBUG */

/*
 * platform_intr_enter
 *
 *	Called at the beginning of the interrupt service routine to
 *	mask all level equal to and below the interrupt priority
 *	of the interrupting vector.  An EOI should be given to
 *	the interrupt controller to enable other HW interrupts.
 *
 *	Return -1 for spurious interrupts
 *
 */
/*ARGSUSED*/
static int
apic_intr_enter(int ipl, int *vectorp)
{
	uchar_t vector;
	int nipl;
	int irq;
	ulong_t iflag;
	apic_cpus_info_t *cpu_infop;

	/*
	 * The real vector delivered is (*vectorp + 0x20), but our caller
	 * subtracts 0x20 from the vector before passing it to us.
	 * (That's why APIC_BASE_VECT is 0x20.)
	 */
	vector = (uchar_t)*vectorp;

	/* if interrupted by the clock, increment apic_nsec_since_boot */
	if (vector == apic_clkvect) {
		if (!apic_oneshot) {
			/* NOTE: this is not MT aware */
			apic_hrtime_stamp++;
			apic_nsec_since_boot += apic_nsec_per_intr;
			apic_hrtime_stamp++;
			last_count_read = apic_hertz_count;
			apic_redistribute_compute();
		}

		/* We will avoid all the book keeping overhead for clock */
		nipl = apic_ipls[vector];

		*vectorp = apic_vector_to_irq[vector + APIC_BASE_VECT];
		if (apic_mode == LOCAL_APIC) {
#if defined(__amd64)
			setcr8((ulong_t)(apic_ipltopri[nipl] >>
			    APIC_IPL_SHIFT));
#else
			if (apic_have_32bit_cr8)
				setcr8((ulong_t)(apic_ipltopri[nipl] >>
				    APIC_IPL_SHIFT));
			else
				LOCAL_APIC_WRITE_REG(APIC_TASK_REG,
				    (uint32_t)apic_ipltopri[nipl]);
#endif
			LOCAL_APIC_WRITE_REG(APIC_EOI_REG, 0);
		} else {
			X2APIC_WRITE(APIC_TASK_REG, apic_ipltopri[nipl]);
			X2APIC_WRITE(APIC_EOI_REG, 0);
		}

		return (nipl);
	}

	cpu_infop = &apic_cpus[psm_get_cpu_id()];

	if (vector == (APIC_SPUR_INTR - APIC_BASE_VECT)) {
		cpu_infop->aci_spur_cnt++;
		return (APIC_INT_SPURIOUS);
	}

	/* Check if the vector we got is really what we need */
	if (apic_revector_pending) {
		/*
		 * Disable interrupts for the duration of
		 * the vector translation to prevent a self-race for
		 * the apic_revector_lock.  This cannot be done
		 * in apic_xlate_vector because it is recursive and
		 * we want the vector translation to be atomic with
		 * respect to other (higher-priority) interrupts.
		 */
		iflag = intr_clear();
		vector = apic_xlate_vector(vector + APIC_BASE_VECT) -
		    APIC_BASE_VECT;
		intr_restore(iflag);
	}

	nipl = apic_ipls[vector];
	*vectorp = irq = apic_vector_to_irq[vector + APIC_BASE_VECT];

	if (apic_mode == LOCAL_APIC) {
#if defined(__amd64)
		setcr8((ulong_t)(apic_ipltopri[nipl] >> APIC_IPL_SHIFT));
#else
		if (apic_have_32bit_cr8)
			setcr8((ulong_t)(apic_ipltopri[nipl] >>
			    APIC_IPL_SHIFT));
		else
			LOCAL_APIC_WRITE_REG(APIC_TASK_REG,
			    (uint32_t)apic_ipltopri[nipl]);
#endif
	} else {
		X2APIC_WRITE(APIC_TASK_REG, apic_ipltopri[nipl]);
	}

	cpu_infop->aci_current[nipl] = (uchar_t)irq;
	cpu_infop->aci_curipl = (uchar_t)nipl;
	cpu_infop->aci_ISR_in_progress |= 1 << nipl;

	/*
	 * apic_level_intr could have been assimilated into the irq struct.
	 * but, having it as a character array is more efficient in terms of
	 * cache usage. So, we leave it as is.
	 */
	if (!apic_level_intr[irq]) {
		if (apic_mode == LOCAL_APIC) {
			LOCAL_APIC_WRITE_REG(APIC_EOI_REG, 0);
		} else {
			X2APIC_WRITE(APIC_EOI_REG, 0);
		}
	}

#ifdef	DEBUG
	APIC_DEBUG_BUF_PUT(vector);
	APIC_DEBUG_BUF_PUT(irq);
	APIC_DEBUG_BUF_PUT(nipl);
	APIC_DEBUG_BUF_PUT(psm_get_cpu_id());
	if ((apic_stretch_interrupts) && (apic_stretch_ISR & (1 << nipl)))
		drv_usecwait(apic_stretch_interrupts);

	if (apic_break_on_cpu == psm_get_cpu_id())
		apic_break();
#endif /* DEBUG */
	return (nipl);
}

/*
 * This macro is a common code used by MMIO local apic and X2APIC
 * local apic.
 */
#define	APIC_INTR_EXIT() \
{ \
	cpu_infop = &apic_cpus[psm_get_cpu_id()]; \
	if (apic_level_intr[irq]) \
		apic_reg_ops->apic_send_eoi(irq); \
	cpu_infop->aci_curipl = (uchar_t)prev_ipl; \
	/* ISR above current pri could not be in progress */ \
	cpu_infop->aci_ISR_in_progress &= (2 << prev_ipl) - 1; \
}

/*
 * Any changes made to this function must also change X2APIC
 * version of intr_exit.
 */
void
apic_intr_exit(int prev_ipl, int irq)
{
	apic_cpus_info_t *cpu_infop;

#if defined(__amd64)
	setcr8((ulong_t)apic_cr8pri[prev_ipl]);
#else
	if (apic_have_32bit_cr8)
		setcr8((ulong_t)(apic_ipltopri[prev_ipl] >> APIC_IPL_SHIFT));
	else
		apicadr[APIC_TASK_REG] = apic_ipltopri[prev_ipl];
#endif

	APIC_INTR_EXIT();
}

/*
 * Same as apic_intr_exit() except it uses MSR rather than MMIO
 * to access local apic registers.
 */
void
x2apic_intr_exit(int prev_ipl, int irq)
{
	apic_cpus_info_t *cpu_infop;

	X2APIC_WRITE(APIC_TASK_REG, apic_ipltopri[prev_ipl]);
	APIC_INTR_EXIT();
}

intr_exit_fn_t
psm_intr_exit_fn(void)
{
	if (apic_mode == LOCAL_X2APIC)
		return (x2apic_intr_exit);

	return (apic_intr_exit);
}

/*
 * Mask all interrupts below or equal to the given IPL.
 * Any changes made to this function must also change X2APIC
 * version of setspl.
 */
static void
apic_setspl(int ipl)
{
#if defined(__amd64)
	setcr8((ulong_t)apic_cr8pri[ipl]);
#else
	if (apic_have_32bit_cr8)
		setcr8((ulong_t)(apic_ipltopri[ipl] >> APIC_IPL_SHIFT));
	else
		apicadr[APIC_TASK_REG] = apic_ipltopri[ipl];
#endif

	/* interrupts at ipl above this cannot be in progress */
	apic_cpus[psm_get_cpu_id()].aci_ISR_in_progress &= (2 << ipl) - 1;
	/*
	 * this is a patch fix for the ALR QSMP P5 machine, so that interrupts
	 * have enough time to come in before the priority is raised again
	 * during the idle() loop.
	 */
	if (apic_setspl_delay)
		(void) apic_reg_ops->apic_get_pri();
}

/*
 * X2APIC version of setspl.
 * Mask all interrupts below or equal to the given IPL
 */
static void
x2apic_setspl(int ipl)
{
	X2APIC_WRITE(APIC_TASK_REG, apic_ipltopri[ipl]);

	/* interrupts at ipl above this cannot be in progress */
	apic_cpus[psm_get_cpu_id()].aci_ISR_in_progress &= (2 << ipl) - 1;
}

/*
 * generates an interprocessor interrupt to another CPU. Any changes made to
 * this routine must be accompanied by similar changes to
 * apic_common_send_ipi().
 */
static void
apic_send_ipi(int cpun, int ipl)
{
	int vector;
	ulong_t flag;

	vector = apic_resv_vector[ipl];

	ASSERT((vector >= APIC_BASE_VECT) && (vector <= APIC_SPUR_INTR));

	flag = intr_clear();

	APIC_AV_PENDING_SET();

	apic_reg_ops->apic_write_int_cmd(apic_cpus[cpun].aci_local_id,
	    vector);

	intr_restore(flag);
}


/*ARGSUSED*/
static void
apic_set_idlecpu(processorid_t cpun)
{
}

/*ARGSUSED*/
static void
apic_unset_idlecpu(processorid_t cpun)
{
}


void
apic_ret()
{
}

/*
 * If apic_coarse_time == 1, then apic_gettime() is used instead of
 * apic_gethrtime().  This is used for performance instead of accuracy.
 */

static hrtime_t
apic_gettime()
{
	int old_hrtime_stamp;
	hrtime_t temp;

	/*
	 * In one-shot mode, we do not keep time, so if anyone
	 * calls psm_gettime() directly, we vector over to
	 * gethrtime().
	 * one-shot mode MUST NOT be enabled if this psm is the source of
	 * hrtime.
	 */

	if (apic_oneshot)
		return (gethrtime());


gettime_again:
	while ((old_hrtime_stamp = apic_hrtime_stamp) & 1)
		apic_ret();

	temp = apic_nsec_since_boot;

	if (apic_hrtime_stamp != old_hrtime_stamp) {	/* got an interrupt */
		goto gettime_again;
	}
	return (temp);
}

/*
 * Here we return the number of nanoseconds since booting.  Note every
 * clock interrupt increments apic_nsec_since_boot by the appropriate
 * amount.
 */
static hrtime_t
apic_gethrtime()
{
	int curr_timeval, countval, elapsed_ticks;
	int old_hrtime_stamp, status;
	hrtime_t temp;
	uint32_t cpun;
	ulong_t oflags;

	/*
	 * In one-shot mode, we do not keep time, so if anyone
	 * calls psm_gethrtime() directly, we vector over to
	 * gethrtime().
	 * one-shot mode MUST NOT be enabled if this psm is the source of
	 * hrtime.
	 */

	if (apic_oneshot)
		return (gethrtime());

	oflags = intr_clear();	/* prevent migration */

	cpun = apic_reg_ops->apic_read(APIC_LID_REG);
	if (apic_mode == LOCAL_APIC)
		cpun >>= APIC_ID_BIT_OFFSET;

	lock_set(&apic_gethrtime_lock);

gethrtime_again:
	while ((old_hrtime_stamp = apic_hrtime_stamp) & 1)
		apic_ret();

	/*
	 * Check to see which CPU we are on.  Note the time is kept on
	 * the local APIC of CPU 0.  If on CPU 0, simply read the current
	 * counter.  If on another CPU, issue a remote read command to CPU 0.
	 */
	if (cpun == apic_cpus[0].aci_local_id) {
		countval = apic_reg_ops->apic_read(APIC_CURR_COUNT);
	} else {
#ifdef	DEBUG
		APIC_AV_PENDING_SET();
#else
		if (apic_mode == LOCAL_APIC)
			APIC_AV_PENDING_SET();
#endif /* DEBUG */

		apic_reg_ops->apic_write_int_cmd(
		    apic_cpus[0].aci_local_id, APIC_CURR_ADD | AV_REMOTE);

		while ((status = apic_reg_ops->apic_read(APIC_INT_CMD1))
		    & AV_READ_PENDING) {
			apic_ret();
		}

		if (status & AV_REMOTE_STATUS)	/* 1 = valid */
			countval = apic_reg_ops->apic_read(APIC_REMOTE_READ);
		else {	/* 0 = invalid */
			apic_remote_hrterr++;
			/*
			 * return last hrtime right now, will need more
			 * testing if change to retry
			 */
			temp = apic_last_hrtime;

			lock_clear(&apic_gethrtime_lock);

			intr_restore(oflags);

			return (temp);
		}
	}
	if (countval > last_count_read)
		countval = 0;
	else
		last_count_read = countval;

	elapsed_ticks = apic_hertz_count - countval;

	curr_timeval = APIC_TICKS_TO_NSECS(elapsed_ticks);
	temp = apic_nsec_since_boot + curr_timeval;

	if (apic_hrtime_stamp != old_hrtime_stamp) {	/* got an interrupt */
		/* we might have clobbered last_count_read. Restore it */
		last_count_read = apic_hertz_count;
		goto gethrtime_again;
	}

	if (temp < apic_last_hrtime) {
		/* return last hrtime if error occurs */
		apic_hrtime_error++;
		temp = apic_last_hrtime;
	}
	else
		apic_last_hrtime = temp;

	lock_clear(&apic_gethrtime_lock);
	intr_restore(oflags);

	return (temp);
}

/* apic NMI handler */
/*ARGSUSED*/
static void
apic_nmi_intr(caddr_t arg, struct regs *rp)
{
	if (apic_shutdown_processors) {
		apic_disable_local_apic();
		return;
	}

	apic_error |= APIC_ERR_NMI;

	if (!lock_try(&apic_nmi_lock))
		return;
	apic_num_nmis++;

	if (apic_kmdb_on_nmi && psm_debugger()) {
		debug_enter("NMI received: entering kmdb\n");
	} else if (apic_panic_on_nmi) {
		/* Keep panic from entering kmdb. */
		nopanicdebug = 1;
		panic("NMI received\n");
	} else {
		/*
		 * prom_printf is the best shot we have of something which is
		 * problem free from high level/NMI type of interrupts
		 */
		prom_printf("NMI received\n");
	}

	lock_clear(&apic_nmi_lock);
}

/*ARGSUSED*/
static int
apic_addspl(int irqno, int ipl, int min_ipl, int max_ipl)
{
	return (apic_addspl_common(irqno, ipl, min_ipl, max_ipl));
}

static int
apic_delspl(int irqno, int ipl, int min_ipl, int max_ipl)
{
	return (apic_delspl_common(irqno, ipl, min_ipl,  max_ipl));
}

static int
apic_post_cpu_start()
{
	int cpun;
	static int cpus_started = 1;
	struct psm_ops *pops = &apic_ops;

	/* We know this CPU + BSP  started successfully. */
	cpus_started++;

	/*
	 * On BSP we would have enabled X2APIC, if supported by processor,
	 * in acpi_probe(), but on AP we do it here.
	 *
	 * We enable X2APIC mode only if BSP is running in X2APIC & the
	 * local APIC mode of the current CPU is MMIO (xAPIC).
	 */
	if (apic_mode == LOCAL_X2APIC && apic_detect_x2apic() &&
	    apic_local_mode() == LOCAL_APIC) {
		apic_enable_x2apic();
	}

	/*
	 * We change psm_send_ipi and send_dirintf only if Solaris
	 * is booted in kmdb & the current CPU is the last CPU being
	 * brought up. We don't need to do anything if Solaris is running
	 * in MMIO mode (xAPIC).
	 */
	if ((boothowto & RB_DEBUG) &&
	    (cpus_started == boot_ncpus || cpus_started == apic_nproc) &&
	    apic_mode == LOCAL_X2APIC) {
		/*
		 * We no longer need help from apic_common_send_ipi()
		 * since we will not start any more CPUs.
		 *
		 * We will need to revisit this if we start supporting
		 * hot-plugging of CPUs.
		 */
		pops->psm_send_ipi = x2apic_send_ipi;
		send_dirintf = pops->psm_send_ipi;
	}

	splx(ipltospl(LOCK_LEVEL));
	apic_init_intr();

	/*
	 * since some systems don't enable the internal cache on the non-boot
	 * cpus, so we have to enable them here
	 */
	setcr0(getcr0() & ~(CR0_CD | CR0_NW));

#ifdef	DEBUG
	APIC_AV_PENDING_SET();
#else
	if (apic_mode == LOCAL_APIC)
		APIC_AV_PENDING_SET();
#endif	/* DEBUG */

	/*
	 * We may be booting, or resuming from suspend; aci_status will
	 * be APIC_CPU_INTR_ENABLE if coming from suspend, so we add the
	 * APIC_CPU_ONLINE flag here rather than setting aci_status completely.
	 */
	cpun = psm_get_cpu_id();
	apic_cpus[cpun].aci_status |= APIC_CPU_ONLINE;

	apic_reg_ops->apic_write(APIC_DIVIDE_REG, apic_divide_reg_init);
	return (PSM_SUCCESS);
}

processorid_t
apic_get_next_processorid(processorid_t cpu_id)
{

	int i;

	if (cpu_id == -1)
		return ((processorid_t)0);

	for (i = cpu_id + 1; i < NCPU; i++) {
		if (CPU_IN_SET(apic_cpumask, i))
			return (i);
	}

	return ((processorid_t)-1);
}


/*
 * type == -1 indicates it is an internal request. Do not change
 * resv_vector for these requests
 */
static int
apic_get_ipivect(int ipl, int type)
{
	uchar_t vector;
	int irq;

	if ((irq = apic_allocate_irq(APIC_VECTOR(ipl))) != -1) {
		if (vector = apic_allocate_vector(ipl, irq, 1)) {
			apic_irq_table[irq]->airq_mps_intr_index =
			    RESERVE_INDEX;
			apic_irq_table[irq]->airq_vector = vector;
			if (type != -1) {
				apic_resv_vector[ipl] = vector;
			}
			return (irq);
		}
	}
	apic_error |= APIC_ERR_GET_IPIVECT_FAIL;
	return (-1);	/* shouldn't happen */
}

static int
apic_getclkirq(int ipl)
{
	int	irq;

	if ((irq = apic_get_ipivect(ipl, -1)) == -1)
		return (-1);
	/*
	 * Note the vector in apic_clkvect for per clock handling.
	 */
	apic_clkvect = apic_irq_table[irq]->airq_vector - APIC_BASE_VECT;
	APIC_VERBOSE_IOAPIC((CE_NOTE, "get_clkirq: vector = %x\n",
	    apic_clkvect));
	return (irq);
}


/*
 * Return the number of APIC clock ticks elapsed for 8245 to decrement
 * (APIC_TIME_COUNT + pit_ticks_adj) ticks.
 */
static uint_t
apic_calibrate(volatile uint32_t *addr, uint16_t *pit_ticks_adj)
{
	uint8_t		pit_tick_lo;
	uint16_t	pit_tick, target_pit_tick;
	uint32_t	start_apic_tick, end_apic_tick;
	ulong_t		iflag;
	uint32_t	reg;

	reg = addr + APIC_CURR_COUNT - apicadr;

	iflag = intr_clear();

	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick < APIC_TIME_MIN ||
	    pit_tick_lo <= APIC_LB_MIN || pit_tick_lo >= APIC_LB_MAX);

	/*
	 * Wait for the 8254 to decrement by 5 ticks to ensure
	 * we didn't start in the middle of a tick.
	 * Compare with 0x10 for the wrap around case.
	 */
	target_pit_tick = pit_tick - 5;
	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick > target_pit_tick || pit_tick_lo < 0x10);

	start_apic_tick = apic_reg_ops->apic_read(reg);

	/*
	 * Wait for the 8254 to decrement by
	 * (APIC_TIME_COUNT + pit_ticks_adj) ticks
	 */
	target_pit_tick = pit_tick - APIC_TIME_COUNT;
	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick > target_pit_tick || pit_tick_lo < 0x10);

	end_apic_tick = apic_reg_ops->apic_read(reg);

	*pit_ticks_adj = target_pit_tick - pit_tick;

	intr_restore(iflag);

	return (start_apic_tick - end_apic_tick);
}

/*
 * Initialise the APIC timer on the local APIC of CPU 0 to the desired
 * frequency.  Note at this stage in the boot sequence, the boot processor
 * is the only active processor.
 * hertz value of 0 indicates a one-shot mode request.  In this case
 * the function returns the resolution (in nanoseconds) for the hardware
 * timer interrupt.  If one-shot mode capability is not available,
 * the return value will be 0. apic_enable_oneshot is a global switch
 * for disabling the functionality.
 * A non-zero positive value for hertz indicates a periodic mode request.
 * In this case the hardware will be programmed to generate clock interrupts
 * at hertz frequency and returns the resolution of interrupts in
 * nanosecond.
 */

static int
apic_clkinit(int hertz)
{
	uint_t		apic_ticks = 0;
	uint_t		pit_ticks;
	int		ret;
	uint16_t	pit_ticks_adj;
	static int	firsttime = 1;

	if (firsttime) {
		/* first time calibrate on CPU0 only */

		apic_reg_ops->apic_write(APIC_DIVIDE_REG, apic_divide_reg_init);
		apic_reg_ops->apic_write(APIC_INIT_COUNT, APIC_MAXVAL);
		apic_ticks = apic_calibrate(apicadr, &pit_ticks_adj);

		/* total number of PIT ticks corresponding to apic_ticks */
		pit_ticks = APIC_TIME_COUNT + pit_ticks_adj;

		/*
		 * Determine the number of nanoseconds per APIC clock tick
		 * and then determine how many APIC ticks to interrupt at the
		 * desired frequency
		 * apic_ticks / (pitticks / PIT_HZ) = apic_ticks_per_s
		 * (apic_ticks * PIT_HZ) / pitticks = apic_ticks_per_s
		 * apic_ticks_per_ns = (apic_ticks * PIT_HZ) / (pitticks * 10^9)
		 * pic_ticks_per_SFns =
		 *   (SF * apic_ticks * PIT_HZ) / (pitticks * 10^9)
		 */
		apic_ticks_per_SFnsecs =
		    ((SF * apic_ticks * PIT_HZ) /
		    ((uint64_t)pit_ticks * NANOSEC));

		/* the interval timer initial count is 32 bit max */
		apic_nsec_max = APIC_TICKS_TO_NSECS(APIC_MAXVAL);
		firsttime = 0;
	}

	if (hertz != 0) {
		/* periodic */
		apic_nsec_per_intr = NANOSEC / hertz;
		apic_hertz_count = APIC_NSECS_TO_TICKS(apic_nsec_per_intr);
	}

	apic_int_busy_mark = (apic_int_busy_mark *
	    apic_sample_factor_redistribution) / 100;
	apic_int_free_mark = (apic_int_free_mark *
	    apic_sample_factor_redistribution) / 100;
	apic_diff_for_redistribution = (apic_diff_for_redistribution *
	    apic_sample_factor_redistribution) / 100;

	if (hertz == 0) {
		/* requested one_shot */
		if (!tsc_gethrtime_enable || !apic_oneshot_enable)
			return (0);
		apic_oneshot = 1;
		ret = (int)APIC_TICKS_TO_NSECS(1);
	} else {
		/* program the local APIC to interrupt at the given frequency */
		apic_reg_ops->apic_write(APIC_INIT_COUNT, apic_hertz_count);
		apic_reg_ops->apic_write(APIC_LOCAL_TIMER,
		    (apic_clkvect + APIC_BASE_VECT) | AV_TIME);
		apic_oneshot = 0;
		ret = NANOSEC / hertz;
	}

	return (ret);

}

/*
 * apic_preshutdown:
 * Called early in shutdown whilst we can still access filesystems to do
 * things like loading modules which will be required to complete shutdown
 * after filesystems are all unmounted.
 */
static void
apic_preshutdown(int cmd, int fcn)
{
	APIC_VERBOSE_POWEROFF(("apic_preshutdown(%d,%d); m=%d a=%d\n",
	    cmd, fcn, apic_poweroff_method, apic_enable_acpi));

	if ((cmd != A_SHUTDOWN) || (fcn != AD_POWEROFF)) {
		return;
	}
}

static void
apic_shutdown(int cmd, int fcn)
{
	int restarts, attempts;
	int i;
	uchar_t	byte;
	ulong_t iflag;

	hpet_acpi_fini();

	/* Send NMI to all CPUs except self to do per processor shutdown */
	iflag = intr_clear();
#ifdef	DEBUG
	APIC_AV_PENDING_SET();
#else
	if (apic_mode == LOCAL_APIC)
		APIC_AV_PENDING_SET();
#endif /* DEBUG */
	apic_shutdown_processors = 1;
	apic_reg_ops->apic_write(APIC_INT_CMD1,
	    AV_NMI | AV_LEVEL | AV_SH_ALL_EXCSELF);

	/* restore cmos shutdown byte before reboot */
	if (apic_cmos_ssb_set) {
		outb(CMOS_ADDR, SSB);
		outb(CMOS_DATA, 0);
	}

	ioapic_disable_redirection();

	/*	disable apic mode if imcr present	*/
	if (apic_imcrp) {
		outb(APIC_IMCR_P1, (uchar_t)APIC_IMCR_SELECT);
		outb(APIC_IMCR_P2, (uchar_t)APIC_IMCR_PIC);
	}

	apic_disable_local_apic();

	intr_restore(iflag);

	/* remainder of function is for shutdown cases only */
	if (cmd != A_SHUTDOWN)
		return;

	/*
	 * Switch system back into Legacy-Mode if using ACPI and
	 * not powering-off.  Some BIOSes need to remain in ACPI-mode
	 * for power-off to succeed (Dell Dimension 4600)
	 * Do not disable ACPI while doing fastreboot
	 */
	if (apic_enable_acpi && fcn != AD_POWEROFF && fcn != AD_FASTREBOOT)
		(void) AcpiDisable();

	if (fcn == AD_FASTREBOOT) {
		apic_reg_ops->apic_write(APIC_INT_CMD1,
		    AV_ASSERT | AV_RESET | AV_SH_ALL_EXCSELF);
	}

	/* remainder of function is for shutdown+poweroff case only */
	if (fcn != AD_POWEROFF)
		return;

	switch (apic_poweroff_method) {
		case APIC_POWEROFF_VIA_RTC:

			/* select the extended NVRAM bank in the RTC */
			outb(CMOS_ADDR, RTC_REGA);
			byte = inb(CMOS_DATA);
			outb(CMOS_DATA, (byte | EXT_BANK));

			outb(CMOS_ADDR, PFR_REG);

			/* for Predator must toggle the PAB bit */
			byte = inb(CMOS_DATA);

			/*
			 * clear power active bar, wakeup alarm and
			 * kickstart
			 */
			byte &= ~(PAB_CBIT | WF_FLAG | KS_FLAG);
			outb(CMOS_DATA, byte);

			/* delay before next write */
			drv_usecwait(1000);

			/* for S40 the following would suffice */
			byte = inb(CMOS_DATA);

			/* power active bar control bit */
			byte |= PAB_CBIT;
			outb(CMOS_DATA, byte);

			break;

		case APIC_POWEROFF_VIA_ASPEN_BMC:
			restarts = 0;
restart_aspen_bmc:
			if (++restarts == 3)
				break;
			attempts = 0;
			do {
				byte = inb(MISMIC_FLAG_REGISTER);
				byte &= MISMIC_BUSY_MASK;
				if (byte != 0) {
					drv_usecwait(1000);
					if (attempts >= 3)
						goto restart_aspen_bmc;
					++attempts;
				}
			} while (byte != 0);
			outb(MISMIC_CNTL_REGISTER, CC_SMS_GET_STATUS);
			byte = inb(MISMIC_FLAG_REGISTER);
			byte |= 0x1;
			outb(MISMIC_FLAG_REGISTER, byte);
			i = 0;
			for (; i < (sizeof (aspen_bmc)/sizeof (aspen_bmc[0]));
			    i++) {
				attempts = 0;
				do {
					byte = inb(MISMIC_FLAG_REGISTER);
					byte &= MISMIC_BUSY_MASK;
					if (byte != 0) {
						drv_usecwait(1000);
						if (attempts >= 3)
							goto restart_aspen_bmc;
						++attempts;
					}
				} while (byte != 0);
				outb(MISMIC_CNTL_REGISTER, aspen_bmc[i].cntl);
				outb(MISMIC_DATA_REGISTER, aspen_bmc[i].data);
				byte = inb(MISMIC_FLAG_REGISTER);
				byte |= 0x1;
				outb(MISMIC_FLAG_REGISTER, byte);
			}
			break;

		case APIC_POWEROFF_VIA_SITKA_BMC:
			restarts = 0;
restart_sitka_bmc:
			if (++restarts == 3)
				break;
			attempts = 0;
			do {
				byte = inb(SMS_STATUS_REGISTER);
				byte &= SMS_STATE_MASK;
				if ((byte == SMS_READ_STATE) ||
				    (byte == SMS_WRITE_STATE)) {
					drv_usecwait(1000);
					if (attempts >= 3)
						goto restart_sitka_bmc;
					++attempts;
				}
			} while ((byte == SMS_READ_STATE) ||
			    (byte == SMS_WRITE_STATE));
			outb(SMS_COMMAND_REGISTER, SMS_GET_STATUS);
			i = 0;
			for (; i < (sizeof (sitka_bmc)/sizeof (sitka_bmc[0]));
			    i++) {
				attempts = 0;
				do {
					byte = inb(SMS_STATUS_REGISTER);
					byte &= SMS_IBF_MASK;
					if (byte != 0) {
						drv_usecwait(1000);
						if (attempts >= 3)
							goto restart_sitka_bmc;
						++attempts;
					}
				} while (byte != 0);
				outb(sitka_bmc[i].port, sitka_bmc[i].data);
			}
			break;

		case APIC_POWEROFF_NONE:

			/* If no APIC direct method, we will try using ACPI */
			if (apic_enable_acpi) {
				if (acpi_poweroff() == 1)
					return;
			} else
				return;

			break;
	}
	/*
	 * Wait a limited time here for power to go off.
	 * If the power does not go off, then there was a
	 * problem and we should continue to the halt which
	 * prints a message for the user to press a key to
	 * reboot.
	 */
	drv_usecwait(7000000); /* wait seven seconds */

}

/*
 * Try and disable all interrupts. We just assign interrupts to other
 * processors based on policy. If any were bound by user request, we
 * let them continue and return failure. We do not bother to check
 * for cache affinity while rebinding.
 */

static int
apic_disable_intr(processorid_t cpun)
{
	int bind_cpu = 0, i, hardbound = 0;
	apic_irq_t *irq_ptr;
	ulong_t iflag;

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	for (i = 0; i <= APIC_MAX_VECTOR; i++) {
		if (apic_reprogram_info[i].done == B_FALSE) {
			if (apic_reprogram_info[i].bindcpu == cpun) {
				/*
				 * CPU is busy -- it's the target of
				 * a pending reprogramming attempt
				 */
				lock_clear(&apic_ioapic_lock);
				intr_restore(iflag);
				return (PSM_FAILURE);
			}
		}
	}

	apic_cpus[cpun].aci_status &= ~APIC_CPU_INTR_ENABLE;

	apic_cpus[cpun].aci_curipl = 0;

	i = apic_min_device_irq;
	for (; i <= apic_max_device_irq; i++) {
		/*
		 * If there are bound interrupts on this cpu, then
		 * rebind them to other processors.
		 */
		if ((irq_ptr = apic_irq_table[i]) != NULL) {
			ASSERT((irq_ptr->airq_temp_cpu == IRQ_UNBOUND) ||
			    (irq_ptr->airq_temp_cpu == IRQ_UNINIT) ||
			    ((irq_ptr->airq_temp_cpu & ~IRQ_USER_BOUND) <
			    apic_nproc));

			if (irq_ptr->airq_temp_cpu == (cpun | IRQ_USER_BOUND)) {
				hardbound = 1;
				continue;
			}

			if (irq_ptr->airq_temp_cpu == cpun) {
				do {
					bind_cpu = apic_next_bind_cpu++;
					if (bind_cpu >= apic_nproc) {
						apic_next_bind_cpu = 1;
						bind_cpu = 0;

					}
				} while (apic_rebind_all(irq_ptr, bind_cpu));
			}
		}
	}

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);

	if (hardbound) {
		cmn_err(CE_WARN, "Could not disable interrupts on %d"
		    "due to user bound interrupts", cpun);
		return (PSM_FAILURE);
	}
	else
		return (PSM_SUCCESS);
}

/*
 * Bind interrupts to the CPU's local APIC.
 * Interrupts should not be bound to a CPU's local APIC until the CPU
 * is ready to receive interrupts.
 */
static void
apic_enable_intr(processorid_t cpun)
{
	int	i;
	apic_irq_t *irq_ptr;
	ulong_t iflag;

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	apic_cpus[cpun].aci_status |= APIC_CPU_INTR_ENABLE;

	i = apic_min_device_irq;
	for (i = apic_min_device_irq; i <= apic_max_device_irq; i++) {
		if ((irq_ptr = apic_irq_table[i]) != NULL) {
			if ((irq_ptr->airq_cpu & ~IRQ_USER_BOUND) == cpun) {
				(void) apic_rebind_all(irq_ptr,
				    irq_ptr->airq_cpu);
			}
		}
	}

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);
}


/*
 * This function will reprogram the timer.
 *
 * When in oneshot mode the argument is the absolute time in future to
 * generate the interrupt at.
 *
 * When in periodic mode, the argument is the interval at which the
 * interrupts should be generated. There is no need to support the periodic
 * mode timer change at this time.
 */
static void
apic_timer_reprogram(hrtime_t time)
{
	hrtime_t now;
	uint_t ticks;
	int64_t delta;

	/*
	 * We should be called from high PIL context (CBE_HIGH_PIL),
	 * so kpreempt is disabled.
	 */

	if (!apic_oneshot) {
		/* time is the interval for periodic mode */
		ticks = APIC_NSECS_TO_TICKS(time);
	} else {
		/* one shot mode */

		now = gethrtime();
		delta = time - now;

		if (delta <= 0) {
			/*
			 * requested to generate an interrupt in the past
			 * generate an interrupt as soon as possible
			 */
			ticks = apic_min_timer_ticks;
		} else if (delta > apic_nsec_max) {
			/*
			 * requested to generate an interrupt at a time
			 * further than what we are capable of. Set to max
			 * the hardware can handle
			 */

			ticks = APIC_MAXVAL;
#ifdef DEBUG
			cmn_err(CE_CONT, "apic_timer_reprogram, request at"
			    "  %lld  too far in future, current time"
			    "  %lld \n", time, now);
#endif
		} else
			ticks = APIC_NSECS_TO_TICKS(delta);
	}

	if (ticks < apic_min_timer_ticks)
		ticks = apic_min_timer_ticks;

	apic_reg_ops->apic_write(APIC_INIT_COUNT, ticks);
}

/*
 * This function will enable timer interrupts.
 */
static void
apic_timer_enable(void)
{
	/*
	 * We should be Called from high PIL context (CBE_HIGH_PIL),
	 * so kpreempt is disabled.
	 */

	if (!apic_oneshot) {
		apic_reg_ops->apic_write(APIC_LOCAL_TIMER,
		    (apic_clkvect + APIC_BASE_VECT) | AV_TIME);
	} else {
		/* one shot */
		apic_reg_ops->apic_write(APIC_LOCAL_TIMER,
		    (apic_clkvect + APIC_BASE_VECT));
	}
}

/*
 * This function will disable timer interrupts.
 */
static void
apic_timer_disable(void)
{
	/*
	 * We should be Called from high PIL context (CBE_HIGH_PIL),
	 * so kpreempt is disabled.
	 */
	apic_reg_ops->apic_write(APIC_LOCAL_TIMER,
	    (apic_clkvect + APIC_BASE_VECT) | AV_MASK);
}

/*
 * Set timer far into the future and return timer
 * current Count in nanoseconds.
 */
hrtime_t
apic_timer_stop_count(void)
{
	hrtime_t	ns_val;
	int		enable_val, count_val;

	/*
	 * Should be called with interrupts disabled.
	 */
	ASSERT(!interrupts_enabled());

	enable_val = apic_reg_ops->apic_read(APIC_LOCAL_TIMER);
	if ((enable_val & AV_MASK) == AV_MASK)
		return ((hrtime_t)-1);		/* timer is disabled */

	count_val = apic_reg_ops->apic_read(APIC_CURR_COUNT);
	ns_val = APIC_TICKS_TO_NSECS(count_val);

	apic_reg_ops->apic_write(APIC_INIT_COUNT, APIC_MAXVAL);

	return (ns_val);
}

/*
 * Reprogram timer after Deep C-State.
 */
void
apic_timer_restart(hrtime_t time)
{
	apic_timer_reprogram(time);
}

ddi_periodic_t apic_periodic_id;

/*
 * If this module needs a periodic handler for the interrupt distribution, it
 * can be added here. The argument to the periodic handler is not currently
 * used, but is reserved for future.
 */
static void
apic_post_cyclic_setup(void *arg)
{
_NOTE(ARGUNUSED(arg))
	/* cpu_lock is held */
	/* set up a periodic handler for intr redistribution */

	/*
	 * In peridoc mode intr redistribution processing is done in
	 * apic_intr_enter during clk intr processing
	 */
	if (!apic_oneshot)
		return;
	/*
	 * Register a periodical handler for the redistribution processing.
	 * On X86, CY_LOW_LEVEL is mapped to the level 2 interrupt, so
	 * DDI_IPL_2 should be passed to ddi_periodic_add() here.
	 */
	apic_periodic_id = ddi_periodic_add(
	    (void (*)(void *))apic_redistribute_compute, NULL,
	    apic_redistribute_sample_interval, DDI_IPL_2);
}

static void
apic_redistribute_compute(void)
{
	int	i, j, max_busy;

	if (apic_enable_dynamic_migration) {
		if (++apic_nticks == apic_sample_factor_redistribution) {
			/*
			 * Time to call apic_intr_redistribute().
			 * reset apic_nticks. This will cause max_busy
			 * to be calculated below and if it is more than
			 * apic_int_busy, we will do the whole thing
			 */
			apic_nticks = 0;
		}
		max_busy = 0;
		for (i = 0; i < apic_nproc; i++) {

			/*
			 * Check if curipl is non zero & if ISR is in
			 * progress
			 */
			if (((j = apic_cpus[i].aci_curipl) != 0) &&
			    (apic_cpus[i].aci_ISR_in_progress & (1 << j))) {

				int	irq;
				apic_cpus[i].aci_busy++;
				irq = apic_cpus[i].aci_current[j];
				apic_irq_table[irq]->airq_busy++;
			}

			if (!apic_nticks &&
			    (apic_cpus[i].aci_busy > max_busy))
				max_busy = apic_cpus[i].aci_busy;
		}
		if (!apic_nticks) {
			if (max_busy > apic_int_busy_mark) {
			/*
			 * We could make the following check be
			 * skipped > 1 in which case, we get a
			 * redistribution at half the busy mark (due to
			 * double interval). Need to be able to collect
			 * more empirical data to decide if that is a
			 * good strategy. Punt for now.
			 */
				if (apic_skipped_redistribute) {
					apic_cleanup_busy();
					apic_skipped_redistribute = 0;
				} else {
					apic_intr_redistribute();
				}
			} else
				apic_skipped_redistribute++;
		}
	}
}


/*
 * The following functions are in the platform specific file so that they
 * can be different functions depending on whether we are running on
 * bare metal or a hypervisor.
 */

/*
 * map an apic for memory-mapped access
 */
uint32_t *
mapin_apic(uint32_t addr, size_t len, int flags)
{
	/*LINTED: pointer cast may result in improper alignment */
	return ((uint32_t *)psm_map_phys(addr, len, flags));
}

uint32_t *
mapin_ioapic(uint32_t addr, size_t len, int flags)
{
	return (mapin_apic(addr, len, flags));
}

/*
 * unmap an apic
 */
void
mapout_apic(caddr_t addr, size_t len)
{
	psm_unmap_phys(addr, len);
}

void
mapout_ioapic(caddr_t addr, size_t len)
{
	mapout_apic(addr, len);
}

/*
 * Check to make sure there are enough irq slots
 */
int
apic_check_free_irqs(int count)
{
	int i, avail;

	avail = 0;
	for (i = APIC_FIRST_FREE_IRQ; i < APIC_RESV_IRQ; i++) {
		if ((apic_irq_table[i] == NULL) ||
		    apic_irq_table[i]->airq_mps_intr_index == FREE_INDEX) {
			if (++avail >= count)
				return (PSM_SUCCESS);
		}
	}
	return (PSM_FAILURE);
}

/*
 * This function allocates "count" MSI vector(s) for the given "dip/pri/type"
 */
int
apic_alloc_msi_vectors(dev_info_t *dip, int inum, int count, int pri,
    int behavior)
{
	int	rcount, i;
	uchar_t	start, irqno;
	uint32_t cpu;
	major_t	major;
	apic_irq_t	*irqptr;

	DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msi_vectors: dip=0x%p "
	    "inum=0x%x  pri=0x%x count=0x%x behavior=%d\n",
	    (void *)dip, inum, pri, count, behavior));

	if (count > 1) {
		if (behavior == DDI_INTR_ALLOC_STRICT &&
		    apic_multi_msi_enable == 0)
			return (0);
		if (apic_multi_msi_enable == 0)
			count = 1;
	}

	if ((rcount = apic_navail_vector(dip, pri)) > count)
		rcount = count;
	else if (rcount == 0 || (rcount < count &&
	    behavior == DDI_INTR_ALLOC_STRICT))
		return (0);

	/* if not ISP2, then round it down */
	if (!ISP2(rcount))
		rcount = 1 << (highbit(rcount) - 1);

	mutex_enter(&airq_mutex);

	for (start = 0; rcount > 0; rcount >>= 1) {
		if ((start = apic_find_multi_vectors(pri, rcount)) != 0 ||
		    behavior == DDI_INTR_ALLOC_STRICT)
			break;
	}

	if (start == 0) {
		/* no vector available */
		mutex_exit(&airq_mutex);
		return (0);
	}

	if (apic_check_free_irqs(rcount) == PSM_FAILURE) {
		/* not enough free irq slots available */
		mutex_exit(&airq_mutex);
		return (0);
	}

	major = (dip != NULL) ? ddi_driver_major(dip) : 0;
	for (i = 0; i < rcount; i++) {
		if ((irqno = apic_allocate_irq(apic_first_avail_irq)) ==
		    (uchar_t)-1) {
			/*
			 * shouldn't happen because of the
			 * apic_check_free_irqs() check earlier
			 */
			mutex_exit(&airq_mutex);
			DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msi_vectors: "
			    "apic_allocate_irq failed\n"));
			return (i);
		}
		apic_max_device_irq = max(irqno, apic_max_device_irq);
		apic_min_device_irq = min(irqno, apic_min_device_irq);
		irqptr = apic_irq_table[irqno];
#ifdef	DEBUG
		if (apic_vector_to_irq[start + i] != APIC_RESV_IRQ)
			DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msi_vectors: "
			    "apic_vector_to_irq is not APIC_RESV_IRQ\n"));
#endif
		apic_vector_to_irq[start + i] = (uchar_t)irqno;

		irqptr->airq_vector = (uchar_t)(start + i);
		irqptr->airq_ioapicindex = (uchar_t)inum;	/* start */
		irqptr->airq_intin_no = (uchar_t)rcount;
		irqptr->airq_ipl = pri;
		irqptr->airq_vector = start + i;
		irqptr->airq_origirq = (uchar_t)(inum + i);
		irqptr->airq_share_id = 0;
		irqptr->airq_mps_intr_index = MSI_INDEX;
		irqptr->airq_dip = dip;
		irqptr->airq_major = major;
		if (i == 0) /* they all bound to the same cpu */
			cpu = irqptr->airq_cpu = apic_bind_intr(dip, irqno,
			    0xff, 0xff);
		else
			irqptr->airq_cpu = cpu;
		DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msi_vectors: irq=0x%x "
		    "dip=0x%p vector=0x%x origirq=0x%x pri=0x%x\n", irqno,
		    (void *)irqptr->airq_dip, irqptr->airq_vector,
		    irqptr->airq_origirq, pri));
	}
	mutex_exit(&airq_mutex);
	return (rcount);
}

/*
 * This function allocates "count" MSI-X vector(s) for the given "dip/pri/type"
 */
int
apic_alloc_msix_vectors(dev_info_t *dip, int inum, int count, int pri,
    int behavior)
{
	int	rcount, i;
	major_t	major;

	mutex_enter(&airq_mutex);

	if ((rcount = apic_navail_vector(dip, pri)) > count)
		rcount = count;
	else if (rcount == 0 || (rcount < count &&
	    behavior == DDI_INTR_ALLOC_STRICT)) {
		rcount = 0;
		goto out;
	}

	if (apic_check_free_irqs(rcount) == PSM_FAILURE) {
		/* not enough free irq slots available */
		rcount = 0;
		goto out;
	}

	major = (dip != NULL) ? ddi_driver_major(dip) : 0;
	for (i = 0; i < rcount; i++) {
		uchar_t	vector, irqno;
		apic_irq_t	*irqptr;

		if ((irqno = apic_allocate_irq(apic_first_avail_irq)) ==
		    (uchar_t)-1) {
			/*
			 * shouldn't happen because of the
			 * apic_check_free_irqs() check earlier
			 */
			DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msix_vectors: "
			    "apic_allocate_irq failed\n"));
			rcount = i;
			goto out;
		}
		if ((vector = apic_allocate_vector(pri, irqno, 1)) == 0) {
			/*
			 * shouldn't happen because of the
			 * apic_navail_vector() call earlier
			 */
			DDI_INTR_IMPLDBG((CE_CONT, "apic_alloc_msix_vectors: "
			    "apic_allocate_vector failed\n"));
			rcount = i;
			goto out;
		}
		apic_max_device_irq = max(irqno, apic_max_device_irq);
		apic_min_device_irq = min(irqno, apic_min_device_irq);
		irqptr = apic_irq_table[irqno];
		irqptr->airq_vector = (uchar_t)vector;
		irqptr->airq_ipl = pri;
		irqptr->airq_origirq = (uchar_t)(inum + i);
		irqptr->airq_share_id = 0;
		irqptr->airq_mps_intr_index = MSIX_INDEX;
		irqptr->airq_dip = dip;
		irqptr->airq_major = major;
		irqptr->airq_cpu = apic_bind_intr(dip, irqno, 0xff, 0xff);
	}
out:
	mutex_exit(&airq_mutex);
	return (rcount);
}

/*
 * Allocate a free vector for irq at ipl. Takes care of merging of multiple
 * IPLs into a single APIC level as well as stretching some IPLs onto multiple
 * levels. APIC_HI_PRI_VECTS interrupts are reserved for high priority
 * requests and allocated only when pri is set.
 */
uchar_t
apic_allocate_vector(int ipl, int irq, int pri)
{
	int	lowest, highest, i;

	highest = apic_ipltopri[ipl] + APIC_VECTOR_MASK;
	lowest = apic_ipltopri[ipl - 1] + APIC_VECTOR_PER_IPL;

	if (highest < lowest) /* Both ipl and ipl - 1 map to same pri */
		lowest -= APIC_VECTOR_PER_IPL;

#ifdef	DEBUG
	if (apic_restrict_vector)	/* for testing shared interrupt logic */
		highest = lowest + apic_restrict_vector + APIC_HI_PRI_VECTS;
#endif /* DEBUG */
	if (pri == 0)
		highest -= APIC_HI_PRI_VECTS;

	for (i = lowest; i <= highest; i++) {
		if (APIC_CHECK_RESERVE_VECTORS(i))
			continue;
		if (apic_vector_to_irq[i] == APIC_RESV_IRQ) {
			apic_vector_to_irq[i] = (uchar_t)irq;
			return (i);
		}
	}

	return (0);
}

/* Mark vector as not being used by any irq */
void
apic_free_vector(uchar_t vector)
{
	apic_vector_to_irq[vector] = APIC_RESV_IRQ;
}

uint32_t
ioapic_read(int ioapic_ix, uint32_t reg)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_REG] = reg;
	return (ioapic[APIC_IO_DATA]);
}

void
ioapic_write(int ioapic_ix, uint32_t reg, uint32_t value)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_REG] = reg;
	ioapic[APIC_IO_DATA] = value;
}

void
ioapic_write_eoi(int ioapic_ix, uint32_t value)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_EOI] = value;
}

static processorid_t
apic_find_cpu(int flag)
{
	processorid_t acid = 0;
	int i;

	/* Find the first CPU with the passed-in flag set */
	for (i = 0; i < apic_nproc; i++) {
		if (apic_cpus[i].aci_status & flag) {
			acid = i;
			break;
		}
	}

	ASSERT((apic_cpus[acid].aci_status & flag) != 0);
	return (acid);
}

/*
 * Call rebind to do the actual programming.
 * Must be called with interrupts disabled and apic_ioapic_lock held
 * 'p' is polymorphic -- if this function is called to process a deferred
 * reprogramming, p is of type 'struct ioapic_reprogram_data *', from which
 * the irq pointer is retrieved.  If not doing deferred reprogramming,
 * p is of the type 'apic_irq_t *'.
 *
 * apic_ioapic_lock must be held across this call, as it protects apic_rebind
 * and it protects apic_find_cpu() from a race in which a CPU can be taken
 * offline after a cpu is selected, but before apic_rebind is called to
 * bind interrupts to it.
 */
int
apic_setup_io_intr(void *p, int irq, boolean_t deferred)
{
	apic_irq_t *irqptr;
	struct ioapic_reprogram_data *drep = NULL;
	int rv;

	if (deferred) {
		drep = (struct ioapic_reprogram_data *)p;
		ASSERT(drep != NULL);
		irqptr = drep->irqp;
	} else
		irqptr = (apic_irq_t *)p;

	ASSERT(irqptr != NULL);

	rv = apic_rebind(irqptr, apic_irq_table[irq]->airq_cpu, drep);
	if (rv) {
		/*
		 * CPU is not up or interrupts are disabled. Fall back to
		 * the first available CPU
		 */
		rv = apic_rebind(irqptr, apic_find_cpu(APIC_CPU_INTR_ENABLE),
		    drep);
	}

	return (rv);
}


uchar_t
apic_modify_vector(uchar_t vector, int irq)
{
	apic_vector_to_irq[vector] = (uchar_t)irq;
	return (vector);
}

char *
apic_get_apic_type()
{
	return (apic_psm_info.p_mach_idstring);
}

void
x2apic_update_psm()
{
	struct psm_ops *pops = &apic_ops;

	ASSERT(pops != NULL);

	/*
	 * We don't need to do any magic if one of the following
	 * conditions is true :
	 * - Not being run under kernel debugger.
	 * - MP is not set.
	 * - Booted with one CPU only.
	 * - One CPU configured.
	 *
	 * We set apic_common_send_ipi() since kernel debuggers
	 * attempt to send IPIs to other slave CPUs during
	 * entry (exit) from (to) debugger.
	 */
	if (!(boothowto & RB_DEBUG) || use_mp == 0 ||
	    apic_nproc == 1 || boot_ncpus == 1) {
		pops->psm_send_ipi =  x2apic_send_ipi;
	} else {
		pops->psm_send_ipi =  apic_common_send_ipi;
	}

	pops->psm_intr_exit = x2apic_intr_exit;
	pops->psm_setspl = x2apic_setspl;

	send_dirintf = pops->psm_send_ipi;

	apic_mode = LOCAL_X2APIC;
	apic_change_ops();
}

static void
apic_intrmap_init(int apic_mode)
{
	int suppress_brdcst_eoi = 0;

	if (psm_vt_ops != NULL) {
		/*
		 * Since X2APIC requires the use of interrupt remapping
		 * (though this is not documented explicitly in the Intel
		 * documentation (yet)), initialize interrupt remapping
		 * support before initializing the X2APIC unit.
		 */
		if (((apic_intrmap_ops_t *)psm_vt_ops)->
		    apic_intrmap_init(apic_mode) == DDI_SUCCESS) {

			apic_vt_ops = psm_vt_ops;

			/*
			 * We leverage the interrupt remapping engine to
			 * suppress broadcast EOI; thus we must send the
			 * directed EOI with the directed-EOI handler.
			 */
			if (apic_directed_EOI_supported() == 0) {
				suppress_brdcst_eoi = 1;
			}

			apic_vt_ops->apic_intrmap_enable(suppress_brdcst_eoi);

			if (apic_detect_x2apic()) {
				apic_enable_x2apic();
			}

			if (apic_directed_EOI_supported() == 0) {
				apic_set_directed_EOI_handler();
			}
		}
	}
}

/*ARGSUSED*/
static void
apic_record_ioapic_rdt(apic_irq_t *irq_ptr, ioapic_rdt_t *irdt)
{
	irdt->ir_hi <<= APIC_ID_BIT_OFFSET;
}

/*ARGSUSED*/
static void
apic_record_msi(apic_irq_t *irq_ptr, msi_regs_t *mregs)
{
	mregs->mr_addr = MSI_ADDR_HDR |
	    (MSI_ADDR_RH_FIXED << MSI_ADDR_RH_SHIFT) |
	    (MSI_ADDR_DM_PHYSICAL << MSI_ADDR_DM_SHIFT) |
	    (mregs->mr_addr << MSI_ADDR_DEST_SHIFT);
	mregs->mr_data = (MSI_DATA_TM_EDGE << MSI_DATA_TM_SHIFT) |
	    mregs->mr_data;
}
