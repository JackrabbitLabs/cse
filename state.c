/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		state.c
cxl_ *
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

#include <pci/pci.h>

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
#include <cxlstate.h>
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

int state_load_devices(struct cxl_switch *state, GHashTable *ht);
int state_load_emulator(struct cxl_switch *state, GHashTable *ht);
int state_load_ports(struct cxl_switch *state, GHashTable *ht);
int state_load_switch(struct cxl_switch *state, GHashTable *ht);
int state_load_vcss(struct cxl_switch *state, GHashTable *ht);
int state_load_from_pci(struct cxl_switch *state);

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
struct cxl_switch *cxls;

/* FUNCTIONS =================================================================*/

/** 
 * Load config file and update state 
 *
 * @param state		struct cxl_switch to fill 
 * @param filename 	char * to yaml config file to load 
 * @return	 		Returns 0 on success, error code otherwise
 *
 * STEPS:
 * 1: Validate inputs 
 * 2: Parse config file into hash table
 * 3: Parse Emulator configuration 
 * 4: Parse Devices
 * 5: Parse Switch 
 * 6: Load physical devices if in a QEMU environment
 * 7: Parse Ports
 * 8: Parse VCSs
 * 9: Free memory allocated for hash table
 */
int state_load(struct cxl_switch *state, char *filename)
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
	
	STEP // 6: Load physical devices if in a QEMU environment
	if (opts[CLOP_QEMU].set == 1)
	{
		rv = state_load_from_pci(state);
		if (rv != 0) 
			goto end;

		goto success;
	}

	STEP // 7: Parse Ports
	rv = state_load_ports(state, ht);
	if (rv != 0) 
		goto end;

	STEP // 8: Parse VCSs
	rv = state_load_vcss(state, ht);
	if (rv != 0) 
		goto end;

success:

	STEP // 9: Free memory allocated for hash table
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
int state_load_devices(struct cxl_switch *state, GHashTable *ht)
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
	state->devices = calloc(INITIAL_NUM_DEVICES, sizeof(struct cxl_device));
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
int state_load_emulator(struct cxl_switch *state, GHashTable *ht)
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
 * Load ports and vcs from physical pci devices
 *
 * @param ht 	GHashTable holding contents of config.yaml file
 * @return 		Returns 0 upon success. Non zero otherwise
 *
 * STEPS
 */
