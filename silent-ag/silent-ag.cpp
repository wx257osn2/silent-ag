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

// NOTE: the test application will crash in release mode (I still don't know why)
// so if you want to use it, compile in debug or release-debug
#if _DEBUG || RELEASEDBG
#include "../silent-ag-test/utils.h"
#include <iostream>
#endif

#include <string>
#include <tchar.h>
#include <winternl.h>
#include <stddef.h>

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
		MessageBoxW:
		7FAAF010720 - 48 83 EC 38           - sub rsp,38
		7FAAF010724 - 45 33 DB              - xor r11d,r11d
		7FAAF010727 - 44 39 1D 0AD00200     - cmp [7FAAF03D738],r11d
	*/

	/*
		Example 64-bit jump to 0xFFFFFFFFBAADF00D:
		something - 68 0DF0ADBA           - push BAADF00D
		004F0005  - C7 44 24 04 FFFFFFFF  - mov [rsp+04],FFFFFFFF
		004F000D  - C3                    - ret 
	*/

	// trampoline to the original api (with 4 trailing zeros that will be replaced by a jmp to MessageBox)
	BYTE hook::trampoline_MessageBox[] = 
	{ 
		0x48, 0x83, 0xEC, 0x38,										// sub rsp,38
		0x45, 0x33, 0xDB,											// xor r11d,r11d
		0x68, 0x00, 0x00, 0x00, 0x00,								// push 00000000 ; low DWORD of the ret address
		0xC7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00,				// mov [rsp+04],00000000 ; high DWORD of the ret address
		0xC3														// ret
	};

	hook::hook()
	{
		// private ctor to prevent instantation of the static class
	}

	int WINAPI hook::hook_MessageBox(_In_opt_ HWND hWnd, _In_opt_ LPCTSTR lpText, 
			_In_opt_ LPCTSTR lpCaption, _In_ UINT uType)
	{
		// hooked MessageBox
		// filters out the Aero Glass demo version message

		std::tstring caption(lpCaption);
		std::tstring text(lpText);

		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: MessageBox call caught!") << utils::wendl;
		#endif

		// kill demo version message
		if (text.find(_T("Big Muscle")) != std::tstring::npos)
			return IDOK;

		// forward the call to the original api
		return hook::pMessageBox(hWnd, lpText, lpCaption, uType);
	}

	FARPROC hook::SafeGetProcAddress(HMODULE module, LPCSTR name)
	{
		// credits to Nigel Bree for this workaround to GetProcAdress redirection
		// not sure if it's actually needed but sometimes GetProcAddress calls are redirected 
		// to apphelp.dll or something for no reason

		typedef unsigned long (WINAPI *LGPA)(LPVOID base,
			ANSI_STRING *name, DWORD ordinal, LPVOID *result);

		static LGPA lgpa;
		static HMODULE ntdll;

		if (!ntdll)
		{
			ntdll = GetModuleHandle(_T("NTDLL.DLL"));
			lgpa = reinterpret_cast<LGPA>(GetProcAddress(ntdll, "LdrGetProcedureAddress"));
		}

		ANSI_STRING proc;
		proc.Length = strlen(name);
		proc.MaximumLength = proc.Length + 1;
		proc.Buffer = const_cast<PCHAR>(name);

		LPVOID result;
		NTSTATUS status;
		status = (*lgpa)(module, & proc, 0, &result);
		
		if (status)
			return 0;

		return reinterpret_cast<FARPROC>(result);
	}

	void hook::attach()
	{
		DWORD dwOldProtect;
		int tries = 0;

		while (!pMessageBoxProc)
		{
			pMessageBoxProc = SafeGetProcAddress(GetModuleHandle(_T("user32.dll")), "MessageBoxW");
			Sleep(10);

			tries++;

			if (tries > 200)
			{
				hook::pMessageBox(NULL, _T("Failed to obtain MessageBoxW proc address"), 
					appname, MB_OK | MB_ICONWARNING);
				return;
			}
		}

		pMessageBox = reinterpret_cast<hook::pfnMessageBox>(hook::pMessageBoxProc);
		pMessageBoxReturn = reinterpret_cast<LPVOID>(reinterpret_cast<LPBYTE>(hook::pMessageBox) + 7);

		// trampoline
		LPDWORD pdwTrampolineRetAddressLow = reinterpret_cast<LPDWORD>(&trampoline_MessageBox[8]);
		LPDWORD pdwTrampolineRetAddressHigh = reinterpret_cast<LPDWORD>(&trampoline_MessageBox[16]);

		// relay func
		LPBYTE pbPush = NULL;
		LPDWORD pdwRelayAddressLow = NULL;
		LPDWORD pdwMovOpcode = NULL;
		LPDWORD pdwRelayAddressHigh = NULL;
		LPBYTE pbRet = NULL;

		// MessageBox
		LPBYTE pbOpcode = reinterpret_cast<LPBYTE>(pMessageBoxProc);
		LPDWORD pdwDistance = reinterpret_cast<LPDWORD>(pbOpcode + 1);
		LPWORD pwNops = reinterpret_cast<LPWORD>(pbOpcode + 5);

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
				appname, MB_OK | MB_ICONWARNING);
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
				appname, MB_OK | MB_ICONWARNING);
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

		// make MessageBox writable
		if (!VirtualProtect(pbOpcode, 7, PAGE_EXECUTE_READWRITE, &dwOldProtect))
		{
			hook::pMessageBox(NULL, _T("Failed to make MessageBox writable"), appname, MB_OK | MB_ICONWARNING);
			return;
		}

		// attempt to hook MessageBox
		// jmp to the relay function allocated near MessageBox
		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: hooking messagebox") << utils::wendl;
		#endif

		*pbOpcode = 0xE9; // jmp
		*pdwDistance = jmp(pbOpcode, pRelay); // regular 32-bit jmp to relay funcion
		*pwNops = 0x9090; // 2 nops to fill the truncated opcode

		if (*pbOpcode != 0xE9)
			hook::pMessageBox(NULL, _T("Failed to hook MessageBox"), appname, MB_OK | MB_ICONWARNING);

		pMessageBox = reinterpret_cast<pfnMessageBox>(&trampoline_MessageBox[0]);

		#if _DEBUG || RELEASEDBG
		std::wcout << _T("dll: messagebox is now hooked") << utils::wendl;
		#endif
	}

	void hook::detach()
	{
		LPDWORD pdwCleanOpcode1 = reinterpret_cast<LPDWORD>(&trampoline_MessageBox[0]);
		LPWORD pwCleanOpcode2a = reinterpret_cast<LPWORD>(reinterpret_cast<LPBYTE>(pdwCleanOpcode1) + 4);
		LPBYTE pbCleanOpcode2b = reinterpret_cast<LPBYTE>(pdwCleanOpcode1) + 6;

		LPDWORD pdwOpcode1 = reinterpret_cast<LPDWORD>(pMessageBoxProc);
		LPWORD pwOpcode2a = reinterpret_cast<LPWORD>(reinterpret_cast<LPBYTE>(pdwOpcode1) + 4);
		LPBYTE pbOpcode2b = reinterpret_cast<LPBYTE>(pdwOpcode1) + 6;

		// attempt to unhook MessageBox
		*pdwOpcode1 = *pdwCleanOpcode1;
		*pwOpcode2a = *pwCleanOpcode2a;
		*pbOpcode2b = *pbCleanOpcode2b;

		// erase relay function
		memset(reinterpret_cast<LPBYTE>(pdwOpcode1) - 15, 0x90, 14);
		VirtualFree(reinterpret_cast<LPBYTE>(pdwOpcode1) - 15, 14, MEM_RELEASE);

		// TODO: restore old memory protection (optional)

		if (*pdwOpcode1 != *pdwCleanOpcode1)
			hook::pMessageBox(NULL, _T("Failed to un-hook MessageBox"), appname, MB_OK | MB_ICONWARNING);

		pMessageBox = reinterpret_cast<pfnMessageBox>(pMessageBoxProc);
	}
}