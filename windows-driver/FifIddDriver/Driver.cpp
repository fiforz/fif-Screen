#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>
#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>
#include <new>

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD FifEvtDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY FifEvtDeviceD0Entry;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION FifEvtParseMonitorDescription;
EVT_IDD_CX_ADAPTER_INIT_FINISHED FifEvtAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES FifEvtAdapterCommitModes;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES FifEvtMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES FifEvtMonitorQueryTargetModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN FifEvtMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN FifEvtMonitorUnassignSwapChain;

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kLogDirectory[] = L"C:\\ProgramData\\FifScreen";
constexpr wchar_t kLogPath[] = L"C:\\ProgramData\\FifScreen\\FifIddDriver.log";

void FifLog(const wchar_t* stage, NTSTATUS status = STATUS_SUCCESS);

class SwapChainProcessor {
 public:
  SwapChainProcessor(IDDCX_SWAPCHAIN swapchain,
                     LUID render_adapter_luid,
                     HANDLE available_buffer_event)
      : swapchain_(swapchain),
        render_adapter_luid_(render_adapter_luid),
        available_buffer_event_(available_buffer_event) {
    terminate_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, &SwapChainProcessor::RunThread, this, 0,
                           nullptr);
  }

  ~SwapChainProcessor() {
    if (terminate_event_ != nullptr) {
      SetEvent(terminate_event_);
    }
    if (thread_ != nullptr) {
      WaitForSingleObject(thread_, INFINITE);
      CloseHandle(thread_);
    } else if (swapchain_ != nullptr) {
      WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swapchain_));
      swapchain_ = nullptr;
    }
    if (terminate_event_ != nullptr) {
      CloseHandle(terminate_event_);
    }
  }

 private:
  static DWORD CALLBACK RunThread(LPVOID argument) {
    reinterpret_cast<SwapChainProcessor*>(argument)->Run();
    return 0;
  }

  void Run() {
    DWORD av_task_index = 0;
    HANDLE av_task =
        AvSetMmThreadCharacteristicsW(L"Distribution", &av_task_index);
    RunCore();
    if (swapchain_ != nullptr) {
      WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swapchain_));
      swapchain_ = nullptr;
    }
    if (av_task != nullptr) {
      AvRevertMmThreadCharacteristics(av_task);
    }
  }

  void RunCore() {
    ComPtr<IDXGIFactory5> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
      FifLog(L"FIFIDD_ERROR_DXGI_FACTORY", static_cast<NTSTATUS>(hr));
      return;
    }

    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapterByLuid(render_adapter_luid_,
                                    IID_PPV_ARGS(&adapter));
    if (FAILED(hr)) {
      FifLog(L"FIFIDD_ERROR_DXGI_ADAPTER", static_cast<NTSTATUS>(hr));
      return;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> device_context;
    hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                           D3D11_SDK_VERSION, &device, nullptr,
                           &device_context);
    if (FAILED(hr)) {
      FifLog(L"FIFIDD_ERROR_D3D11_DEVICE", static_cast<NTSTATUS>(hr));
      return;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    hr = device.As(&dxgi_device);
    if (FAILED(hr)) {
      FifLog(L"FIFIDD_ERROR_DXGI_DEVICE", static_cast<NTSTATUS>(hr));
      return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE set_device = {};
    set_device.pDevice = dxgi_device.Get();
    hr = IddCxSwapChainSetDevice(swapchain_, &set_device);
    if (FAILED(hr)) {
      FifLog(L"FIFIDD_ERROR_SWAPCHAIN_SET_DEVICE", static_cast<NTSTATUS>(hr));
      return;
    }
    FifLog(L"FIFIDD_SWAPCHAIN_SET_DEVICE_SUCCESS");

    bool logged_first_frame = false;
    for (;;) {
      ComPtr<IDXGIResource> acquired_buffer;
      IDARG_OUT_RELEASEANDACQUIREBUFFER buffer = {};
      hr = IddCxSwapChainReleaseAndAcquireBuffer(swapchain_, &buffer);
      if (hr == E_PENDING) {
        HANDLE wait_handles[] = {available_buffer_event_, terminate_event_};
        DWORD wait_result =
            WaitForMultipleObjects(ARRAYSIZE(wait_handles), wait_handles, FALSE,
                                   16);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_TIMEOUT) {
          continue;
        }
        break;
      }
      if (FAILED(hr)) {
        FifLog(L"FIFIDD_SWAPCHAIN_ACQUIRE_END", static_cast<NTSTATUS>(hr));
        break;
      }

      acquired_buffer.Attach(buffer.MetaData.pSurface);
      acquired_buffer.Reset();

      hr = IddCxSwapChainFinishedProcessingFrame(swapchain_);
      if (FAILED(hr)) {
        FifLog(L"FIFIDD_ERROR_SWAPCHAIN_FINISH_FRAME",
               static_cast<NTSTATUS>(hr));
        break;
      }
      if (!logged_first_frame) {
        FifLog(L"FIFIDD_SWAPCHAIN_FIRST_FRAME");
        logged_first_frame = true;
      }
    }
  }

  IDDCX_SWAPCHAIN swapchain_ = nullptr;
  LUID render_adapter_luid_ = {};
  HANDLE available_buffer_event_ = nullptr;
  HANDLE terminate_event_ = nullptr;
  HANDLE thread_ = nullptr;
};

