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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _TOPO_HC_H
#define	_TOPO_HC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Allowable hardware component names for hc FMRIs
 */
#define	BANK		"bank"
#define	BAY		"bay"
#define	BLADE		"blade"
#define	BRANCH		"branch"
#define	CMP		"CMP"
#define	CENTERPLANE	"centerplane"
#define	CHASSIS		"chassis"
#define	CHIP		"chip"
#define	CORE		"core"
#define	STRAND		"strand"
#define	CHIP_SELECT	"chip-select"
#define	CONTROLLER	"controller"
#define	CPU		"cpu"
#define	CPUBOARD	"cpuboard"
#define	DIMM		"dimm"
#define	DISK		"disk"
#define	DRAM		"dram"
#define	DRAMCHANNEL	"dram-channel"
#define	FAN		"fan"
#define	FANMODULE	"fanmodule"
#define	HOSTBRIDGE	"hostbridge"
#define	INTERCONNECT	"interconnect"
#define	IOBOARD		"ioboard"
#define	MEMBOARD	"memboard"
#define	MEMORYBUFFER	"memory-buffer"
#define	MEMORYCONTROL	"memory-controller"
#define	MICROCORE	"micro-core"
#define	MOTHERBOARD	"motherboard"
#define	NIU		"niu"
#define	NIUFN		"niufn"
#define	PCI_BUS		"pcibus"
#define	PCI_DEVICE	"pcidev"
#define	PCI_FUNCTION    "pcifn"
#define	PCIEX_BUS	"pciexbus"
#define	PCIEX_DEVICE	"pciexdev"
#define	PCIEX_FUNCTION  "pciexfn"
#define	PCIEX_ROOT	"pciexrc"
#define	PCIEX_SWUP	"pciexswu"
#define	PCIEX_SWDWN	"pciexswd"
#define	POWERMODULE	"powermodule"
#define	PSU		"psu"
#define	RANK		"rank"
#define	RISER		"riser"
#define	SHELF		"shelf"
#define	SES_ENCLOSURE	"ses-enclosure"
#define	SP		"sp"
#define	SUBCHASSIS	"subchassis"
#define	SYSTEMBOARD	"systemboard"
#define	XAUI		"xaui"
#define	XFP		"xfp"

/*
 * Allowable hc node property group and property names
 */
#define	TOPO_PGROUP_IO		"io"
#define	TOPO_IO_DEVTYPE		"devtype"
#define	TOPO_IO_DRIVER		"driver"
#define	TOPO_IO_MODULE		"module"
#define	TOPO_IO_DEV		"dev"
#define	TOPO_IO_DEVID		"devid"
#define	TOPO_IO_DEV_PATH	"devfs-path"
#define	TOPO_IO_AP_PATH		"ap-path"
#define	TOPO_IO_PHYS_PATH	"phys-path"

#define	TOPO_PGROUP_PCI		"pci"
#define	TOPO_PCI_VENDID		"vendor-id"
#define	TOPO_PCI_DEVID		"device-id"
#define	TOPO_PCI_EXCAP		"extended-capabilities"
#define	TOPO_PCI_BDF		"BDF"
#define	TOPO_PCI_CLASS		"class-code"
#define	TOPO_PCI_AADDR		"assigned-addresses"

#ifdef	__cplusplus
}
#endif

#endif	/* _TOPO_HC_H */
