// HDHOOK.c - main module for VxD HDHOOK

#define   DEVICE_MAIN
#include "HDHOOK.h"
#include "..\EXE\IoctlCmd.h"
#undef    DEVICE_MAIN

BOOLEAN IsHooked = TRUE;
DISKINFO __HookNew={"Q54E8MZH            ", "TS 0                                    "};
/////////////////////12345678901234567890////1234567890123456789012345678901234567890

Declare_Virtual_Device(HDHOOK)

DefineControlHandler(SYS_DYNAMIC_DEVICE_INIT, OnSysDynamicDeviceInit);
DefineControlHandler(SYS_DYNAMIC_DEVICE_EXIT, OnSysDynamicDeviceExit);
DefineControlHandler(W32_DEVICEIOCONTROL, OnW32Deviceiocontrol);

BOOL __cdecl ControlDispatcher(
	DWORD dwControlMessage,
	DWORD EBX,
	DWORD EDX,
	DWORD ESI,
	DWORD EDI,
	DWORD ECX)
{
	START_CONTROL_DISPATCH

		ON_SYS_DYNAMIC_DEVICE_INIT(OnSysDynamicDeviceInit);
		ON_SYS_DYNAMIC_DEVICE_EXIT(OnSysDynamicDeviceExit);
		ON_W32_DEVICEIOCONTROL(OnW32Deviceiocontrol);

	END_CONTROL_DISPATCH

	return TRUE;
}

void ModifyDiskInfo(rt_IdeDInfo *pOutBufVxD)
{
	USHORT i;
	IDSECTOR *pIDSector;
	if (pOutBufVxD->DiskExists[0])
	{
		pIDSector = (IDSECTOR *)(&(pOutBufVxD->DisksRawInfo[0]));
		for (i=0; i<DISK_SERIAL_BUFF_LENGTH; ++i)
		{
			pIDSector->sSerialNumber[i] = __HookNew.sSerialNumber[i];
		}
		for (i=0; i<DISK_MODEL_BUFF_LENGTH; ++i)
		{
			pIDSector->sModelNumber[i] = __HookNew.sModelNumber[i];
		}
	}
	return;
}
/*
VOID GetPortVal(WORD wPortAddr, PDWORD pdwPortVal, BYTE bSize)
{
	switch (bSize)
	{
	case 1:
		*pdwPortVal = _inp(wPortAddr);
		break;
	case 2:
		*pdwPortVal = _inpw(wPortAddr);
		break;
	case 4:
		*pdwPortVal = _inpd(wPortAddr);
		break;
	}
}

VOID SetPortVal(WORD wPortAddr, DWORD dwPortVal, BYTE bSize)
{
	switch (bSize)
	{
	case 1:
		_outp(wPortAddr, dwPortVal);
		break;
	case 2:
		_outpw(wPortAddr, (WORD)dwPortVal);
		break;
	case 4:
		_outpd(wPortAddr, dwPortVal);
		break;
	}
}
*/
BOOL OnSysDynamicDeviceInit()
{
	return TRUE;
}

BOOL OnSysDynamicDeviceExit()
{
	return TRUE;
}

#define GetPortByte(wPortAddr) _inp(wPortAddr)
#define GetPortWord(wPortAddr) _inpw(wPortAddr)
#define SetPortByte(wPortAddr, dwPortVal) _outp(wPortAddr, dwPortVal)

DWORD OnW32Deviceiocontrol(PIOCTLPARAMS p)
{
	rt_IdeDInfo *pOutBufVxD;
	USHORT nDrive;
	switch (p->dioc_IOCtlCode)
	{
	case 1:
		*(p->dioc_bytesret) = 0;//p->dioc_cbOutBuf;
		pOutBufVxD = (rt_IdeDInfo *)p->dioc_OutBuf;
		// Get IDE Drive info from the hardware ports loop through all possible drives
		for (nDrive=0; nDrive<8; nDrive++)
		{
			DWORD portValue = 0;
			WORD baseAddress;	// Base address of drive controller 
			int waitLoop;
			USHORT index;
			USHORT nIDE = nDrive /2;
			switch (nIDE)
			{
			case 0: baseAddress = 0x1f0; break;
			case 1: baseAddress = 0x170; break;
			case 2: baseAddress = 0x1e8; break;
			case 3: baseAddress = 0x168; break;
			}
			// Wait for controller not busy 
			waitLoop = 100000;
			while (--waitLoop > 0)
			{
				//GetPortVal((WORD)(baseAddress + 7), &portValue, (BYTE) 1);
				portValue = GetPortByte((WORD)(baseAddress + 7));
				if ((portValue & 0x40) == 0x40)
				{
					// drive is ready
					break;
				}
				if ((portValue & 0x01) == 0x01)
				{
					// previous drive command ended in error
					break;
				}
			}
			if (waitLoop < 1)
			{
				//pOutBufVxD->DiskExists[nDrive] = 0;
				continue;
			}
			// Set Master or Slave drive
			if ((nDrive % 2) == 0)
			{
				//SetPortVal((WORD)(baseAddress + 6), 0xA0, 1);
				SetPortByte((WORD)(baseAddress + 6), 0xA0);
			}
			else
			{
				//SetPortVal((WORD)(baseAddress + 6), 0xB0, 1);
				SetPortByte((WORD)(baseAddress + 6), 0xB0);
			}
			// Get drive info data
			//SetPortVal((WORD)(baseAddress + 7), 0xEC, 1);
			SetPortByte((WORD)(baseAddress + 7), 0xEC);
			// Wait for data ready
			waitLoop = 100000;
			while (--waitLoop > 0)
			{
				//GetPortVal((WORD)(baseAddress + 7), &portValue, 1);
				portValue = GetPortByte((WORD)(baseAddress + 7));
				if ((portValue & 0x48) == 0x48)
				{
					// see if the drive is ready and has it's info ready for us
					break;
				}
				if ((portValue & 0x01) == 0x01)
				{
					// see if there is a drive error
					break;
				}
			}
			// check for time out or other error
			if (waitLoop < 1 || portValue & 0x01)
			{
				//pOutBufVxD->DiskExists[nDrive] = 0;
				continue;
			}
			// read drive id information
			for (index = 0; index < 256; index++)
			{
				portValue = 0;
				//GetPortVal(baseAddress, &portValue, 2);
				portValue = GetPortWord(baseAddress);
				pOutBufVxD->DisksRawInfo[nDrive*256+index] = portValue;
			}
			pOutBufVxD->IDEExists[nIDE] = 1;
			pOutBufVxD->DiskExists[nDrive] = 1;
		}
		if (IsHooked == TRUE)
		{
			ModifyDiskInfo(pOutBufVxD);
		}
		return 0;
	case 0:
		return 0;
	case IOCTL_HDHOOK_HOOK:
		IsHooked = TRUE;
		return 0;
	case IOCTL_HDHOOK_UNHOOK:
		IsHooked = FALSE;
		return 0;
	case IOCTL_HDHOOK_SETNEWVALUE:
		if (p->dioc_cbInBuf < sizeof(DISKINFO) || p->dioc_InBuf == NULL)
		{
			return 1;
		}
		__HookNew = *(DISKINFO *)p->dioc_InBuf;
		return 0;
	}
	return 0;
}
