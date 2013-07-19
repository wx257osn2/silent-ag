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

#include "utils.h"
#include <tchar.h>

namespace utils
{
	std::wostream& wendl(std::wostream& out) 
	{
		return out.put(L'\r').put(L'\n').flush();
	}

	FARPROC SafeGetProcAddress(HMODULE module, LPCSTR name)
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
}