int state_load_from_pci(struct cxl_switch *state)
{
	INIT

	int rv, cache, mem;
	unsigned int num_dvsec, nr;
	__u32 l, type, vppbid;
	__u16 w;
	struct pci_dev *dev, *parent;
	struct pci_cap *cap;
	struct cxl_port cp;
	__u32 fillflags;

	ENTER 

	// Initialize variables 
	rv = 1;
	fillflags =  PCI_FILL_IDENT		
				|PCI_FILL_CLASS		
				|PCI_FILL_CAPS		
				|PCI_FILL_EXT_CAPS	
				|PCI_FILL_PHYS_SLOT	
				|PCI_FILL_MODULE_ALIAS
				|PCI_FILL_LABEL		
				|PCI_FILL_NUMA_NODE	
				|PCI_FILL_IO_FLAGS	
				|PCI_FILL_CLASS_EXT	
				|PCI_FILL_SUBSYS		
				|PCI_FILL_PARENT		
				|PCI_FILL_DRIVER;

	STEP // : get pci_access ptr, init pci_access ptr, get all the devices
	state->pacc = pci_alloc(); 		
	pci_init(state->pacc);			
	pci_scan_bus(state->pacc);		

	// STEP 3: Iiterate over all devices
	for ( dev = state->pacc->devices ; dev ; dev = dev->next ) 
	{
		pci_fill_info(dev, PCI_FILL_CLASS); 	

		// If this is a PCI-to-PCI Bridge then check if it is a CXL upstream port 
		if ( (dev->device_class >> 8) == 0x06 && (dev->device_class & 0x0FF) == 0x04 )
		{
			// Get more info about this device 
			pci_fill_info(dev, fillflags);

			// Get PCI Express Capability 
			cap = pci_find_cap(dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
			if (cap == NULL)
				continue;

			// Determine port type 
			w = pci_read_word(dev, cap->addr + PCI_EXP_FLAGS);
			type = (w & PCI_EXP_FLAGS_TYPE) >> 4;
			if (type != PCI_EXP_TYPE_UPSTREAM)
				continue;

			// Clear the local CXL Port before filling it
			memset(&cp, 0, sizeof(cp));

			// Get max speed / width / vppbid 
  			l = pci_read_long(dev, cap->addr + PCI_EXP_LNKCAP);
  			cp.mls = l & PCI_EXP_LNKCAP_SPEED;
  			cp.mlw = (l & PCI_EXP_LNKCAP_WIDTH) >> 4;
			vppbid = l >> 24;

			// Get cur speed / width 
  			w = pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA);
  			cp.cls = w & PCI_EXP_LNKSTA_SPEED;
  			cp.nlw = (w & PCI_EXP_LNKSTA_WIDTH) >> 4;
		
			// If parent is NULL, then skip this device as we know nothing about it
			parent = dev->parent;
			if (parent == NULL)
				continue;
		
			// Get all info about the parent device 
			pci_fill_info(parent, fillflags);

			// Get PCI Express Capability 
			cap = pci_find_cap(parent, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
			if (cap == NULL)
				continue;

			// Get physical port id from Slot Number in PCI Express capability
			l = pci_read_long(parent, cap->addr + PCI_EXP_SLTCAP);
			cp.ppid = ((l & PCI_EXP_SLTCAP_PSN) >> 19);

			// Set switch IDs based on the upstream port identifiers
			state->vid = dev->vendor_id;
			state->did = dev->device_id;
			state->ssid = dev->subsys_id;
			state->svid = dev->subsys_vendor_id;
			state->sn =    ((__u64) dev->domain_16 		) << 48
						|  ((__u64) dev->device_class 	) << 32
						|  ((__u64) dev->prog_if 		) << 24
						|  ((__u64) dev->bus 			) << 16
						|  ((__u64) dev->dev 			) << 8
						|  ((__u64) dev->func 			) ;

			// Set bind in vcs 
			state->vcss[0].uspid = cp.ppid;
			state->vcss[0].state = FMVS_ENABLED;
			state->vcss[0].vppbs[vppbid].ppid = cp.ppid;
			state->vcss[0].vppbs[vppbid].bind_status = FMBS_BOUND_PORT;
			state->vcss[0].vppbs[vppbid].ldid = 0;

			// Fill local struct cxl_port fields 
			cp.state = FMPS_USP;
			cp.dt = FMDT_CXL_TYPE_1;
			cp.speeds = cp.mls;
			cp.ltssm = FMLS_L0;
			cp.lane = 0;
			cp.lane_rev = 0;
			cp.perst = 0;
			cp.prsnt = 1;
			cp.pwrctrl = 0;
			cp.dev = dev;
			cp.dv = FMDV_CXL2_0;
			cp.cv = FMCV_CXL1_1 | FMCV_CXL2_0;

			// Copy local struct cxl_port into global state 
			memcpy(&state->ports[cp.ppid], &cp, sizeof(cp));
		}

		// If this is a CXL device, then gather more info and the parent's info 
		else if ( (dev->device_class >> 8) == 0x05 && (dev->device_class & 0x0FF) == 0x02 )
		{
			// Get more info about this device 
			pci_fill_info(dev, fillflags);

			// If parent is NULL, then skip this device as we know nothing about it
			parent = dev->parent;
			if (parent == NULL)
				continue;
		
			// Get all info about the parent device 
			pci_fill_info(parent, fillflags);

			// Get PCI Express Capability (Parent)
			cap = pci_find_cap(parent, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
			if (cap == NULL)
				continue;

			// Determine port type (Parent)
			w = pci_read_word(parent, cap->addr + PCI_EXP_FLAGS);
			type = (w & PCI_EXP_FLAGS_TYPE) >> 4;
			if ( type != PCI_EXP_TYPE_DOWNSTREAM )
				continue;

			// Clear the local CXL Port before filling it
			memset(&cp, 0, sizeof(cp));

			// Get physical port number from SLot number in PCI Express capability (Parent)
			l = pci_read_long(parent, cap->addr + PCI_EXP_SLTCAP);
			cp.ppid = ((l & PCI_EXP_SLTCAP_PSN) >> 19);

			// Get max speed / width / vppbid (Parent)
  			l = pci_read_long(parent, cap->addr + PCI_EXP_LNKCAP);
  			cp.mls = l & PCI_EXP_LNKCAP_SPEED;
  			cp.mlw = (l & PCI_EXP_LNKCAP_WIDTH) >> 4;
			vppbid = l >> 24;

			// Get cur speeds
  			w = pci_read_word(parent, cap->addr + PCI_EXP_LNKSTA);
  			cp.cls = w & PCI_EXP_LNKSTA_SPEED;
  			cp.nlw = (w & PCI_EXP_LNKSTA_WIDTH) >> 4;
		
			// Get number of DVSEC Capabilities in Dev. Num returned in variable num_dvsec
			num_dvsec = 0;
			cap = pci_find_cap_nr(dev, PCI_EXT_CAP_ID_DVSEC, PCI_CAP_EXTENDED, &num_dvsec);

			// Loop through DVSEC capabilities
			for ( nr = 0 ; nr < num_dvsec ; nr++)
			{
				// Get DVSEC Capability entry number nr
				cap = pci_find_cap_nr(dev, PCI_EXT_CAP_ID_DVSEC, PCI_CAP_EXTENDED, &nr);

				// Get the DVSEC.type
  				w = pci_read_long(dev, cap->addr + PCI_DVSEC_HEADER2);

				// If DVSEC.type==0, this DVSEC Capability describes device type 
				if (w == 0)
				{
					// Get the flags indicating what CXL protocols are supported
					w = pci_read_word(dev, cap->addr + PCI_CXL_DEV_CAP);
					cache = w & PCI_CXL_DEV_CAP_CACHE;
					mem  = (w & PCI_CXL_DEV_CAP_MEM) >> 2;

					// Determine Device Type 
					if      (cache == 1 && mem == 0) cp.dt = FMDT_CXL_TYPE_1;
					else if (cache == 1 && mem == 1) cp.dt = FMDT_CXL_TYPE_2;
					else if (cache == 0 && mem == 1) cp.dt = FMDT_CXL_TYPE_3;

					break;
				}

				// If the DVSEC Type is 9 then this is a MLD DVSEC 
				else if (w == 9)
				{
					// Set that this is a MLD device 
					cp.ld = pci_read_word(dev, cap->addr + PCI_CXL_MLD_NUM_LD);
					cp.dt = FMDT_CXL_TYPE_3_POOLED;
				}
			}

			// Set bind in vcs 
			state->vcss[0].state = FMVS_ENABLED;
			state->vcss[0].vppbs[vppbid].ppid = cp.ppid;
			state->vcss[0].vppbs[vppbid].bind_status = FMBS_BOUND_PORT;
			state->vcss[0].vppbs[vppbid].ldid = 0;

			// Fill local struct cxl_port fields 
			cp.state = FMPS_DSP;
			cp.speeds = cp.mls;
			cp.ltssm = FMLS_L0;
			cp.lane = 0;
			cp.lane_rev = 0;
			cp.perst = 0;
			cp.prsnt = 1;
			cp.pwrctrl = 0;
			cp.dev = dev;
			cp.dv = FMDV_CXL2_0;
			cp.cv = FMCV_CXL1_1 | FMCV_CXL2_0;

			// Copy local struct cxl_port into global state 
			memcpy(&state->ports[cp.ppid], &cp, sizeof(cp));
		}
	}

	rv = 0;

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
int state_load_ports(struct cxl_switch *state, GHashTable *ht)
{
	INIT
	int rv;
	unsigned i, k;
	yl_obj_t *ylo;
	struct cxl_port *port;

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
						cxls_connect(port, &state->devices[k], cxls->dir);		
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
int state_load_switch(struct cxl_switch *state, GHashTable *ht)
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
int state_load_vcss(struct cxl_switch *state, GHashTable *ht)
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
	struct cxl_switch *s;
	yl_obj_t *ylo, *ylo_did;
	unsigned did;
	void * ptr; 

	ENTER

	// Initialize varialbes
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch*) user_data;

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
		ptr = calloc(s->len_devices + INITIAL_NUM_DEVICES, sizeof(struct cxl_device));

		// Copy the existing data to new buffer
		memcpy(ptr, s->devices, s->num_devices * sizeof(struct cxl_device));

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
	struct cxl_device *d;
	yl_obj_t *ylo;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	d = (struct cxl_device*) user_data;

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
			d->mld = calloc(1, sizeof(struct cxl_mld));
	
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
	struct cxl_mld *mld;
	yl_obj_t *ylo;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	mld = (struct cxl_mld*) user_data;

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
	struct cxl_device *d;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	d = (struct cxl_device*) user_data;

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
	struct cxl_switch *s;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch*) user_data;

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
	struct cxl_switch *s;
	yl_obj_t *ylo;
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	s = (struct cxl_switch*) user_data;

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
	else if (!strcmp(key, "num_decoders")) 		s->num_decoders 	= atoi(ylo->str);
	else if (!strcmp(key, "mlw")) 				s->mlw 				= atoi(ylo->str);
	else if (!strcmp(key, "speeds")) 			s->speeds 			= strtoul(ylo->str, NULL, 0);
	else if (!strcmp(key, "mls"))			 	s->mls 				= atoi(ylo->str);
	else if (!strcmp(key, "num_ports"))  		cxls_init_ports(s, atoi(ylo->str));
	else if (!strcmp(key, "num_vcss"))     		cxls_init_vcss(s, atoi(ylo->str), s->num_vppbs);
	else if (!strcmp(key, "num_vppbs"))    		cxls_init_vcss(s, s->num_vcss, atoi(ylo->str));

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
	struct cxl_port *ports;
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	ports = (struct cxl_port*) user_data;

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
	struct cxl_port *port; 
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	port = (struct cxl_port*) user_data;
	
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
	struct cxl_vcs *vcss; 
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vcss = (struct cxl_vcs*) user_data;

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
	struct cxl_vcs *vcs;

	ENTER

	// Initialize varialbes
	ylo = (yl_obj_t*) value;
	vcs = (struct cxl_vcs*) user_data;
	
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
	struct cxl_vppb *vppbs; 
	int rv, id;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vppbs = (struct cxl_vppb*) user_data;

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
	struct cxl_vppb *vppb; 
	int rv;

	ENTER

	// Initialize varialbes
	rv = 1;
	ylo = (yl_obj_t*) value;
	vppb = (struct cxl_vppb*) user_data;

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

