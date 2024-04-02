/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		options.c
 *
 * @brief 		Code file for cli options parsing using argp/argz library
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */
/* INCLUDES ==================================================================*/

/* memset()
 */
#include <string.h> 

/* free()
 */
#include <stdlib.h>

/* inet_pton()
 */
#include <arpa/inet.h>

/* 
 */
#include <linux/types.h> 

/* struct argp
 * struct argp_state
 * argp_parse()
 */
#include <argp.h>

/* autl_prnt_buf()
 */
#include <arrayutils.h>

#include "options.h"

#include <unistd.h>
/* MACROS ====================================================================*/

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

static int pr_main(int key, char *arg, struct argp_state *state);
static void print_help();
static void print_options(struct argp_option *o);
static void print_options_array(struct opt *o);
static void print_usage(struct argp_option *o);

/* GLOBAL VARIABLES ==========================================================*/

/**
 * Global char pointer to dynamically store the name of the application 
 *
 * This is allocated and stored when parse_options() is called
 */
static char *app_name;

/**
 * String representation of CLOP Enumeration 
 */ 
char *STR_CLOP[] = {
	"VERBOSITY",			
	"MCTP_VERBOSITY",
	"PRINT_STATE",	
	"PRINT_OPTIONS",	
	"CONFIG_FILE",	
	"TCP_PORT"
};

/**
 * String representation of CLVO Enumeration 
 */ 
char *STR_CLVO[] = {
	"General verbose output", 	// CVSO_GENERAL 	= 0
	"Call Stack",				// CVSO_CALLSTACK 	= 1
	"STEPS",					// CVSO_STEPS 		= 2 
	"Parsing",					// CVSO_PARSE 		= 3 
	"Actions",					// CVSO_ACTIONS 	= 4 
	"Commands",					// CVSO_COMMANDS	= 5 
	"Errors"					// CVSO_ERRORS 		= 6 
};

/**
 * Options array that stores all parsed optioons
 */
struct opt *opts = NULL;

/**
 * Global string used by argp to print version with --version
 */
const char *argp_program_version = "version 0.1";

/**
 * Global string used by argp when printing the help message
 */
const char *argp_program_bug_address = "code@jrlabs.io";

/**
 * Aray of argp_option structs to define the CLI options 
 *
 * Non character options 
 * 701 - verbosity-options
 * 702 - verbosity-hex
 * 703 - verbosity-mctp
 */
struct argp_option ao_main[] =						
{	
	{0,0,0,0, "File Options",1},						
  	{"config",  			'c', "FILE", 0, "File name of CXL switch config file", 0},
	{0,0,0,0, "Networking Options",2},
  	{"tcp-port", 			'P', "INT", 0, "Server TCP Port", 0},
  	{"tcp-address", 		'T', "INT", 0, "Server TCP Address", 0}
	,	
	{0,0,0,0, "Verbosity Options",8}, 
  	{"print-options",		706,  NULL, OPTION_HIDDEN, "Print the initial State", 0},
  	{"state", 				's',  NULL, OPTION_HIDDEN, "Print the initial State", 0},
  	{"log", 		    	'l',  NULL, 0, "Emit Log output", 0} ,	
  	{"verbose", 			'v',  NULL, 0, "Verbose output", 0} ,	
  	{"verbosity", 			'V', "INT", 0, "Set Verbosity Option", 0},	
  	{"verbosity-hex", 		'X', "HEX", 0, "Set Verbosity Bitfield (in hex)",	0},
  	{"verbosity-mctp", 		'Z', "HEX", OPTION_HIDDEN, "MCTP Verbosity Bitfield (in hex)", 0},
  	{"options", 			707,  NULL, 0, "Print list of verbosity flags",	0},
	{0,0,0,0, "Help Options", 9}, 
  	{"help",    			'h', NULL, 0, "Display Help", 0},
  	{"usage",   			701, NULL, 0, "Display Usage", 0},	
  	{"version", 			702, NULL, 0, "Display Version", 0},
	{0,0,0,0,0,0}
};

/**
 * Options that are passed to argp
 */ 
struct argp argp = 
{ 	
	ao_main, 	// An array of argp_option structures 
	pr_main, 	// Function to call when parsing each option
	NULL, 		// Help text "ARG1 ARG2" that is displayed after usage eg: Usage: argp.out [OPTION...] ARG1 ARG2
	NULL, 		// User help text at top of --help output
	NULL,		// A vector of argp_children structures
	NULL,		// A function to filter the output of help messages
	NULL 		// String domain used to translate strings in argp library
};

/* FUNCTIONS =================================================================*/

/**
 * Return a string representation of CLI Option Names [CLOP]
 *
 * @param u CLI Option Enumeration value [CLOP]
 */
