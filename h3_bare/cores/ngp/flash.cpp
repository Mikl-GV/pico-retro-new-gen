//---------------------------------------------------------------------------
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version. See also the license.txt file for
//	additional informations.
//---------------------------------------------------------------------------

//
// Flash chip emulation by Flavor
//   with ideas from Koyote (who originally got ideas from Flavor :)
// for emulation of NGPC carts
//

#ifndef __GP32__
#include "StdAfx.h"
#endif
#include "memory.h"
#include "flash.h"
#include <string.h>

//#define DEBUG_FLASH
#ifdef DEBUG_FLASH
FILE *debugFile = NULL;
#define stderr debugFile
#define stdout debugFile
#endif


/* Manuf ID's
Supported
0x98		Toshiba
0xEC		Samsung
0xB0		Sharp

Other
0x89		Intel
0x01		AMD
0xBF		SST
*/
unsigned char manufID = 0x98;   //we're always Toshiba!
unsigned char deviceID = 0x2F;
unsigned char cartSize = 32;
unsigned long bootBlockStartAddr = 0x1F0000;
unsigned char bootBlockStartNum = 31;
//unsigned long cartAddrMask = 0x3FFFFF;

unsigned char currentWriteCycle = 1;  //can be 1 through 6
unsigned char currentCommand = NO_COMMAND;
unsigned char needToWriteFile = 0;

#define MAX_BLOCKS 35 //a 16m chip has 35 blocks (SA0-SA34)
unsigned char blocksDirty[2][MAX_BLOCKS];  //max of 2 chips
char ngfFilename[300] = {0};

#define FLASH_WRITE 0
#define FLASH_ERASE 1



void setupFlashParams()
{
    switch(cartSize)
    {
        default:
        case 32:
	        deviceID = 0x2F;  //the upper chip will always be 16bit
            bootBlockStartAddr = 0x1F0000;
            bootBlockStartNum = 31;
            //cartAddrMask=0x3FFFFF;
            break;
        case 16:
			deviceID = 0x2F;
            bootBlockStartAddr = 0x1F0000;
            bootBlockStartNum = 31;
            //cartAddrMask=0x1FFFFF;
            break;
        case 8:
	        deviceID = 0x2C;
            bootBlockStartAddr = 0xF0000;
            bootBlockStartNum = 15;
            //cartAddrMask=0x0FFFFF;
            break;
        case 4:
	        deviceID = 0xAB;
            bootBlockStartAddr = 0x70000;
            bootBlockStartNum = 7;
            //cartAddrMask=0x07FFFF;
            break;
        case 0:
	        manufID = 0x00;
	        deviceID = 0x00;
            bootBlockStartAddr = 0x00000;
            bootBlockStartNum = 0;
            //cartAddrMask=0;
            break;
    }
}

unsigned char blockNumFromAddr(unsigned long addr)
{
    addr &= 0x1FFFFF/* & cartAddrMask*/;

    if(addr >= bootBlockStartAddr)
    {
        unsigned long bootAddr = addr-bootBlockStartAddr;
        //boot block is 32k, 8k, 8k, 16k (0x8000,0x2000,0x2000,0x4000)
        if(bootAddr < 0x8000)
            return (bootBlockStartAddr / 0x10000);
        else if(bootAddr < 0xA000)
            return (bootBlockStartAddr / 0x10000) + 1;
        else if(bootAddr < 0xC000)
            return (bootBlockStartAddr / 0x10000) + 2;
        else if(bootAddr < 0x10000)
            return (bootBlockStartAddr / 0x10000) + 3;
    }

    return addr / 0x10000;
}

unsigned long blockNumToAddr(unsigned char chip, unsigned char blockNum)
{
    unsigned long addr;

    if(blockNum >= bootBlockStartNum)
    {
        addr = bootBlockStartNum * 0x10000;

        unsigned char bootBlock = blockNum - bootBlockStartNum;
        if(bootBlock>=1)
            addr+= 0x8000;
        if(bootBlock>=2)
            addr+= 0x2000;
        if(bootBlock>=3)
            addr+= 0x2000;
    }
    else
        addr = blockNum * 0x10000;

    if(chip)
        addr+=0x200000;

	return addr;
}

unsigned long blockSize(unsigned char blockNum)
{
    if(blockNum >= bootBlockStartNum)
    {
        unsigned char bootBlock = blockNum - bootBlockStartNum;
        if(bootBlock==3)
            return 0x4000;
        if(bootBlock==2)
            return 0x2000;
        if(bootBlock==1)
            return 0x2000;
        if(bootBlock==0)
            return 0x8000;
    }

    return 0x10000;
}

void flashWriteByte(unsigned long addr, unsigned char data, unsigned char operation)
{
    //addr &= cartAddrMask;  //the stuff gets mirrored to the higher slots.

	if(blockNumFromAddr(addr) == 0)  //hack because DWARP writes to bank 0
		return;

	//set a dirty flag for the block that we are writing to
	if(addr < 0x200000)
	{
        blocksDirty[0][blockNumFromAddr(addr)] = 1;
        needToWriteFile = 1;
	}
	else if(addr < 0x400000)
	{
        blocksDirty[1][blockNumFromAddr(addr)] = 1;
        needToWriteFile = 1;
	}
	else
		return;  //panic

	//changed to &= because it's actually how flash works
	//flash memory can be erased (changed to 0xFF)
	//and when written, 1s can become 0s, but you can't turn 0s into 1s (except by erasing)
	if(operation == FLASH_ERASE)
		mainrom[addr] = 0xFF;		//we're just erasing, so set to 0xFF
	else
		mainrom[addr] &= data;		//actually writing data
}

