#include <windows.h>
//#include <shellapi.h>
#include <winioctl.h>
#include <stdio.h>
#include <shlobj.h>
#include "HDHook.h"
#include "IoctlCmd.h"

#define WM_NOTIFYICON	(WM_APP+100)
#define MAX_LOADSTRING	100

// Global Variables:
//TCHAR szWindowClass[MAX_LOADSTRING];
TCHAR szTitle[MAX_LOADSTRING];	// The title bar text
TCHAR szConfigFile[MAX_PATH];	// �����ļ�����
HINSTANCE	__hInstance;		// current instance
HANDLE		__SysHandle = INVALID_HANDLE_VALUE;		// ��װ�����ľ��
BOOL		__bIsHooked = FALSE;
DWORD NTVersion;				// Windows�汾��
DISKINFO	HDHookOld, HDHookNew;
FILTER		FilterDefinition;	// ���̹��˶���

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	SettingProc(HWND, UINT, WPARAM, LPARAM);
BOOL				IconMessage(HWND, DWORD, PSTR);
BOOL				WriteResourceToFile(UINT, UINT, const char *);
BOOL				LoadDeviceDriver(const TCHAR * Name, const TCHAR * Path, HANDLE * lphDevice);
BOOL				UnloadDeviceDriver(const TCHAR * Name);
BOOL				ReadPhysicalDriveOnNT(DISKINFO &szValue);
BOOL				ReadPhysicalDriveOn9X(DISKINFO &szValue);

//�ַ����ߵ�λ����
void Byte2Swap(PCHAR str, USHORT length)
{
	CHAR ch;
	for (USHORT i=0; i<length; i+=2)
	{
		ch = str[i];
		str[i] = str[i+1];
		str[i+1] = ch;
	}
}

//ȥ���ַ���ĩβ�Ŀո�
void ByteTrim(PCHAR str, USHORT length)
{
	while (length > 0)
	{
		length--;
		if ((str[length] != ' ') && (str[length] != '\0'))
			break;
		else
			str[length] = '\0';
	}
	//str[length+1] = '\0';
}

// Copies in to out, but removes leading and trailing whitespace
void TrimBoth(CHAR *out, const CHAR *in)
{
	INT i, first, last;
	// Find the first non-space character (maybe none)
	first = -1;
	for (i=0; in[i]; i++)
	{
		if (!isspace((int)in[i]))
		{
			first = i;
			break;
		}
	}
	if (-1 == first)
	{
		// There are no non-space characters
		out[0] = '\0';
		return;
	}
	// Find the last non-space character
	for (i=strlen(in)-1; i>=first && isspace((int)in[i]); i--)
		;
	last = i; 
	strncpy(out, in+first, last-first+1);
	out[last-first+1] = '\0';
} 

//������ݷ�ʽ
BOOL CreateLink(LPSTR szPath, LPSTR szLink)
{
	CoInitialize(NULL);
	HRESULT hres;
	IShellLink *psl;
	IPersistFile *ppf;
	WORD wsz[MAX_PATH];
	//����һ��IShellLinkʵ��
	hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)&psl);
	if (FAILED(hres))
		return FALSE;
	//����Ŀ��Ӧ�ó���
	psl->SetPath(szPath);
	//���ÿ�ݼ�(�˴���ΪShift+Ctrl+'R')
	//psl->SetHotkey(MAKEWORD('R', HOTKEYF_SHIFT|HOTKEYF_CONTROL));
	//���ڱ����ݷ�ʽ�������ļ�(*.lnk)
	hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
	if (FAILED(hres))
		return FALSE;
	//ȷ�������ļ���ΪANSI��ʽ
	MultiByteToWideChar(CP_ACP, 0, szLink, -1, wsz, MAX_PATH);
	//�����ݷ�ʽ�������ļ�*.lnk
	hres = ppf->Save(wsz, STGM_READWRITE);
	//�ͷ�IPersistFile��IShellLink�ӿ�
	ppf->Release();
	psl->Release();
	CoUninitialize();
	return TRUE;
}

