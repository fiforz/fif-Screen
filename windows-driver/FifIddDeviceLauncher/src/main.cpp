#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <swdevice.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kEnumeratorName[] = L"FifScreenIdd";
constexpr wchar_t kParentDevice[] = L"HTREE\\ROOT\\0";
constexpr wchar_t kInstanceId[] = L"FifIddDriver";
constexpr wchar_t kHardwareId[] = L"FifIddDriver";
constexpr wchar_t kHardwareIdsMultiSz[] = L"FifIddDriver\0\0";
constexpr wchar_t kCompatibleIdsMultiSz[] = L"FifIddDriver\0\0";
constexpr wchar_t kDescription[] = L"FifScreen Indirect Display";
constexpr wchar_t kOwnerMutexName[] = L"Local\\FifScreenIddOwnerMutex";
constexpr wchar_t kStopEventName[] = L"Local\\FifScreenIddStopEvent";
constexpr wchar_t kOwnerStateMapName[] = L"Local\\FifScreenIddOwnerState";
constexpr DWORD kCreateCallbackTimeoutMs = 10 * 1000;
constexpr DWORD kStopTimeoutMs = 15 * 1000;
constexpr DWORD kPollIntervalMs = 500;
constexpr size_t kSharedInstanceIdChars = 512;

HANDLE g_stop_event = nullptr;

struct CreateContext {
  HANDLE event = nullptr;
  HRESULT result = E_PENDING;
  std::wstring instance_id;
};

struct SharedOwnerState {
  DWORD process_id = 0;
  wchar_t instance_id[kSharedInstanceIdChars]{};
};

struct OwnerProbe {
  bool object_exists = false;
  bool owner_running = false;
  DWORD wait_result = WAIT_FAILED;
};

class ScopedHandle {
 public:
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
  ~ScopedHandle() { reset(); }

  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  HANDLE get() const { return handle_; }
  HANDLE* put() {
    reset();
    return &handle_;
  }
  explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }

  HANDLE release() {
    HANDLE handle = handle_;
    handle_ = nullptr;
    return handle;
  }

  void reset(HANDLE handle = nullptr) {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
    handle_ = handle;
  }

 private:
  HANDLE handle_ = nullptr;
};

std::wstring to_lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return value;
}

bool starts_with_case_insensitive(const std::wstring& value, const std::wstring& prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  return to_lower(value.substr(0, prefix.size())) == to_lower(prefix);
}

bool contains_case_insensitive(const std::wstring& value, const std::wstring& needle) {
  return to_lower(value).find(to_lower(needle)) != std::wstring::npos;
}

std::wstring format_win32_error(DWORD error) {
  wchar_t* message = nullptr;
  const DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error,
      0,
      reinterpret_cast<wchar_t*>(&message),
      0,
      nullptr);

  std::wstring result = size ? std::wstring(message, size) : L"<no message>";
  if (message) {
    LocalFree(message);
  }
  while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
    result.pop_back();
  }
  return result;
}

std::vector<std::wstring> parse_multi_sz(const std::vector<BYTE>& bytes) {
  std::vector<std::wstring> values;
  if (bytes.empty()) {
    return values;
  }

  const auto* begin = reinterpret_cast<const wchar_t*>(bytes.data());
  const auto* end = reinterpret_cast<const wchar_t*>(bytes.data() + bytes.size());
  const wchar_t* current = begin;
  while (current < end && *current != L'\0') {
    std::wstring value(current);
    values.push_back(value);
    current += value.size() + 1;
  }
  return values;
}

std::vector<BYTE> get_registry_property(HDEVINFO info_set,
                                        SP_DEVINFO_DATA& dev_info,
                                        DWORD property) {
  DWORD required_size = 0;
  DWORD type = 0;
  SetupDiGetDeviceRegistryPropertyW(
      info_set, &dev_info, property, &type, nullptr, 0, &required_size);

  if (required_size == 0) {
    return {};
  }

  std::vector<BYTE> buffer(required_size);
  if (!SetupDiGetDeviceRegistryPropertyW(
          info_set, &dev_info, property, &type, buffer.data(),
          static_cast<DWORD>(buffer.size()), &required_size)) {
    return {};
  }
  return buffer;
}

