/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		cse.c
 *
 * @brief 		Code file for entry point CXL Switch Emulator 
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

/* memset()
 */
#include <string.h>

/* autl_prnt_buf()
 */
#include <arrayutils.h>

/* mctp_init()
 * mctp_set_mh()
 * mctp_run()
 */
#include <mctp.h>
#include <cxlstate.h>

#include "signals.h"

#include "options.h"

#include "state.h"

#include "fmapi_handler.h"
#include "emapi_handler.h"

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

#define CSLN_PORTS 		32
#define CSLN_VCSS 		32
#define CSLN_VPPBS 		256

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * cse main
 *
 * STEPS 
 *  0: Parse CLI options
 *  1: Register Signal Handlers
 *  2: Initialize global state array 
 *  3: Load state file 
 *  4: Print the state 
 *  5: MCTP Init
 *  6: Run MCTP
 *  7: While loop 
 *  8: Stop MCTP
 *  9: Free memory
 */
int main(int argc, char* argv[]) 
{
	INIT
	int rv;
	struct mctp *m;

	// Initialize varaibles
	cxls = NULL;
	stop_requested = 0;
	rv = 1;

	// 0: Parse CLI options
	rv = options_parse(argc,argv);
	if (rv != 0) 
	{
		printf("Error: Parse options failed: %d\n", rv);
		goto end;
	}

	STEP // 1: Register Signal Handlers
	signals_register();

	STEP // 2: Initialize global state array 
	cxls = cxls_init(CSLN_PORTS, CSLN_VCSS, CSLN_VPPBS);
	if (cxls == NULL) 
	{
		printf("Error: state init failed \n");
		goto end_options;		
	}

	STEP // 3: Load state file 
	if (opts[CLOP_CONFIG_FILE].set) 
	{
		rv = state_load(cxls, opts[CLOP_CONFIG_FILE].str);
		if (rv < 0) 
		{
			printf("Error: state load config file  failed \n");
			goto end_state;		
		}
	}
	
//	STEP // 4: Build PCI Representation
	
	STEP // 5: Print the state 
	if (opts[CLOP_PRINT_STATE].set) 
		cxls_prnt(cxls);

	STEP // 6: MCTP Init
	m = mctp_init();
	if (m == NULL) 
		goto end_state;

	// Set supported MCTP Message Versions
	mctp_set_version(m, MCMT_CXLFMAPI,	0xF2,0xF1,0xFF,0x00);
	mctp_set_version(m, MCMT_CXLCCI,	0xF2,0xF1,0xFF,0x00);

	// Set Message handler functions
	mctp_set_handler(m, MCMT_CXLFMAPI, fmapi_handler);
	mctp_set_handler(m, MCMT_CSE, emapi_handler);

	// Set MCTP verbosity levels
	mctp_set_verbosity(m, opts[CLOP_MCTP_VERBOSITY].u64);

	STEP // 7: Run MCTP
	rv = mctp_run(m, opts[CLOP_TCP_PORT].u16, opts[CLOP_TCP_ADDRESS].u32, MCRM_SERVER, 1, 1);
	if (rv != 0)
	{
		switch (rv)
		{
			case -1: 
				printf("Socket create failed\n");
				break;
			case -2: 
				printf("Socket bind failed\n");
				break;
			case -3:
				printf("Socket connect failed");
				break;
			case 1:
				printf("Could not create Connection Handler Thread\n");
				break;
			case 2:
				printf("MCTP threads failed to start\n");
				break;
		}
		goto end_mctp;
	}

	STEP // 8: While loop 
	while ( stop_requested == 0 ) 
	{
		sleep(1);
	}

	STEP // 9: Stop MCTP
	mctp_stop(m);

end_mctp:

	mctp_free(m);

	rv = 0;

end_state:
	
	cxls_free(cxls);

end_options:

	options_free(opts);

end:

	EXIT(rv)

	return rv;
};

