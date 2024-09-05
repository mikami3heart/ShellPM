/* ##################################################################
 *
 * PMlib - Performance Monitor library
 *
 * Copyright (c) 2010-2011 VCAD System Research Program, RIKEN.
 * All rights reserved.
 *
 * Copyright (c) 2012-2015 Advanced Institute for Computational Science, RIKEN.
 * All rights reserved.
 *
 * Copyright (c) 2016-2020 Research Institute for Information Technology(RIIT), Kyushu University.
 * All rights reserved.
 *
 * ###################################################################
 */

//@file   papi_ext.c
//@brief  PMlib C functions to link with PAPI library

#include "stdio.h"
#include "papi.h"
#include <string.h>
#include <stdlib.h>

#define HL_STOP		0
#define HL_START	1

typedef struct _HighLevelInfo
{
	int EventSet;					        /**< EventSet of the thread */
	short int num_evts;				    /**< number of events in the eventset */
	short int running;				    /**< STOP, START, or RATE */
	long long initial_real_time;	/**< Start real time */
	long long initial_proc_time;	/**< Start processor time */
	long long last_real_time;		  /**< Previous value of real time */
	long long last_proc_time;		  /**< Previous value of processor time */
	long long total_ins;			    /**< Total instructions */
} HighLevelInfo;

void my_internal_cleanup_hl_info( HighLevelInfo * state );
int my_internal_check_state( HighLevelInfo ** state );


void print_state_HighLevelInfo(HighLevelInfo *state)
{
	if ( state == NULL ) {
		fprintf(stderr,"*** error. <print_state_HighLevelInfo> state==NULL\n");
		return;
	}
	fprintf(stderr,"\t <print_state_HighLevelInfo> starts\n");
	fprintf(stderr,"\t struct state:  EventSet=%d, num_evts=%d, running=%d\n",
						state->EventSet, state->num_evts, state->running);
}