std::wstring get_string_property(HDEVINFO info_set,
                                 SP_DEVINFO_DATA& dev_info,
                                 DWORD property) {
  const auto bytes = get_registry_property(info_set, dev_info, property);
  if (bytes.empty()) {
    return L"";
  }
  return reinterpret_cast<const wchar_t*>(bytes.data());
}

struct DeviceRecord {
  SP_DEVINFO_DATA data{};
  std::wstring instance_id;
  std::wstring description;
  std::wstring friendly_name;
  std::wstring service;
  std::wstring driver_key;
  std::vector<std::wstring> hardware_ids;
  ULONG devnode_status = 0;
  ULONG problem_code = 0;
  bool devnode_status_available = false;
};

struct DisplayRecord {
  std::wstring device_name;
  std::wstring device_string;
  std::wstring device_id;
  DWORD state_flags = 0;
  DWORD monitor_count = 0;
};

struct TopologyRecord {
  std::wstring source_name;
  std::wstring source_adapter_path;
  std::wstring target_adapter_path;
  std::wstring target_monitor_path;
  bool active = false;
};

bool is_fifscreen_adapter_path(const std::wstring& path) {
  return contains_case_insensitive(path, L"FifScreenIdd") ||
         contains_case_insensitive(path, L"FifIddDriver");
}

std::wstring get_adapter_path(const LUID& adapter_id) {
  DISPLAYCONFIG_ADAPTER_NAME adapter{};
  adapter.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
  adapter.header.size = sizeof(adapter);
  adapter.header.adapterId = adapter_id;
  if (DisplayConfigGetDeviceInfo(&adapter.header) != ERROR_SUCCESS) {
    return L"";
  }
  return adapter.adapterDevicePath;
}

std::vector<TopologyRecord> find_fifscreen_topology() {
  for (int attempt = 0; attempt < 3; ++attempt) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    LONG result = GetDisplayConfigBufferSizes(
        QDC_ALL_PATHS, &path_count, &mode_count);
    if (result != ERROR_SUCCESS) {
      return {};
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    result = QueryDisplayConfig(QDC_ALL_PATHS, &path_count, paths.data(),
                                &mode_count, modes.data(), nullptr);
    if (result == ERROR_INSUFFICIENT_BUFFER) {
      continue;
    }
    if (result != ERROR_SUCCESS) {
      return {};
    }

    std::vector<TopologyRecord> records;
    for (UINT32 index = 0; index < path_count; ++index) {
      const auto& path = paths[index];
      const std::wstring target_adapter_path = get_adapter_path(path.targetInfo.adapterId);
      if (!is_fifscreen_adapter_path(target_adapter_path)) {
        continue;
      }

      DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
      source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source.header.size = sizeof(source);
      source.header.adapterId = path.sourceInfo.adapterId;
      source.header.id = path.sourceInfo.id;
      (void)DisplayConfigGetDeviceInfo(&source.header);

      DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
      target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target.header.size = sizeof(target);
      target.header.adapterId = path.targetInfo.adapterId;
      target.header.id = path.targetInfo.id;
      (void)DisplayConfigGetDeviceInfo(&target.header);

      records.push_back(TopologyRecord{
          source.viewGdiDeviceName,
          get_adapter_path(path.sourceInfo.adapterId),
          target_adapter_path,
          target.monitorDevicePath,
          (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0,
      });
    }
    return records;
  }
  return {};
}

bool topology_is_extended(const TopologyRecord& topology) {
  return topology.active && !topology.source_name.empty() &&
         is_fifscreen_adapter_path(topology.source_adapter_path);
}

bool is_fifscreen_display(const DISPLAY_DEVICEW& device) {
  return contains_case_insensitive(device.DeviceString, L"FifScreen") ||
         contains_case_insensitive(device.DeviceID, L"FIFSCREEN") ||
         contains_case_insensitive(device.DeviceName, L"FifScreen");
}

std::vector<DisplayRecord> find_fifscreen_displays() {
  std::vector<DisplayRecord> displays;
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) {
      break;
    }
    if (!is_fifscreen_display(device)) {
      continue;
    }

    DisplayRecord record;
    record.device_name = device.DeviceName;
    record.device_string = device.DeviceString;
    record.device_id = device.DeviceID;
    record.state_flags = device.StateFlags;

    for (DWORD monitor_index = 0;; ++monitor_index) {
      DISPLAY_DEVICEW monitor{};
      monitor.cb = sizeof(monitor);
      if (!EnumDisplayDevicesW(device.DeviceName, monitor_index, &monitor, 0)) {
        break;
      }
      ++record.monitor_count;
    }
    displays.push_back(std::move(record));
  }
  return displays;
}

