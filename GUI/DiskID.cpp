#include <windows.h>
#include <stdio.h>
#define _WIN32_WINNT 0x0500		// !!!!!!
#include <winioctl.h>
#include "IoctlCmd.h"

#define MAX_IDE_DRIVES	4
#define GETVERSIONOUTPARAMS		GETVERSIONINPARAMS
//#define DFP_GET_VERSION		SMART_GET_VERSION	//0x00074080
//#define DFP_RCV_DRIVE_DATA	SMART_RCV_DRIVE_DATA//0x0007c088

typedef struct _IDSECTOR
{
	USHORT wGenConfig;
	USHORT wNumCyls;
	USHORT wReserved;
	USHORT wNumHeads;
	USHORT wBytesPerTrack;
	USHORT wBytesPerSector;
	USHORT wSectorsPerTrack;
	USHORT wVendorUnique[3];
	CHAR sSerialNumber[20];
	USHORT wBufferType;
	USHORT wBufferSize;
	USHORT wECCSize;
	CHAR sFirmwareRev[8];
	CHAR sModelNumber[40];
	USHORT wMoreVendorUnique;
	USHORT wDoubleWordIO;
	USHORT wCapabilities;
	USHORT wReserved1;
	USHORT wPIOTiming;
	USHORT wDMATiming;
	USHORT wBS;
	USHORT wNumCurrentCyls;
	USHORT wNumCurrentHeads;
	USHORT wNumCurrentSectorsPerTrack;
	ULONG ulCurrentSectorCapacity;
	USHORT wMultSectorStuff;
	ULONG ulTotalAddressableSectors;
	USHORT wSingleWordDMA;
	USHORT wMultiWordDMA;
	BYTE bReserved[128];
} IDSECTOR, *PIDSECTOR;		// from DiskID32

//---------------------------------------------------------------------------
// Send an IDENTIFY command to the drive
// bDriveNum = 0~3, bIDCmd = IDE_ATA_IDENTIFY or IDE_ATAPI_IDENTIFY
static BOOL DoIdentify(HANDLE hPhysicalDriveIOCTL, PSENDCMDINPARAMS pSCIP,
	PSENDCMDOUTPARAMS pSCOP, BYTE btIDCmd, BYTE btDriveNum, PDWORD pdwBytesReturned)
{
	// Set up data structures for IDENTIFY command.
	pSCIP->cBufferSize = IDENTIFY_BUFFER_SIZE;
	pSCIP->irDriveRegs.bFeaturesReg = 0;
	pSCIP->irDriveRegs.bSectorCountReg = 1;
	pSCIP->irDriveRegs.bSectorNumberReg = 1;
	pSCIP->irDriveRegs.bCylLowReg = 0;
	pSCIP->irDriveRegs.bCylHighReg = 0;
	// Compute the drive number.
	pSCIP->irDriveRegs.bDriveHeadReg = (btDriveNum & 1) ? 0xB0 : 0xA0;
	// The command can either be IDE identify or ATAPI identify.
	pSCIP->irDriveRegs.bCommandReg = btIDCmd;
	pSCIP->bDriveNumber = btDriveNum;
	pSCIP->cBufferSize = IDENTIFY_BUFFER_SIZE;

	return DeviceIoControl(hPhysicalDriveIOCTL, SMART_RCV_DRIVE_DATA,
		(LPVOID)pSCIP, sizeof(SENDCMDINPARAMS) - 1,
		(LPVOID)pSCOP, sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE - 1,
		pdwBytesReturned, NULL);
}

