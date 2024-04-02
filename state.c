/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		state.c
 *
 * @brief 		Code file to manage the CXL switch state 
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

/* memset()
 */
#include <string.h>

/* printf()
 */
#include <stdio.h>

/* malloc()
 * free()
 */
#include <stdlib.h>

/* errno
 */
#include <errno.h>

/* mmap()
 */
#include <sys/mman.h>

/** GHashTable 
 * g_hash_table_foreach()
/ */
#include <glib-2.0/glib.h>

/* autl_prnt_buf()
 */
#include <arrayutils.h>

/* yl_obj_t
 * ly_load()
 * yl_free()
 *
 */
#include <yamlloader.h>

#include <fmapi.h>

#include <pciutils.h>

#include "options.h" 

#include "state.h"

/* MACROS ====================================================================*/

#define MAX_STR 256

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

int state_load_devices(struct cxl_switch_state *state, GHashTable *ht);
int state_load_emulator(struct cxl_switch_state *state, GHashTable *ht);
int state_load_ports(struct cxl_switch_state *state, GHashTable *ht);
int state_load_switch(struct cxl_switch_state *state, GHashTable *ht);
int state_load_vcss(struct cxl_switch_state *state, GHashTable *ht);

void _parse_devices(gpointer key, gpointer value, gpointer user_data);
void _parse_device(gpointer key, gpointer value, gpointer user_data);
void _parse_device_mld(gpointer key, gpointer value, gpointer user_data);
void _parse_device_pciecfg(gpointer key, gpointer value, gpointer user_data);
void _parse_device_pcicap(gpointer key, gpointer value, gpointer user_data);
void _parse_device_pciecap(gpointer key, gpointer value, gpointer user_data);
void _parse_device_port(gpointer key, gpointer value, gpointer user_data);
void _parse_emulator(gpointer key, gpointer value, gpointer user_data);
void _parse_ports(gpointer key, gpointer value, gpointer user_data);
void _parse_port(gpointer key, gpointer value, gpointer user_data);
void _parse_switch(gpointer key, gpointer value, gpointer user_data);
void _parse_vcss(gpointer key, gpointer value, gpointer user_data);
void _parse_vcs(gpointer key, gpointer value, gpointer user_data);
void _parse_vppbs(gpointer key, gpointer value, gpointer user_data);
void _parse_vppb(gpointer key, gpointer value, gpointer user_data);

void state_print_pcie_cfg_space(__u8 *cfgspace, unsigned indent);

/* GLOBAL VARIABLES ==========================================================*/

/**
 * Global pointer to CXL Switch State
 */
struct cxl_switch_state *cxl_state;

/* FUNCTIONS =================================================================*/

/**
 * Convert state representation to fmapi representation for FM API Identify Switch Device
 *
 * @param[in] 	cs 	struct cxl_switch_state* to pull info from
 * @param[out] 	fi 	struct fmapi_psc_ident* to put info into 
 */
void state_conv_identity(struct cxl_switch_state *cs, struct fmapi_psc_id_rsp *fi)
{
	// Zero out destination
	memset(fi, 0, sizeof(*fi));

	// Copy static information 
	fi->ingress_port 	= cs->ingress_port;		//!< Ingress Port ID 
	fi->num_ports 		= cs->num_ports;		//!< Total number of physical ports
	fi->num_vcss 		= cs->num_vcss;			//!< Max number of VCSs
	fi->num_vppbs 		= cs->num_vppbs;		//!< Max number of vPPBs 
	fi->num_decoders 	= cs->num_decoders;		//!< Number of HDM decoders available per USP 

	// Compute dynamic information 
	for ( int i = 0 ; i < cs->num_ports ; i++ ) {
		if ( cs->ports[i].state != FMPS_DISABLED ) 
			fi->active_ports[i/8] |= (0x01 << (i % 8));
	}

	for ( int i = 0 ; i < cs->num_vcss ; i++ ) {
		if ( cs->vcss[i].state == FMVS_ENABLED) 
			fi->active_vcss[i/8] |= (0x01 << (i % 8));
	}

	for ( int i = 0 ; i < cs->num_vcss ; i++ ) {
		for ( int j = 0 ; j < MAX_VPPBS_PER_VCS ; j++ ) {
			if ( cs->vcss[i].vppbs[j].bind_status != FMBS_UNBOUND ) 
				fi->active_vppbs++;
		}
	}
}

/**
 * Convert state representation to fmapi representation for FM API Phy Port State Response
 *
 * @param[in] 	src 	struct cxl_switch_state* to pull info from
 * @param[out] 	dst 	struct fmapi_psc_port_info* to put info into 
 */
void state_conv_port_info(struct port *src, struct fmapi_psc_port_info *dst)
{
	// Zero out destination
	memset(dst, 0, sizeof(*dst));

	// Copy static information 
	dst->ppid 		= src->ppid;	//!< Physical Port ID
	dst->state 		= src->state;	//!< Current Port Configuration State [FMPS]
	dst->dv 		= src->dv;		//!< Connected Device CXL Version [FMDV]
	dst->dt 		= src->dt;		//!< Connected Device Type [FMDT]
	dst->cv			= src->cv;		//!< Connected device CXL Version [FMCV]
	dst->mlw		= src->mlw; 	//!< Max link width
	dst->nlw 		= src->nlw;		//!< Negotiated link width [FMNW]
	dst->speeds		= src->speeds; 	//!< Supported Link speeds vector [FMSS]
	dst->mls		= src->mls; 	//!< Max Link Speed [FMMS]
	dst->cls		= src->cls; 	//!< Current Link Speed [FMMS] 
	dst->ltssm		= src->ltssm; 	//!< LTSSM State [FMLS]
	dst->lane 		= src->lane;	//!< First negotiated lane number
	dst->lane_rev 	= src->lane_rev;//!< Link State Flags [FMLF] and [FMLO]
	dst->perst 		= src->perst; 	//!< Link State Flags [FMLF] and [FMLO]
	dst->prsnt 		= src->prsnt; 	//!< Link State Flags [FMLF] and [FMLO]
	dst->pwrctrl 	= src->pwrctrl;	//!< Link State Flags [FMLF] and [FMLO]
	dst->num_ld	= src->ld;			//!< Supported Logical Device (LDs) count 
}

/**
 * Convert state representation to fmapi representation for FM API Get Virtual CXL Switch info
 *
 * @param[in] 	src 	struct cxl_switch_state* to pull info from
 * @param[out] 	dst 	struct fmapi_psc_port_info* to put info into 
 */