bool device_is_healthy(const DeviceRecord& device) {
  return device.devnode_status_available && device.problem_code == 0 &&
         (device.devnode_status & DN_STARTED) != 0 && !device.driver_key.empty();
}

bool is_fifscreen_device(const DeviceRecord& record) {
  if (starts_with_case_insensitive(record.instance_id, L"SWD\\FifScreenIdd\\")) {
    return true;
  }

  for (const auto& hardware_id : record.hardware_ids) {
    if (_wcsicmp(hardware_id.c_str(), kHardwareId) == 0) {
      return true;
    }
  }
  return false;
}

std::vector<DeviceRecord> find_devices(bool present_only) {
  const DWORD flags = DIGCF_ALLCLASSES | (present_only ? DIGCF_PRESENT : 0);
  HDEVINFO info_set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, flags);
  if (info_set == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("SetupDiGetClassDevsW failed");
  }

  std::vector<DeviceRecord> devices;
  for (DWORD index = 0;; ++index) {
    SP_DEVINFO_DATA dev_info{};
    dev_info.cbSize = sizeof(dev_info);
    if (!SetupDiEnumDeviceInfo(info_set, index, &dev_info)) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS) {
        break;
      }
      continue;
    }

    wchar_t instance_id[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(dev_info.DevInst, instance_id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
      continue;
    }

    DeviceRecord record;
    record.data = dev_info;
    record.instance_id = instance_id;
    record.description = get_string_property(info_set, dev_info, SPDRP_DEVICEDESC);
    record.friendly_name = get_string_property(info_set, dev_info, SPDRP_FRIENDLYNAME);
    record.service = get_string_property(info_set, dev_info, SPDRP_SERVICE);
    record.driver_key = get_string_property(info_set, dev_info, SPDRP_DRIVER);
    record.hardware_ids = parse_multi_sz(get_registry_property(info_set, dev_info, SPDRP_HARDWAREID));
    record.devnode_status_available =
        CM_Get_DevNode_Status(&record.devnode_status, &record.problem_code, dev_info.DevInst, 0) ==
        CR_SUCCESS;

    if (is_fifscreen_device(record)) {
      devices.push_back(record);
    }
  }

  SetupDiDestroyDeviceInfoList(info_set);
  return devices;
}

OwnerProbe probe_owner() {
  OwnerProbe probe;
  ScopedHandle mutex(OpenMutexW(SYNCHRONIZE, FALSE, kOwnerMutexName));
  if (!mutex) {
    return probe;
  }

  probe.object_exists = true;
  probe.wait_result = WaitForSingleObject(mutex.get(), 0);
  if (probe.wait_result == WAIT_TIMEOUT) {
    probe.owner_running = true;
  } else if (probe.wait_result == WAIT_OBJECT_0 || probe.wait_result == WAIT_ABANDONED) {
    ReleaseMutex(mutex.get());
  }
  return probe;
}

SharedOwnerState read_owner_state() {
  SharedOwnerState state;
  ScopedHandle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, kOwnerStateMapName));
  if (!mapping) {
    return state;
  }

  const auto* view = static_cast<const SharedOwnerState*>(
      MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, sizeof(SharedOwnerState)));
  if (!view) {
    return state;
  }

  state = *view;
  UnmapViewOfFile(view);
  return state;
}

void write_owner_state(HANDLE mapping, const std::wstring& instance_id) {
  auto* view = static_cast<SharedOwnerState*>(
      MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedOwnerState)));
  if (!view) {
    throw std::runtime_error("MapViewOfFile failed for owner state");
  }

  ZeroMemory(view, sizeof(SharedOwnerState));
  view->process_id = GetCurrentProcessId();
  wcsncpy_s(view->instance_id, kSharedInstanceIdChars, instance_id.c_str(), _TRUNCATE);
  UnmapViewOfFile(view);
}

