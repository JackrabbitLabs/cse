/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		signals.c
 *
 * @brief 		Code file for signal handling functions 
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */

/* INCLUDES ==================================================================*/

/* gettid()
 */
#define _GNU_SOURCE

#include <unistd.h>

/* printf()
 */
#include <stdio.h>

/* signal()
 */
#include <signal.h>

/* strsignal()
 */
#include <string.h>

#include "options.h"

#include "signals.h"

/* MACROS ====================================================================*/

#ifdef CSE_VERBOSE
 #define INIT 			unsigned step = 0;
 #define ENTER 					if (opts[CLOP_VERBOSITY].u64 & CLVB_CALLSTACK) 	printf("%d:%s Enter\n", 			gettid(), __FUNCTION__);
 #define STEP 			step++; if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u\n", 			gettid(), __FUNCTION__, step);
 #define HEX32(m, i)			if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u %s: 0x%x\n",	gettid(), __FUNCTION__, step, m, i);
 #define INT32(m, i)			if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u %s: %d\n",	gettid(), __FUNCTION__, step, m, i);
 #define EXIT(rc) 				if (opts[CLOP_VERBOSITY].u64 & CLVB_CALLSTACK) 	printf("%d:%s Exit: %d\n", 			gettid(), __FUNCTION__,rc);
#else
 #define ENTER
 #define EXIT(rc)
 #define STEP
 #define HEX32(m, i)
 #define INT32(m, i)
 #define INIT 
#endif // CSE_VERBOSE

#define IFV(u) 							if (opts[CLOP_VERBOSITY].u64 & u) 

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

/* GLOBAL VARIABLES ==========================================================*/

/**
 * Global variable used for the signal handlers to tell the main loop to stop
 */
int stop_requested = 0;

/* FUNCTIONS =================================================================*/

/**
 * Register signal handlers
 */
void signals_register()
{
	ENTER

	signal(SIGINT, signals_sigint);

	EXIT(0)
}

/**
 * Handler for SIGINT (CTRL-C)
 */
void signals_sigint(int sig)
{
	ENTER

	IFV(CLVB_CALLSTACK) printf("Caught Signal: %d - %s\n",  sig, strsignal(sig));

	stop_requested = 1;	

	EXIT(0)
}

