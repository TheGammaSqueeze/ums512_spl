
PLATFORM_RELFLAGS += -fno-common -ffixed-r8
#-msoft-float

# Make ARMv5 to allow more compilers to work, even though its v7a.
PF_CPPFLAGS_ARMV7 := $(call cc-option, -march=armv7-a, -march=armv8-a)
PLATFORM_CPPFLAGS += $(PF_CPPFLAGS_ARMV7)
# =========================================================================
#
# Supply options according to compiler version
#
# =========================================================================
PLATFORM_RELFLAGS +=$(call cc-option,-mshort-load-bytes,\
		    $(call cc-option,-malignment-traps,))

# AArch64 gcc does not recognize -mno-unaligned-access (that is the ARM32 spelling);
# cc-option silently drops it, so the compiler was free to emit unaligned stur/ldur.
# The SPL runs with no MMU, where an unaligned access data-aborts (this hung the
# eMMC CMD2 R2 response copy in SDHOST_GetRspFromBuf). -mstrict-align forces the
# compiler to never generate unaligned accesses. Keep the ARM32 spelling as fallback.
PLATFORM_RELFLAGS +=$(call cc-option,-mstrict-align,$(call cc-option,-mno-unaligned-access))
PLATFORM_RELFLAGS += $(call cc-option, -msoft-float)