void state_conv_vcs_info(struct vcs *src, struct fmapi_vsc_info_blk *dst)
{
	// Zero out destination
	memset(dst, 0, sizeof(*dst));

	// Copy static information 
	dst->vcsid 			= src->vcsid;	 	//!< Virtual CXL Switch ID
	dst->state 			= src->state;		//!< VCS State [FMVS]
	dst->uspid 			= src->uspid;		//!< USP ID. Upstream physical port ID
	dst->num 			= src->num;			//!< Number of vPPBs
	
	//!< Variable array of PPB Status Blocksa
	for (int i = 0 ; i < dst->num ; i++) {
		dst->list[i].status 	= src->vppbs[i].bind_status;
		dst->list[i].ppid 		= src->vppbs[i].ppid;
		dst->list[i].ldid 		= src->vppbs[i].ldid;
	}
}

/**
 * Free memory allocated by the CXL Switch State  
 * 
 * STEPS:
 * 1: Destroy Mutex
 * 2: Free pci config space memory 
 * 3: Free Port MLD config space
 * 4: unmap memory space if present 
 * 5: Free Port MLD
 * 6: Free VCSs
 * 7: Free ports
 * 8: Free devices
 * 9: Free Switch State
 */ 
void state_free(struct cxl_switch_state *state)
{
	INIT
	unsigned i, k;
	struct port *p;
	struct cse_device *d;
	
	ENTER

	if (state == NULL) 
		return;
	
	STEP // 1: Destroy mutex
	pthread_mutex_destroy(&state->mtx);

	STEP // 2: Free pci config space memory 
	for ( i = 0 ; i < state->num_ports ; i++ ) 
	{
		p = &state->ports[i];
		if ( p->cfgspace != NULL ) 
		{
			free(p->cfgspace);
			p->cfgspace = NULL;
		}
	}

	STEP // 3: Free Port MLD config space
	for ( i = 0 ; i < state->num_ports ; i++ ) 
	{
		p = &state->ports[i];
		if (p->mld != NULL) 
		{
			for ( k = 0 ; k < MAX_LD ; k++ ) 
			{
				if ( p->mld->cfgspace[k] != NULL )
				{
					free(p->mld->cfgspace[k]);
					p->mld->cfgspace[k] = NULL;
				}
			}
		}
	}

	STEP // 4: unmap memory space if present 
	for ( i = 0 ; i < state->num_ports ; i++ ) 
	{
		p = &state->ports[i];
		if (p->mld != NULL) 
		{
			if (p->mld->memspace != NULL) 
			{
				munmap(p->mld->memspace, p->mld->memory_size);
				p->mld->memspace = NULL;
			}

			if (p->mld->file != NULL) 
			{
				free(p->mld->file);
				p->mld->file = NULL;
			}
		}
	}
	
	STEP // 5: Free Port MLD
	for ( i = 0 ; i < state->num_ports ; i++ )
	{
		p = &state->ports[i];
		if (p->mld != NULL)
		{
			free(p->mld);
			p->mld = NULL;
		}
	}
	
	STEP // 6: Free VCSs
	if (state->vcss != NULL) 
	{
		free(state->vcss);
		state ->vcss = NULL;
	}

	STEP // 7: Free Ports
	if (state->ports != NULL) 
	{
		free(state->ports);
		state->ports = NULL;
	}

	STEP // 8: Free devices
	if (state->devices != NULL ) 
	{
		for ( i = 0 ; i < state->len_devices ; i++ )
		{
			d = &state->devices[i];

			// Free device name string if present 
			if (d->name != NULL) 
			{
				free(d->name);
				d->name = NULL;
			}

			// Free device pcie config space if present 
			if (d->cfgspace != NULL) 
			{
				free(d->cfgspace);
				d->cfgspace = NULL;
			}

			// Free device MLD if present 
			if (d->mld != NULL)
			{
				free(d->mld);
				d->mld = NULL;
			}
		}

		free(state->devices);
		state->devices = NULL;
	}
	state->len_devices = 0;
	state->num_devices = 0;

	STEP // 9: Free Switch State
	if (state->dir != NULL)
	{
		free(state->dir);
		state->dir = NULL;
	}

	free(state);
	state = NULL;

	EXIT(0)
}

/**
 * Initialize state object with default values 
 * 
 * @return 	struct state. Returns 0 upon error and sets errno
 *
 * STEPS
 * 1: Validate inputs
 * 2: Initalize State Identity
 * 3: Initalize Ports 
 * 4: Initalize VCSs
 * 5: Initalize PCIe config space register
 */
struct cxl_switch_state *state_init(unsigned ports, unsigned vcss, unsigned vppbs)
{
	INIT
	unsigned i;
	struct port *p;
	struct vcs *v;
	struct cxl_switch_state *state;

	ENTER

	STEP // 1: Validate inputs
	if (ports > MAX_PORTS)
		ports = MAX_PORTS;
	if (vcss > MAX_VCSS)
		vcss = MAX_VCSS;
	if (vppbs > MAX_VPPBS)
		vppbs = MAX_VPPBS;

	STEP // 2: Initalize State Identity
	state = calloc(1, sizeof(struct cxl_switch_state));
	if(state == NULL) {
		errno = ENOMEM;
		goto end; 
	}
	
	// Initialize Identity information
	state->version = 1;
	state->vid = 0xb1b2;
	state->did = 0xc1c2;
	state->svid = 0xd1d2;
	state->ssid = 0xe1e2;
	state->sn = 0xa1a2a3a4a5a6a7a8;
	state->ingress_port = 1;
	state->num_ports = ports;
	state->num_vcss = vcss;
	state->num_vppbs = vppbs;
	state->num_decoders = 42;

	// Initialize Mutex
	pthread_mutex_init(&state->mtx, NULL);

	STEP // 3: Initalize Ports 
	state->ports = calloc(ports, sizeof(struct port));
	if(state->ports == NULL) {
		errno = ENOMEM;
		goto end_state; 
	}

	// Set default port values
	for ( i = 0 ; i < ports ; i++ ) {
		p 				= &state->ports[i];
		p->ppid 		= i;
		p->state 		= FMPS_DISABLED;
   		p->dv 			= FMDV_NOT_CXL;
   		p->dt 			= FMDT_NONE;
   		p->cv 			= 0;
   		p->mlw 			= 16;
   		p->nlw 			= 0;
   		p->speeds 		= FMSS_PCIE5 | FMSS_PCIE4 | FMSS_PCIE3 | FMSS_PCIE2 | FMSS_PCIE1;
   		p->mls 			= FMMS_PCIE5;
   		p->cls 			= 0;
   		p->ltssm 		= FMLS_DISABLED;
   		p->lane 		= 0;
		p->lane_rev 	= 0;
		p->perst 		= 0;
		p->prsnt 		= 0;
		p->pwrctrl 		= 0;
   		p->ld 			= 0;
	}

