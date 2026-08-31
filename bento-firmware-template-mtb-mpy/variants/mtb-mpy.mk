# BENTO_VARIANT=mtb-mpy — ModusToolbox + MicroPython.
#
# This is what the template has always built. Everything here is ON, and the
# values are the ones proj_cm33_ns/Makefile and Makefile.micropython used
# before the variant split existed; moving them here changed no behaviour.

# The MicroPython VM, its port, the ~30 extension modules, the REPL and the
# TACP host link the BENTO IDE flashes through.
BENTO_HAS_MPY := 1

# Reachable from Python only, so they follow the VM.
#   TACP  — the IDE's file-push and program-mode protocol
#   REPL  — the UART console
BENTO_HAS_TACP := 1
BENTO_HAS_REPL := 1

# Storage. LittleFS is mounted by executing a Python source string
# (mpy_main.c), and tesaiot_config_store.c persists the broker, device id, TLS
# mode and WiFi credentials the same way. Five plain-C files read that config —
# mqtt_client_config.c, mqtt_task.c, subscriber_task.c, mqtt_mtls_setup.c and
# tesaiot_https.c — so in this variant the C side depends on the VM for
# configuration even though none of those files contains a MicroPython symbol.
BENTO_STORAGE := micropython-vfs

DEFINES += BENTO_HAS_MPY=1
