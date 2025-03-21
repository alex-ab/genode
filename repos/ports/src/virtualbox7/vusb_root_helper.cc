/*
 * \brief  Help to stop OHCI/PDM drivers
 * \author Sebastian Sumpf
 * \date   2026-06-17
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <VBox/types.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>

/* enable PDMDRVINSINT member in PDMDRVINS (pdm.h) */
#define PDMDRVINSINT_DECLARED
typedef struct PDMDRVINSINT
{
	void *_dymmy1;
	void *_dummy2;
	void *_dummy3;
	void *_dummy4;
	PVM   pVM;
} PDMDRVINSINT;

#include <VBox/vmm/pdm.h>


/*
 * Process pending EMT requests during OHCI poweroff in case OHCI is running.
 * ohciR3ServicePeriodicList iterates over all 128 endpoints and ends up calling
 * PGMR3PhysReadExternal trough ohciR3PhysReadCacheRead/ohciR3ReadTd which must
 * be serviced by EMT threads. In case of a system-wide shutdown, EMT-0 has to
 * handle these requests because vusbRhSetFrameProcessing will block in
 * RTSemEventMultiWait (Wait for signal from the thread that it stopped)
 * forever. For this to work we added ohciR3PowerOff to suspend OHCI using
 * vusbRhSetFrameProcessing. Otherwise, thread suspend of the OHCI-frame worker
 * using pdmR3ThreadSuspendAll in PDMR3PowerOff will block forever because of
 * pending PGMR3PhysReadExternal requests in EMT-0.
 *
 * This function is called from vusbRhSetFrameProcessing when frame processing
 * is turned off.
 */
void vusb_request_process(PPDMDRVINS pDrvIns)
{
	PVM pVM = pDrvIns->Internal.s.pVM;

	/* call for all potential 128 EPs */
	for (unsigned i = 0; i < 128; i++) {
		VMR3ReqProcessU(pVM->pUVM, VMCPUID_ANY, true /*fPriorityOnly*/);
		/* always wait for at least 1 ms to let requester execute */
		RTThreadSleep(1);
	}
}
