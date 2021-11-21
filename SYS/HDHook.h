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
	// SSDT的基地址
	unsigned int *ServiceTableBase;
	// 此域用于操作系统的checked builds，包含着SSDT中每个服务被调用次数的计数器。这个计数器由INT 2Eh处理程序KiSystemService更新
	// 仅适用于checked build版本
	unsigned int *ServiceCounterTableBase;
	// 由ServiceTableBase描述的服务的数目
	unsigned int NumberOfServices;
	// 包含每个系统服务参数字节数表的基地址
	unsigned char *ParamTableBase;
} ServiceDescriptorTableEntry_t;
#pragma pack()

// Pointer to the image of the system service table
__declspec(dllimport) ServiceDescriptorTableEntry_t KeServiceDescriptorTable;

PMDL	g_pmdlSystemCall;
PVOID	*MappedSystemCallTable;

// 获得函数在SSDT中的索引宏
#define SYSCALL_INDEX(_Function)			*(PULONG)((PUCHAR)_Function+1)
// 调换自己的hook函数与原系统函数的地址
#define HOOK_SYSCALL(_Function, _Hook)		(PVOID)InterlockedExchange( (PLONG) &MappedSystemCallTable[SYSCALL_INDEX(_Function)], (LONG) _Hook )
// 卸载hook函数
#define UNHOOK_SYSCALL(_Function, _Hook)	InterlockedExchange( (PLONG) &MappedSystemCallTable[SYSCALL_INDEX(_Function)], (LONG) _Hook )
// 获得SSDT基址宏
#define SYSTEMSERVICE(_Function)			KeServiceDescriptorTable.ServiceTableBase[*(PULONG)((PUCHAR)_Function+1)]

#define IOCTL_TRANSFER_TYPE(_iocontrol)	(_iocontrol & 0x3)
#define SMART_RCV_DRIVE_DATA	0x0007C088	//ATA接口的硬盘
#define IOCTL_SCSI_MINIPORT		0x0004D008	//SCSI接口的硬盘
#define INVALID_HANDLE_VALUE	((HANDLE) -1)

// Convenient mutex macros
#define MUTEX_INIT(v)		KeInitializeMutex( &v, 0 )
#define MUTEX_WAIT(v)		KeWaitForMutexObject( &v, Executive, KernelMode, FALSE, NULL )
#define MUTEX_RELEASE(v)	KeReleaseMutex( &v, FALSE )
