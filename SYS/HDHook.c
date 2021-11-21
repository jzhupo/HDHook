/******************************************
���ߣ�����
ʱ�䣺2009��11��17��
******************************************/

#include <ntddk.h>
#include <stdarg.h>
#include <stdio.h>
#include "..\GUI\IoctlCmd.h"
#include "HDHook.h"
#include "Filter.h"

/*******
ȫ�ֱ���
*******/
// our user-interface device object
PDEVICE_OBJECT GUIDevice = NULL;
ZWDEVICEIOCONTROLFILE RealZwDeviceIoControlFile;

// ԭʼ�ͷ����Ӳ����Ϣ
DISKINFO __HookOld, __HookNew;
BOOLEAN	IsHooked = FALSE;

/***************
����ϵͳ�ҹ�����
***************/
// Our own routine for ZwDeviceIocontrolFile
// We change the hard disk serial number value requested by user
NTSTATUS HookZwDeviceIoControlFile(IN HANDLE FileHandle,
									IN HANDLE Event OPTIONAL,
									IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
									IN PVOID ApcContext OPTIONAL,
									OUT PIO_STATUS_BLOCK IoStatusBlock,
									IN ULONG IoControlCode,
									IN PVOID InputBuffer OPTIONAL,
									IN ULONG InputBufferLength,
									OUT PVOID OutputBuffer OPTIONAL,
									IN ULONG OutputBufferLength)
{
	NTSTATUS rc;
	BOOLEAN bIOCTL = FALSE;

	// �ж�IoControlcode�ǲ���ȡ���кŵ�ֵ
	if (IoControlCode == SMART_RCV_DRIVE_DATA &&
		(IDE_ATA_IDENTIFY == ((PUCHAR)InputBuffer)[10] || IDE_ATAPI_IDENTIFY == ((PUCHAR)InputBuffer)[10]))
	{
		bIOCTL = TRUE;
	}
	else if (IoControlCode == IOCTL_SCSI_MINIPORT &&
		(IDE_ATA_IDENTIFY == ((PUCHAR)InputBuffer)[38] || IDE_ATAPI_IDENTIFY == ((PUCHAR)InputBuffer)[38]))
	{
		bIOCTL = TRUE;
	}

	rc = RealZwDeviceIoControlFile(FileHandle,
									Event,
									ApcRoutine,
									ApcContext,
									IoStatusBlock,
									IoControlCode,
									InputBuffer,
									InputBufferLength,
									OutputBuffer,
									OutputBufferLength);
	//The driver returns the SENDCMDOUTPARAMS structure and a 512-byte buffer
	//of drive data to the buffer at Irp->AssociatedIrp.SystemBuffer.

	if ((NT_SUCCESS(rc)) && (bIOCTL == TRUE) && (OutputBufferLength >= 528))
	{
		CHAR szBuffer[20];
		// �ж��Ƿ��ڹ��˽����б���
		if (GetProcessName(szBuffer))
		{
			ULONG i;
			PUCHAR Locate;
			KdPrint(("Process %s is in the filter\n", szBuffer));
			// �жϷ���ֵ���Ƿ������ǰ��Ӳ�����кţ�IDEƫ��=36��SCSI=64�����ǵĻ��üٵ��滻
			Locate = BmSubString((PUCHAR)OutputBuffer, (PUCHAR)__HookOld.sSerialNumber,
				OutputBufferLength, DISK_SERIAL_BUFF_LENGTH);
			if (Locate)
			{
				for (i=0; i<DISK_SERIAL_BUFF_LENGTH; i++)
				{
					Locate[i] = __HookNew.sSerialNumber[i];
				}
				KdPrint(("SerialNumber Location= %d/%d\n", Locate-(PUCHAR)OutputBuffer, OutputBufferLength));
				OutputBufferLength -= Locate-(PUCHAR)OutputBuffer+34;
				OutputBuffer = Locate+34;
			}
#if DBG
			else
			{
				KdPrint(("SerialNumber NOT found.\n"));
			}
#endif
			// ģ�ͺţ�IDEƫ��=70��SCSI=98��
			Locate = BmSubString((PUCHAR)OutputBuffer, (PUCHAR)__HookOld.sModelNumber,
				OutputBufferLength, DISK_MODEL_BUFF_LENGTH);
			if (Locate)
			{
				for (i=0; i<DISK_MODEL_BUFF_LENGTH; i++)
				{
					Locate[i] = __HookNew.sModelNumber[i];
				}
				KdPrint(("ModelName Location= %d/%d\n", Locate-(PUCHAR)OutputBuffer, OutputBufferLength));
			}
		}
#if DBG
		else
		{
			KdPrint(("Process %s is NOT in the filter\n", szBuffer));
		}
#endif
	}
/*
	else if (IoControlCode == IOCTL_STORAGE_QUERY_PROPERTY)
	{
		ULONG i;
		PSTORAGE_DEVICE_DESCRIPTOR output = (PSTORAGE_DEVICE_DESCRIPTOR) OutputBuffer;
		if (output->SerialNumberOffset)
		{
			PCHAR SerialNum = (PCHAR)output+output->SerialNumberOffset;
			for (i=0; i<strlen(SerialNum) && i<sizeof(szFakeSerial); i++)
			{
				SerialNum[i] = szFakeSerial[i];
			}
		}
		if (output->ProductIdOffset)
		{
			PCHAR ProductId = (PCHAR)output+output->ProductIdOffset;
			for (i=0; i<strlen(ProductId) && i<sizeof(szFakeProductId); i++)
			{
				ProductId[i] = szFakeProductId[i];
			}
		}
		if (output->VendorIdOffset)
		{
			PCHAR VendorId = (PCHAR)output+output->VendorIdOffset;
			for (i=0; i<strlen(VendorId) && i<sizeof(szFakeVendorId); i++)
			{
				VendorId[i] = szFakeVendorId[i];
			}
		}
	}
*/
	return(rc);
}