	STEP // 4: Initalize VCSs
	state->vcss = calloc(vcss, sizeof(struct vcs));
	if(state->vcss == NULL) {
		errno = ENOMEM;
		goto end_ports; 
	}

	// Set default vcs values
	for ( i = 0 ; i < vcss ; i++) {
		v 			= &state->vcss[i];
		v->vcsid	= i;
		v->state	= FMVS_DISABLED;
		v->uspid	= 0;
		v->num		= 0;

		// Set the vcs->vppb[] array to zero
		memset(v->vppbs, 0, MAX_VPPBS_PER_VCS * sizeof(struct vppb));
	}

 	STEP // 5: Initalize PCIe config space register
	for ( i = 0 ; i < ports ; i++ )
	{
		state->ports[i].cfgspace = calloc(1, CFG_SPACE_SIZE);
		if(state->vcss == NULL) {
			errno = ENOMEM;
			goto end_cfgspace; 
		}
	}

	goto end;

end_cfgspace:

	for ( i = 0 ; i < ports ; i++ ) {
		if( cxl_state->ports[i].cfgspace != NULL ) {
			free(cxl_state->ports[i].cfgspace);
			cxl_state->ports[i].cfgspace = NULL;
		}
	}

	free(state->vcss);
	state->vcss = NULL;

end_ports:

	free(state->ports);
	state->ports = NULL;

end_state:

	free(state);
	state = NULL;

end:

	EXIT(0)

	return state;
}

/** 
 * Load config file and update state 
 *
 * @param state		struct cxl_switch_state to fill 
 * @param filename 	char * to yaml config file to load 
 * @return	 		Returns 0 on success, error code otherwise
 *
 * STEPS:
 * 1: Validate inputs 
 * 2: Parse config file into hash table
 * 3: Parse Emulator configuration 
 * 4: Parse Devices
 * 5: Parse Switch 
 * 6: Parse Ports
 * 7: Parse VCSs
 * 8: Free memory allocated for hash table
 */
