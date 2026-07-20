#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

TARGET_SCREEN_WIDTH := 1080

# Inherit some common PixelOS stuff.
$(call inherit-product, vendor/custom/config/common_full_phone.mk)

# Inherit from kunzite device
$(call inherit-product, device/xiaomi/kunzite/device.mk)

PRODUCT_NAME := custom_kunzite
PRODUCT_DEVICE := kunzite
PRODUCT_MANUFACTURER := Xiaomi
PRODUCT_BRAND := Redmi
PRODUCT_MODEL := 25098RA98G

PRODUCT_SYSTEM_NAME := kunzite_global
PRODUCT_SYSTEM_DEVICE := kunzite

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="kunzite_global-user 15 AQ3A.250129.001 OS3.0.301.0.WPQMIXM release-keys" \
    BuildFingerprint=Redmi/kunzite_global/kunzite:15/AQ3A.250129.001/OS3.0.301.0.WPQMIXM:user/release-keys \
    DeviceName=$(PRODUCT_SYSTEM_DEVICE) \
    DeviceProduct=$(PRODUCT_SYSTEM_NAME)

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi
