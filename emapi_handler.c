/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		emapi_handler.c
 *
 * @brief 		Code file for methods to respond to CXL Emulator API commands
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Feb 2024
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
#include <emapi.h>

#include "signals.h"

#include "options.h"

#include "state.h"

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

#define ISO_TIME_BUF_LEN 	32

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

static int emop_conn_dev   (struct mctp *m, struct mctp_action *ma);
static int emop_disconn_dev(struct mctp *m, struct mctp_action *ma);
static int emop_list_dev   (struct mctp *m, struct mctp_action *ma);
static int emop_unsupported(struct mctp *m, struct mctp_action *ma);

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * Handler for all CXL Emulator API Opcodes
 * 
 * @return 	0 upon success, 1 otherwise 
 *
 * STEPS 
 * 1: Deserialize Header
 * 2: Verify EM API Message Type
 * 3: Handle Opcode
 */
int emapi_handler(struct mctp *m, struct mctp_action *ma)
{
	INIT
	struct emapi_hdr hdr; 
	int rv;

	ENTER

	// Initialize variables 
	rv = 1;

	STEP // 1: Deserialize Header
	if ( emapi_deserialize(&hdr, ma->req->payload, EMOB_HDR, NULL) == 0 )
		goto fail;

	STEP // 2: Verify EM API Message Type
	if (hdr.type != EMMT_REQ) 
		goto fail;

	STEP // 3: Handle Opcode
	HEX32("Opcode",  hdr.opcode);
	switch(hdr.opcode)
	{
		case EMOP_EVENT: 						// 0x00
			break;

		case EMOP_LIST_DEV:						// 0x01
			rv = emop_list_dev(m, ma);
			break;

		case EMOP_CONN_DEV:						// 0x02
			rv = emop_conn_dev(m, ma);
			break;

		case EMOP_DISCON_DEV: 					// 0x03
			rv = emop_disconn_dev(m, ma);
			break;

		default: 
			rv = emop_unsupported(m, ma);
			break;
	}

	rv = 0;
	goto end;

fail:

	ma->completion_code = 1;
	pq_push(m->acq, ma);

end:

	EXIT(rv)

	return rv; 
}

