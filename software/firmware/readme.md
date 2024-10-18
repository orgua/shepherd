# PRU-Firmware

**Main Documentation**: <https://orgua.github.io/shepherd>

**Source Code**: <https://github.com/orgua/shepherd/tree/main/software/firmware>

---

The directory currently contains:

- shepherd-firmware (pru0 & pru1) for emulation & harvest
- programmer swd (pru0)
- programmer sbw (pru0)

Compiling can be done with GCC or CGT, but GCC is experimental for now. The makefile supports the following targets:

- clean,
- all,
- install.

The makefile will decide which compiler to use depending on the env-variables defined. CGT will always be the fallback if `PRU_CGT` is not defined.

General use:

```Shell
cd shepherd/software/firmware/pru0-shepherd-fw
make clean
make
sudo make install
```

Generate the two programmers (SWD is default):

```Shell
cd shepherd/software/firmware/pru0-programmer
make clean
make
# equals 'make TYPE=SWD'

make TYPE=SBW
sudo make install
```

## Debug via GPIO

The firmwares use GPIO to signal their states. Additionally, the Chip-Select of PRU0 is helpful.

- Pru0 debug pin0 = P8_12, pin1 = P8_11
- Pru1 debug pin0 = P8_28, pin1 = P8_30
- Pru0 spi-cs = P9_25

## Install CGT

- corresponding ansible-playbook -> `shepherd/deploy/roles/sheep/task/toolchain_pru_ti.yml`
- install [CGT](https://www.ti.com/tool/PRU-CGT#downloads)
- clone [PSSP](https://git.ti.com/cgit/pru-software-support-package/pru-software-support-package/)
  - make sure to use `v5.9.0`
  - Note: >= `v6.0.0` is reserved for kernel >=5.10 and needs a intc_map.h
- setup env-variables, like below

```Shell
# Desktop Linux
export PRU_CGT=/path/to/pru/code/gen/tools/ti-cgt-pru_#Version
export PRU_CGT_SUPPORT=/path/to/ti/pru-software-support-package-#Version
# Windows
set PRU_CGT=C:/path/to/pru/code/gen/tools/ti-cgt-pru_#Version
set PRU_CGT_SUPPORT=C:/path/to/ti/pru-software-support-package-#Version
# ARM Linux
export PRU_CGT=/usr/share/ti/cgt-pru
export PRU_CGT_SUPPORT=/usr/share/ti/pru-software-support-package
```

## Install GCC-Port

- corresponding ansible-playbook -> `shepherd/deploy/roles/sheep/task/toolchain_pru_gcc.yml`
- install the cross toolchain from [gnupru](https://github.com/dinuxbg/gnupru.git)
- install the PRU software support packages from [pssp](https://github.com/dinuxbg/pru-software-support-package.git)
  - checkout branch `linux-4.19-rproc`
- setup env variables, like below

```Shell
# ARM Linux
export PRU_CGT=/usr/share/shepherd-tools/cgt-pru
export PRU_CGT_SUPPORT=/usr/share/shepherd-tools/pru-software-support-package
```

## Differences CGT vs GCC

Challenges while porting firmware to also be compatible with GCC.

### Assembly (solved)

- file-ending is different
	- CGT: `.asm`
	- GCC: `.s`
- setting assembly-constants differs from ti compiler (CGT)
	- CGT: `VAR .set value`
	- GCC: `.equ VAR, value`
- fix is to use [+x with gcc](https://gcc.gnu.org/onlinedocs/gcc/Overall-Options.html)
	-  encapsulation `-x assembler SRCASM -x none` when loading the asm-sources into the compiler

### Multiplication (solved)

- pru can only multiply with [register-magic](https://github.com/dinuxbg/gnupru/wiki/Multiplication)
- current code may use loops instead of this magic
- ~~we probably need an asm-version for `mul32()`~~ (with overflow safety, like the c-version)

### Overflow of program memory (mostly solved)

- pru1-code compiles, but pru0 fails with ~ 3 kB overflow of program memory (8 kB)
- disabling debug-symbols (`-g0`) does not change program memory, but size of elf-file gets reduced significantly
	- pru1 fw size shrinks from > 40 kB to < 8 kB
- link-time-optimization helps somewhat (`-flto`, `-fuse-linker-plugin`)
	- pru1 fw size shrinks from 7.38 kB to 6.30 kB
	- pru0 overflow reduces from 3060 byte to 2690 byte
- disabling some debug code had just minimal change (-20 byte)
- did read through gnupru github-issues, but found no clue
- did read through large parts of gcc v12.1 doc (gcc.pdf) with no luck
- enabling `-ffast-math` does nothing to our code-size (should only help with float-ops)
- compiling code with `uint32`-only (replaced `uint64`) works
- compiling u32 with `-fno-inline` overflows by 300 byte -> clean out minor FNs to allow compiling
- compiling original code (u64) with `-fno-inline` without size-hack reduces overflow from 2672 to 2276 bytes.
	- is this a self-made inline-fuckup?
	- removing `inline` from our codebase brings overflow back to 2672 bytes
	- that's strange
- comparing functions-size between source with u64 and u32-mod
  - converter_xyz-Fns grow by factor 1.6 to 2.4
- **issue report confirmed at least 2 gcc-bugs**
- **possible partial solution: divide codebase into the two subsystems.**
  - but timing-constraints were tough already. Probably GCC won't help us here for now. But we keep this solution in our sight.

[more details](https://github.com/orgua/shepherd/blob/main/software/firmware/readme_overflow_issue.md)

### Optional

- `-DPRU0` could be replaced, as [gcc defines](https://github.com/dinuxbg/gnuprumcu/blob/HEAD/device-specs/am335x.pru0) something like `-D__AM335X_PRU0__` -> should be compatible with GCT
