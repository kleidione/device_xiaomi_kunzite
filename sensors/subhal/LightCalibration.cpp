/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "LightCalibration.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

constexpr char kCaliJson[] = "/mnt/vendor/persist/sensors/lightSensorCali.json";
constexpr char kConfigJson[] = "/odm/etc/sensors/lightSensorConfig.json";
constexpr char kConfigJsonSec[] = "/odm/etc/sensors/lightSensorConfigSec.json";
constexpr char kConfigJsonThird[] = "/odm/etc/sensors/lightSensorConfigThird.json";
constexpr char kConfigJsonFourth[] = "/odm/etc/sensors/lightSensorConfigFourth.json";
constexpr char kPanelInfo[] = "/sys/class/mi_display/disp-DSI-0/panel_info";
constexpr char kPanelP17_35[] = "mdss_dsi_p17_35_0F_0b";
constexpr char kPanelP17_41_02[] = "mdss_dsi_p17_41_02_0a";
constexpr char kPanelP17_41_0F[] = "mdss_dsi_p17_41_0F_0c";

const char* configForPanel() {
    std::string info;
    if (!android::base::ReadFileToString(kPanelInfo, &info)) {
        LOG(WARNING) << "no panel info, assuming the primary panel";
        return kConfigJson;
    }
    const char* config = kConfigJson;
    const char* name = "primary";
    if (info.find(kPanelP17_41_02) != std::string::npos) {
        config = kConfigJsonSec;
        name = "secondary";
    } else if (info.find(kPanelP17_41_0F) != std::string::npos) {
        config = kConfigJsonThird;
        name = "third";
    } else if (info.find(kPanelP17_35) == std::string::npos) {
        config = kConfigJsonFourth;
        name = "fourth (unknown panel)";
    }
    LOG(INFO) << "panel is " << name;
    return config;
}
constexpr char kRegistryDir[] = "/mnt/vendor/persist/sensors/registry/registry";
// kunzite uses the sip1328 under-display ALS. Its registry entries are indexed ("0".."13"),
// not named, and its scale lives in sip1328_platform.als.fac_cal as "lux_scale".
constexpr char kCoefFile[] = "sip1328_platform.coefficient";
constexpr char kFacCalFile[] = "sip1328_platform.als.fac_cal";
constexpr size_t kCoefCount = 7;
constexpr float kLuxDivisor = 25600.0f;

// Finds `"key"` only when it is used as a JSON key (followed by ':'), so a value such as
// "ver":"2" is never mistaken for the key "2".
size_t findKey(const std::string& blob, const std::string& key, size_t from = 0) {
    const std::string needle = "\"" + key + "\"";
    for (size_t k = blob.find(needle, from); k != std::string::npos;
         k = blob.find(needle, k + 1)) {
        size_t after = k + needle.size();
        while (after < blob.size() && isspace(static_cast<unsigned char>(blob[after]))) {
            after++;
        }
        if (after < blob.size() && blob[after] == ':') {
            return k;
        }
    }
    return std::string::npos;
}

bool findFloat(const std::string& blob, const std::string& key, float* out) {
    size_t k = findKey(blob, key);
    if (k == std::string::npos) {
        return false;
    }
    size_t d = findKey(blob, "data", k);
    if (d == std::string::npos) {
        return false;
    }
    size_t q1 = blob.find('"', d + 6);
    if (q1 == std::string::npos) {
        return false;
    }
    size_t q2 = blob.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return false;
    }
    *out = strtof(blob.substr(q1 + 1, q2 - q1 - 1).c_str(), nullptr);
    return true;
}

}  // namespace

bool LightCalibration::loadConfigCoefficients(const std::string& path) {
    std::string blob;
    if (!android::base::ReadFileToString(path, &blob)) {
        return false;
    }
    size_t start = blob.find("\"channel_coef\"");
    if (start == std::string::npos) {
        return false;
    }
    size_t q1 = blob.find('"', blob.find("\"coef_data\"", start) + 11);
    size_t q2 = q1 == std::string::npos ? q1 : blob.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return false;
    }

    const auto parts = android::base::Split(blob.substr(q1 + 1, q2 - q1 - 1), ",");
    if (parts.size() < 7) {
        return false;
    }
    float v[kCoefCount];
    for (size_t i = 0; i < kCoefCount; i++) {
        v[i] = strtof(parts[i].c_str(), nullptr);
    }
    applyCoefficients(v);
    return true;
}

// Single place that defines what each of the 7 channel coefficients means. The order is the
// one used by both the registry ("0".."6") and the panel config's "coef_data".
// ASSUMPTION (not confirmed by vendor docs): [coef0_a, coef1_a, coef0_b, coef1_b, ir_thr,
// dgf_a, dgf_b]. Validate against a reference lux meter and change only this function.
void LightCalibration::applyCoefficients(const float* v) {
    mCoef0A = v[0];
    mCoef1A = v[1];
    mCoef0B = v[2];
    mCoef1B = v[3];
    mIrThreshold = v[4];
    mDgfA = v[5];
    mDgfB = v[6];
}

