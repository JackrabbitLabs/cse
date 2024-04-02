/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		state.h
 *
 * @brief 		Header file to manage the CXL switch state 
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */
/* INCLUDES ==================================================================*/

#ifndef _STATE_H
#define _STATE_H

/* __u8
 * __u16
 */
#include <linux/types.h>

/* pthread_mutex_t
 */
#include <pthread.h>

#include <fmapi.h> 

/* MACROS ====================================================================*/

#define MAX_LD 				16
#define MAX_PORTS 			256
#define MAX_VCSS 			MAX_PORTS
#define MAX_VPPBS_PER_VCS  	256
#define MAX_VPPBS 			MAX_PORTS * MAX_LD

#define MAX_INDENT 			32
#define INDENT 				2
#define CFG_SPACE_SIZE      4096
#define MAX_FILE_NAME_LEN	256

#define INITIAL_NUM_DEVICES 32

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/


/**
 * Multi Logical Device Object*
 *
 * This device aggregates all the descriptors for a CXL MLD Logical Device 
 *
 * CXL 2.0 v1.0 Table 111,112,113,116,117,118,119
 */
struct mld {

	/* LD Info: Table 111*/
	__u64 memory_size; 				//!< Total device memory capacity
	__u16 num;						//!< Number of Logical Devices supported
	__u8 epc;						//!< Egress Port Congestion Supported 
	__u8 ttr;						//!< Temporary Throughput Reduction Supported 

	/* LD Allocations: Table 112,113 */
	__u8 granularity;				//!< Memory Granularity [FMMG]
	__u64 rng1[FM_MAX_NUM_LD]; 		//!< Range 1 Allocation Multiplier
	__u64 rng2[FM_MAX_NUM_LD];		//!< Range 2 Allocation Multiplier

	/* LD QoS Control parameters: Table 116*/
	__u8 epc_en; //!< QoS Telem: Egress Port Congestion Enable. Bitfield [FMQT]
	__u8 ttr_en; //!< QoS Telem: Temporary Throuhput Reduction Enable. Bitfield [FMQT]
	__u8 egress_mod_pcnt;	//!< Egress Moderate Percentage: Threshold in percent for Egress Port Congestion mechanism to indicate moderate congestion. Valid range is 1-100. Default is 10.
	__u8 egress_sev_pcnt;	//!< Egress Severe Percentage: Threshold in percent for Egress Port Congestion mechanism to indicate severe congestion. Valid range is 1-100. Default is 25
	__u8 sample_interval;	//!< Backpressure Sample Interval: Interval in ns for Egress Port Congestion mechanism to take samples. Valid range is 0-15. Default is 8 (800 ns of history). Value of 0 disables the mechanism.
	__u16 rcb;				//!< ReqCmpBasis. Estimated maximum sustained sum of requests and recent responses across the entire device, serving as the basis for QoS Limit Fraction. Valid range is 0-65,535. Value of 0 disables the mechanism. Default is 0.
	__u8 comp_interval;				//!< Completion Collection Interval: Interval in ns for Completion Counting mechanism to collect the number of transmitted responses in a single counter. Valid range is 0-255. Default is 64

	/* LD QoS Status: Table 117*/
	__u8 bp_avg_pcnt;				//!< Backpressure Average Percentage: Current snapshot of the measured Egress Port average congestion. Table 117

	/* LD QoS Allocated BW Fractions: Table 118 */
	__u8 alloc_bw[FM_MAX_NUM_LD];

	/* LD QoS BW Limit Fractions: Table 119 */
	__u8 bw_limit[FM_MAX_NUM_LD];

	__u8 *cfgspace[FM_MAX_NUM_LD];	//!< Buffers representing PCIe config space for each logical device

	__u8 mmap;						//!< Direction to mmap a file for the memory space
	char *file;						//!< Filename for mmaped file 
	__u8 *memspace;					//!< Buffer representing memory space for entire logical device
};

/**
 * Virtual PCIe-to-PCIe Bridge Object 
 *
 * CXL 2.0 v1.0 Table 99
 */
struct vppb {
	__u16 vppbid;				//!< Index of this vPPB in the state->vppbs[] array
	__u8 bind_status;		//!< PBB Binding Status [FMBS]
	__u8 ppid;				//!< Physical port number of bound port
	__u16 ldid;				//!< ID of LD bound to port from MLD on associated physical port
};

/**
 * Virtual CXL Switch Object
 * 
 * CXL 2.0 v1.0 Table 99
 */
struct vcs {
	__u8 vcsid;				//!< VCS ID - Index of this vcs in the state->vcss[] array
	__u8 state; 			//!< Virtual CXL switch State [FMVS]
	__u8 uspid; 			//!< USP Physical Port ID
	__u8 num;				//!< Number of vPPBs
	
	//!< Array of pointers to vPPB objects
	struct vppb vppbs[MAX_VPPBS_PER_VCS];	
};

/**
 * CXL Switch Port Object
 *
 * CXL 2.0 v1.0 Table 92
 */
