#include "stdafx.h"
#include "RaceCondition.h"
#include "structs.h"
extern "C" NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath);
NTSTATUS RcIoCtl(PDEVICE_OBJECT DeviceObject,PIRP Irp);
void rcdriverUnload(IN PDRIVER_OBJECT DriverObject);
NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject,PIRP Irp);

DEVICE_OBJECT *DeviceOb;
UNICODE_STRING SymName;

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath)
{
	NTSTATUS status;
	UNICODE_STRING DevName;
	DriverObject->DriverUnload = rcdriverUnload;
	RtlCreateUnicodeString(&DevName,L"\\Device\\RCDEVICE");
	status = IoCreateDevice(DriverObject,0,&DevName,FILE_DEVICE_UNKNOWN,FILE_DEVICE_UNKNOWN,FALSE,&DeviceOb);
	if(!NT_SUCCESS(status))
	{
		//DbgPrint("IoCreateDevice failed with status code %x !\n",status);
		return status;
	}
	RtlCreateUnicodeString(&SymName,L"\\DosDevices\\rcdevice");
	status = IoCreateSymbolicLink(&SymName,&DevName);
	if(!NT_SUCCESS(status))
	{
		//DbgPrint("IoCreateSymbolicLink failed with status code %x !",status);
		IoDeleteDevice(DeviceOb);
		return status;
	}
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RcIoCtl;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose; 
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;

	return STATUS_SUCCESS;
}


void rcdriverUnload(IN PDRIVER_OBJECT DriverObject)
{
	if(DeviceOb)
	{
		IoDeleteSymbolicLink(&SymName);
		IoDeleteDevice(DeviceOb);
	}
}

NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject,PIRP Irp)
{
	return STATUS_SUCCESS;
}

NTSTATUS RcIoCtl(PDEVICE_OBJECT DeviceObject,PIRP Irp)
{
	PIO_STACK_LOCATION sl;
	PUserStruct Input;
	Ustruct* ustruct;
	int num = 0;
	int* Addr;
	sl = IoGetCurrentIrpStackLocation(Irp);
	/* IOCTL_RaceCondition == 0x22e054 */
	if(sl->Parameters.DeviceIoControl.IoControlCode == IOCTL_RaceCondition
		&& sl->Parameters.DeviceIoControl.InputBufferLength == sizeof(UserStruct)
		&& sl->Parameters.DeviceIoControl.OutputBufferLength == 0)
	{
		Input = (PUserStruct)Irp->AssociatedIrp.SystemBuffer;
		if( Input->field1 == 0x1586 && Input->field3 == 0x1844)
		{
			/*Probe the UserStruct's ustruct field for read*/
			__try
			{
				ProbeForRead(Input->ustruct,sizeof(ustruct),__alignof(Ustruct*));
			} 
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				NTSTATUS status = GetExceptionCode();
				Irp->IoStatus.Status = status;
				IoCompleteRequest(Irp,IO_NO_INCREMENT);
				return status;
			}
			ustruct = Input->ustruct;
			/*Probe the Ustruct's UserAddress field for write*/
			__try
			{
				ProbeForWrite(ustruct->UserAddress,sizeof(int*),__alignof(int*)); //First read from user-mode memory
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				NTSTATUS status = GetExceptionCode();
				Irp->IoStatus.Status = status;
				IoCompleteRequest(Irp,IO_NO_INCREMENT);
				return status;
			}
			num += ( Input->field1 ^ Input->field3 ) - ( Input->field4 + 0x7780 ); 
			//The field is read a second time from user-mode memory (ustruct->UserAddress)
			Addr = ustruct->UserAddress;
			/*write the result to the user-mode address*/
			*Addr = num;
		}
	}
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp,IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
