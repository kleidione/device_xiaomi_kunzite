/*
 * Copyright (C) 2024 LibreMobileOS Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProviderExtension.h"
#include <fstream>
#include <log/log.h>

#define TORCH_BRIGHTNESS "brightness"
#define TOGGLE_SWITCH "/sys/devices/platform/soc/c42d000.qcom,spmi/spmi-0/0-05/c42d000.qcom,spmi:qcom,pm6150l@5:qcom,leds@d300/leds/led:switch_2/brightness"

static std::string kTorchLedPaths[] = {
        "/sys/devices/platform/soc/c42d000.qcom,spmi/spmi-0/0-05/c42d000.qcom,spmi:qcom,pm6150l@5:qcom,leds@d300/leds/led:torch_0",
        "/sys/devices/platform/soc/c42d000.qcom,spmi/spmi-0/0-05/c42d000.qcom,spmi:qcom,pm6150l@5:qcom,leds@d300/leds/led:torch_1",
};

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
    if (file.fail()) {
        ALOGE("Failed to write '%s' to %s", std::to_string(value).c_str(), path.c_str());
    }
}

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;
    file >> result;
    return file.fail() ? def : result;
}

bool supportsTorchStrengthControlExt() {
    return true;
}

bool supportsSetTorchModeExt() {
    return false;
}

static constexpr int32_t kTorchMinStrengthLevel     = 10;
static constexpr int32_t kTorchDefaultStrengthLevel = 25;
static constexpr int32_t kTorchMaxStrengthLevel     = 200;

int32_t getTorchDefaultStrengthLevelExt() {
    return kTorchDefaultStrengthLevel;
}

int32_t getTorchMaxStrengthLevelExt() {
    return kTorchMaxStrengthLevel;
}

int32_t getTorchStrengthLevelExt() {
    auto node = kTorchLedPaths[0] + "/" + TORCH_BRIGHTNESS;
    int32_t val = get(node, kTorchMinStrengthLevel);
    return (val < kTorchMinStrengthLevel) ? kTorchMinStrengthLevel : val;
}

void setTorchStrengthLevelExt(int32_t torchStrength, bool enabled) {
    if (enabled) {
        int32_t effectiveStrength = (torchStrength < kTorchMinStrengthLevel)
                                        ? kTorchMinStrengthLevel
                                        : torchStrength;

        // Reset the switch before applying the new value, forcing the
        // driver to relatch the new current instead of ignoring the
        // change while the LED is already on.
        set(TOGGLE_SWITCH, 0);

        for (auto& path : kTorchLedPaths) {
            set(path + "/" + TORCH_BRIGHTNESS, effectiveStrength);
        }

        set(TOGGLE_SWITCH, 1);
    } else {
        set(TOGGLE_SWITCH, 0);
    }
}

void setTorchModeExt(bool enabled) {
    if (enabled) {
        int32_t strength = getTorchStrengthLevelExt();
        if (strength <= 0) strength = kTorchDefaultStrengthLevel;
        setTorchStrengthLevelExt(strength, true);
    } else {
        setTorchStrengthLevelExt(0, false);
    }
}
