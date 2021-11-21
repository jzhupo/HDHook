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
TCHAR szConfigFile[MAX_PATH];	// 配置文件名称
HINSTANCE	__hInstance;		// current instance
HANDLE		__SysHandle = INVALID_HANDLE_VALUE;		// 安装驱动的句柄
BOOL		__bIsHooked = FALSE;
DWORD NTVersion;				// Windows版本号
DISKINFO	HDHookOld, HDHookNew;
FILTER		FilterDefinition;	// 进程过滤定义

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

//字符串高低位交换
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

//去掉字符串末尾的空格
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

//创建快捷方式
BOOL CreateLink(LPSTR szPath, LPSTR szLink)
{
	CoInitialize(NULL);
	HRESULT hres;
	IShellLink *psl;
	IPersistFile *ppf;
	WORD wsz[MAX_PATH];
	//创建一个IShellLink实例
	hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)&psl);
	if (FAILED(hres))
		return FALSE;
	//设置目标应用程序
	psl->SetPath(szPath);
	//设置快捷键(此处设为Shift+Ctrl+'R')
	//psl->SetHotkey(MAKEWORD('R', HOTKEYF_SHIFT|HOTKEYF_CONTROL));
	//用于保存快捷方式的数据文件(*.lnk)
	hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
	if (FAILED(hres))
		return FALSE;
	//确保数据文件名为ANSI格式
	MultiByteToWideChar(CP_ACP, 0, szLink, -1, wsz, MAX_PATH);
	//保存快捷方式的数据文件*.lnk
	hres = ppf->Save(wsz, STGM_READWRITE);
	//释放IPersistFile和IShellLink接口
	ppf->Release();
	psl->Release();
	CoUninitialize();
	return TRUE;
}

//配置文件ini
void GetConfigFile(TCHAR* buff, int nBuffLen)
{
	GetModuleFileName(NULL, buff, nBuffLen);
	//LPTSTR lpsz = strrchr(buff, '\\');
	//strcpy(lpsz, "\\HDHook.ini");
	LPTSTR lpsz = strrchr(buff, '.');
	strcpy(lpsz, ".ini");
}

//费尔托斯特杀毒软件
void LoadTwister(UINT function)
{
	CHAR szBuffer[MAX_PATH], szTemp[MAX_PATH];
	HKEY hKEY;
	DWORD lpType = REG_SZ;
	DWORD lpcbData = 255;
	switch (function)
	{
	case 0://启动费尔托斯特
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
	case 1://注册
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKEY) == ERROR_SUCCESS)
		{
			// 删除注册表自启动项
			RegDeleteValue(hKEY, "Twister");
			RegCloseKey(hKEY);
			// 创建启动组快捷方式
			LPITEMIDLIST pidl;
			SHGetSpecialFolderLocation(NULL, (NTVersion < 0x80000000) ? CSIDL_COMMON_STARTUP : CSIDL_STARTUP, &pidl);
			SHGetPathFromIDList(pidl, szTemp);
			strcat(szTemp, "\\费尔托斯特注册.lnk");
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
		MessageBox(NULL, "只支持XP/2000/NT操作系统 !", "错误", MB_OK|MB_ICONERROR);
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
	IconMessage(hWnd, NIM_MODIFY, "激活状态");
}

