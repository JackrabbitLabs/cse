/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_vsc_handler.c
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
 * Handler for FM API VSC Generate AER Opcode
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
int fmop_vsc_aer(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct vcs *v;

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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API VSC Generate AER Event. VCSID: %d vPPBID: %d\n", now, req.obj.vsc_aer_req.vcsid, req.obj.vsc_aer_req.vppbid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.vsc_aer_req.vcsid >= cxl_state->num_vcss) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested VCSID exceeds number of VCSs present. Requested VCSID: %d Present: %d\n", now, req.obj.vsc_aer_req.vcsid, cxl_state->num_vcss);
		goto send;
	}
	v = &cxl_state->vcss[req.obj.vsc_aer_req.vcsid];

	// Validate vppbid 
	if (req.obj.vsc_aer_req.vppbid >= v->num) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested vPPBID exceeds number of vPPBs present in requested VCS. Requested vPPBID: %d Present: %d\n", now, req.obj.vsc_aer_req.vppbid, v->num);
		goto send;
	}

	STEP // 10: Perform Action 
	IFV(CLVB_ACTIONS) printf("%s ACT: Generating AER on VSCID: %d vPPBID: %d Error: 0x%08x\n", now, req.obj.vsc_aer_req.vcsid, req.obj.vsc_aer_req.vppbid, req.obj.vsc_aer_req.error_type);

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

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
 * Handler for FM API VSC Bind Opcode
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
int fmop_vsc_bind(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct vcs *v;
	struct vppb *b;
	struct port *p;

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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API VSC Bind vPPB. VCSID: %d vPPBID: %d PPID: %d LDID: 0x%04x\n", now, req.obj.vsc_bind_req.vcsid, req.obj.vsc_bind_req.vppbid, req.obj.vsc_bind_req.ppid, req.obj.vsc_bind_req.ldid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	// Validate vcsid
	if (req.obj.vsc_bind_req.vcsid >= cxl_state->num_vcss) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: VCS ID out of range. VCSID: %d\n", now, req.obj.vsc_bind_req.vcsid);
		goto send; 
	}
	v = &cxl_state->vcss[req.obj.vsc_bind_req.vcsid];
	
	// Validate vppbid 
	if (req.obj.vsc_bind_req.vppbid >= cxl_state->vcss[req.obj.vsc_bind_req.vcsid].num) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: vPPB ID out of range. vPPBID: %d\n", now, req.obj.vsc_bind_req.vppbid);
		goto send;
	}
	b = &v->vppbs[req.obj.vsc_bind_req.vppbid];

	// Validate port id 
	if (req.obj.vsc_bind_req.ppid >= cxl_state->num_ports)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: PPID ID out of range. PPID: %d\n", now, req.obj.vsc_bind_req.ppid);
		goto send;
	}
	p = &cxl_state->ports[req.obj.vsc_bind_req.ppid];	

	// Check bindability to this port 

	// Check state of port
	if (p->state == FMPS_DISABLED)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port is in a disabled state. PPID: %d State: %s\n", now, req.obj.vsc_bind_req.ppid, fmps(p->state));
		goto send;
	}

	// If an LD is specified, check if the port is connected to a Type-3 Devices 
	if (req.obj.vsc_bind_req.ldid != 0xFFFF && !(p->dt == FMDT_CXL_TYPE_3 || p->dt == FMDT_CXL_TYPE_3_POOLED) ) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Bind to an MLD LD requested and specified port is not attached to a Type 3 Device\n", now);
		goto send;
	}

	// If port is an MLD port, an LDID must be specified 
	if (p->ld > 0 && req.obj.vsc_bind_req.ldid == 0xFFFF)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Cannot bind to the physical port of an MLD device\n", now);
		goto send;
	}

	// If an LD is specified, check if the port can support multiple LDs 
	if (req.obj.vsc_bind_req.ldid != 0xFFFF && p->ld == 0) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Specified port does not support multiple Logical Devices: \n", now);
		goto send;
	}

	// Check if vPPB is aleady bound
	if (b->bind_status != FMBS_UNBOUND) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Specified vPPB is not available to be bound. vPPBID: %d STATUS: %s\n", now, req.obj.vsc_bind_req.vppbid, fmbs(b->bind_status));
		goto send;
	}

	STEP // 10: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Binding VCSID: %d vPPBID: %d PPID: %d LDID: 0x%04x\n", now, req.obj.vsc_bind_req.vcsid, req.obj.vsc_bind_req.vppbid, req.obj.vsc_bind_req.ppid, req.obj.vsc_bind_req.ldid);

	if (req.obj.vsc_bind_req.ldid != 0xFFFF) 
	{
		b->bind_status = FMBS_BOUND_LD;
		b->ppid = req.obj.vsc_bind_req.ppid;
		b->ldid = req.obj.vsc_bind_req.ldid;
	}
	else 
	{
		b->bind_status = FMBS_BOUND_PORT;
		b->ppid = req.obj.vsc_bind_req.ppid;
		b->ldid = 0;
	}

	STEP // 6: Set port state to be a downstream port
	p->state = FMPS_DSP;

	// Update Background Operation Status
	cxl_state->bos_running = 0;
	cxl_state->bos_pcnt = 100;
	cxl_state->bos_opcode = req.hdr.opcode;
	cxl_state->bos_rc = FMRC_SUCCESS;
	cxl_state->bos_ext = 0;

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_BACKGROUND_OP_STARTED;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

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
 * Handler for FM API VSC Get Virtual CXL Switch Info Opcode
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
int fmop_vsc_info(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct vcs *v;
	unsigned i, k, stop, vppbid_start, vppbid_limit;
	struct fmapi_vsc_info_blk *blk;
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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API VSC Get Virtual Switch Info. Num: %d\n", now, req.obj.vsc_info_req.num);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	rsp.obj.vsc_info_rsp.num = 0;
	vppbid_start = req.obj.vsc_info_req.vppbid_start;
	vppbid_limit = req.obj.vsc_info_req.vppbid_limit; 
	for ( i = 0 ; i < req.obj.vsc_info_req.num ; i++ ) 
	{
		id = req.obj.vsc_info_req.vcss[i];

		// Break, if we have reached the maximum number of VCS entities that can be returned 
		if (i >= FM_MAX_VCS_PER_RSP)
			break;
		
		// Skip VCS IDs that exceed current size 
		if (id >= cxl_state->num_vcss) 
			continue;

		// Get pointers to objects to copy 
		v = &cxl_state->vcss[id];	// The struct vcs to copy from 
		blk = &rsp.obj.vsc_info_rsp.list[i];			// The struct fmapi_vcs_info_blk to copy into

		// Zero out destination
		memset(blk, 0, sizeof(*blk));
	
		// Copy information 
		blk->vcsid 		= v->vcsid;	 	// Virtual CXL Switch ID
		blk->state 		= v->state;		// VCS State [FMVS]
		blk->uspid 		= v->uspid;		// USP ID. Upstream physical port ID
		blk->total		= v->num;		// Total Number of vPPBs in the VCS. 
		blk->num 		= 0;			// The number vppb blks returned in this object

		// Determine number of vPPB entires to return
		stop = v->num; 
		if ( vppbid_limit < (stop - vppbid_start) )
			stop = vppbid_start + vppbid_limit; 

		// Variable array of PPB Status Blocks
		for ( k = vppbid_start ; k < stop ; k++ ) {
			blk->list[k].status 	= v->vppbs[k].bind_status;
			blk->list[k].ppid 		= v->vppbs[k].ppid;
			blk->list[k].ldid 		= v->vppbs[k].ldid;
			blk->num++;
		}
		rsp.obj.vsc_info_rsp.num++;
	}

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

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
 * Handler for FM API VSC Unbind Opcode
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
int fmop_vsc_unbind(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	
	unsigned rc;
	int rv, len;

	struct vcs *v;
	struct vppb *b;
	struct port *p;

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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API VSC Unbind vPPB. VCSID: %d vPPBID: %d\n", now, req.obj.vsc_unbind_req.vcsid, req.obj.vsc_unbind_req.vppbid);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	// Validate vcsid
	if (req.obj.vsc_unbind_req.vcsid >= cxl_state->num_vcss) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: VCS ID out of range. VCSID: %d\n", now, req.obj.vsc_unbind_req.vcsid);
		goto send; 
	}
	v = &cxl_state->vcss[req.obj.vsc_unbind_req.vcsid];
	
	// Validate vppbid 
	if (req.obj.vsc_unbind_req.vppbid >= cxl_state->vcss[req.obj.vsc_unbind_req.vcsid].num) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: vPPB ID out of range. vPPBID: %d\n", now, req.obj.vsc_unbind_req.vppbid);
		goto send;
	}
	b = &v->vppbs[req.obj.vsc_unbind_req.vppbid];

	// Validate bind status of vppb
	if (b->bind_status == FMBS_UNBOUND || b->bind_status == FMBS_INPROGRESS) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: vPPB was not bound. vPPBID %d\n", now, req.obj.vsc_unbind_req.vppbid);
		goto send;
	}

	// Validate port id that the vppb was bound to  
	if (b->ppid >= cxl_state->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: PPID of bound port out of range. PPID: %d\n", now, b->ppid);
		b->bind_status = FMBS_UNBOUND;
		goto send;
	}
	p = &cxl_state->ports[b->ppid];	

	// Check bindability to this port 

	// Check state of port
	if ( !(p->state == FMPS_BINDING || p->state == FMPS_UNBINDING || p->state == FMPS_USP || p->state == FMPS_DSP) )
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port is not in a bound state. PPID: %d State: %s\n", now, b->ppid, fmps(p->state));
		goto send;
	}

	STEP // 10: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Unbinding VCSID: %d vPPBID: %d\n", now, req.obj.vsc_unbind_req.vcsid, req.obj.vsc_unbind_req.vppbid);

	b->bind_status = FMBS_UNBOUND;	
	b->ppid = 0;
	b->ldid = 0;
	
	// Update Background Operation Status
	cxl_state->bos_running = 0;
	cxl_state->bos_pcnt = 100;
	cxl_state->bos_opcode = req.hdr.opcode;
	cxl_state->bos_rc = FMRC_SUCCESS;
	cxl_state->bos_ext = 0;

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_BACKGROUND_OP_STARTED;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

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

