#include "WWUtil.h"

void
WWWaveFormatDebug(WAVEFORMATEX *v)
{
	(void)v;

	dprintf(
		"  cbSize=%d\n"
		"  nAvgBytesPerSec=%d\n"
		"  nBlockAlign=%d\n"
		"  nChannels=%d\n"
		"  nSamplesPerSec=%d\n"
		"  wBitsPerSample=%d\n"
		"  wFormatTag=0x%x\n",
		v->cbSize,
		v->nAvgBytesPerSec,
		v->nBlockAlign,
		v->nChannels,
		v->nSamplesPerSec,
		v->wBitsPerSample,
		v->wFormatTag);
}

void
WWWFEXDebug(WAVEFORMATEXTENSIBLE *v)
{
	(void)v;

	dprintf(
		"  dwChannelMask=0x%x\n"
		"  Samples.wValidBitsPerSample=%d\n"
		"  SubFormat=0x%x\n",
		v->dwChannelMask,
		v->Samples.wValidBitsPerSample,
		v->SubFormat);
}