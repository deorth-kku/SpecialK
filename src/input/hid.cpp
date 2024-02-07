﻿/**
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
#include <hidclass.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"Input Mgr."

#define SK_HID_READ(type)  SK_HID_Backend->markRead   (type);
#define SK_HID_WRITE(type) SK_HID_Backend->markWrite  (type);
#define SK_HID_VIEW(type)  SK_HID_Backend->markViewed (type);
#define SK_HID_HIDE(type)  SK_HID_Backend->markHidden (type);

enum class SK_Input_DeviceFileType
{
  None    = 0,
  HID     = 1,
  NVIDIA  = 2,
  Invalid = 4
};

struct SK_NVIDIA_DeviceFile {
  SK_NVIDIA_DeviceFile (void) = default;
  SK_NVIDIA_DeviceFile (HANDLE file, const wchar_t *wszPath) : hFile (file)
  {
    wcsncpy_s ( wszDevicePath, MAX_PATH,
                      wszPath, _TRUNCATE );
  };

  HANDLE  hFile                        = INVALID_HANDLE_VALUE;
  wchar_t wszDevicePath [MAX_PATH + 2] = { };
  bool    bDisableDevice               = FALSE;
};

struct SK_HID_DeviceFile {
  HIDP_CAPS         hidpCaps                           = { };
  wchar_t           wszProductName      [128]          = { };
  wchar_t           wszManufacturerName [128]          = { };
  wchar_t           wszSerialNumber     [128]          = { };
  wchar_t           wszDevicePath       [MAX_PATH + 2] = { };
  std::vector<BYTE> last_data_read;
  sk_input_dev_type device_type                        = sk_input_dev_type::Other;
  BOOL              bDisableDevice                     = FALSE;
  HANDLE            hFile                              = INVALID_HANDLE_VALUE;

  SK_HID_DeviceFile (void) = default;

  SK_HID_DeviceFile (HANDLE file, const wchar_t *wszPath)
  {
    static
      concurrency::concurrent_unordered_map <std::wstring, SK_HID_DeviceFile> known_paths;

    // This stuff can be REALLY slow when games are constantly enumerating devices,
    //   so keep a cache handy.
    if (known_paths.count (wszPath))
    {
      memcpy (this, &known_paths.at (wszPath), sizeof (SK_HID_DeviceFile));
      return;
    }

    wchar_t *lpFileName = nullptr;

    if (wszPath != nullptr)
    {
      wcsncpy_s ( wszDevicePath, MAX_PATH,
                  wszPath,       _TRUNCATE );
    }

    PHIDP_PREPARSED_DATA                 preparsed_data = nullptr;
    if (SK_HidD_GetPreparsedData (file, &preparsed_data))
    {
      if (HIDP_STATUS_SUCCESS == SK_HidP_GetCaps (preparsed_data, &hidpCaps))
      {
#if 0
        if (! DuplicateHandle (
                GetCurrentProcess (), file,
                GetCurrentProcess (), &hFile,
                  0x0, FALSE, DUPLICATE_SAME_ACCESS ) )
        {
          SK_LOGi0 (L"Failed to duplicate handle for HID device: %ws!", lpFileName);
          return;
        }
#else
        std::ignore = lpFileName;
        hFile       = file;
#endif

        DWORD dwBytesRead = 0;

        SK_DeviceIoControl (
          hFile, IOCTL_HID_GET_PRODUCT_STRING, 0, 0,
          wszProductName, 128, &dwBytesRead, nullptr
        );

        SK_DeviceIoControl (
          hFile, IOCTL_HID_GET_MANUFACTURER_STRING, 0, 0,
          wszManufacturerName, 128, &dwBytesRead, nullptr
        );

        SK_DeviceIoControl (
          hFile, IOCTL_HID_GET_SERIALNUMBER_STRING, 0, 0,
          wszSerialNumber, 128, &dwBytesRead, nullptr
        );

        if (hidpCaps.UsagePage == HID_USAGE_PAGE_GENERIC)
        {
          switch (hidpCaps.Usage)
          {
            case HID_USAGE_GENERIC_GAMEPAD:
            case HID_USAGE_GENERIC_JOYSTICK:
            case HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER:
            {
              device_type = sk_input_dev_type::Gamepad;
            } break;

            case HID_USAGE_GENERIC_POINTER:
            case HID_USAGE_GENERIC_MOUSE:
            {
              device_type = sk_input_dev_type::Mouse;
            } break;

            case HID_USAGE_GENERIC_KEYBOARD:
            case HID_USAGE_GENERIC_KEYPAD:
            {
              device_type = sk_input_dev_type::Keyboard;
            } break;
          }
        }

        if (device_type == sk_input_dev_type::Other)
        {
          if (hidpCaps.UsagePage == HID_USAGE_PAGE_KEYBOARD)
          {
            device_type = sk_input_dev_type::Keyboard;
          }

          else if (hidpCaps.UsagePage == HID_USAGE_PAGE_VR    ||
                   hidpCaps.UsagePage == HID_USAGE_PAGE_SPORT ||
                   hidpCaps.UsagePage == HID_USAGE_PAGE_GAME  ||
                   hidpCaps.UsagePage == HID_USAGE_PAGE_ARCADE)
          {
            device_type = sk_input_dev_type::Gamepad;
          }

          else
          {
            SK_LOGi1 (
              L"Unknown HID Device Type (Product=%ws):  UsagePage=%x, Usage=%x",
                wszProductName, hidpCaps.UsagePage, hidpCaps.Usage
            );
          }
        }

        SK_ReleaseAssert ( // WTF?
          SK_HidD_FreePreparsedData (preparsed_data) != FALSE
        );
      }
    }

    known_paths.insert ({ wszPath, *this });
  }

  bool setPollingFrequency (DWORD dwFreq)
  {
    std::ignore = dwFreq;

    return false;
  }

  bool isInputAllowed (void) const
  {
    if (bDisableDevice)
      return false;

    switch (device_type)
    {
      case sk_input_dev_type::Mouse:
        return (! SK_ImGui_WantMouseCapture ());
      case sk_input_dev_type::Keyboard:
        return (! SK_ImGui_WantKeyboardCapture ());
      case sk_input_dev_type::Gamepad:
        return (! SK_ImGui_WantGamepadCapture ());
      default: // No idea what this is, ignore it...
        break;
    }

    return true;
  }
};

       concurrency::concurrent_vector        <SK_HID_PlayStationDevice>     SK_HID_PlayStationControllers;
static concurrency::concurrent_unordered_map <HANDLE, SK_HID_DeviceFile>    SK_HID_DeviceFiles;
static concurrency::concurrent_unordered_map <HANDLE, SK_NVIDIA_DeviceFile> SK_NVIDIA_DeviceFiles;

// Faster check for known device files, the type can be checked after determining whether SK
//   knows about this device...
static concurrency::concurrent_unordered_set <HANDLE>                      SK_Input_DeviceFiles;

std::tuple <SK_Input_DeviceFileType, void*, bool>
SK_Input_GetDeviceFileAndState (HANDLE hFile)
{
  // Bloom filter since most file reads -are not- for input devices
  if ( SK_Input_DeviceFiles.find (hFile) ==
       SK_Input_DeviceFiles.cend (     ) )
  {
    return
      { SK_Input_DeviceFileType::None, nullptr, true };
  }

  //
  // Figure out device type
  //
  if ( const auto hid_iter  = SK_HID_DeviceFiles.find (hFile);
                  hid_iter != SK_HID_DeviceFiles.cend (     ) )
  {
    auto& hid_device =
          hid_iter->second;

    if (hid_device.device_type != sk_input_dev_type::Other)
    {
      return
        { SK_Input_DeviceFileType::HID, &hid_device, hid_device.isInputAllowed () };
    }
  }

  else if ( const auto nv_iter  = SK_NVIDIA_DeviceFiles.find (hFile);
                       nv_iter != SK_NVIDIA_DeviceFiles.cend (     ) )
  {
    return
      { SK_Input_DeviceFileType::NVIDIA, &nv_iter->second, true };
  }

  return
    { SK_Input_DeviceFileType::Invalid, nullptr, true };
}

SetupDiGetClassDevsW_pfn             SK_SetupDiGetClassDevsW             = nullptr;
SetupDiGetClassDevsExW_pfn           SK_SetupDiGetClassDevsExW           = nullptr;
SetupDiGetClassDevsA_pfn             SK_SetupDiGetClassDevsA             = nullptr;
SetupDiGetClassDevsExA_pfn           SK_SetupDiGetClassDevsExA           = nullptr;
SetupDiEnumDeviceInfo_pfn            SK_SetupDiEnumDeviceInfo            = nullptr;
SetupDiEnumDeviceInterfaces_pfn      SK_SetupDiEnumDeviceInterfaces      = nullptr;
SetupDiGetDeviceInterfaceDetailW_pfn SK_SetupDiGetDeviceInterfaceDetailW = nullptr;
SetupDiGetDeviceInterfaceDetailA_pfn SK_SetupDiGetDeviceInterfaceDetailA = nullptr;
SetupDiDestroyDeviceInfoList_pfn     SK_SetupDiDestroyDeviceInfoList     = nullptr;

XINPUT_STATE hid_to_xi { };
  
void SK_HID_SetupPlayStationControllers (void)
{
  HDEVINFO hid_device_set = 
    SK_SetupDiGetClassDevsW (&GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_DEVICEINTERFACE |
                                                                       DIGCF_PRESENT);
  
  if (hid_device_set != INVALID_HANDLE_VALUE)
  {
    SP_DEVINFO_DATA devInfoData = {
      .cbSize = sizeof (SP_DEVINFO_DATA)
    };
  
    SP_DEVICE_INTERFACE_DATA devInterfaceData = {
      .cbSize = sizeof (SP_DEVICE_INTERFACE_DATA)
    };
  
    for (                                     DWORD dwDevIdx = 0            ;
          SK_SetupDiEnumDeviceInfo (hid_device_set, dwDevIdx, &devInfoData) ;
                                                  ++dwDevIdx )
    {
      devInfoData.cbSize      = sizeof (SP_DEVINFO_DATA);
      devInterfaceData.cbSize = sizeof (SP_DEVICE_INTERFACE_DATA);
  
      if (! SK_SetupDiEnumDeviceInterfaces ( hid_device_set, nullptr, &GUID_DEVINTERFACE_HID,
                                                   dwDevIdx, &devInterfaceData) )
      {
        continue;
      }
  
      static wchar_t devInterfaceDetailData [MAX_PATH + 2];
  
      ULONG ulMinimumSize = 0;
  
      SK_SetupDiGetDeviceInterfaceDetailW (
        hid_device_set, &devInterfaceData, nullptr,
          0, &ulMinimumSize, nullptr );
  
      if (GetLastError () != ERROR_INSUFFICIENT_BUFFER)
        continue;
  
      if (ulMinimumSize > sizeof (wchar_t) * (MAX_PATH + 2))
        continue;
  
      SP_DEVICE_INTERFACE_DETAIL_DATA *pDevInterfaceDetailData =
        (SP_DEVICE_INTERFACE_DETAIL_DATA *)devInterfaceDetailData;
  
      pDevInterfaceDetailData->cbSize =
        sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA);
  
      if ( SK_SetupDiGetDeviceInterfaceDetailW (
             hid_device_set, &devInterfaceData, pDevInterfaceDetailData,
               ulMinimumSize, &ulMinimumSize, nullptr ) )
      {
        wchar_t *wszFileName =
          pDevInterfaceDetailData->DevicePath;
  
        if (StrStrIW (wszFileName, L"VID_054c") && SK_CreateFile2 != nullptr)
        {
          SK_HID_PlayStationDevice controller;
  
          wcsncpy_s (controller.wszDevicePath, MAX_PATH,
                                wszFileName,   _TRUNCATE);

          controller.hDeviceFile =
            SK_CreateFile2 ( wszFileName, FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                                          FILE_SHARE_READ   | FILE_SHARE_WRITE,
                                            OPEN_EXISTING, nullptr );
  
          if (controller.hDeviceFile != nullptr)
          {
            if (! SK_HidD_GetPreparsedData (controller.hDeviceFile, &controller.pPreparsedData))
            	continue;

            HIDP_CAPS                                      caps = { };
              SK_HidP_GetCaps (controller.pPreparsedData, &caps);

            controller.input_report.resize (caps.InputReportByteLength);

            std::vector <HIDP_BUTTON_CAPS>
              buttonCapsArray;
              buttonCapsArray.resize (caps.NumberInputButtonCaps);

            std::vector <HIDP_VALUE_CAPS>
              valueCapsArray;
              valueCapsArray.resize (caps.NumberInputValueCaps);

            USHORT num_caps =
              caps.NumberInputButtonCaps;

            if ( HIDP_STATUS_SUCCESS ==
              SK_HidP_GetButtonCaps ( HidP_Input,
                                        buttonCapsArray.data (), &num_caps,
                                          controller.pPreparsedData ) )
            {
              for (UINT i = 0 ; i < num_caps ; ++i)
              {
                // Face Buttons
                if (buttonCapsArray [i].IsRange)
                {
                  controller.button_report_id =
                    buttonCapsArray [i].ReportID;
                  controller.button_usage_min =
                    buttonCapsArray [i].Range.UsageMin;
                  controller.button_usage_max =
                    buttonCapsArray [i].Range.UsageMax;

                  controller.buttons.resize (
                    static_cast <size_t> (
                      controller.button_usage_max -
                      controller.button_usage_min + 1
                    )
                  );
                }

                // ???
                else
                {
                  // No idea what a third set of buttons would be...
                  SK_ReleaseAssert (num_caps <= 2);
                }
              }

              USHORT value_caps_count =
                sk::narrow_cast <USHORT> (valueCapsArray.size ());

              if ( HIDP_STATUS_SUCCESS ==
                     SK_HidP_GetValueCaps ( HidP_Input, valueCapsArray.data (),
                                                       &value_caps_count,
                                                        controller.pPreparsedData ) )
              {
                controller.value_caps.resize (value_caps_count);

                for ( int idx = 0; idx < value_caps_count; ++idx )
                {
                  controller.value_caps [idx] = valueCapsArray [idx];
                }
              }

              // We need a contiguous array to read-back the set buttons,
              //   rather than allocating it dynamically, do it once and reuse.
              controller.button_usages.resize (controller.buttons.size ());

              USAGE idx = 0;

              for ( auto& button : controller.buttons )
              {
                button.UsagePage = buttonCapsArray [0].UsagePage;
                button.Usage     = controller.button_usage_min + idx++;
                button.state     = false;
              }
            }

            controller.bConnected = true;
            controller.bDualSense =
              StrStrIW (wszFileName, L"PID_0DF2") != nullptr ||
              StrStrIW (wszFileName, L"PID_0CE6") != nullptr;

            controller.bDualShock4 =
              StrStrIW (wszFileName, L"PID_05C4") != nullptr ||
              StrStrIW (wszFileName, L"PID_09CC") != nullptr ||
              StrStrIW (wszFileName, L"PID_0BA0") != nullptr;

            controller.bDualShock3 =
              StrStrIW (wszFileName, L"PID_0268") != nullptr;
  
            SK_HID_PlayStationControllers.push_back (controller);
          }
        }
      }
    }
  
    SK_SetupDiDestroyDeviceInfoList (hid_device_set);
  }
}

//////////////////////////////////////////////////////////////
//
// HIDClass (User mode)
//
//////////////////////////////////////////////////////////////
HidD_GetPreparsedData_pfn   HidD_GetPreparsedData_Original  = nullptr;
HidD_FreePreparsedData_pfn  HidD_FreePreparsedData_Original = nullptr;
HidD_GetFeature_pfn         HidD_GetFeature_Original        = nullptr;
HidP_GetData_pfn            HidP_GetData_Original           = nullptr;
HidP_GetCaps_pfn            HidP_GetCaps_Original           = nullptr;
HidP_GetUsages_pfn          HidP_GetUsages_Original         = nullptr;

HidD_GetPreparsedData_pfn   SK_HidD_GetPreparsedData   = nullptr;
HidD_FreePreparsedData_pfn  SK_HidD_FreePreparsedData  = nullptr;
HidD_GetInputReport_pfn     SK_HidD_GetInputReport     = nullptr;
HidD_GetFeature_pfn         SK_HidD_GetFeature         = nullptr;
HidP_GetData_pfn            SK_HidP_GetData            = nullptr;
HidP_GetCaps_pfn            SK_HidP_GetCaps            = nullptr;
HidP_GetButtonCaps_pfn      SK_HidP_GetButtonCaps      = nullptr;
HidP_GetValueCaps_pfn       SK_HidP_GetValueCaps       = nullptr;
HidP_GetUsages_pfn          SK_HidP_GetUsages          = nullptr;
HidP_GetUsageValue_pfn      SK_HidP_GetUsageValue      = nullptr;
HidP_GetUsageValueArray_pfn SK_HidP_GetUsageValueArray = nullptr;

bool
SK_HID_FilterPreparsedData (PHIDP_PREPARSED_DATA pData)
{
  bool filter = false;

        HIDP_CAPS caps;
  const NTSTATUS  stat =
          SK_HidP_GetCaps (pData, &caps);

  if ( stat           == HIDP_STATUS_SUCCESS &&
       caps.UsagePage == HID_USAGE_PAGE_GENERIC )
  {
    switch (caps.Usage)
    {
      case HID_USAGE_GENERIC_GAMEPAD:
      case HID_USAGE_GENERIC_JOYSTICK:
      case HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER:
      {
        if (SK_ImGui_WantGamepadCapture () && (! config.input.gamepad.native_ps4))
        {
          filter = true;

          SK_HID_HIDE (sk_input_dev_type::Gamepad);
        }

        else
        {
          SK_HID_READ (sk_input_dev_type::Gamepad);
          SK_HID_VIEW (sk_input_dev_type::Gamepad);
        }
      } break;

      case HID_USAGE_GENERIC_POINTER:
      case HID_USAGE_GENERIC_MOUSE:
      {
        if (SK_ImGui_WantMouseCapture ())
        {
          filter = true;

          SK_HID_HIDE (sk_input_dev_type::Mouse);
        }

        else
        {
          SK_HID_READ (sk_input_dev_type::Mouse);
          SK_HID_VIEW (sk_input_dev_type::Mouse);
        }
      } break;

      case HID_USAGE_GENERIC_KEYBOARD:
      case HID_USAGE_GENERIC_KEYPAD:
      {
        if (SK_ImGui_WantKeyboardCapture ())
        {
          filter = true;

          SK_HID_HIDE (sk_input_dev_type::Keyboard);
        }

        else
        {
          SK_HID_READ (sk_input_dev_type::Keyboard);
          SK_HID_VIEW (sk_input_dev_type::Keyboard);
        }
      } break;
    }
  }

  //SK_LOG0 ( ( L"HID Preparsed Data - Stat: %04x, UsagePage: %02x, Usage: %02x",
                //stat, caps.UsagePage, caps.Usage ),
              //L" HIDInput ");

  return filter;
}

PHIDP_PREPARSED_DATA* SK_HID_PreparsedDataP = nullptr;
PHIDP_PREPARSED_DATA  SK_HID_PreparsedData  = nullptr;

_Must_inspect_result_
_Success_(return==TRUE)
BOOLEAN __stdcall
HidD_GetPreparsedData_Detour (
  _In_  HANDLE                HidDeviceObject,
  _Out_ PHIDP_PREPARSED_DATA *PreparsedData )
{
  SK_LOG_FIRST_CALL

        PHIDP_PREPARSED_DATA pData = nullptr;
  const BOOLEAN              bRet  =
    HidD_GetPreparsedData_Original ( HidDeviceObject,
                                       &pData );

  if (bRet && pData != nullptr)
  {
    SK_HID_PreparsedDataP = PreparsedData;
    SK_HID_PreparsedData  = pData;

    if ( config.input.gamepad.disable_hid  ||
                 SK_HID_FilterPreparsedData (pData))
    {
      if (! HidD_FreePreparsedData_Original (pData))
      {
        SK_ReleaseAssert (false && L"The Sky is Falling!");
      }

      return FALSE;
    }

    if (PreparsedData != nullptr)
       *PreparsedData = pData;
  }

  return
    bRet;
}

BOOLEAN
__stdcall
HidD_FreePreparsedData_Detour (
  _In_ PHIDP_PREPARSED_DATA PreparsedData ) noexcept
{
  BOOLEAN bRet =
    HidD_FreePreparsedData_Original (
                               PreparsedData );
  if ( SK_HID_PreparsedData == PreparsedData )
       SK_HID_PreparsedData = nullptr;

  return bRet;
}

BOOLEAN
_Success_ (return)
__stdcall
HidD_GetFeature_Detour ( _In_  HANDLE HidDeviceObject,
                         _Out_ PVOID  ReportBuffer,
                         _In_  ULONG  ReportBufferLength )
{
  SK_LOG_FIRST_CALL

  ////SK_HID_READ (sk_input_dev_type::Gamepad)

  bool                 filter = false;
  PHIDP_PREPARSED_DATA pData  = nullptr;

  if (SK_ImGui_WantGamepadCapture ())
  {
    if (HidD_GetPreparsedData_Original (HidDeviceObject, &pData))
    {
      if (SK_HID_FilterPreparsedData (pData))
        filter = true;

      HidD_FreePreparsedData_Original (pData);
    }
  }

  BOOLEAN bRet =
    HidD_GetFeature_Original (
      HidDeviceObject, ReportBuffer,
                       ReportBufferLength
    );

  if (filter)
  {
    ZeroMemory (ReportBuffer, ReportBufferLength);
  }

  return bRet;
}

NTSTATUS
__stdcall
HidP_GetUsages_Detour (
  _In_                                        HIDP_REPORT_TYPE     ReportType,
  _In_                                        USAGE                UsagePage,
  _In_opt_                                    USHORT               LinkCollection,
  _Out_writes_to_(*UsageLength, *UsageLength) PUSAGE               UsageList,
  _Inout_                                     PULONG               UsageLength,
  _In_                                        PHIDP_PREPARSED_DATA PreparsedData,
  _Out_writes_bytes_(ReportLength)            PCHAR                Report,
  _In_                                        ULONG                ReportLength
)
{
  SK_LOG_FIRST_CALL

  NTSTATUS ret =
    HidP_GetUsages_Original ( ReportType, UsagePage,
                                LinkCollection, UsageList,
                                  UsageLength, PreparsedData,
                                    Report, ReportLength );

  // De we want block this I/O?
  bool filter = false;

  if (          ret == HIDP_STATUS_SUCCESS &&
       ( ReportType == HidP_Input          ||
         ReportType == HidP_Output         ||
         ReportType == HidP_Feature ) )
  {
    if (SK_ImGui_WantGamepadCapture ())
    {
      // This will classify the data for us, so don't record this event yet.
      filter =
        SK_HID_FilterPreparsedData (PreparsedData);
    }
  }

  if (filter)
  {
    SK_ReleaseAssert (UsageLength != nullptr);

    ZeroMemory (UsageList, *UsageLength);
                           *UsageLength = 0;
  }

  return ret;
}

NTSTATUS
__stdcall
HidP_GetData_Detour (
  _In_    HIDP_REPORT_TYPE     ReportType,
  _Out_   PHIDP_DATA           DataList,
  _Inout_ PULONG               DataLength,
  _In_    PHIDP_PREPARSED_DATA PreparsedData,
  _In_    PCHAR                Report,
  _In_    ULONG                ReportLength )
{
  SK_LOG_FIRST_CALL

  NTSTATUS ret =
    HidP_GetData_Original ( ReportType, DataList,
                              DataLength, PreparsedData,
                                Report, ReportLength );


  // De we want block this I/O?
  bool filter = false;

  if (          ret == HIDP_STATUS_SUCCESS &&
       ( ReportType == HidP_Input          ||
         ReportType == HidP_Output ) )
  {
    // This will classify the data for us, so don't record this event yet.
    filter =
      SK_HID_FilterPreparsedData (PreparsedData);
  }


  if (filter && (DataLength != nullptr))
  {
    using  HidP_MaxDataListLength_pfn = ULONG (WINAPI *)(HIDP_REPORT_TYPE, PHIDP_PREPARSED_DATA);
    static HidP_MaxDataListLength_pfn
        SK_HidP_MaxDataListLength =
          (HidP_MaxDataListLength_pfn)SK_GetProcAddress (L"hid.dll",
          "HidP_MaxDataListLength");

    if ( SK_HidP_MaxDataListLength != nullptr &&
         SK_HidP_MaxDataListLength (ReportType, PreparsedData) != 0 )
    {
      if (    DataList != nullptr)
      memset (DataList, 0, sizeof (HIDP_DATA) * *DataLength);
                                                *DataLength = 0;
    }
  }

  return ret;
}

OpenFileMappingW_pfn      OpenFileMappingW_Original      = nullptr;
CreateFileMappingW_pfn    CreateFileMappingW_Original    = nullptr;
ReadFile_pfn              ReadFile_Original              = nullptr;
ReadFileEx_pfn            ReadFileEx_Original            = nullptr;
GetOverlappedResult_pfn   GetOverlappedResult_Original   = nullptr;
GetOverlappedResultEx_pfn GetOverlappedResultEx_Original = nullptr;
DeviceIoControl_pfn       DeviceIoControl_Original       = nullptr;

static CreateFileA_pfn CreateFileA_Original = nullptr;
static CreateFileW_pfn CreateFileW_Original = nullptr;
static CreateFile2_pfn CreateFile2_Original = nullptr;

CreateFile2_pfn              SK_CreateFile2 = nullptr;
ReadFile_pfn                    SK_ReadFile = nullptr;

BOOL
WINAPI
SK_DeviceIoControl (HANDLE       hDevice,
                    DWORD        dwIoControlCode,
                    LPVOID       lpInBuffer,
                    DWORD        nInBufferSize,
                    LPVOID       lpOutBuffer,
                    DWORD        nOutBufferSize,
                    LPDWORD      lpBytesReturned,
                    LPOVERLAPPED lpOverlapped)
{
  static DeviceIoControl_pfn _DeviceIoControl = nullptr;

  if (DeviceIoControl_Original != nullptr)
  {
    return DeviceIoControl_Original (
      hDevice, dwIoControlCode, lpInBuffer, nInBufferSize,
        lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped
    );
  }

  else
  {
    if (_DeviceIoControl == nullptr)
    {
      _DeviceIoControl =
        (DeviceIoControl_pfn)SK_GetProcAddress (L"kernel32.dll",
        "DeviceIoControl");
    }
  }

  if (_DeviceIoControl != nullptr)
  {
    return _DeviceIoControl (
      hDevice, dwIoControlCode, lpInBuffer, nInBufferSize,
        lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped
    );
  }

  return FALSE;
}

static
HANDLE
WINAPI
OpenFileMappingW_Detour (DWORD   dwDesiredAccess,
                         BOOL    bInheritHandle,
                         LPCWSTR lpName)
{
  SK_LOG_FIRST_CALL

  SK_LOGi0 (L"OpenFileMappingW (%ws)", lpName);

  return
    OpenFileMappingW_Original (
      dwDesiredAccess, bInheritHandle, lpName
    );
}

static
HANDLE
WINAPI
CreateFileMappingW_Detour (HANDLE                hFile,
                           LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
                           DWORD                 flProtect,
                           DWORD                 dwMaximumSizeHigh,
                           DWORD                 dwMaximumSizeLow,
                           LPCWSTR               lpName)
{
  SK_LOG_FIRST_CALL

  SK_LOGi0 (L"CreateFileMappingW (%ws)", lpName);

  return
    CreateFileMappingW_Original (
      hFile, lpFileMappingAttributes, flProtect,
        dwMaximumSizeHigh, dwMaximumSizeLow, lpName
    );
}

static
BOOL
WINAPI
ReadFile_Detour (HANDLE       hFile,
                 LPVOID       lpBuffer,
                 DWORD        nNumberOfBytesToRead,
                 LPDWORD      lpNumberOfBytesRead,
                 LPOVERLAPPED lpOverlapped)
{
  SK_LOG_FIRST_CALL

  const auto &[ dev_file_type, dev_ptr, dev_allowed ] =
    SK_Input_GetDeviceFileAndState (hFile);

  switch (dev_file_type)
  {
    case SK_Input_DeviceFileType::HID:
    {
      auto hid_file =
        (SK_HID_DeviceFile *)dev_ptr;

      auto pTlsBackedBuffer =
        SK_TLS_Bottom ()->scratch_memory->log.
                             formatted_output.alloc (nNumberOfBytesToRead);

      auto pBuffer =
        // Overlapped reads should pass their original pointer to ReadFile,
        //   unless we intend to block them (and cancel said IO request).
        ((lpBuffer != nullptr && lpOverlapped == nullptr) || (! dev_allowed)) ?
                                                             pTlsBackedBuffer : lpBuffer;

      BOOL bRet =
        ReadFile_Original (
          hFile, pBuffer, nNumberOfBytesToRead,
            lpNumberOfBytesRead, lpOverlapped
        );

      if (bRet)
      {
        if (! dev_allowed)
        {
          SK_ReleaseAssert (lpBuffer != nullptr);

          if (lpOverlapped == nullptr || CancelIo (hFile))
          {
            SK_HID_HIDE (hid_file->device_type);

            if (lpOverlapped == nullptr) // lpNumberOfBytesRead MUST be non-null
            {
              if (hid_file->last_data_read.size () >= *lpNumberOfBytesRead)
              {
                // Give the game old data, from before we started blocking stuff...
                memcpy (lpBuffer, hid_file->last_data_read.data (), *lpNumberOfBytesRead);
              }
            }

            else
            {
              SK_RunOnce (
                SK_LOGi0 (L"ReadFile HID IO Cancelled")
              );
            }

            return TRUE;
          }
        }

        else
        {
          SK_HID_READ (hid_file->device_type);

          if (lpNumberOfBytesRead != nullptr && lpBuffer != nullptr && lpOverlapped == nullptr)
          {
            if (hid_file->last_data_read.size () < nNumberOfBytesToRead)
                hid_file->last_data_read.resize (  nNumberOfBytesToRead * 2);

            memcpy (hid_file->last_data_read.data (), pTlsBackedBuffer, *lpNumberOfBytesRead);
            memcpy (lpBuffer,                         pTlsBackedBuffer, *lpNumberOfBytesRead);
          }

          SK_HID_VIEW (hid_file->device_type);
        }
      }

      return bRet;
    } break;

    case SK_Input_DeviceFileType::NVIDIA:
    {
      SK_MessageBus_Backend->markRead (2);
    } break;
  }

  return
    ReadFile_Original (
      hFile, lpBuffer, nNumberOfBytesToRead,
        lpNumberOfBytesRead, lpOverlapped );
}

static
BOOL
WINAPI
ReadFileEx_Detour (HANDLE                          hFile,
                   LPVOID                          lpBuffer,
                   DWORD                           nNumberOfBytesToRead,
                   LPOVERLAPPED                    lpOverlapped,
                   LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
  SK_LOG_FIRST_CALL

  BOOL bRet =
    ReadFileEx_Original (
      hFile, lpBuffer, nNumberOfBytesToRead,
        lpOverlapped, lpCompletionRoutine
    );

  // Early-out
  if (! bRet)
    return bRet;

  const auto &[ dev_file_type, dev_ptr, dev_allowed ] =
    SK_Input_GetDeviceFileAndState (hFile);

  switch (dev_file_type)
  {
    case SK_Input_DeviceFileType::HID:
    {
      auto hid_file =
        (SK_HID_DeviceFile *)dev_ptr;

      if (! dev_allowed)
      {
        if (CancelIo (hFile))
        {
          SK_HID_HIDE (hid_file->device_type);

          SK_RunOnce (
            SK_LOGi0 (L"ReadFileEx HID IO Cancelled")
          );

          return TRUE;
        }
      }

      SK_HID_READ (hid_file->device_type);
      SK_HID_VIEW (hid_file->device_type);
    } break;

    case SK_Input_DeviceFileType::NVIDIA:
    {
      SK_MessageBus_Backend->markRead (2);
    }
  }

  return bRet;
}

bool
SK_StrSupW (const wchar_t *wszString, const wchar_t *wszPattern, int len = -1)
{
  if ( nullptr == wszString || wszPattern == nullptr )
    return        wszString == wszPattern;

  if (len == -1)
      len = sk::narrow_cast <int> (wcslen (wszPattern));

  return
    0 == StrCmpNIW (wszString, wszPattern, len);
}

bool
SK_StrSupA (const char *szString, const char *szPattern, int len = -1)
{
  if ( nullptr == szString || szPattern == nullptr )
    return        szString == szPattern;

  if (len == -1)
      len = sk::narrow_cast <int> (strlen (szPattern));

  return
    0 == StrCmpNIA (szString, szPattern, len);
}

static
HANDLE
WINAPI
CreateFileA_Detour (LPCSTR                lpFileName,
                    DWORD                 dwDesiredAccess,
                    DWORD                 dwShareMode,
                    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                    DWORD                 dwCreationDisposition,
                    DWORD                 dwFlagsAndAttributes,
                    HANDLE                hTemplateFile)
{
  SK_LOG_FIRST_CALL

  HANDLE hRet =
    CreateFileA_Original (
      lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition,
          dwFlagsAndAttributes, hTemplateFile );

  const bool bSuccess = (LONG_PTR)hRet > 0;

  // Examine all UNC paths closely, some of these files are
  //   input devices in disguise...
  if ( bSuccess && SK_StrSupA (lpFileName, R"(\\)", 2) )
  {
    // Log UNC paths we don't expect for debugging
    static const bool bTraceAllUNC =
      ( config.system.log_level > 0 || config.file_io.trace_reads );

    std::wstring widefilename =
       SK_UTF8ToWideChar (lpFileName);

    const auto lpWideFileName =
                 widefilename.c_str ();

    if (SK_StrSupA (lpFileName, R"(\\?\hid)", 7))
    {
      bool bSkipExistingFile = false;

      if ( auto hid_file  = SK_HID_DeviceFiles.find (hRet);
                hid_file != SK_HID_DeviceFiles.end  (    ) && StrCmpIW (lpWideFileName,
                hid_file->second.wszDevicePath) == 0 )
      {
        bSkipExistingFile = true;
      }

      if (! bSkipExistingFile)
      {
        SK_HID_DeviceFile hid_file (hRet, lpWideFileName);

        if (hid_file.device_type != sk_input_dev_type::Other)
        {
          SK_Input_DeviceFiles.insert (hRet);
        }

        auto&& dev_file =
          SK_HID_DeviceFiles [hRet];

        dev_file.last_data_read.clear ();
        dev_file = std::move (hid_file);
      }
    }

    else if (SK_StrSupA (lpFileName, R"(\\.\pipe)", 8))
    {
#if 0 // Add option to disable MessageBus? It seems harmless
      if (config.input.gamepad.steam.disabled_to_game)
      {
        // If we can't close this, something's gone wrong...
        SK_ReleaseAssert (
          SK_CloseHandle (hRet)
        );

        return
          INVALID_HANDLE_VALUE;
      }
#endif

      SK_Input_DeviceFiles.insert (  hRet                );
      SK_NVIDIA_DeviceFiles         [hRet] =
               SK_NVIDIA_DeviceFile (hRet, lpWideFileName);
    }

    else if (bTraceAllUNC && SK_StrSupA (lpFileName, R"(\\)", 2))
    {
      SK_LOGi0 (L"CreateFileA (%hs)", lpFileName);
    }
  }

  return hRet;
}

static
HANDLE
WINAPI
CreateFile2_Detour (
  _In_     LPCWSTR                           lpFileName,
  _In_     DWORD                             dwDesiredAccess,
  _In_     DWORD                             dwShareMode,
  _In_     DWORD                             dwCreationDisposition,
  _In_opt_ LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams )
{
  SK_LOG_FIRST_CALL

  HANDLE hRet =
    CreateFile2_Original (
      lpFileName, dwDesiredAccess, dwShareMode,
        dwCreationDisposition, pCreateExParams );

  const bool bSuccess = (LONG_PTR)hRet > 0;

  // Examine all UNC paths closely, some of these files are
  //   input devices in disguise...
  if ( bSuccess && SK_StrSupW (lpFileName, LR"(\\)", 2) )
  {
    // Log UNC paths we don't expect for debugging
    static const bool bTraceAllUNC =
      ( config.system.log_level > 0 || config.file_io.trace_reads );

    if (SK_StrSupW (lpFileName, LR"(\\?\hid)", 7))
    {
      bool bSkipExistingFile = false;

      if ( auto hid_file  = SK_HID_DeviceFiles.find (hRet);
                hid_file != SK_HID_DeviceFiles.end  (    ) && StrCmpIW (lpFileName,
                hid_file->second.wszDevicePath) == 0 )
      {
        bSkipExistingFile = true;
      }

      if (! bSkipExistingFile)
      {
        SK_HID_DeviceFile hid_file (hRet, lpFileName);

        if (hid_file.device_type != sk_input_dev_type::Other)
        {
          SK_Input_DeviceFiles.insert (hRet);
        }

        auto&& dev_file =
          SK_HID_DeviceFiles [hRet];

        dev_file.last_data_read.clear ();
        dev_file = std::move (hid_file);
      }
    }

    else if (SK_StrSupW (lpFileName, LR"(\\.\pipe)", 8))
    {
#if 0 // Add option to disable MessageBus? It seems harmless
      if (config.input.gamepad.steam.disabled_to_game)
      {
        // If we can't close this, something's gone wrong...
        SK_ReleaseAssert (
          SK_CloseHandle (hRet)
        );

        return
          INVALID_HANDLE_VALUE;
      }
#endif

      SK_Input_DeviceFiles.insert (  hRet            );
      SK_NVIDIA_DeviceFiles         [hRet] =
               SK_NVIDIA_DeviceFile (hRet, lpFileName);
    }

    else if (bTraceAllUNC && SK_StrSupW (lpFileName, LR"(\\)", 2))
    {
      SK_LOGi0 (L"CreateFile2 (%ws)", lpFileName);
    }
  }

  return hRet;
}

static
HANDLE
WINAPI
CreateFileW_Detour ( LPCWSTR               lpFileName,
                     DWORD                 dwDesiredAccess,
                     DWORD                 dwShareMode,
                     LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                     DWORD                 dwCreationDisposition,
                     DWORD                 dwFlagsAndAttributes,
                     HANDLE                hTemplateFile )
{
  SK_LOG_FIRST_CALL

  HANDLE hRet =
    CreateFileW_Original (
      lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition,
          dwFlagsAndAttributes, hTemplateFile );

  const bool bSuccess = (LONG_PTR)hRet > 0;

  // Examine all UNC paths closely, some of these files are
  //   input devices in disguise...
  if ( bSuccess && SK_StrSupW (lpFileName, LR"(\\)", 2) )
  {
    // Log UNC paths we don't expect for debugging
    static const bool bTraceAllUNC =
      ( config.system.log_level > 0 || config.file_io.trace_reads );

    if (SK_StrSupW (lpFileName, LR"(\\?\hid)", 7))
    {
      bool bSkipExistingFile = false;
      
      if ( auto hid_file  = SK_HID_DeviceFiles.find (hRet);
                hid_file != SK_HID_DeviceFiles.end  (    ) && StrCmpIW (lpFileName,
                hid_file->second.wszDevicePath) == 0 )
      {
        bSkipExistingFile = true;
      }

      if (! bSkipExistingFile)
      {
        SK_HID_DeviceFile hid_file (hRet, lpFileName);

        if (hid_file.device_type != sk_input_dev_type::Other)
        {
          SK_Input_DeviceFiles.insert (hRet);
        }

        auto&& dev_file =
          SK_HID_DeviceFiles [hRet];

        dev_file.last_data_read.clear ();
        dev_file = std::move (hid_file);
      }
    }

    else if (SK_StrSupW (lpFileName, LR"(\\.\pipe)", 8))
    {
#if 0 // Add option to disable MessageBus? It seems harmless
      if (config.input.gamepad.steam.disabled_to_game)
      {
        // If we can't close this, something's gone wrong...
        SK_ReleaseAssert (
          SK_CloseHandle (hRet)
        );

        return
          INVALID_HANDLE_VALUE;
      }
#endif

      SK_Input_DeviceFiles.insert (  hRet            );
      SK_NVIDIA_DeviceFiles         [hRet] =
               SK_NVIDIA_DeviceFile (hRet, lpFileName);
    }

    else if (bTraceAllUNC && SK_StrSupW (lpFileName, LR"(\\)", 2))
    {
      SK_LOGi0 (L"CreateFileW (%ws)", lpFileName);
    }
  }

  return hRet;
}

// This is the most common way that games that manually open USB HID
//   device files actually read their input (usually the non-Ex variant).
BOOL
WINAPI
GetOverlappedResultEx_Detour (HANDLE       hFile,
                              LPOVERLAPPED lpOverlapped,
                              LPDWORD      lpNumberOfBytesTransferred,
                              DWORD        dwMilliseconds,
                              BOOL         bWait)
{
  SK_LOG_FIRST_CALL

  const auto &[ dev_file_type, dev_ptr, dev_allowed ] =
    SK_Input_GetDeviceFileAndState (hFile);

  switch (dev_file_type)
  {
    case SK_Input_DeviceFileType::HID:
    {
      auto hid_file =
        (SK_HID_DeviceFile *)dev_ptr;

      BOOL bRet = TRUE;

      if (! dev_allowed)
      {
        if (CancelIo (hFile))
        {
          SK_HID_HIDE (hid_file->device_type);

          SK_RunOnce (
            SK_LOGi0 (L"GetOverlappedResultEx HID IO Cancelled")
          );
        }
      }

      else
      {
        bRet =
          GetOverlappedResultEx_Original (
            hFile, lpOverlapped, lpNumberOfBytesTransferred, dwMilliseconds, bWait
          );
      }

      if (bRet != FALSE)
      {
        if (dev_allowed)
        {
          SK_HID_READ (hid_file->device_type);
          SK_HID_VIEW (hid_file->device_type);
          // We did the bulk of this processing in ReadFile_Detour (...)
          //   nothing to be done here for now.
        }
      }

      return bRet;
    } break;

    case SK_Input_DeviceFileType::NVIDIA:
    {
      SK_MessageBus_Backend->markRead (2);
    } break;
  }

  return
    GetOverlappedResultEx_Original (
      hFile, lpOverlapped, lpNumberOfBytesTransferred, dwMilliseconds, bWait
    );
}

BOOL
WINAPI
GetOverlappedResult_Detour (HANDLE       hFile,
                            LPOVERLAPPED lpOverlapped,
                            LPDWORD      lpNumberOfBytesTransferred,
                            BOOL         bWait)
{
  SK_LOG_FIRST_CALL

  const auto &[ dev_file_type, dev_ptr, dev_allowed ] =
    SK_Input_GetDeviceFileAndState (hFile);

  switch (dev_file_type)
  {
    case SK_Input_DeviceFileType::HID:
    {
      auto hid_file =
        (SK_HID_DeviceFile *)dev_ptr;

      BOOL bRet = TRUE;

      if (! dev_allowed)
      {
        if (CancelIo (hFile))
        {
          SK_HID_HIDE (hid_file->device_type);

          SK_RunOnce (
            SK_LOGi0 (L"GetOverlappedResult HID IO Cancelled")
          );
        }
      }

      else
        bRet =
          GetOverlappedResult_Original (
            hFile, lpOverlapped, lpNumberOfBytesTransferred,
              bWait
          );

      if (bRet != FALSE)
      {
        if (dev_allowed)
        {
          SK_HID_READ (hid_file->device_type);
          SK_HID_VIEW (hid_file->device_type);
          // We did the bulk of this processing in ReadFile_Detour (...)
          //   nothing to be done here for now.
        }
      }

      return bRet;
    }

    case SK_Input_DeviceFileType::NVIDIA:
    {
      SK_MessageBus_Backend->markRead (2);
    } break;
  }

  return
    GetOverlappedResult_Original (
      hFile, lpOverlapped, lpNumberOfBytesTransferred,
        bWait
    );
}

BOOL
WINAPI
DeviceIoControl_Detour (HANDLE       hDevice,
                        DWORD        dwIoControlCode,
                        LPVOID       lpInBuffer,
                        DWORD        nInBufferSize,
                        LPVOID       lpOutBuffer,
                        DWORD        nOutBufferSize,
                        LPDWORD      lpBytesReturned,
                        LPOVERLAPPED lpOverlapped)
{
  SK_LOG_FIRST_CALL

  const DWORD dwDeviceType =
    DEVICE_TYPE_FROM_CTL_CODE (dwIoControlCode),
        dwFunctionNum =       (dwIoControlCode >> 2) & 0xFFF;

  SK_LOGi2 (
    L"DeviceIoControl %p (Type: %x, Function: %d)", hDevice,
      dwDeviceType, dwFunctionNum
  );

  return
    DeviceIoControl_Original (
      hDevice, dwIoControlCode, lpInBuffer, nInBufferSize,
        lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped
    );
}

//
// We don't actually call any of these, the hooks will be routed
//   through a copy of SetupAPI.dll that SK maintains, instead of
//     one that Valve can render non-functional.
//
static SetupDiGetClassDevsW_pfn
       SetupDiGetClassDevsW_Original             = nullptr;
static SetupDiGetClassDevsA_pfn
       SetupDiGetClassDevsA_Original             = nullptr;
static SetupDiGetClassDevsExW_pfn
       SetupDiGetClassDevsExW_Original           = nullptr;
static SetupDiGetClassDevsExA_pfn
       SetupDiGetClassDevsExA_Original           = nullptr;
static SetupDiEnumDeviceInterfaces_pfn
       SetupDiEnumDeviceInterfaces_Original      = nullptr;
static SetupDiGetDeviceInterfaceDetailW_pfn
       SetupDiGetDeviceInterfaceDetailW_Original = nullptr;
static SetupDiGetDeviceInterfaceDetailA_pfn
       SetupDiGetDeviceInterfaceDetailA_Original = nullptr;
static SetupDiDestroyDeviceInfoList_pfn
       SetupDiDestroyDeviceInfoList_Original     = nullptr;

HDEVINFO
WINAPI
SetupDiGetClassDevsW_Detour (
  _In_opt_ const GUID   *ClassGuid,
  _In_opt_       PCWSTR  Enumerator,
  _In_opt_       HWND    hwndParent,
  _In_           DWORD   Flags )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetClassDevsW (ClassGuid, Enumerator, hwndParent, Flags);
}

HDEVINFO
WINAPI
SetupDiGetClassDevsA_Detour (
  _In_opt_ const GUID  *ClassGuid,
  _In_opt_       PCSTR  Enumerator,
  _In_opt_       HWND   hwndParent,
  _In_           DWORD  Flags )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetClassDevsA (ClassGuid, Enumerator, hwndParent, Flags);
}

HDEVINFO
WINAPI
SetupDiGetClassDevsExW_Detour (
  _In_opt_ CONST GUID    *ClassGuid,
  _In_opt_       PCWSTR   Enumerator,
  _In_opt_       HWND     hwndParent,
  _In_           DWORD    Flags,
  _In_opt_       HDEVINFO DeviceInfoSet,
  _In_opt_       PCWSTR   MachineName,
  _Reserved_     PVOID    Reserved )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetClassDevsExW (
      ClassGuid, Enumerator, hwndParent, Flags,
        DeviceInfoSet, MachineName, Reserved );
}

HDEVINFO
WINAPI
SetupDiGetClassDevsExA_Detour (
  _In_opt_ CONST GUID    *ClassGuid,
  _In_opt_       PCSTR    Enumerator,
  _In_opt_       HWND     hwndParent,
  _In_           DWORD    Flags,
  _In_opt_       HDEVINFO DeviceInfoSet,
  _In_opt_       PCSTR    MachineName,
  _Reserved_     PVOID    Reserved )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetClassDevsExA (
      ClassGuid, Enumerator, hwndParent, Flags,
        DeviceInfoSet, MachineName, Reserved );
}

BOOL
WINAPI
SetupDiEnumDeviceInterfaces_Detour (
  _In_       HDEVINFO                  DeviceInfoSet,
  _In_opt_   PSP_DEVINFO_DATA          DeviceInfoData,
  _In_ CONST GUID                     *InterfaceClassGuid,
  _In_       DWORD                     MemberIndex,
  _Out_      PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiEnumDeviceInterfaces ( DeviceInfoSet, DeviceInfoData,
                                       InterfaceClassGuid, MemberIndex,
                                         DeviceInterfaceData );
}

BOOL
WINAPI
SetupDiGetDeviceInterfaceDetailW_Detour (
  _In_      HDEVINFO                           DeviceInfoSet,
  _In_      PSP_DEVICE_INTERFACE_DATA          DeviceInterfaceData,
  _Out_writes_bytes_to_opt_(DeviceInterfaceDetailDataSize, *RequiredSize)
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
  _In_      DWORD                              DeviceInterfaceDetailDataSize,
  _Out_opt_ _Out_range_(>=, sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            PDWORD                             RequiredSize,
  _Out_opt_ PSP_DEVINFO_DATA                   DeviceInfoData )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetDeviceInterfaceDetailW ( DeviceInfoSet,
                                          DeviceInterfaceData,
                                          DeviceInterfaceDetailData,
                                          DeviceInterfaceDetailDataSize,
                                                           RequiredSize,
                                                           DeviceInfoData );
}

BOOL
WINAPI
SetupDiGetDeviceInterfaceDetailA_Detour (
  _In_      HDEVINFO                           DeviceInfoSet,
  _In_      PSP_DEVICE_INTERFACE_DATA          DeviceInterfaceData,
  _Out_writes_bytes_to_opt_(DeviceInterfaceDetailDataSize, *RequiredSize)
            PSP_DEVICE_INTERFACE_DETAIL_DATA_A DeviceInterfaceDetailData,
  _In_      DWORD                              DeviceInterfaceDetailDataSize,
  _Out_opt_ _Out_range_(>=, sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A))
            PDWORD                             RequiredSize,
  _Out_opt_ PSP_DEVINFO_DATA                   DeviceInfoData )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiGetDeviceInterfaceDetailA ( DeviceInfoSet,
                                          DeviceInterfaceData,
                                          DeviceInterfaceDetailData,
                                          DeviceInterfaceDetailDataSize,
                                                           RequiredSize,
                                                           DeviceInfoData );
}

BOOL
WINAPI
SetupDiDestroyDeviceInfoList_Detour (
  _In_ HDEVINFO DeviceInfoSet )
{
  if (SK_GetCallingDLL () != SK_GetDLL ())
  {
    SK_LOG_FIRST_CALL
  }

  return
    SK_SetupDiDestroyDeviceInfoList (DeviceInfoSet);
}

void
SK_Input_HookHID (void)
{
  if (! config.input.gamepad.hook_hid)
    return;

  static volatile LONG hooked = FALSE;

  if (! InterlockedCompareExchange (&hooked, TRUE, FALSE))
  {
    SK_LOG0 ( ( L"Game uses HID, installing input hooks..." ),
                L"  Input   " );

    if (config.input.gamepad.disable_hid)
    {
      SK_CreateDLLHook2 (     L"HID.DLL",
                               "HidP_GetData",
                                HidP_GetData_Detour,
       static_cast_p2p <void> (&HidP_GetData_Original) );

      SK_CreateDLLHook2 (     L"HID.DLL",
                               "HidD_GetPreparsedData",
                                HidD_GetPreparsedData_Detour,
       static_cast_p2p <void> (&HidD_GetPreparsedData_Original) );

      SK_CreateDLLHook2 (     L"HID.DLL",
                               "HidD_FreePreparsedData",
                                HidD_FreePreparsedData_Detour,
       static_cast_p2p <void> (&HidD_FreePreparsedData_Original) );

      HidP_GetCaps_Original =
        (HidP_GetCaps_pfn)SK_GetProcAddress ( SK_GetModuleHandle (L"HID.DLL"),
                                              "HidP_GetCaps" );
    }

    SK_CreateDLLHook2 (     L"HID.DLL",
                             "HidD_GetFeature",
                              HidD_GetFeature_Detour,
     static_cast_p2p <void> (&HidD_GetFeature_Original) );

    SK_CreateDLLHook2 (     L"HID.DLL",
                             "HidP_GetUsages",
                              HidP_GetUsages_Detour,
     static_cast_p2p <void> (&HidP_GetUsages_Original) );

    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "CreateFileA",
                               CreateFileA_Detour,
      static_cast_p2p <void> (&CreateFileA_Original) );

    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "CreateFileW",
                               CreateFileW_Detour,
      static_cast_p2p <void> (&CreateFileW_Original) );

    // This was added in Windows 8, be mindful...
    //   it might not exist on WINE or in compat mode, etc.
    if (SK_GetProcAddress (L"kernel32.dll", "CreateFile2"))
    {
      SK_CreateDLLHook2 (      L"kernel32.dll",
                                "CreateFile2",
                                 CreateFile2_Detour,
        static_cast_p2p <void> (&CreateFile2_Original) );
    }

    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "DeviceIoControl",
                               DeviceIoControl_Detour,
      static_cast_p2p <void> (&DeviceIoControl_Original) );

#if 1
    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "ReadFile",
                               ReadFile_Detour,
      static_cast_p2p <void> (&ReadFile_Original) );

    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "ReadFileEx",
                               ReadFileEx_Detour,
      static_cast_p2p <void> (&ReadFileEx_Original) );
#endif

    // Hooked and then forwarded to the GetOverlappedResultEx hook
    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "GetOverlappedResult",
                               GetOverlappedResult_Detour,
      static_cast_p2p <void> (&GetOverlappedResult_Original) );

    SK_CreateDLLHook2 (      L"kernel32.dll",
                              "GetOverlappedResultEx",
                               GetOverlappedResultEx_Detour,
      static_cast_p2p <void> (&GetOverlappedResultEx_Original) );

    if (ReadAcquire (&__SK_Init) > 0) SK_ApplyQueuedHooks ();

    InterlockedIncrementRelease (&hooked);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&hooked, 2);
}

bool
SK_Input_PreHookHID (void)
{
  static std::filesystem::path path_to_driver_base =
        (std::filesystem::path (SK_GetInstallPath ()) /
                               LR"(Drivers\HID)"),
                                     driver_name =
                  SK_RunLHIfBitness (64, L"HID_SK64.dll",
                                         L"HID_SK32.dll"),
                             path_to_driver = 
                             path_to_driver_base /
                                     driver_name;

  static std::filesystem::path path_to_setupapi_base =
        (std::filesystem::path (SK_GetInstallPath ()) /
                               LR"(Drivers\SetupAPI)"),
                                     setupapi_name =
                  SK_RunLHIfBitness (64, L"SetupAPI_SK64.dll",
                                         L"SetupAPI_SK32.dll"),
                             path_to_setupapi = 
                             path_to_setupapi_base /
                                     setupapi_name;

  static std::filesystem::path path_to_kernel_base =
        (std::filesystem::path (SK_GetInstallPath ()) /
                               LR"(Drivers\Kernel32)"),
                                     kernel_name =
                  SK_RunLHIfBitness (64, L"Kernel32_SK64.dll",
                                         L"Kernel32_SK32.dll"),
                             path_to_kernel = 
                             path_to_kernel_base /
                                     kernel_name;

  static const auto *pSystemDirectory =
    SK_GetSystemDirectory ();

  std::filesystem::path
    path_to_system_hid =
      (std::filesystem::path (pSystemDirectory) / L"hid.dll");

  std::filesystem::path
    path_to_system_kernel =
      (std::filesystem::path (pSystemDirectory) / L"kernel32.dll");

  std::filesystem::path
    path_to_system_setupapi =
      (std::filesystem::path (pSystemDirectory) / L"SetupAPI.dll");

  std::error_code ec =
    std::error_code ();

  if (std::filesystem::exists (path_to_system_hid, ec))
  {
    if ( (! std::filesystem::exists ( path_to_driver,      ec))||
         (! SK_Assert_SameDLLVersion (path_to_driver.    c_str (),
                                      path_to_system_hid.c_str ()) ) )
    { SK_CreateDirectories           (path_to_driver.c_str ());

      if (   std::filesystem::exists (path_to_system_hid,                 ec))
      { std::filesystem::remove      (                    path_to_driver, ec);
        std::filesystem::copy_file   (path_to_system_hid, path_to_driver, ec);
      }
    }
  }

  if (std::filesystem::exists (path_to_system_kernel, ec))
  {
    if ( (! std::filesystem::exists ( path_to_kernel,         ec))||
         (! SK_Assert_SameDLLVersion (path_to_kernel.       c_str (),
                                      path_to_system_kernel.c_str ()) ) )
    { SK_CreateDirectories           (path_to_kernel.c_str ());

      if (   std::filesystem::exists (path_to_system_kernel,                 ec))
      { std::filesystem::remove      (                       path_to_kernel, ec);
        std::filesystem::copy_file   (path_to_system_kernel, path_to_kernel, ec);
      }
    }
  }

  if (std::filesystem::exists (path_to_system_setupapi, ec))
  {
    if ( (! std::filesystem::exists ( path_to_setupapi,         ec))||
         (! SK_Assert_SameDLLVersion (path_to_setupapi.       c_str (),
                                      path_to_system_setupapi.c_str ()) ) )
    { SK_CreateDirectories           (path_to_setupapi.c_str ());

      if (   std::filesystem::exists (path_to_system_setupapi,                   ec))
      { std::filesystem::remove      (                         path_to_setupapi, ec);
        std::filesystem::copy_file   (path_to_system_setupapi, path_to_setupapi, ec);
      }
    }
  }

  HMODULE hModHID =
    SK_LoadLibraryW (path_to_driver.c_str ());

  HMODULE hModKernel32 =
    SK_LoadLibraryW (path_to_kernel.c_str ());

  HMODULE hModSetupAPI =
    SK_LoadLibraryW (path_to_setupapi.c_str ());

  if (! (hModHID && hModKernel32 && hModSetupAPI))
  {
    SK_LOGi0 (L"Missing required HID DLLs (!!)");
  }
                               
  SK_HidD_GetPreparsedData =
    (HidD_GetPreparsedData_pfn)SK_GetProcAddress (hModHID,
    "HidD_GetPreparsedData");

  SK_HidD_FreePreparsedData =
    (HidD_FreePreparsedData_pfn)SK_GetProcAddress (hModHID,
    "HidD_FreePreparsedData");

  SK_HidD_GetFeature =
    (HidD_GetFeature_pfn)SK_GetProcAddress (hModHID,
    "HidD_GetFeature");

  SK_HidP_GetData =
    (HidP_GetData_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetData");

  SK_HidP_GetCaps =
    (HidP_GetCaps_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetCaps");

  SK_HidP_GetButtonCaps =
    (HidP_GetButtonCaps_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetButtonCaps");

  SK_HidP_GetValueCaps =
    (HidP_GetValueCaps_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetValueCaps");

  SK_HidP_GetUsages =
    (HidP_GetUsages_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetUsages");

  SK_HidP_GetUsageValue =
    (HidP_GetUsageValue_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetUsageValue");

  SK_HidP_GetUsageValueArray =
    (HidP_GetUsageValueArray_pfn)SK_GetProcAddress (hModHID,
    "HidP_GetUsageValueArray");

  SK_HidD_GetInputReport =
    (HidD_GetInputReport_pfn)SK_GetProcAddress (hModHID,
    "HidD_GetInputReport");

  SK_CreateFile2 =
    (CreateFile2_pfn)SK_GetProcAddress (hModKernel32,
    "CreateFile2");

  SK_ReadFile =
    (ReadFile_pfn)SK_GetProcAddress (hModKernel32,
    "ReadFile");

  SK_SetupDiGetClassDevsW =
    (SetupDiGetClassDevsW_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetClassDevsW");

  SK_SetupDiGetClassDevsA =
    (SetupDiGetClassDevsA_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetClassDevsA");

  SK_SetupDiGetClassDevsExW =
    (SetupDiGetClassDevsExW_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetClassDevsExW");

  SK_SetupDiGetClassDevsExA =
    (SetupDiGetClassDevsExA_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetClassDevsExA");

  SK_SetupDiEnumDeviceInfo =
    (SetupDiEnumDeviceInfo_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiEnumDeviceInfo");

  SK_SetupDiEnumDeviceInterfaces =
    (SetupDiEnumDeviceInterfaces_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiEnumDeviceInterfaces");

  SK_SetupDiGetDeviceInterfaceDetailW =
    (SetupDiGetDeviceInterfaceDetailW_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetDeviceInterfaceDetailW");

  SK_SetupDiGetDeviceInterfaceDetailA =
    (SetupDiGetDeviceInterfaceDetailA_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiGetDeviceInterfaceDetailA");

  SK_SetupDiDestroyDeviceInfoList =
    (SetupDiDestroyDeviceInfoList_pfn)SK_GetProcAddress (hModSetupAPI,
    "SetupDiDestroyDeviceInfoList");

  if (config.input.gamepad.steam.disabled_to_game)
  {
    // These causes periodic hitches (thanks Valve), so hook them and
    //   keep Valve's dirty hands off of them.
    SK_RunOnce (
      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetClassDevsW",
                                           SetupDiGetClassDevsW_Detour,
                  static_cast_p2p <void> (&SetupDiGetClassDevsW_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetClassDevsA",
                                           SetupDiGetClassDevsA_Detour,
                  static_cast_p2p <void> (&SetupDiGetClassDevsA_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetClassDevsExW",
                                           SetupDiGetClassDevsExW_Detour,
                  static_cast_p2p <void> (&SetupDiGetClassDevsExW_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetClassDevsExA",
                                           SetupDiGetClassDevsExA_Detour,
                  static_cast_p2p <void> (&SetupDiGetClassDevsExA_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiEnumDeviceInterfaces",
                                           SetupDiEnumDeviceInterfaces_Detour,
                  static_cast_p2p <void> (&SetupDiEnumDeviceInterfaces_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetDeviceInterfaceDetailW",
                                           SetupDiGetDeviceInterfaceDetailW_Detour,
                  static_cast_p2p <void> (&SetupDiGetDeviceInterfaceDetailW_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiGetDeviceInterfaceDetailA",
                                           SetupDiGetDeviceInterfaceDetailA_Detour,
                  static_cast_p2p <void> (&SetupDiGetDeviceInterfaceDetailA_Original));

      SK_CreateDLLHook2 (L"SetupAPI.dll", "SetupDiDestroyDeviceInfoList",
                                           SetupDiDestroyDeviceInfoList_Detour,
                  static_cast_p2p <void> (&SetupDiDestroyDeviceInfoList_Original));
    );
  }

  if (! config.input.gamepad.hook_hid)
    return false;

  static
    sk_import_test_s tests [] = {
      { "hid.dll", false }
    };

  SK_TestImports (
    SK_GetModuleHandle (nullptr), tests, 1
  );

  if (tests [0].used || SK_GetModuleHandle (L"hid.dll"))
  {
    SK_Input_HookHID ();

    return true;
  }

  return false;
}