int state_load(struct cxl_switch_state *state, char *filename)
{
	INIT
	int rv;
	GHashTable *ht;
	char *default_file = "config.yaml";

	ENTER

	// Initialize varialbes
	rv = 1;

	STEP // 1: Validate inputs 
	if( state == NULL ) {
		rv = EINVAL;
		goto end;
	}

	if( filename == NULL )
		filename = default_file;

	STEP // 2: Parse config file into hash table 
	ht = yl_load(filename);
	if ( ht == NULL ) {
		rv = errno;
		goto end;
	}

	STEP // 3: Parse Emulator configuration 
	rv = state_load_emulator(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 4: Parse Devices
	rv = state_load_devices(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 5: Parse Switch 
	rv = state_load_switch(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 6: Parse Ports
	rv = state_load_ports(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 7: Parse VCSs
	rv = state_load_vcss(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 8: Free memory allocated for hash table
	yl_free(ht);

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Load device definitions from hash table into memory
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 * 1: Obtain hash table 
 * 2: Allocate memory for devices in state
 * 3: Parse each entry in the hash table
 */
int state_load_devices(struct cxl_switch_state *state, GHashTable *ht)
{
	INIT
	int rv;
	yl_obj_t *ylo;

	ENTER

	// Initialize variables
	rv = 1;

	STEP // 1: Obtain devices hash table 
	ylo = (yl_obj_t*) g_hash_table_lookup(ht, "devices");
	if (ylo == NULL || ylo->ht == NULL) 
		goto end;

	STEP // 2: Allocate memory for devices in state
	state->devices = calloc(INITIAL_NUM_DEVICES, sizeof(struct cse_device));
	if (state->devices == NULL)
		goto end;
	state->len_devices = INITIAL_NUM_DEVICES;
	
	STEP // 3: Parse each entry in the device table
	g_hash_table_foreach(ylo->ht, _parse_devices, state);	

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Load emulator configration variables from hash table into memory
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 * 1: Obtain hash table 
 * 2: Parse each entry in the hash table
 */
int state_load_emulator(struct cxl_switch_state *state, GHashTable *ht)
{
	INIT
	int rv;
	yl_obj_t *ylo;

	ENTER

	// Initialize variables
	rv = 1;

	STEP // 1: Obtain devices hash table 
	ylo = (yl_obj_t*) g_hash_table_lookup(ht, "emulator");
	if (ylo == NULL || ylo->ht == NULL) 
		goto end;

	STEP // 2: Parse each entry in the device table
	g_hash_table_foreach(ylo->ht, _parse_emulator, state);	

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Load port definitions from hash table into memory
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 * 1: Initialize port state to defaults 
 * 2: Obtain hash table 
 * 3: Parse each entry in the hash table
 * 4: Instantiate each port device 
 */
int state_load_ports(struct cxl_switch_state *state, GHashTable *ht)
{
	INIT
	int rv;
	unsigned i, k;
	yl_obj_t *ylo;
	struct port *port;

	ENTER

	// Initialize variables
	rv = 1;

	STEP // 1: Initialize port state to defaults 
	for ( i = 0 ; i < state->num_ports ; i++ )
	{
		port = &state->ports[i];
		port->state = FMPS_DSP;
		port->mlw = state->mlw;
		port->mls = state->mls;
		port->speeds = state->speeds;
    	port->ltssm = FMLS_L0;
		port->lane_rev = 0;
		port->perst = 0;
		port->prsnt = 0;
		port->pwrctrl = 0;
    	port->ld = 0;
	}

	STEP // 2: Obtain hash table 
	ylo = (yl_obj_t*) g_hash_table_lookup(ht, "ports");
	if (ylo == NULL || ylo->ht == NULL) 
		goto end;

	STEP // 3: Parse each entry in the hash table
	g_hash_table_foreach(ylo->ht, _parse_ports, state->ports);	

	STEP // 4: Instantiate each port device 
	for ( i = 0 ; i < state->num_ports ; i++ )
	{
		port = &state->ports[i];

		// If the port has a device name, copy values from it 	
		if ( port->device_name != NULL ) 
			for ( k = 0 ; k < state->num_devices ; k++ ) 
				if (state->devices[k].name != NULL)
					if ( !strcmp(state->devices[k].name, port->device_name) ) 
						state_connect_device(port, &state->devices[k]);		
	}

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Load switch definitions from hash table into memory
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 * 1: Obtain hash table 
 * 2: Parse each entry in the hash table
 */
int state_load_switch(struct cxl_switch_state *state, GHashTable *ht)
{
	INIT
	int rv;
	yl_obj_t *ylo;

	ENTER

	// Initialize variables
	rv = 1;

	STEP // 1: Obtain hash table 
	ylo = (yl_obj_t*) g_hash_table_lookup(ht, "switch");
	if (ylo == NULL || ylo->ht == NULL) 
		goto end;

	STEP // 2: Parse each entry in the hash table
	g_hash_table_foreach(ylo->ht, _parse_switch, state);	

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Load VCS definitions from hash table into memory
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 * 1: Obtain hash table 
 * 2: Parse each entry in the hash table
 */
int state_load_vcss(struct cxl_switch_state *state, GHashTable *ht)
{
	INIT
	int rv;
	yl_obj_t *ylo;

	ENTER

	// Initialize variables
	rv = 1;

	STEP // 1: Obtain hash table 
	ylo = (yl_obj_t*) g_hash_table_lookup(ht, "vcss");
	if (ylo == NULL || ylo->ht == NULL) 
		goto end;

	STEP // 2: Parse each entry in the hash table
	g_hash_table_foreach(ylo->ht, _parse_vcss, state->vcss);	

	rv = 0;

end:

	EXIT(rv)

	return rv;
}

/**
 * Copy data from a device definition to a port 
 *
 * @param p 	struct port* to fill with data
 * @param d 	struct cse_device* to pull the data from
 * 
 * STEPS:
 * 1: Copy basic parameters 
 * 2: Copy PCIe config space to the port
 * 3: Copy MLD information if present 
 * 4: Memory Map a file if requested by the device profile 
 */
int state_connect_device(struct port *p, struct cse_device *d)
{
	INIT 
	int rv;
	unsigned i;
	char filename[MAX_FILE_NAME_LEN];
	FILE *fp;

	ENTER

	// Initialize variables
	rv = 1;

	// Validate Inputs 
	if (d->name == NULL)
		goto end;

	STEP // 1: Copy basic parameters 
    p->dv = d->dv;			
    p->dt = d->dt;			
    p->cv = d->cv;			
	p->ltssm = FMLS_L0;
	p->lane = 0;
	p->lane_rev = 0;
	p->perst = 0;
	p->pwrctrl = 0;
	p->ld = 0;

	// If the device definition says this is a rootport then set as an Upstream Port
	if( d->rootport == 1 )
		p->state = FMPS_USP;
	else 
		p->state = FMPS_DSP;

	// Pick the lower of the two widths
	if (d->mlw < p->mlw)
    	p->nlw = d->mlw << 4;
	else 
		p->nlw = p->mlw << 4;

	// Pick the lower of the two speeds
	if (d->mls < p->mls)
		p->cls = d->mls;
	else 
		p->cls = p->mls;

	// Set present bit 
 	p->prsnt = 1; 

	STEP // 2: Copy PCIe config space to the port
	memcpy(p->cfgspace, d->cfgspace, CFG_SPACE_SIZE);

	STEP // 3: Copy MLD information if present 
	if (d->mld != NULL) 
	{
    	p->ld = d->mld->num;

		// Allocate memory for MLD object in the port
		p->mld = malloc(sizeof(struct mld));

		// Copy MLD from device definition to port 
		memcpy(p->mld, d->mld, sizeof(struct mld));

		for ( i = 0 ; i < d->mld->num ; i++ )
		{
			// Allocate memory for each LD pcie config space
			p->mld->cfgspace[i] = malloc(CFG_SPACE_SIZE);
			
			// Copy PCIe config space from device definition to port
			memcpy(p->mld->cfgspace[i], d->cfgspace, CFG_SPACE_SIZE);
		}
	}

	STEP // 4: Memory Map a file if requested by the device profile 
	if (d->mld != NULL && d->mld->mmap == 1) 
	{
		// Prepare filename
		sprintf(filename, "%s/port%02d", cxl_state->dir, p->ppid);

		// Create file
		fp = fopen(filename, "w+");
		if (fp == NULL) {
			printf("Error: Could not open file: %s\n", filename);
			goto end;
		}

		// Truncate file to desired length
		rv = ftruncate(fileno(fp), p->mld->memory_size);
		if (rv != 0) {
			printf("Error: Could not truncate file. Memory Size: 0x%llx errno: %d\n", p->mld->memory_size, errno);
			goto end;
		}

		// mmap file
		p->mld->memspace = mmap(NULL, p->mld->memory_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(fp), 0);
		if (p->mld->memspace == NULL) {
			printf("Error: Could not mmap the file. errno: %d\n", errno);
			rv = 1;
			goto end;
		}

		// Save the filename to the port mld object 
		p->mld->file = strdup(filename);

		// Close file 
		fclose(fp);
	
	}

	rv = 0;

end:

	return rv;
}

/**
 * Clear / Free data from a port device definition 
 *
 * This function essemtially makes it appear as if the device has been removed from the slot
 *
 * @param p	struct port* The port to clear of values
 *
 * STEPS:
 * 1: Clear basic parameters 
 * 2: Clear PCIe config space 
 * 3: Free device name 
 * 4: Unmemmap MLD if present 
 * 5: Free PCIe cfg space for each ld
 * 6: Free MLD if present
 */
int state_disconnect_device(struct port *p)
{
	INIT
	int rv;
	unsigned i;

	ENTER 

	// Initialize variables
	rv = 1;

	STEP // 1: Clear basic parameters 
    p->dv = 0;
    p->dt = 0;			
    p->cv = 0;			
    p->nlw = 0;
	p->cls = 0;
	p->ltssm = 0;
	p->lane = 0;
	p->lane_rev = 0;
	p->perst = 0; 
 	p->prsnt = 0;
	p->pwrctrl = 0;
	p->ld = 0;

	STEP // 2: Clear PCIe config space 
	memset(p->cfgspace, 0, CFG_SPACE_SIZE);

	STEP // 3: Free device name 
	if (p->device_name != NULL) 
	{
		free(p->device_name);
		p->device_name = NULL;
	}

	STEP // 4: Unmemmap MLD if present 
	if (p->mld != NULL && p->mld->memspace != NULL)
	{
		msync (p->mld->memspace, p->mld->memory_size, MS_SYNC); 
		munmap(p->mld->memspace, p->mld->memory_size);
		p->mld->memspace = NULL;
	}

	STEP // 5: Free PCIe cfg space for each ld
	if (p->mld != NULL) 
	{
		for ( i = 0 ; i < p->mld->num ; i++ ) {
			if ( p->mld->cfgspace[i] != NULL ) {
				free(p->mld->cfgspace[i]);
				p->mld->cfgspace[i] = NULL;
			}
		}
	}

	STEP // 6: Free MLD if present
	if (p->mld != NULL) 
	{
		free(p->mld);
		p->mld = NULL;
	}

	rv = 0;

	return rv;
}

/**
 * Function to parse a device entry in the hash table
 *
 * STEPS
 * 1: Obtain device ID from device entry
 * 2: Check if there is space for a new device in the device table, allocate more if needed
 * 3: Duplicate key string into state object
 * 4: Run parse function for each entry in sub hash table
 */
void _parse_devices(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct cxl_switch_state *s;
	yl_obj_t *ylo, *ylo_did;
	unsigned did;
	void * ptr; 

	ENTER

	// Initialize varialbes
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch_state*) user_data;

	IFV(CLVB_PARSE) printf("%d:%s Key: %s\n", gettid(), __FUNCTION__, (char*) key);
	
	STEP // 1: Obtain device ID from device entry
	ylo_did = (yl_obj_t*) g_hash_table_lookup(ylo->ht, "did");
	if (ylo_did == NULL || ylo_did->str == NULL) 
		goto end;

	did = strtoul(ylo_did->str, NULL, 0);

	STEP // 2: Check if there is space for a new device in the device table, allocate more if needed
	if (s->num_devices >= s->len_devices) 
	{
		// Allocate more memory
		ptr = calloc(s->len_devices + INITIAL_NUM_DEVICES, sizeof(struct cse_device));

		// Copy the existing data to new buffer
		memcpy(ptr, s->devices, s->num_devices * sizeof(struct cse_device));

		// free old buffer
		free(s->devices);

		// reassign new buffer to state 
		s->devices = ptr;
	}

	STEP // 3: Duplicate key string into state object
	s->devices[did].name = strdup(key);

	STEP // 4: Run parse function for each entry in sub hash table
	g_hash_table_foreach(ylo->ht, _parse_device, &s->devices[did]);	

	if ( (did + 1 )> s->num_devices)
		s->num_devices = did + 1;

end:

	EXIT(0);
}

/**
 * Function to parse each device hashtable entry in the hashtable
 * 
 * STEPS
 * 1: Verify the yaml loader object hash table is not NULL
 * 2: Call parser for each type of ntry 
 */
void _parse_device(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	int rv;
	struct cse_device *d;
	yl_obj_t *ylo;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	d = (struct cse_device*) user_data;

	STEP // 1: Verify the yaml loader object hash table is not NULL
	if (ylo->ht == NULL) 
		goto end;

	STEP // 2: Call parser for each type of ntry 
	if (!strcmp(key, "port"))
	{
		g_hash_table_foreach(ylo->ht, _parse_device_port, d);	
	}
	else if (!strcmp(key, "pcicfg"))
	{
		// Allocate memory for PCIe config space 
		if (d->cfgspace == NULL)
			d->cfgspace = calloc(1, CFG_SPACE_SIZE);
	
		g_hash_table_foreach(ylo->ht, _parse_device_pciecfg, d->cfgspace);	
	}
	else if (!strcmp(key, "mld")) 
	{
		// Allocate memory for MLD struct 
		if (d->mld == NULL)
			d->mld = calloc(1, sizeof(struct mld));
	
		g_hash_table_foreach(ylo->ht, _parse_device_mld, d->mld);	
	}
	
	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each device mld entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_device_mld(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	int rv;
	struct mld *mld;
	yl_obj_t *ylo;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	mld = (struct mld*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	STEP // 2: Assign KV pairs to state variables 
	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if      (!strcmp(key, "memory_size"))		mld->memory_size 		= strtoul(ylo->str, NULL, 16);
	else if (!strcmp(key, "num"))				mld->num				= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "epc"))				mld->epc				= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "ttr"))				mld->ttr				= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "granularity"))		mld->granularity		= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "epc_en"))			mld->epc_en				= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "ttr_en"))			mld->ttr_en 			= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "egress_mod_pcnt"))	mld->egress_mod_pcnt	= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "egress_sev_pcnt"))	mld->egress_sev_pcnt	= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "sample_interval"))	mld->sample_interval	= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "rcb"))				mld->rcb				= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "comp_interval"))		mld->comp_interval		= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "bp_avg_pcnt"))		mld->bp_avg_pcnt		= strtoul(ylo->str, NULL, 10);
	else if (!strcmp(key, "rng1"))				autl_csv_to_u64(mld->rng1, ylo->str, FM_MAX_NUM_LD, 0);
	else if (!strcmp(key, "rng2"))				autl_csv_to_u64(mld->rng2, ylo->str, FM_MAX_NUM_LD, 0);
	else if (!strcmp(key, "alloc_bw"))			autl_csv_to_u8(mld->alloc_bw, ylo->str, FM_MAX_NUM_LD, 1);
	else if (!strcmp(key, "bw_limit"))			autl_csv_to_u8(mld->bw_limit, ylo->str, FM_MAX_NUM_LD, 1);
	else if (!strcmp(key, "mmap"))				mld->mmap				= strtoul(ylo->str, NULL, 0);

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each device pci config space entry in the hashtable
 *
 * STEPS:
 * 1: Assign KV pairs to state variables 
 * 2: Call parse function for each sub entry
 */