// �ù������̵�ָ���滻ϵͳ�������ZwDeviceIoControlFile��ʵ������������ʵ���̵�ַ
VOID HookStart(void)
{
	if (!IsHooked)
	{
		// hook system calls
		RealZwDeviceIoControlFile = HOOK_SYSCALL(ZwDeviceIoControlFile, HookZwDeviceIoControlFile);
		IsHooked = TRUE;
		KdPrint(("Real ZwDeviceIoControlFile Address= 0x%08X\n", RealZwDeviceIoControlFile));
		KdPrint(("Hook ZwDeviceIoControlFile Address= 0x%08X\n", HookZwDeviceIoControlFile));
	}
}

// �ָ�ZwDeviceIoControlFile����ʵ��ַ
VOID HookStop(void)
{
	if (IsHooked)
	{
		// unhook system calls
		UNHOOK_SYSCALL(ZwDeviceIoControlFile, RealZwDeviceIoControlFile);
		IsHooked = FALSE;
	}
}


/***********
�豸��������
***********/
BOOLEAN HDHookDeviceControl(IN PFILE_OBJECT FileObject, IN BOOLEAN Wait,
							IN PVOID InputBuffer, IN ULONG InputBufferLength,
							OUT PVOID OutputBuffer, IN ULONG OutputBufferLength,
							IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus,
							IN PDEVICE_OBJECT DeviceObject)
{
	// Its a message from our GUI!
	IoStatus->Status = STATUS_SUCCESS;	// ����ɹ�
	IoStatus->Information = 0;		// ����û�з���

	switch (IoControlCode)
	{
	case IOCTL_HDHOOK_HOOK://��ʼ��ƭ
		KdPrint(("IoControlCode= HDHOOK_HOOK (0x%X)\n", IoControlCode));
		HookStart();
		break;

	case IOCTL_HDHOOK_UNHOOK://ֹͣ��ƭ
		KdPrint(("IoControlCode= HDHOOK_UNHOOK (0x%X)\n", IoControlCode));
		HookStop();
		break;

	case IOCTL_HDHOOK_SETOLDVALUE://����������ǰ�Լ�Ӳ�̵���ʵ����
		KdPrint(("IoControlCode= HDHOOK_SETOLDVALUE (0x%X)\n", IoControlCode));
		if (InputBufferLength < sizeof(DISKINFO) || InputBuffer == NULL)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}
		__HookOld = *(DISKINFO *)InputBuffer;
		break;

	case IOCTL_HDHOOK_SETNEWVALUE://�����µ�Ӳ����ƭ����
		KdPrint(("IoControlCode= HDHOOK_SETNEWVALUE (0x%X)\n", IoControlCode));
		if (InputBufferLength < sizeof(DISKINFO) || InputBuffer == NULL)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}
		__HookNew = *(DISKINFO *)InputBuffer;
		break;

	case IOCTL_HDHOOK_SETFILTER://�������ù��˽����б�
		KdPrint(("IoControlCode= HDHOOK_SETFILTER (0x%X)\n", IoControlCode));
		if (InputBufferLength < sizeof(FILTER) || InputBuffer == NULL)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}
		FilterDef = *(FILTER *)InputBuffer;
		UpdateFilters();
		break;

	case IOCTL_HDHOOK_GETVERSION://���������İ汾��
		KdPrint(("IoControlCode= HDHOOK_GETVERSION (0x%X)\n", IoControlCode));
		if (OutputBufferLength < sizeof(ULONG) || OutputBuffer == NULL)
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		*(ULONG *)OutputBuffer = HDHOOK_VERSION;
		IoStatus->Information = sizeof(ULONG);
		break;

	default:
		KdPrint(("Unknown IRP_MJ_DEVICE_CONTROL.\n"));
		IoStatus->Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	return TRUE;
}

