/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_mpc_handler.c
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

/* struct timespec 
 * timespec_get()
 *
 */
#include <time.h>

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
#include <arrayutils.h>
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

int fmop_mcc_get_ld_alloc	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_get_qos_alloc	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_get_qos_ctrl	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_get_qos_limit	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_get_qos_stat	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_info			(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_set_ld_alloc	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_set_qos_alloc	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_set_qos_ctrl	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);
int fmop_mcc_set_qos_limit	(struct cxl_port *p, struct fmapi_msg *req, struct fmapi_msg *rsp);

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * Handler for FM API MPC LD CXL.io Configuration Opcode
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
int fmop_mpc_cfg(struct mctp *m, struct mctp_action *ma)
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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MPC LD CXL.io Config. PPID: %d  LDID: %d\n", now, req.obj.mpc_cfg_req.ppid, req.obj.mpc_cfg_req.ldid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 

	// Validate port number 
	if (req.obj.mpc_cfg_req.ppid >= cxls->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Invalid Port number requested. PPID: %d\n", now, req.obj.mpc_cfg_req.ppid);
		goto send;
	}
	p = &cxls->ports[req.obj.mpc_cfg_req.ppid];

	// Validate port is not bound 
	//if ( !(p->state == FMPS_DISABLED) ) 
	//{ 
	//	IFV(CLVB_ERRORS) printf("%s ERR: Port is in a bound state. PPID: %d State: %s\n", now, req.obj.mpc_cfg_req.ppid, fmps(p->state));
	//	goto send;
	//}

	// Validate device attached to port is an MLD port
	if ( !(p->dt == FMDT_CXL_TYPE_3 || p->dt == FMDT_CXL_TYPE_3_POOLED) ) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port is not Type 3 device: Type: %s\n", now, fmdt(p->dt));
		goto send;
	}

	// Validate LDID 
	if (req.obj.mpc_cfg_req.ldid >= p->ld) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested LD ID exceeds supported LD count of specified port. Requested LDID: %d\n", now, req.obj.mpc_cfg_req.ldid);
		goto send;
	}

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	switch (req.obj.mpc_cfg_req.type)
	{
		case FMCT_READ:				// 0x00
		{
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io Read on PPID: %d LDID: %d\n", now, req.obj.mpc_cfg_req.ppid, req.obj.mpc_cfg_req.ldid);

			reg = (req.obj.mpc_cfg_req.ext << 8) | req.obj.mpc_cfg_req.reg;

			rsp.obj.mpc_cfg_rsp.data[0] = 0;
			rsp.obj.mpc_cfg_rsp.data[1] = 0;
			rsp.obj.mpc_cfg_rsp.data[2] = 0;
			rsp.obj.mpc_cfg_rsp.data[3] = 0;

			if (req.obj.mpc_cfg_req.fdbe & 0x01) rsp.obj.mpc_cfg_rsp.data[0] = p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+0];
			if (req.obj.mpc_cfg_req.fdbe & 0x02) rsp.obj.mpc_cfg_rsp.data[1] = p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+1];
			if (req.obj.mpc_cfg_req.fdbe & 0x04) rsp.obj.mpc_cfg_rsp.data[2] = p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+2];
			if (req.obj.mpc_cfg_req.fdbe & 0x08) rsp.obj.mpc_cfg_rsp.data[3] = p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+3];
		}
			break;

		case FMCT_WRITE:			// 0x01
		{
			HEX32("Write Data",  *((int*)req.obj.mpc_cfg_req.data));
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io Write on PPID: %d LDID: %d\n", now, req.obj.mpc_cfg_req.ppid, req.obj.mpc_cfg_req.ldid);

			reg = (req.obj.mpc_cfg_req.ext << 8) | req.obj.mpc_cfg_req.reg;

			if (req.obj.mpc_cfg_req.fdbe & 0x01) p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+0] = req.obj.mpc_cfg_req.data[0];
			if (req.obj.mpc_cfg_req.fdbe & 0x02) p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+1] = req.obj.mpc_cfg_req.data[1];
			if (req.obj.mpc_cfg_req.fdbe & 0x04) p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+2] = req.obj.mpc_cfg_req.data[2];
			if (req.obj.mpc_cfg_req.fdbe & 0x08) p->mld->cfgspace[req.obj.mpc_cfg_req.ldid][reg+3] = req.obj.mpc_cfg_req.data[3];
		}
			break;

		default:  
			IFV(CLVB_ERRORS) printf("%s ERR: Invalid Action\n", now);
			goto end;
	}

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
 * Handler for FM API MPC LD CXL.io Memory Opcode
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
int fmop_mpc_mem(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct cxl_port *p;
	__u64 base, max, ld_size, granularity; 

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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MPC LD CXL.io Mem. PPID: %d  LDID: %d\n", now, req.obj.mpc_mem_req.ppid, req.obj.mpc_mem_req.ldid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 

	// Validate port number 
	if (req.obj.mpc_mem_req.ppid >= cxls->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Invalid Port number requested. PPID: %d\n", now, req.obj.mpc_mem_req.ppid);
		goto send;
	}
	p = &cxls->ports[req.obj.mpc_mem_req.ppid];

	// Validate port is not bound 
	//if ( !(p->state == FMPS_DISABLED) ) 
	//{ 
	//	IFV(CLVB_ERRORS) printf("%s ERR: Port is in a bound state: %s PPID: %d\n", now, fmps(p->state), req.obj.mpc_mem_req.ppid);
	//	goto send;
	//}

	// Validate device attached to port is an MLD port
	if ( !(p->dt == FMDT_CXL_TYPE_3 || p->dt == FMDT_CXL_TYPE_3_POOLED) ) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port is not Type 3 device. Requested Type: %s\n", now, fmdt(p->dt));
		goto send;
	}

	// Validate LDID 
	if (req.obj.mpc_mem_req.ldid >= p->ld) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested LD ID exceeds supported LD count of specified port. LDID: %d\n", now, req.obj.mpc_mem_req.ldid);
		goto send;
	}

	// Validate memory backed file is mmaped 
	if (p->mld == NULL || p->mld->memspace == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested port does not have memory space on the specified port. Port: %d\n", now, p->ppid);

		rc = FMRC_UNSUPPORTED;
		goto send;
	}

	// Validate offset & length
	if (req.obj.mpc_mem_req.len > 4096) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested length exceeds maximum length supported (4096B). Requested Len: %d\n", now, req.obj.mpc_mem_req.len);
		goto send;
	}

	// Get granularity in bytes
	granularity = 1024*1024;
	switch (p->mld->granularity)
	{
		case FMMG_256MB: 	granularity *= 256; 	break;
		case FMMG_512MB: 	granularity *= 512; 	break;
		case FMMG_1GB:   	granularity *= 1024; 	break;
	}

	// compute size of requested LD 
	base = granularity *  p->mld->rng1[req.obj.mpc_mem_req.ldid]; 		// base is the byte offset into the memspace 
	max  = granularity * (p->mld->rng2[req.obj.mpc_mem_req.ldid] + 1); // max is the byte offset start of the next LD in the memspace 
	ld_size = max - base;								// ld size in bytes 
	
	// Verify requested offset + len does not exceed the end of the LD 
	if ( (req.obj.mpc_mem_req.offset + req.obj.mpc_mem_req.len) >= ld_size) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested offset + length exceeds maximum size of LD. LD Max size (Bytes): %llu. Requested up to Byte: %llu\n", now, ld_size, req.obj.mpc_mem_req.offset + req.obj.mpc_mem_req.len);
		goto send;
	}

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	switch (req.obj.mpc_mem_req.type)
	{
		case FMCT_READ:				// 0x00
			INT32("Request Len", req.obj.mpc_mem_req.len);
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io MEM Read on PPID: %d LDID: %d\n", now, req.obj.mpc_mem_req.ppid, req.obj.mpc_mem_req.ldid);

			rsp.obj.mpc_mem_rsp.len = req.obj.mpc_mem_req.len;
			memcpy(rsp.obj.mpc_mem_rsp.data, &p->mld->memspace[base + req.obj.mpc_mem_req.offset], req.obj.mpc_mem_req.len);

			break;

		case FMCT_WRITE:			// 0x01
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io MEM Write on PPID: %d LDID: %d\n", now, req.obj.mpc_mem_req.ppid, req.obj.mpc_mem_req.ldid);

			memcpy(&p->mld->memspace[base + req.obj.mpc_mem_req.offset], req.obj.mpc_mem_req.data, req.obj.mpc_mem_req.len);

			autl_prnt_buf(req.obj.mpc_mem_req.data, req.obj.mpc_mem_req.len, 4, 0);

			break;
	}

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
 * Handler for FM API MPC Tunnel Management Command Opcode
 *
 * @param hdr	fmapi_hdr* 
 * @param src	__u8* to Request FM API Message Payload in serialized form
 * @param dst 	__u8* to Respnse FM API Message Payload in serialized form 
 * @return 		1 to send reponse back to requestor, 0 to not send it
 *
 * STEPS
 * 1: Deserialize FM API Request Payload 
 * 2: Validate port number 
 * 3: Validate device attached to port is an MLD port
 * 4: Confirm MCTP Message Type 
 * 5: Extract FM API HDR and switch on mesage opcode
 * 6: Verify FM API Message is a request
 * 7: Deserialize payload into buffer 
 * 8: Perform Requested Action
 * 9: Serialize FM API Payload 
 * 10: Fill Response FM API HDR 
 * 11: Serialize FM API Header 
 * 12: Set MCTP Type 
 * 13: Serialize FM API Response Payload 
 * 14: Set return code
 */