//�����ļ�ini
void GetConfigFile(TCHAR* buff, int nBuffLen)
{
	GetModuleFileName(NULL, buff, nBuffLen);
	//LPTSTR lpsz = strrchr(buff, '\\');
	//strcpy(lpsz, "\\HDHook.ini");
	LPTSTR lpsz = strrchr(buff, '.');
	strcpy(lpsz, ".ini");
}

//�Ѷ���˹��ɱ�����
void LoadTwister(UINT function)
{
	CHAR szBuffer[MAX_PATH], szTemp[MAX_PATH];
	HKEY hKEY;
	DWORD lpType = REG_SZ;
	DWORD lpcbData = 255;
	switch (function)
	{
	case 0://�����Ѷ���˹��
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "0", szTemp, 2, szConfigFile);
		if ('1' == szTemp[0])
		{
			if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Filseclab\\Twister", 0, KEY_READ, &hKEY) != ERROR_SUCCESS)
			{
				break;
			}
			if (RegQueryValueEx(hKEY, "FilseclabPath", NULL, &lpType, (PUCHAR)szBuffer, &lpcbData) == ERROR_SUCCESS)
			{
				strcat(szBuffer, "\\Twister.exe -a");
				WinExec(szBuffer, SW_NORMAL);
			}
			RegCloseKey(hKEY);
		}
		break;
	case 1://ע��
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKEY) == ERROR_SUCCESS)
		{
			// ɾ��ע�����������
			RegDeleteValue(hKEY, "Twister");
			RegCloseKey(hKEY);
			// �����������ݷ�ʽ
			LPITEMIDLIST pidl;
			SHGetSpecialFolderLocation(NULL, (NTVersion < 0x80000000) ? CSIDL_COMMON_STARTUP : CSIDL_STARTUP, &pidl);
			SHGetPathFromIDList(pidl, szTemp);
			strcat(szTemp, "\\�Ѷ���˹��ע��.lnk");
			GetModuleFileName(NULL, szBuffer, MAX_PATH);
			CreateLink(szBuffer, szTemp);
		}
		break;
	default:
		break;
	}
	return;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	//HACCEL hAccelTable;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	//LoadString(hInstance, IDC_GUI_CLASS, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	/*DWORD */NTVersion = GetVersion();
/*
	if (NTVersion >= 0x80000000)
	{
		MessageBox(NULL, "ֻ֧��XP/2000/NT����ϵͳ !", "����", MB_OK|MB_ICONERROR);
		return FALSE;
	};
*/
	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	//hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_GUI);
	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		//if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		//}
	}

	return msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX); 
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = (WNDPROC)WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_HDHOOK);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = (LPCSTR)IDC_HDHOOK;
	wcex.lpszClassName = szTitle;	//szWindowClass;
	wcex.hIconSm = NULL;	//LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);
	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	HWND hWnd;
	// Store instance handle in our global variable
	__hInstance = hInstance;
	//hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	hWnd = CreateWindow(szTitle, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}

	//ShowWindow(hWnd, nCmdShow);
	//UpdateWindow(hWnd);
	return TRUE;
}

VOID HookOn(HWND hWnd)
{
	DWORD dwDummy;
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_HOOK, NULL, 0, NULL, 0, &dwDummy, NULL);
	__bIsHooked = TRUE;
	IconMessage(hWnd, NIM_MODIFY, "����״̬");
}

VOID HookOff(HWND hWnd)
{
	DWORD dwDummy;
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_UNHOOK, NULL, 0, NULL, 0, &dwDummy, NULL);
	__bIsHooked = FALSE;
	IconMessage(hWnd, NIM_MODIFY, "�ر�״̬");
}

VOID SetOldValue(DISKINFO &szValue)
{
	DWORD dwDummy;
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_SETOLDVALUE,
		&szValue, sizeof(DISKINFO), NULL, 0, &dwDummy, NULL);
}

