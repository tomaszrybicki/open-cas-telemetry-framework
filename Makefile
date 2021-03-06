# Copyright(c) 2012-2018 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear

SHELL := /bin/bash
.DEFAULT_GOAL := all
CMAKE_FILE=CMakeLists.txt

TEST_DIR=build/test
TEST_PREFIX=./rootfs

ifdef DEBUG
	ifndef BUILD_DIR
		BUILD_DIR=build/debug
	endif
	BUILD_TYPE=DEBUG
	ifndef PREFIX
		PREFIX=./rootfs
	endif
else
	ifndef BUILD_DIR
		BUILD_DIR=build/release
	endif
	BUILD_TYPE=RELEASE
	ifndef PREFIX
		PREFIX=/
		RUN_LD_CONFIG=1
	endif
endif
SOURCE_PATH:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

.PHONY: init all install uninstall reinstall package clean doc

init:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake $(SOURCE_PATH) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)

all: init
	$(MAKE) -C $(BUILD_DIR) all

doc: init
	$(MAKE) -C $(BUILD_DIR) doc

install: all
	cmake -DCOMPONENT=octf-install -P $(BUILD_DIR)/cmake_install.cmake

uninstall:
	xargs rm -f < $(BUILD_DIR)/install_manifest_octf-install.txt

reinstall: | uninstall install

package: all
	$(MAKE) -C $(BUILD_DIR) package

test:
	BUILD_DIR=$(TEST_DIR) PREFIX=$(TEST_PREFIX) $(MAKE) all
	BUILD_DIR=$(TEST_DIR) PREFIX=$(TEST_PREFIX) $(MAKE) install
	$(MAKE) -C $(TEST_DIR) run-unit-tests

clean:
	$(info Cleaning $(BUILD_DIR))
	@if [ -d $(BUILD_DIR) ] ; \
	then \
		$(MAKE) -C $(BUILD_DIR) clean ; \
		rm -rf $(BUILD_DIR) ; \
	fi
