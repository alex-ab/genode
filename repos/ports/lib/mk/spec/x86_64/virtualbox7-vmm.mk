include $(REP_DIR)/lib/mk/virtualbox7-common.inc

SRC_CC += VMM/VMMR3/VMR3.cpp
SRC_CC += VMM/VMMAll/VMAll.cpp
SRC_CC += VMM/VMMAll/VMMAll.cpp
SRC_CC += VMM/VMMR3/VMMR3.cpp
SRC_CC += VMM/VMMR3/GVMMR3.cpp

SRC_CC += VMM/VMMAll/GCMAll.cpp
SRC_CC += VMM/VMMR3/GCMR3.cpp

SRC_CC += VMM/VMMR3/STAMR3.cpp

SRC_CC += VMM/VMMR3/SSMR3.cpp

SRC_CC += VMM/VMMR3/NEMR3.cpp
SRC_CC += VMM/VMMAll/NEMAll.cpp

SRC_CC += VMM/VMMR3/VMMR3VTable.cpp

SRC_CC += VMM/VMMR3/PDMR3.cpp
SRC_CC += VMM/VMMR3/PDMR3AsyncCompletion.cpp
SRC_CC += VMM/VMMR3/PDMR3AsyncCompletionFile.cpp
SRC_CC += VMM/VMMR3/PDMR3AsyncCompletionFileFailsafe.cpp
SRC_CC += VMM/VMMR3/PDMR3AsyncCompletionFileNormal.cpp
SRC_CC += VMM/VMMR3/PDMR3BlkCache.cpp
SRC_CC += VMM/VMMR3/PDMR3CritSect.cpp
SRC_CC += VMM/VMMR3/PDMR3DevHlp.cpp
SRC_CC += VMM/VMMR3/PDMR3DevMiscHlp.cpp
SRC_CC += VMM/VMMR3/PDMR3Device.cpp
SRC_CC += VMM/VMMR3/PDMR3Driver.cpp
SRC_CC += VMM/VMMR3/PDMR3NetShaper.cpp
SRC_CC += VMM/VMMR3/PDMR3Queue.cpp
SRC_CC += VMM/VMMR3/PDMR3Task.cpp
SRC_CC += VMM/VMMR3/PDMR3Thread.cpp
SRC_CC += VMM/VMMR3/PDMR3Usb.cpp

SRC_CC += VMM/VMMAll/PDMAll.cpp
SRC_CC += VMM/VMMAll/PDMAllQueue.cpp
SRC_CC += VMM/VMMAll/PDMAllCritSect.cpp
SRC_CC += VMM/VMMAll/PDMAllCritSectRw.cpp
SRC_CC += VMM/VMMAll/PDMAllTask.cpp

SRC_CC += VMM/VMMR3/TMR3.cpp
SRC_CC += VMM/VMMAll/TMAll.cpp
SRC_CC += VMM/VMMAll/TMAllVirtual.cpp
SRC_CC += VMM/VMMAll/TMAllReal.cpp
SRC_CC += VMM/VMMAll/TMAllCpu.cpp
SRC_CC += VMM/VMMAll/TRPMAll.cpp

SRC_CC += VMM/VMMR3/CFGMR3.cpp

SRC_CC += VMM/VMMAll/CPUMAllCpuId.cpp
SRC_CC += VMM/VMMAll/CPUMAllMsrs.cpp
SRC_CC += VMM/VMMAll/CPUMAllRegs.cpp

SRC_CC += VMM/VMMR3/VMR3Emt.cpp
SRC_CC += VMM/VMMR3/VMR3Req.cpp

SRC_CC += VMM/VMMAll/DBGFAll.cpp
SRC_CC += VMM/VMMR3/DBGFR3Cpu.cpp
SRC_CC += VMM/VMMR3/DBGFR3Info.cpp
SRC_CC += VMM/VMMR3/DBGFR3OS.cpp
SRC_CC += VMM/VMMR3/DBGFR3PlugIn.cpp
SRC_CC += VMM/VMMR3/DBGFR3BugCheck.cpp
SRC_CC += VMM/VMMR3/DBGFR3Flow.cpp
SRC_CC += VMM/VMMR3/DBGFR3FlowTrace.cpp
SRC_CC += VMM/VMMR3/DBGFR3SampleReport.cpp
SRC_CC += VMM/VMMR3/DBGFR3Type.cpp

SRC_CC += VMM/VMMR3/CPUMR3.cpp
SRC_CC += VMM/VMMR3/CPUMR3CpuIdInfo.cpp
SRC_CC += VMM/VMMR3/CPUMR3CpuIdInfo-x86.cpp
SRC_CC += VMM/VMMR3/CPUMR3Db.cpp
SRC_CC += VMM/VMMR3/CPUMR3Host-x86.cpp

SRC_CC += VMM/VMMR3/target-x86/APICR3-x86.cpp
SRC_CC += VMM/VMMR3/target-x86/HMR3-x86.cpp