// In this routine we handle requests to our own device. The only
// requests we care about handling explicitly are IOCTL commands that
// we will get from the GUI. We also expect to get Create and Close
// commands when the GUI opens and closes communications with us.
NTSTATUS HDHookDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	PIO_STACK_LOCATION irpStack;
	PVOID	inputBuffer;
	PVOID	outputBuffer;
	ULONG	inputBufferLength;
	ULONG	outputBufferLength;
	ULONG	ioControlCode;

	// Go ahead and set the request up as successful
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	// Get a pointer to the current location in the Irp. This is where
	// the function codes and parameters are located.
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	// Get the pointer to the input/output buffer and its length
	inputBuffer = Irp->AssociatedIrp.SystemBuffer;
	inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outputBuffer = Irp->AssociatedIrp.SystemBuffer;
	outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

	switch (irpStack->MajorFunction)
	{
	case IRP_MJ_CREATE:
		KdPrint(("IRP_MJ_CREATE\n"));
		break;

	case IRP_MJ_SHUTDOWN:
		KdPrint(("IRP_MJ_SHUTDOWN\n"));
		break;

	case IRP_MJ_CLOSE:
		KdPrint(("IRP_MJ_CLOSE\n"));
		break;

	case IRP_MJ_DEVICE_CONTROL:
		KdPrint(("IRP_MJ_DEVICE_CONTROL\n"));
		// See if the output buffer is really a user buffer that we can just dump data into.
		if (IOCTL_TRANSFER_TYPE(ioControlCode) == METHOD_NEITHER)
		{
			outputBuffer = Irp->UserBuffer;
		}

		// Its a request from the GUI
		HDHookDeviceControl(irpStack->FileObject, TRUE,
							inputBuffer, inputBufferLength,
							outputBuffer, outputBufferLength,
							ioControlCode, &Irp->IoStatus, DeviceObject);
		break;

	default:
		KdPrint(("Unknown MajorFunction= %d\n", irpStack->MajorFunction));
		break;
	}
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