SwapChainProcessor* g_swapchain_processor = nullptr;

struct FifDeviceContext {
  IDDCX_ADAPTER adapter;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FifDeviceContext, FifGetDeviceContext);

struct FifMode {
  DWORD width;
  DWORD height;
  DWORD refresh_hz;
};

constexpr FifMode kModes[] = {
    {1920, 1080, 60},
    {1280, 720, 60},
};

constexpr GUID kMonitorContainerId = {
    0x0df5a7b4,
    0x1b62,
    0x4f51,
    {0xa8, 0x2b, 0x10, 0xd8, 0x94, 0xd2, 0x45, 0x2c},
};

void FifLog(const wchar_t* stage, NTSTATUS status) {
  CreateDirectoryW(kLogDirectory, nullptr);

  HANDLE file =
      CreateFileW(kLogPath, FILE_APPEND_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  SYSTEMTIME now = {};
  GetLocalTime(&now);

  wchar_t line[384] = {};
  int chars = swprintf_s(
      line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %ls status=0x%08X\r\n",
      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
      now.wMilliseconds, stage, static_cast<unsigned int>(status));
  if (chars > 0) {
    DWORD bytes_written = 0;
    WriteFile(file, line, static_cast<DWORD>(chars * sizeof(wchar_t)),
              &bytes_written, nullptr);
  }

  CloseHandle(file);
}

void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO* mode,
                    DWORD width,
                    DWORD height,
                    DWORD refresh_hz,
                    bool monitor_mode) {
  mode->totalSize.cx = width;
  mode->totalSize.cy = height;
  mode->activeSize.cx = width;
  mode->activeSize.cy = height;
  mode->AdditionalSignalInfo.vSyncFreqDivider = monitor_mode ? 0 : 1;
  mode->AdditionalSignalInfo.videoStandard = 255;
  mode->vSyncFreq.Numerator = refresh_hz;
  mode->vSyncFreq.Denominator = 1;
  mode->hSyncFreq.Numerator = refresh_hz * height;
  mode->hSyncFreq.Denominator = 1;
  mode->scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
  mode->pixelRate = static_cast<UINT64>(refresh_hz) * width * height;
}

IDDCX_MONITOR_MODE CreateMonitorMode(const FifMode& fif_mode) {
  IDDCX_MONITOR_MODE mode = {};
  mode.Size = sizeof(mode);
  mode.Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
  FillSignalInfo(&mode.MonitorVideoSignalInfo, fif_mode.width, fif_mode.height,
                 fif_mode.refresh_hz, true);
  return mode;
}