bool wait_for_no_fifscreen_devices(DWORD timeout_ms) {
  const DWORD start = GetTickCount();
  for (;;) {
    if (find_devices(true).empty()) {
      return true;
    }

    if (GetTickCount() - start >= timeout_ms) {
      return false;
    }
    Sleep(kPollIntervalMs);
  }
}

bool wait_for_owner_stopped_and_device_absent(DWORD timeout_ms) {
  const DWORD start = GetTickCount();
  for (;;) {
    const OwnerProbe owner = probe_owner();
    const bool device_present = !find_devices(true).empty();
    if (!owner.owner_running && !device_present) {
      return true;
    }

    if (GetTickCount() - start >= timeout_ms) {
      return false;
    }
    Sleep(kPollIntervalMs);
  }
}

enum class CreateDecision {
  AllowCreate,
  AlreadyRunning,
  InconsistentOwnerState,
  OrphanOrTransitionalDeviceState,
};

CreateDecision decide_create(bool owner_running, bool device_present) {
  if (owner_running && device_present) {
    return CreateDecision::AlreadyRunning;
  }
  if (owner_running && !device_present) {
    return CreateDecision::InconsistentOwnerState;
  }
  if (!owner_running && device_present) {
    return CreateDecision::OrphanOrTransitionalDeviceState;
  }
  return CreateDecision::AllowCreate;
}

const wchar_t* decision_name(CreateDecision decision) {
  switch (decision) {
    case CreateDecision::AllowCreate:
      return L"ALLOW_CREATE";
    case CreateDecision::AlreadyRunning:
      return L"ALREADY_RUNNING";
    case CreateDecision::InconsistentOwnerState:
      return L"INCONSISTENT_OWNER_STATE";
    case CreateDecision::OrphanOrTransitionalDeviceState:
      return L"ORPHAN_OR_TRANSITIONAL_DEVICE_STATE";
  }
  return L"UNKNOWN";
}

void WINAPI creation_callback(HSWDEVICE,
                              HRESULT result,
                              PVOID context,
                              PCWSTR device_instance_id) {
  auto* create_context = static_cast<CreateContext*>(context);
  create_context->result = result;
  if (device_instance_id) {
    create_context->instance_id = device_instance_id;
  }
  SetEvent(create_context->event);
}

BOOL WINAPI console_ctrl_handler(DWORD control_type) {
  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      if (g_stop_event) {
        SetEvent(g_stop_event);
      }
      return TRUE;
    default:
      return FALSE;
  }
}

