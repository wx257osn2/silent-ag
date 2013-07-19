/*
	Copyright 2013 Francesco "Franc[e]sco" Noferi (francesco1149@gmail.com)

	This file is part of silent-ag.

	silent-ag is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	silent-ag is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with silent-ag.  If not, see <http://www.gnu.org/licenses/>.
*/

// NOTE: this test application will crash in release mode (I still don't know why)
// so if you want to use it, compile in debug or release-debug

#include "utils.h"
#include <iostream>
#include <Windows.h>
#include <tchar.h>

typedef int (WINAPI *pfnMessageBoxTimeout)(_In_ HWND hWnd, _In_ LPCTSTR lpText, 
	_In_ LPCTSTR lpCaption, _In_ UINT uType, _In_ WORD wLanguageId, _In_ DWORD dwMilliseconds);

int MessageBoxTimeout(_In_ HWND hWnd, _In_ LPCTSTR lpText, 
	_In_ LPCTSTR lpCaption, _In_ UINT uType, _In_ WORD wLanguageId, _In_ DWORD dwMilliseconds)
{
	static pfnMessageBoxTimeout pMessageBoxTimeout = NULL;

	if (!pMessageBoxTimeout)
	{
	#if _UNICODE
		pMessageBoxTimeout = reinterpret_cast<pfnMessageBoxTimeout>(utils::SafeGetProcAddress(GetModuleHandle(_T("user32.dll")), "MessageBoxTimeoutW"));
	#else
		pMessageBoxTimeout = reinterpret_cast<pfnMessageBoxTimeout>(utils::SafeGetProcAddress(GetModuleHandle(_T("user32.dll")), "MessageBoxTimeoutA"));
	#endif

		// getprocaddress failed
		if (!pMessageBoxTimeout)
		{
			std::tcout << _T("failed to get the proc address for MessageBoxTimeout") << utils::wendl;
			std::cin.get();
			exit(0);
		}
	}

	return pMessageBoxTimeout(hWnd, lpText, lpCaption, uType, wLanguageId, dwMilliseconds);
}

void testboxes()
{
	MessageBoxTimeout(NULL, _T("This messagebox should remain uncaught"), _T("Some messagebox"), MB_OK | MB_ICONINFORMATION, 0, 0x7FFFFFFF);

	MessageBoxTimeout(NULL, _T("This is a DEMO version of Aero Glass for Win8 v0.95.\n!!! USE AT YOUR OWN RISK !!!\n\n") 
		_T("Copyright (C) 2013 by Big Muscle"), _T("Aero Glass for DWM"), MB_OK | MB_ICONINFORMATION, 0, 0x7FFFFFFF);
}

int _tmain(int argc, _TCHAR *argv[])
{
	LoadLibrary(_T("user32.dll")); // console apps don't load user32 unless you call stuff from it
	std::tcout << _T("starting the un-hooked test") << utils::wendl;
	testboxes();
	std::tcout << _T("simulating dll injection") << utils::wendl;
	LoadLibrary(_T("silent-ag.dll")); // simulating injection
	std::tcout << _T("starting the hooked test") << utils::wendl;
	testboxes();

	return 0;
}