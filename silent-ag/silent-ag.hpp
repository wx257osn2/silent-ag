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

#pragma once

#include <Windows.h>

namespace silentag
{
	class hook
	{
	public:	
		static void attach();
		static void detach();
		
	protected:
		typedef int (WINAPI *pfnMessageBox)(_In_opt_ HWND hWnd, _In_opt_ LPCTSTR lpText, 
			_In_opt_  LPCTSTR lpCaption, _In_ UINT uType);

		static LPCTSTR appname;
		static LPVOID pMessageBoxProc;
		static pfnMessageBox pMessageBox;
		static LPVOID pMessageBoxReturn;
		static LPVOID pRelay;
		static BYTE trampoline_MessageBox[21];

		hook();
		static int WINAPI hook_MessageBox(_In_opt_ HWND hWnd, _In_opt_ LPCTSTR lpText, 
			_In_opt_  LPCTSTR lpCaption, _In_ UINT uType);
		static FARPROC SafeGetProcAddress(HMODULE module, LPCSTR name);
	};
}