# The firmware's main.c and UI as one wasm app a QR boot loader runs (src/qrboot). The parser is linked
# in, and the board comes from the loader's imports
QRBOOT_TEST_SEED ?= 1
QRBOOT_SRC := src/main.c src/ui/ui.c $(QRGEN)/qrcodegen.c $(CORE_SRC) components/parts/signer/secp256k1_unity.c \
  components/parts/parser/c/src/psbt.c components/parts/parser/c/src/ur.c \
  src/qrboot/hal.c src/qrboot/qr_reader.c src/qrboot/parser_direct.c

build/qrboot/app.wasm: $(QRBOOT_SRC) src/qrboot/qrboot.h build/font8x16.h build/test_psbt.h
	mkdir -p build/qrboot
	$(LLVM)/clang --target=wasm32-wasip1 --sysroot=$(WASI) -nostartfiles -nodefaultlibs -fno-builtin-printf -fno-builtin-puts -fno-builtin-putchar \
	  -Oz -Wall -Wno-unused-function -DNDEBUG $(LIME_FLAGS) -DQRBOOT_APP=1 -DNO_LCD=0 -DTESTNET=0 \
	  -DTEST_SEED=$(QRBOOT_TEST_SEED) $(SECP_DEFS) -Iweb -Isrc/qrboot -Isrc/drivers -Isrc/runtime -Isrc/ui -Ibuild \
	  -Icomponents/parts/signer -Icomponents/parts/parser/c/include -I$(QRGEN) -I$(SECP)/include -I$(SECP)/src \
	  -Wl,--no-entry -Wl,--export=main -Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=65536 \
	  --no-wasm-opt -Wl,--keep-section=target_features \
	  -o $@ $(QRBOOT_SRC) -lc $(RTLIB)/libclang_rt.builtins.a
	$(WASM_OPT) $@ -Oz -o $@
	@shasum -a 256 $@

# The Mac loader that runs it: WAMR's classic interpreter, the firmware's QR reader built for the host
QRBOOT_HOST_DIR := build/qrboot/host
$(QRBOOT_HOST_DIR)/wamr/libiwasm.a:
	cmake -S third_party/wasm-micro-runtime/product-mini/platforms/darwin -B $(QRBOOT_HOST_DIR)/wamr -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_AOT=0 \
	  -DWAMR_BUILD_JIT=0 -DWAMR_BUILD_SIMD=0 -DWAMR_BUILD_LIBC_BUILTIN=0 -DWAMR_BUILD_LIBC_WASI=0 >/dev/null
	ninja -C $(QRBOOT_HOST_DIR)/wamr iwasm_static

$(QRBOOT_HOST_DIR)/release/libqr_reader.a: src/runtime/qr_reader/src/lib.rs src/runtime/qr_reader/Cargo.lock
	cargo build --release --locked --no-default-features --manifest-path src/runtime/qr_reader/Cargo.toml --target-dir $(QRBOOT_HOST_DIR)

$(QRBOOT_HOST_DIR)/host: tools/qrboot/host.c $(QRBOOT_HOST_DIR)/wamr/libiwasm.a $(QRBOOT_HOST_DIR)/release/libqr_reader.a
	cc -O2 -Wall -Wextra -Ithird_party/wasm-micro-runtime/core/iwasm/include -o $@ $< \
	  $(QRBOOT_HOST_DIR)/wamr/libiwasm.a $(QRBOOT_HOST_DIR)/release/libqr_reader.a -lz -lm -lpthread