BOOL ReadPhysicalDriveOnNT(DISKINFO &szValue)
{
	BYTE btIDOutCmd[sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE - 1];

	for (USHORT nDrive=0; nDrive<MAX_IDE_DRIVES; nDrive++)
	{
		HANDLE hPhysicalDriveIOCTL;
		CHAR szDriveName[32];
		// Try to get a handle to PhysicalDrive IOCTL, report failure and exit if can't.
		sprintf(szDriveName, "\\\\.\\PhysicalDrive%d", nDrive);
		// Windows NT, Windows 2000, must have admin rights
		hPhysicalDriveIOCTL = CreateFile(szDriveName, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hPhysicalDriveIOCTL != INVALID_HANDLE_VALUE)
		{
			DWORD dwBytesReturned = 0;
			GETVERSIONOUTPARAMS gvopVersionParams;
			// Get the version, etc of PhysicalDrive IOCTL
			ZeroMemory(&gvopVersionParams, sizeof(GETVERSIONOUTPARAMS));
			if ( DeviceIoControl(hPhysicalDriveIOCTL, SMART_GET_VERSION, NULL, 0,
				&gvopVersionParams, sizeof(gvopVersionParams), &dwBytesReturned, NULL) )
			{
				// If there is a IDE device at number "i" issue commands to the device
				//if (gvopVersionParams.bIDEDeviceMap > 0)
				//{
					// IDE or ATAPI IDENTIFY cmd
					BYTE btIDCmd = 0;
					SENDCMDINPARAMS InParams;
					// Now, get the ID sector for all IDE devices in the system.
					// If the device is ATAPI use the IDE_ATAPI_IDENTIFY command,
					// otherwise use the IDE_ATA_IDENTIFY command.
					btIDCmd =(BYTE)( (gvopVersionParams.bIDEDeviceMap >> nDrive & 0x10) ? IDE_ATAPI_IDENTIFY : IDE_ATA_IDENTIFY );
					ZeroMemory(&InParams, sizeof(SENDCMDINPARAMS));
					ZeroMemory(btIDOutCmd, sizeof(btIDOutCmd));
					if ( DoIdentify(hPhysicalDriveIOCTL, &InParams, (PSENDCMDOUTPARAMS)btIDOutCmd,
						(BYTE)btIDCmd, (BYTE)nDrive, &dwBytesReturned) )
					{
						int i;
						PIDSECTOR pIDSector=(PIDSECTOR)((SENDCMDOUTPARAMS *)btIDOutCmd)->bBuffer;
						for (i=0; i<DISK_SERIAL_BUFF_LENGTH; i++)
						{
							szValue.sSerialNumber[i] = pIDSector->sSerialNumber[i];
						}
						szValue.sSerialNumber[DISK_SERIAL_BUFF_LENGTH] = '\0';
						for (i=0; i<DISK_MODEL_BUFF_LENGTH; i++)
						{
							szValue.sModelNumber[i] = pIDSector->sModelNumber[i];
						}
						szValue.sModelNumber[DISK_MODEL_BUFF_LENGTH] = '\0';
						
						CloseHandle(hPhysicalDriveIOCTL);
						hPhysicalDriveIOCTL = NULL;
						return TRUE;
					}
				//}
			}
			CloseHandle(hPhysicalDriveIOCTL);
		}
	}
	return FALSE;
}


//----------------------------------------------------
#define FILE_DEVICE_SCSI	0x0000001b
#define IOCTL_SCSI_MINIPORT	0x0004D008	// see NTDDSCSI.H for definition
#define IOCTL_SCSI_MINIPORT_IDENTIFY	((FILE_DEVICE_SCSI << 16) + 0x0501)

typedef struct _SRB_IO_CONTROL
{
	ULONG HeaderLength;
	UCHAR Signature[8];
	ULONG Timeout;
	ULONG ControlCode;
	ULONG ReturnCode;
	ULONG Length;
} SRB_IO_CONTROL, *PSRB_IO_CONTROL;

// this is kind of a back door via the SCSI mini port driver into the IDE drives
BOOL ReadIdeAsScsiDriveOnNT(DISKINFO &szValue)
{
	for (USHORT controller=0; controller<2; controller++)
	{
		HANDLE hScsiDriveIOCTL = 0;
		CHAR szDriveName[32];
		// Try to get a handle to PhysicalDrive IOCTL, report failure and exit if can't.
		sprintf(szDriveName, "\\\\.\\Scsi%d:", controller);
		// Windows NT, Windows 2000, any rights should do
		hScsiDriveIOCTL = CreateFile(szDriveName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);
		if (hScsiDriveIOCTL != INVALID_HANDLE_VALUE)
		{
			for (USHORT nDrive=0; nDrive<2; nDrive++)
			{
				CHAR buffer[sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE];
				SRB_IO_CONTROL *p = (SRB_IO_CONTROL *) buffer;
				SENDCMDINPARAMS *pin = (SENDCMDINPARAMS *)(buffer + sizeof(SRB_IO_CONTROL));
				DWORD dummy;
				memset(buffer, 0, sizeof(buffer));
				p->HeaderLength = sizeof(SRB_IO_CONTROL);
				p->Timeout = 10000;
				p->Length = sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE;
				p->ControlCode = IOCTL_SCSI_MINIPORT_IDENTIFY;
				strncpy((CHAR *)p->Signature, "SCSIDISK", 8);
				pin->irDriveRegs.bCommandReg = IDE_ATA_IDENTIFY;
				pin->bDriveNumber = (BYTE)nDrive;
				if (DeviceIoControl(hScsiDriveIOCTL, IOCTL_SCSI_MINIPORT,
					buffer, sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDINPARAMS) - 1,
					buffer, sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE,
					&dummy, NULL))
				{
					SENDCMDOUTPARAMS *pOut = (SENDCMDOUTPARAMS *)(buffer + sizeof(SRB_IO_CONTROL));
					PIDSECTOR pIDSector = (PIDSECTOR)(pOut->bBuffer);
					if (pIDSector->sModelNumber[0])
					{
						USHORT i = 0;
						for (i=0; i<DISK_SERIAL_BUFF_LENGTH; i++)
						{
							szValue.sSerialNumber[i] = pIDSector->sSerialNumber[i];
						}
						szValue.sSerialNumber[DISK_SERIAL_BUFF_LENGTH] = '\0';
						for (i=0; i<DISK_MODEL_BUFF_LENGTH; i++)
						{
							szValue.sModelNumber[i] = pIDSector->sModelNumber[i];
						}
						szValue.sModelNumber[DISK_MODEL_BUFF_LENGTH] = '\0';

						CloseHandle(hScsiDriveIOCTL);
						hScsiDriveIOCTL = NULL;
						return TRUE;
					}
				}
			}
			CloseHandle(hScsiDriveIOCTL);
		}
	}
	return FALSE;
}