unsigned char flashReadInfo(unsigned long addr)
{
    currentWriteCycle = 1;
    currentCommand = COMMAND_INFO_READ;

    switch(addr&0x03)
    {
        case 0:
            return manufID;
        case 1:
            return deviceID;
        case 2:
            return 0;  //block not protected
        case 3:  //thanks Koyote
		default:
            return 0x80;
    }
}

void flashChipWrite(unsigned long addr, unsigned char data)
{
    if(addr >= 0x800000 && cartSize != 32)
        return;

    switch(currentWriteCycle)
    {
        case 1:
            if((addr & 0xFFFF) == 0x5555 && data == 0xAA)
                currentWriteCycle++;
            else if(data == 0xF0)
			{
                currentWriteCycle=1;//this is a reset command
				writeSaveGameFile();
			}
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;
        case 2:
            if((addr & 0xFFFF) == 0x2AAA && data == 0x55)
                currentWriteCycle++;
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;
        case 3:
            if((addr & 0xFFFF) == 0x5555 && data == 0x80)
                currentWriteCycle++;//continue on
            else if((addr & 0xFFFF) == 0x5555 && data == 0xF0)
			{
                currentWriteCycle=1;
				writeSaveGameFile();
			}
            else if((addr & 0xFFFF) == 0x5555 && data == 0x90)
            {
                currentWriteCycle++;
                currentCommand = COMMAND_INFO_READ;
                //now, the next time we read from flash, we should return a ID value
                //  or a block protect value
                break;
            }
            else if((addr & 0xFFFF) == 0x5555 && data == 0xA0)
            {
                currentWriteCycle++;
                currentCommand = COMMAND_BYTE_PROGRAM;
                break;
            }
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;

        case 4:
            if(currentCommand == COMMAND_BYTE_PROGRAM)//time to write to flash memory
            {
				if(addr >= 0x200000 && addr < 0x400000)
					addr -= 0x200000;
				else if(addr >= 0x800000 && addr < 0xA00000)
					addr -= 0x600000;

				//should be changed to just write to mainrom
				flashWriteByte(addr, data, FLASH_WRITE);

                currentWriteCycle=1;
            }
            else if((addr & 0xFFFF) == 0x5555 && data == 0xAA)
                currentWriteCycle++;
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;
        case 5:
            if((addr & 0xFFFF) == 0x2AAA && data == 0x55)
                currentWriteCycle++;
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;
        case 6:
            if((addr & 0xFFFF) == 0x5555 && data == 0x10)//chip erase
            {
                currentWriteCycle=1;
                currentCommand = COMMAND_CHIP_ERASE;

                //erase the entire chip
                //memset it to all 0xFF
                //I think we won't implement this

                break;
            }
            if(data == 0x30 || data == 0x50)//block erase
            {
                unsigned char chip=0;
                currentWriteCycle=1;
                currentCommand = COMMAND_BLOCK_ERASE;

                //erase the entire block that contains addr
                //memset it to all 0xFF

                if(addr >= 0x800000)
                    chip = 1;

                vectFlashErase(chip, blockNumFromAddr(addr));
                break;
            }
            else
                currentWriteCycle=1;

            currentCommand = NO_COMMAND;
            break;


        default:
            currentWriteCycle = 1;
            currentCommand = NO_COMMAND;
            break;
    }
}

//this should be called when a ROM is unloaded
void flashShutdown()
{
	writeSaveGameFile();
}

//this should be called when a ROM is loaded
void flashStartup()
{
	memset(blocksDirty[0], 0, MAX_BLOCKS*sizeof(blocksDirty[0][0]));
	memset(blocksDirty[1], 0, MAX_BLOCKS*sizeof(blocksDirty[0][0]));
	needToWriteFile = 0;
}

void vectFlashWrite(unsigned char chip, unsigned int to, unsigned char *fromAddr, unsigned int numBytes)
{
    if(chip)
        to+=0x200000;

    //memcpy(dest,fromAddr,numBytes);
	while(numBytes--)
	{
		flashWriteByte(to, *fromAddr, FLASH_WRITE);
		fromAddr++;
		to++;
	}
}

void vectFlashErase(unsigned char chip, unsigned char blockNum)
{
    //this needs to be modified to take into account boot block areas (less than 64k)
    unsigned long blockAddr = blockNumToAddr(chip, blockNum);
	unsigned long numBytes = blockSize(blockNum);

    //memset block to 0xFF
    //memset(&mainrom[blockAddr], 0xFF, numBytes);
	while(numBytes--)
	{
		flashWriteByte(blockAddr, 0xFF, FLASH_ERASE);
		blockAddr++;
	}
}

void vectFlashChipErase(unsigned char chip)
{
}

void setFlashSize(unsigned long romSize)
{
    //add individual hacks here.
    if(strncmp((const char *)&mainrom[0x24], "DELTA WARP ", 11)==0)//delta warp
    {
        //1 8mbit chip
        cartSize = 8;
    }
    else if(romSize > 0x200000)
    {
        //2 16mbit chips
        cartSize = 32;
    }
    else if(romSize > 0x100000)
    {
        //1 16mbit chip
        cartSize = 16;
    }
    else if(romSize > 0x080000)
    {
        //1 8mbit chip
        cartSize = 8;
    }
    else if(romSize > 0x040000)
    {
        //1 4mbit chip
        cartSize = 4;
    }
    else if(romSize == 0)  //no cart, just emu BIOS
    {
        cartSize = 0;
    }
    else
    {
        //we don't know.  It's probablly a homebrew or something cut down
        // so just pretend we're a Bung! cart
        //2 16mbit chips
        cartSize = 32;
    }

    setupFlashParams();

	flashStartup();
}