# BENTO_VARIANT=mtb-only — ModusToolbox only. Plain C on FreeRTOS, no VM.
#
# What works, verified by linking the CM33_NS objects without the MicroPython
# archives and reading the undefined list — not by reading source and judging:
#   - CM55 entire image. Zero MicroPython symbols in 1,448 translation units.
#   - WiFi association, and saved networks: /.wifi_creds is read at boot and
#     written at save time through storage_c — the mpy variant defers the
#     write to a REPL-idle flusher that does not exist here, so this variant
#     writes immediately from the WiFi worker task. Byte-compatible both
#     directions, including the legacy XOR-32 checksum on read.
#   - Config persistence: /.tesaiot_config through the same parser and
#     serialiser, with the two I/O blocks compiled against bento_storage.
#   - The MQTT session: tesaiot_mqtt.c compiles as an ordinary source here.
#   - OPTIGA CSR and Protected Update. libbento_hsm.a needs zero MPY symbols.
#   - CM33<->CM55 IPC: main() calls cm33_ipc_communication_setup() itself,
#     because sensor_auto_task_create() was the only boot-path caller and the
#     HSM/TESAIoT handlers RegisterCallback without setting the pipe up.
#
# What is lost by design: the REPL, the TACP host link (so the BENTO IDE cannot
# flash this variant), /boot.py and /main.py, and every Python module.
#
# One deliberate behavioural difference: a LittleFS volume that fails to mount
# is REPORTED and left untouched. The mpy variant formats on any mount error
# (vfs_mount_script's bare except), which recovers transient faults by wiping
# /main.py and the config. On a board people work on, the wipe is the worse
# failure; bento_storage_format() exists for when erasing is meant.

BENTO_HAS_MPY  := 0
BENTO_HAS_TACP := 0
BENTO_HAS_REPL := 0
BENTO_STORAGE  := c-native

DEFINES += BENTO_HAS_MPY=0

# littlefs is vendored at bento_libs/claw/common/storage_c/littlefs — pristine
# upstream v2.11, the same revision the MicroPython port builds. NO_MALLOC
# because every buffer is supplied statically; a code path that would need the
# heap fails to compile instead of quietly allocating.
DEFINES += LFS2_NO_MALLOC LFS2_NO_DEBUG LFS2_NO_WARN LFS2_NO_ERROR LFS2_NO_ASSERT

# Accepted for compatibility with scripts written while this variant refused
# to build; it no longer changes anything.
BENTO_ALLOW_INCOMPLETE_MTB_ONLY ?=