//----------------------------------------------------
typedef struct _rt_IdeDInfo_
{
	BYTE IDEExists[4];
	BYTE DiskExists[8];
	WORD DisksRawInfo[8*256];
} rt_IdeDInfo, *pt_IdeDInfo;

typedef struct _rt_DiskInfo_
{
	BOOL DiskExists;
	BOOL ATAdevice;
	BOOL RemovableDevice;
	WORD TotLogCyl;
	WORD TotLogHeads;
	WORD TotLogSPT;
	char SerialNumber[20];
	char FirmwareRevision[8];
	char ModelNumber[40];
	WORD CurLogCyl;
	WORD CurLogHeads;
	WORD CurLogSPT;
} rt_DiskInfo;

#define m_cVxDFunctionIdesDInfo		1

BOOL ReadPhysicalDriveOn9X(DISKINFO &szValue)
{
	BOOL done = FALSE;
	USHORT nDrive = 0;
	HANDLE VxDHandle = 0;
	DWORD lpBytesReturned = 0;
	// set the thread priority high so that we get exclusive access to the disk
	BOOL status = SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	//if (0 == status) printf("\nERROR: Could not SetPriorityClass, LastError: %d\n", GetLastError ());
	// 1. Make an output buffer for the VxD
	rt_IdeDInfo info;
	pt_IdeDInfo pOutBufVxD = &info;
	// HAVE to zero out the buffer space for the IDE information!
	// If this is NOT done then garbage could be in the memory locations indicating if a disk exists or not.
	ZeroMemory (&info, sizeof(info));
	// 1. Try to load the VxD
	// must use the short file name path to open a VXD file
	char szDriveName[MAX_PATH], shortFileNamePath[MAX_PATH];
/*
	char *p = NULL;
	// get the directory that the exe was started from
	//GetModuleFileName(NULL, (LPSTR)szDriveName, sizeof(szDriveName));
	// cut the exe name from string
	p = &(szDriveName[strlen(szDriveName) - 1]);
	while (p >= szDriveName && *p && '\\' != *p) p--;
	*p = '\0';
	GetShortPathName(szDriveName, shortFileNamePath, MAX_PATH);
*/
	GetSystemDirectory(shortFileNamePath, sizeof(shortFileNamePath));
	sprintf(szDriveName, "\\\\.\\%s\\FILVDISK.VXD", shortFileNamePath);
	//VxDHandle = CreateFile("\\\\.\\IDE21201.VXD", 0, 0, 0, 0, FILE_FLAG_DELETE_ON_CLOSE, 0);
	VxDHandle = CreateFile(szDriveName, 0, 0, 0, 0, FILE_FLAG_OVERLAPPED|FILE_FLAG_DELETE_ON_CLOSE, 0);
	if (VxDHandle != INVALID_HANDLE_VALUE)
	{
		// 2. Run VxD function
		DeviceIoControl(VxDHandle, m_cVxDFunctionIdesDInfo, 0, 0, pOutBufVxD, sizeof(pt_IdeDInfo), &lpBytesReturned, 0);
		// 3. Unload VxD
		CloseHandle(VxDHandle);
	}
	else
	{
		//MessageBox (NULL, "ERROR: Could not open IDE21201.VXD file", "´íÎó", MB_ICONSTOP);
	}
	// 4. Translate and store data
	for (nDrive=0; nDrive<MAX_IDE_DRIVES; nDrive++)
	{
		if ((pOutBufVxD->DiskExists[nDrive]) && (pOutBufVxD->IDEExists[nDrive/2]))
		{
			USHORT i;
			PIDSECTOR pIDSector=(PIDSECTOR)&(pOutBufVxD->DisksRawInfo[nDrive*256]);
			for (i=0; i<DISK_SERIAL_BUFF_LENGTH; i++)
			{
				szValue.sSerialNumber[i] = pIDSector->sSerialNumber[i];
			}
			szValue.sSerialNumber[DISK_SERIAL_BUFF_LENGTH] = '\0';
			for (i=0; i<DISK_MODEL_BUFF_LENGTH; i++)
			{
				szValue.sModelNumber[i] = pIDSector->sModelNumber[i];
			}
			szValue.sModelNumber[DISK_MODEL_BUFF_LENGTH] = '\0';
			done = TRUE;
			break;
		}
	}
	// reset the thread priority back to normal
	SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	return done;
}
