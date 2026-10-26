# Default configuration for morello-softmmu

INCLUDE_WORKAROUND=aarch64-softmmu.mak
#
# CHERI-specific settings:
#
# Same as aarch64-softmmu.mak but with the extra aarch64-cheri.xml
TARGET_XML_FILES= aarch64-core.xml aarch64-fpu.xml arm-core.xml arm-vfp.xml arm-vfp3.xml arm-neon.xml arm-m-profile.xml aarch64-capability.xml
TARGET_CHERI=y
TARGET_MORELLO=y
TARGET_SUPPORTS_MTTCG=n
