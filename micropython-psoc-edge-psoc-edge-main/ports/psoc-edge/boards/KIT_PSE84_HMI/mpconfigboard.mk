BOARD_VERSION=release-v1.1.0

# QSPI block device OFF on this board — known open defect, 2026-08-28.
# The gate is this make variable, not the C macro in mpconfigboard.h: it both
# compiles psoc_edge_qspi_flash.c and defines MICROPY_ENABLE_EXT_QSPI_FLASH,
# which is what guards the QSPI_Flash entry in modpsocedge.c.
# With it at 1 the board hangs before the MicroPython banner, spinning inside
# Cy_SMIF_ReceiveDataBlocking_Ext. See the comment block in mpconfigboard.h.
MICROPY_PY_EXT_FLASH = 1
