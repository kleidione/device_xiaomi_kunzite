#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2025-2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.fixups_blob import (
    BlobFixupCtx,
    File,
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

from extract_utils.tools import (
    llvm_objdump_path,
)
from extract_utils.utils import (
    run_cmd,
)

namespace_imports = [
    'device/xiaomi/kunzite',
    'hardware/xiaomi',
    'hardware/qcom-caf/sm8450-6.6',
    'hardware/qcom-caf/wlan',
    'vendor/qcom/opensource/commonsys/display',
    'vendor/qcom/opensource/commonsys-intf/display',
    'vendor/qcom/opensource/dataservices',
]

def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None

lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    (
        'vendor.qti.ImsRtpService-V1-ndk',
        'vendor.qti.diaghal-V1-ndk',
        'vendor.qti.diaghal@1.0.so',
        'vendor.qti.hardware.wifidisplaysession_aidl-V1-ndk',
        'vendor.qti.ims.uceaidlservice-V1-ndk',
        'vendor.qti.qccsyshal_aidl-V1-ndk',
        'vendor.qti.qccvndhal_aidl-V1-ndk',
    ): lib_fixup_vendor_suffix,
}

def blob_fixup_graphic_buffer_size(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    disassemble_symbols: [str],
    *args,
    **kwargs,
):
    for line in run_cmd(
        [
            llvm_objdump_path,
            f'--disassemble-symbols={",".join(disassemble_symbols)}',
            file_path,
        ]
    ).splitlines():
        line = line.split(maxsplit=5)
        if len(line) != 6:
            continue

        # The size of GraphicBuffer changed from 0x100 to 0xd30
        offset, _, instruction, register, value, _ = line
        if instruction == 'mov' and register[:-1] == 'w0' and value == '#0x100':
            with open(file_path, 'rb+') as f:
                f.seek(int(offset[:-1], 16))
                f.write(b'\x00\xa6\x81\x52')  # AArch64 mov w0, #0xd30

blob_fixups: blob_fixups_user_type = {
    'system/framework/WfdCommon.jar': blob_fixup()
        .apktool_patch('blob-patches/WfdCommon.patch'),
    'system_ext/lib64/libwfdservice.so': blob_fixup()
        .replace_needed('android.media.audio.common.types-V4-cpp.so', 'android.media.audio.common.types-V5-cpp.so'),
    'vendor/lib64/vendor.qti.hardware.camera.postproc@1.0-service-impl.so': blob_fixup()
        .replace_needed('android.hardware.graphics.allocator-V1-ndk.so', 'android.hardware.graphics.allocator-V2-ndk.so')
        .replace_needed('android.hardware.camera.common-V2-ndk.so', 'android.hardware.camera.common-V1-ndk.so'),
    'vendor/lib64/libcameraopt.so': blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    (
        'odm/bin/hw/vendor.xiaomi.hw.touchfeature-service',
        'vendor/lib64/libsensorndkbridge.so',
    ): blob_fixup()
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    (
        'vendor/bin/hw/vendor.qti.camera.provider-service_64',
        'vendor/lib64/camx.provider-impl.so',
        'vendor/lib64/hw/libaudioeffecthal.qti.so',
        'vendor/lib64/libaudiocloudctrl.so',
        'vendor/lib64/libdpps.so',
        'vendor/lib64/libsnapdragoncolor-manager.so',
    ): blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/etc/media_codecs_parrot_v0.xml',
        'vendor/etc/media_codecs_parrot_v1.xml',
        'vendor/etc/media_codecs_parrot_v2.xml',
    ): blob_fixup()
        .regex_replace('.+media_codecs_(google_audio|google_c2|google_telephony|vendor_audio).+\n', ''),
    'vendor/etc/seccomp_policy/gnss@2.0-qsap-location.policy': blob_fixup()
        .add_line_if_missing('sched_get_priority_min: 1')
        .add_line_if_missing('sched_get_priority_max: 1'),
    (
        'vendor/bin/chimcxtest',
        'vendor/bin/chiofflinepostproctest',
        'vendor/lib64/camera/components/com.qti.node.dewarp.so',
        'vendor/lib64/hw/com.qti.chi.override.so',
        'vendor/lib64/libcamximageformatutils.so',
    ): blob_fixup()
        .replace_needed('android.hardware.graphics.allocator-V1-ndk.so', 'android.hardware.graphics.allocator-V2-ndk.so'),
    'vendor/lib64/android.hardware.bluetooth.audio-impl_prebuilt.so': blob_fixup()
        .replace_needed('libbluetooth_audio_session_aidl.so', 'libbluetooth_audio_session_aidl_prebuilt.so'),
    'vendor/lib64/libaudioserviceexampleimpl.so': blob_fixup()
        .add_needed('libaudioutils_shim.so')
        .replace_needed('android.hardware.bluetooth.audio-impl.so', 'android.hardware.bluetooth.audio-impl_prebuilt.so')
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so')
        .replace_needed('libbluetooth_audio_session_aidl.so', 'libbluetooth_audio_session_aidl_prebuilt.so'),
    'vendor/lib64/libcameraopt.so': blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    (
        'vendor/lib64/libMiPhotoFilter.so',
        'vendor/lib64/libTrueSight.so',
        'vendor/lib64/libmorpho_Ldc.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_acquire')
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_isSupported')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_lockPlanes')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock'),
    'vendor/lib64/camera/components/com.mi.node.tsskinbeautifier.so': blob_fixup()
        .call(
            blob_fixup_graphic_buffer_size,
            [
                'ChiNodeEntry',
            ],
        ),
    'vendor/lib64/libqcodec2_core.so': blob_fixup()
        .add_needed('libcodec2_shim.so')
        .replace_needed('android.hardware.graphics.common-V5-ndk.so', 'android.hardware.graphics.common-V7-ndk.so'),
    (
         'vendor/lib64/soundfx/libhwdapaidl.so',
         'vendor/lib64/soundfx/libswgamedapaidl.so',
    ): blob_fixup()
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so'),
    'vendor/lib64/libwfdmmsrc_proprietary.so': blob_fixup()
        .replace_needed('android.media.audio.common.types-V2-ndk.so', 'android.media.audio.common.types-V3-ndk.so'),
    'vendor/lib64/libqtigefar.so': blob_fixup()
        .replace_needed('android.hardware.audio.core-V1-ndk.so', 'android.hardware.audio.core-V2-ndk.so'),
} # fmt: skip

module = ExtractUtilsModule(
    'kunzite',
    'xiaomi',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
