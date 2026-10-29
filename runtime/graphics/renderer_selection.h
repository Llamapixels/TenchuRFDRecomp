#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
extern uint32_t g_tenchu_renderer;
void TenchuInitializeRenderer();
uint32_t TenchuActiveRenderer();
bool TenchuVulkanAvailable();
void TenchuRequestRestart();
bool TenchuRendererMessage(uint32_t,uintptr_t,intptr_t,intptr_t*);
bool TenchuPresentationPaused();
void TenchuPresentationStarted();
HRESULT WINAPI TenchuCreateDXGIFactory2(UINT,REFIID,void**);
HRESULT WINAPI TenchuD3D12CreateDevice(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
HRESULT WINAPI TenchuD3D12GetDebugInterface(REFIID,void**);
HRESULT WINAPI TenchuD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC*,D3D_ROOT_SIGNATURE_VERSION,ID3DBlob**,ID3DBlob**);
