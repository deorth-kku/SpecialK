/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/

#include <SpecialK/stdafx.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"DX12Device"

#include <SpecialK/render/d3d12/d3d12_pipeline_library.h>
#include <SpecialK/render/d3d12/d3d12_command_queue.h>
#include <SpecialK/render/d3d12/d3d12_dxil_shader.h>
#include <SpecialK/render/d3d12/d3d12_device.h>

// Various device-resource hacks are here for HDR
#include <SpecialK/render/dxgi/dxgi_hdr.h>

extern volatile LONG  __d3d12_hooked;
LPVOID pfnD3D12CreateDevice = nullptr;

D3D12Device_CreateGraphicsPipelineState_pfn
D3D12Device_CreateGraphicsPipelineState_Original = nullptr;
D3D12Device_CreateRenderTargetView_pfn
D3D12Device_CreateRenderTargetView_Original      = nullptr;
D3D12Device_CreateCommittedResource_pfn
D3D12Device_CreateCommittedResource_Original     = nullptr;
D3D12Device_CreatePlacedResource_pfn
D3D12Device_CreatePlacedResource_Original        = nullptr;

HRESULT
STDMETHODCALLTYPE
D3D12Device_CreateGraphicsPipelineState_Detour (
             ID3D12Device                       *This,
_In_   const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc,
             REFIID                              riid,
_COM_Outptr_ void                              **ppPipelineState )
{
  HRESULT hrPipelineCreate =
    D3D12Device_CreateGraphicsPipelineState_Original (
      This, pDesc,
      riid, ppPipelineState
    );

  if (pDesc == nullptr)
    return hrPipelineCreate;
                                                                                                  // {4D5298CA-D9F0-6133-A19D-B1D597920000}
static constexpr GUID SKID_D3D12KnownVtxShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x00 } };
static constexpr GUID SKID_D3D12KnownPixShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x01 } };
static constexpr GUID SKID_D3D12KnownGeoShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x02 } };
static constexpr GUID SKID_D3D12KnownHulShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x03 } };
static constexpr GUID SKID_D3D12KnownDomShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x04 } };
static constexpr GUID SKID_D3D12KnownComShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x05 } };
static constexpr GUID SKID_D3D12KnownMshShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x06 } };
static constexpr GUID SKID_D3D12KnownAmpShaderDigest = { 0x4d5298ca, 0xd9f0,  0x6133, { 0xa1, 0x9d, 0xb1, 0xd5, 0x97, 0x92, 0x00, 0x07 } };

  struct shader_repo_s
  {
    struct {
      const char*         name;
      SK_D3D12_ShaderType mask;
    } type;

    struct hash_s {
      struct dxilHashTest
      {
        // std::hash <DxilContainerHash>
        //
        size_t operator ()( const DxilContainerHash& h ) const
        {
          size_t      __h = 0;
          for (size_t __i = 0; __i < DxilContainerHashSize; ++__i)
          {
            __h = h.Digest [__i] +
                    (__h << 06)  +  (__h << 16)
                                 -   __h;
          }

          return __h;
        }

        // std::equal_to <DxilContainerHash>
        //
        bool operator ()( const DxilContainerHash& h1,
                          const DxilContainerHash& h2 ) const
        {
          return
            ( 0 == memcmp ( h1.Digest,
                            h2.Digest, DxilContainerHashSize ) );
        }
      };

      concurrency::concurrent_unordered_set <DxilContainerHash, dxilHashTest, dxilHashTest>
            used;
      GUID  guid;

      hash_s (const GUID& guid_)
      {   memcpy ( &guid,&guid_, sizeof (GUID) );
      };
    } hash;
  } static vertex   { { "Vertex",   SK_D3D12_ShaderType::Vertex   }, { shader_repo_s::hash_s (SKID_D3D12KnownVtxShaderDigest) } },
           pixel    { { "Pixel",    SK_D3D12_ShaderType::Pixel    }, { shader_repo_s::hash_s (SKID_D3D12KnownPixShaderDigest) } },
           geometry { { "Geometry", SK_D3D12_ShaderType::Geometry }, { shader_repo_s::hash_s (SKID_D3D12KnownGeoShaderDigest) } },
           hull     { { "Hull",     SK_D3D12_ShaderType::Hull     }, { shader_repo_s::hash_s (SKID_D3D12KnownHulShaderDigest) } },
           domain   { { "Domain",   SK_D3D12_ShaderType::Domain   }, { shader_repo_s::hash_s (SKID_D3D12KnownDomShaderDigest) } },
           compute  { { "Compute",  SK_D3D12_ShaderType::Compute  }, { shader_repo_s::hash_s (SKID_D3D12KnownComShaderDigest) } },
           mesh     { { "Mesh",     SK_D3D12_ShaderType::Mesh     }, { shader_repo_s::hash_s (SKID_D3D12KnownMshShaderDigest) } },
           amplify  { { "Amplify",  SK_D3D12_ShaderType::Amplify  }, { shader_repo_s::hash_s (SKID_D3D12KnownAmpShaderDigest) } };

  static const
    std::unordered_map <SK_D3D12_ShaderType, shader_repo_s&>
      repo_map =
        { { SK_D3D12_ShaderType::Vertex,   vertex   },
          { SK_D3D12_ShaderType::Pixel,    pixel    },
          { SK_D3D12_ShaderType::Geometry, geometry },
          { SK_D3D12_ShaderType::Hull,     hull     },
          { SK_D3D12_ShaderType::Domain,   domain   },
          { SK_D3D12_ShaderType::Compute,  compute  },
          { SK_D3D12_ShaderType::Mesh,     mesh     },
          { SK_D3D12_ShaderType::Amplify,  amplify  } };

  auto _StashAHash = [&](SK_D3D12_ShaderType type)
  {
    const D3D12_SHADER_BYTECODE
                     *pBytecode = nullptr;

    switch (type)
    {
      case SK_D3D12_ShaderType::Vertex:  pBytecode = (D3D12_SHADER_BYTECODE *)(
        pDesc->VS.BytecodeLength ? pDesc->VS.pShaderBytecode : nullptr); break;
      case SK_D3D12_ShaderType::Pixel:   pBytecode = (D3D12_SHADER_BYTECODE *)(
        pDesc->PS.BytecodeLength ? pDesc->PS.pShaderBytecode : nullptr); break;
      case SK_D3D12_ShaderType::Geometry:pBytecode = (D3D12_SHADER_BYTECODE *)(
        pDesc->GS.BytecodeLength ? pDesc->GS.pShaderBytecode : nullptr); break;
      case SK_D3D12_ShaderType::Domain:  pBytecode = (D3D12_SHADER_BYTECODE *)(
        pDesc->DS.BytecodeLength ? pDesc->DS.pShaderBytecode : nullptr); break;
      case SK_D3D12_ShaderType::Hull:    pBytecode = (D3D12_SHADER_BYTECODE *)(
        pDesc->HS.BytecodeLength ? pDesc->HS.pShaderBytecode : nullptr); break;
      default:                           pBytecode = nullptr;            break;
    }

    if (pBytecode != nullptr/* && repo_map.count (type)*/)
    {
      auto FourCC =  ((DxilContainerHeader *)pBytecode)->HeaderFourCC;
      auto pHash  = &((DxilContainerHeader *)pBytecode)->Hash.Digest [0];

      if ( FourCC == DFCC_Container || FourCC == DFCC_DXIL )
      {
        shader_repo_s& repo =
          repo_map.at (type);

        if (repo.hash.used.insert (((DxilContainerHeader *)pBytecode)->Hash).second)
        {
          SK_LOG0 ( ( L"%9hs Shader (BytecodeType=%s) [%02x%02x%02x%02x%02x%02x%02x%02x"
                                                     L"%02x%02x%02x%02x%02x%02x%02x%02x]",
                      repo.type.name, FourCC ==
                                        DFCC_Container ?
                                               L"DXBC" : L"DXIL",
              pHash [ 0], pHash [ 1], pHash [ 2], pHash [ 3], pHash [ 4], pHash [ 5],
              pHash [ 6], pHash [ 7], pHash [ 8], pHash [ 9], pHash [10], pHash [11],
              pHash [12], pHash [13], pHash [14], pHash [15] ),
                    __SK_SUBSYSTEM__ );
        }

        //SK_ComQIPtr <ID3D12Object>
        //    pPipelineState ((IUnknown *)*ppPipelineState);
        //if (pPipelineState != nullptr)
        //{
        //  ////pPipelineState->SetPrivateData ( repo.hash.guid,
        //  ////                         DxilContainerHashSize,
        //  ////                                     pHash );
        //}
      }

      else
      {
        shader_repo_s& repo =
          repo_map.at (type);

        SK_LOG0 ( ( L"%9hs Shader (FourCC=%hs,%lu)", repo.type.name, (char *)&FourCC, FourCC ), __SK_SUBSYSTEM__ );
      }
    }
  };

  if (SUCCEEDED (hrPipelineCreate))
  {
    if (pDesc->VS.pShaderBytecode) _StashAHash (SK_D3D12_ShaderType::Vertex);
    if (pDesc->PS.pShaderBytecode) _StashAHash (SK_D3D12_ShaderType::Pixel);
    if (pDesc->GS.pShaderBytecode) _StashAHash (SK_D3D12_ShaderType::Geometry);
    if (pDesc->HS.pShaderBytecode) _StashAHash (SK_D3D12_ShaderType::Hull);
    if (pDesc->DS.pShaderBytecode) _StashAHash (SK_D3D12_ShaderType::Domain);
  }

  return
    hrPipelineCreate;
}

