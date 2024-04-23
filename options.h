/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		options.h
 *
 * @brief 		Header file for cli options parsing using argp/argz library
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 * Macro / Enumeration Prefixes (CL)
 * CLOP	- CLI Option (OP)
 * CLVB - CLI Verbosity Bit Field (VB)
 * CLVO - CLI Verbosity Options (VO)
 *
 * Standard key mapping 
 * -h --help 			Display Help
 * -T --tcp-port 		Server TCP Port
 * -V --verbosity 		Set Verbosity Flag
 * -X --verbosity-hex	Set all Verbosity Flags with hex value
 *
 * Non char key mapping
 * 701 - usage
 * 702 - version
 * 703 - data
 * 704 - infile
 * 705 - outfile
 * 706 - print-options
 * 707 - options
 */
#ifndef _OPTIONS_H
#define _OPTIONS_H

/* INCLUDES ==================================================================*/

/* 
 */
#include <linux/types.h> 

/* MACROS ====================================================================*/

#define CLMR_DEFAULT_SERVER_PORT 	2508
#define CLMR_MAX_NAME_LEN 			64
#define CLMR_HELP_COLUMN 			30
#define CLMR_MAX_HELP_WIDTH 		100
#define CLMR_MAX_ARG_STR_LEN 		256

/* ENUMERATIONS ==============================================================*/

/**
 * CLI Verbosity Options (VO)
 */
enum _CLVO
{
	CLVO_GENERAL	= 0,
	CLVO_CALLSTACK 	= 1,
	CLVO_STEPS		= 2,
	CLVO_PARSE		= 3,
	CLVO_ACTIONS	= 4,
	CLVO_COMMANDS   = 5,
	CLVO_ERRORS 	= 6,
	CLVO_MAX
};

/**
 * CLI Verbosity Bit Field (VB)
 */
enum _CLVB
{
	CLVB_GENERAL	= (0x01 << 0),
	CLVB_CALLSTACK 	= (0x01 << 1),
	CLVB_STEPS		= (0x01 << 2),
	CLVB_PARSE		= (0x01 << 3),
	CLVB_ACTIONS	= (0x01 << 4),
	CLVB_COMMANDS	= (0x01 << 5),
	CLVB_ERRORS		= (0x01 << 6),
};

/**
 * CLI Option (OP)
 */
enum _CLOP
{	
	CLOP_VERBOSITY,			//!< Verbosity level bitfield <u64>
	CLOP_MCTP_VERBOSITY,	//!< MCTP Library Verbosity level: Bitfield <u64>
	CLOP_PRINT_STATE,		//!< Print the state <set>
	CLOP_PRINT_OPTS,		//!< Print CLI options to console <set>
	CLOP_CONFIG_FILE,		//!< File to load CXL Switch configuration data <str>
	CLOP_TCP_PORT,			//!< TCP Port to listen on for connections <u16>
	CLOP_TCP_ADDRESS,		//!< TCP Address to listen on for connections <u32>
	CLOP_QEMU,				//!< qemu switches, no emulation (for now) 
	CLOP_MAX
};


/* STRUCTS ===================================================================*/

/**
 * CLI Option Struct
 *
 * Each command line parameter is stored in one of these objects
 */
struct opt
{
	int 			set;	//!< Not set (0), set (1) 
	__u8 			u8; 	//!< Unsigned char value
	__u16 			u16; 	//!< Unsigned long value
	__u32 			u32;	//!< Unsigned long value 
	__u64 			u64;	//!< Unsigned long long value 
	__s32 	 		val;	//!< Generic signed value
	__u64 			num;	//!< Number of items 
	__u64 			len;	//!< Data Buffer Length 
	char 			*str;	//!< String value 
	__u8 			*buf;	//!< Data buffer 
};

/* GLOBAL VARIABLES ==========================================================*/

/**
 * Array of options from args parse
 */
extern struct opt *opts;

/* PROTOTYPES ================================================================*/

char *clop(int u);

/**
 * Free allocated memory by option parsing proceedure
 *
 * @return 0 upon success. Non zero otherwise
 */
int options_free(struct opt *opts);

/**
 * Parse command line options 
 */
int options_parse(int argc, char *argv[]);

#endif //ifndef _OPTIONS_H