int command_create() {
  const OwnerProbe existing_owner = probe_owner();
  const bool existing_device = !find_devices(true).empty();
  CreateDecision decision = decide_create(existing_owner.owner_running, existing_device);

  if (decision == CreateDecision::OrphanOrTransitionalDeviceState) {
    std::wcout << L"precreate_state=" << decision_name(decision) << L"\n";
    std::wcout << L"waiting_for_existing_device_teardown_ms=" << kStopTimeoutMs << L"\n";
    if (wait_for_no_fifscreen_devices(kStopTimeoutMs)) {
      decision = CreateDecision::AllowCreate;
    }
  }

  if (decision != CreateDecision::AllowCreate) {
    std::wcerr << L"create_refused=" << decision_name(decision) << L"\n";
    return 3;
  }

  ScopedHandle owner_mutex(CreateMutexW(nullptr, TRUE, kOwnerMutexName));
  if (!owner_mutex) {
    std::wcerr << L"CreateMutexW failed: " << format_win32_error(GetLastError()) << L"\n";
    return 1;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    std::wcerr << L"create_refused=OWNER_ALREADY_EXISTS\n";
    return 3;
  }

  ScopedHandle stop_event(CreateEventW(nullptr, TRUE, FALSE, kStopEventName));
  if (!stop_event) {
    std::wcerr << L"CreateEventW stop event failed: " << format_win32_error(GetLastError()) << L"\n";
    return 1;
  }
  ResetEvent(stop_event.get());
  g_stop_event = stop_event.get();

  CreateContext context;
  context.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!context.event) {
    std::wcerr << L"CreateEventW failed: " << format_win32_error(GetLastError()) << L"\n";
    return 1;
  }

  ScopedHandle callback_event(context.event);
  SW_DEVICE_CREATE_INFO create_info{};
  create_info.cbSize = sizeof(create_info);
  create_info.pszInstanceId = kInstanceId;
  create_info.pszzHardwareIds = kHardwareIdsMultiSz;
  create_info.pszzCompatibleIds = kCompatibleIdsMultiSz;
  create_info.pszDeviceDescription = kDescription;
  create_info.CapabilityFlags = SWDeviceCapabilitiesRemovable |
                                SWDeviceCapabilitiesSilentInstall |
                                SWDeviceCapabilitiesDriverRequired;

  HSWDEVICE sw_device = nullptr;
  const HRESULT hr = SwDeviceCreate(
      kEnumeratorName,
      kParentDevice,
      &create_info,
      0,
      nullptr,
      creation_callback,
      &context,
      &sw_device);

  if (FAILED(hr)) {
    std::wcerr << L"SwDeviceCreate failed: 0x" << std::hex << hr << L"\n";
    return 1;
  }

  const DWORD wait_result = WaitForSingleObject(context.event, kCreateCallbackTimeoutMs);
  if (wait_result != WAIT_OBJECT_0 || FAILED(context.result)) {
    std::wcerr << L"Software device creation did not complete. wait=0x"
               << std::hex << wait_result << L" result=0x" << context.result << L"\n";
    if (sw_device) {
      SwDeviceClose(sw_device);
      wait_for_no_fifscreen_devices(kStopTimeoutMs);
    }
    return 1;
  }

  const HRESULT lifetime_hr = SwDeviceSetLifetime(sw_device, SWDeviceLifetimeHandle);
  if (FAILED(lifetime_hr)) {
    std::wcerr << L"SwDeviceSetLifetime(SWDeviceLifetimeHandle) failed: 0x" << std::hex
               << lifetime_hr << L"\n";
    SwDeviceClose(sw_device);
    wait_for_no_fifscreen_devices(kStopTimeoutMs);
    return 1;
  }

  ScopedHandle owner_state(CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedOwnerState), kOwnerStateMapName));
  if (!owner_state) {
    std::wcerr << L"CreateFileMappingW owner state failed: " << format_win32_error(GetLastError())
               << L"\n";
    SwDeviceClose(sw_device);
    wait_for_no_fifscreen_devices(kStopTimeoutMs);
    return 1;
  }
  write_owner_state(owner_state.get(), context.instance_id);

  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

  std::wcout << L"created_instance=" << context.instance_id << L"\n";
  std::wcout << L"hardware_id=" << kHardwareId << L"\n";
  std::wcout << L"lifetime_mode=HANDLE\n";
  std::wcout << L"owner_pid=" << GetCurrentProcessId() << L"\n";
  std::wcout << L"owner_running=true\n";
  std::wcout << L"stop_event=" << kStopEventName << L"\n";

  WaitForSingleObject(stop_event.get(), INFINITE);
  std::wcout << L"stop_requested=true\n";

  SwDeviceClose(sw_device);
  sw_device = nullptr;
  const bool disappeared = wait_for_no_fifscreen_devices(kStopTimeoutMs);
  SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
  g_stop_event = nullptr;
  std::wcout << L"removed_by_close=true\n";
  std::wcout << L"device_disappeared=" << (disappeared ? L"true" : L"false") << L"\n";
  ReleaseMutex(owner_mutex.get());
  return disappeared ? 0 : 1;
}