VOID HookOff(HWND hWnd)
{
	DWORD dwDummy;
	DeviceIoControl(__SysHandle, IOCTL_HDHOOK_UNHOOK, NULL, 0, NULL, 0, &dwDummy, NULL);
	__bIsHooked = FALSE;
	IconMessage(hWnd, NIM_MODIFY, "关闭状态");
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

	// 只运行一个实例
	BOOL bFound;
	HANDLE hMutexOneInstantance = CreateMutex(NULL, TRUE, szTitle);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		bFound = TRUE; 
	if (hMutexOneInstantance)
		ReleaseMutex(hMutexOneInstantance);
	if (TRUE == bFound)
	{
		MessageBox(NULL, "本程序已经在运行 !", "错误", MB_OK|MB_ICONERROR);
		ExitProcess(0);
	}

	if (NTVersion < 0x80000000)
	{
		// 释放驱动文件
		GetWindowsDirectory(szTemp, sizeof(szTemp));
		strcat(szTemp, "\\Temp\\");
		strcat(szTemp, DRIVER_FILE_NAME);
		WriteResourceToFile(IDR_SYS, 100, szTemp);

		if (! LoadDeviceDriver(SERVICE_NAME, szTemp, &__SysHandle))
		{
			// 再重试一次
			if (! LoadDeviceDriver(SERVICE_NAME, szTemp, &__SysHandle))
			{
				MessageBox(hWnd, "设备驱动程序打开失败 !", "错误", MB_OK|MB_ICONERROR);
				ExitProcess(0);
			}
		}
		if (! DeviceIoControl(__SysHandle, IOCTL_HDHOOK_GETVERSION, NULL, 0, &versionNumber,
			sizeof(DWORD), &dwDummy, NULL) || versionNumber != HDHOOK_VERSION)
		{
			MessageBox(hWnd, "驱动程序版本错误 !", "错误", MB_OK|MB_ICONERROR);
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
				MessageBox(NULL, "设备驱动程序打开失败 !", "错误", MB_OK|MB_ICONERROR);
				ExitProcess(0);
			}
		}
		else
		{
			CloseHandle(__SysHandle);
			__SysHandle = INVALID_HANDLE_VALUE;
			if (MessageBox(NULL, "需要重新启动来替换原有的系统文件，继续吗？", "重新启动", MB_ICONEXCLAMATION|MB_OKCANCEL) == IDOK)
			{
				strcat(szTemp, "\\IOSUBSYS\\SMARTVSD.VXD");
				if (GetFileAttributes(szTemp) != -1)
					DeleteFile(szTemp);
				//重启电脑//EWX_FORCE
				ExitWindowsEx(EWX_REBOOT, 0);
			}
			else
			{
				ExitProcess(0);
			}
		}
	}

	// 先关闭钩子，再读取真实序列号和模型号
	HookOff(hWnd);
	//ZeroMemory(&HDHookOld, sizeof(DISKINFO));
	if (NTVersion < 0x80000000)
	{
		if (!ReadPhysicalDriveOnNT(HDHookOld))
		{
			//ReadIdeAsScsiDriveInNT(HDHookOld);
			MessageBox(hWnd, "硬盘序列号获取失败 !", "错误", MB_OK|MB_ICONERROR);
			ExitProcess(0);
		}
	}
	else
	{
		if (!ReadPhysicalDriveOn9X(HDHookOld))
		{
			MessageBox(hWnd, "硬盘序列号获取失败 !", "错误", MB_OK|MB_ICONERROR);
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
		// 仿真序列号无效时使用真实序列号
		SetNewValue(HDHookOld);
	}
	Byte2Swap(HDHookOld.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
	ByteTrim(HDHookOld.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
	Byte2Swap(HDHookOld.sModelNumber, DISK_MODEL_BUFF_LENGTH);
	ByteTrim(HDHookOld.sModelNumber, DISK_MODEL_BUFF_LENGTH);

	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, "*", FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
	GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, "", FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
	SetFilter(FilterDefinition);

	// 正式开启钩子
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
		IconMessage(hWnd, NIM_ADD, "关闭状态");	// 初始状态
		Load(hWnd);
		CHAR szBuffer[3];
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SetTimer(hWnd, 1, 10000, NULL);		// 定时10秒退出
		break;

	case WM_COMMAND:
		wmId	= LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			MessageBox(NULL, "硬盘序列号仿真工具\n胡泊\nhupo@tju.edu.cn", "关于", MB_OK | MB_ICONWARNING);
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
			// 判断实例是否唯一
			hwnd = FindWindow("#32770", "硬盘序列号仿真工具 - 设置");
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
			// 不关闭驱动句柄则不会立刻从驱动列表中删除
			CloseHandle(__SysHandle);
		}
		if (NTVersion < 0x80000000)
		{
			UnloadDeviceDriver(SERVICE_NAME);
		}
		PostQuitMessage(0);
		break;

	case WM_TIMER:// 定时10秒退出
		KillTimer(hWnd, 1);
		DestroyWindow(hWnd);
		break;

	case WM_NOTIFYICON:
		switch (lParam)
		{
		case WM_LBUTTONUP:// 左键单击切换
			if (__bIsHooked)
				HookOff(hWnd);
			else
				HookOn(hWnd);
			return 0;
		case WM_RBUTTONUP://右键单击弹出菜单
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
		//case WM_LBUTTONDBLCLK:// 双击图标
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
		//限制长度
		SendMessage(GetDlgItem(hDlg, IDC_NEWDISKSERIAL), EM_LIMITTEXT, (WPARAM)(DISK_SERIAL_BUFF_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_NEWDISKMODEL) , EM_LIMITTEXT, (WPARAM)(DISK_MODEL_BUFF_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_INCLUDEFILTER), EM_LIMITTEXT, (WPARAM)(FILTER_MAX_LENGTH), 0L);
		SendMessage(GetDlgItem(hDlg, IDC_EXCLUDEFILTER), EM_LIMITTEXT, (WPARAM)(FILTER_MAX_LENGTH), 0L);
		//原始序列号
		SetDlgItemText(hDlg, IDC_OLDDISKSERIAL, HDHookOld.sSerialNumber);
		//原始模型号
		SetDlgItemText(hDlg, IDC_OLDDISKMODEL, HDHookOld.sModelNumber);
		//仿真序列号
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_SERIAL, "", HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_NEWDISKSERIAL, HDHookNew.sSerialNumber);
		//仿真模型号
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_MODEL, "", HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_NEWDISKMODEL, HDHookNew.sModelNumber);
		//过滤进程名
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, "*", FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_INCLUDEFILTER, FilterDefinition.IncludeFilter);
		//排除进程名
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, "", FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1, szConfigFile);
		SetDlgItemText(hDlg, IDC_EXCLUDEFILTER, FilterDefinition.ExcludeFilter);
		//单选框1
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_SETCHECK, BST_CHECKED, 0);
		else
			SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_SETCHECK, BST_UNCHECKED, 0);
		//单选框2
		GetPrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szBuffer, 2, szConfigFile);
		if ('1' == szBuffer[0])
			SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_SETCHECK, BST_CHECKED, 0);
		else
			SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_SETCHECK, BST_UNCHECKED, 0);
		return TRUE;

	case WM_COMMAND:
		wmId	= LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		if (wmId == IDOK || wmId == IDCANCEL || wmId == IDC_CANCEL)	//关闭
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		if (wmId == IDC_APPLY)	//应用
		{
			ZeroMemory(&HDHookNew, sizeof(DISKINFO));
			//修改序列号
			GetDlgItemText(hDlg, IDC_NEWDISKSERIAL, HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH+1);
			ByteTrim(HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
			if (strlen(HDHookNew.sSerialNumber) < 1)
			{
				MessageBox(hDlg, "序列号必须输入至少1个字符 !", "错误", MB_OK|MB_ICONERROR);
				break;
			}
			//修改模型号
			GetDlgItemText(hDlg, IDC_NEWDISKMODEL, HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH+1);
			ByteTrim(HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH);
			if (strlen(HDHookNew.sModelNumber) < 2)
			{
				MessageBox(hDlg, "模型号必须输入至少2个字符 !", "错误", MB_OK|MB_ICONERROR);
				break;
			}
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_SERIAL, HDHookNew.sSerialNumber, szConfigFile);
			Byte2Swap(HDHookNew.sSerialNumber, DISK_SERIAL_BUFF_LENGTH);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_MODEL, HDHookNew.sModelNumber, szConfigFile);
			Byte2Swap(HDHookNew.sModelNumber, DISK_MODEL_BUFF_LENGTH);
			SetNewValue(HDHookNew);
			//过滤进程名
			GetDlgItemText(hDlg, IDC_INCLUDEFILTER, FilterDefinition.IncludeFilter, FILTER_MAX_LENGTH+1);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_INCLUDE, FilterDefinition.IncludeFilter, szConfigFile);
			//排除进程名
			GetDlgItemText(hDlg, IDC_EXCLUDEFILTER, FilterDefinition.ExcludeFilter, FILTER_MAX_LENGTH+1);
			WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_EXCLUDE, FilterDefinition.ExcludeFilter, szConfigFile);
			SetFilter(FilterDefinition);
			//单选框1
			if (SendMessage(GetDlgItem(hDlg, IDC_CHECKRUN), BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "1", szConfigFile);
				LoadTwister(1);
			}
			else
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTORUN, "0", szConfigFile);
			}
			//单选框2
			if (SendMessage(GetDlgItem(hDlg, IDC_CHECKEND), BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "1", szConfigFile);
			}
			else
			{
				WritePrivateProfileString(PROFILE_APP_NAME, PROFILE_KEY_NAME_AUTOEND, "0", szConfigFile);
			}
			MessageBox(hDlg, "新的数值已经设置成功 !", "完成", MB_OK|MB_ICONINFORMATION);
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
