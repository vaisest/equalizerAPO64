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

#pragma once

#include "VSTPluginLibrary.h"

namespace Steinberg {
class IPluginFactory;
}

class VST3PluginLibrary : public VSTPluginLibrary
{
public:
	static bool isVST3(const std::wstring& libPath);

	VST3PluginLibrary(const std::wstring& libPath);
	~VST3PluginLibrary();

	Steinberg::IPluginFactory* getFactory();

protected:
	bool loadFunctions() override;
	int customInitialize() override;

private:
	static std::wstring getModulePath(const std::wstring& libPath);
	static bool exportsPluginFactory(void* fileBase);

	typedef Steinberg::IPluginFactory* (__stdcall* getPluginFactoryFunc)();
	typedef bool (__stdcall* dllFunc)();
	getPluginFactoryFunc GetPluginFactory = NULL;
	dllFunc InitDll = NULL;
	dllFunc ExitDll = NULL;
	Steinberg::IPluginFactory* factory = NULL;
};