void
STDMETHODCALLTYPE
D3D12Device_CreateRenderTargetView_Detour (
                ID3D12Device                  *This,
_In_opt_        ID3D12Resource                *pResource,
_In_opt_  const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
_In_            D3D12_CPU_DESCRIPTOR_HANDLE    DestDescriptor )
{
  // HDR Fix-Ups
  if ( pResource != nullptr && __SK_HDR_16BitSwap   &&
       pDesc     != nullptr && pDesc->ViewDimension ==
                                D3D12_RTV_DIMENSION_TEXTURE2D )
  {
    auto desc =
      pResource->GetDesc ();

    if ( desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         desc.Format    == DXGI_FORMAT_R16G16B16A16_FLOAT )
    {
      if (                       pDesc->Format  != DXGI_FORMAT_UNKNOWN &&
          DirectX::MakeTypeless (pDesc->Format) != DXGI_FORMAT_R16G16B16A16_TYPELESS)
      {
        SK_LOG_FIRST_CALL

        auto fixed_desc = *pDesc;
             fixed_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        return
          D3D12Device_CreateRenderTargetView_Original ( This,
            pResource, &fixed_desc,
              DestDescriptor
          );
      }
    }
  }

  return
    D3D12Device_CreateRenderTargetView_Original ( This,
       pResource, pDesc,
         DestDescriptor
    );
}