int fmop_mpc_tmc(struct mctp *m, struct mctp_action *ma)
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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MPC Tunneled Management Command. PPID: %d\n", now, req.obj.mpc_tmc_req.ppid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxls->mtx);

	STEP // 9: Validate Inputs 

	// Validate MCTP Message Type 
	if (req.obj.mpc_tmc_req.type != MCMT_CXLCCI) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Tunneled command did not have a CXL CCI MCTP Type code. Tunneled MCTP Type code: %d\n", now, req.obj.mpc_tmc_req.type);
		goto send;
	}

	// Validate port number 
	if (req.obj.mpc_tmc_req.ppid >= cxls->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s Invalid Port number requested. PPID: %d\n", now, req.obj.mpc_tmc_req.ppid);
		goto send;
	}
	p = &cxls->ports[req.obj.mpc_tmc_req.ppid];

	// Validate device attached to port is an MLD port
	if ( !(p->dt == FMDT_CXL_TYPE_3 || p->dt == FMDT_CXL_TYPE_3_POOLED) ) 
	{
		IFV(CLVB_ERRORS) printf("%s Port is not Type 3 device. Type: %s\n", now, fmdt(p->dt));
		goto send;
	}

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	{
		struct fmapi_msg src, dst;

		// Configure Buffer pointers
		src.buf = (struct fmapi_buf*) req.obj.mpc_tmc_req.msg;
		dst.buf = (struct fmapi_buf*) rsp.obj.mpc_tmc_rsp.msg;

		// Deserialize Sub Header
		fmapi_deserialize(&src.hdr, src.buf->hdr, FMOB_HDR, 0);
	
		// Verify sub message is a request
		if (src.hdr.category != FMMT_REQ) 
		{
			IFV(CLVB_ERRORS) printf("%s ERR: Tunneled FM API Message Category is not a request. Tunneled FM API Message Category: %d\n", now, src.hdr.category);

			// Fill Sub Header 
			len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, src.hdr.tag, src.hdr.opcode, 0, 0, FMRC_INVALID_INPUT, 0);

			// Serialize Sub Header 
			fmapi_serialize(dst.buf->hdr, &dst.hdr, FMOB_HDR);

			goto sub;
		}

		// Handle Opcode 
		switch (src.hdr.opcode)
		{
			case FMOP_MCC_INFO: 			len = fmop_mcc_info         (p, &src, &dst); break; // 0x5400
			case FMOP_MCC_ALLOC_GET: 		len = fmop_mcc_get_ld_alloc (p, &src, &dst); break; // 0x5401
			case FMOP_MCC_ALLOC_SET: 		len = fmop_mcc_set_ld_alloc (p, &src, &dst); break; // 0x5402
			case FMOP_MCC_QOS_CTRL_GET: 	len = fmop_mcc_get_qos_ctrl (p, &src, &dst); break; // 0x5403
			case FMOP_MCC_QOS_CTRL_SET: 	len = fmop_mcc_set_qos_ctrl (p, &src, &dst); break; // 0x5404
			case FMOP_MCC_QOS_STAT: 		len = fmop_mcc_get_qos_stat (p, &src, &dst); break; // 0x5405
			case FMOP_MCC_QOS_BW_ALLOC_GET: len = fmop_mcc_get_qos_alloc(p, &src, &dst); break; // 0x5406
			case FMOP_MCC_QOS_BW_ALLOC_SET: len = fmop_mcc_set_qos_alloc(p, &src, &dst); break; // 0x5407
			case FMOP_MCC_QOS_BW_LIMIT_GET: len = fmop_mcc_get_qos_limit(p, &src, &dst); break; // 0x5408
			case FMOP_MCC_QOS_BW_LIMIT_SET: len = fmop_mcc_set_qos_limit(p, &src, &dst); break; // 0x5409
			default:  
				IFV(CLVB_ERRORS) printf("%s ERR: Tunneled FM API Mesage has an invalid opcode. Tunneled FM API Message Opcode %d\n", now, src.hdr.opcode);
				
				// Fill Sub Header 
				len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, src.hdr.tag, src.hdr.opcode, 0, 0, FMRC_UNSUPPORTED, 0);

				// Serialize Sub Header 
				fmapi_serialize(dst.buf->hdr, &dst.hdr, FMOB_HDR);
				break;
		}

sub:

		// Fill Response Object
		rsp.obj.mpc_tmc_rsp.len = len;
		rsp.obj.mpc_tmc_rsp.type = req.obj.mpc_tmc_req.type;
	}

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

