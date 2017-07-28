#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <tchar.h>
#include <strsafe.h>
#include "ztime.h"
#include "resource.h"
#include "autobuild.h"

#pragma comment(lib, "advapi32.lib")

#define TC_NAME TEXT("pfzim_TimeChangeControl")

HINSTANCE g_hInstance = NULL;
HWND hwndMainDlg = NULL;
unsigned int WM_TRAY;
NOTIFYICONDATA NotifyIconData;
HMENU hMenu, hMenuZero;

HANDLE g_hEventPopup;
HANDLE g_hEventAction;

long tick = 0;

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HWND g_hWnd = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR *);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR *);
VOID SvcReportEvent(LPTSTR);
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


void SvcInstall()
{
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
	TCHAR szPath[MAX_PATH];
	TCHAR szCmdLine[MAX_PATH + 64] = TEXT("\"");

	if(!GetModuleFileName(NULL, szPath, MAX_PATH))
	{
		//printf("Cannot install service (%d)\n", GetLastError());
		MessageBox(NULL, TEXT("Cannot install service"), TEXT("Failed"), MB_OK);
		return;
	}

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if(schSCManager == NULL)
	{
		//printf("OpenSCManager failed (%d)\n", GetLastError());
		MessageBox(NULL, TEXT("OpenSCManager failed"), TEXT("Failed"), MB_OK);
		return;
	}

	_tcscat(szCmdLine, szPath);
	_tcscat(szCmdLine, TEXT("\" /service"));

	schService = CreateService(
		schSCManager,              // SCM database 
		TC_NAME,                   // name of service 
		L"Time Change Control",     // service name to display 
		SERVICE_ALL_ACCESS,        // desired access 
		SERVICE_WIN32_OWN_PROCESS, // service type 
		SERVICE_AUTO_START,      // start type 
		SERVICE_ERROR_NORMAL,      // error control type 
		szCmdLine,                    // path to service's binary 
		NULL,                      // no load ordering group 
		NULL,                      // no tag identifier 
		NULL,                      // no dependencies 
		NULL,                      // LocalSystem account 
		NULL);                     // no password 

	if(schService == NULL)
	{
		//printf("CreateService failed (%d)\n", GetLastError());
		MessageBox(NULL, TEXT("CreateService failed"), TEXT("Failed"), MB_OK);
		CloseServiceHandle(schSCManager);
		return;
	}
	else
	{
		HKEY hSubKey;

		StartService(schService, 0, NULL);

		if(RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), &hSubKey) == ERROR_SUCCESS)
		{
			RegSetValueEx(hSubKey, TEXT("pfzim_TimeChangeControl"), NULL, REG_SZ, (BYTE*)szPath, (_tcslen(szPath)+1) * sizeof(szPath[0]));
			RegCloseKey(hSubKey);
		}
		MessageBox(NULL, TEXT("Service installed successfully"), TEXT("OK"), MB_OK);
	}

	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}

void SvcUninstall()
{
	SC_HANDLE schSCManager;
	SC_HANDLE schService;

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if(schSCManager == NULL)
	{
		//printf("OpenSCManager failed (%d)\n", GetLastError());
		MessageBox(NULL, TEXT("OpenSCManager failed"), TEXT("Failed"), MB_OK);
		return;
	}


	schService = OpenService(schSCManager, TC_NAME, SERVICE_STOP | DELETE);

	if(schService == NULL)
	{
		//printf("CreateService failed (%d)\n", GetLastError());
		MessageBox(NULL, TEXT("OpenService failed"), TEXT("Failed"), MB_OK);
		CloseServiceHandle(schSCManager);
		return;
	}
	else
	{
		HKEY hSubKey;

		if(DeleteService(schService))
		{
			if(RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), &hSubKey) == ERROR_SUCCESS)
			{
				RegDeleteValue(hSubKey, TEXT("pfzim_TimeChangeControl"));
				RegCloseKey(hSubKey);
			}
			MessageBox(NULL, TEXT("Service uninstalled successfully"), TEXT("OK"), MB_OK);
		}
		else
		{
			MessageBox(NULL, TEXT("Delete service failed"), TEXT("Error"), MB_OK);
		}
	}

	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}

