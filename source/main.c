/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <gccore.h>
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

//For whatever reason this has to be a 0x1D680
//buffer size or else the GBA just freezes up
extern u8 gba_mb_gba[];
extern u32 gba_mb_gba_size;

u8 *resbuf,*cmdbuf;
volatile u16 pads = 0;
volatile bool ctrlerr = false;
void ctrlcb(s32 chan, u32 ret)
{
	if(ret)
	{
		ctrlerr = true;
		return;
	}
	//just call us again
	pads = (~((resbuf[1]<<8)|resbuf[0]))&0x3FF;
	SI_Transfer(1,cmdbuf,1,resbuf,5,ctrlcb,350);
}

volatile u32 transval = 0;
void transcb(s32 chan, u32 ret)
{
	transval = 1;
}

volatile u32 resval = 0;
void acb(s32 res, u32 val)
{
	resval = val;
}

unsigned int docrc(u32 crc, u32 val)
{
	int i;
	for(i = 0; i < 0x20; i++)
	{
		if((crc^val)&1)
		{
			crc>>=1;
			crc^=0xa1c1;
		}
		else
			crc>>=1;
		val>>=1;
	}
	return crc;
}

static inline void wait_for_transfer()
{
	//350 is REALLY pushing it already, cant go further
	do{ usleep(350); }while(transval == 0);
}

