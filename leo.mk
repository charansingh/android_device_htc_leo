#
# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# This is the product configuration for a generic GSM leo,
# not specialized for any geography.
#

# The gps config appropriate for this device
PRODUCT_COPY_FILES += \
    device/htc/leo/gps.conf:system/etc/gps.conf

## (1) First, the most specific values, i.e. the aspects that are specific to GSM

PRODUCT_COPY_FILES += \
    device/htc/leo/init.leo.rc:root/init.leo.rc \

PRODUCT_PROPERTY_OVERRIDES += \
    ro.com.google.clientidbase=android-tmobile-us \
    ro.com.google.clientidbase.vs=android-hms-tmobile-us \
    ro.com.google.clientidbase.ms=android-hms-tmobile-us \
    ro.com.google.locationfeatures=1 \
    ro.com.google.networklocation=1 \
    ro.com.google.gmsversion=2.2_r6 \
    ro.setupwizard.enable_bypass=1 \
    dalvik.vm.lockprof.threshold=500 \
    dalvik.vm.dexopt-flags=m=y
    ro.media.dec.jpeg.memcap=20000000 \
    dalvik.vm.lockprof.threshold=500 \
    dalvik.vm.dexopt-flags=m=y \
    ro.opengles.version=131072


# Default network type.
# 0 => WCDMA preferred.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.telephony.default_network=0

# For HSDPA low throughput
PRODUCT_PROPERTY_OVERRIDES += \
    ro.ril.disable.power.collapse = 1

# For PDP overlap problem
PRODUCT_PROPERTY_OVERRIDES += \
    ro.ril.avoid.pdp.overlap = 1

# we have enough storage space to hold precise GC data
PRODUCT_TAGS += dalvik.gc.type-precise

# Set default_france.acdb to audio_ctl driver if the ro.cid is HTC__203
PRODUCT_PROPERTY_OVERRIDES += \
    ro.ril.enable.prl.recognition = 0


# This is a high density device with more memory, so larger vm heaps for it.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapsize=32m

# leo have huge 250Mb unwritable system and small 50Mb cache .
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.dexopt-data-only=1

## (2) Also get non-open-source GSM-specific aspects if available
$(call inherit-product-if-exists, vendor/htc/leo/leo-vendor.mk)


DEVICE_PACKAGE_OVERLAYS += device/htc/leo/overlay

PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.xml

# media config xml file
PRODUCT_COPY_FILES += \
    device/htc/leo/media_profiles.xml:system/etc/media_profiles.xml

PRODUCT_PACKAGES += \
    sensors.leo \
    lights.leo \
    librs_jni \
    gralloc.qsd8k \
    copybit.qsd8k \
    gps.leo \
    libOmxCore \
    libOmxVidEnc



# leo uses high-density artwork where available
PRODUCT_LOCALES := hdpi

PRODUCT_COPY_FILES += \
    device/htc/leo/vold.fstab:system/etc/vold.fstab

# Keylayouts
PRODUCT_COPY_FILES += \
    device/htc/leo/leo-keypad.kl:system/usr/keylayout/leo-keypad.kl \
    device/htc/leo/leo-keypad.kcm.bin:system/usr/keychars/leo-keypad.kcm.bin \
    device/htc/leo/h2w_headset.kl:system/usr/keylayout/h2w_headset.kl

# Firmware
PRODUCT_COPY_FILES += \
    device/htc/leo/firmware/fw_bcm4329.bin:system/etc/firmware/fw_bcm4329.bin \
    device/htc/leo/firmware/fw_bcm4329_apsta.bin:system/etc/firmware/fw_bcm4329_apsta.bin


ifeq ($(TARGET_PREBUILT_KERNEL),)
LOCAL_KERNEL := device/htc/leo/kernel
else
LOCAL_KERNEL := $(TARGET_PREBUILT_KERNEL)
endif

PRODUCT_COPY_FILES += \
    $(LOCAL_KERNEL):kernel

PRODUCT_COPY_FILES += \
    device/htc/leo/modules/bcm4329.ko:system/lib/modules/bcm4329.ko \

# media profiles and capabilities spec
$(call inherit-product, device/htc/leo/media_a1026.mk)

# stuff common to all HTC phones
$(call inherit-product, device/htc/common/common.mk)

PRODUCT_NAME := htc_leo
PRODUCT_DEVICE := leo