HRESULT
STDMETHODCALLTYPE
D3D12Device_CreateCommittedResource_Detour (
                 ID3D12Device           *This,
_In_       const D3D12_HEAP_PROPERTIES  *pHeapProperties,
                 D3D12_HEAP_FLAGS        HeapFlags,
_In_       const D3D12_RESOURCE_DESC    *pDesc,
                 D3D12_RESOURCE_STATES   InitialResourceState,
_In_opt_   const D3D12_CLEAR_VALUE      *pOptimizedClearValue,
                 REFIID                  riidResource,
_COM_Outptr_opt_ void                  **ppvResource )
{
  D3D12_RESOURCE_DESC _desc = *pDesc;

  if (SK_GetCurrentGameID () == SK_GAME_ID::EldenRing)
  {
    if (pDesc->Alignment == 4096)// && pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        _desc.Alignment = 0;

    if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
      if (pDesc->Alignment == 4096)
      {
        SK_RunOnce (SK_ImGui_Warning (L"Gotcha!"));

        _desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      }
    }
  }

  return
    D3D12Device_CreateCommittedResource_Original ( This,
      pHeapProperties, HeapFlags, &_desc/*pDesc*/, InitialResourceState,
        pOptimizedClearValue, riidResource, ppvResource );
}

HRESULT
STDMETHODCALLTYPE
D3D12Device_CreatePlacedResource_Detour (
                 ID3D12Device           *This,
_In_             ID3D12Heap             *pHeap,
                 UINT64                  HeapOffset,
_In_       const D3D12_RESOURCE_DESC    *pDesc,
                 D3D12_RESOURCE_STATES   InitialState,
_In_opt_   const D3D12_CLEAR_VALUE      *pOptimizedClearValue,
                 REFIID                  riid,
_COM_Outptr_opt_ void                  **ppvResource )
{
  D3D12_RESOURCE_DESC _desc = *pDesc;

  if (SK_GetCurrentGameID () == SK_GAME_ID::EldenRing)
  {
    if (pDesc->Alignment == 4096)// && pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
         _desc.Alignment = 0;

    if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
      if (pDesc->Alignment == 4096)
      {
        SK_RunOnce (SK_ImGui_Warning (L"Gotcha!"));

        _desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      }
    }
  }

  return
    D3D12Device_CreatePlacedResource_Original ( This,
      pHeap, HeapOffset, &_desc/*pDesc*/, InitialState,
        pOptimizedClearValue, riid, ppvResource );
}