/**
 * Handler for EM API Connect Device Command 
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
static int emop_conn_dev(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct emapi_msg reqm, rspm;
	struct emapi_buf *reqb, *rspb;
	unsigned rc;
	int rv, len;
	__u8 ppid, dev;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto fail;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	reqb = (struct emapi_buf*) ma->req->payload;
	rspb = (struct emapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( emapi_deserialize(&reqm.hdr, reqb->hdr, EMOB_HDR, NULL) <= 0 )
		goto fail;

	STEP // 6: Deserialize Request Object 
	if ( emapi_deserialize(&reqm.obj, reqb->payload, emapi_emob_req(reqm.hdr.opcode), NULL) < 0 )
		goto fail;

	STEP // 7: Extract parameters
	ppid = reqm.hdr.a;
	dev  = reqm.hdr.b; 

	IFV(CLVB_COMMANDS) printf("%s CMD: EM API Connect Device. PPID: %d Device: %d\n", now, ppid, dev);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (ppid >= cxl_state->num_ports)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: PPID out of range. PPID: %d Total: %d\n", now, ppid, cxl_state->num_ports);
		goto send;
	}
	
	if (dev >= cxl_state->num_devices) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Device ID out of range. Device ID: %d Total: %d\n", now, dev, cxl_state->num_devices);
		goto send;
	}
	
	if (cxl_state->devices[dev].name == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Device is NULL. Device ID: %d\n", now, dev);
		goto send;
	}

	IFV(CLVB_ACTIONS) printf("%s ACT: Connecting Device %d to PPID %d\n", now, dev, ppid);

	STEP // 10: Perform Action 
	state_connect_device(&cxl_state->ports[ppid], &cxl_state->devices[dev]);	

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = emapi_serialize(rspb->payload, &rspm.obj, emapi_emob_rsp(reqm.hdr.opcode), NULL);

	STEP // 13: Set return code
	rc = EMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	if(len < 0)
		goto fail;

	STEP // 15: Fill Response Header
	ma->rsp->len = emapi_fill_hdr(&rspm.hdr, EMMT_RSP, reqm.hdr.tag, rc, reqm.hdr.opcode, len, 0, 0);

	STEP // 16: Serialize Header 
	emapi_serialize(rspb->hdr, &rspm.hdr, EMOB_HDR, NULL);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;
	goto end;

fail:

	ma->completion_code = 1;	
	pq_push(m->acq, ma);

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for EM API Disconnect Device Command 
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
static int emop_disconn_dev(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct emapi_msg reqm, rspm;
	struct emapi_buf *reqb, *rspb;
	unsigned rc;
	int rv, len;
	__u8 ppid, all, start, end, i;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto fail;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	STEP // 4: Set buffer pointers 
	reqb = (struct emapi_buf*) ma->req->payload;
	rspb = (struct emapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( emapi_deserialize(&reqm.hdr, reqb->hdr, EMOB_HDR, NULL) <= 0 )
		goto fail;

	STEP // 6: Deserialize Request Object 
	if ( emapi_deserialize(&reqm.obj, reqb->payload, emapi_emob_req(reqm.hdr.opcode), NULL) < 0 )
		goto fail;

	STEP // 7: Extract parameters
	ppid = reqm.hdr.a;
	all  = reqm.hdr.b; 

	IFV(CLVB_COMMANDS) printf("%s CMD: EM API Disconnect Device. PPID: %d All: %d\n", now, ppid, all);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (all) {
		start = 0;
		end = cxl_state->num_ports;
	}
	else {
		start = ppid;
		end = ppid+1;
	}

	if (start >= cxl_state->num_ports) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: PPID out of range. PPID: %d Total: %d\n", now, ppid, cxl_state->num_ports);
		goto send;
	}

	STEP // 10: Perform Action 
	for ( i = start ; i < end ; i++ )
	{
		// Validate if port is connected 
		if (cxl_state->ports[i].prsnt == 1) 
		{
			IFV(CLVB_ACTIONS) printf("%s ACT: Disconnecting PPID %d\n", now, i);

			// Perform disconnect
			state_disconnect_device(&cxl_state->ports[i]);	
		}
	}

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object
	len = emapi_serialize(rspb->payload, &rspm.obj, emapi_emob_rsp(reqm.hdr.opcode), NULL);

	STEP // 13: Set return code
	rc = EMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	if (len < 0)
		goto fail;

	STEP // 15: Fill Response Header
	ma->rsp->len = emapi_fill_hdr(&rspm.hdr, EMMT_RSP, reqm.hdr.tag, rc, reqm.hdr.opcode, len, 0, 0);

	STEP // 16: Serialize Header 
	emapi_serialize(rspb->hdr, &rspm.hdr, EMOB_HDR, NULL);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;
	goto end;

fail:

	ma->completion_code = 1;	
	pq_push(m->acq, ma);

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for EM API List Devices Opcode
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
static int emop_list_dev(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct emapi_msg reqm, rspm;
	struct emapi_buf *reqb, *rspb;
	unsigned rc;
	int rv, len;

	unsigned i, count;
	__u8 num_requested, start_num; 
	struct cse_device *d;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	count = 0;

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto fail;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	STEP // 4: Set buffer pointers 
	reqb = (struct emapi_buf*) ma->req->payload;
	rspb = (struct emapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( emapi_deserialize(&reqm.hdr, reqb->hdr, EMOB_HDR, NULL) <= 0 )
		goto fail;

	STEP // 6: Deserialize Request Object 
	if ( emapi_deserialize(&reqm.obj, reqb->payload, emapi_emob_req(reqm.hdr.opcode), NULL) < 0 )
		goto fail;

	STEP // 7: Extract parameters
	num_requested = reqm.hdr.a;
	start_num     = reqm.hdr.b; 

	IFV(CLVB_COMMANDS) printf("%s CMD: EM API list Devices. Start: %d Num: %d\n", now, start_num, num_requested);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (num_requested == 0)
		num_requested = (cxl_state->num_devices - start_num);

	if (start_num >= cxl_state->num_devices) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Start num out of range. Start: %d Total: %d\n", now, start_num, num_requested);
		goto send;
	}
	
	if ( (start_num + num_requested) >= cxl_state->num_devices)
		num_requested = (cxl_state->num_devices - start_num);

	STEP // 10: Perform Action 
	IFV(CLVB_ACTIONS) printf("%s ACT: Responding with %d devices\n", now, num_requested);

	STEP // 11: Prepare Response Object
	for ( i = 0 ; i < num_requested ; i++ )
	{
		d = &cxl_state->devices[start_num + i];

		// Serialize the id number
		rspb->payload[len+0] = start_num + i;

		// Serialize the name string 
		if (d->name != NULL )
		{
			rspb->payload[len+1] = strlen(d->name) + 1;
			memcpy(&rspb->payload[len+2], d->name, rspb->payload[len+1]);
		}
		else 
			rspb->payload[len+1] = 0;

		len += (2 + rspb->payload[len+1]);
		count++;
	}

	STEP // 12: Serialize Response Object

	STEP // 13: Set return code
	rc = EMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	STEP // 15: Fill Response Header
	ma->rsp->len = emapi_fill_hdr(&rspm.hdr, EMMT_RSP, reqm.hdr.tag, rc, reqm.hdr.opcode, len, count, 0);

	STEP // 16: Serialize Header 
	emapi_serialize(rspb->hdr, &rspm.hdr, EMOB_HDR, NULL);

	STEP // 17: Push response mctp_msg onto queue 
	pq_push(m->tmq, ma);

	rv = 0;
	goto end;

fail:

	ma->completion_code = 1;	
	pq_push(m->acq, ma);

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for EM API List Devices Opcode
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
static int emop_unsupported(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct emapi_msg reqm, rspm;
	struct emapi_buf *reqb, *rspb;
	unsigned rc;
	int rv;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	rc = EMRC_UNSUPPORTED;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto fail;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	STEP // 4: Set buffer pointers 
	reqb = (struct emapi_buf*) ma->req->payload;
	rspb = (struct emapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( emapi_deserialize(&reqm.hdr, reqb->hdr, EMOB_HDR, NULL) <= 0 )
		goto fail;

	STEP // 6: Deserialize Request Object 

	STEP // 7: Extract parameters
	IFV(CLVB_COMMANDS) printf("%s ERR: Unsupported Opcode: 0x%04x\n", now, reqm.hdr.opcode);

	STEP // 8: Obtain lock on switch state 

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object

	STEP // 12: Serialize Response Object

	STEP // 13: Set return code

	STEP // 14: Release lock on switch state 

	STEP // 15: Fill Response Header
	ma->rsp->len = emapi_fill_hdr(&rspm.hdr, EMMT_RSP, reqm.hdr.tag, rc, reqm.hdr.opcode, 0, 0, 0);

	STEP // 16: Serialize Header 
	emapi_serialize(rspb->hdr, &rspm.hdr, EMOB_HDR, NULL);

	STEP // 17: Push response mctp_msg onto queue 
	pq_push(m->tmq, ma);

	rv = 0;
	goto end;

fail:

	ma->completion_code = 1;	
	pq_push(m->acq, ma);

end:				

	EXIT(rc)

	return rv;
}

