#include <ntifs.h>

//#define KRNL_DBG //uncomment to break in user-mode

typedef enum _KAPC_ENVIRONMENT
{
	OriginalApcEnvironment,
	AttachedApcEnvironment,
	CurrentApcEnvironment,
	InsertApcEnvironment
}KAPC_ENVIRONMENT, *PKAPC_ENVIRONMENT;


extern void KeInitializeApc(
	PRKAPC Apc,
	PRKTHREAD Thread,
	UINT32 Environment,
	PVOID KernelRoutine,
	PVOID RundownRoutine,
	PVOID NormalRoutine,
	KPROCESSOR_MODE ProcessorMode,
	PVOID NormalContext
	);

extern BOOLEAN KeInsertQueueApc(
	PRKAPC Apc,
	PVOID SystemArgument1,
	PVOID SystemArgument2,
	KPRIORITY Increment);

extern UCHAR* PsGetProcessImageFileName();

VOID Apc_KernelRoutine(PRKAPC Apc, PVOID *NormalRoutine, PVOID *NormalContext, PVOID *SystemArgument1, PVOID *SystemArgument2);
VOID KernelApc_NormalRoutine(PVOID NormalContext, PVOID SystemArgument1, PVOID SystemArgument2);
VOID CreateThreadNotifyRoutine(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create);

__declspec(align(8)) KMUTEX UserApcMutex;
UINT8 UserApcQueued; //Only queue a single user-mode APC
LONG nAPC;

VOID Apc_KernelRoutine(PRKAPC Apc, PVOID *NormalRoutine, PVOID *NormalContext, PVOID *SystemArgument1, PVOID *SystemArgument2)
{
	if ( Apc->ApcMode == KernelMode )
		InterlockedAdd(&nAPC, 1);
	ExFreePool(Apc);
}

VOID KernelApc_NormalRoutine(PVOID NormalContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
	PKAPC Apc;
	LARGE_INTEGER Timeout;
	PVOID umAddress = NULL;
	SIZE_T Size = PAGE_SIZE;
	UCHAR shellcode[] = {	0x90,0x90,0x90,
#ifdef KRNL_DBG	
				0xCC,
#endif
				0xC3
			    };

	Timeout.QuadPart = -500000;
	if (KeWaitForMutexObject(&UserApcMutex, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
	{
		InterlockedAdd(&nAPC, -1);
		return; //Release the thread
	}

	if (UserApcQueued)
		goto cleanup;

	/*
	Allocate executable memory in user space and copy the shellcode
	*/
	if (!NT_SUCCESS(ZwAllocateVirtualMemory(ZwCurrentProcess(), &umAddress, 0, &Size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
	{
		goto cleanup;
	}
	RtlZeroMemory(umAddress, Size);
	RtlCopyMemory(umAddress, shellcode, sizeof(shellcode));

	/*
	Queue the user-mode APC to our thread
	*/
	Apc = ExAllocatePool(NonPagedPool, sizeof(KAPC));
	KeInitializeApc(Apc, PsGetCurrentThread(), CurrentApcEnvironment, Apc_KernelRoutine, NULL, umAddress, UserMode, NULL);
	if ( KeInsertQueueApc(Apc, NULL, NULL, 0) )
	{
		PsRemoveCreateThreadNotifyRoutine(CreateThreadNotifyRoutine);
		UserApcQueued = 1;
	}
	else
	{
		//Apc->Inserted == FALSE
		ExFreePool(Apc);
	}
cleanup :
	KeReleaseMutex(&UserApcMutex, FALSE);
	InterlockedAdd(&nAPC, -1);
}

VOID CreateThreadNotifyRoutine(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create)
{
	PETHREAD Thread;
	PKAPC Apc;
	
	if ( !Create )
		return;

	if (!NT_SUCCESS(PsLookupThreadByThreadId(ThreadId, &Thread)))
		return;

	if (PsIsSystemThread(Thread))
		goto cleanup;
	/*
	Check against a process to queue the APC to or whatever here...
	This driver queues a user-mode APC to an arbitrary thread (the first one it encounters).
	
	PEPROCESS Process;
	UCHAR* ProcessFileName;
	Process = IoThreadToProcess(Thread);
	ProcessFileName = PsGetProcessImageFileName(Process);
	if (!strstr(ProcessFileName, "winlogon"))
		return;
	...
	*/

	/*
	Queue a kernel-mode APC to be delivered in the context of the new thread
	*/
	Apc = ExAllocatePool(NonPagedPool, sizeof(KAPC));
	KeInitializeApc(Apc, Thread, OriginalApcEnvironment, Apc_KernelRoutine, NULL, KernelApc_NormalRoutine, KernelMode, NULL);
	if (!KeInsertQueueApc(Apc, NULL, NULL, 0))
		ExFreePool(Apc);
	
cleanup:
	ObDereferenceObject(Thread);
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	//DEBUG
	LARGE_INTEGER Interval;
	Interval.QuadPart = -500000;
	while ( !UserApcQueued || InterlockedAdd(&nAPC, 0) )
	{
		KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	}
	KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	KeInitializeMutex(&UserApcMutex, 0);
	DriverObject->DriverUnload = DriverUnload;
	PsSetCreateThreadNotifyRoutine(CreateThreadNotifyRoutine);
	return STATUS_SUCCESS;
}