VOID SetNewValue(DISKINFO &szValue)
{
	DWORD dwDummy;
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_SETNEWVALUE,
		&szValue, sizeof(DISKINFO), NULL, 0, &dwDummy, NULL);
}

VOID SetFilter(FILTER &szValue)
{
	DWORD dwDummy;
	_strupr(szValue.IncludeFilter);
	_strupr(szValue.ExcludeFilter);
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_SETFILTER,
		&szValue, sizeof(FILTER), NULL, 0, &dwDummy, NULL);
}

VOID Load(HWND hWnd)
{
	DWORD dwDummy, versionNumber;
	CHAR szTemp[MAX_PATH];

	// ֻ����һ��ʵ��
	BOOL bFound;
	HANDLE hMutexOneInstantance = CreateMutex(NULL, TRUE, szTitle);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		bFound = TRUE; 
	if (hMutexOneInstantance)
		ReleaseMutex(hMutexOneInstantance);
	if (TRUE == bFound)
	{
		MessageBox(NULL, "�������Ѿ������� !", "����", MB_OK|MB_ICONERROR);
		ExitProcess(0);
	}

	if (NTVersion < 0x80000000)
	{
		// �ͷ������ļ�
		GetWindowsDirectory(szTemp, sizeof(szTemp));
		strcat(szTemp, "\\Temp\\");
		strcat(szTemp, DRIVER_FILE_NAME);
		WriteResourceToFile(IDR_SYS, 100, szTemp);

		if (! LoadDeviceDriver(SERVICE_NAME, szTemp, &__SysHandle))
		{
			// ������һ��
			if (! LoadDeviceDriver(SERVICE_NAME, szTemp, &__SysHandle))
			{
				MessageBox(hWnd, "�豸���������ʧ�� !", "����", MB_OK|MB_ICONERROR);
				ExitProcess(0);
			}
		}
		if (! DeviceIoControl(__SysHandle, IOCTL_HDHOOK_GETVERSION, NULL, 0, &versionNumber,
			sizeof(DWORD), &dwDummy, NULL) || versionNumber != HDHOOK_VERSION)
		{
			MessageBox(hWnd, "��������汾���� !", "����", MB_OK|MB_ICONERROR);
			ExitProcess(0);
		}
	}
	else
	{
		CHAR szDriveName[MAX_PATH];
		GetSystemDirectory(szTemp, sizeof(szTemp));
		__SysHandle = CreateFile("\\\\.\\Smartvsd", 0, 0, 0, CREATE_NEW, 0, 0);
		if (INVALID_HANDLE_VALUE == __SysHandle)
		{
			strcpy(szDriveName, szTemp);
			strcat(szDriveName, "\\FILVDISK.VXD");
			WriteResourceToFile(IDR_VXD, 100, szDriveName);
			sprintf(szDriveName, "\\\\.\\%s\\FILVDISK.VXD", szTemp);
			__SysHandle = CreateFile(szDriveName, 0, 0, 0, 0, FILE_FLAG_OVERLAPPED|FILE_FLAG_DELETE_ON_CLOSE, 0);
			if (INVALID_HANDLE_VALUE == __SysHandle)
			{
				MessageBox(NULL, "�豸���������ʧ�� !", "����", MB_OK|MB_ICONERROR);
				ExitProcess(0);
			}
		}
		else
		{
			CloseHandle(__SysHandle);
			__SysHandle = INVALID_HANDLE_VALUE;
			if (MessageBox(NULL, "��Ҫ�����������滻ԭ�е�ϵͳ�ļ���������", "��������", MB_ICONEXCLAMATION|MB_OKCANCEL) == IDOK)
			{
				strcat(szTemp, "\\IOSUBSYS\\SMARTVSD.VXD");
				if (GetFileAttributes(szTemp) != -1)
					DeleteFile(szTemp);
				//��������//EWX_FORCE
				ExitWindowsEx(EWX_REBOOT, 0);
			}
			else
			{
				ExitProcess(0);
			}
		}
	}

	// �ȹرչ��ӣ��ٶ�ȡ��ʵ���кź�ģ�ͺ�
	HookOff(hWnd);
	//ZeroMemory(&HDHookOld, sizeof(DISKINFO));
	if (NTVersion < 0x80000000)
	{
		if (!ReadPhysicalDriveOnNT(HDHookOld))
		{
			//ReadIdeAsScsiDriveInNT(HDHookOld);
			MessageBox(hWnd, "Ӳ�����кŻ�ȡʧ�� !", "����", MB_OK|MB_ICONERROR);
			ExitProcess(0);
		}
	}
	else
	{
		if (!ReadPhysicalDriveOn9X(HDHookOld))
		{
			MessageBox(hWnd, "Ӳ�����кŻ�ȡʧ�� !", "����", MB_OK|MB_ICONERROR);
			ExitProcess(0);
		}
	}
	SetOldValue(HDHookOld);

	ZeroMemory(&HDHookNew, sizeof(DISKINFO));
	GetConfigFile(szConfigFile, MAX_PATH);
	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_SERIAL, "", HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH+1, szConfigFile);
	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_MODEL, "", HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH+1, szConfigFile);
	if (strlen(HDHookNew.sSerialNumber) > 0 && strlen(HDHookNew.sModelNumber) > 0)
	{
		Byte2Swap(HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
		Byte2Swap(HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH);
		SetNewValue(HDHookNew);
	}
	else
	{
		// �������к���Чʱʹ����ʵ���к�
		SetNewValue(HDHookOld);
	}
	Byte2Swap(HDHookOld.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
	ByteTrim(HDHookOld.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
	Byte2Swap(HDHookOld.sModelNumber, DISK_MODEL_BUFF_LENGTH);
	ByteTrim(HDHookOld.sModelNumber, DISK_MODEL_BUFF_LENGTH);

	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, "*", FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, "", FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
	SetFilter(FilterDefinition);

	// ��ʽ��������
	HookOn(hWnd);
	LoadTwister(0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	HDC hdc;
	PAINTSTRUCT ps;
	HWND hwnd;

	switch (message)
	{
	case WM_CREATE:
		IconMessage(hWnd, NIM_ADD, "�ر�״̬");	// ��ʼ״̬
		Load(hWnd);
		CHAR szBuffer[3];
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SetTimer(hWnd, 1, 10000, NULL);		// ��ʱ10���˳�
		break;

	case WM_COMMAND:
		wmId	= LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			MessageBox(NULL, "Ӳ�����кŷ��湤��\n����\nhupo@tju.edu.cn", "����", MB_OK | MB_ICONWARNING);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		case IDM_HOOKON:
			HookOn(hWnd);
			break;
		case IDM_HOOKOFF:
			HookOff(hWnd);
			break;
		case IDM_SETTING:
			// �ж�ʵ���Ƿ�Ψһ
			hwnd = FindWindow("#32770", "Ӳ�����кŷ��湤�� - ����");
			if (IsWindow(hwnd))
			{
				SetForegroundWindow(hwnd);
				//return 0;
			}
			else
			{
				//SetForegroundWindow(hWnd);
				DialogBox(__hInstance, (LPCTSTR)IDD_SETTING, hWnd, (DLGPROC)SettingProc);
			}
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		//RECT rt;
		//GetClientRect(hWnd, &rt);
		//DrawText(hdc, szHello, strlen(szHello), &rt, DT_CENTER);
		EndPaint(hWnd, &ps);
		break;

	case WM_DESTROY:
		HookOff(hWnd);
		IconMessage(hWnd, NIM_DELETE, NULL);
		if (__SysHandle != INVALID_HANDLE_VALUE)
		{
			// ���ر���������򲻻����̴������б���ɾ��
			CloseHandle(__SysHandle);
		}
		if (NTVersion < 0x80000000)
		{
			UnloadDeviceDriver(SERVICE_NAME);
		}
		PostQuitMessage(0);
		break;

	case WM_TIMER:// ��ʱ10���˳�
		KillTimer(hWnd, 1);
		DestroyWindow(hWnd);
		break;

	case WM_NOTIFYICON:
		switch (lParam)
		{
		case WM_LBUTTONUP:// ��������л�
			if (__bIsHooked)
				HookOff(hWnd);
			else
				HookOn(hWnd);
			return 0;
		case WM_RBUTTONUP://�Ҽ����������˵�
			{
				POINT point;
				HMENU hMenu, hSubMenu;
				// Get mouse position
				GetCursorPos(&point);
				// Popup context menu
				hMenu = LoadMenu(__hInstance, MAKEINTRESOURCE(IDC_HDHOOK));
				hSubMenu = GetSubMenu(hMenu, 0);
				if (__bIsHooked)
				{
					//CheckMenuItem(hMenu,IDM_HOOKON,TRUE);
					SetMenuDefaultItem(hSubMenu, IDM_HOOKON, FALSE);
				}
				else
				{
					//CheckMenuItem(hMenu,IDM_HOOKOFF,TRUE);
					SetMenuDefaultItem(hSubMenu, IDM_HOOKOFF, FALSE);
				}
				//SetForegroundWindow(hWnd);
				TrackPopupMenu(hSubMenu, TPM_LEFTBUTTON|TPM_RIGHTBUTTON|TPM_LEFTALIGN,
					point.x, point.y, 0, hWnd, NULL);
				PostMessage(hWnd, WM_NULL, 0, 0);
				DestroyMenu(hMenu);
			}
			break;
		//case WM_LBUTTONDBLCLK:// ˫��ͼ��
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for Setting box.
LRESULT CALLBACK SettingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;

	switch (message)
	{
	case WM_INITDIALOG:
		CHAR szBuffer[3];
		//���Ƴ���
		SendMessage(GetDlgItem(hDlg, IDC_NEWDISKSERIAL), EM_LIMITTEXT, (WPARAM)(DISK_SERIAL_BUFF_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_NEWDISKMODEL) , EM_LIMITTEXT, (WPARAM)(DISK_MODEL_BUFF_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_INCLUDEFILTER), EM_LIMITTEXT, (WPARAM)(FILTER_MAX_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_EXCLUDEFILTER), EM_LIMITTEXT, (WPARAM)(FILTER_MAX_LENGTH), 0L);
		//ԭʼ���к�
		SetDlgItemText(hDlg, IDC_OLDDISKSERIAL, HDHookOld.sSerialNumber);
		//ԭʼģ�ͺ�
		SetDlgItemText(hDlg, IDC_OLDDISKMODEL, HDHookOld.sModelNumber);
		//�������к�
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_SERIAL, "", HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_NEWDISKSERIAL, HDHookNew.sSerialNumber);
		//����ģ�ͺ�
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_MODEL, "", HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_NEWDISKMODEL, HDHookNew.sModelNumber);
		//���˽�����
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, "*", FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_INCLUDEFILTER, FilterDefinition.IncludeFilter);
		//�ų�������
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, "", FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_EXCLUDEFILTER, FilterDefinition.ExcludeFilter);
		//��ѡ��1
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_SETCHECK, BST_CHECKED, 0);
		else
			SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_SETCHECK, BST_UNCHECKED, 0);
		//��ѡ��2
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_SETCHECK, BST_CHECKED, 0);
		else
			SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_SETCHECK, BST_UNCHECKED, 0);
		return TRUE;

	case WM_COMMAND:
		wmId	= LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		if (wmId == IDOK || wmId == IDCANCEL || wmId == IDC_CANCEL)	//�ر�
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		if (wmId == IDC_APPLY)	//Ӧ��
		{
			ZeroMemory(&HDHookNew, sizeof(DISKINFO));
			//�޸����к�
			GetDlgItemText(hDlg, IDC_NEWDISKSERIAL, HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH+1);
			ByteTrim(HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
			if (strlen(HDHookNew.sSerialNumber) < 1)
			{
				MessageBox(hDlg, "���кű�����������1���ַ� !", "����", MB_OK|MB_ICONERROR);
				break;
			}
			//�޸�ģ�ͺ�
			GetDlgItemText(hDlg, IDC_NEWDISKMODEL, HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH+1);
			ByteTrim(HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH);
			if (strlen(HDHookNew.sModelNumber) < 2)
			{
				MessageBox(hDlg, "ģ�ͺű�����������2���ַ� !", "����", MB_OK|MB_ICONERROR);
				break;
			}
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_SERIAL, HDHookNew.sSerialNumber, szConfigFile);
			Byte2Swap(HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_MODEL, HDHookNew.sModelNumber, szConfigFile);
			Byte2Swap(HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH);
			SetNewValue(HDHookNew);
			//���˽�����
			GetDlgItemText(hDlg, IDC_INCLUDEFILTER, FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, FilterDefinition.IncludeFilter, szConfigFile);
			//�ų�������
			GetDlgItemText(hDlg, IDC_EXCLUDEFILTER, FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, FilterDefinition.ExcludeFilter, szConfigFile);
			SetFilter(FilterDefinition);
			//��ѡ��1
			if (SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "1", szConfigFile);
				LoadTwister(1);
			}
			else
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "0", szConfigFile);
			}
			//��ѡ��2
			if (SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "1", szConfigFile);
			}
			else
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szConfigFile);
			}
			MessageBox(hDlg, "�µ���ֵ�Ѿ����óɹ� !", "���", MB_OK|MB_ICONINFORMATION);
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		//return TRUE;
		//break;
	}
	return FALSE;
}