IDDCX_TARGET_MODE CreateTargetMode(const FifMode& fif_mode) {
  IDDCX_TARGET_MODE mode = {};
  mode.Size = sizeof(mode);
  FillSignalInfo(&mode.TargetVideoSignalInfo.targetVideoSignalInfo, fif_mode.width,
                 fif_mode.height, fif_mode.refresh_hz, false);
  return mode;
}

NTSTATUS CreateMonitor(IDDCX_ADAPTER adapter) {
  FifLog(L"FIFIDD_MONITOR_CREATE_START");

  IDDCX_MONITOR_INFO monitor_info = {};
  monitor_info.Size = sizeof(monitor_info);
  monitor_info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
  monitor_info.ConnectorIndex = 0;
  monitor_info.MonitorContainerId = kMonitorContainerId;
  monitor_info.MonitorDescription.Size = sizeof(monitor_info.MonitorDescription);
  monitor_info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
  monitor_info.MonitorDescription.DataSize = 0;
  monitor_info.MonitorDescription.pData = nullptr;

  IDARG_IN_MONITORCREATE monitor_create = {};
  monitor_create.pMonitorInfo = &monitor_info;

  IDARG_OUT_MONITORCREATE monitor_create_out = {};
  NTSTATUS status = IddCxMonitorCreate(adapter, &monitor_create, &monitor_create_out);
  if (!NT_SUCCESS(status)) {
    FifLog(L"FIFIDD_ERROR_MONITOR_CREATE", status);
    return status;
  }
  FifLog(L"FIFIDD_MONITOR_CREATE_SUCCESS");

  IDARG_OUT_MONITORARRIVAL arrival_out = {};
  status = IddCxMonitorArrival(monitor_create_out.MonitorObject, &arrival_out);
  FifLog(NT_SUCCESS(status) ? L"FIFIDD_MONITOR_ARRIVAL_SUCCESS"
                            : L"FIFIDD_ERROR_MONITOR_ARRIVAL",
         status);
  return status;
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, UINT reason, LPVOID reserved) {
  UNREFERENCED_PARAMETER(instance);
  UNREFERENCED_PARAMETER(reason);
  UNREFERENCED_PARAMETER(reserved);
  return TRUE;
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object,
                                PUNICODE_STRING registry_path) {
  FifLog(L"FIFIDD_DRIVER_ENTRY");

  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, FifEvtDeviceAdd);

  NTSTATUS status =
      WdfDriverCreate(driver_object, registry_path, &attributes, &config,
                      WDF_NO_HANDLE);
  FifLog(NT_SUCCESS(status) ? L"FIFIDD_DRIVER_CREATE_SUCCESS"
                            : L"FIFIDD_ERROR_DRIVER_CREATE",
         status);
  return status;
}

_Use_decl_annotations_
NTSTATUS FifEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
  UNREFERENCED_PARAMETER(driver);
  FifLog(L"FIFIDD_DEVICE_ADD");

  WDF_PNPPOWER_EVENT_CALLBACKS pnp_power_callbacks;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_power_callbacks);
  pnp_power_callbacks.EvtDeviceD0Entry = FifEvtDeviceD0Entry;
  WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_power_callbacks);

  IDD_CX_CLIENT_CONFIG idd_config;
  IDD_CX_CLIENT_CONFIG_INIT(&idd_config);
  idd_config.EvtIddCxParseMonitorDescription = FifEvtParseMonitorDescription;
  idd_config.EvtIddCxAdapterInitFinished = FifEvtAdapterInitFinished;
  idd_config.EvtIddCxAdapterCommitModes = FifEvtAdapterCommitModes;
  idd_config.EvtIddCxMonitorGetDefaultDescriptionModes = FifEvtMonitorGetDefaultModes;
  idd_config.EvtIddCxMonitorQueryTargetModes = FifEvtMonitorQueryTargetModes;
  idd_config.EvtIddCxMonitorAssignSwapChain = FifEvtMonitorAssignSwapChain;
  idd_config.EvtIddCxMonitorUnassignSwapChain = FifEvtMonitorUnassignSwapChain;

  NTSTATUS status = IddCxDeviceInitConfig(device_init, &idd_config);
  if (!NT_SUCCESS(status)) {
    FifLog(L"FIFIDD_ERROR_IDDCX_DEVICE_INIT_CONFIG", status);
    return status;
  }
  FifLog(L"FIFIDD_IDDCX_DEVICE_INIT_CONFIG_SUCCESS");

  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FifDeviceContext);

  WDFDEVICE device = nullptr;
  status = WdfDeviceCreate(&device_init, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    FifLog(L"FIFIDD_ERROR_WDF_DEVICE_CREATE", status);
    return status;
  }
  FifLog(L"FIFIDD_WDF_DEVICE_CREATE_SUCCESS");

  auto* context = FifGetDeviceContext(device);
  context->adapter = nullptr;

  status = IddCxDeviceInitialize(device);
  FifLog(NT_SUCCESS(status) ? L"FIFIDD_IDDCX_DEVICE_INITIALIZE_SUCCESS"
                            : L"FIFIDD_ERROR_IDDCX_DEVICE_INITIALIZE",
         status);
  return status;
}

