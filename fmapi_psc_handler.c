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

	struct port *p; 
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
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.psc_cfg_req.ppid >= cxl_state->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested PPDI exceeds number of ports present. Requested PPID: %d Present: %d\n", now, req.obj.psc_cfg_req.ppid, cxl_state->num_ports);
		goto send;
	}
	p = &cxl_state->ports[req.obj.psc_cfg_req.ppid];

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
			
			if (req.obj.psc_cfg_req.fdbe & 0x01) rsp.obj.psc_cfg_rsp.data[0] = p->cfgspace[reg+0];
			if (req.obj.psc_cfg_req.fdbe & 0x02) rsp.obj.psc_cfg_rsp.data[1] = p->cfgspace[reg+1];
			if (req.obj.psc_cfg_req.fdbe & 0x04) rsp.obj.psc_cfg_rsp.data[2] = p->cfgspace[reg+2];
			if (req.obj.psc_cfg_req.fdbe & 0x08) rsp.obj.psc_cfg_rsp.data[3] = p->cfgspace[reg+3];
		}
			break;

		case FMCT_WRITE:			// 0x01
		{
			HEX32("Write Data", *((int*)req.obj.psc_cfg_req.data));
			IFV(CLVB_ACTIONS) printf("%s ACT: Performing CXL.io Write on PPID: %d\n", now, req.obj.psc_cfg_req.ppid);

			reg = (req.obj.psc_cfg_req.ext << 8) | req.obj.psc_cfg_req.reg;

			if (req.obj.psc_cfg_req.fdbe & 0x01) p->cfgspace[reg+0] = req.obj.psc_cfg_req.data[0];
			if (req.obj.psc_cfg_req.fdbe & 0x02) p->cfgspace[reg+1] = req.obj.psc_cfg_req.data[1];
			if (req.obj.psc_cfg_req.fdbe & 0x04) p->cfgspace[reg+2] = req.obj.psc_cfg_req.data[2];
			if (req.obj.psc_cfg_req.fdbe & 0x08) p->cfgspace[reg+3] = req.obj.psc_cfg_req.data[3];
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
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	state_conv_identity(cxl_state, &rsp.obj.psc_id_rsp);

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
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	for ( i = 0, rsp.obj.psc_port_rsp.num = 0 ; i < req.obj.psc_port_req.num ; i++ ) 
	{
		id = req.obj.psc_port_req.ports[i];
		
		// Validate portid 
		if (id >= cxl_state->num_ports)
			continue;

		// Copy the data 
		state_conv_port_info(&cxl_state->ports[id], &rsp.obj.psc_port_rsp.list[i]);
		rsp.obj.psc_port_rsp.num++;
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

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API PSC Physical Port Control. PPID: %d Opcode: %d\n", now, req.obj.psc_port_ctrl_req.ppid, req.obj.psc_port_ctrl_req.opcode);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.psc_port_ctrl_req.ppid >= cxl_state->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested PPID exceeds number of ports present. Requested PPID: %d Present: %d\n", now, req.obj.psc_port_ctrl_req.ppid, cxl_state->num_ports);
		goto send;
	}
	p = &cxl_state->ports[req.obj.psc_port_ctrl_req.ppid];

	STEP // 10: Perform Action 
	switch (req.obj.psc_port_ctrl_req.opcode)
	{
		case FMPO_ASSERT_PERST:			// 0x00
			IFV(CLVB_ACTIONS) printf("%s ACT: Asserting PERST on PPID: %d\n", now, req.obj.psc_port_ctrl_req.ppid);
			
			p->perst = 0x1;
			break;

		case FMPO_DEASSERT_PERST:		// 0x01
			IFV(CLVB_ACTIONS) printf("%s ACT: Deasserting PERST on PPID: %d\n", now, req.obj.psc_port_ctrl_req.ppid);

			p->perst = 0x0;
			break;

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

