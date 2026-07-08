#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD FifEvtDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY FifEvtDeviceD0Entry;
EVT_IDD_CX_ADAPTER_INIT_FINISHED FifEvtAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES FifEvtAdapterCommitModes;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES FifEvtMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES FifEvtMonitorQueryTargetModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN FifEvtMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN FifEvtMonitorUnassignSwapChain;

namespace {

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
    return status;
  }

  IDARG_OUT_MONITORARRIVAL arrival_out = {};
  return IddCxMonitorArrival(monitor_create_out.MonitorObject, &arrival_out);
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
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, FifEvtDeviceAdd);

  return WdfDriverCreate(driver_object, registry_path, &attributes, &config,
                         WDF_NO_HANDLE);
}

_Use_decl_annotations_
NTSTATUS FifEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
  UNREFERENCED_PARAMETER(driver);

  WDF_PNPPOWER_EVENT_CALLBACKS pnp_power_callbacks;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_power_callbacks);
  pnp_power_callbacks.EvtDeviceD0Entry = FifEvtDeviceD0Entry;
  WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_power_callbacks);

  IDD_CX_CLIENT_CONFIG idd_config;
  IDD_CX_CLIENT_CONFIG_INIT(&idd_config);
  idd_config.EvtIddCxAdapterInitFinished = FifEvtAdapterInitFinished;
  idd_config.EvtIddCxAdapterCommitModes = FifEvtAdapterCommitModes;
  idd_config.EvtIddCxMonitorGetDefaultDescriptionModes = FifEvtMonitorGetDefaultModes;
  idd_config.EvtIddCxMonitorQueryTargetModes = FifEvtMonitorQueryTargetModes;
  idd_config.EvtIddCxMonitorAssignSwapChain = FifEvtMonitorAssignSwapChain;
  idd_config.EvtIddCxMonitorUnassignSwapChain = FifEvtMonitorUnassignSwapChain;

  NTSTATUS status = IddCxDeviceInitConfig(device_init, &idd_config);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FifDeviceContext);

  WDFDEVICE device = nullptr;
  status = WdfDeviceCreate(&device_init, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  auto* context = FifGetDeviceContext(device);
  context->adapter = nullptr;

  return IddCxDeviceInitialize(device);
}

_Use_decl_annotations_
NTSTATUS FifEvtDeviceD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE previous_state) {
  UNREFERENCED_PARAMETER(previous_state);

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

  return status;
}

_Use_decl_annotations_
NTSTATUS FifEvtAdapterInitFinished(IDDCX_ADAPTER adapter,
                                   const IDARG_IN_ADAPTER_INIT_FINISHED* args) {
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
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorGetDefaultModes(
    IDDCX_MONITOR monitor,
    const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* args,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* out_args) {
  UNREFERENCED_PARAMETER(monitor);

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

  // Frame transport is intentionally out of scope for this build-hardening stage.
  WdfObjectDelete(args->hSwapChain);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS FifEvtMonitorUnassignSwapChain(IDDCX_MONITOR monitor) {
  UNREFERENCED_PARAMETER(monitor);
  return STATUS_SUCCESS;
}
