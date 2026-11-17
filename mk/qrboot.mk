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

# The loader in a browser (tools/qrboot/web), with the app's printable codes and test frames beside it.
# python3 tools/qrboot/serve.py, then open http://127.0.0.1:8000/
QRBOOT_WEB := build/qrboot/web
QRBOOT_WEB_QR := build/qrboot/web-qr/wasm32-unknown-unknown/release/qr_reader.wasm
$(QRBOOT_WEB_QR): src/runtime/qr_reader/src/lib.rs src/runtime/qr_reader/Cargo.lock
	cargo rustc --release --locked --no-default-features --manifest-path src/runtime/qr_reader/Cargo.toml \
	  --target wasm32-unknown-unknown --target-dir build/qrboot/web-qr --crate-type cdylib

qrboot-web: build/qrboot/app.wasm $(QRBOOT_WEB_QR) $(wildcard tools/qrboot/web/*) build/psbt/own_mixed_nwu.ur
	mkdir -p $(QRBOOT_WEB)
	cp tools/qrboot/web/* $(QRBOOT_WEB)/ && cp $(QRBOOT_WEB_QR) $(QRBOOT_WEB)/qr_reader.wasm
	python3 tools/qrboot/codes.py pack build/qrboot/app.wasm $(QRBOOT_WEB)/codes
	echo 000000000000000000000000000000000000000000000003 > build/qrboot/seed.txt
	python3 tools/qrboot/codes.py text build/qrboot/seed.txt $(QRBOOT_WEB)/test/seedqr
	python3 tools/qrboot/codes.py text build/psbt/own_mixed_nwu.ur $(QRBOOT_WEB)/test/psbt
.PHONY: qrboot-web