_Use_decl_annotations_
NTSTATUS FifEvtParseMonitorDescription(
    const IDARG_IN_PARSEMONITORDESCRIPTION* args,
    IDARG_OUT_PARSEMONITORDESCRIPTION* out_args) {
  UNREFERENCED_PARAMETER(args);
  FifLog(L"FIFIDD_PARSE_MONITOR_DESCRIPTION");

  out_args->MonitorModeBufferOutputCount = ARRAYSIZE(kModes);
  if (args->MonitorModeBufferInputCount == 0) {
    out_args->PreferredMonitorModeIdx = 0;
    return STATUS_SUCCESS;
  }
  if (args->MonitorModeBufferInputCount < ARRAYSIZE(kModes)) {
    out_args->PreferredMonitorModeIdx = NO_PREFERRED_MODE;
    return STATUS_BUFFER_TOO_SMALL;
  }

  for (UINT i = 0; i < ARRAYSIZE(kModes); ++i) {
    args->pMonitorModes[i] = CreateMonitorMode(kModes[i]);
  }
  out_args->PreferredMonitorModeIdx = 0;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtDeviceD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE previous_state) {
  UNREFERENCED_PARAMETER(previous_state);
  FifLog(L"FIFIDD_ADAPTER_INIT_START");

  auto* context = FifGetDeviceContext(device);
  if (context->adapter != nullptr) {
    return STATUS_SUCCESS;
  }

  IDDCX_ADAPTER_CAPS adapter_caps = {};
  adapter_caps.Size = sizeof(adapter_caps);
  adapter_caps.MaxMonitorsSupported = 1;
  adapter_caps.EndPointDiagnostics.Size = sizeof(adapter_caps.EndPointDiagnostics);
  adapter_caps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
  adapter_caps.EndPointDiagnostics.TransmissionType =
      IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
  adapter_caps.EndPointDiagnostics.pEndPointFriendlyName = L"FifScreen";
  adapter_caps.EndPointDiagnostics.pEndPointManufacturerName = L"FifScreen";
  adapter_caps.EndPointDiagnostics.pEndPointModelName = L"FifScreen Idd";

  IDDCX_ENDPOINT_VERSION endpoint_version = {};
  endpoint_version.Size = sizeof(endpoint_version);
  endpoint_version.MajorVer = 1;
  adapter_caps.EndPointDiagnostics.pFirmwareVersion = &endpoint_version;
  adapter_caps.EndPointDiagnostics.pHardwareVersion = &endpoint_version;

  IDARG_IN_ADAPTER_INIT adapter_init = {};
  adapter_init.WdfDevice = device;
  adapter_init.pCaps = &adapter_caps;

  IDARG_OUT_ADAPTER_INIT adapter_init_out = {};
  NTSTATUS status = IddCxAdapterInitAsync(&adapter_init, &adapter_init_out);
  if (NT_SUCCESS(status)) {
    context->adapter = adapter_init_out.AdapterObject;
  }
  FifLog(NT_SUCCESS(status) ? L"FIFIDD_ADAPTER_INIT_ASYNC_SUCCESS"
                            : L"FIFIDD_ERROR_ADAPTER_INIT_ASYNC",
         status);

  return status;
}