int my_papi_add_events ( int *events, int num_events)
{
	HighLevelInfo *state = NULL;
	int i, retval;

	if ( num_events == 0 ) {
		return PAPI_OK;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_add_events> num_events=%d\n", num_events);
	for (i=0; i<num_events; i++){
		fprintf(stderr, "  i:%d events[i]:%u\n", i,events[i]);
	}
	#endif

	if ( ( retval = my_internal_check_state( &state ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_add_events> :: <_check_state>\n");
		return retval;
	}

	if (( retval = PAPI_add_events( state->EventSet, events, num_events )) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_add_events> :: <PAPI_add_events> state->EventSet=%d, num_events=%d\n", state->EventSet, num_events);

		my_internal_cleanup_hl_info( state );
		PAPI_cleanup_eventset( state->EventSet );
		return retval;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	print_state_HighLevelInfo(state);
	#endif

	return PAPI_OK;
}


int my_papi_bind_start ( long long *values, int num_events)
{
	HighLevelInfo *state = NULL;
	int retval;

	if ( num_events == 0 ) {
		return PAPI_OK;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_bind_start> num_events=%d\n", num_events);
	#endif

	if ( ( retval = my_internal_check_state( &state ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_start> :: <_check_state>\n");
		return retval;
	}
	if ( ( retval = PAPI_start( state->EventSet ) ) != PAPI_OK ) {
		if ( retval == PAPI_EISRUN) {
		// Event set is already running.
		// This is usually the case for multi level measurement sections
		// As of current version, we ignore this, i.e. single level support
		}
		else {
			fprintf(stderr,"*** error. <my_papi_bind_start> :: <PAPI_start>\n");
			return retval;
		}
	}
	state->running = HL_START;

	return PAPI_OK;
}


int my_papi_bind_stop ( long long *values, int num_events)
{
	HighLevelInfo *state = NULL;
	int retval;

	if ( num_events == 0 ) {
		return PAPI_OK;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_bind_stop> num_events=%d\n", num_events);
	#endif

	if ( ( retval = my_internal_check_state( &state ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_stop> :: <_check_state>\n");
		return retval;
	}
	if ( ( retval = PAPI_stop( state->EventSet, values ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_stop> :: <PAPI_stop>\n");
		state->running = HL_STOP;
		return retval;
	}
	if ( ( retval = PAPI_start( state->EventSet ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_stop> :: <PAPI_start>\n");
		state->running = HL_STOP;
		return retval;
	}
	return PAPI_OK;
}


int my_papi_bind_read ( long long *values, int num_events)
{
	HighLevelInfo *state = NULL;
	int retval;

	if ( num_events == 0 ) {
		return PAPI_OK;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_bind_read> \n");
	#endif

	if ( ( retval = my_internal_check_state( &state ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_read> :: <_check_state> \n");
		return retval;
	}
	if ( ( retval = PAPI_read( state->EventSet, values ) ) != PAPI_OK ) {
		fprintf(stderr,"*** error. <my_papi_bind_read> :: <PAPI_read>\n");
		return retval;
	}
	return PAPI_OK;
}


void my_papi_name_to_code ( char* c_event, int* i_event)
{
	int retval;
	//	retval = PAPI_event_name_to_code( c_event, &i_event );

	retval = PAPI_event_name_to_code( c_event, i_event );
	if ( retval != PAPI_OK ) {
		fprintf(stderr,"*** error. <PAPI_event_name_to_code> c_event=[%s], retval=%d\n", c_event, retval);
		return;
	}
	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_name_to_code> c_event=[%s], i_event=%u\n",
		c_event, *i_event);
	#endif

	return;
}

//
// The following routines should not be necessary if all the papi routines
// are exposed to user space.
// Unfortunately, Fugaku RHEL strips _internal* routines and hide from
// user space.
// So they are partially extracted and modified from papi_hl.c
//
int my_internal_check_state( HighLevelInfo ** hlstate )
{
	int retval;
	int p_get;
	HighLevelInfo *state = NULL;
	//
	//  Starting from PAPI 6.0, defined macro PAPI_HIGH_LEVEL_TLS is deleted from papi.h
	//

	//	p_get = PAPI_get_thr_specific(PAPI_HIGH_LEVEL_TLS, (void **) &state );
	p_get = PAPI_get_thr_specific(PAPI_USR1_TLS, (void **) &state );
	if ( p_get != PAPI_OK || state == NULL ) {

		state = (HighLevelInfo *) malloc(sizeof(HighLevelInfo));
		if ( state == NULL ) return ( PAPI_ENOMEM );
		#ifdef DEBUG_PRINT_PAPI_EXT
		fprintf(stderr,"\t <my_internal_check_state> called malloc. size=%d, address=%p \n", sizeof(HighLevelInfo), state);
		#endif

		memset(state, 0, sizeof(HighLevelInfo));
		state->EventSet = PAPI_NULL;
		if ( (retval = PAPI_create_eventset(&state->EventSet)) != PAPI_OK )
			return (retval);
		//	if ( (retval = PAPI_set_thr_specific(PAPI_HIGH_LEVEL_TLS, state)) != PAPI_OK )
		if ( (retval = PAPI_set_thr_specific(PAPI_USR1_TLS, state)) != PAPI_OK )
			return (retval);
	}
	*hlstate = state;
	return PAPI_OK;
}

void my_internal_cleanup_hl_info( HighLevelInfo * state )
{
	state->num_evts = 0;
	state->running = HL_STOP;
	state->initial_real_time = -1;
   	state->initial_proc_time = -1;
	state->total_ins = 0;
	return;
}


void my_papi_internal_free()
{
	HighLevelInfo *state = NULL;
	int retval;

	if ( ( retval = my_internal_check_state( &state ) ) != PAPI_OK ) {
		fprintf(stderr,"\n *** PMlib warning. <my_papi_internal_free> will not cleanup HighLevelInfo *state. \n");
		return;
	}

	#ifdef DEBUG_PRINT_PAPI_EXT
	fprintf(stderr,"\t <my_papi_internal_free> size=%d, address=%p \n", sizeof(HighLevelInfo), state);
	#endif

	my_internal_cleanup_hl_info( state );
	PAPI_cleanup_eventset( state->EventSet );
	free( state );

	return;
}
