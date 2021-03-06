##
## SPDX-License-Identifier: MIT
## Copyright (C) 2020 by the ryujin authors
##

SHELL:=bash
default: all

SOURCEDIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))
BUILDDIR:=build
GENERATOR:=Ninja
MAKE_COMMAND:=ninja
MAKE_FILE:=build.ninja

NP:=2
MPIRUN:=mpirun --bind-to none --oversubscribe -np $(NP)
EXECUTABLE:=ryujin
PARAMETER_FILE:=$(EXECUTABLE).prm

edit: all
	@if [ \! -f $(BUILDDIR)/run/$(PARAMETER_FILE) ]; then cd $(BUILDDIR)/run; $(MPIRUN) ./$(EXECUTABLE); fi
	@vim $(BUILDDIR)/run/$(PARAMETER_FILE)

run: all
	@cd $(BUILDDIR)/run && time ASAN_OPTIONS=detect_leaks=0 $(MPIRUN) ./$(EXECUTABLE)

rum:
	@echo "... and a bottle of rum"

run_clean:
	@rm -f $(BUILDDIR)/run/*.(vtk|vtu|log|txt|gnuplot)
	@rm -f $(BUILDDIR)/run/*-*.*

.PHONY: default edit run run_clean

##########################################################################

rebuild_cache:
	@mkdir -p $(BUILDDIR)
	@cd $(BUILDDIR) && cmake -G$(GENERATOR) $(SOURCEDIR)

edit_cache:
	@mkdir -p $(BUILDDIR)
	@cd $(BUILDDIR) && ccmake -G$(GENERATOR) $(SOURCEDIR)

$(BUILDDIR)/$(MAKE_FILE):
	@mkdir -p $(BUILDDIR)
	@cd $(BUILDDIR) && cmake -G$(GENERATOR) $(SOURCEDIR)

release:
	@mkdir -p $(BUILDDIR)
	@cd $(BUILDDIR) && cmake -DCMAKE_BUILD_TYPE=Release -G$(GENERATOR) $(SOURCEDIR)
	@cd $(BUILDDIR) && $(MAKE_COMMAND)

debug:
	@mkdir -p $(BUILDDIR)
	@cd $(BUILDDIR) && cmake -DCMAKE_BUILD_TYPE=Debug -G$(GENERATOR) $(SOURCEDIR)
	@cd $(BUILDDIR) && $(MAKE_COMMAND)

Makefile:
	

%: $(BUILDDIR)/$(MAKE_FILE)
	@cd $(BUILDDIR) && $(MAKE_COMMAND) $@

.PHONY: rebuild_cache edit_cache release debug
