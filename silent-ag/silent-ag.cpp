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

#include "silent-ag.hpp"
#include "../silent-ag-test/utils.h"

// NOTE: the test application might crash in release mode (although it seems 
// to have stopped now) so if it crashes, compile in debug or release-debug
#if _DEBUG || RELEASEDBG
#include <iostream>
#endif

#include <string>
#include <tchar.h>

#if _UNICODE
#define tstring wstring
#else
#define tstring string
#endif

#define jmp(frm, to) (int)(((int)to - (int)frm) - 5)
#define LODWORD(l) ((DWORD)((DWORDLONG)(l)))
#define HIDWORD(l) ((DWORD)(((DWORDLONG)(l)>>32)&0xFFFFFFFF))

namespace silentag
{
	LPCTSTR hook::appname = _T("silent-ag");

	LPVOID hook::pMessageBoxProc = NULL;

	// pointer to the trampoline (or original api if not hooked)
	hook::pfnMessageBox hook::pMessageBox = NULL;

	// return address of the trampoline
	LPVOID hook::pMessageBoxReturn = NULL;

	// address of the relay function that will be allocated near MessageBox to relay a 32-bit jmp to a 64-bit jmp
	LPVOID hook::pRelay = NULL;

	/*
		MessageBoxW (unused at the moment):
		7FAAF010720 - 48 83 EC 38           - sub rsp,38
		7FAAF010724 - 45 33 DB              - xor r11d,r11d
		7FAAF010727 - 44 39 1D 0AD00200     - cmp [7FAAF03D738],r11d
	*/

	/*
		MessageBoxTimeoutW:
		7FF15190638 - FF F3                 - push ebx
		7FF1519063A - 55                    - push rbp
		7FF1519063B - 56                    - push rsi
		7FF1519063C - 57                    - push rdi
	*/

	/*
		Example 64-bit jump to 0xFFFFFFFFBAADF00D:
		something - 68 0DF0ADBA           - push BAADF00D
		004F0005  - C7 44 24 04 FFFFFFFF  - mov [rsp+04],FFFFFFFF
		004F000D  - C3                    - ret 
	*/

	// trampoline to the original api with placeholders for the 64-bit jump to the original API
	BYTE hook::trampoline_MessageBox[] = 
    { 
        0xFF, 0xF3,										// push ebx
        0x55,											// push rbp
        0x56,											// push rsi
        0x57,											// push rdi
        0x68, 0x00, 0x00, 0x00, 0x00,					// push 00000000 ; low DWORD of the ret address
        0xC7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00, // mov [rsp+04],00000000 ; high DWORD of the ret address
        0xC3											// ret
    };

	hook::hook()
	{
		// private ctor to prevent instantation of the static class
	}

	int WINAPI hook::hook_MessageBox(_In_ HWND hWnd, _In_ LPCTSTR lpText, 
		_In_ LPCTSTR lpCaption, _In_ UINT uType, _In_ WORD wLanguageId, _In_ DWORD dwMilliseconds)
	{
		// hooked MessageBox
		// filters out the Aero Glass demo version message

		std::tstring text(lpText);

		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: MessageBox call caught!") << utils::wendl;
		#endif

		// kill demo version message
		if (text.find(_T("Big Muscle")) != std::tstring::npos)
			return IDOK;

		// forward the call to the original api
		return hook::pMessageBox(hWnd, lpText, lpCaption, uType, wLanguageId, dwMilliseconds);
	}

