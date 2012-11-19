/*
 * Copyright 2012, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "DebugReportGenerator.h"

#include <AutoLocker.h>
#include <File.h>

#include "Architecture.h"
#include "CpuState.h"
#include "Image.h"
#include "Register.h"
#include "StackFrame.h"
#include "StackTrace.h"
#include "StringUtils.h"
#include "Team.h"
#include "Thread.h"
#include "UiUtils.h"


DebugReportGenerator::DebugReportGenerator(::Team* team)
	:
	BReferenceable(),
	fTeam(team),
	fArchitecture(team->GetArchitecture())
{
	fArchitecture->AcquireReference();
}


DebugReportGenerator::~DebugReportGenerator()
{
	fArchitecture->ReleaseReference();
}


status_t
DebugReportGenerator::Init()
{
	// TODO: anything needed here?
	return B_OK;
}


DebugReportGenerator*
DebugReportGenerator::Create(::Team* team)
{
	DebugReportGenerator* self = new DebugReportGenerator(team);

	try {
		self->Init();
	} catch (...) {
		delete self;
		throw;
	}

	return self;
}


status_t
DebugReportGenerator::GenerateReport(const entry_ref& outputPath)
{
	BFile file(&outputPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result != B_OK)
		return result;

	BString output;
	result = _GenerateReportHeader(output);
	if (result != B_OK);
		return result;

	result = _DumpLoadedImages(output);
	if (result != B_OK)
		return result;

	result = _DumpRunningThreads(output);
	if (result != B_OK)
		return result;

	result = file.Write(output.String(), output.Length());
	if (result < 0)
		return result;

	return B_OK;
}


status_t
DebugReportGenerator::_GenerateReportHeader(BString& _output)
{
	AutoLocker<Team> locker(fTeam);

	BString data;
	data.SetToFormat("Debug information for team %s (%" B_PRId32 "):\n\n");
	_output << data;

	return B_OK;
}


status_t
DebugReportGenerator::_DumpLoadedImages(BString& _output)
{
	AutoLocker<Team> locker(fTeam);

	BString data;
	for (ImageList::ConstIterator it = fTeam->Images().GetIterator();
		 Image* image = it.Next();) {
		const ImageInfo& info = image->Info();
		try {
			data.SetToFormat("\t%s, id: %" B_PRId32", type: %" B_PRId32 ", "
				"Text: 0x%" B_PRIx64 ", %" B_PRIu64 " bytes, Data: 0x%"
				B_PRIx64 ", %" B_PRIu64 " bytes\n", info.Name().String(),
				info.ImageID(), info.Type(), info.TextBase(), info.TextSize(),
				info.DataBase(), info.DataSize());

			_output << data;
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
DebugReportGenerator::_DumpRunningThreads(BString& _output)
{
	AutoLocker<Team> locker(fTeam);

	BString data;
	status_t result = B_OK;
	for (ThreadList::ConstIterator it = fTeam->Threads().GetIterator();
		 Thread* thread = it.Next();) {
		try {
			data.SetToFormat("\t%s %s, id: %" B_PRId32", state: %" B_PRId32
				"\n", thread->Name(), thread->IsMainThread()
					? "(main)" : "", UiUtils::ThreadStateToString(
					thread->State(), thread->StoppedReason()));

			_output << data;

			if (thread->State() == THREAD_STATE_STOPPED)
				result = _DumpDebuggedThreadInfo(_output, thread);
			if (result != B_OK)
				return result;
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
DebugReportGenerator::_DumpDebuggedThreadInfo(BString& _output, Thread* thread)
{
	AutoLocker<Team> locker(fTeam);

	StackTrace* trace = thread->GetStackTrace();
	if (trace == NULL)
		return B_OK;

	BString data;
	for (int32 i = 0; StackFrame* frame = trace->FrameAt(i); i++) {
		char functionName[512];
		data.SetToFormat("0x%" B_PRIx64 "\t0x%" B_PRIx64 "\t%s\n",
			frame->FrameAddress(), frame->InstructionPointer(),
			UiUtils::FunctionNameForFrame(frame, functionName,
				sizeof(functionName)));

		_output << data;
	}

	_output << "\nRegisters:\n\n";

	CpuState* state = thread->GetCpuState();
	BVariant value;
	for (int32 i = 0; i < fArchitecture->CountRegisters(); i++) {
		const Register* reg = fArchitecture->Registers() + i;
		state->GetRegisterValue(reg, value);

		char buffer[64];
		data.SetToFormat("%s\t%0x%s\n", reg->Name(),
			UiUtils::VariantToString(value, buffer, sizeof(buffer)));
		_output << data;
	}

	return B_OK;
}
