#include <windows.h>
#include <cfgmgr32.h>
#include <conio.h>
#include <setupapi.h>
#include <swdevice.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kEnumeratorName[] = L"FifScreenIdd";
constexpr wchar_t kParentDevice[] = L"HTREE\\ROOT\\0";
constexpr wchar_t kInstanceId[] = L"FifIddDriver";
constexpr wchar_t kHardwareId[] = L"FifIddDriver";
constexpr wchar_t kHardwareIdsMultiSz[] = L"FifIddDriver\0\0";
constexpr wchar_t kCompatibleIdsMultiSz[] = L"FifIddDriver\0\0";
constexpr wchar_t kDescription[] = L"FifScreen Indirect Display";

struct CreateContext {
  HANDLE event = nullptr;
  HRESULT result = E_PENDING;
  std::wstring instance_id;
};

std::wstring to_lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return value;
}

bool contains_case_insensitive(const std::wstring& haystack, const std::wstring& needle) {
  return to_lower(haystack).find(to_lower(needle)) != std::wstring::npos;
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
  std::vector<std::wstring> hardware_ids;
};

bool is_fifscreen_device(const DeviceRecord& record) {
  if (contains_case_insensitive(record.instance_id, L"SWD\\FifScreenIdd") ||
      contains_case_insensitive(record.instance_id, L"FifIddDriver")) {
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
    record.hardware_ids = parse_multi_sz(get_registry_property(info_set, dev_info, SPDRP_HARDWAREID));

    if (is_fifscreen_device(record)) {
      devices.push_back(record);
    }
  }

  SetupDiDestroyDeviceInfoList(info_set);
  return devices;
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

int command_create() {
  CreateContext context;
  context.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!context.event) {
    std::wcerr << L"CreateEventW failed: " << format_win32_error(GetLastError()) << L"\n";
    return 1;
  }

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
    CloseHandle(context.event);
    return 1;
  }

  const DWORD wait_result = WaitForSingleObject(context.event, 10 * 1000);
  if (wait_result != WAIT_OBJECT_0 || FAILED(context.result)) {
    std::wcerr << L"Software device creation did not complete. wait=0x"
               << std::hex << wait_result << L" result=0x" << context.result << L"\n";
    if (sw_device) {
      SwDeviceClose(sw_device);
    }
    CloseHandle(context.event);
    return 1;
  }

  std::wcout << L"created_instance=" << context.instance_id << L"\n";
  std::wcout << L"hardware_id=" << kHardwareId << L"\n";
  std::wcout << L"Press X to remove the software device and exit.\n";

  for (;;) {
    const int key = _getch();
    if (key == L'x' || key == L'X') {
      break;
    }
  }

  SwDeviceClose(sw_device);
  CloseHandle(context.event);
  std::wcout << L"removed_by_close=true\n";
  return 0;
}

int command_status() {
  const auto devices = find_devices(false);
  if (devices.empty()) {
    std::wcout << L"fifscreen_software_device_present=false\n";
    return 0;
  }

  std::wcout << L"fifscreen_software_device_present=true\n";
  for (const auto& device : devices) {
    std::wcout << L"instance_id=" << device.instance_id << L"\n";
    if (!device.friendly_name.empty()) {
      std::wcout << L"friendly_name=" << device.friendly_name << L"\n";
    }
    if (!device.description.empty()) {
      std::wcout << L"description=" << device.description << L"\n";
    }
    for (const auto& hardware_id : device.hardware_ids) {
      std::wcout << L"hardware_id=" << hardware_id << L"\n";
    }
  }
  return 0;
}

int command_remove() {
  HDEVINFO info_set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
  if (info_set == INVALID_HANDLE_VALUE) {
    std::wcerr << L"SetupDiGetClassDevsW failed: " << format_win32_error(GetLastError()) << L"\n";
    return 1;
  }

  int removed = 0;
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
    record.hardware_ids = parse_multi_sz(get_registry_property(info_set, dev_info, SPDRP_HARDWAREID));

    if (!is_fifscreen_device(record)) {
      continue;
    }

    SP_REMOVEDEVICE_PARAMS remove_params{};
    remove_params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    remove_params.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    remove_params.Scope = DI_REMOVEDEVICE_GLOBAL;
    remove_params.HwProfile = 0;

    if (!SetupDiSetClassInstallParamsW(
            info_set, &dev_info, &remove_params.ClassInstallHeader, sizeof(remove_params)) ||
        !SetupDiCallClassInstaller(DIF_REMOVE, info_set, &dev_info)) {
      std::wcerr << L"remove_failed instance_id=" << record.instance_id
                 << L" error=" << format_win32_error(GetLastError()) << L"\n";
      SetupDiDestroyDeviceInfoList(info_set);
      return 1;
    }

    std::wcout << L"removed_instance=" << record.instance_id << L"\n";
    ++removed;
  }

  SetupDiDestroyDeviceInfoList(info_set);
  std::wcout << L"removed_count=" << removed << L"\n";
  return 0;
}

void print_usage() {
  std::wcout << L"fif-idd-device-launcher <create|status|remove>\n";
  std::wcout << L"  create  Create FifScreen software device and hold it until X is pressed.\n";
  std::wcout << L"  status  Enumerate exact FifScreen software device records only.\n";
  std::wcout << L"  remove  Remove exact FifScreen software device records only.\n";
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
    if (command == L"remove") {
      return command_remove();
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }

  print_usage();
  return 2;
}