void _parse_device_pciecfg(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct pcie_cfg_hdr *ph;
	yl_obj_t *ylo;

	ENTER

	// Initialize varialbes
	ylo = (yl_obj_t*) value;
	ph = (struct pcie_cfg_hdr*) user_data;

	STEP // 1: Assign KV pairs to state variables 
	if (ylo->str != NULL) 
	{
		IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);

		if      (!strcmp(key, "vendor"))			ph->vendor 			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "device"))			ph->device			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "command"))			ph->command 		= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "status"))			ph->status			= strtoul(ylo->str, NULL, 0);

		else if (!strcmp(key, "revid"))				ph->rev				= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "baseclass"))			ph->baseclass		= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "subclass"))			ph->subclass		= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "pi"))				ph->pi				= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "cacheline"))			ph->cls				= strtoul(ylo->str, NULL, 0);
		
		else if (!strcmp(key, "type"))				ph->type			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "subvendor"))			ph->subvendor		= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "subsystem"))			ph->subsystem		= strtoul(ylo->str, NULL, 0);


		else if (!strcmp(key, "intline"))			ph->intline			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "intpin"))			ph->intpin			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "mingnt"))			ph->mingnt			= strtoul(ylo->str, NULL, 0);
		else if (!strcmp(key, "maxlat"))			ph->maxlat			= strtoul(ylo->str, NULL, 0);
	}

	STEP // 2: Call parse function for each sub entry
	if (ylo->ht != NULL) 
	{
		if (!strcmp(key, "cap")) {
			g_hash_table_foreach(ylo->ht, _parse_device_pcicap, ph);	

			// Clear rsvd2 field now that we are done parsing the capabilities list
			ph->rsvd2 = 0;
		}
		else if (!strcmp(key, "ecap")) {
			g_hash_table_foreach(ylo->ht, _parse_device_pciecap, ph);	

			// Clear rsvd2 field now that we are done parsing the capabilities list
			ph->rsvd2 = 0;
		}
	}

	EXIT(0)
}

