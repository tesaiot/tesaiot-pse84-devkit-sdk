/*
 * fw_identity_ld.inc — linker fragment for the .fw_identity record (contract §2.1, spec 01 §3).
 *
 * This is DOCUMENTATION / copy-source. Because CM55 non-secure linker scripts are byte-identical
 * across the BENTO firmware projects, the same fragment is pasted into each project's
 *   bsps/TARGET_*/COMPONENT_CM55/TOOLCHAIN_{GCC_ARM,LLVM_ARM}/pse84_ns_cm55.ld
 * immediately AFTER the ".mcu_boot_header" output section and BEFORE ".app_code_main".
 *
 * Placement: base(m55_nvm_sel) + MCUBOOT_HEADER_SIZE(0x400) = 0x60580400 (derived, not hardcoded).
 *
 * IMPORTANT (deviates from contract §2.1 "vectors at +0x500"): ".app_code_main" keeps
 * ALIGN(VECTORS_ALIGNMENT=0x400). The 256-byte record ends the location counter at +0x500, and
 * ALIGN(0x400) then places __Vectors at base+0x800 (NOT +0x500). This is REQUIRED — a CM55 vector
 * table must be >=0x400-aligned for VTOR, so a +0x500 table would HardFault. The identity stays at
 * +0x400 (what readers use); vectors move from +0x400 to +0x800. Report to contract owners.
 */

    /* --- paste AFTER `.mcu_boot_header (...) > m55_nvm_sel` --- */
    .fw_identity ORIGIN(m55_nvm_sel) + MCUBOOT_HEADER_SIZE (READONLY) :
    {
        __fw_identity_start = .;
        KEEP(*(.fw_identity))
        . = __fw_identity_start + 0x100;   /* reserve exactly 256 bytes */
        __fw_identity_end = .;
    } > m55_nvm_sel
    /* `.app_code_main : ALIGN(VECTORS_ALIGNMENT)` follows — __Vectors now at base+0x800 */
