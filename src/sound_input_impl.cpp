#include "sound_input_impl.hpp"

#include <opus.h>

#include <algorithm>
#include <array>
#include "ringbuffer.hpp"
#include "rnnoise.h"

#include "voice_exception.hpp"

kvoice::sound_input_impl::sound_input_impl(std::string_view device_guid, std::int32_t        sample_rate,
                                           std::int32_t     frames_per_buffer, std::uint32_t bitrate)
    : sample_rate_(sample_rate),
      frames_per_buffer_(frames_per_buffer),
      encoder_buffer(kOpusFrameSize) {
    BOOL creation_status = false;
    if (device_guid.empty()) {
        creation_status = BASS_RecordInit(-1); // default device
    } else {
        BASS_DEVICEINFO info;
        for (auto i = 0u; BASS_RecordGetDeviceInfo(i, &info); i++) {
            if ((info.flags & BASS_DEVICE_ENABLED)) {
                auto type = info.flags & BASS_DEVICE_TYPE_MASK;
                if ((type == BASS_DEVICE_TYPE_MICROPHONE ||
                     type == BASS_DEVICE_TYPE_HANDSET ||
                     type == BASS_DEVICE_TYPE_HEADSET ||
                     type == BASS_DEVICE_TYPE_LINE ||
                     type == BASS_DEVICE_TYPE_DIGITAL) &&
                    device_guid == info.driver) {
                    creation_status = BASS_RecordInit(i);
                }
            }
        }
    }

    if (!creation_status && BASS_ErrorGetCode() != BASS_ERROR_ALREADY)
        throw voice_exception::create_formatted(
            "Couldn't open capture device {}", device_guid);

    record_handle = BASS_RecordStart(sample_rate, 1, BASS_SAMPLE_FLOAT | BASS_RECORD_PAUSE, &bass_cb, this);

    if (!record_handle) throw voice_exception::create_formatted("Couldn't start capture on device {}", device_guid);

    BASS_ChannelSetAttribute(record_handle, BASS_ATTRIB_GRANULE, static_cast<float>(frames_per_buffer));
    input_volume_fx = BASS_ChannelSetFX(record_handle, BASS_FX_VOLUME, 0);

    set_mic_gain(input_volume);

    int opus_err;
    encoder = opus_encoder_create(sample_rate, 1, OPUS_APPLICATION_VOIP, &opus_err);

    if (opus_err != OPUS_OK || !encoder)
        throw voice_exception::create_formatted("Couldn't create opus encoder (errc = {})", opus_err);

    if ((opus_err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate))) != OPUS_OK)
        throw voice_exception::create_formatted("Couldn't set encoder bitrate (errc = {})", opus_err);

    rnnoise = rnnoise_create(NULL);
}

kvoice::sound_input_impl::~sound_input_impl() {
    BASS_RecordFree();
    opus_encoder_destroy(encoder);
    rnnoise_destroy(rnnoise);
}

bool kvoice::sound_input_impl::enable_input() {
    if (!input_active) {
        if (record_handle) {
            return input_active = BASS_ChannelPlay(record_handle, false);
        }
        return false;
    }
    return false;
}


bool kvoice::sound_input_impl::disable_input() {
    if (input_active) {
        if (record_handle) {
            return input_active = !BASS_ChannelPause(record_handle);
        }
        return false;
    }
    return false;
}

void kvoice::sound_input_impl::set_mic_gain(float gain) {
    input_volume = gain;
    BASS_FX_VOLUME_PARAM param;
    param.fTarget = gain;
    param.fTime = 0;
    BASS_FXSetParameters(input_volume_fx, &param); // apply volume effect settings
}

void kvoice::sound_input_impl::change_device(std::string_view device_guid) {
    BASS_RecordFree();
    BOOL creation_status = false;
    if (device_guid.empty()) {
        creation_status = BASS_RecordInit(-1); // default device
    } else {
        BASS_DEVICEINFO info;
        for (auto i = 0u; BASS_RecordGetDeviceInfo(i, &info); i++) {
            if ((info.flags & BASS_DEVICE_ENABLED)) {
                auto type = info.flags & BASS_DEVICE_TYPE_MASK;
                if ((type == BASS_DEVICE_TYPE_MICROPHONE ||
                     type == BASS_DEVICE_TYPE_HANDSET ||
                     type == BASS_DEVICE_TYPE_HEADSET ||
                     type == BASS_DEVICE_TYPE_LINE ||
                     type == BASS_DEVICE_TYPE_DIGITAL) &&
                    device_guid == info.driver) {
                    creation_status = BASS_RecordInit(i);
                }
            }
        }
    }

    if (!creation_status) throw voice_exception::create_formatted("Couldn't open capture device {}", device_guid);

    record_handle = BASS_RecordStart(sample_rate_, 1, BASS_SAMPLE_FLOAT, &bass_cb, this);

    if (!record_handle) throw voice_exception::create_formatted("Couldn't start capture on device {}", device_guid);

    BASS_ChannelSetAttribute(record_handle, BASS_ATTRIB_GRANULE, static_cast<float>(frames_per_buffer_));
    input_volume_fx = BASS_ChannelSetFX(record_handle, BASS_FX_VOLUME, 0);

    set_mic_gain(input_volume);

    if (!input_active) BASS_ChannelPause(record_handle);
}

void kvoice::sound_input_impl::set_input_callback(std::function<on_voice_input_t> cb) {
    on_voice_input = std::move(cb);
}

void kvoice::sound_input_impl::set_raw_input_callback(std::function<on_voice_raw_input> cb) {
    on_raw_voice_input = std::move(cb);
}

void kvoice::sound_input_impl::toggle_rnnoise(bool toogle) {
    rnnoise_active = toogle;
}

BOOL kvoice::sound_input_impl::process_input(HRECORD handle, const void* buffer, DWORD length) {
    using namespace std::chrono_literals;

    auto        float_buff = static_cast<float*>(const_cast<void*>(buffer));
    const DWORD buff_len = length / sizeof(float);

    std::array<std::uint8_t, kPacketMaxSize> packet{};

    const float mic_level = *std::max_element(float_buff, float_buff + buff_len);
    if (on_raw_voice_input)
        on_raw_voice_input(float_buff, buff_len, mic_level);

    temporary_buffer.writeBuff(float_buff, buff_len);

    while (temporary_buffer.readAvailable() >= kOpusFrameSize) {
        auto count = temporary_buffer.readBuff(encoder_buffer.data(), kOpusFrameSize);

        if (rnnoise_active)
            rnnoise_process_frame(rnnoise, encoder_buffer.data(), encoder_buffer.data());

        const int len = opus_encode_float(encoder, encoder_buffer.data(), count, packet.data(),
                                          kPacketMaxSize);
        if (len < 0 || len > kPacketMaxSize) return true;
        if (on_voice_input)
            on_voice_input(packet.data(), len);
    }

    return true;
}

BOOL kvoice::sound_input_impl::bass_cb(HRECORD handle, const void* buffer, DWORD length, void* user) {
    return static_cast<kvoice::sound_input_impl*>(user)->process_input(handle, buffer, length);
}