char *clop(int u)
{
	if (u >= CLOP_MAX) 
		return NULL;
	return STR_CLOP[u];
}

/** 
 * Parse function called by parse args for each parameter passed in on the command line
 *
 * @detail 	This function is called by parse_argp() for each option encoutered
 * @return 	0 success, non-zero to indicate a problem 
 *
 * STEPS:
 * 1: Get a pointer to thearguments object for argz
 * 2: Handle each option based on the key 
 */
static int pr_main (	
	int key, 					// short name for the option e.g. 'd' or a number 
	char *arg,					// pointer to string entered after the key 
	struct argp_state *state	// State that is available to each call to this function
)
{
	struct opt *o;
	int rv, i;

	// STEP 2: Handle each option based on the key 
	switch (key)
	{
		// config
		case 'c': 
			o = &opts[CLOP_CONFIG_FILE];
			o->set = 1;
			o->str = strndup(arg, CLMR_MAX_ARG_STR_LEN);
			break;
			
		// help
		case 'h': 
			print_help();
			exit(0);
			break;

		// log
		case 'l': 
			o = &opts[CLOP_VERBOSITY];
			o->set = 1;

			o->u64 |= CLVB_ACTIONS;
			o->u64 |= CLVB_COMMANDS;
			o->u64 |= CLVB_ERRORS;
			break;

		// tcp-port
		case 'P': 
			o = &opts[CLOP_TCP_PORT];
			o->set = 1;
			o->u16 = strtoul(arg, NULL, 0);
			break;

		// TCP Address
		case 'T': 
			o = &opts[CLOP_TCP_ADDRESS];
			o->set = 1;
			rv = inet_pton(AF_INET, arg, &o->u32);
			if (rv != 1)
			{
				printf("Invalid TCP IP Address\n");
				exit(rv);
			}
			rv = 0;
			break;

		// state
		case 's': 
			o = &opts[CLOP_PRINT_STATE];
			o->set = 1;
			break;

		// verbose
		case 'v': 
			o = &opts[CLOP_VERBOSITY];
			o->set = 1;

			// Set General Verbosity bit
			o->u64 |= CLVO_GENERAL;
			break;

		// verbosity
		case 'V': 
			o = &opts[CLOP_VERBOSITY];
			o->set = 1;
			i = atoi(arg);

			// Validate Option
			if ( ( i < 0 ) || ( i >= CLVO_MAX ) ) {
				printf("Error: Invalid Verbosity option");
				exit(1);
			}

			// Set the verbosity bit 
			o->u64 |= (0x01 << i);
			break;
	
		// verbosity-hex
		case 'X': 
			o = &opts[CLOP_VERBOSITY];
			o->set = 1;
			o->u64 = strtoul(arg, NULL, 16);
			break;
		
		// verbosity-mctp
		case 'Z': 
			o = &opts[CLOP_MCTP_VERBOSITY];
			o->set = 1;
			o->u64 = strtoul(arg, NULL, 16);
			break;

		// usage 
		case 701: 
			print_usage(ao_main);
			exit(0);
			break;

		// version
		case 702: 
			printf("%s\n", argp_program_version);
			exit(0);
			break;

		// options
		case 706: 
			o = &opts[CLOP_PRINT_OPTS];
			o->set = 1;
			break;
		
		// options
		case 707: 
			printf("Verbosity options:\n");
			for ( i = 0 ; i < CLVO_MAX ; i++)
				printf("%2d: %s\n", i, STR_CLVO[i]);
			exit(0);
			break;
		
		// Called for non option parameter
		case ARGP_KEY_ARG:
			argp_failure (state, 1, 0, "too many arguments");			 
			break;

		// Last call. Verify parameters. Fill in missing values
		case ARGP_KEY_END:

			// Set default port if not specified
			if (!opts[CLOP_TCP_PORT].set) {
				opts[CLOP_TCP_PORT].set = 1;
				opts[CLOP_TCP_PORT].u16 = CLMR_DEFAULT_SERVER_PORT;
			}

			// Print options if requested
			if (opts[CLOP_PRINT_OPTS].set) 
				print_options_array(opts);

			break;
	} 

	return 0;	
}

/**
 * Print the Help output
 *
 * STEPS
 * 1: Print the Global Header Statement
 * 2: Print usage	
 * 3: Print level header and flagged options
 */
static void print_help()
{
	// STEP 1: Print the Global Header Statement
	printf("CXL Switch Emulator\n");
	
printf("\n\
Usage: %s <options>\n", app_name);
	print_options(ao_main);
	printf("\n");
}

/**
 * Print the command line flag options to the screen as part of help output
 *
 * @param o the menu level [CLAP] enum
 */
