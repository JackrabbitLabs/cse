/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_mcc_handler.c
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
 * Handler for FM API MCC Get LD Alloc Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_get_ld_alloc(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	unsigned i, end;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get LD Allocations. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	// If start num exceeds number of vppbids return invalid 
	if (req->obj.mcc_alloc_get_req.start > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested start ldid exceeds number of logical devices on this mld. Start: %d Actual: %d\n", now, req->obj.mcc_alloc_get_req.start, p->mld->num);
		goto send;
	}

	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	rsp->obj.mcc_alloc_get_rsp.total 		= p->mld->num;
	rsp->obj.mcc_alloc_get_rsp.granularity 	= p->mld->granularity;
	rsp->obj.mcc_alloc_get_rsp.start 		= req->obj.mcc_alloc_get_req.start;
	rsp->obj.mcc_alloc_get_rsp.num 			= 0;

	end = p->mld->num;
	if (req->obj.mcc_alloc_get_req.limit < (end - req->obj.mcc_alloc_get_req.start) )
		end = req->obj.mcc_alloc_get_req.start + req->obj.mcc_alloc_get_req.limit;
	
	for ( i = req->obj.mcc_alloc_get_req.start ; i < end ; i++ )
	{
		rsp->obj.mcc_alloc_get_rsp.list[rsp->obj.mcc_alloc_get_rsp.num].rng1 = p->mld->rng1[i];	
		rsp->obj.mcc_alloc_get_rsp.list[rsp->obj.mcc_alloc_get_rsp.num].rng2 = p->mld->rng2[i];	
		rsp->obj.mcc_alloc_get_rsp.num++;
	}

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Get QoS Allocated BW Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_get_qos_alloc(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	unsigned i;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get QoS Allocated. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	rsp->obj.mcc_qos_bw_alloc.num = req->obj.mcc_qos_bw_alloc_get_req.num;
	rsp->obj.mcc_qos_bw_alloc.start = req->obj.mcc_qos_bw_alloc_get_req.start;
	if ( (p->mld->num - req->obj.mcc_qos_bw_alloc_get_req.start) < req->obj.mcc_qos_bw_alloc_get_req.num )
		rsp->obj.mcc_qos_bw_alloc.num = p->mld->num - req->obj.mcc_qos_bw_alloc_get_req.start;

	for ( i = 0 ; i < rsp->obj.mcc_qos_bw_alloc.num ; i++) 
		rsp->obj.mcc_qos_bw_alloc.list[i] = p->mld->alloc_bw[i+req->obj.mcc_qos_bw_alloc_get_req.start];

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Get QoS Control Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_get_qos_ctrl(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get QoS Control. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	
	rsp->obj.mcc_qos_ctrl.epc_en 			= p->mld->epc_en;
	rsp->obj.mcc_qos_ctrl.ttr_en 			= p->mld->ttr_en;
	rsp->obj.mcc_qos_ctrl.egress_mod_pcnt 	= p->mld->egress_mod_pcnt; 
	rsp->obj.mcc_qos_ctrl.egress_sev_pcnt 	= p->mld->egress_sev_pcnt;
	rsp->obj.mcc_qos_ctrl.sample_interval 	= p->mld->sample_interval;
	rsp->obj.mcc_qos_ctrl.rcb 				= p->mld->rcb;
	rsp->obj.mcc_qos_ctrl.comp_interval 	= p->mld->comp_interval;

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Get QoS BW Limit Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_get_qos_limit(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	int i;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get QoS Limit. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	rsp->obj.mcc_qos_bw_limit.num 	= req->obj.mcc_qos_bw_limit_get_req.num;
	rsp->obj.mcc_qos_bw_limit.start = req->obj.mcc_qos_bw_limit_get_req.start;
	if ( (p->mld->num - req->obj.mcc_qos_bw_limit_get_req.start) < req->obj.mcc_qos_bw_limit_get_req.num )
		rsp->obj.mcc_qos_bw_limit.num = p->mld->num - req->obj.mcc_qos_bw_limit_get_req.start;

	for ( i = 0 ; i < rsp->obj.mcc_qos_bw_limit.num ; i++ ) 
		rsp->obj.mcc_qos_bw_limit.list[i] = p->mld->bw_limit[i+req->obj.mcc_qos_bw_limit_get_req.start];

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Get QoS Status Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_get_qos_stat(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get QoS Status. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 

	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}
	
	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	rsp->obj.mcc_qos_stat_rsp.bp_avg_pcnt = p->mld->bp_avg_pcnt;

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Info Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_info(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Get LD Info. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	STEP // 6: Perform Action 

	STEP // 7: Prepare Response Object
	rsp->obj.mcc_info_rsp.size 	= p->mld->memory_size ;
	rsp->obj.mcc_info_rsp.num 	= p->mld->num;
	rsp->obj.mcc_info_rsp.epc 	= p->mld->epc;
	rsp->obj.mcc_info_rsp.ttr 	= p->mld->ttr;

	STEP // 8: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

	STEP // 9: Set return code
	rc = FMRC_SUCCESS;

send:

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Set LD Alloc Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Set return code
 *   8: Prepare Response Object
 *   9: Serialize Response Object
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_set_ld_alloc(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	int i;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Set LD Allocations. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	// Verify requested LD count does not exceed actual LD count
	if (req->obj.mcc_alloc_set_req.num > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested number of LD entries exceeds number of LDs present. Requested: %d Present: %d\n", now, req->obj.mcc_alloc_set_req.num, p->mld->num);
		goto send;
	}

	// Verify the start LD ID does not exceed actual LD count 
	if (req->obj.mcc_alloc_set_req.start > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested started LD ID exceeds number of LDs present. Start: %d Present: %d\n", now, req->obj.mcc_alloc_set_req.start, p->mld->num);
		goto send;
	}

	// Verify the final LD ID does not exceed actual LD count 
	if ((req->obj.mcc_alloc_set_req.start + req->obj.mcc_alloc_set_req.num) > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested start + num exceeds number of LDs present. End: %d Present: %d\n", now, req->obj.mcc_alloc_set_req.start+req->obj.mcc_alloc_set_req.num, p->mld->num);
		goto send;
	}

	STEP // 6: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Setting LD Allocations on PPID: %d\n", now, p->ppid);

	for ( i = 0 ; i < req->obj.mcc_alloc_set_req.num ; i++ ) 
	{
		p->mld->rng1[i+req->obj.mcc_alloc_set_req.start] = req->obj.mcc_alloc_set_req.list[i].rng1;
		p->mld->rng2[i+req->obj.mcc_alloc_set_req.start] = req->obj.mcc_alloc_set_req.list[i].rng2;
	}

	STEP // 7: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 8: Prepare Response Object
	rsp->obj.mcc_alloc_set_rsp.num = req->obj.mcc_alloc_set_req.num;
	rsp->obj.mcc_alloc_set_rsp.start = req->obj.mcc_alloc_set_req.start;
	for ( i = 0 ; i < rsp->obj.mcc_alloc_set_rsp.num ; i++ ) {
		rsp->obj.mcc_alloc_set_rsp.list[i].rng1 = p->mld->rng1[i+rsp->obj.mcc_alloc_set_rsp.start];
		rsp->obj.mcc_alloc_set_rsp.list[i].rng2 = p->mld->rng2[i+rsp->obj.mcc_alloc_set_rsp.start];	
	}

	STEP // 9: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Set QoS BW Allocated Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Set return code
 *   8: Prepare Response Object
 *   9: Serialize Response Object
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_set_qos_alloc(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	int i;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Set QoS Allocated. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	// Verify requested LD count does not exceed actual LD count
	if (req->obj.mcc_qos_bw_alloc.num > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested number of LD entries exceeds number of LDs present. Requested: %d Present: %d\n", now, req->obj.mcc_qos_bw_alloc.num, p->mld->num);
		goto send;
	}

	// Verify requested LD count does not exceed actual LD count
	if ((req->obj.mcc_qos_bw_alloc.start + req->obj.mcc_qos_bw_alloc.num) > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested start + number of LD entries exceeds number of LDs present. Requested: %d Present: %d\n", now, req->obj.mcc_qos_bw_alloc.num, p->mld->num);
		goto send;
	}
	STEP // 6: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Setting QoS Allocations on PPID: %d\n", now, p->ppid);

	for ( i = 0 ; i < req->obj.mcc_qos_bw_alloc.num ; i++ ) 
		p->mld->alloc_bw[i+req->obj.mcc_qos_bw_alloc.start] = req->obj.mcc_qos_bw_alloc.list[i];

	STEP // 7: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 8: Prepare Response Object
	rsp->obj.mcc_qos_bw_alloc.start = req->obj.mcc_qos_bw_alloc.start;
	rsp->obj.mcc_qos_bw_alloc.num = req->obj.mcc_qos_bw_alloc.num;
	for ( i = 0 ; i < rsp->obj.mcc_qos_bw_alloc.num ; i++ ) 
		rsp->obj.mcc_qos_bw_alloc.list[i] = p->mld->alloc_bw[i+rsp->obj.mcc_qos_bw_alloc.start];

	STEP // 9: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Set QoS Control Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Set return code
 *   8: Prepare Response Object
 *   9: Serialize Response Object
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_set_qos_ctrl(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Set QoS Control. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	STEP // 6: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Setting QoS Control on PPID: %d\n", now, p->ppid);

	p->mld->epc_en 				= req->obj.mcc_qos_ctrl.epc_en;
	p->mld->ttr_en 				= req->obj.mcc_qos_ctrl.ttr_en;
	p->mld->egress_mod_pcnt 	= req->obj.mcc_qos_ctrl.egress_mod_pcnt;
	p->mld->egress_sev_pcnt 	= req->obj.mcc_qos_ctrl.egress_sev_pcnt;
	p->mld->sample_interval 	= req->obj.mcc_qos_ctrl.sample_interval;
	p->mld->rcb 				= req->obj.mcc_qos_ctrl.rcb;
	p->mld->comp_interval 		= req->obj.mcc_qos_ctrl.comp_interval;

	STEP // 7: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 8: Prepare Response Object
	rsp->obj.mcc_qos_ctrl.epc_en 				= p->mld->epc_en;
	rsp->obj.mcc_qos_ctrl.ttr_en 				= p->mld->ttr_en;
	rsp->obj.mcc_qos_ctrl.egress_mod_pcnt 		= p->mld->egress_mod_pcnt;
	rsp->obj.mcc_qos_ctrl.egress_sev_pcnt 		= p->mld->egress_sev_pcnt;
	rsp->obj.mcc_qos_ctrl.sample_interval 		= p->mld->sample_interval;
	rsp->obj.mcc_qos_ctrl.rcb 					= p->mld->rcb;
	rsp->obj.mcc_qos_ctrl.comp_interval			= p->mld->comp_interval;

	STEP // 9: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

/**
 * Handler for FM API MCC Set QoS BW Limit Opcode
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @param req 	struct fmapi_msg* 
 * @param rsp   struct fmapi_msg*
 * @return 		length of serialized message (FMLN_HDR + object)
 *
 * STEPS
 *   1: Initialize variables
 *   2: Deserialize Request Header
 *   3: Deserialize Request Object 
 *   4: Extract parameters
 *   5: Validate Inputs 
 *   6: Perform Action 
 *   7: Prepare Response Object
 *   8: Serialize Response Object
 *   9: Set return code
 *  10: Fill Response Header
 *  11: Serialize Response Header 
 *  12: Return length of MF API Message (FMLN_HDR + object)
 */
int fmop_mcc_set_qos_limit(struct port *p, struct fmapi_msg *req, struct fmapi_msg *rsp)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	unsigned rc;
	int rv, len;

	int i;

	ENTER

	STEP // 1: Initialize variables
	rv = 0; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);
	
	STEP // 2: Deserialize Header
	if ( fmapi_deserialize(&req->hdr, req->buf->hdr, FMOB_HDR, 0) <= 0)
		goto end;

	STEP // 3: Deserialize Request Object 
	if ( fmapi_deserialize(&req->obj, req->buf->payload, fmapi_fmob_req(req->hdr.opcode), NULL) < 0 )
		goto end;

    STEP // 4: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API MCC Set QoS Limit. PPID: %d\n", now, p->ppid);

	STEP // 5: Validate Inputs 
	
	// If port does not have an mld device return invalid
	if (p->mld == NULL) 
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Port not connected to an MLD\n", now);
		goto send;
	}

	// Verify requested LD count does not exceed actual LD count
	if (req->obj.mcc_qos_bw_limit.num > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested number of LD entries exceeds number of LDs present. Requested: %d Present: %d\n", now, req->obj.mcc_qos_bw_limit.num, p->mld->num);
		goto send;
	}

	// Verify requested LD count does not exceed actual LD count
	if ((req->obj.mcc_qos_bw_limit.start + req->obj.mcc_qos_bw_limit.num) > p->mld->num)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested start + number of LD entries exceeds number of LDs present. Requested: %d Present: %d\n", now, req->obj.mcc_qos_bw_limit.num, p->mld->num);
		goto send;
	}

	STEP // 6: Perform Action 

	IFV(CLVB_ACTIONS) printf("%s ACT: Setting QoS Limit on PPID: %d\n", now, p->ppid);

	for ( i = 0 ; i < req->obj.mcc_qos_bw_limit.num ; i++ ) 
		p->mld->bw_limit[i+req->obj.mcc_qos_bw_limit.start] = req->obj.mcc_qos_bw_limit.list[i];

	STEP // 7: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 8: Prepare Response Object
	rsp->obj.mcc_qos_bw_limit.start = req->obj.mcc_qos_bw_limit.start;
	rsp->obj.mcc_qos_bw_limit.num = req->obj.mcc_qos_bw_limit.num;
	for ( i = 0 ; i < rsp->obj.mcc_qos_bw_limit.num ; i++ ) 
		rsp->obj.mcc_qos_bw_limit.list[i] = p->mld->bw_limit[i+req->obj.mcc_qos_bw_limit.start];

	STEP // 9: Serialize Response Object
	len = fmapi_serialize(rsp->buf->payload, &rsp->obj, fmapi_fmob_rsp(req->hdr.opcode));

 	STEP // 10: Fill Response Header
	rv = fmapi_fill_hdr(&rsp->hdr, FMMT_RESP, req->hdr.tag, req->hdr.opcode, 0, len, rc, 0);

	STEP // 11: Serialize Response Header 
	fmapi_serialize(rsp->buf->hdr, &rsp->hdr, FMOB_HDR);

end:

	EXIT(rc)
	
	STEP // 12: Return length of MF API Message (FMLN_HDR + object)
	return rv;
}

