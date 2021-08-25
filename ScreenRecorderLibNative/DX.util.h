#pragma once
#include "CommonTypes.h"

HRESULT InitializeDx(_In_opt_ IDXGIAdapter *adapter, _Out_ DX_RESOURCES *Data);
HRESULT GetAdapterForDevice(_In_ ID3D11Device *pDevice, _Outptr_ IDXGIAdapter **ppAdapter);
HRESULT GetAdapterForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIAdapter **ppAdapter);
HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput);

/// <summary>
/// Returns the main display output of the system.
/// </summary>
/// <param name="ppOutput">The IDXGIOutput corresponding to the main output</param>
HRESULT GetMainOutput(_Outptr_result_maybenull_ IDXGIOutput **ppOutput);

/// <summary>
/// Create the recording output rectangles for each source in the input, based on their coordinates and any custom size, crop or position rules.
/// </summary>
/// <param name="sources">The recording sources to process</param>
/// <param name="outputs">A vector of pairs, containing the source and corresponding rectangle.</param>
HRESULT GetOutputRectsForRecordingSources(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<std::pair<RECORDING_SOURCE, RECT>> *outputs);

/// <summary>
/// Initialize shaders for drawing to screen
/// </summary>
/// <param name="pDevice"></param>
/// <returns></returns>
HRESULT InitShaders(_In_ ID3D11Device *pDevice, _Outptr_ ID3D11PixelShader **ppPixelShader, _Outptr_ ID3D11VertexShader **ppVertexShader, _Outptr_ ID3D11InputLayout **ppInputLayout);

/// <summary>
/// Creates a list of all display adapters on the system.
/// </summary>
/// <param name="pOutputs">A vector containing IDXGIOutput interfaces</param>
void EnumOutputs(_Out_ std::vector<IDXGIOutput *> *pOutputs);

/// <summary>
/// Creates a union of the input rectangles. The union is the smallest rectangle that contains all source rectangles.
/// </summary>
/// <param name="inputs">A vector of RECT coordinates to combine.</param>
/// <param name="pOutRect">The combined union of input rectangles.</param>
/// <param name="pOffsets">Optional vector containing offsets corresponding to any gaps between input rects. </param>
void GetCombinedRects(_In_ std::vector<RECT> inputs, _Out_ RECT *pOutRect, _Out_opt_ std::vector<SIZE> *pOffsets);

/// <summary>
/// Gets the display name of the monitor corresponding to the monitor handle.
/// </summary>
/// <param name="monitor">A handle to a monitor</param>
/// <returns>The display name of the monitor</returns>
std::wstring GetMonitorName(HMONITOR monitor);

std::vector<IDXGIAdapter *> EnumDisplayAdapters();
void CleanDx(_Inout_ DX_RESOURCES *Data);
void SetViewPort(_In_ ID3D11DeviceContext *deviceContext, _In_ UINT Width, _In_ UINT Height);
void SetDebugName(_In_ ID3D11DeviceChild *child, _In_ const std::string &name);
_Ret_maybenull_  HANDLE GetSharedHandle(_In_ ID3D11Texture2D *pSurface);