static void print_options(struct argp_option *o)
{
	int len;

	while (o->doc != NULL) 
	{
		// Break if this is the ending  NULL entry
		if ( !o->name && !o->key && !o->arg && !o->flags && !o->doc && !o->group )
			break;

		// Skip Hidden Options
		if (o->flags & OPTION_HIDDEN) {
			o++;
			continue;
		}

		// Determine if this is a section heading
		else if ( !o->name && !o->key && !o->arg && !o->flags && o->doc)
			printf("\n %s:\n", o->doc);

		// Print normal option entry 
		else { 

			// IF this option has a single character key, print the key, else print spaces
			if (isalnum(o->key)) 
				printf("  -%c, ", o->key);
			else 
				printf("      ");
			len = 6;

			// If this option has a long name, print the long name
			if (o->name) {
				printf("--%s", o->name);
				len += strlen(o->name) + 2;
			}

			// If this option has an arg type, print the type 
			if (o->arg) {
				printf("=%s", o->arg);
				len += strlen(o->arg) + 1;
			}

			// Print remaining spaces up to description column
			for ( int i = 0 ; i < CLMR_HELP_COLUMN - len ; i++ )
				printf(" ");
			
			// Print description of this option
			printf("%s\n", o->doc);
		}
		o++;
	}
}

/**
 * Debug function to print out the options array at the end of parsing
 */ 
static void print_options_array(struct opt *o)
{
	int i, len, maxlen;

	maxlen = 0;

	// Find max length of CLOP String
	for (i = 0 ; i < CLOP_MAX ; i++) {
		len = strlen(clop(i));
		if (len > maxlen)
			maxlen = len;
	}

	// Print Header 
	printf("##");						// index 
	printf(" Name");						// OP Name 
	for (int k = 5 ; k <= maxlen ; k++)	// Spaces 
		printf(" ");
	printf(" S"); 						// Set 	
	printf("   u8");  					// u8
	printf("    u16"); 					// u16
	printf("        u32"); 				// u32
	printf("                u64"); 		// u64
	printf("    val"); 					// val
	printf("                num"); 		// num
	printf("                len"); 		// len
	printf(" str");						// str
	printf("\n");

	// Print each entry
	for (i = 0 ; i < CLOP_MAX ; i++) {
		// index 
		printf("%02d", i);

		// OP Name 
		printf(" %s", clop(i));

		// Spaces 
		for (int k = strlen(clop(i)) ; k < maxlen ; k++)
			printf(" ");

		printf(" %d", 			o[i].set); // Set 	
		printf(" 0x%02x", 		o[i].u8);  // u8
		printf(" 0x%04x", 		o[i].u16); // u16
		printf(" 0x%08x", 		o[i].u32); // u32
		printf(" 0x%016llx", 	o[i].u64); // u64
		printf(" 0x%04x", 		o[i].val); // val
		printf(" 0x%016llx", 	o[i].num); // num
		printf(" 0x%016llx", 	o[i].len); // len

		if (o[i].str)
			printf(" %s", o[i].str);

		printf("\n");

		if (o[i].len > 0)
			autl_prnt_buf(o[i].buf, o[i].len, 4, 0);
	}
}

