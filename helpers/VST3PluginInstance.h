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

#include <vector>
// before the VST3 headers, which define VST_VERSION as a macro
#include "VSTPluginInstance.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

class VST3PluginLibrary;

class VST3PluginInstance : public VSTPluginInstance, public Steinberg::Vst::IComponentHandler, public Steinberg::IPlugFrame
{
public:
	VST3PluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
	~VST3PluginInstance();

	bool initialize() override;

	int numInputs() const override;
	int numOutputs() const override;
	bool canReplacing() const override;
	int uniqueID() const override;
	std::wstring getName() const override;
	bool canDoubleReplacing() const override;

	void prepareForProcessing(float sampleRate, int blockSize) override;
	void writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap) override;
	void readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const override;

	void startProcessing() override;
	void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount) override;
	void processReplacing(float** inputArray, float** outputArray, int frameCount) override;
	void stopProcessing() override;

	void startEditing(HWND hWnd, short* width, short* height) override;
	void stopEditing() override;

	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override;
	Steinberg::uint32 PLUGIN_API release() override;

	Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) override;
	Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

	Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

private:
	bool initializeInternal();
	void processInternal(void** inputArray, void** outputArray, int frameCount);
	void flushParameters() const;

	VST3PluginLibrary* vst3Library;
	Steinberg::IPtr<Steinberg::FUnknown> hostContext;
	Steinberg::IPtr<Steinberg::Vst::IComponent> component;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
	Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
	Steinberg::IPtr<Steinberg::IPlugView> view;
	bool separateController = false;
	Steinberg::TUID classID = {0};
	std::string name;
	int inputChannelCount = 0;
	int outputChannelCount = 0;
	bool doublePrecision = false;
	bool processing = false;

	mutable Steinberg::Vst::HostProcessData processData;
	mutable Steinberg::Vst::ParameterChanges inputChanges;
	Steinberg::Vst::ProcessContext processContext = {};
	std::vector<double> silentBuffer;
	std::vector<double> discardBuffer;
};
