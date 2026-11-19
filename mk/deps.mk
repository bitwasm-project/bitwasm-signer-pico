RISCV_TC_REL := https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0
ifeq ($(shell uname -s),Darwin)
RISCV_TC_PKG := riscv-toolchain-16-mac.zip
RISCV_TC_SUM := 186538414857e31012aaa41f5f7ecfdee5bbd42fe3f4ec047232ac06d35a24c1
else
RISCV_TC_PKG := riscv-toolchain-16-x86_64-lin.tar.gz
RISCV_TC_SUM := fc36f1b37f99de54a358115443638023de34e15fe199e718e50cca4d212e7e9f
endif

# The Arm GNU toolchain for the original Pico (RP2040), only fetched by make deps-rp2040
ARM_TC_REL := https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel
ifeq ($(shell uname -s)-$(shell uname -m),Darwin-arm64)
ARM_TC_HOST := darwin-arm64
ARM_TC_SUM  := c7c78ffab9bebfce91d99d3c24da6bf4b81c01e16cf551eb2ff9f25b9e0a3818
else ifeq ($(shell uname -s),Darwin)
ARM_TC_HOST := darwin-x86_64
ARM_TC_SUM  := 2d9e717dd4f7751d18936ae1365d25916534105ebcb7583039eff1092b824505
else ifeq ($(shell uname -m),aarch64)
ARM_TC_HOST := aarch64
ARM_TC_SUM  := 87330bab085dd8749d4ed0ad633674b9dc48b237b61069e3b481abd364d0a684
else
ARM_TC_HOST := x86_64
ARM_TC_SUM  := 62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823
endif
ARM_TC_PKG := arm-gnu-toolchain-14.2.rel1-$(ARM_TC_HOST)-arm-none-eabi.tar.xz

# Pin every dependency to a commit, especially code that handles keys; never track the tip of a branch.
# Review the diff before updating a pin.
SECP_REV   := 46db787112beabdb5e17e0dc35680716f1057e7b
WAMR_REV   := f5f57c09aee623436f5fb87a90798fdd2cdf39fd
PICO_REV   := 079c6f39023649b154152db30f1d781e884879bc
QRGEN_REV  := 3c6d0b3cefb4e049dc337e82237c9644399716a8
SPLEEN_REV := 57f9219328c9f5873085320fe8bc8f7dd34b8791

# $(1) destination, $(2) URL, $(3) commit
define clone_at
	git clone --filter=blob:none $(2) third_party/$(1)
	cd third_party/$(1) && git checkout --detach $(3)
endef

deps:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,pico-sdk,https://github.com/raspberrypi/pico-sdk.git,$(PICO_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
	cd third_party/pico-sdk && git submodule update --init --depth 1 lib/tinyusb 2>/dev/null || true
	curl -sL -o third_party/$(RISCV_TC_PKG) $(RISCV_TC_REL)/$(RISCV_TC_PKG)
	cd third_party && echo "$(RISCV_TC_SUM)  $(RISCV_TC_PKG)" | (shasum -a 256 -c 2>/dev/null || sha256sum -c)
	mkdir -p third_party/riscv-toolchain && cd third_party && case $(RISCV_TC_PKG) in \
	  *.zip) unzip -q $(RISCV_TC_PKG) -d riscv-toolchain ;; *) tar xzf $(RISCV_TC_PKG) -C riscv-toolchain ;; esac
	rm third_party/$(RISCV_TC_PKG)

# Run after make deps, which fetches pico-sdk
deps-rp2040:
	curl -sL -o third_party/$(ARM_TC_PKG) $(ARM_TC_REL)/$(ARM_TC_PKG)
	cd third_party && echo "$(ARM_TC_SUM)  $(ARM_TC_PKG)" | (shasum -a 256 -c 2>/dev/null || sha256sum -c)
	mkdir -p third_party/arm-toolchain && tar xJf third_party/$(ARM_TC_PKG) -C third_party/arm-toolchain --strip-components=1
	rm third_party/$(ARM_TC_PKG)
.PHONY: deps-rp2040

# Dependencies for CI host checks; excludes hardware and QEMU requirements
deps-host:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
.PHONY: deps-host

# Check that each available third_party dependency matches its pinned commit
check-deps:
	@for d in secp256k1:$(SECP_REV) wasm-micro-runtime:$(WAMR_REV) pico-sdk:$(PICO_REV) \
	          QR-Code-generator:$(QRGEN_REV) spleen:$(SPLEEN_REV); do \
	  n=$${d%%:*}; want=$${d#*:}; \
	  if [ ! -d third_party/$$n ]; then echo "$$n: not fetched (skipped)"; continue; fi; \
	  got=$$(git -C third_party/$$n rev-parse HEAD 2>/dev/null); \
	  if [ "$$got" != "$$want" ]; then echo "$$n: $$got != $$want"; exit 1; fi; done
	@echo "every fetched third_party is at its pinned commit"
.PHONY: check-deps

.PHONY: deps
