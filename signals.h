/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		signals.h
 *
 * @brief 		Header file for signal handling functions 
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */
#ifndef _SIGNALS_H 
#define _SIGNALS_H

/* INCLUDES ==================================================================*/

/* MACROS ====================================================================*/

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

/**
 * Register signal handlers
 */
void signals_register();

/**
 * Handler for SIGINT (CTRL-C)
 */
void signals_sigint(int sig);

/* GLOBAL VARIABLES ==========================================================*/

extern int stop_requested;

#endif //ifndef _SIGNALS_H