BOOL IconMessage(HWND hDlg, DWORD dwMessage, PSTR pszTip)
{	
	BOOL retval;
	NOTIFYICONDATA tnd;
	HICON hIcon;

	if (__bIsHooked)
		hIcon = LoadIcon(__hInstance, MAKEINTRESOURCE(IDI_HOOKON));
	else
		hIcon = LoadIcon(__hInstance, MAKEINTRESOURCE(IDI_HOOKOFF));

	tnd.cbSize = sizeof(NOTIFYICONDATA);
	tnd.hWnd = hDlg;
	tnd.uID = 0x123456;
	tnd.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP;
	tnd.hIcon = hIcon;
	tnd.uCallbackMessage = WM_NOTIFYICON;
	if (pszTip)
	{
		lstrcpyn(tnd.szTip, pszTip, sizeof(tnd.szTip));
	}
	else
	{
		tnd.szTip[0] = '\0';
	}
	retval = Shell_NotifyIcon(dwMessage, &tnd);
	if (hIcon)
		DestroyIcon(hIcon);
	return retval;
}

BOOL WriteResourceToFile(UINT idResource, UINT idType, const CHAR *fileName)
{
	HRSRC hResInfo = FindResource(__hInstance, MAKEINTRESOURCE(idResource), MAKEINTRESOURCE(idType));
	if (!hResInfo)
		return false;

	HGLOBAL hgRes = LoadResource(__hInstance, hResInfo);
	if (!hgRes)
		return false;

	void *pvRes = LockResource(hgRes);
	if (!pvRes)
		return false;

	DWORD cbRes = SizeofResource(__hInstance, hResInfo);

	HANDLE hFile = CreateFile(fileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (INVALID_HANDLE_VALUE == hFile)
	{
		//int a = GetLastError();
		return false;
	}

	DWORD cbWritten;
	WriteFile(hFile, pvRes, cbRes, &cbWritten, 0);
	CloseHandle(hFile);
	return true;
}