	void hook::attach()
	{
		DWORD dwOldProtect;
		int tries = 0;

		while (!pMessageBoxProc)
		{
		#if _UNICODE
			pMessageBoxProc = utils::SafeGetProcAddress(GetModuleHandle(_T("user32.dll")), "MessageBoxTimeoutW");
		#else
			pMessageBoxProc = utils::SafeGetProcAddress(GetModuleHandle(_T("user32.dll")), "MessageBoxTimeoutA");
		#endif

			Sleep(10);
			tries++;

			if (tries > 200)
			{
				hook::pMessageBox(NULL, _T("Failed to obtain MessageBoxW proc address"), 
					appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);
				return;
			}
		}

		pMessageBox = reinterpret_cast<hook::pfnMessageBox>(hook::pMessageBoxProc);
		pMessageBoxReturn = reinterpret_cast<LPVOID>(reinterpret_cast<LPBYTE>(hook::pMessageBox) + 5);

		// trampoline
		LPDWORD pdwTrampolineRetAddressLow = reinterpret_cast<LPDWORD>(&trampoline_MessageBox[6]);
		LPDWORD pdwTrampolineRetAddressHigh = reinterpret_cast<LPDWORD>(&trampoline_MessageBox[14]);

		// relay func
		LPBYTE pbPush = NULL;
		LPDWORD pdwRelayAddressLow = NULL;
		LPDWORD pdwMovOpcode = NULL;
		LPDWORD pdwRelayAddressHigh = NULL;
		LPBYTE pbRet = NULL;

		// MessageBox
		LPBYTE pbOpcode = reinterpret_cast<LPBYTE>(pMessageBoxProc);
		LPDWORD pdwDistance = reinterpret_cast<LPDWORD>(pbOpcode + 1);

		// make the trampoline bytecode executable
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: setting up trampoline") << utils::wendl 
				   << _T("pMessageBoxProc = ") << pMessageBoxProc << utils::wendl 
				   << _T("pbOpcode = ") << pbOpcode << utils::wendl 
				   << _T("trampoline_MessageBox = ") << &trampoline_MessageBox[0] << utils::wendl;
		#endif

		if (!VirtualProtect(reinterpret_cast<LPVOID>(&trampoline_MessageBox[0]), 
				sizeof(trampoline_MessageBox), PAGE_EXECUTE_READWRITE, &dwOldProtect))
		{
			hook::pMessageBox(NULL, _T("Failed to make the trampoline executable"), 
				appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);
			return;
		}

		// write jmp from trampoline to MessageBox
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: writing trampoline jmp") << utils::wendl;
		#endif

		*pdwTrampolineRetAddressLow = LODWORD(pMessageBoxReturn);
		*pdwTrampolineRetAddressHigh = HIDWORD(pMessageBoxReturn);

		// alloc relay function 15 bytes before MessageBox (there are like 16+ nops before the WinAPI)
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: allocating relay function") << utils::wendl;
		#endif

		if (!VirtualProtect(pbOpcode - 15, 14, PAGE_EXECUTE_READWRITE, &dwOldProtect))
		{
			// this is probabilly unnecessary because VirtualAlloc already does it, but whatever
			hook::pMessageBox(NULL, _T("Failed to make memory writable for the relay function"), 
				appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);
			return;
		}

		pRelay = reinterpret_cast<LPVOID>(pbOpcode - 15);
		VirtualAlloc(pRelay, 14, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		SecureZeroMemory(reinterpret_cast<LPBYTE>(pbOpcode - 15), 14);

		pbPush = reinterpret_cast<LPBYTE>(pRelay);
		pdwRelayAddressLow = reinterpret_cast<LPDWORD>(pbPush + 1);
		pdwMovOpcode = reinterpret_cast<LPDWORD>(pbPush + 5);
		pdwRelayAddressHigh = reinterpret_cast<LPDWORD>(pbPush + 9);
		pbRet = reinterpret_cast<LPBYTE>(pbPush + 13);

		// write relay func
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: writing relay function") << utils::wendl 
				   << _T("pRelay = ") << pRelay << utils::wendl 
				   << _T("hook_MessageBox = ") << hook_MessageBox << utils::wendl;
		#endif

		*pbPush = 0x68; // push
		*pdwRelayAddressLow = LODWORD(hook_MessageBox); // lower dword of the hook address
		*pdwMovOpcode = 0x042444C7; // mov [rsp+04]
		*pdwRelayAddressHigh = HIDWORD(hook_MessageBox); // higher dword of the hook address
		*pbRet = 0xC3; // ret

		// make MessageBoxTimeout writable
		if (!VirtualProtect(pbOpcode, 5, PAGE_EXECUTE_READWRITE, &dwOldProtect))
		{
			hook::pMessageBox(NULL, _T("Failed to make MessageBoxTimeout writable"), 
				appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);
			return;
		}

		// attempt to hook MessageBoxTimeout
		// jmp to the relay function allocated near MessageBox
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: hooking messageboxtimeout") << utils::wendl;
		#endif

		*pbOpcode = 0xE9; // jmp
		*pdwDistance = jmp(pbOpcode, pRelay); // regular 32-bit jmp to relay funcion

		if (*pbOpcode != 0xE9)
			hook::pMessageBox(NULL, _T("Failed to hook MessageBoxTimeout"), 
				appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);

		pMessageBox = reinterpret_cast<pfnMessageBox>(&trampoline_MessageBox[0]);

		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: messageboxtimeout is now hooked") << utils::wendl;
		#endif
	}

	void hook::detach()
	{
		LPBYTE pbCleanOpcode1a = reinterpret_cast<LPBYTE>(&trampoline_MessageBox[0]);
		LPDWORD pdwCleanOpcode1b = reinterpret_cast<LPDWORD>(reinterpret_cast<LPBYTE>(pbCleanOpcode1a) + 1);

		LPBYTE pbOpcode1a = reinterpret_cast<LPBYTE>(pMessageBoxProc);
		LPDWORD pdwOpcode1b = reinterpret_cast<LPDWORD>(reinterpret_cast<LPBYTE>(pbOpcode1a) + 1);

		// attempt to unhook MessageBox
		*pbOpcode1a = *pbCleanOpcode1a;
		*pdwOpcode1b = *pdwCleanOpcode1b;

		// erase relay function
		memset(pbOpcode1a - 15, 0x90, 14);
		VirtualFree(pbOpcode1a - 15, 14, MEM_RELEASE);

		// TODO: restore old memory protection (optional)

		if (*pbOpcode1a != *pbCleanOpcode1a)
			hook::pMessageBox(NULL, _T("Failed to un-hook MessageBoxTimeout"), 
				appname, MB_OK | MB_ICONWARNING, 0, 0x7FFFFFFF);

		pMessageBox = reinterpret_cast<pfnMessageBox>(pMessageBoxProc);
	}
}