/*
	Copyright (C) 2019 Chris Cope <git@captainnet.co.uk>

	This file is part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

/*
   Send and receive protocol for Byron by doorbell
   Format is as follows:
   
   42 pulses
   
   1st pulse, short pulse header
   20 pairs, short then long = 0, long then short = 1
   42nd pulse, footer
   
   1st and 42nd used to validate and then ignored
   
   Data stream format:  AAAA AAAA BBBB BBBB CCCC
   
   A = bits 0 to 7 - System id - fixed ? - mapped to systemcode
   B = bits 8 to 15 - Unit code - changes when battery replaced - mapped to unit
   C = bits 16 to 19 - Bell code - changes when the bell button is pressed - mapped to id
   
   Short pulse 500us, long pulse 1000us, footer 3000us
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "byron_by_chime.h"

#define PULSE_MULTIPLIER    2
#define MIN_PULSE_LENGTH    407
#define AVG_PULSE_LENGTH    400
#define MAX_PULSE_LENGTH    572
#define RAW_LENGTH          42
#define FOOTER_MULTIPLIER   6
static int validate(void) 
{
	logprintf(LOG_DEBUG, "byron_validate() rawlen=%d byron_by_chime->raw[byron_by_chime->rawlen-1]=%d, byron_by_chime->raw[0]=%d", byron_by_chime->rawlen, byron_by_chime->raw[byron_by_chime->rawlen-1], byron_by_chime->raw[0]);
	if(byron_by_chime->rawlen == RAW_LENGTH) 
	{
		if(byron_by_chime->raw[byron_by_chime->rawlen-1] >= (int)(MIN_PULSE_LENGTH*FOOTER_MULTIPLIER) &&
			 byron_by_chime->raw[byron_by_chime->rawlen-1] <= (int)(MAX_PULSE_LENGTH*FOOTER_MULTIPLIER) &&
			 byron_by_chime->raw[0] >= MIN_PULSE_LENGTH &&
			 byron_by_chime->raw[0] <= MAX_PULSE_LENGTH)
	    {
		    return 0;
		}
	}
	return -1;
}

static void createMessage(int sys, int unit, int bell) 
{
	byron_by_chime->message = json_mkobject();
	json_append_member(byron_by_chime->message, "systemcode", json_mknumber(sys, 0));
	json_append_member(byron_by_chime->message, "unitcode", json_mknumber(unit, 0));
	json_append_member(byron_by_chime->message, "id", json_mknumber(bell, 0));
}

static void parseCode(void) 
{
	int binary[RAW_LENGTH/2];
	int x = 0;


	if(byron_by_chime->rawlen>RAW_LENGTH) 
	{
		logprintf(LOG_ERR, "byron_by_chime: parsecode - invalid parameter passed (rawlen) %d", byron_by_chime->rawlen);
	}
	else
	{
		for(x = 0; x < byron_by_chime->rawlen-1; x += 2) 
		{
			logprintf(LOG_DEBUG,"byron: [%d]", byron_by_chime->raw[x+1]);
			if(byron_by_chime->raw[x+1] > ((int)((double)MIN_PULSE_LENGTH*((double)PULSE_MULTIPLIER))))
			{
				binary[x/2] = 1;
			} 
			else 
			{
				binary[x/2] = 0;
			}
		}
	
		int sys = binToDecRev(binary, 0, 7);
		int unit = binToDecRev(binary, 8, 15);
		int id = binToDecRev(binary, 16, 19);  //this is the bell
		
		createMessage(sys, unit, id);
	}
}

static void createZero(int s, int e) 
{
	int i;
	for(i = s; i <= e; i += 2) 
	{
		byron_by_chime->raw[i] = AVG_PULSE_LENGTH;
		byron_by_chime->raw[i+1] = AVG_PULSE_LENGTH * PULSE_MULTIPLIER;
	}
}

static void createOne(int s, int e) 
{
	int i;
	for(i=s;i<=e;i+=2) 
	{
		byron_by_chime->raw[i] = AVG_PULSE_LENGTH * PULSE_MULTIPLIER;
		byron_by_chime->raw[i+1] = AVG_PULSE_LENGTH;
	}
}

static void createHeader(void) 
{
	byron_by_chime->raw[0] = AVG_PULSE_LENGTH;
}

static void createFooter(void) 
{
	byron_by_chime->raw[byron_by_chime->rawlen-1] = FOOTER_MULTIPLIER*AVG_PULSE_LENGTH;
}

static void clearCode(void) 
{
	createHeader();
	createZero(1, byron_by_chime->rawlen-3);
}

static void createSys(int sys)
{
	int binary[8];
	int length = 0;
	int i = 0;
	int x = 15;  //pulses 1 to 16
	
	length = decToBin(sys, binary);
	for (i = length; i >= 0; i--)
	{
	  if (binary[i] == 1)
	  {
	    createOne(x, x+1);
	  }
	  x -= 2;	    
	}
}

static void createUnit(int unit)
{
	int binary[8];
	int length = 0;
	int i = 0;
	int x = 31;  //pulse 17 to 32
	
	length = decToBin(unit, binary);
	for (i = length; i >= 0; i--)
	{
	  if (binary[i] == 1)
	  {
	    createOne(x, x+1);
	  }
	  x -= 2;	    
	}
}

static void createId(int id) 
{
	int binary[4];
	int length = 0;
	int i = 0;
	int x = 39;  //pulse 33 to 40
	
	length = decToBin(id, binary);
	for (i = length; i >= 0; i--)
	{
	  if (binary[i] == 1)
	  {
	    createOne(x, x+1);
	  }
	  x -= 2;	    
	}
}

static int createCode(JsonNode *code) 
{
	int retVal = EXIT_FAILURE;
	double itmp = -1;
	int sys = -1;
	int unit = -1;
	int id = -1;

	if(json_find_number(code, "systemcode", &itmp) == 0)
	{
		sys = (int)round(itmp);
	}
	
	if(json_find_number(code, "unit", &itmp) == 0)
	{
		unit = (int)round(itmp);
	}
	
	if(json_find_number(code, "id", &itmp) == 0)
	{
		id = (int)round(itmp);
	}

	if( (sys == -1) || (unit == -1) || (id == -1))
	{
		logprintf(LOG_ERR, "byron_by_chime: insufficient number of arguments (%d, %d, %d)", sys, unit, id);
	}
    else 
    {
		byron_by_chime->rawlen = RAW_LENGTH;
		createMessage(sys, unit, id);
		clearCode();
		createSys(sys);
		createUnit(unit);
		createId(id);
		createFooter();
		retVal = EXIT_SUCCESS;
	}
	return retVal;
}

static void printHelp(void) {
	printf("\t -s --systemcode=systemcode\t\t\tcontrol bell with this system code\n");
	printf("\t -u --unit=unit\t\t\tcontrol the bell unit with this code\n");
	printf("\t -i --id\t\t\tsend this id to the bell\n");
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void byronByChimeInit(void) {

	protocol_register(&byron_by_chime);
	protocol_set_id(byron_by_chime, "byron_by_chime");
	protocol_device_add(byron_by_chime, "byron_by_chime", "Byron BY Doorbell");
	byron_by_chime->devtype = ALARM;
	byron_by_chime->hwtype = RF433;
	byron_by_chime->minrawlen = RAW_LENGTH;
	byron_by_chime->maxrawlen = RAW_LENGTH;
	byron_by_chime->maxgaplen = 3200;//MAX_PULSE_LENGTH * FOOTER_MULTIPLIER;//3200;//(int)(PULSE_FOOTER*1.1);//3200;//(int)(PULSE_FOOTER*0.9);
	byron_by_chime->mingaplen = 2800;//MIN_PULSE_LENGTH * FOOTER_MULTIPLIER;//2800;//(int)(PULSE_FOOTER*0.96);//2800;//(int)(PULSE_FOOTER*1.1);

	options_add(&byron_by_chime->options, "s", "systemcode", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-4][0-9][0-9]|655[0-2][0-9]|6553[0-5])$");
	options_add(&byron_by_chime->options, "u", "unit", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-4][0-9][0-9]|655[0-2][0-9]|6553[0-5])$");
	options_add(&byron_by_chime->options, "i", "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([0-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-4])$");
	
	options_add(&byron_by_chime->options, "0", "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&byron_by_chime->options, "0", "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	byron_by_chime->parseCode=&parseCode;
	byron_by_chime->createCode=&createCode;
	byron_by_chime->printHelp=&printHelp;
	byron_by_chime->validate=&validate;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) 
{
	module->name = "byron_by_chime";
	module->version = "1.0";
	module->reqversion = "6.0";
	module->reqcommit = "84";
}

void init(void) 
{
	byronByChimeInit();
}
#endif


