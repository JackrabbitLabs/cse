/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_psc_handler.c
 *
 * @brief 		Code file for methods to respond to FM API commands
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

#include <pci/pci.h>

/* struct timespec 
 * timespec_get()
 *
 */
#include <time.h>

/* system()
 */
#include <stdlib.h>

/* autl_prnt_buf()
 */
#include <arrayutils.h>

/* mctp_init()
 * mctp_set_mh()
 * mctp_run()
 */
#include <mctp.h>
#include <ptrqueue.h>
#include <timeutils.h>
#include <cxlstate.h>
#include "signals.h"

#include "options.h"

#include "state.h"

#include <fmapi.h>

#include "fmapi_handler.h"

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

#define IFV(u) 					if (opts[CLOP_VERBOSITY].u64 & u) 

#define ISO_TIME_BUF_LEN		32

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * Handler for FM API PSC CXL.io Configuration Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_psc_cfg(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct cxl_port *p; 
	__u16 reg; 

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API PSC CXL.io Config. PPID: %d\n", now, req.obj.psc_cfg_req.ppid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.psc_cfg_req.ppid >= cxls->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested PPDI exceeds number of ports present. Requested PPID: %d Present: %d\n", now, req.obj.psc_cfg_req.ppid, cxls->num_ports);
		goto send;
	}
	p = &cxls->ports[req.obj.psc_cfg_req.ppid];

	// Validate port is not bound or is an MLD port 
	//if ( !(p->state == FMPS_DISABLED || p->ld > 0) ) 
	//{
	//	IFV(CLVB_ERRORS) printf("%s Port is not unbound or is not an MLD Port. PPID: %d Port State: %s Num LD: %d\n", now, req.obj.psc_cfg_req.ppid, fmps(p->state), p->ld);
	//	goto send;
	//}

	STEP // 10: Perform Action 
	switch (req.obj.psc_cfg_req.type)
	{
		case FMCT_READ:				// 0x00
		{
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io Read on PPID: %d\n", now, req.obj.psc_cfg_req.ppid);

			reg = (req.obj.psc_cfg_req.ext << 8) | req.obj.psc_cfg_req.reg;

			rsp.obj.psc_cfg_rsp.data[0] = 0;
			rsp.obj.psc_cfg_rsp.data[1] = 0;
			rsp.obj.psc_cfg_rsp.data[2] = 0;
			rsp.obj.psc_cfg_rsp.data[3] = 0;
			
			if (opts[CLOP_QEMU].set == 1)
			{
				switch(req.obj.psc_cfg_req.fdbe)
				{
					case 0x01: 
					{
						__u8 b = pci_read_byte(p->dev, reg);	

						rsp.obj.psc_cfg_rsp.data[0] = b;
					} break;

					case 0x03:
					{
						// Verify word aligned 
						if ((reg & 0x1) != 0)
							goto send;

						__u16 w = pci_read_word(p->dev, reg);	

						rsp.obj.psc_cfg_rsp.data[0] = ( w      ) & 0x00FF;
						rsp.obj.psc_cfg_rsp.data[1] = ( w >> 8 ) & 0x00FF;
					} break;

					case 0x0F:
					{
						// Verify long aligned 
						if ((reg & 0x3) != 0)
							goto send;

						__u32 l = pci_read_long(p->dev, reg);	

						rsp.obj.psc_cfg_rsp.data[0] = ( l      ) & 0x00FF;
						rsp.obj.psc_cfg_rsp.data[1] = ( l >> 8 ) & 0x00FF;
						rsp.obj.psc_cfg_rsp.data[2] = ( l >> 16) & 0x00FF;
						rsp.obj.psc_cfg_rsp.data[3] = ( l >> 24) & 0x00FF;
					} break;

					default: 
						goto send;
				}
			} 
			else 
			{
				if (req.obj.psc_cfg_req.fdbe & 0x01) rsp.obj.psc_cfg_rsp.data[0] = p->cfgspace[reg+0];  
				if (req.obj.psc_cfg_req.fdbe & 0x02) rsp.obj.psc_cfg_rsp.data[1] = p->cfgspace[reg+1];
				if (req.obj.psc_cfg_req.fdbe & 0x04) rsp.obj.psc_cfg_rsp.data[2] = p->cfgspace[reg+2]; 
				if (req.obj.psc_cfg_req.fdbe & 0x08) rsp.obj.psc_cfg_rsp.data[3] = p->cfgspace[reg+3];
			}
		}
			break;

		case FMCT_WRITE:			// 0x01
		{
			HEX32("Write Data", *((int*)req.obj.psc_cfg_req.data));
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io Write on PPID: %d\n", now, req.obj.psc_cfg_req.ppid);

			reg = (req.obj.psc_cfg_req.ext << 8) | req.obj.psc_cfg_req.reg;

			if (opts[CLOP_QEMU].set == 1)
			{
				switch(req.obj.psc_cfg_req.fdbe)
				{
					case 0x01:
					{
						pci_write_byte(p->dev, reg, req.obj.psc_cfg_req.data[0]);
					} break;

					case 0x03:
					{
						// Verify word aligned 
						if ((reg & 0x1) != 0)
							goto send;

						__u16 w =  (req.obj.psc_cfg_req.data[1] << 8) 
						          | req.obj.psc_cfg_req.data[0];

						pci_write_word(p->dev, reg, w);
					} break;

					case 0x0F:
					{
						// Verify long aligned 
						if ((reg & 0x3) != 0)
							goto send;

						__u32 l = (req.obj.psc_cfg_req.data[3] << 24) 
						         |(req.obj.psc_cfg_req.data[2] << 16)
						         |(req.obj.psc_cfg_req.data[1] <<  8)
						         |(req.obj.psc_cfg_req.data[0]      );

						pci_write_long(p->dev, reg, l);
					} break;

					default: 
						goto send;
				}
			}
			else
			{
				if (req.obj.psc_cfg_req.fdbe & 0x01) p->cfgspace[reg+0] = req.obj.psc_cfg_req.data[0];
				if (req.obj.psc_cfg_req.fdbe & 0x02) p->cfgspace[reg+1] = req.obj.psc_cfg_req.data[1];
				if (req.obj.psc_cfg_req.fdbe & 0x04) p->cfgspace[reg+2] = req.obj.psc_cfg_req.data[2];
				if (req.obj.psc_cfg_req.fdbe & 0x08) p->cfgspace[reg+3] = req.obj.psc_cfg_req.data[3];
			}
		}
			break;
	}

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxls->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API PSC Identify Switch Device Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_psc_id(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API PSC Identify Switch Device\n", now);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	{
		struct cxl_switch *cs 		= cxls;
		struct fmapi_psc_id_rsp *fi = &rsp.obj.psc_id_rsp;

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
			for ( int j = 0 ; j < cs->vcss[i].num ; j++ ) {
				if ( cs->vcss[i].vppbs[j].bind_status != FMBS_UNBOUND ) 
					fi->active_vppbs++;
			}
		}
	}

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxls->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API PSC Get Physical Port State Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_psc_port(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	int i;
	__u8 id;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API PSC Get Physical Port Status. Num: %d\n", now, req.obj.psc_port_req.num);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	for ( i = 0, rsp.obj.psc_port_rsp.num = 0 ; i < req.obj.psc_port_req.num ; i++ ) 
	{
		id = req.obj.psc_port_req.ports[i];
		
		// Validate portid 
		if (id >= cxls->num_ports)
			continue;

		// Copy the data 
		struct cxl_port *src 			= &cxls->ports[id];
		struct fmapi_psc_port_info *dst = &rsp.obj.psc_port_rsp.list[i];

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

		rsp.obj.psc_port_rsp.num++;
	}

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxls->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API PSC Physical Port Control Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_psc_port_ctrl(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct cxl_port *p;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API PSC Physical Port Control. PPID: %d Opcode: %d\n", now, req.obj.psc_port_ctrl_req.ppid, req.obj.psc_port_ctrl_req.opcode);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.psc_port_ctrl_req.ppid >= cxls->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested PPID exceeds number of ports present. Requested PPID: %d Present: %d\n", now, req.obj.psc_port_ctrl_req.ppid, cxls->num_ports);
		goto send;
	}
	p = &cxls->ports[req.obj.psc_port_ctrl_req.ppid];

	STEP // 10: Perform Action 
	switch (req.obj.psc_port_ctrl_req.opcode)
	{
		case FMPO_ASSERT_PERST:			// 0x00
		{
			char cmd[64];
			sprintf(cmd, "echo 0 > /sys/bus/pci/slots/%d/power", p->ppid);

			IFV(CLVB_ACTIONS) printf("%s ACT: Asserting PERST on PPID: %d\n", now, req.obj.psc_port_ctrl_req.ppid);

			// Disable the device 
			if ( opts[CLOP_QEMU].set == 1 )
				rv = system(cmd);
			
			// Set PERST bit 
			p->perst = 0x1;
		} break;

		case FMPO_DEASSERT_PERST:		// 0x01
		{
			char cmd[64];
			sprintf(cmd, "echo 1 > /sys/bus/pci/slots/%d/power", p->ppid);

			IFV(CLVB_ACTIONS) printf("%s ACT: Deasserting PERST on PPID: %d\n", now, req.obj.psc_port_ctrl_req.ppid);

			// Enable the device 
			if ( opts[CLOP_QEMU].set == 1 )
				rv = system(cmd);

			p->perst = 0x0;
		} break;

		case FMPO_RESET_PPB:			// 0x02
			IFV(CLVB_ACTIONS) printf("%s ACT: Resetting PPID: %d\n", now, req.obj.psc_port_ctrl_req.ppid);

			break;

		default:  
			IFV(CLVB_ERRORS) printf("%s ERR: Invalid port control action Opcode. Opcode: 0x%04x\n", now, req.obj.psc_port_ctrl_req.opcode);
			goto end;
	}

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxls->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

