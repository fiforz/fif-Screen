#include "mf_h264_encoder.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <ppl.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace fif::host {

namespace {

using Microsoft::WRL::ComPtr;

std::string hresult_text(HRESULT hr) {
  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex
      << static_cast<std::uint32_t>(hr);
  return out.str();
}

std::string narrow_utf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::string activation_name(IMFActivate* activation) {
  UINT32 name_length = 0;
  if (FAILED(activation->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &name_length))) {
    return "unknown";
  }
  std::wstring name(name_length + 1, L'\0');
  UINT32 copied = 0;
  if (FAILED(activation->GetString(MFT_FRIENDLY_NAME_Attribute, name.data(),
                                   name_length + 1, &copied))) {
    return "unknown";
  }
  name.resize(copied);
  return narrow_utf8(name);
}

void log_h264_encoder_candidates() {
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
  IMFActivate** activations = nullptr;
  UINT32 count = 0;
  const HRESULT hr = MFTEnumEx(
      MFT_CATEGORY_VIDEO_ENCODER,
      MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER,
      &input, &output, &activations, &count);
  if (FAILED(hr)) {
    std::cout << "FIFSCREEN_HOST event=encoder_enumeration_failed hr="
              << hresult_text(hr) << "\n";
    return;
  }
  for (UINT32 i = 0; i < count; ++i) {
    UINT32 async = FALSE;
    activations[i]->GetUINT32(MF_TRANSFORM_ASYNC, &async);
    UINT32 hardware_url_length = 0;
    const bool hardware = SUCCEEDED(activations[i]->GetStringLength(
        MFT_ENUM_HARDWARE_URL_Attribute, &hardware_url_length));
    std::cout << "FIFSCREEN_HOST event=encoder_candidate index=" << i
              << " name=\"" << activation_name(activations[i]) << "\""
              << " hardware=" << (hardware ? "true" : "false")
              << " async=" << (async ? "true" : "false") << "\n";
    activations[i]->Release();
  }
  CoTaskMemFree(activations);
}

bool activate_hardware_h264_encoder(ComPtr<IMFTransform>& transform,
                                    std::string& name) {
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
  IMFActivate** activations = nullptr;
  UINT32 count = 0;
  const HRESULT hr = MFTEnumEx(
      MFT_CATEGORY_VIDEO_ENCODER,
      MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
      &input, &output, &activations, &count);
  if (FAILED(hr)) {
    return false;
  }

  bool activated = false;
  for (UINT32 i = 0; i < count && !activated; ++i) {
    ComPtr<IMFTransform> candidate;
    if (SUCCEEDED(activations[i]->ActivateObject(IID_PPV_ARGS(&candidate)))) {
      name = activation_name(activations[i]);
      transform = std::move(candidate);
      activated = true;
    }
  }
  for (UINT32 i = 0; i < count; ++i) {
    activations[i]->Release();
  }
  CoTaskMemFree(activations);
  return activated;
}

std::uint64_t steady_now_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint8_t clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void bgra_to_nv12(const std::uint8_t* bgra,
                  std::uint32_t stride,
                  std::uint32_t width,
                  std::uint32_t height,
                  std::uint8_t* nv12) {
  auto* y_plane = nv12;
  auto* uv_plane = nv12 + static_cast<std::size_t>(width) * height;

  constexpr std::uint32_t kRowsPerChunk = 32;
  const std::uint32_t chunk_count = (height + kRowsPerChunk - 1) / kRowsPerChunk;
  concurrency::parallel_for<std::uint32_t>(0, chunk_count, [&](std::uint32_t chunk) {
    const std::uint32_t first_y = chunk * kRowsPerChunk;
    const std::uint32_t end_y = std::min(height, first_y + kRowsPerChunk);
    for (std::uint32_t y = first_y; y < end_y; y += 2) {
      const auto* row0 = bgra + static_cast<std::size_t>(y) * stride;
      const auto* row1 = bgra + static_cast<std::size_t>(std::min(y + 1, height - 1)) * stride;
      auto* y0 = y_plane + static_cast<std::size_t>(y) * width;
      auto* y1 = y_plane + static_cast<std::size_t>(std::min(y + 1, height - 1)) * width;
      auto* uv = uv_plane + static_cast<std::size_t>(y / 2) * width;

      for (std::uint32_t x = 0; x < width; x += 2) {
        int u_sum = 0;
        int v_sum = 0;
        for (std::uint32_t dy = 0; dy < 2; ++dy) {
          const auto* row = dy == 0 ? row0 : row1;
          auto* y_out = dy == 0 ? y0 : y1;
          for (std::uint32_t dx = 0; dx < 2; ++dx) {
            const std::uint32_t pixel_x = std::min(x + dx, width - 1);
            const auto* pixel = row + static_cast<std::size_t>(pixel_x) * 4;
            const int b = pixel[0];
            const int g = pixel[1];
            const int r = pixel[2];
            y_out[pixel_x] = clamp_byte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            u_sum += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            v_sum += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
          }
        }
        uv[x] = clamp_byte((u_sum + 2) / 4);
        if (x + 1 < width) {
          uv[x + 1] = clamp_byte((v_sum + 2) / 4);
        }
      }
    }
  });
}

