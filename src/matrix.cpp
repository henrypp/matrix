/************************************
*	Matrix Screensaver
*	Copyright © 2011 Henry++
*************************************/

// Unicode
#ifndef UNICODE
#define UNICODE
#endif

// Include
#include <windows.h>
#include <commctrl.h>

#include <stdio.h>

#include "matrix.h"
#include "resource.h"

// Manifest
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

void _ShowCursor(BOOL bShow)
{
	if (bShow)
		while(ShowCursor(1)<=0);
	else
		while(ShowCursor(0)>=0);
};

LRESULT CALLBACK ScreensaverProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	MATRIX* matrix = GetMatrix(hwndDlg);

	switch(uMsg)
	{
		case WM_NCCREATE:
			_ShowCursor(0);

			matrix = CreateMatrix(hwndDlg, ((CREATESTRUCT*)lParam)->cx, ((CREATESTRUCT*)lParam)->cy);

			if(!matrix)
			{
				MessageBox(hwndDlg, L"Ошибка работы скринсейвера", L"Ошибка", MB_OK | MB_ICONSTOP);
				return 0;
			}

			SetMatrix(hwndDlg, matrix);
			SetTimer(hwndDlg, 0xdeadbeef, ((SPEED_MAX - g_nMatrixSpeed) + SPEED_MIN) * 10, 0);

			return 1;

		case WM_TIMER:
			DecodeMatrix(hwndDlg, matrix);
			break;

		case WM_KEYDOWN:
			if(wParam == VK_ESCAPE)
				SendMessage(hwndDlg, WM_CLOSE, 0, 0);

			break;

		case WM_CLOSE:
			_ShowCursor(1);
			DestroyMatrix(matrix);

			DestroyWindow(hwndDlg);
			PostQuitMessage(0);

			break;

		default:
			return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
	}

	return 0;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, RECT* rc, LPARAM lParam)
{
	HWND hWnd = CreateWindowEx(WS_EX_TOPMOST, APP_NAME_SHORT, APP_NAME L" " APP_VERSION, WS_POPUP | WS_VISIBLE, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, NULL, NULL, GetModuleHandle(0), NULL);
	SetWindowPos(hWnd, 0, rc->left, rc->top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW);

	return 1;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
	MSG msg = {0};
	WNDCLASSEX wcex = {sizeof(wcex)};
	INITCOMMONCONTROLSEX icex = {sizeof(icex)};

	// Init Controls Library
	icex.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icex);

	// Check Already Running
	HANDLE hMutex = CreateMutex(NULL, TRUE, APP_NAME_SHORT);

	if(GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(hMutex);
		return 0;
	}

	// Parse Command Line
	if(!wcsnicmp(L"/s", lpCmdLine, 2))
	{
		// Create Window Class
		wcex.hInstance = hInstance;
		wcex.lpszClassName = APP_NAME_SHORT;
		wcex.lpfnWndProc = ScreensaverProc;
		wcex.style = 0;
		wcex.cbWndExtra = sizeof(MATRIX*);
		wcex.cbClsExtra		= 0;
		wcex.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_MAIN));
		wcex.hIconSm = LoadIcon(NULL, MAKEINTRESOURCE(IDI_MAIN));
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.lpszMenuName = NULL;
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

		if(!RegisterClassEx(&wcex))
			return 0;

		_crc_reg = (WORD)GetTickCount();

		// Multi-Monitor Support
		EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);

		while(GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	else if(!wcsnicmp(L"/c", lpCmdLine, 2) || !wcslen(lpCmdLine))
	{
		// Configuration
		MessageBox(0, L"Настройки отсутствуют", L"Ошибка", MB_OK | MB_ICONWARNING);
	}
	else
	{
		return 0;
	}
	
	return msg.wParam;
}