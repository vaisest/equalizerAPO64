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
#include <wincrypt.h>
#include "StringHelper.h"
#include "VST3PluginLibrary.h"
#include "VST3PluginInstance.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

VST3PluginInstance::VST3PluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
	: VSTPluginInstance(library, processLevel), vst3Library(static_cast<VST3PluginLibrary*>(library.get()))
{
}

VST3PluginInstance::~VST3PluginInstance()
{
	stopEditing();
	stopProcessing();

	if (controller)
	{
		controller->setComponentHandler(nullptr);
		if (separateController)
		{
			FUnknownPtr<Vst::IConnectionPoint> componentConnection(component);
			FUnknownPtr<Vst::IConnectionPoint> controllerConnection(controller);
			if (componentConnection && controllerConnection)
			{
				componentConnection->disconnect(controllerConnection);
				controllerConnection->disconnect(componentConnection);
			}
			controller->terminate();
		}
		controller = nullptr;
	}

	processor = nullptr;
	if (component)
	{
		component->terminate();
		component = nullptr;
	}
}

bool VST3PluginInstance::initialize()
{
	bool result = false;

	__try
	{
		result = initializeInternal();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = false;
	}

	return result;
}

bool VST3PluginInstance::initializeInternal()
{
	IPluginFactory* factory = vst3Library->getFactory();
	if (factory == NULL)
		return false;

	PClassInfo info;
	bool found = false;
	for (int32 i = 0; i < factory->countClasses(); i++)
	{
		if (factory->getClassInfo(i, &info) == kResultOk && strncmp(info.category, kVstAudioEffectClass, PClassInfo::kCategorySize) == 0)
		{
			found = true;
			break;
		}
	}
	if (!found)
		return false;

	memcpy(classID, info.cid, sizeof(TUID));
	name = string(info.name, strnlen(info.name, PClassInfo::kNameSize));

	hostContext = owned(static_cast<FUnknown*>(new HostApplication()));

	IComponent* newComponent = NULL;
	if (factory->createInstance(info.cid, IComponent_iid, (void**)&newComponent) != kResultOk || newComponent == NULL)
		return false;
	component = owned(newComponent);
	if (component->initialize(hostContext) != kResultOk)
	{
		component = nullptr;
		return false;
	}

	processor = FUnknownPtr<IAudioProcessor>(component);
	if (!processor)
		return false;

	BusInfo busInfo;
	if (component->getBusCount(kAudio, kInput) > 0 && component->getBusInfo(kAudio, kInput, 0, busInfo) == kResultOk)
	{
		inputChannelCount = busInfo.channelCount;
		component->activateBus(kAudio, kInput, 0, true);
	}
	if (component->getBusCount(kAudio, kOutput) > 0 && component->getBusInfo(kAudio, kOutput, 0, busInfo) == kResultOk)
	{
		outputChannelCount = busInfo.channelCount;
		component->activateBus(kAudio, kOutput, 0, true);
	}

	doublePrecision = processor->canProcessSampleSize(kSample64) == kResultTrue;

	// the editor and the parameter flushing it needs are only used from the GUI thread
	if (getProcessLevel() == 1)
	{
		controller = FUnknownPtr<IEditController>(component);
		if (!controller)
		{
			TUID controllerID;
			IEditController* newController = NULL;
			if (component->getControllerClassId(controllerID) == kResultOk
				&& factory->createInstance(controllerID, IEditController_iid, (void**)&newController) == kResultOk && newController != NULL)
			{
				controller = owned(newController);
				if (controller->initialize(hostContext) == kResultOk)
				{
					separateController = true;
					FUnknownPtr<Vst::IConnectionPoint> componentConnection(component);
					FUnknownPtr<Vst::IConnectionPoint> controllerConnection(controller);
					if (componentConnection && controllerConnection)
					{
						componentConnection->connect(controllerConnection);
						controllerConnection->connect(componentConnection);
					}
				}
				else
				{
					controller = nullptr;
				}
			}
		}

		if (controller)
			controller->setComponentHandler(this);

		prepareForProcessing(48000.0f, 512);
		startProcessing();
	}

	return true;
}