int command_status() {
  const auto devices = find_devices(true);
  const auto recorded_devices = find_devices(false);
  const auto displays = find_fifscreen_displays();
  const auto topology = find_fifscreen_topology();
  const OwnerProbe owner = probe_owner();
  const SharedOwnerState owner_state = read_owner_state();
  const bool all_devices_healthy = !devices.empty() &&
      std::all_of(devices.begin(), devices.end(), device_is_healthy);
  const bool monitor_present = !topology.empty();
  const bool display_active = std::any_of(
      topology.begin(), topology.end(), topology_is_extended);
  std::wcout << L"owner_running=" << (owner.owner_running ? L"true" : L"false") << L"\n";
  if (owner_state.process_id != 0) {
    std::wcout << L"owner_pid=" << owner_state.process_id << L"\n";
  } else {
    std::wcout << L"owner_pid=NOT_AVAILABLE\n";
  }

  std::wcout << L"fifscreen_software_device_present="
             << (devices.empty() ? L"false" : L"true") << L"\n";
  std::wcout << L"fifscreen_software_device_recorded="
             << (recorded_devices.empty() ? L"false" : L"true") << L"\n";
  std::wcout << L"fifscreen_software_device_healthy="
             << (all_devices_healthy ? L"true" : L"false") << L"\n";
  std::wcout << L"fifscreen_display_adapter_present="
             << (displays.empty() ? L"false" : L"true") << L"\n";
  std::wcout << L"fifscreen_monitor_present="
             << (monitor_present ? L"true" : L"false") << L"\n";
  std::wcout << L"fifscreen_display_active="
             << (display_active ? L"true" : L"false") << L"\n";

  for (const auto& path : topology) {
    std::wcout << L"topology_active=" << (path.active ? L"true" : L"false") << L"\n";
    std::wcout << L"topology_extended="
               << (topology_is_extended(path) ? L"true" : L"false") << L"\n";
    std::wcout << L"topology_source_name=" << path.source_name << L"\n";
    std::wcout << L"topology_source_adapter=" << path.source_adapter_path << L"\n";
    std::wcout << L"topology_target_adapter=" << path.target_adapter_path << L"\n";
    std::wcout << L"topology_target_monitor=" << path.target_monitor_path << L"\n";
  }

  for (const auto& display : displays) {
    std::wcout << L"display_name=" << display.device_name << L"\n";
    std::wcout << L"display_string=" << display.device_string << L"\n";
    std::wcout << L"display_id=" << display.device_id << L"\n";
    std::wcout << L"display_state_flags=0x" << std::hex << display.state_flags << L"\n";
    std::wcout << L"display_monitor_count=" << std::dec << display.monitor_count << L"\n";
  }

  if (devices.empty()) {
    if (owner_state.instance_id[0] != L'\0') {
      std::wcout << L"actual_instance_id=" << owner_state.instance_id << L"\n";
    } else {
      std::wcout << L"actual_instance_id=NOT_CREATED\n";
    }
    for (const auto& device : recorded_devices) {
      std::wcout << L"recorded_instance_id=" << device.instance_id << L"\n";
    }
    return 0;
  }

  for (const auto& device : devices) {
    std::wcout << L"instance_id=" << device.instance_id << L"\n";
    std::wcout << L"actual_instance_id=" << device.instance_id << L"\n";
    if (!device.friendly_name.empty()) {
      std::wcout << L"friendly_name=" << device.friendly_name << L"\n";
    }
    if (!device.description.empty()) {
      std::wcout << L"description=" << device.description << L"\n";
    }
    for (const auto& hardware_id : device.hardware_ids) {
      std::wcout << L"hardware_id=" << hardware_id << L"\n";
    }
    if (!device.service.empty()) {
      std::wcout << L"service=" << device.service << L"\n";
    }
    if (!device.driver_key.empty()) {
      std::wcout << L"driver_key=" << device.driver_key << L"\n";
    }
    if (device.devnode_status_available) {
      std::wcout << L"devnode_status=0x" << std::hex << device.devnode_status << L"\n";
      std::wcout << L"problem_code=" << std::dec << device.problem_code << L"\n";
    } else {
      std::wcout << L"devnode_status=NOT_AVAILABLE\n";
      std::wcout << L"problem_code=NOT_AVAILABLE\n";
    }
  }
  return 0;
}

