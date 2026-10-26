arm_ss = ss.source_set()
arm_common_ss = ss.source_set()
arm_common_system_ss = ss.source_set()
arm_system_ss = ss.source_set()
arm_user_ss = ss.source_set()

# CHERI: upstream builds most of these once in arm_common_*_ss, but TARGET_CHERI
# (morello) changes CPUARMState and the helpers, so anything that includes
# target/arm/cpu.h has to stay per target.
arm_ss.add(files(
  'cpu.c',
  'cpregs-gcs.c',
  'cpregs-pmu.c',
  'debug_helper.c',
  'gdbstub.c',
  'helper.c',
  'mmuidx.c',
  'vfp_fpscr.c',
))

arm_ss.add(when: 'TARGET_AARCH64', if_true: files(
  'cpu64.c',
  'gdbstub64.c',
), if_false: files(
  'cpu32-stubs.c',
))

arm_ss.add(when: 'TARGET_CHERI', if_true: files(
  'cheri_helper.c'
))

arm_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING',
           if_true: files('common-semi-target.c'))

arm_system_ss.add(files(
  'arch_dump.c',
  'arm-powerctl.c',
  'arm-qmp-cmds.c',
  'cortex-regs.c',
  'cpu-irq.c',
  'machine.c',
  'ptw.c',
), capstone)

arm_system_ss.add(when: 'CONFIG_KVM', if_true: files('hyp_gdbstub.c', 'kvm.c'), if_false: files('kvm-stub.c'))
arm_system_ss.add(when: 'CONFIG_HVF', if_true: files('hyp_gdbstub.c'))

arm_user_ss.add(files(
  'el2-stubs.c',
))

subdir('hvf')
subdir('whpx')

if 'CONFIG_TCG' in config_all_accel
   subdir('tcg')
else
    arm_common_system_ss.add(files('tcg-stubs.c'))
endif

target_arch += {'arm': arm_ss}
target_system_arch += {'arm': arm_system_ss}
target_user_arch += {'arm': arm_user_ss}
target_common_arch += {'arm': arm_common_ss}
target_common_system_arch += {'arm': arm_common_system_ss}