// ����ж������
VOID HDHookUnload(IN PDRIVER_OBJECT DriverObject)
{
	const WCHAR deviceLinkBuffer[] = L"\\DosDevices\\"DRIVER_NAME;
	UNICODE_STRING deviceLinkUnicodeString;
	ULONG i;
	LARGE_INTEGER lDelay;
	PRKTHREAD CurrentThread;

	KdPrint(("Unload Driver begin.\n"));

	// �������
	if (IsHooked)
		HookStop();

	// Unlock and Free MDL
	if (g_pmdlSystemCall)
	{
		MmUnmapLockedPages(MappedSystemCallTable, g_pmdlSystemCall);
		IoFreeMdl(g_pmdlSystemCall);
		g_pmdlSystemCall = NULL;
	}

	// �ͷŶ�̬�ڴ�
	MUTEX_WAIT(FilterMutex);
	for (i=0; i<NumProcessIncludeFilter; i++)
	{
		ExFreePool(ProcessIncludeFilter[i]);
	}
	for (i=0; i<NumProcessExcludeFilter; i++)
	{
		ExFreePool(ProcessExcludeFilter[i]);
	}
	NumProcessIncludeFilter = 0;
	NumProcessExcludeFilter = 0;
	MUTEX_RELEASE(FilterMutex);

	CurrentThread = KeGetCurrentThread();
	// �ѵ�ǰ�߳�����Ϊ��ʵʱģʽ���Ա����������о�����Ӱ����������
	KeSetPriorityThread(CurrentThread, LOW_REALTIME_PRIORITY);

	//bit lame delay - but to prevent BSOD on active hooked system services
	//could improve here with remove locks, but you will never get a 100% correct situation
	lDelay = RtlConvertLongToLargeInteger(-10000 * 5000);
	KeDelayExecutionThread(KernelMode, FALSE, &lDelay);

	// Delete the symbolic link for our device
	RtlInitUnicodeString(&deviceLinkUnicodeString, deviceLinkBuffer);
	IoDeleteSymbolicLink(&deviceLinkUnicodeString);

	// Delete the device object
	IoDeleteDevice(DriverObject->DeviceObject);

	KdPrint(("Unload Driver end.\n"));
}

// Installable driver initialization. Here we just set ourselves up.
NTSTATUS DriverEntry( IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath )
{
	const WCHAR		deviceNameBuffer[] = L"\\Device\\"DRIVER_NAME;
	const WCHAR		deviceLinkBuffer[] = L"\\DosDevices\\"DRIVER_NAME;
	UNICODE_STRING	deviceNameUnicodeString;
	UNICODE_STRING	deviceLinkUnicodeString;
	NTSTATUS		ntStatus;

	KdPrint(("DriverEntry with RegistryPath= %wZ\n", RegistryPath));

	// Setup our name and symbolic link
	RtlInitUnicodeString(&deviceNameUnicodeString, deviceNameBuffer);
	RtlInitUnicodeString(&deviceLinkUnicodeString, deviceLinkBuffer);

	// Set up the device used for GUI communications
	ntStatus = IoCreateDevice(DriverObject, 0, &deviceNameUnicodeString, FILE_DEVICE_HDHOOK, 0, TRUE, &GUIDevice);
	if (!NT_SUCCESS(ntStatus))
	{
		return ntStatus;
	}

	// Create a symbolic link that the GUI can specify to gain access to this driver/device
	ntStatus = IoCreateSymbolicLink(&deviceLinkUnicodeString, &deviceNameUnicodeString);
	if (!NT_SUCCESS(ntStatus))
	{
		if (GUIDevice)
		{
			IoDeleteDevice(GUIDevice);
		}
		return ntStatus;
	}

	// Create dispatch points for all routines that must be handled
	DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] =
	DriverObject->MajorFunction[IRP_MJ_CREATE] =
	DriverObject->MajorFunction[IRP_MJ_CLOSE] =
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HDHookDispatch;
	// Its extremely unsafe to unload a system-call hooker, so this is only enabled in the debug version for testing purposes.
	DriverObject->DriverUnload = HDHookUnload;

	// Initialize mutex
	MUTEX_INIT(FilterMutex);

	// save old system call locations
	//RealZwDeviceIoControlFile = (ZWDEVICEIOCONTROLFILE) (SYSTEMSERVICE(ZwDeviceIoControlFile));
	
	// Map the memory into our domain so we can change the permissions on the MDL
	g_pmdlSystemCall = MmCreateMdl(NULL, KeServiceDescriptorTable.ServiceTableBase, KeServiceDescriptorTable.NumberOfServices*4);
	if (!g_pmdlSystemCall)
	{
		IoDeleteSymbolicLink(&deviceLinkUnicodeString);
		IoDeleteDevice(GUIDevice);
		return STATUS_UNSUCCESSFUL;
	}
	//�Ƿ�ҳ�ڴ�
	MmBuildMdlForNonPagedPool(g_pmdlSystemCall);
	// Change the flags of the MDL
	g_pmdlSystemCall->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
	//����
	MappedSystemCallTable = MmMapLockedPages(g_pmdlSystemCall, KernelMode);
	
	// Find the process name offset
	ProcessNameOffset = GetProcessNameOffset();
	KdPrint(("ProcessNameOffset= %ul\n", ProcessNameOffset));

	return STATUS_SUCCESS;
}