int VST3PluginInstance::numInputs() const
{
	return inputChannelCount;
}

int VST3PluginInstance::numOutputs() const
{
	return outputChannelCount;
}

bool VST3PluginInstance::canReplacing() const
{
	return true;
}

int VST3PluginInstance::uniqueID() const
{
	int32 id[4];
	memcpy(id, classID, sizeof(id));

	return id[0] ^ id[1] ^ id[2] ^ id[3];
}

std::wstring VST3PluginInstance::getName() const
{
	return StringHelper::toWString(name, CP_UTF8);
}

bool VST3PluginInstance::canDoubleReplacing() const
{
	return doublePrecision;
}

void VST3PluginInstance::prepareForProcessing(float sampleRate, int blockSize)
{
	if (!processor)
		return;

	ProcessSetup setup = {kRealtime, doublePrecision ? kSample64 : kSample32, blockSize, sampleRate};
	processor->setupProcessing(setup);

	// buffers of the main busses are replaced in process, all other busses use silent or discarded buffers
	silentBuffer.assign(blockSize, 0.0);
	discardBuffer.assign(blockSize, 0.0);
	processData.prepare(*component, 0, setup.symbolicSampleSize);
	for (int32 i = 0; i < processData.numInputs; i++)
		for (int32 j = 0; j < processData.inputs[i].numChannels; j++)
			processData.inputs[i].channelBuffers64[j] = (Sample64*)silentBuffer.data();
	for (int32 i = 0; i < processData.numOutputs; i++)
		for (int32 j = 0; j < processData.outputs[i].numChannels; j++)
			processData.outputs[i].channelBuffers64[j] = (Sample64*)discardBuffer.data();

	processData.processMode = kRealtime;
	processData.inputParameterChanges = &inputChanges;
	processContext.sampleRate = sampleRate;
	processData.processContext = &processContext;
}

// the chunk contains the size of the component state, the component state and the controller state
void VST3PluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	if (!component || chunkData == L"")
		return;

	// the loaded state replaces pending parameter changes
	inputChanges.clearQueue();

	DWORD size = 0;
	CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, NULL, &size, NULL, NULL);
	vector<BYTE> data(size);
	uint32 componentSize = 0;
	if (CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, data.data(), &size, NULL, NULL) != TRUE || size < sizeof(componentSize))
		return;
	memcpy(&componentSize, data.data(), sizeof(componentSize));
	if (componentSize > size - sizeof(componentSize))
		return;

	MemoryStream componentStream(data.data() + sizeof(componentSize), componentSize);
	component->setState(&componentStream);
	if (controller)
	{
		componentStream.seek(0, IBStream::kIBSeekSet, NULL);
		controller->setComponentState(&componentStream);

		TSize controllerSize = size - sizeof(componentSize) - componentSize;
		if (controllerSize > 0)
		{
			MemoryStream controllerStream(data.data() + sizeof(componentSize) + componentSize, controllerSize);
			controller->setState(&controllerStream);
		}
	}
}

void VST3PluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
	chunkData = L"";
	paramMap.clear();

	if (!component)
		return;

	flushParameters();

	MemoryStream stream;
	uint32 componentSize = 0;
	stream.write(&componentSize, sizeof(componentSize), NULL);
	if (component->getState(&stream) != kResultOk)
		return;
	componentSize = (uint32)(stream.getSize() - sizeof(componentSize));
	if (controller)
		controller->getState(&stream);
	memcpy(stream.getData(), &componentSize, sizeof(componentSize));

	DWORD stringLength = 0;
	CryptBinaryToStringW((BYTE*)stream.getData(), (DWORD)stream.getSize(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &stringLength);
	wchar_t* string = new wchar_t[stringLength];
	if (CryptBinaryToStringW((BYTE*)stream.getData(), (DWORD)stream.getSize(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, string, &stringLength) == TRUE)
		chunkData = string;
	delete[] string;
}

void VST3PluginInstance::startProcessing()
{
	if (!processor || processing)
		return;

	component->setActive(true);
	processor->setProcessing(true);
	processing = true;
}

void VST3PluginInstance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
	processInternal((void**)inputArray, (void**)outputArray, frameCount);
}

