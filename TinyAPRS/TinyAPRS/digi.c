/*
 * \file digi.c
 * <!--
 * This file is part of TinyAPRS.
 * Released under GPL License
 *
 * Copyright 2015 Shawn Chain (shawn.chain@gmail.com)
 *
 * -->
 *
 * \brief 
 *
 * \author shawn
 * \date 2015-4-13
 */

#include "digi.h"

#include <net/afsk.h>
#include <net/ax25.h>
#include <drv/ser.h>
#include <drv/timer.h>
#include <io/kfile.h>

#include "global.h"
#include "settings.h"

typedef struct CacheEntry{
	uint16_t hash;
	uint32_t timestamp; // in seconds
}CacheEntry;

#define CACHE_SIZE CFG_DIGI_DUP_CHECK_CACHE_SIZE
#define DUP_CHECK_INTERVAL CFG_DIGI_DUP_CHECK_INTERVAL
static CacheEntry cache[CACHE_SIZE];
static uint8_t cacheIndex;
#define CURRENT_TIME_SECONDS() (ticks_to_ms(timer_clock()) / 1000)


void digi_init(void){
	memset(&cache,0,sizeof(CacheEntry) * CACHE_SIZE);
	cacheIndex = 0;
}

static bool _digi_repeat_message(AX25Msg *msg){
	// force delay 100ms
	timer_delayTicks(ms_to_ticks(250));

	{
	char fmt[16];
	sprintf_P(fmt,PSTR("digipeat:\r\n"));
	kfile_printf(&g_serial.fd,fmt);
	}
	ax25_print(&g_serial.fd, msg);
	ax25_sendMsg(&g_ax25, msg);
	return true;
}



static uint16_t _digi_calc_hash(AX25Msg *msg){
	uint16_t hash = 0;
	for(size_t i = 0;i < msg->len;i++){
		hash = hash * 31 + msg->info[i];
	}
	return hash;
}

/*
 * duplication checks
 */
static bool _digi_check_duplication(AX25Msg *msg){
	(void)msg;
	bool dup = false;
	uint16_t hash = _digi_calc_hash(msg);
	for(uint8_t i = CACHE_SIZE ;cacheIndex > 0 &&  i >0 ;i--){
		uint8_t j = (cacheIndex - 1 + i) % CACHE_SIZE;
		if((cache[j].hash == hash) && (CURRENT_TIME_SECONDS() - cache[j].timestamp) < DUP_CHECK_INTERVAL ){
			dup = true;
			break;
		}
	}
	if(!dup){
		cacheIndex = (cacheIndex + 1) % CACHE_SIZE;
		cache[cacheIndex - 1].hash = hash;
		cache[cacheIndex - 1].timestamp = ticks_to_ms(timer_clock()) / 1000;
		return true;
	}
	return false;
}

bool digi_handle_aprs_message(struct AX25Msg *msg){
	for(int i = 0;i < msg->rpt_cnt;i++){
		AX25Call *rpt = msg->rpt_lst + i;
		uint8_t len = 5;
		if( ((strncasecmp_P(rpt->call,PSTR("WIDE1"),5) == 0) || (strncasecmp_P(rpt->call,PSTR("WIDE2"),5) == 0) || (strncasecmp_P(rpt->call,PSTR("WIDE3"),5) == 0))
				&& (rpt->ssid > 0)
				&& !(AX25_REPEATED(msg,i)) ){

			// check duplications;
			if(!_digi_check_duplication(msg)){
				// seems duplicated in cache, drop
				//kfile_printf(&g_serial.fd,"duplicated!\r\n");
				return false;
			}

			if(rpt->ssid >1){
				rpt->ssid--; // SSID-1
				if(i < AX25_MAX_RPT - 1){
					// copy WIDEn-(N-1) to next rpt list
					AX25Call *c = rpt + 1;
					memset(&c->call,0,6);
					memcpy(&c->call,&rpt->call,5);
					c->ssid = rpt->ssid;
					msg->rpt_cnt++; // we inserted my-call as one of the rpt address.
				}else{
					// no space left for the new digi call, drop;
					return false;
				}
			}
			settings_get(SETTINGS_MY_CALL,rpt->call,&len);
			settings_get(SETTINGS_MY_SSID,&rpt->ssid,&len);
			AX25_SET_REPEATED(msg,i,1);
			return _digi_repeat_message(msg);
		}
	}// end for

	return false;
}