/**
 * Print the usage information for a option level
 *
 * @param option 	Menu item from enum [CLAP]
 * @param o 		struct argp_option* to the string data to pull from	
 * STEPS:
 * 1: Initialize variables
 * 2: Generate header text 
 * 3: Count the number of short options with no argument
 * 4: If there is at least one short option with no arg, append short options with no argument here
 * 5: Append short options with arguments 
 * 6: Append long options
 * 7: Find index of last space before character 80
 * 8: Loop through usage buffer and break it up into smaller chunks 
*/
static void print_usage(struct argp_option *o)
{
	int hdr_len, buf_len, num, i, index;
	char buf[4096];
	char str[4096];
	char *ptr;
	struct argp_option *original;
	
	// STEP 1: Initialize variables
	num = 0;
	index = 0;
	hdr_len = 0; 
	buf_len = 1;
	memset(buf, 0, 4096);
	memset(str, 0, 4096);
	original = o;
	ptr = buf;

	// STEP 2: Generate header text 
	sprintf(str, "Usage: %s ",      				app_name);
	hdr_len = strlen(str);

	// STEP 3: Count the number of short options with no argument
	while ( !( !o->name && !o->key && !o->arg && !o->flags && !o->doc && !o->group ) )
	{
		if (isalnum(o->key) && !o->arg) 
			num++;
		o++;
	}
	
	// Reset pointer
	o = original;

	// STEP 4: If there is at least one short option with no arg, append short options with no argument here
	if ( num > 0 ) 
	{
		// Add Leader [-
		sprintf(&buf[buf_len], "[-");
		buf_len += 2;

		// Add each key character
		while ( !( !o->name && !o->key && !o->arg && !o->flags && !o->doc && !o->group ) )
		{
			// If this option has a single character key, print the key, else print spaces
			if (isalnum(o->key) && !o->arg) {
				sprintf(&buf[buf_len], "%c", o->key);
				buf_len += 1;
			}
			o++;
		}

		// Add trailing ]
		sprintf(&buf[buf_len], "] ");
		buf_len += 2;
	}
	
	// Reset pointer
	o = original;

	// STEP 5: Append short options with arguments 
	while ( !( !o->name && !o->key && !o->arg && !o->flags && !o->doc && !o->group ) )
	{
		// If this option has a single character key and an arg 
		if (isalnum(o->key) && o->arg) {
			sprintf(&buf[buf_len], "[-%c=%s] ", o->key, o->arg);
			buf_len += 6;
			buf_len += strlen(o->arg);
		}
		o++;
	}
	
	// Reset pointer
	o = original;

	// STEP 6: Append long options
	while ( !( !o->name && !o->key && !o->arg && !o->flags && !o->doc && !o->group ) )
	{
		// If this option has a long name, print the long name
		if (o->name) 
		{
			sprintf(&buf[buf_len], "[--%s", o->name);
			buf_len += strlen(o->name) + 3;

			// If this option has an arg type, print the type 
			if (o->arg) 
			{
				sprintf(&buf[buf_len], "=%s", o->arg);
				buf_len += strlen(o->arg) + 1;
			}

			// Add trailing ]
			sprintf(&buf[buf_len], "] ");
			buf_len += 2;
		}
		o++;
	}

	// STEP 7: Find index of last space before character 80
	index = 0;
	for ( i = 1 ; i < (CLMR_MAX_HELP_WIDTH-hdr_len) ; i++ ) {
		if (ptr[i] == ' ') 
			index = i;
		if (ptr[i]==0)
			break;
	}

	// STEP 8: Loop through usage buffer and break it up into smaller chunks 
	while (index != 0)
	{
		// Copy the line of the buffer into str 
		memcpy(&str[hdr_len], &ptr[1], index-1); 

		// Set the next char after the string to 0
		str[hdr_len+index-1] = 0;

		// Print the merged string 
		printf("%s\n", str);

		// Clear the header portion of the print str
		memset(str, ' ', hdr_len);

		// Advance buffer
		ptr = &ptr[index];

		// Find index of last space before character 80
		index = 0;
		for ( i = 1 ; i < (CLMR_MAX_HELP_WIDTH-hdr_len) ; i++ ) {
			if (ptr[i] == ' ') 
				index = i;
			if (ptr[i]==0)
				break;
		}
	}
}

/**
 * Free allocated memory by option parsing proceedure
 *
 * @return 0 upon success. Non zero otherwise
 */
int options_free(struct opt *opts)
{
	struct opt *o;
	int i, rv;

	rv = 1;

	// Free app_name 
	if (app_name != NULL)
		free(app_name);
	app_name = NULL;

	// Verify inputs
	if (opts == NULL)
		goto end;

	// For each option in the array, free any allocated memory in the option
	for ( i = 0 ; i < CLOP_MAX ; i++ )
	{
		o = &opts[i];	

		// Free buf field
		if (o->buf)
			free(o->buf);
		o->buf = NULL;

		// Free str field
		if (o->str)
			free(o->str);
		o->str = NULL;
	}

	// Free options array
	free(opts);
	opts = NULL;

	rv = 0;

end:

	return rv;
}

/**
 * Parse the command line options 
 *
 * @param argc 	int representing the number of cli parameters 
 * @param argv 	char** of cli parameter strings
 * @return 		Returns 0 on success, error code on error
 *
 * STEPS
 * 1: Zero out global options array
 * 2: Zero out arguments array for argz use 
 * 3: Parse arguments 
 * 4: argz allocates some memory that needs to be freed after parsing 
 */
int options_parse(int argc, char *argv[])
{
	int rv, len;

	// Initialize variables
	rv = 1;

	// STEP 1: Store app name in global variable
	len = strnlen(argv[0], CLMR_MAX_NAME_LEN);
	if (len > 0) 
	{
		app_name = malloc(len+1);
		if (app_name == NULL) 
			goto end;
	
		if (!strncmp("./", argv[0], 2))
			memcpy(app_name, &argv[0][2], len-1);
		else
			memcpy(app_name, argv[0], len+1);
	}

	// STEP 2: Allocate and clear memory for options array
	opts = calloc(CLOP_MAX, sizeof(struct opt));
	if (opts == NULL) 
		goto end_name;

	// STEP 3: Parse arguments
  	rv = argp_parse(&argp, argc, argv, ARGP_IN_ORDER | ARGP_NO_HELP, 0, NULL);

	goto end;

end_name:

	free(app_name);

end:

	return rv;
}

