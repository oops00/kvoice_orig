﻿#include "kvoice.hpp"

#include "voice_exception.hpp"
#include "sound_output_impl.hpp"
#include "sound_input_impl.hpp"

std::unordered_map<std::string, std::string> kvoice::get_input_devices() {
    std::unordered_map<std::string, std::string> res;

    BASS_DEVICEINFO info;
    for (auto i = 0u; BASS_RecordGetDeviceInfo(i, &info); i++) {
        if ((info.flags & BASS_DEVICE_ENABLED)) {
            auto type = info.flags & BASS_DEVICE_TYPE_MASK;
            if (type == BASS_DEVICE_TYPE_MICROPHONE ||
                type == BASS_DEVICE_TYPE_HANDSET ||
                type == BASS_DEVICE_TYPE_HEADSET ||
                type == BASS_DEVICE_TYPE_LINE ||
                type == BASS_DEVICE_TYPE_DIGITAL) {
                res[info.driver] = info.name;
            }
        }
    }
    return res;
}

std::unordered_map<std::string, std::string> kvoice::get_output_devices() {
    std::unordered_map<std::string, std::string> res;

    BASS_DEVICEINFO info;
    for (auto i = 1u; BASS_GetDeviceInfo(i, &info); i++)
        if (info.flags & BASS_DEVICE_ENABLED)
            res[info.driver] = info.name;

    return res;
}

std::unique_ptr<kvoice::sound_output> kvoice::create_sound_output(
    std::string_view device_guid, std::uint32_t sample_rate) {
    try {
        return std::make_unique<sound_output_impl>(device_guid, sample_rate);
    } catch (const voice_exception&) {
        return nullptr;
    }
}

std::unique_ptr<kvoice::sound_input> kvoice::create_sound_input(
    std::string_view device_guid, std::uint32_t       sample_rate,
    std::uint32_t    frames_per_buffer, std::uint32_t bitrate) {
    try {
        return std::make_unique<sound_input_impl>(device_guid, sample_rate, frames_per_buffer, bitrate);
    } catch (const voice_exception&) {
        return nullptr;
    }
}
