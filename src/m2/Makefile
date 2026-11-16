# M2 framework build (amiga-debug toolchain: m68k-amiga-elf-gcc + elf2hunk)

ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
endif

# ビルド用ディレクトリを作成 (フレッシュクローン対応)
ifdef WINDOWS
$(shell if not exist obj mkdir obj)
$(shell if not exist out mkdir out)
else
$(shell mkdir -p obj out)
endif

subdirs := $(wildcard */)
VPATH = $(subdirs)
cpp_sources := $(wildcard *.cpp) $(wildcard $(addsuffix *.cpp,$(subdirs)))
cpp_objects := $(addprefix obj/,$(patsubst %.cpp,%.o,$(notdir $(cpp_sources))))
c_sources := $(wildcard *.c) $(wildcard $(addsuffix *.c,$(subdirs)))
c_objects := $(addprefix obj/,$(patsubst %.c,%.o,$(notdir $(c_sources))))
s_sources := data/music_data.s
s_objects := $(addprefix obj/,$(patsubst %.s,%.o,$(notdir $(s_sources))))
vasm_sources := $(wildcard *.asm) $(wildcard $(addsuffix *.asm, $(subdirs)))
vasm_objects := $(addprefix obj/, $(patsubst %.asm,%.o,$(notdir $(vasm_sources))))
objects := $(cpp_objects) $(c_objects) $(s_objects) $(vasm_objects)

program = out/m2
OUT = $(program)
CC = m68k-amiga-elf-gcc
AS = m68k-amiga-elf-as
VASM = vasmm68k_mot

ifdef WINDOWS
	SDKDIR = $(abspath $(dir $(shell where $(CC)))..\m68k-amiga-elf\sys-include)
else
	SDKDIR = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include)
endif

CCFLAGS   = -g -m68020-60 -Ofast -fno-optimize-sibling-calls -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-tree-loop-distribution -flto -fno-exceptions
# 注: -ffunction-sections / -fdata-sections / -fwhole-program は使わない。
# メインループに曲切り替えのコードを足したところ、これらを付けたビルドだけが
# 起動直後に画面が真っ黒のまま止まった (外すと正常)。LDFLAGS の --gc-sections が
# 必要なコードを落としているようなので、無効化している。
CPPFLAGS  = $(CCFLAGS) -fno-rtti -fcoroutines -fno-use-cxa-atexit
ASFLAGS   = -mcpu=68020 -g --register-prefix-optional -I$(SDKDIR)
# -flto-partition=one: LTO を 1 パーティションで生成する。複数パーティション
# だと、別パーティションの関数への末尾呼び出し (bra.l) が R_68K_PC32 の
# リロケーションとして残り、elf2hunk がそれを絶対リロケーション (HUNK_RELOC32)
# として出力する。ロード時に PC 相対の変位へコード先頭番地が足され、
# 飛び先が BSS になって落ちる (2026-11-14 WinUAE デバッガで確認:
# render_phase_a の bra.l write_palette が -$22364 + $21E228 = +$1FBEC4)。
# 上の「コードを足すと真っ黒で止まる」も同じ原因と思われる。
LDFLAGS   = -flto-partition=one -Wl,--emit-relocs,--gc-sections,-Ttext=0,-Map=$(OUT).map
VASMFLAGS = -m68020 -Felf -opt-fconst -nowarn=62 -dwarf=3 -quiet -x -I. -I$(SDKDIR)

all: $(OUT).exe

# 最後に、Amiga の CF カード (既定 G:iles、環境変数 AMIGA_CF_DIR) があれば
# 変わったファイルだけコピーする (run/deploy-cf.ps1)。
$(OUT).exe: $(OUT).elf
	$(info Elf2Hunk $(program).exe)
	@python $(CURDIR)/../../tools/check_pcrel.py $(OUT).elf
	@elf2hunk $(OUT).elf $(OUT).exe
	@python $(CURDIR)/../../tools/hunk_fast.py $(OUT).exe
	-@powershell -NoProfile -ExecutionPolicy Bypass -File $(CURDIR)/../../run/deploy-cf.ps1

$(OUT).elf: $(objects)
	$(info Linking $(program).elf)
	@$(CC) $(CCFLAGS) $(LDFLAGS) $(objects) -o $@
	@m68k-amiga-elf-objdump --disassemble --no-show-raw-ins --visualize-jumps -S $@ >$(OUT).s

clean:
	$(info Cleaning...)
ifdef WINDOWS
	@del /q obj\* out\*
else
	@$(RM) obj/* out/*
endif

$(cpp_objects) : obj/%.o : %.cpp
	$(info Compiling $<)
	@$(CC) $(CPPFLAGS) -c -o $@ $(CURDIR)/$<

$(c_objects) : obj/%.o : %.c
	$(info Compiling $<)
	@$(CC) $(CCFLAGS) -c -o $@ $(CURDIR)/$<

$(s_objects): obj/%.o : %.s
	$(info Assembling $<)
	@$(AS) $(ASFLAGS) -o $@ $(CURDIR)/$<

$(vasm_objects): obj/%.o : %.asm
	$(info Assembling $<)
	@$(VASM) $(VASMFLAGS) -o $@ $(CURDIR)/$<