void VST3PluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	processInternal((void**)inputArray, (void**)outputArray, frameCount);
}

void VST3PluginInstance::processInternal(void** inputArray, void** outputArray, int frameCount)
{
	if (!processing)
		return;

	if (processData.numInputs > 0)
		for (int i = 0; i < inputChannelCount; i++)
			processData.inputs[0].channelBuffers64[i] = (Sample64*)inputArray[i];
	if (processData.numOutputs > 0)
		for (int i = 0; i < outputChannelCount; i++)
			processData.outputs[0].channelBuffers64[i] = (Sample64*)outputArray[i];

	processData.numSamples = frameCount;
	processor->process(processData);
}

void VST3PluginInstance::stopProcessing()
{
	if (!processing)
		return;

	processor->setProcessing(false);
	component->setActive(false);
	processing = false;
}

// parameter changes from the editor and state changes may only reach the component state after a process call
void VST3PluginInstance::flushParameters() const
{
	if (!processing)
		return;

	processData.numSamples = 0;
	processor->process(processData);
	inputChanges.clearQueue();
}

void VST3PluginInstance::startEditing(HWND hWnd, short* width, short* height)
{
	*width = 0;
	*height = 0;

	if (!controller)
		return;

	IPlugView* newView = controller->createView(ViewType::kEditor);
	if (newView == NULL)
		return;
	view = owned(newView);

	if (view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue)
	{
		view = nullptr;
		return;
	}

	view->setFrame(this);
	if (view->attached(hWnd, kPlatformTypeHWND) != kResultOk)
	{
		view->setFrame(nullptr);
		view = nullptr;
		return;
	}

	ViewRect rect;
	if (view->getSize(&rect) == kResultOk)
	{
		*width = (short)rect.getWidth();
		*height = (short)rect.getHeight();
	}
}

void VST3PluginInstance::stopEditing()
{
	if (!view)
		return;

	view->removed();
	view->setFrame(nullptr);
	view = nullptr;
}

tresult PLUGIN_API VST3PluginInstance::queryInterface(const TUID _iid, void** obj)
{
	if (FUnknownPrivate::iidEqual(_iid, FUnknown_iid) || FUnknownPrivate::iidEqual(_iid, IComponentHandler_iid))
	{
		*obj = static_cast<IComponentHandler*>(this);
		return kResultOk;
	}
	if (FUnknownPrivate::iidEqual(_iid, IPlugFrame_iid))
	{
		*obj = static_cast<IPlugFrame*>(this);
		return kResultOk;
	}

	*obj = NULL;
	return kNoInterface;
}

// lifetime is managed by the owner, not by reference counting
uint32 PLUGIN_API VST3PluginInstance::addRef()
{
	return 1;
}

uint32 PLUGIN_API VST3PluginInstance::release()
{
	return 1;
}

tresult PLUGIN_API VST3PluginInstance::beginEdit(ParamID id)
{
	return kResultOk;
}

tresult PLUGIN_API VST3PluginInstance::performEdit(ParamID id, ParamValue valueNormalized)
{
	int32 index;
	IParamValueQueue* queue = inputChanges.addParameterData(id, index);
	if (queue != NULL)
		queue->addPoint(0, valueNormalized, index);

	return kResultOk;
}

tresult PLUGIN_API VST3PluginInstance::endEdit(ParamID id)
{
	onAutomate();
	return kResultOk;
}

tresult PLUGIN_API VST3PluginInstance::restartComponent(int32 flags)
{
	if (flags & (kParamValuesChanged | kReloadComponent))
		onAutomate();
	return kResultOk;
}

tresult PLUGIN_API VST3PluginInstance::resizeView(IPlugView* view, ViewRect* newSize)
{
	if (view == NULL || newSize == NULL)
		return kInvalidArgument;

	onSizeWindow(newSize->getWidth(), newSize->getHeight());

	return view->onSize(newSize);
}