void
SK_D3D12_InstallDeviceHooks (ID3D12Device *pDev12)
{
  assert (pDev12 != nullptr);

  if (pDev12 == nullptr)
    return;

  static bool
      once = false;
  if (once) return;

  SK_CreateVFTableHook2 ( L"ID3D12Device::CreateGraphicsPipelineState",
                            *(void ***)*(&pDev12), 10,
                             D3D12Device_CreateGraphicsPipelineState_Detour,
                   (void **)&D3D12Device_CreateGraphicsPipelineState_Original );

  SK_CreateVFTableHook2 ( L"ID3D12Device::CreateRenderTargetView",
                           *(void ***)*(&pDev12), 20,
                            D3D12Device_CreateRenderTargetView_Detour,
                  (void **)&D3D12Device_CreateRenderTargetView_Original );
  
  SK_CreateVFTableHook2 ( L"ID3D12Device::CreateCommittedResource",
                           *(void ***)*(&pDev12), 27,
                            D3D12Device_CreateCommittedResource_Detour,
                  (void **)&D3D12Device_CreateCommittedResource_Original );
  
  SK_CreateVFTableHook2 ( L"ID3D12Device::CreatePlacedResource",
                           *(void ***)*(&pDev12), 29,
                            D3D12Device_CreatePlacedResource_Detour,
                  (void **)&D3D12Device_CreatePlacedResource_Original );

  // 21 CreateDepthStencilView
  // 22 CreateSampler
  // 23 CopyDescriptors
  // 24 CopyDescriptorsSimple
  // 25 GetResourceAllocationInfo
  // 26 GetCustomHeapProperties
  // 27 CreateCommittedResource
  // 28 CreateHeap
  // 29 CreatePlacedResource
  // 30 CreateReservedResource
  // 31 CreateSharedHandle
  // 32 OpenSharedHandle
  // 33 OpenSharedHandleByName
  // 34 MakeResident
  // 35 Evict
  // 36 CreateFence
  // 37 GetDeviceRemovedReason
  // 38 GetCopyableFootprints 
  // 39 CreateQueryHeap
  // 40 SetStablePowerState
  // 41 CreateCommandSignature
  // 42 GetResourceTiling
  // 43 GetAdapterLuid

  // ID3D12Device1
  //---------------
  // 44 CreatePipelineLibrary
  // 45 SetEventOnMultipleFenceCompletion
  // 46 SetResidencyPriority

  SK_ComQIPtr <ID3D12Device1>
                    pDevice1 (pDev12);
  if (   nullptr != pDevice1 )
  {
    SK_D3D12_HookPipelineLibrary (pDevice1.p);
  }

  // ID3D12Device2
  //---------------
  // 47 CreatePipelineState

  // ID3D12Device3
  //---------------
  // 48 OpenExistingHeapFromAddress
  // 49 OpenExistingHeapFromFileMapping
  // 50 EnqueueMakeResident

  // ID3D12Device4
  //---------------
  // 51 CreateCommandList1
  // 52 CreateProtectedResourceSession
  // 53 CreateCommittedResource1
  // 54 CreateHeap1
  // 55 CreateReservedResource1
  // 56 GetResourceAllocationInfo1

  // ID3D12Device5
  //---------------
  // 57 CreateLifetimeTracker
  // 58 RemoveDevice
  // 59 EnumerateMetaCommand
  // 60 EnumerateMetaCommandParameters
  // 61 CreateMetaCommand
  // 62 CreateStateObject
  // 63 GetRaytracingAccelerationStructurePrebuildInfo
  // 64 CheckDriverMatchingIdentifier

  // ID3D12Device6
  //---------------
  // 65 SetBackgroundProcessingMode

  // ID3D12Device7
  //---------------
  // 66 AddToStateObject
  // 67 CreateProtectedResourceSession1

  // ID3D12Device8
  //---------------
  // 68 GetResourceAllocationInfo2
  // 69 CreateCommittedResource2
  // 70 CreatePlacedResource1
  // 71 CreateSamplerFeedbackUnorderedAccessView
  // 72 GetCopyableFootprints1

  // ID3D12Device9
  //---------------
  // 73 CreateShaderCacheSession
  // 74 ShaderCacheControl
  // 75 CreateCommandQueue1

  SK_ApplyQueuedHooks ();

  once = true;
}