SRC_CC += VMM/VMMR3/target-x86/CPUMR3CpuId-x86.cpp
SRC_CC += VMM/VMMR3/target-x86/CPUMR3-x86.cpp
SRC_CC += VMM/VMMR3/target-x86/CPUMR3Msr-x86.cpp

SRC_CC += VMM/VMMAll/EMAll.cpp
SRC_CC += VMM/VMMR3/EMR3.cpp
SRC_CC += VMM/VMMR3/EMR3HM.cpp
SRC_CC += VMM/VMMR3/EMR3Nem.cpp

SRC_CC += VMM/VMMR3/TRPMR3.cpp

SRC_CC += VMM/VMMR3/VMMR3GuruMeditation.cpp

SRC_CC += VMM/VMMAll/IEMAll.cpp
SRC_CC += VMM/VMMAll/IEMAllMem.cpp
SRC_CC += VMM/VMMAll/IEMAllTlb.cpp
SRC_CC += VMM/VMMR3/IEMR3.cpp

SRC_CC += VMM/VMMAll/target-x86/APICAll-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/HMAll-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/HMAllSVM-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/HMAllVMX-x86.cpp

SRC_CC += VMM/VMMAll/target-x86/IEMAll-x86.cpp
SRC_S  += VMM/VMMAll/target-x86/IEMAllAImpl-x86-amd64.asm
SRC_CC += VMM/VMMAll/target-x86/IEMAllAImplC-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllCImpl-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllCImplSvmInstr-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllCImplVmxInstr-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllHlpFpu-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllDbg-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllMem-x86.cp
SRC_CC += VMM/VMMAll/target-x86/IEMAllOpcodeFetch-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllOpHlp-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllXcpt-x86.cpp

SRC_CC += VMM/VMMAll/target-x86/IEMAllIntprTables1-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllIntprTables2-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllIntprTables3-x86.cpp
SRC_CC += VMM/VMMAll/target-x86/IEMAllIntprTables4-x86.cpp

SRC_CC += VMM/VMMAll/target-x86/PDMAllApic-x86.cpp

SRC_CC += VMM/VMMAll/target-x86/PGMAllPool-x86.cpp

SRC_CC += VMM/VMMAll/target-x86/SELMAll-x86.cpp

SRC_CC += VMM/VMMAll/GIMAll.cpp
SRC_CC += VMM/VMMAll/GIMAllHv.cpp
SRC_CC += VMM/VMMAll/GIMAllKvm.cpp
SRC_CC += VMM/VMMR3/GIMR3.cpp
SRC_CC += VMM/VMMR3/GIMR3Hv.cpp
SRC_CC += VMM/VMMR3/GIMR3Kvm.cpp
SRC_CC += VMM/VMMR3/GIMR3Minimal.cpp

SRC_CC += VMM/VMMR3/GMMR3.cpp

SRC_CC += VMM/VMMR3/PGMR3.cpp
SRC_CC += VMM/VMMR3/PGMR3Dbg.cpp
SRC_CC += VMM/VMMR3/PGMR3Handler.cpp
SRC_CC += VMM/VMMR3/PGMR3Phys.cpp
SRC_S  += VMM/VMMR3/PGMR3DbgA.asm
SRC_CC += VMM/VMMR3/target-x86/PGMR3Pool-x86.cpp
SRC_CC += VMM/VMMAll/PGMAll.cpp
SRC_CC += VMM/VMMAll/PGMAllHandler.cpp
SRC_CC += VMM/VMMAll/PGMAllPhys.cpp

# C++17 does not allow the use of the 'register' specifier
CC_OPT_VMM/VMMAll/PGMAll = -Dregister=

SRC_CC += VMM/VMMR3/IOMR3.cpp
SRC_CC += VMM/VMMR3/IOMR3IoPort.cpp
SRC_CC += VMM/VMMR3/IOMR3Mmio.cpp
SRC_CC += VMM/VMMAll/IOMAll.cpp
SRC_CC += VMM/VMMAll/IOMAllMmioNew.cpp


SRC_CC += VMM/VMMR3/MMR3.cpp
SRC_CC += VMM/VMMR3/MMR3Heap.cpp
SRC_CC += VMM/VMMAll/MMAll.cpp

CC_OPT += -DVBOX_IN_VMM

# definitions needed by SSMR3.cpp
CC_OPT += -DKBUILD_TYPE=\"debug\" \
          -DKBUILD_TARGET=\"genode\" \
          -DKBUILD_TARGET_ARCH=\"x86\"

# definitions needed by VMMAll.cpp
CC_OPT += -DVBOX_SVN_REV=~0

INC_DIR += $(VBOX_DIR)/VMM
INC_DIR += $(VBOX_DIR)/VMM/include
INC_DIR += $(VIRTUALBOX_DIR)/src/libs/softfloat-3e/source/include

# override conflicting parts of the libc headers
INC_DIR += $(REP_DIR)/src/virtualbox7/include/libc

CC_CXX_WARN_STRICT =