LightCalibration::LightCalibration() {
    if (!loadLeakageTable(kCaliJson, "panel_Info_cali", &mLeakage)) {
        LOG(ERROR) << "no panel leakage table, light sensor will be uncompensated";
        return;
    }
    const std::string config = configForPanel();
    if (!loadLeakageTable(config, "panel_Info_cali_ori", &mFullWhite)) {
        LOG(WARNING) << "no full white leakage table, screen content will not be compensated";
    }
    loadCwbInfo(config);
    if (!loadCoefficients(kRegistryDir)) {
        LOG(WARNING) << "no ALS coefficients in the sensor registry, falling back to the "
                        "panel config";
        if (!loadConfigCoefficients(config)) {
            LOG(ERROR) << "no ALS coefficients anywhere, light sensor will not report";
            return;
        }
        mCoefSource = "panel config (scale not available, using 1)";
    }

    // lux per unit of net ALS for the common case (ir ~ 0). Refuse absurd values instead of
    // reporting millions of lux and pinning the display at maximum brightness.
    const float factor = mCoef0A / kLuxDivisor * mDgfA * mScale;
    if (!(mDgfA > 0.f && mDgfA < 100.f) || !(mDgfB > 0.f && mDgfB < 100.f) ||
        !(factor > 0.001f && factor < 1000.f)) {
        LOG(ERROR) << "implausible ALS coefficients from " << mCoefSource << ": coef_0_a=" << mCoef0A
                   << " dgf_a=" << mDgfA << " dgf_b=" << mDgfB << " scale=" << mScale
                   << " lux/unit=" << factor << ", light sensor will not report";
        return;
    }

    mValid = true;
    LOG(INFO) << "calibration loaded from " << mCoefSource << ": " << mLeakage.size()
              << " leakage rows, coef_0_a=" << mCoef0A << " coef_1_a=" << mCoef1A
              << " coef_0_b=" << mCoef0B << " coef_1_b=" << mCoef1B << " ir_thr=" << mIrThreshold
              << " dgf_a=" << mDgfA << " dgf_b=" << mDgfB << " scale=" << mScale
              << " lux/unit=" << factor;
}

bool LightCalibration::loadLeakageTable(const std::string& path, const std::string& key,
                                        std::vector<LeakageRow>* out) {
    std::string blob;
    if (!android::base::ReadFileToString(path, &blob)) {
        LOG(ERROR) << "failed to read " << path;
        return false;
    }

    size_t start = blob.find("\"" + key + "\"");
    if (start == std::string::npos) {
        return false;
    }
    size_t arr = blob.find('[', start);
    size_t end = blob.find(']', arr);
    if (arr == std::string::npos || end == std::string::npos) {
        return false;
    }

    for (size_t pos = arr; pos < end;) {
        size_t q1 = blob.find('"', pos);
        if (q1 == std::string::npos || q1 > end) {
            break;
        }
        size_t q2 = blob.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 > end) {
            break;
        }
        auto parts = android::base::Split(blob.substr(q1 + 1, q2 - q1 - 1), ",");
        if (parts.size() >= 3) {
            out->push_back({strtof(parts[0].c_str(), nullptr),
                            strtof(parts[1].c_str(), nullptr),
                            strtof(parts[2].c_str(), nullptr)});
        }
        pos = q2 + 1;
    }

    std::sort(out->begin(), out->end(),
              [](const LeakageRow& a, const LeakageRow& b) { return a.brightness < b.brightness; });
    return out->size() > 1;
}