bool starts_with_annex_b(const std::vector<std::uint8_t>& bytes) {
  return bytes.size() >= 4 &&
      ((bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1) ||
       (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1));
}

std::vector<std::uint8_t> normalize_annex_b(std::vector<std::uint8_t> bytes) {
  if (bytes.empty() || starts_with_annex_b(bytes)) {
    return bytes;
  }

  std::vector<std::uint8_t> annex_b;
  annex_b.reserve(bytes.size() + 32);
  std::size_t offset = 0;
  while (offset + 4 <= bytes.size()) {
    const std::uint32_t length =
        (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
    offset += 4;
    if (length == 0 || length > bytes.size() - offset) {
      return bytes;
    }
    annex_b.insert(annex_b.end(), {0, 0, 0, 1});
    annex_b.insert(annex_b.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
  }
  return offset == bytes.size() ? std::move(annex_b) : std::move(bytes);
}

std::vector<std::uint8_t> annex_b_nal_types(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> types;
  for (std::size_t i = 0; i + 3 < bytes.size();) {
    std::size_t start = std::string::npos;
    std::size_t prefix = 0;
    for (; i + 3 < bytes.size(); ++i) {
      if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1) {
        start = i;
        prefix = 3;
        break;
      }
      if (i + 4 < bytes.size() && bytes[i] == 0 && bytes[i + 1] == 0 &&
          bytes[i + 2] == 0 && bytes[i + 3] == 1) {
        start = i;
        prefix = 4;
        break;
      }
    }
    if (start == std::string::npos || start + prefix >= bytes.size()) {
      break;
    }
    types.push_back(bytes[start + prefix] & 0x1fu);
    i = start + prefix + 1;
  }
  return types;
}

bool set_codec_u32(ICodecAPI* api, const GUID& key, std::uint32_t value) {
  if (!api) {
    return false;
  }
  VARIANT setting;
  VariantInit(&setting);
  setting.vt = VT_UI4;
  setting.ulVal = value;
  return SUCCEEDED(api->SetValue(&key, &setting));
}

bool set_codec_bool(ICodecAPI* api, const GUID& key, bool value) {
  if (!api) {
    return false;
  }
  VARIANT setting;
  VariantInit(&setting);
  setting.vt = VT_BOOL;
  setting.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  return SUCCEEDED(api->SetValue(&key, &setting));
}

}  // namespace

struct MfH264Encoder::Impl {
  struct PendingFrame {
    LONGLONG sample_time = 0;
    std::uint64_t frame_id = 0;
    std::uint64_t capture_ns = 0;
    std::uint64_t submit_ns = 0;
  };

  EncoderConfig config{};
  ComPtr<IMFTransform> transform;
  ComPtr<ICodecAPI> codec_api;
  ComPtr<IMFMediaEventGenerator> event_generator;
  MFT_OUTPUT_STREAM_INFO output_info{};
  DWORD input_stream = 0;
  DWORD output_stream = 0;
  bool com_initialized = false;
  bool mf_started = false;
  bool initialized = false;
  bool async_transform = false;
  bool bgra_input = false;
  bool need_input = true;
  bool force_idr = true;
  std::string encoder_name = "Microsoft H.264 Encoder MFT";
  std::string error;
  std::deque<PendingFrame> pending_frames;