/**
 * Function to parse each device pci config space capabilities entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Find ptr to last cap in the list
 * 3: Fill in the new capability header 
 * 4: Convert CSV string to bytes at the location after the new pci capabilities header
 * 5: Store the offset to the next capability entry in the list in reserved field 
 */
void _parse_device_pcicap(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct pcie_cfg_hdr *ph;
	yl_obj_t *ylo;
	struct pcie_cap *pc; 
	__u8 *base, *ptr;
	int rv;
	
	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	ph = (struct pcie_cfg_hdr*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);

	STEP // 2: Find ptr to last cap in the list
	/* If the cap ptr in the pci_hdr is null, then there are no capabilities in the current list
	 * Set the cap pointer to 0x40 which is the next byte after the pci_hdra
	 * And set the ptr to that byte in memory
	 */
	base = (__u8*) ph;
	if (ph->cap == 0)
	{
		ph->cap = 0x40;
		ptr = base + ph->cap;
	}
	else {
		// Get PC pointer to first entry in table
		pc = (struct pcie_cap*) (base + ph->cap);

		// Walk the linked list until a null pointer is found in the next field
		while (pc->next != 0) 
			pc = (struct pcie_cap*) (base + pc->next);

		// Set the pointer of the next entry
		pc->next = ph->rsvd2;

		// prepare the pointer of the next extry to fill out 
		ptr = base + ph->rsvd2;
	}

	STEP // 3: Fill in the new capability header 
	pc = (struct pcie_cap*) ptr;
	pc->id = strtoul(key, NULL, 0);
	pc->next = 0;
	ptr += 2;

	STEP // 4: Convert CSV string to bytes at the location after the new pci capabilities header
	rv = autl_csv_to_u8(ptr, ylo->str, 128, 1);

	STEP // 5: Store the offset to the next capability entry in the list in reserved field 
	ph->rsvd2 = ptr + rv - base;

	rv = 0;
	
end:

	EXIT(rv)
}

/**
 * Function to parse each device pci config space extended capability entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Find ptr to last cap in the list
 */
void _parse_device_pciecap(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct pcie_cfg_hdr *ph;
	yl_obj_t *ylo;
	struct pcie_ecap *pc; 
	__u8 *base, *ptr;
	__u32 k;
	int rv, num;
	
	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	ph = (struct pcie_cfg_hdr*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	STEP // 2: Find ptr to last cap in the list

	// ptr to start of pci hdr
	base = (__u8*) ph;
	
	// Get ptr to first ecap entry
	pc = (struct pcie_ecap*) (base + 0x100);
	
	if (pc->id == 0) {
		ptr = (__u8*) pc;
	}
	else {
		// Walk the linked list until a null pointer is found in the next field
		while (pc->next != 0) 
			pc = (struct pcie_ecap*) (base + pc->next);
	
		// Set the pointer of the next entryf from saved end ptr
		pc->next = ph->rsvd2;
	
		// prepare the pointer of the next extry to fill out 
		ptr = base + ph->rsvd2;
	}
	
	// Fill in the new capability header 
	pc = (struct pcie_ecap*) ptr;
	k = strtoul(key, NULL, 0);
	pc->id = k >> 4;
	pc->ver = k & 0x0F;
	pc->next = 0;
	ptr += 4;
	
	// Convert CSV string to bytes at the location after the new pci capabilities header
	num = autl_csv_to_u8(ptr, ylo->str, 128, 1);
	
	// Store the offset to the next capability entry in the list in reserved field 
	ph->rsvd2 = ptr + num - base;

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each device port entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_device_port(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct cse_device *d;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	d = (struct cse_device*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	STEP // 2: Assign KV pairs to state variables 

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if      (!strcmp(key, "dv"))		d->dv		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "dt"))		d->dt		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "cv"))		d->cv		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "mlw"))		d->mlw		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "mls"))		d->mls		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "rootport"))	d->rootport	= strtoul(ylo->str, NULL,0);
	
	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each emulator configuration entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_emulator(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct cxl_switch_state *s;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch_state*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	STEP // 2: Assign KV pairs to state variables 

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if (!strcmp(key, "verbosity-hex")) {
		opts[CLOP_VERBOSITY].set 					= 1;
		opts[CLOP_VERBOSITY].u64 					= strtoull(ylo->str, NULL, 16);
	}
	else if (!strcmp(key, "verbosity-mctp")) {
		opts[CLOP_MCTP_VERBOSITY].set 				= 1;
		opts[CLOP_MCTP_VERBOSITY].u64 				= strtoull(ylo->str, NULL, 16);
	}
	else if (!strcmp(key, "tcp-port")) {
		opts[CLOP_TCP_PORT].set 					= 1;
		opts[CLOP_TCP_PORT].u16 					= strtoull(ylo->str, NULL, 0);
	}
	else if (!strcmp(key, "dir"))
		s->dir 										= strdup(ylo->str);

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse switch entries in the hash table
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_switch(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	struct cxl_switch_state *s;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch_state*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if (ylo->str == NULL) 
		goto end;

	STEP // 2: Assign KV pairs to state variables 

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if      (!strcmp(key, "version"))      		s->version 			= atoi(ylo->str);
	else if (!strcmp(key, "vid"))          		s->vid 				= strtoul(ylo->str, NULL,16);
	else if (!strcmp(key, "did"))          		s->did 				= strtoul(ylo->str, NULL,16);
	else if (!strcmp(key, "svid"))         		s->svid 			= strtoul(ylo->str, NULL,16);
	else if (!strcmp(key, "ssid"))         		s->ssid 			= strtoul(ylo->str, NULL,16);
	else if (!strcmp(key, "sn"))           		s->sn				= strtoull(ylo->str, NULL, 0);
	else if (!strcmp(key, "max_msg_size_n"))	s->max_msg_size_n	= atoi(ylo->str);
	else if (!strcmp(key, "bos_running"))       s->bos_running		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "bos_pcnt"))         	s->bos_pcnt 		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "bos_opcode"))        s->bos_opcode 		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "bos_rc"))         	s->bos_rc 	 		= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "bos_ext"))         	s->bos_ext 			= strtoul(ylo->str, NULL,0);
	else if (!strcmp(key, "msg_rsp_limit_n"))	s->msg_rsp_limit_n	= atoi(ylo->str);
	else if (!strcmp(key, "ingress_port")) 		s->ingress_port 	= atoi(ylo->str);
	else if (!strcmp(key, "num_ports"))  		s->num_ports 		= atoi(ylo->str);
	else if (!strcmp(key, "num_vcss"))     		s->num_vcss 		= atoi(ylo->str);
	else if (!strcmp(key, "num_vppbs"))    		s->num_vppbs		= atoi(ylo->str);
	else if (!strcmp(key, "num_decoders")) 		s->num_decoders 	= atoi(ylo->str);
	else if (!strcmp(key, "mlw")) 				s->mlw 				= atoi(ylo->str);
	else if (!strcmp(key, "speeds")) 			s->speeds 			= strtoul(ylo->str, NULL, 0);
	else if (!strcmp(key, "mls"))			 	s->mls 				= atoi(ylo->str);

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each ports entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object hash table is not NULL
 * 2: Call parse function for each port
 */