_Use_decl_annotations_
NTSTATUS FifEvtAdapterInitFinished(IDDCX_ADAPTER adapter,
                                   const IDARG_IN_ADAPTER_INIT_FINISHED* args) {
  FifLog(L"FIFIDD_ADAPTER_INIT_FINISHED", args->AdapterInitStatus);
  if (!NT_SUCCESS(args->AdapterInitStatus)) {
    return STATUS_SUCCESS;
  }

  return CreateMonitor(adapter);
}

_Use_decl_annotations_
NTSTATUS FifEvtAdapterCommitModes(IDDCX_ADAPTER adapter,
                                  const IDARG_IN_COMMITMODES* args) {
  UNREFERENCED_PARAMETER(adapter);
  UNREFERENCED_PARAMETER(args);
  FifLog(L"FIFIDD_ADAPTER_COMMIT_MODES");
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorGetDefaultModes(
    IDDCX_MONITOR monitor,
    const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* args,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* out_args) {
  UNREFERENCED_PARAMETER(monitor);
  FifLog(L"FIFIDD_MONITOR_GET_DEFAULT_MODES");

  out_args->DefaultMonitorModeBufferOutputCount = ARRAYSIZE(kModes);
  if (args->DefaultMonitorModeBufferInputCount == 0) {
    return STATUS_SUCCESS;
  }
  if (args->DefaultMonitorModeBufferInputCount < ARRAYSIZE(kModes)) {
    return STATUS_BUFFER_TOO_SMALL;
  }

  for (UINT i = 0; i < ARRAYSIZE(kModes); ++i) {
    args->pDefaultMonitorModes[i] = CreateMonitorMode(kModes[i]);
  }
  out_args->PreferredMonitorModeIdx = 0;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorQueryTargetModes(IDDCX_MONITOR monitor,
                                       const IDARG_IN_QUERYTARGETMODES* args,
                                       IDARG_OUT_QUERYTARGETMODES* out_args) {
  UNREFERENCED_PARAMETER(monitor);
  FifLog(L"FIFIDD_MONITOR_QUERY_TARGET_MODES");

  out_args->TargetModeBufferOutputCount = ARRAYSIZE(kModes);
  if (args->TargetModeBufferInputCount == 0) {
    return STATUS_SUCCESS;
  }
  if (args->TargetModeBufferInputCount < ARRAYSIZE(kModes)) {
    return STATUS_BUFFER_TOO_SMALL;
  }

  for (UINT i = 0; i < ARRAYSIZE(kModes); ++i) {
    args->pTargetModes[i] = CreateTargetMode(kModes[i]);
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorAssignSwapChain(IDDCX_MONITOR monitor,
                                      const IDARG_IN_SETSWAPCHAIN* args) {
  UNREFERENCED_PARAMETER(monitor);
  FifLog(L"FIFIDD_SWAPCHAIN_ASSIGNED");

  delete g_swapchain_processor;
  g_swapchain_processor =
      new (std::nothrow) SwapChainProcessor(args->hSwapChain,
                                            args->RenderAdapterLuid,
                                            args->hNextSurfaceAvailable);
  if (g_swapchain_processor == nullptr) {
    WdfObjectDelete(reinterpret_cast<WDFOBJECT>(args->hSwapChain));
    FifLog(L"FIFIDD_ERROR_SWAPCHAIN_PROCESSOR_ALLOC", STATUS_NO_MEMORY);
    return STATUS_NO_MEMORY;
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorUnassignSwapChain(IDDCX_MONITOR monitor) {
  UNREFERENCED_PARAMETER(monitor);
  FifLog(L"FIFIDD_SWAPCHAIN_UNASSIGNED");
  delete g_swapchain_processor;
  g_swapchain_processor = nullptr;
  return STATUS_SUCCESS;
}