  bool fail(const std::string& operation, HRESULT hr) {
    error = operation + " failed (" + hresult_text(hr) + ")";
    initialized = false;
    return false;
  }

  bool configure_media_types() {
    ComPtr<IMFMediaType> output_type;
    HRESULT hr = MFCreateMediaType(&output_type);
    if (FAILED(hr)) {
      return fail("MFCreateMediaType(output)", hr);
    }
    output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    output_type->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate_bps);
    output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    output_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
    output_type->SetUINT32(MF_MT_MPEG2_LEVEL, eAVEncH264VLevel4_2);
    MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, config.width, config.height);
    MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, config.refresh_hz, 1);
    MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = transform->SetOutputType(output_stream, output_type.Get(), 0);
    if (FAILED(hr)) {
      output_type->DeleteItem(MF_MT_MPEG2_LEVEL);
      output_type->DeleteItem(MF_MT_MPEG2_PROFILE);
      hr = transform->SetOutputType(output_stream, output_type.Get(), 0);
    }
    if (FAILED(hr)) {
      return fail("IMFTransform::SetOutputType", hr);
    }

    ComPtr<IMFMediaType> input_type;
    hr = MFCreateMediaType(&input_type);
    if (FAILED(hr)) {
      return fail("MFCreateMediaType(input)", hr);
    }
    input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    input_type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, config.width, config.height);
    MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, config.refresh_hz, 1);
    MFSetAttributeRatio(input_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    input_type->SetUINT32(MF_MT_DEFAULT_STRIDE, config.width * 4);
    input_type->SetUINT32(MF_MT_SAMPLE_SIZE, config.width * config.height * 4);
    hr = transform->SetInputType(input_stream, input_type.Get(), 0);
    if (SUCCEEDED(hr)) {
      bgra_input = true;
      return true;
    }

    input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    input_type->SetUINT32(MF_MT_DEFAULT_STRIDE, config.width);
    input_type->SetUINT32(MF_MT_SAMPLE_SIZE, config.width * config.height * 3 / 2);
    hr = transform->SetInputType(input_stream, input_type.Get(), 0);
    if (FAILED(hr)) {
      return fail("IMFTransform::SetInputType", hr);
    }
    bgra_input = false;
    return true;
  }

  void reset_transform_state() {
    codec_api.Reset();
    event_generator.Reset();
    transform.Reset();
    output_info = {};
    input_stream = 0;
    output_stream = 0;
    initialized = false;
    async_transform = false;
    bgra_input = false;
    need_input = true;
    force_idr = true;
    pending_frames.clear();
  }

  bool configure_active_transform() {
    HRESULT hr = S_OK;
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(transform->GetAttributes(&attributes))) {
      UINT32 is_async = FALSE;
      attributes->GetUINT32(MF_TRANSFORM_ASYNC, &is_async);
      async_transform = is_async != FALSE;
      if (async_transform) {
        hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        if (FAILED(hr)) {
          return fail("IMFAttributes::SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK)", hr);
        }
      }
      attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    }

    DWORD input_count = 0;
    DWORD output_count = 0;
    hr = transform->GetStreamCount(&input_count, &output_count);
    if (FAILED(hr) || input_count != 1 || output_count != 1) {
      return fail("IMFTransform::GetStreamCount", FAILED(hr) ? hr : E_UNEXPECTED);
    }
    DWORD input_id = 0;
    DWORD output_id = 0;
    hr = transform->GetStreamIDs(1, &input_id, 1, &output_id);
    if (SUCCEEDED(hr)) {
      input_stream = input_id;
      output_stream = output_id;
    } else if (hr != E_NOTIMPL) {
      return fail("IMFTransform::GetStreamIDs", hr);
    }

    if (!configure_media_types()) {
      return false;
    }

    transform.As(&codec_api);
    set_codec_bool(codec_api.Get(), CODECAPI_AVEncCommonLowLatency, true);
    set_codec_bool(codec_api.Get(), CODECAPI_AVEncCommonRealTime, true);
    set_codec_u32(codec_api.Get(), CODECAPI_AVEncCommonRateControlMode,
                  eAVEncCommonRateControlMode_CBR);
    set_codec_u32(codec_api.Get(), CODECAPI_AVEncCommonMeanBitRate, config.bitrate_bps);
    set_codec_u32(codec_api.Get(), CODECAPI_AVEncCommonQualityVsSpeed, 100);
    set_codec_u32(codec_api.Get(), CODECAPI_AVEncMPVGOPSize, config.refresh_hz);
    set_codec_u32(codec_api.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    set_codec_bool(codec_api.Get(), CODECAPI_AVEncH264CABACEnable, true);

    hr = transform->GetOutputStreamInfo(output_stream, &output_info);
    if (FAILED(hr)) {
      return fail("IMFTransform::GetOutputStreamInfo", hr);
    }
    transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (async_transform) {
      hr = transform.As(&event_generator);
      if (FAILED(hr)) {
        return fail("QueryInterface(IMFMediaEventGenerator)", hr);
      }
      need_input = false;
    } else {
      need_input = true;
    }
    initialized = true;
    force_idr = true;
    pending_frames.clear();
    error.clear();
    return true;
  }

  bool initialize(const EncoderConfig& requested) {
    if (requested.width == 0 || requested.height == 0 || requested.refresh_hz == 0 ||
        (requested.width & 1u) != 0 || (requested.height & 1u) != 0) {
      error = "H.264 encoder requires non-zero even dimensions and frame rate";
      return false;
    }
    config = requested;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
      com_initialized = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
      return fail("CoInitializeEx", hr);
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
      return fail("MFStartup", hr);
    }
    mf_started = true;

    log_h264_encoder_candidates();

    if (activate_hardware_h264_encoder(transform, encoder_name)) {
      if (configure_active_transform()) {
        return true;
      }
      std::cout << "FIFSCREEN_HOST event=encoder_candidate_rejected encoder=\""
                << encoder_name << "\" reason=\"" << error << "\"\n";
      reset_transform_state();
      error.clear();
    }

    encoder_name = "Microsoft H.264 Encoder MFT";
    hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&transform));
    if (FAILED(hr)) {
      return fail("CoCreateInstance(CLSID_CMSH264EncoderMFT)", hr);
    }
    return configure_active_transform();
  }

  std::vector<EncodedPacket> drain_output() {
    std::vector<EncodedPacket> packets;
    while (initialized) {
      ComPtr<IMFSample> provided_sample;
      MFT_OUTPUT_DATA_BUFFER output{};
      output.dwStreamID = output_stream;

      if ((output_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
        HRESULT hr = MFCreateSample(&provided_sample);
        if (FAILED(hr)) {
          fail("MFCreateSample(output)", hr);
          break;
        }
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD buffer_size = std::max<DWORD>(
            output_info.cbSize,
            std::max<DWORD>(1024 * 1024, config.width * config.height * 2));
        hr = output_info.cbAlignment > 0
            ? MFCreateAlignedMemoryBuffer(buffer_size, output_info.cbAlignment, &buffer)
            : MFCreateMemoryBuffer(buffer_size, &buffer);
        if (FAILED(hr)) {
          fail("MFCreateMemoryBuffer(output)", hr);
          break;
        }
        provided_sample->AddBuffer(buffer.Get());
        output.pSample = provided_sample.Get();
      }

      DWORD status = 0;
      const HRESULT hr = transform->ProcessOutput(0, 1, &output, &status);
      if (output.pEvents) {
        output.pEvents->Release();
      }
      if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        break;
      }
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> available;
        if (SUCCEEDED(transform->GetOutputAvailableType(output_stream, 0, &available)) &&
            SUCCEEDED(transform->SetOutputType(output_stream, available.Get(), 0))) {
          transform->GetOutputStreamInfo(output_stream, &output_info);
          continue;
        }
      }
      if (FAILED(hr)) {
        fail("IMFTransform::ProcessOutput", hr);
        break;
      }

      ComPtr<IMFSample> encoded_sample;
      if (provided_sample && output.pSample == provided_sample.Get()) {
        encoded_sample = provided_sample;
      } else if (output.pSample) {
        encoded_sample.Attach(output.pSample);
      }
      if (!encoded_sample) {
        continue;
      }

      ComPtr<IMFMediaBuffer> contiguous;
      if (FAILED(encoded_sample->ConvertToContiguousBuffer(&contiguous))) {
        continue;
      }
      BYTE* data = nullptr;
      DWORD max_length = 0;
      DWORD current_length = 0;
      if (FAILED(contiguous->Lock(&data, &max_length, &current_length))) {
        continue;
      }
      std::vector<std::uint8_t> bytes(data, data + current_length);
      contiguous->Unlock();
      bytes = normalize_annex_b(std::move(bytes));
      if (bytes.empty()) {
        continue;
      }

      UINT32 clean_point = FALSE;
      encoded_sample->GetUINT32(MFSampleExtension_CleanPoint, &clean_point);
      const auto nal_types = annex_b_nal_types(bytes);
      const bool has_idr = std::find(nal_types.begin(), nal_types.end(), 5) != nal_types.end();
      const bool has_config =
          std::find(nal_types.begin(), nal_types.end(), 7) != nal_types.end() ||
          std::find(nal_types.begin(), nal_types.end(), 8) != nal_types.end();
      const bool has_vcl = std::any_of(nal_types.begin(), nal_types.end(),
                                       [](std::uint8_t type) { return type >= 1 && type <= 5; });

      PendingFrame metadata;
      LONGLONG sample_time = 0;
      encoded_sample->GetSampleTime(&sample_time);
      if (has_vcl && !pending_frames.empty()) {
        auto match = std::find_if(pending_frames.begin(), pending_frames.end(),
                                  [sample_time](const PendingFrame& pending) {
                                    return pending.sample_time == sample_time;
                                  });
        if (match == pending_frames.end()) {
          match = pending_frames.begin();
        }
        metadata = *match;
        pending_frames.erase(pending_frames.begin(), std::next(match));
      } else {
        metadata.sample_time = sample_time;
        metadata.capture_ns = sample_time > 0
            ? static_cast<std::uint64_t>(sample_time) * 100
            : steady_now_ns();
        metadata.submit_ns = metadata.capture_ns;
      }

      EncodedPacket packet;
      packet.frame_id = metadata.frame_id;
      packet.t0_capture_ns = metadata.capture_ns;
      packet.t1_submit_ns = metadata.submit_ns;
      packet.t2_encoded_ns = steady_now_ns();
      packet.is_idr = clean_point != FALSE || has_idr;
      packet.is_codec_config = has_config && !has_vcl;
      packet.bytes = std::move(bytes);
      packets.push_back(std::move(packet));
      if (async_transform) {
        break;
      }
    }
    return packets;
  }

  static void append_packets(std::vector<EncodedPacket>& destination,
                             std::vector<EncodedPacket> source) {
    destination.insert(destination.end(),
                       std::make_move_iterator(source.begin()),
                       std::make_move_iterator(source.end()));
  }

  bool process_async_event(bool wait, std::vector<EncodedPacket>& packets) {
    if (!event_generator) {
      return false;
    }
    ComPtr<IMFMediaEvent> event;
    const HRESULT hr = event_generator->GetEvent(
        wait ? 0 : MF_EVENT_FLAG_NO_WAIT, &event);
    if (!wait && hr == MF_E_NO_EVENTS_AVAILABLE) {
      return false;
    }
    if (FAILED(hr)) {
      fail("IMFMediaEventGenerator::GetEvent", hr);
      return false;
    }

    HRESULT event_status = S_OK;
    event->GetStatus(&event_status);
    if (FAILED(event_status)) {
      fail("asynchronous encoder event", event_status);
      return false;
    }
    MediaEventType type = MEUnknown;
    event->GetType(&type);
    if (type == METransformNeedInput) {
      need_input = true;
    } else if (type == METransformHaveOutput) {
      append_packets(packets, drain_output());
    }
    return initialized;
  }

  std::vector<EncodedPacket> encode_bgra(const std::uint8_t* bgra,
                                         std::uint32_t stride,
                                         std::uint64_t frame_id,
                                         std::uint64_t timestamp_ns) {
    if (!initialized || !bgra || stride < config.width * 4) {
      return {};
    }

    std::vector<EncodedPacket> packets;
    if (async_transform) {
      while (pending_frames.size() >= 2 && initialized) {
        process_async_event(true, packets);
      }
      while (!need_input && initialized) {
        process_async_event(true, packets);
      }
      while (initialized && process_async_event(false, packets)) {
      }
      if (!initialized) {
        return packets;
      }
    }

    if (force_idr) {
      set_codec_u32(codec_api.Get(), CODECAPI_AVEncVideoForceKeyFrame, TRUE);
      force_idr = false;
    }

    ComPtr<IMFSample> sample;
    HRESULT hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
      fail("MFCreateSample(input)", hr);
      return {};
    }
    ComPtr<IMFMediaBuffer> buffer;
    const DWORD input_size = bgra_input
        ? config.width * config.height * 4
        : config.width * config.height * 3 / 2;
    hr = MFCreateMemoryBuffer(input_size, &buffer);
    if (FAILED(hr)) {
      fail("MFCreateMemoryBuffer(input)", hr);
      return {};
    }
    BYTE* data = nullptr;
    DWORD max_length = 0;
    if (FAILED(buffer->Lock(&data, &max_length, nullptr)) || max_length < input_size) {
      fail("IMFMediaBuffer::Lock(input)", E_FAIL);
      return {};
    }
    if (bgra_input) {
      const std::size_t row_bytes = static_cast<std::size_t>(config.width) * 4;
      for (std::uint32_t y = 0; y < config.height; ++y) {
        std::memcpy(data + static_cast<std::size_t>(y) * row_bytes,
                    bgra + static_cast<std::size_t>(y) * stride, row_bytes);
      }
    } else {
      bgra_to_nv12(bgra, stride, config.width, config.height, data);
    }
    buffer->Unlock();
    buffer->SetCurrentLength(input_size);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(static_cast<LONGLONG>(timestamp_ns / 100));
    sample->SetSampleDuration(static_cast<LONGLONG>(10'000'000 / config.refresh_hz));

    const std::uint64_t submit_ns = steady_now_ns();
    hr = transform->ProcessInput(input_stream, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
      if (async_transform) {
        while (!need_input && initialized) {
          process_async_event(true, packets);
        }
      } else {
        append_packets(packets, drain_output());
      }
      hr = transform->ProcessInput(input_stream, sample.Get(), 0);
      if (FAILED(hr)) {
        fail("IMFTransform::ProcessInput(retry)", hr);
        return packets;
      }
    }
    if (FAILED(hr)) {
      fail("IMFTransform::ProcessInput", hr);
      return packets;
    }

    pending_frames.push_back(PendingFrame{
        static_cast<LONGLONG>(timestamp_ns / 100), frame_id, timestamp_ns, submit_ns});
    if (async_transform) {
      need_input = false;
      process_async_event(true, packets);
      while (initialized && process_async_event(false, packets)) {
      }
    } else {
      append_packets(packets, drain_output());
    }
    return packets;
  }

  void shutdown() {
    if (transform) {
      transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
      transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
      transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    codec_api.Reset();
    event_generator.Reset();
    transform.Reset();
    initialized = false;
    pending_frames.clear();
    if (mf_started) {
      MFShutdown();
      mf_started = false;
    }
    if (com_initialized) {
      CoUninitialize();
      com_initialized = false;
    }
  }
};

MfH264Encoder::MfH264Encoder() : impl_(std::make_unique<Impl>()) {}

MfH264Encoder::~MfH264Encoder() {
  shutdown();
}

bool MfH264Encoder::initialize(const EncoderConfig& config) {
  shutdown();
  return impl_->initialize(config);
}

std::vector<EncodedPacket> MfH264Encoder::encode_bgra(const std::uint8_t* bgra,
                                                       std::uint32_t stride,
                                                       std::uint64_t frame_id,
                                                       std::uint64_t timestamp_ns) {
  return impl_->encode_bgra(bgra, stride, frame_id, timestamp_ns);
}

void MfH264Encoder::request_idr() {
  impl_->force_idr = true;
}

void MfH264Encoder::shutdown() {
  if (impl_) {
    impl_->shutdown();
  }
}

bool MfH264Encoder::healthy() const {
  return impl_ && impl_->initialized;
}

const std::string& MfH264Encoder::name() const {
  return impl_->encoder_name;
}

const std::string& MfH264Encoder::last_error() const {
  return impl_->error;
}

}  // namespace fif::host
