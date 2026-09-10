BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: all sdk configure build install clean

all: build

sdk:
	./scripts/bootstrap-sdk.sh

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config Release

install: build
	./scripts/install.sh "$(BUILD_DIR)/out/tsguard.so"

clean:
	rm -rf "$(BUILD_DIR)"
