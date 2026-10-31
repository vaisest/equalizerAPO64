/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <Imagehlp.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "VST3PluginLibrary.h"

using namespace std;

// VST3 modules export GetPluginFactory, VST2 modules export VSTPluginMain
bool VST3PluginLibrary::isVST3(const wstring& libPath)
{
	DWORD attributes = GetFileAttributesW(libPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return false;
	if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		return true;

	bool result = false;

	HANDLE hFile = CreateFileW(libPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hMap != NULL)
		{
			void* mapAddr = MapViewOfFileEx(hMap, FILE_MAP_READ, 0, 0, 0, NULL);
			if (mapAddr != NULL)
			{
				result = exportsPluginFactory(mapAddr);
				UnmapViewOfFile(mapAddr);
			}
			CloseHandle(hMap);
		}
		CloseHandle(hFile);
	}

	return result;
}

bool VST3PluginLibrary::exportsPluginFactory(void* fileBase)
{
	bool result = false;

	__try
	{
		PIMAGE_NT_HEADERS ntHeaders = ImageNtHeader(fileBase);
		ULONG size;
		PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)ImageDirectoryEntryToData(fileBase, FALSE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size);
		if (ntHeaders != NULL && exports != NULL)
		{
			DWORD* names = (DWORD*)ImageRvaToVa(ntHeaders, fileBase, exports->AddressOfNames, NULL);
			for (DWORD i = 0; names != NULL && i < exports->NumberOfNames && !result; i++)
			{
				const char* name = (const char*)ImageRvaToVa(ntHeaders, fileBase, names[i], NULL);
				result = name != NULL && strcmp(name, "GetPluginFactory") == 0;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// malformed file
		result = false;
	}

	return result;
}

VST3PluginLibrary::VST3PluginLibrary(const wstring& libPath)
	: VSTPluginLibrary(getModulePath(libPath))
{
}

VST3PluginLibrary::~VST3PluginLibrary()
{
	if (factory != NULL)
	{
		factory->release();
		factory = NULL;
	}

	if (ExitDll != NULL)
		ExitDll();
}

Steinberg::IPluginFactory* VST3PluginLibrary::getFactory()
{
	return factory;
}

bool VST3PluginLibrary::loadFunctions()
{
	GetPluginFactory = (getPluginFactoryFunc)GetProcAddress(module, "GetPluginFactory");
	InitDll = (dllFunc)GetProcAddress(module, "InitDll");
	ExitDll = (dllFunc)GetProcAddress(module, "ExitDll");

	return GetPluginFactory != NULL;
}

int VST3PluginLibrary::customInitialize()
{
	if (InitDll != NULL && !InitDll())
	{
		ExitDll = NULL;
		return LOADING_FAILED;
	}

	factory = GetPluginFactory();
	if (factory == NULL)
		return FUNCTIONS_MISSING;

	return 0;
}

// a bundle is a directory containing the module for each architecture
wstring VST3PluginLibrary::getModulePath(const wstring& libPath)
{
	DWORD attributes = GetFileAttributesW(libPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return libPath;

#if defined(_M_ARM64)
	const wchar_t* archs[] = {L"arm64-win", L"arm64x-win"};
#elif defined(_WIN64)
	const wchar_t* archs[] = {L"x86_64-win"};
#else
	const wchar_t* archs[] = {L"x86-win"};
#endif
	wstring modulePath;
	for (const wchar_t* arch : archs)
	{
		modulePath = libPath + L"\\Contents\\" + arch + L"\\" + PathFindFileNameW(libPath.c_str());
		if (GetFileAttributesW(modulePath.c_str()) != INVALID_FILE_ATTRIBUTES)
			break;
	}

	return modulePath;
}