D3D12CreateDevice_pfn D3D12CreateDevice_Import = nullptr;
volatile LONG         __d3d12_ready            = FALSE;

void
WaitForInitD3D12 (void)
{
  SK_Thread_SpinUntilFlagged (&__d3d12_ready);
}

HRESULT
WINAPI
D3D12CreateDevice_Detour (
  _In_opt_  IUnknown          *pAdapter,
            D3D_FEATURE_LEVEL  MinimumFeatureLevel,
  _In_      REFIID             riid,
  _Out_opt_ void             **ppDevice )
{
  WaitForInitD3D12 ();

  DXGI_LOG_CALL_0 ( L"D3D12CreateDevice" );

  dll_log->LogEx ( true,
                     L"[  D3D 12  ]  <~> Minimum Feature Level - %s\n",
                         SK_DXGI_FeatureLevelsToStr (
                           1,
                             (DWORD *)&MinimumFeatureLevel
                         ).c_str ()
                 );

  if ( pAdapter != nullptr )
  {
    int iver =
      SK_GetDXGIAdapterInterfaceVer ( pAdapter );

    // IDXGIAdapter3 = DXGI 1.4 (Windows 10+)
    if ( iver >= 3 )
    {
      SK::DXGI::StartBudgetThread ( (IDXGIAdapter **)&pAdapter );
    }
  }

  HRESULT res;

  DXGI_CALL (res,
    D3D12CreateDevice_Import ( pAdapter,
                                 MinimumFeatureLevel,
                                   riid,
                                     ppDevice )
  );

  if ( SUCCEEDED ( res ) )
  {
    if ( ppDevice != nullptr )
    {
      //if ( *ppDevice != g_pD3D12Dev )
      //{
        // TODO: This isn't the right way to get the feature level
        dll_log->Log ( L"[  D3D 12  ] >> Device = %ph (Feature Level:%hs)",
                         *ppDevice,
                           SK_DXGI_FeatureLevelsToStr ( 1,
                                                         (DWORD *)&MinimumFeatureLevel//(DWORD *)&ret_level
                                                      ).c_str ()
                     );

        //g_pD3D12Dev =
        //  (IUnknown *)*ppDevice;
      //}
    }
  }

  return res;
}

bool
SK_D3D12_HookDeviceCreation (void)
{
  static bool hooked = false;
  if (        hooked)
    return    hooked;

  if ( MH_OK ==
         SK_CreateDLLHook ( L"d3d12.dll",
                             "D3D12CreateDevice",
                              D3D12CreateDevice_Detour,
                   (LPVOID *)&D3D12CreateDevice_Import,
                          &pfnD3D12CreateDevice )
     )
  {
    std::exchange (hooked, true);
    
    InterlockedIncrement (&__d3d12_ready);
  }

  return hooked;
}

void
SK_D3D12_EnableHooks (void)
{
  if (pfnD3D12CreateDevice != nullptr)
    SK_EnableHook (pfnD3D12CreateDevice);

  InterlockedIncrement (&__d3d12_hooked);
}