int command_extend() {
  const auto current_topology = find_fifscreen_topology();
  if (std::any_of(current_topology.begin(), current_topology.end(),
                  topology_is_extended)) {
    std::wcout << L"set_display_config_result=ALREADY_EXTENDED\n";
    std::wcout << L"fifscreen_display_extended=true\n";
    return 0;
  }

  const LONG result = SetDisplayConfig(
      0, nullptr, 0, nullptr, SDC_APPLY | SDC_TOPOLOGY_EXTEND);
  std::wcout << L"set_display_config_result=" << result << L"\n";
  if (result != ERROR_SUCCESS) {
    std::wcerr << L"extend_failed=" << format_win32_error(result) << L"\n";
    return 1;
  }

  const DWORD started = GetTickCount();
  while (GetTickCount() - started < kCreateCallbackTimeoutMs) {
    const auto topology = find_fifscreen_topology();
    if (std::any_of(topology.begin(), topology.end(), topology_is_extended)) {
      std::wcout << L"fifscreen_display_extended=true\n";
      return 0;
    }
    Sleep(250);
  }
  std::wcerr << L"fifscreen_display_extended=false\n";
  return 1;
}

int command_remove() {
  const OwnerProbe owner = probe_owner();
  const bool device_present = !find_devices(true).empty();

  if (!owner.owner_running) {
    if (device_present) {
      std::wcerr << L"remove_refused=ORPHAN_OR_TRANSITIONAL_DEVICE_STATE\n";
      return 3;
    }
    std::wcout << L"owner_running=false\n";
    std::wcout << L"fifscreen_software_device_present=false\n";
    std::wcout << L"removed_count=0\n";
    return 0;
  }

  ScopedHandle stop_event(OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName));
  if (!stop_event) {
    std::wcerr << L"remove_failed=STOP_EVENT_NOT_FOUND error=" << format_win32_error(GetLastError())
               << L"\n";
    return 1;
  }

  if (!SetEvent(stop_event.get())) {
    std::wcerr << L"remove_failed=SET_STOP_EVENT_FAILED error=" << format_win32_error(GetLastError())
               << L"\n";
    return 1;
  }

  std::wcout << L"stop_signal_sent=true\n";
  const bool stopped = wait_for_owner_stopped_and_device_absent(kStopTimeoutMs);
  std::wcout << L"owner_stopped_and_device_absent=" << (stopped ? L"true" : L"false") << L"\n";
  return stopped ? 0 : 1;
}

int command_selftest() {
  struct Case {
    bool owner;
    bool device;
    CreateDecision expected;
  };

  const std::array<Case, 4> cases{{
      {false, false, CreateDecision::AllowCreate},
      {true, true, CreateDecision::AlreadyRunning},
      {true, false, CreateDecision::InconsistentOwnerState},
      {false, true, CreateDecision::OrphanOrTransitionalDeviceState},
  }};

  bool pass = true;
  for (const auto& test_case : cases) {
    const CreateDecision actual = decide_create(test_case.owner, test_case.device);
    const bool ok = actual == test_case.expected;
    pass = pass && ok;
    std::wcout << L"selftest owner=" << (test_case.owner ? L"true" : L"false")
               << L" device=" << (test_case.device ? L"true" : L"false")
               << L" decision=" << decision_name(actual)
               << L" expected=" << decision_name(test_case.expected)
               << L" result=" << (ok ? L"PASS" : L"FAIL") << L"\n";
  }
  return pass ? 0 : 1;
}

void print_usage() {
  std::wcout << L"fif-idd-device-launcher <create|status|extend|remove|selftest>\n";
  std::wcout << L"  create   Create FifScreen software device and hold it as the owner process.\n";
  std::wcout << L"  status  Enumerate exact FifScreen software device records only.\n";
  std::wcout << L"  extend  Apply the Windows extended-display topology and verify FifScreen.\n";
  std::wcout << L"  remove  Signal the current FifScreen owner process to close its device handle.\n";
  std::wcout << L"  selftest  Run no-device duplicate-state logic tests.\n";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  if (argc != 2) {
    print_usage();
    return 2;
  }

  const std::wstring command = argv[1];
  try {
    if (command == L"create") {
      return command_create();
    }
    if (command == L"status") {
      return command_status();
    }
    if (command == L"extend") {
      return command_extend();
    }
    if (command == L"remove") {
      return command_remove();
    }
    if (command == L"selftest") {
      return command_selftest();
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }

  print_usage();
  return 2;
}
