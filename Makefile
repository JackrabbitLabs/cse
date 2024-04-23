# SPDX-License-Identifier: Apache-2.0
# ******************************************************************************
#
# @file			Makefile
#
# @brief        Makefile for CXL Switch Endpoint Application
#
# @copyright    Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
#
# @date         Mar 2024
# @author       Barrett Edwards <code@jrlabs.io>
#
# ******************************************************************************

CC=gcc
CFLAGS?= -g3 -O0 -Wall -Wextra
MACROS?=-D CSE_VERBOSE
INCLUDE_DIR?=/usr/local/include
LIB_DIR?=/usr/local/lib  
LOCAL_INCLUDE_DIR?=./include
LOCAL_LIB_DIR?=./lib
INCLUDE_PATH=-I $(LOCAL_INCLUDE_DIR) -I $(INCLUDE_DIR) -I /usr/include/glib-2.0 -I /usr/lib/`uname -m`-linux-gnu/glib-2.0/include/ -I /usr/lib64/glib-2.0/include 
LIB_PATH=-L $(LOCAL_LIB_DIR) -L $(LIB_DIR)
LIBS=-l yamlloader -l yaml -l glib-2.0 -l mctp -l uuid -l ptrqueue -l fmapi -l emapi -l arrayutils -l timeutils -l pci -l cxlstate -l pciutils 
TARGET=cse

all: $(TARGET)

$(TARGET): main.c options.o state.o signals.o emapi_handler.o fmapi_handler.o fmapi_isc_handler.o fmapi_psc_handler.o fmapi_vsc_handler.o fmapi_mpc_handler.o fmapi_mcc_handler.o
	$(CC)    $^ $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) $(LIB_PATH) $(LIBS) -o $@

emapi_handler.o: emapi_handler.c emapi_handler.h
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_mcc_handler.o: fmapi_mcc_handler.c
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_mpc_handler.o: fmapi_mpc_handler.c
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_vsc_handler.o: fmapi_vsc_handler.c
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_psc_handler.o: fmapi_psc_handler.c
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_isc_handler.o: fmapi_isc_handler.c
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

fmapi_handler.o: fmapi_handler.c fmapi_handler.h
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

signals.o: signals.c signals.h
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

options.o: options.c options.h
	$(CC) -c $< $(CFLAGS) $(MACROS)  $(INCLUDE_PATH) -o $@  

state.o: state.c state.h
	$(CC) -c $< $(CFLAGS) $(MACROS) $(INCLUDE_PATH) -o $@  

clean:
	rm -rf ./*.o ./*.a $(TARGET)

doc: 
	doxygen

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

uninstall:
	sudo rm /usr/local/bin/$(TARGET)

# List all non file name targets as PHONY
.PHONY: all clean doc install uninstall

# Variables 
# $^ 	Will expand to be all the sensitivity list
# $< 	Will expand to be the frist file in sensitivity list
# $@	Will expand to be the target name (the left side of the ":" )
# -c 	gcc will compile but not try and link 