void WINAPI SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
	gSvcStatusHandle = RegisterServiceCtrlHandler(TC_NAME, SvcCtrlHandler);

	if(!gSvcStatusHandle)
	{
		SvcReportEvent(TEXT("RegisterServiceCtrlHandler"));
		return;
	}

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwServiceSpecificExitCode = 0;

	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

	SvcInit(dwArgc, lpszArgv);
}

void SvcInit(DWORD dwArgc, LPTSTR *lpszArgv)
{
	MyRegisterClass(NULL);

	if(!InitInstance(NULL, SW_HIDE))
	{
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}

	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	// Setup event with permissions for anyone

	if(!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
	{
	}
	if(!SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE))
	{
	}
	sa.nLength = sizeof(sd);
	sa.bInheritHandle = FALSE;
	sa.lpSecurityDescriptor = &sd;

	g_hEventPopup = CreateEvent(&sa, TRUE, FALSE, L"Global\\" TC_NAME "Popup");
	g_hEventAction = CreateEvent(&sa, TRUE, FALSE, L"Global\\" TC_NAME "Action");

	MSG msg;

	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

	while(GetMessage(&msg, nullptr, 0, 0))
	{
		//if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	CloseHandle(g_hEventAction);
	CloseHandle(g_hEventPopup);

	ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

void ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;

	// Fill in the SERVICE_STATUS structure.

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if(dwCurrentState == SERVICE_START_PENDING)
		gSvcStatus.dwControlsAccepted = 0;
	else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if((dwCurrentState == SERVICE_RUNNING) ||
		(dwCurrentState == SERVICE_STOPPED))
		gSvcStatus.dwCheckPoint = 0;
	else gSvcStatus.dwCheckPoint = dwCheckPoint++;

	// Report the status of the service to the SCM.
	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

void WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
	switch(dwCtrl)
	{
	case SERVICE_CONTROL_STOP:
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

		SendMessage(g_hWnd, WM_CLOSE, 0, 0);

		ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

		return;

	case SERVICE_CONTROL_INTERROGATE:
		break;

	default:
		break;
	}

}

void SvcReportEvent(LPTSTR szFunction)
{
	//*
	HANDLE hEventSource;
	LPCTSTR lpszStrings[2];
	TCHAR Buffer[80];

	hEventSource = RegisterEventSource(NULL, TC_NAME);

	if(hEventSource != NULL)
	{
		StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

		lpszStrings[0] = TC_NAME;
		lpszStrings[1] = Buffer;

		ReportEvent(hEventSource,        // event log handle
			EVENTLOG_ERROR_TYPE, // event type
			0,                   // event category
			0,					 // event identifier
			NULL,                // no security identifier
			2,                   // size of lpszStrings array
			0,                   // no binary data
			lpszStrings,         // array of strings
			NULL);               // no binary data

		DeregisterEventSource(hEventSource);
	}
	//*/
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	wcex.hIconSm = wcex.hIcon;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = TC_NAME "Class";

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	//hInst = hInstance; // Store instance handle in our global variable

	g_hWnd = CreateWindow(TC_NAME "Class", TC_NAME "Title", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

	if(!g_hWnd)
	{
		return FALSE;
	}

	//ShowWindow(g_hWnd, nCmdShow);
	//UpdateWindow(g_hWnd);

	return TRUE;
}

SYSTEMTIME last_system_time[2];

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_TIMECHANGE:
		KillTimer(hWnd, 1);
		// compare 
		SYSTEMTIME new_system_time;
		memcpy(&last_system_time[1], &last_system_time[0], sizeof(last_system_time[1]));

		GetSystemTime(&new_system_time);
		// Detect time change direction (backward or forward) (limit 54000 seconds)
		if(timecompare(&new_system_time, &last_system_time[1], 1) > 0) // forward
		{
			timeincminutes(&last_system_time[1], 180);
			if(timecompare(&new_system_time, &last_system_time[1], 1) > 0)
			{
				// warning here!
				ResetEvent(g_hEventAction);
				SetEvent(g_hEventPopup);
				if(WaitForSingleObject(g_hEventAction, 15000) != WAIT_OBJECT_0)
				{
					timeincseconds(&last_system_time[0], 60);
					SetSystemTime(&last_system_time[0]);
				}
			}
		}
		else // backward
		{
			timeincminutes(&new_system_time, 180);
			if(timecompare(&new_system_time, &last_system_time[1], 1) < 0)
			{
				// warning here!
				ResetEvent(g_hEventAction);
				SetEvent(g_hEventPopup);
				if(WaitForSingleObject(g_hEventAction, 15000) != WAIT_OBJECT_0)
				{
					timeincseconds(&last_system_time[0], 60);
					SetSystemTime(&last_system_time[0]);
				}
			}
		}

		SetTimer(hWnd, 1, 60000, NULL);
		break;
	case WM_TIMER:
		memcpy(&last_system_time[0], &last_system_time[1], sizeof(last_system_time[0]));
		GetSystemTime(&last_system_time[1]);

		SetTimer(hWnd, 1, 60000, NULL);
		break;
	case WM_CREATE:
		GetSystemTime(&last_system_time[0]);
		memcpy(&last_system_time[1], &last_system_time[0], sizeof(last_system_time[1]));

		SetTimer(hWnd, 1, 60000, NULL);

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}



DWORD WINAPI ThreadProc(LPVOID lpParam)
{
	ResetEvent(g_hEventPopup);
	while(1)
	{
		if(WaitForSingleObject(g_hEventPopup, INFINITE) == WAIT_OBJECT_0)
		{
			ResetEvent(g_hEventPopup);
			//PostMessage(hwndMainDlg, WM_TIMECONFIRM, 0, 0L);
			tick = 15;
			ShowWindow(hwndMainDlg, SW_SHOW);
			SetForegroundWindow(hwndMainDlg);
			SetTimer(hwndMainDlg, 1, 1000, NULL);
		}
	}
}

INT_PTR CALLBACK DlgProc_About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch(message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

BOOL MainDlg_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	hwndMainDlg = hwnd;

	return TRUE;
}

void MainDlg_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch(id)
	{
	case ID_MAIN_ABOUT:
		DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUT), hwnd, DlgProc_About);
		break;
	case ID_MAIN_EXIT:
		DestroyWindow(hwnd);
		PostQuitMessage(0);
		break;
	case IDC_ACCEPT:
		SetEvent(g_hEventAction);
		KillTimer(hwnd, 1);
		ShowWindow(hwnd, SW_HIDE);
		break;
	case IDC_RESTORE:
		tick = 0;
		break;
	case IDCANCEL:
		break;
	}
}