void LightCalibration::loadCwbInfo(const std::string& path) {
    std::string blob;
    if (!android::base::ReadFileToString(path, &blob)) {
        return;
    }
    size_t start = blob.find("\"cwbInfo\"");
    if (start == std::string::npos) {
        return;
    }

    auto field = [&](const char* key) {
        std::vector<float> out;
        size_t k = blob.find(std::string("\"") + key + "\"", start);
        if (k == std::string::npos) {
            return out;
        }
        size_t q1 = blob.find('"', blob.find(':', k));
        size_t q2 = q1 == std::string::npos ? q1 : blob.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            return out;
        }
        for (const auto& part : android::base::Split(blob.substr(q1 + 1, q2 - q1 - 1), ",")) {
            out.push_back(strtof(part.c_str(), nullptr));
        }
        return out;
    };

    const auto centre = field("Center");
    const auto pow = field("PowPara");
    const auto radius = field("Radius");
    const size_t count = radius.size() >= 7 ? (radius.size() - 3) / 4 : 0;
    if (centre.size() < 2 || pow.size() < 2 || count == 0 ||
        radius.size() != count * 4 + count) {
        LOG(WARNING) << "cwbInfo is incomplete, screen content will not be compensated";
        return;
    }

    for (size_t i = 0; i < count; i++) {
        const float stride = radius[count * 4 + i];
        if (stride <= 0.f) {
            LOG(WARNING) << "cwbInfo region " << i << " has no stride";
            return;
        }
        mCwb.regions.push_back({static_cast<int32_t>(radius[i * 4 + 0]),
                                static_cast<int32_t>(radius[i * 4 + 1]),
                                static_cast<int32_t>(radius[i * 4 + 2]),
                                static_cast<int32_t>(radius[i * 4 + 3]),
                                1.f / (stride * stride * stride)});
    }

    mCwb.centreX = static_cast<int32_t>(centre[0]);
    mCwb.centreY = static_cast<int32_t>(centre[1]);
    mCwb.powLow = pow[0];
    mCwb.powHigh = pow[1];
    mCwb.valid = true;

    for (size_t i = 0; i < mCwb.regions.size(); i++) {
        const auto& r = mCwb.regions[i];
        LOG(INFO) << "cwbInfo region " << i << ": l" << r.left << " t" << r.top << " r" << r.right
                  << " b" << r.bottom << " weight " << r.weight;
    }
    LOG(INFO) << "cwbInfo: centre " << mCwb.centreX << "," << mCwb.centreY << " pow "
              << mCwb.powLow << ".." << mCwb.powHigh;
}

float LightCalibration::panelGamma(int32_t) const {
    return mCwb.valid ? mCwb.powHigh : 2.2f;
}

bool LightCalibration::loadCoefficients(const std::string& dir) {
    std::string blob;
    if (!android::base::ReadFileToString(dir + "/" + kCoefFile, &blob)) {
        LOG(WARNING) << "cannot read " << dir << "/" << kCoefFile;
        return false;
    }

    float v[kCoefCount];
    for (size_t i = 0; i < kCoefCount; i++) {
        if (!findFloat(blob, std::to_string(i), &v[i])) {
            LOG(ERROR) << kCoefFile << " is missing coefficient " << i;
            return false;
        }
    }

    float scale = 1.f;
    std::string facBlob;
    if (android::base::ReadFileToString(dir + "/" + kFacCalFile, &facBlob) &&
        findFloat(facBlob, "lux_scale", &scale)) {
        mScale = scale;
    } else {
        LOG(WARNING) << "no lux_scale in " << kFacCalFile << ", using " << mScale;
    }

    applyCoefficients(v);
    mCoefSource = "registry";
    return true;
}

float LightCalibration::interpolate(const std::vector<LeakageRow>& table, int32_t brightness,
                                    bool ir) const {
    if (table.empty()) {
        return 0.f;
    }

    const float dbv = static_cast<float>(brightness);
    const LeakageRow& first = table.front();
    const LeakageRow& last = table.back();
    if (dbv <= first.brightness) {
        return ir ? first.ir : first.als;
    }
    if (dbv >= last.brightness) {
        return ir ? last.ir : last.als;
    }

    for (size_t i = 1; i < table.size(); i++) {
        const LeakageRow& hi = table[i];
        if (dbv > hi.brightness) {
            continue;
        }
        const LeakageRow& lo = table[i - 1];
        const float span = hi.brightness - lo.brightness;
        if (span <= 0.f) {
            return ir ? lo.ir : lo.als;
        }
        const float t = (dbv - lo.brightness) / span;
        return ir ? lo.ir + (hi.ir - lo.ir) * t : lo.als + (hi.als - lo.als) * t;
    }
    return ir ? last.ir : last.als;
}

float LightCalibration::leakage(int32_t brightness, bool ir) const {
    const float content = mContentLevel.load();
    if (content >= 0.f && mFullWhite.size() > 1) {
        return interpolate(mFullWhite, brightness, ir) * content;
    }
    return interpolate(mLeakage, brightness, ir);
}

float LightCalibration::leakageAls(int32_t brightness) const {
    return leakage(brightness, false);
}

float LightCalibration::leakageIr(int32_t brightness) const {
    return leakage(brightness, true);
}

float LightCalibration::toLux(float als, float ir, int32_t brightness) const {
    if (!mValid) {
        return -1.f;
    }
    float alsNet = std::max(als - leakageAls(brightness), 0.f);
    float irNet = std::max(ir - leakageIr(brightness), 0.f);
    float ratio = als > 0.f ? (ir / als) : 0.f;
    bool useB = ratio > mIrThreshold;
    float coef0 = useB ? mCoef0B : mCoef0A;
    float coef1 = useB ? mCoef1B : mCoef1A;
    float dgf = useB ? mDgfB : mDgfA;
    float lux = (coef0 * alsNet + coef1 * irNet) / kLuxDivisor * dgf * mScale;
    return std::max(lux, 0.f);
}