void endproc()
{
	printf("GC Button pressed, exit\n");
	exit(0);
}
unsigned int calckey(unsigned int size)
{
	unsigned int ret = 0;
	size=(size-0x200) >> 3;
	int res1 = (size&0x3F80) << 1;
	res1 |= (size&0x4000) << 2;
	res1 |= (size&0x7F);
	res1 |= 0x380000;
	int res2 = res1;
	res1 = res2 >> 0x10;
	int res3 = res2 >> 8;
	res3 += res1;
	res3 += res2;
	res3 <<= 24;
	res3 |= res2;
	res3 |= 0x80808080;

	if((res3&0x200) == 0)
	{
		ret |= (((res3)&0xFF)^0x4B)<<24;
		ret |= (((res3>>8)&0xFF)^0x61)<<16;
		ret |= (((res3>>16)&0xFF)^0x77)<<8;
		ret |= (((res3>>24)&0xFF)^0x61);
	}
	else
	{
		ret |= (((res3)&0xFF)^0x73)<<24;
		ret |= (((res3>>8)&0xFF)^0x65)<<16;
		ret |= (((res3>>16)&0xFF)^0x64)<<8;
		ret |= (((res3>>24)&0xFF)^0x6F);
	}
	return ret;
}
int main(int argc, char *argv[]) 
{
	void *xfb = NULL;
	GXRModeObj *rmode = NULL;
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	int x = 24, y = 32, w, h;
	w = rmode->fbWidth - (32);
	h = rmode->xfbHeight - (48);
	CON_InitEx(rmode, x, y, w, h);
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	PAD_Init();
	cmdbuf = memalign(32,32);
	resbuf = memalign(32,32);
	int i;
	while(1)
	{
		printf("\x1b[2J");
		printf("\x1b[37m");
		printf("Simple GBA Input Viewer by FIX94\n");
		printf("You can press any GC controller button to quit\n");
		printf("Waiting for GBA in port 2...\n");
		resval = 0;
		ctrlerr = false;

		SI_GetTypeAsync(1,acb);
		while(1)
		{
			if(resval)
			{
				if(resval == 0x80 || resval & 8)
				{
					resval = 0;
					SI_GetTypeAsync(1,acb);
				}
				else if(resval)
					break;
			}
			PAD_ScanPads();
			VIDEO_WaitVSync();
			if(PAD_ButtonsHeld(0))
				endproc();
		}
		if(resval & SI_GBA)
		{
			printf("GBA Found!\n");
			resbuf[2]=0;
			while(!(resbuf[2]&0x10))
			{
				cmdbuf[0] = 0xFF; //reset
				transval = 0;
				SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,0);
				wait_for_transfer();
				cmdbuf[0] = 0; //status
				transval = 0;
				SI_Transfer(1,cmdbuf,1,resbuf,3,transcb,0);
				wait_for_transfer();
			}
			printf("Ready, sending input stub\n");
			unsigned int sendsize = ((gba_mb_gba_size+7)&~7);
			unsigned int ourkey = calckey(sendsize);
			printf("Our Key: %08x\n", ourkey);
			//get current sessionkey
			memset(resbuf,0,32);
			cmdbuf[0]=0x14; //read
			transval = 0;
			SI_Transfer(1,cmdbuf,1,resbuf,5,transcb,0);
			wait_for_transfer();
			u32 sessionkey = (resbuf[3]^0x6F)<<24|(resbuf[2]^0x64)<<16|(resbuf[1]^0x65)<<8|(resbuf[0]^0x73);
			//send over our own key
			cmdbuf[0]=0x15;cmdbuf[1]=(ourkey>>24)&0xFF;cmdbuf[2]=(ourkey>>16)&0xFF;cmdbuf[3]=(ourkey>>8)&0xFF;cmdbuf[4]=ourkey&0xFF;
			transval = 0;
			SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,0);
			wait_for_transfer();
			unsigned int fcrc = 0x15a0;
			//send over gba header
			for(i = 0; i < 0xC0; i+=4)
			{
				cmdbuf[0]=0x15;cmdbuf[1]=gba_mb_gba[i];cmdbuf[2]=gba_mb_gba[i+1];cmdbuf[3]=gba_mb_gba[i+2];cmdbuf[4]=gba_mb_gba[i+3];
				transval = 0;
				resbuf[0] = 0;
				SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,0);
				wait_for_transfer();
				if(!(resbuf[0]&0x2)) printf("Possible error %02x\n",resbuf[0]);
			}
			printf("Header done! Sending ROM...\n");
			for(i = 0xC0; i < sendsize; i+=4)
			{
				u32 enc = ((gba_mb_gba[i+3]<<24)|(gba_mb_gba[i+2]<<16)|(gba_mb_gba[i+1]<<8)|(gba_mb_gba[i]));
				fcrc=docrc(fcrc,enc);
				sessionkey = (sessionkey*0x6177614B)+1;
				enc^=sessionkey;
				enc^=((~(i+(0x20<<20)))+1);
				enc^=0x20796220;
				cmdbuf[0]=0x15;cmdbuf[1]=(enc>>0)&0xFF;cmdbuf[2]=(enc>>8)&0xFF;cmdbuf[3]=(enc>>16)&0xFF;cmdbuf[4]=(enc>>24)&0xFF;
				transval = 0;
				resbuf[0] = 0;
				SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,0);
				wait_for_transfer();
				if(!(resbuf[0]&0x2)) printf("Possible error %02x\n",resbuf[0]);
			}
			fcrc |= (sendsize<<16);
			printf("ROM done! CRC: %08x\n", fcrc);
			//send over CRC
			sessionkey = (sessionkey*0x6177614B)+1;
			fcrc^=sessionkey;
			fcrc^=((~(i+(0x20<<20)))+1);
			fcrc^=0x20796220;
			cmdbuf[0]=0x15;cmdbuf[1]=(fcrc>>0)&0xFF;cmdbuf[2]=(fcrc>>8)&0xFF;cmdbuf[3]=(fcrc>>16)&0xFF;cmdbuf[4]=(fcrc>>24)&0xFF;
			transval = 0;
			resbuf[0] = 0;
			SI_Transfer(1,cmdbuf,5,resbuf,1,transcb,0);
			wait_for_transfer();
			//get crc back (unused)
			memset(resbuf,0,32);
			cmdbuf[0]=0x14; //read
			transval = 0;
			SI_Transfer(1,cmdbuf,1,resbuf,5,transcb,0);
			wait_for_transfer();
			//start read chain
			cmdbuf[0] = 0x14; //read
			transval = 0;
			//delay needed for gba fadeout
			SI_Transfer(1,cmdbuf,1,resbuf,5,ctrlcb,1000000);
			//hm
			while(1)
			{
				if(ctrlerr) break;
				printf("\rA:%i B:%i Select:%i Start:%i Right:%i Left:%i Up:%i Down:%i R:%i L:%i",
					!!(pads&1),!!(pads&2),!!(pads&4),!!(pads&8),!!(pads&16),!!(pads&32),!!(pads&64),!!(pads&128),!!(pads&256),!!(pads&512));
				VIDEO_WaitVSync();
			}
		}
	}
	return 0;
}