void MainDlg_TrayIconNotify(HWND hwnd, LONG wParam, LONG lParam)
{
	// wParam - icon uID;
	// lParam - WM_MESSAGE;
	switch(wParam)
	{
	case 1:
		switch(lParam)
		{
		case WM_LBUTTONDBLCLK:
			DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUT), hwnd, DlgProc_About);
			break;
		case WM_RBUTTONDOWN:
			{
				POINT pt;
				GetCursorPos(&pt);
				SetForegroundWindow(hwnd);
				TrackPopupMenu(hMenuZero, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
				PostMessage(hwnd, WM_NULL, 0, 0L);
			}
		break;
		}
		break;
	}
}

INT_PTR CALLBACK MainDlg_DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
		HANDLE_MSG(hwnd, WM_COMMAND, MainDlg_OnCommand);
		HANDLE_MSG(hwnd, WM_INITDIALOG, MainDlg_OnInitDialog);
	case WM_QUERYENDSESSION:
		//If an application can terminate conveniently, it
		//should return TRUE; otherwise, it should return FALSE.
		SetWindowLong(hwnd, DWL_MSGRESULT, TRUE);
		return 1L;
	case WM_ENDSESSION:
		DestroyWindow(hwndMainDlg);
		PostQuitMessage(0);
		break;
	case WM_LBUTTONDOWN:
		PostMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
		break;
	case WM_TIMER:
		if(tick > 0)
		{
			tick--;
			TCHAR Buffer[80];
			SYSTEMTIME st;
			GetLocalTime(&st);
			StringCchPrintf(Buffer, 80, TEXT("Установлено время: %.2d.%.2d.%.4d %.2d:%.2d"), st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
			SetDlgItemText(hwnd, IDC_TIME, Buffer);
			StringCchPrintf(Buffer, 80, TEXT("Вернуть предыдущее значение (%d)"), tick);
			SetDlgItemText(hwnd, IDC_RESTORE, Buffer);
			SetTimer(hwnd, 1, 1000, NULL);
		}
		else
		{
			//ResetEvent(g_hEventPopup);
			SetDlgItemText(hwnd, IDC_RESTORE, TEXT("Вернуть предыдущее значение (15)"));
			ShowWindow(hwnd, SW_HIDE);
		}
		break;
	default:
		if(uMsg == WM_TRAY)
		{
			MainDlg_TrayIconNotify(hwnd, wParam, lParam);
			/*
			tick = 15;
			TCHAR Buffer[80];
			SYSTEMTIME st;
			GetLocalTime(&st);
			StringCchPrintf(Buffer, 80, TEXT("Новое время: %.2d.%.2d.%.4d %.2d:%.2d"), st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
			SetDlgItemText(hwnd, IDC_TIME, Buffer);
			ShowWindow(hwndMainDlg, SW_SHOW);
			SetForegroundWindow(hwndMainDlg);
			SetTimer(hwndMainDlg, 1, 1000, NULL);
			*/
		}
	}
	return 0L;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	g_hInstance = hInstance;

	HANDLE hThread;
	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	if(strlen(lpCmdLine) > 0)
	{
		if(strcmp(lpCmdLine, "/install") == 0)
		{
			//printf("Installing service...");
			SvcInstall();
			return 0;
		}
		else if(strcmp(lpCmdLine, "/uninstall") == 0)
		{
			//printf("Installing service...");
			SvcUninstall();
			return 0;
		}
		else if(strcmp(lpCmdLine, "/service") == 0)
		{
			SERVICE_TABLE_ENTRY DispatchTable[] =
			{
				{TC_NAME,		(LPSERVICE_MAIN_FUNCTION)SvcMain},
				{NULL,			NULL}
			};

			if(!StartServiceCtrlDispatcher(DispatchTable))
			{
				SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
			}

			return 0;
		}
		else
		{
			MessageBox(NULL, TEXT("Invalid parameter"), TEXT("ERROR"), MB_OK);
			return 1;
		}
	}
	// Setup event with permissions for anyone

	if(!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
	{
	}
	if(!SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE))
	{
	}
	sa.nLength = sizeof(sd);
	sa.bInheritHandle = FALSE;
	sa.lpSecurityDescriptor = &sd;

	g_hEventPopup = CreateEvent(&sa, TRUE, FALSE, L"Global\\" TC_NAME "Popup");
	g_hEventAction = CreateEvent(&sa, TRUE, FALSE, L"Global\\" TC_NAME "Action");

	WM_TRAY = RegisterWindowMessage(TEXT("pfzim_TimeChangeControlMessage"));
	hwndMainDlg = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAINDLG), NULL, (DLGPROC)MainDlg_DlgProc);

	hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_MENU));
	hMenuZero = GetSubMenu(hMenu, 0);

	hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);

	NotifyIconData.cbSize = sizeof(NOTIFYICONDATA);
	NotifyIconData.hWnd = hwndMainDlg;
	NotifyIconData.uID = 1;
	NotifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	NotifyIconData.uCallbackMessage = WM_TRAY;
	NotifyIconData.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	lstrcpy(NotifyIconData.szTip, TEXT("Time Change Control"));
	Shell_NotifyIcon(NIM_ADD, &NotifyIconData);

	while(GetMessage(&msg, NULL, 0L, 0L) > 0)
	{
		if(!IsDialogMessage(hwndMainDlg, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	Shell_NotifyIcon(NIM_DELETE, &NotifyIconData);
	CloseHandle(hThread);

	return 0;
}
