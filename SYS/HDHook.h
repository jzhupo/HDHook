/*******
HdHook.h
*******/

//typedef unsigned int	UINT;
//typedef char			CHAR;
//typedef char *		PCHAR;
//typedef PVOID			POBJECT;

NTSYSAPI NTSTATUS NTAPI ZwDeviceIoControlFile( IN HANDLE FileHandle,
												IN HANDLE Event OPTIONAL,
												IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
												IN PVOID ApcContext OPTIONAL,
												OUT PIO_STATUS_BLOCK IoStatusBlock,
												IN ULONG IoControlCode,
												IN PVOID InputBuffer OPTIONAL,
												IN ULONG InputBufferLength,
												OUT PVOID OutputBuffer OPTIONAL,
												IN ULONG OutputBufferLength );

typedef NTSTATUS (*ZWDEVICEIOCONTROLFILE)( IN HANDLE FileHandle,
											IN HANDLE Event OPTIONAL,
											IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
											IN PVOID ApcContext OPTIONAL,
											OUT PIO_STATUS_BLOCK IoStatusBlock,
											IN ULONG IoControlCode,
											IN PVOID InputBuffer OPTIONAL,
											IN ULONG InputBufferLength,
											OUT PVOID OutputBuffer OPTIONAL,
											IN ULONG OutputBufferLength );

#pragma pack(1)
typedef struct ServiceDescriptorEntry
{
	// SSDT�Ļ���ַ
	unsigned int *ServiceTableBase;
	// �������ڲ���ϵͳ��checked builds��������SSDT��ÿ�����񱻵��ô����ļ������������������INT 2Eh�������KiSystemService����
	// ��������checked build�汾
	unsigned int *ServiceCounterTableBase;
	// ��ServiceTableBase�����ķ������Ŀ
	unsigned int NumberOfServices;
	// ����ÿ��ϵͳ��������ֽ�����Ļ���ַ
	unsigned char *ParamTableBase;
} ServiceDescriptorTableEntry_t;
#pragma pack()

// Pointer to the image of the system service table
__declspec(dllimport) ServiceDescriptorTableEntry_t KeServiceDescriptorTable;

PMDL	g_pmdlSystemCall;
PVOID	*MappedSystemCallTable;

// ��ú�����SSDT�е�������
#define SYSCALL_INDEX(_Function)			*(PULONG)((PUCHAR)_Function+1)
// �����Լ���hook������ԭϵͳ�����ĵ�ַ
#define HOOK_SYSCALL(_Function, _Hook)		(PVOID)InterlockedExchange( (PLONG) &MappedSystemCallTable[SYSCALL_INDEX(_Function)], (LONG) _Hook )
// ж��hook����
#define UNHOOK_SYSCALL(_Function, _Hook)	InterlockedExchange( (PLONG) &MappedSystemCallTable[SYSCALL_INDEX(_Function)], (LONG) _Hook )
// ���SSDT��ַ��
#define SYSTEMSERVICE(_Function)			KeServiceDescriptorTable.ServiceTableBase[*(PULONG)((PUCHAR)_Function+1)]

#define IOCTL_TRANSFER_TYPE(_iocontrol)	(_iocontrol & 0x3)
#define SMART_RCV_DRIVE_DATA	0x0007C088	//ATA�ӿڵ�Ӳ��
#define IOCTL_SCSI_MINIPORT		0x0004D008	//SCSI�ӿڵ�Ӳ��
#define INVALID_HANDLE_VALUE	((HANDLE) -1)

// Convenient mutex macros
#define MUTEX_INIT(v)		KeInitializeMutex( &v, 0 )
#define MUTEX_WAIT(v)		KeWaitForMutexObject( &v, Executive, KernelMode, FALSE, NULL )
#define MUTEX_RELEASE(v)	KeReleaseMutex( &v, FALSE )
