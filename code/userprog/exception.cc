// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "machine.h"
#include "system.h"
#include "syscall.h"

void SyscallEnd()
{
	int pc = machine->ReadRegister(PCReg);
	machine->WriteRegister(PrevPCReg, pc);
	pc = machine->ReadRegister(NextPCReg);
	machine->WriteRegister(PCReg, pc);
	pc += 4;
	machine->WriteRegister(NextPCReg, pc);
	return;
}

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------
#define LRU
void ExceptionHandler(ExceptionType which)
{
	int type = machine->ReadRegister(2);
	int i;
	unsigned int vpn, offset;

	if (which == SyscallException)
	{
		switch (type)
		{
		case SC_Halt:
		{
			DEBUG('a', "Shutdown, initiated by user program.\n");
			interrupt->Halt();
			break;
		}
		case SC_Exit:
		{
			int retVal = machine->ReadRegister(4);
			DEBUG('a', "Exit call\n");
			printf("Exit code %d\n", retVal);
#ifdef USE_TLB
#ifdef LRU
			printf("LRU:\n");
#else
			printf("FIFO:\n");
#endif
			printf("miss number:%d, total number:%d\n", missCnt, memCnt);
			printf("miss rate:%f\n", float(missCnt) / memCnt);
#endif
			PrintThreadStates();
			currentThread->Finish();
			break;
		}
		default:;
		}
		SyscallEnd();
	}
	/* lab4 begin */
	else if (which == PageFaultException)
	{
		int badVAddr = machine->registers[BadVAddrReg];
		vpn = (unsigned)badVAddr / PageSize;
		// TLB
		if (machine->tlb != NULL)
		{
			for (i = 0; i < TLBSize; ++i)
			{
				if (machine->tlb[i].valid == false)
				{
					machine->tlb[i].valid = true;
					machine->tlb[i].interval = 0;
					if (i == 0)
						machine->tlb[0].replace = true;
					machine->tlb[i].virtualPage = machine->tlb[i].physicalPage = vpn;
					break;
				}
			}
			if (i == TLBSize)
			{
#ifdef LRU
				int LRUid, max_intv = -1;
				for (i = 0; i < TLBSize; ++i)
				{
					if (machine->tlb[i].interval > max_intv)
					{
						LRUid = i;
						max_intv = machine->tlb[i].interval;
					}
				}
				machine->tlb[LRUid].interval = 0;
				machine->tlb[LRUid].virtualPage = machine->tlb[LRUid].physicalPage = vpn;
#else
				int FIFOid;
				for (i = 0; i < TLBSize; ++i)
				{
					if (machine->tlb[i].replace)
					{
						FIFOid = i;
						machine->tlb[i].replace = false;
						machine->tlb[(i + 1) % TLBSize].replace = true;
						break;
					}
				}
				machine->tlb[FIFOid].virtualPage = machine->tlb[FIFOid].physicalPage = vpn;
#endif
			}
		}
		// Inverted Page Table
		else
		{
			TranslationEntry *ptable = machine->pageTable;
			int LRUid, ppn, max_intv = -1;
			int npages = NumPhysPages;
			int old_tid;
			if ((ppn = machine->memMap->Find()) == -1)
			{
				int poffset, voffset;
				for (int i = 0; i < npages; ++i)
				{
					if (ptable[i].valid && (ptable[i].interval > max_intv))
					{
						max_intv = ptable[i].interval;
						LRUid = i;
					}
				}
				old_tid = ptable[LRUid].tid;
				printf("New vpn %d of thread %d \"%s\" replace vpn %d of thread %d \"%s\" in ppn %d\n",
					   vpn, currentThread->getTid(), currentThread->getName(), ptable[LRUid].virtualPage,
					   old_tid, tInfo[old_tid].name, LRUid);
				if (ptable[LRUid].dirty)
				{
					poffset = ptable[LRUid].physicalPage * PageSize;
					voffset = ptable[LRUid].virtualPage * PageSize;
					tInfo[old_tid].threadPointer->space->swapFile->WriteAt(&(machine->mainMemory[poffset]), PageSize, voffset);
				}
				ptable[LRUid].valid = FALSE;
				ppn = LRUid;
			}
			printf("Place vpn %d in ppn %d\n", vpn, ppn);
			ptable[ppn].virtualPage = vpn;
			ptable[ppn].physicalPage = ppn;
			ptable[ppn].dirty = FALSE;
			ptable[ppn].valid = TRUE;
			ptable[ppn].use = TRUE;
			ptable[ppn].interval = 0;
			ptable[ppn].tid = currentThread->getTid();
			currentThread->space->swapFile->ReadAt(&(machine->mainMemory[ppn * PageSize]), PageSize, vpn * PageSize);
		}
	}
	/* lab4 end */
	else
	{
		printf("Unexpected user mode exception %d %d\n", which, type);
		ASSERT(FALSE);
	}
}