void _parse_ports(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct port *ports;
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	ports = (struct port*) user_data;

	STEP // 1: Verify the yaml loader object hash table is not NULL
	if ( ylo->ht == NULL ) 
		goto end;

	STEP // 2: Call parse function for each port
	id = atoi(key);

	IFV(CLVB_PARSE) printf("%d:%s Parsing Port: %d\n", gettid(), __FUNCTION__, id);

	g_hash_table_foreach(ylo->ht, _parse_port, &ports[id]); 

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each port entry in the hashtable
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_port(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct port *port; 
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	port = (struct port*) user_data;
	
	STEP // 1: Verify the yaml loader object string is not NULL
	if ( ylo->str == NULL ) 
		goto end;

	STEP // 2: Assign KV pairs to state variables 

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if (!strcmp(key, "device")) 	port->device_name 	= strdup(ylo->str);
	else if (!strcmp(key, "mlw")) 	port->mlw 			= atoi(ylo->str);
	else if (!strcmp(key, "mls")) 	port->mls 			= atoi(ylo->str);
	else if (!strcmp(key, "state"))	port->state 		= strtoul(ylo->str, NULL, 0);

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each VCS entry 
 *
 * STEPS:
 * 1: Verify the yaml loader object hash table is not NULL
 * 2: Call parse function for each VCS
 */
void _parse_vcss(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct vcs *vcss; 
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vcss = (struct vcs*) user_data;

	STEP // 1: Verify the yaml loader object hash table is not NULL
	if ( ylo->ht == NULL ) 
		goto end;

	STEP // 2: Call parse function for each vcs
	id = atoi(key);

	IFV(CLVB_PARSE) printf("%d:%s Parsing VCS: %d\n", gettid(), __FUNCTION__, id);

	g_hash_table_foreach(ylo->ht, _parse_vcs, &vcss[id]); 

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each VCS block entry 
 *
 * STEPS:
 * 1: Assign KV pairs to state variables 
 * 2: Call parse function for vPPBs 
 */
void _parse_vcs(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct vcs *vcs;

	ENTER

	// Initialize varialbes
	ylo = (yl_obj_t*) value;
	vcs = (struct vcs*) user_data;
	
	STEP // 1: Assign KV pairs to state variables 
	if ( ylo->str != NULL )
	{
		IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);

		if      (!strcmp(key, "state"))			vcs->state		= atoi(ylo->str);
		else if (!strcmp(key, "uspid"))			vcs->uspid		= atoi(ylo->str);
		else if (!strcmp(key, "num_vppb"))		vcs->num		= atoi(ylo->str);
	}

	STEP // 2: Call parse function for vPPBs
	if (ylo->ht != NULL) 
	{
		g_hash_table_foreach(ylo->ht, _parse_vppbs, vcs->vppbs); 
	}

	EXIT(0)
}

/**
 * Function to parse each vPPB entry 
 *
 * STEPS:
 * 1: Verify the yaml loader object hash table is not NULL
 * 2: Call parse function for each vPPB
 */
void _parse_vppbs(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct vppb *vppbs; 
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vppbs = (struct vppb*) user_data;

	STEP // 1: Verify the yaml loader object hash table is not NULL
	if ( ylo->ht == NULL ) 
		goto end;

	STEP // 2: Call parse function for each vPPB
	id = atoi(key);

	IFV(CLVB_PARSE) printf("%d:%s Parsing vPPB: %d", gettid(), __FUNCTION__, id);

	g_hash_table_foreach(ylo->ht, _parse_vppb, &vppbs[id]); 

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Function to parse each vPPB block entry 
 *
 * STEPS:
 * 1: Verify the yaml loader object string is not NULL
 * 2: Assign KV pairs to state variables 
 */
void _parse_vppb(gpointer key, gpointer value, gpointer user_data)
{
	INIT
	yl_obj_t *ylo;
	struct vppb *vppb; 
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vppb = (struct vppb*) user_data;

	STEP // 1: Verify the yaml loader object string is not NULL
	if ( ylo->str == NULL )
		goto end;

	STEP // 2: Assign KV pairs to state variables 

	IFV(CLVB_PARSE) printf("%d:%s Parsing Key: %s VAL: %s\n", gettid(), __FUNCTION__, (char*) key,  ylo->str);
	
	if      (!strcmp(key, "bind_status"))	vppb->bind_status	= atoi(ylo->str);
	else if (!strcmp(key, "ppid"))			vppb->ppid			= atoi(ylo->str);
	else if (!strcmp(key, "ldid"))			vppb->ldid			= atoi(ylo->str);

	rv = 0;

end:

	EXIT(rv)
}

/**
 * Print the CXL Switch State 
 */ 
void state_print(struct cxl_switch_state *state)
{
	state_print_identity(state, 0);
	state_print_ports(state, 0);
	state_print_vcss(state, 0);
}

/**
 * Print the Device List
 */ 
void state_print_devices(struct cxl_switch_state *s)
{
	struct cse_device *d;

	if (s->devices == NULL) 
		return;
	
	for ( unsigned i = 0 ; i < s->num_devices ; i++ )
	{
		d = &s->devices[i];

		printf("%s:\n",       d->name);
		printf("  Port:\n"); 	
		printf("    dt:     %2d - %s\n", d->dt, fmdt(d->dt));
		printf("    dv:     %2d - %s\n", d->dv, fmdv(d->dv));
		printf("    cv:     %2d - %s\n", d->cv, fmvc(d->cv));
		printf("    mlw:    %2d\n",      d->mlw);
		
		pcie_prnt_cfgspace(d->cfgspace, 2);
	}
}

/**
 * Print the CXL Switch Idenfity Information
 *
 * @param	struct cxl_switch_state* to print
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_identity(struct cxl_switch_state *s, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields
	printf("%singress_port: %u\n", 		space, s->ingress_port);
	printf("%snum_ports:    %u\n", 		space, s->num_ports);
	printf("%snum_vcss:     %u\n", 		space, s->num_vcss);
	printf("%snum_vppbs:    %u\n", 		space, s->num_vppbs);
	printf("%snum_decoders: %u\n", 		space, s->num_decoders);
	printf("%sdir:          %s\n", 		space, s->dir);

}

/**
 * Print CXL MLD Info
 *
 * @param 	mld		struct mld* to use to print
 * @param	indent 	The number of spaces to indent the printed text
 */
void state_print_mld(struct mld *mld, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	printf("%sMulti-Logical Device:\n", space);

	space[indent] = ' ';
	space[indent+2] = 0;

	printf("%sMemory Size                               0x%016llx\n", 	space, mld->memory_size);
	printf("%sNum LD                                    %d\n", 			space, mld->num);
	printf("%sEgress Port Congestion Supported          %d\n", 			space, mld->epc);
	printf("%sTemporary Throughput Reduction Supported  %d\n", 			space, mld->ttr);
	printf("%sGranularity                               %d - %s\n", 	space, mld->granularity, fmmg(mld->granularity));
	printf("%sEgress Port Congestion Enabled            %d\n", 			space, mld->epc_en);
	printf("%sTemporary Throughput Reduction Enabled    %d\n", 			space, mld->ttr_en);
	printf("%sEgress Moderate Percentage                %d\n", 			space, mld->egress_mod_pcnt);
	printf("%sEgress Severe Percentage                  %d\n", 			space, mld->egress_sev_pcnt);
	printf("%sBackpressure Sample Interval              %d\n", 			space, mld->sample_interval);
	printf("%sReqCmpBasis                               %d\n", 			space, mld->rcb);
	printf("%sCompletion Collection Interval            %d\n", 			space, mld->comp_interval);
	printf("%sBackpressure Average Percentage           %d\n", 			space, mld->bp_avg_pcnt);
	printf("%smmap                                      %d\n", 			space, mld->mmap);
	printf("%smmap file                                 %s\n", 			space, mld->file);
	printf("\n");
	printf("%sLDID  Range 1            Range 2            Alloc BW BW Limit\n", space);
	printf("%s----  ------------------ ------------------ -------- --------\n", space);
	for ( int i = 0 ; i < mld->num ; i++ ) 
		printf("%s%4d: 0x%016llx 0x%016llx %8d %8d\n", space, i, mld->rng1[i], mld->rng2[i], mld->alloc_bw[i], mld->bw_limit[i]);
}

/**
 * Print CXL Ports 
 *
 * @param	struct cxl_switch_stat* to use to print
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_ports(struct cxl_switch_state *s, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields
	printf("%sports:\n", space);

	for (int i = 0 ; i < s->num_ports ; i++) {
		printf("%s  %02u:\n", space,i);
		state_print_port(&s->ports[i], indent + 2 + INDENT);
	}
}

/**
 * Print the CXL Port Information
 *
 * @param	struct port* to print
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_port(struct port *p, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields
	printf("%sstate:                   %u\t\t%s\n", 	space, p->state, 	fmps(p->state));
   	printf("%sdv:                      %u\t\t%s\n", 	space, p->dv, 		fmdv(p->dv));
   	printf("%sdt:                      %u\t\t%s\n", 	space, p->dt,		fmdt(p->dt));
   	printf("%scv:                      0x%02x\n", 		space, p->cv);
   	printf("%smax_link_width:          %u\n", 			space, p->mlw);
   	printf("%sneg_link_width:          %u\n", 			space, p->nlw);
   	printf("%sspeeds:                  0x%02x\n", 		space, p->speeds);
   	printf("%smax_link_speed:          %u\t\t%s\n", 	space, p->mls, 		fmms(p->mls));
   	printf("%scur_link_speed:          %u\t\t%s\n", 	space, p->cls, 		fmms(p->cls));
   	printf("%sltssm:                   %u\t\t%s\n", 	space, p->ltssm,	fmls(p->ltssm));
   	printf("%sfirst_lane:              %u\n", 			space, p->lane);
	printf("%sLane Reversal State      %d\n", 			space, p->lane_rev);
	printf("%sPCIe Reset State         %d\n", 			space, p->perst);
	printf("%sPort Presence pin state  %d\n", 			space, p->prsnt);
	printf("%sPower Control State      %d\n", 			space, p->pwrctrl);
   	printf("%sld:                      %u\n", 			space, p->ld);
   	printf("%sDevice Name              %s\n", 			space, p->device_name);

	if (p->cfgspace != NULL) {
		pcie_prnt_cfgspace(p->cfgspace, indent);
		autl_prnt_buf(p->cfgspace, 1024, 16, 1);	
	}

	if (p->mld != NULL) 
		state_print_mld(p->mld, indent);
}

/**
 * Print the CXL VCS List 
 *
 * @param	struct cxl_switch_state* to print from 
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_vcss(struct cxl_switch_state *s, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields
	printf("%svcss:\n", space);

	for (int i = 0 ; i < s->num_vcss ; i++) {
		printf("%s  %02u:\n", space, i);
		state_print_vcs(&s->vcss[i], indent + 2 + INDENT);
	}
}

/**
 * Print information for a single CXL VCS 
 *
 * @param	struct vcs* to print
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_vcs(struct vcs *v, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields of the VCS
	printf("%sstate:          %u\t\t%s\n", 	space, v->state, 			fmvs(v->state));
   	printf("%suspid:          %u\n", 		space, v->uspid);
   	printf("%snum_vppb:       %u\n", 		space, v->num);
	printf("%svppbs:\n",					space);

	// Print the vPPBs of the VCS
	for (int i = 0 ; i < v->num ; i++) {
		printf("%s  %u:\n", space, i);
		state_print_vppb(&v->vppbs[i], indent + 2 + INDENT);
	}
}

/**
 * Print information for a single CXL vPPB
 *
 * @param	struct vppb* to print
 * @param 	indent The number of spaces to indent the printed text
 */
void state_print_vppb(struct vppb *b, unsigned indent)
{
	char space[MAX_INDENT] = "                                ";

	// Handle indent
	if (indent >= MAX_INDENT) 
		indent = MAX_INDENT; 
	space[indent] = 0;

	// Print fields of the VCS
	printf("%sldid:           %u\n", 		space, b->ldid);
	printf("%sppid:           %u\n", 		space, b->ppid);
	printf("%sbind_status:    %u\t\t%s\n", 	space, b->bind_status, 	fmbs(b->bind_status));
}