struct port {
	__u8 ppid;				//!< Port ID - Index of this port in the state->ports[] array
	__u8 state;				//!< Current Port Configuration State [FMPS]
    __u8 dv;				//!< Connected Device CXL version [FMDV]
    __u8 dt;				//!< Connected device type [FMDT]
    __u8 cv;				//!< Connected CXL version bitmask [FMVC]
    __u8 mlw;				//!< Max Link Width. Integer number of lanes (1,2,4,8,16)
    __u8 nlw;				//!< Negotiated Link Width [FMNW]
    __u8 speeds;			//!< Supported Link Speeds Vector [FMSS]
    __u8 mls;				//!< Maximum Link Speed [FMMS]
    __u8 cls;				//!< Current Link Speed [FMMS]
    __u8 ltssm;				//!< LTSSM State [FMLS]
	__u8 lane;				//!< First negotiated lane number (Integer lane number)

    /** Link State Flags [FMLF] [FMLO] */ 
	__u8 lane_rev; 	//!< Lane reversal state. 0=standard, 1=rev [FMLO]
	__u8 perst;		//!< PCIe Reset State PERST#	
	__u8 prsnt;		//!< Port Presence pin state PRSNT#
	__u8 pwrctrl;	//!< Power Control State (PWR_CTRL)
	
    __u8 ld;				//!< Additional supported LD Count (beyond 1)
	__u8 *cfgspace;			//!< Buffer representing PCIe config space
	struct mld *mld;		//!< State for MLD
	char *device_name;		//!< Name of device used to populate this port
};

struct cse_device 
{
	char *name;				//!< Name of device 
	__u8 rootport;			//!< Root Port Device. 1=root, 2=endpoint
    __u8 dv;				//!< Connected Device CXL version [FMDV]
    __u8 dt;				//!< Connected device type [FMDT]
    __u8 cv;				//!< Connected CXL version bitmask [FMVC]
    __u8 mlw;				//!< Maximum Link Width. Integer number of lanes (1,2,4,8,16)
    __u8 mls;				//!< Maximum Link Speed [FMMS]
	__u8 *cfgspace;			//!< Buffer representing PCIe config space
	struct mld *mld;		//!< MLD info if this is an MLD
};

/**
 * CXL Switch State Identify Information
 *
 * CXL 2.0 v1 Table 89
 */
struct cxl_switch_state { 
	__u8 version; 			//!< Device Management Version

	__u16 vid; 				//!< PCIe Vendor ID 
	__u16 did; 				//!< PCIe Device ID 
	__u16 svid;				//!< PCIe Subsystem Vendor ID 
	__u16 ssid; 			//!< PCIe Subsystem ID 
	__u64 sn; 				//!< Device Serial Number
	__u8 max_msg_size_n;	//!< Max fmapi msg size. 2^n

	__u8 msg_rsp_limit_n;	//!< Message Response Limit n of 2^n

	__u8 bos_running;		//!< Background operation status 0=none, 1=running
	__u8 bos_pcnt;			//!< Background operation percent complete [0-100]
	__u16 bos_opcode;		//!< Background operation opcode
	__u16 bos_rc;			//!< Background operation return code
	__u16 bos_ext;			//!< Background operation Extended Vendor Status 

	__u8 ingress_port;		//!< Ingress Port ID 
	__u8 num_ports;			//!< Total number of physical ports
	__u8 num_vcss; 			//!< Max number of VCSs
	__u16 num_vppbs;		//!< Max number of vPPBs 
	__u16 active_vppbs;		//!< Number of active vPPBs
	__u8 num_decoders;		//!< Number of HDM decoders available per USP 

	struct port *ports;		//!< array of Port objects
	struct vcs *vcss;		//!< array of VCS objects

	struct cse_device *devices; //!< array of device definitions 
	__u16 len_devices;		//!< Number of entries supported in devices array
	__u16 num_devices;		//!< Number of entries in devices array

	/* Port defaults */
    __u8 mlw;				//!< Max Link Width. Integer number of lanes (1,2,4,8,16)
    __u8 speeds;			//!< Supported Link Speeds Vector [FMSS]
    __u8 mls;				//!< Maximum Link Speed [FMMS]
	char *dir;				//!< Filepath to directory for instantiated memory
	
	pthread_mutex_t mtx;	//!< Mutex to control access to this object
};


/* PROTOTYPES ================================================================*/

struct cxl_switch_state *state_init(unsigned ports, unsigned vcss, unsigned vppbs);
int state_load(struct cxl_switch_state *s, char *filename);
void state_free(struct cxl_switch_state *s);

int state_connect_device(struct port *p, struct cse_device *d);
int state_disconnect_device(struct port *p);

/* Conversion  Functions */
void state_conv_identity(struct cxl_switch_state *src, struct fmapi_psc_id_rsp *dst);
void state_conv_port_info(struct port *src, struct fmapi_psc_port_info *dst);
void state_conv_vcs_info(struct vcs *src, struct fmapi_vsc_info_blk *dst);

/* Print Functions */
void state_print(struct cxl_switch_state *s);
void state_print_identity(struct cxl_switch_state *s, unsigned indent);
void state_print_ports(struct cxl_switch_state *s, unsigned indent);
void state_print_port(struct port *p, unsigned indent);
void state_print_vcss(struct cxl_switch_state *s, unsigned indent);
void state_print_vcs(struct vcs *v, unsigned indent);
void state_print_vppb(struct vppb *b, unsigned indent);
void state_print_mld(struct mld *mld, unsigned indent);

void state_print_devices(struct cxl_switch_state *s);

/* GLOBAL VARIABLES ==========================================================*/

extern struct cxl_switch_state *cxl_state;

#endif //ifndef _STATE_H
