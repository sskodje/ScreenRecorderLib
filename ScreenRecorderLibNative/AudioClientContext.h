#pragma once
#include <Audioclient.h>
#include <atlbase.h>
#include "CommonTypes.h"


struct AudioClientContext
{
public:
	CComPtr<IAudioClient> client;
	AudioClientKind kind;

	AudioClientContext() :client(nullptr), kind(AudioClientKind::Endpoint) {

	}

	AudioClientContext(const CComPtr<IAudioClient> &client, const AudioClientKind &kind)
		: client(client), kind(kind)
	{
	}
};