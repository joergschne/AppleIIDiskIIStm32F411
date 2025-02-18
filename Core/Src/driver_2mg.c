
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fatfs.h"

#include "driver_2mg.h"
#include "utils.h"
#include "main.h"
#include "log.h"

extern long database;                                            // start of the data segment in FAT
extern int csize;  
extern volatile enum FS_STATUS fsState;
unsigned int fat2mgCluster[20];
extern image_info_t mountImageInfo;

#define NIBBLE_BLOCK_SIZE  416 //402
#define NIBBLE_SECTOR_SIZE 512

#define IMG2_HEADER_SIZE 64

/*
static const unsigned char scramble[] = {
	0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
	};
*/

static  unsigned char   dsk2nicSectorMap[] = {0, 0x7, 0xe, 0x6, 0xd, 0x5, 0xc, 0x4, 0xb, 0x3, 0xa, 0x2, 0x9, 0x1, 0x8, 0xf};
static  unsigned char   po2nicSectorMap[] = {0, 0x8, 0x1, 0x9, 0x2, 0xa, 0x3, 0xb, 0x4, 0xc, 0x5, 0xd, 0x6, 0xe, 0x7, 0xf};

static const char encTable[] = {
	0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,
	0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
	0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
	0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
	0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,
	0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
	0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,
	0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};
static const char decTable[] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x02,0x03,0x00,0x04,0x05,0x06,
	0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x08,0x00,0x00,0x00,0x09,0x0a,0x0b,0x0c,0x0d,
	0x00,0x00,0x0e,0x0f,0x10,0x11,0x12,0x13,0x00,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1b,0x00,0x1c,0x1d,0x1e,
	0x00,0x00,0x00,0x1f,0x00,0x00,0x20,0x21,0x00,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
	0x00,0x00,0x00,0x00,0x00,0x29,0x2a,0x2b,0x00,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,
	0x00,0x00,0x33,0x34,0x35,0x36,0x37,0x38,0x00,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f
};

// for bit flip

static const unsigned char FlipBit1[4] = { 0, 2, 1, 3 };
static const unsigned char FlipBit2[4] = { 0, 8, 4, 12 };
static const unsigned char FlipBit3[4] = { 0, 32, 16, 48 };

__uint32_t IMG2_DataBlockCount;
__uint32_t IMG2_DataOffset;
__uint32_t IMG2_DataByteCount;

int get2mgTrackFromPh(int phtrack){
    return phtrack >> 2;
}

unsigned int get2mgTrackSize(int trk){
    return 16*NIBBLE_BLOCK_SIZE*8;
}

long get2mgSDAddr(int trk,int block,int csize, long database){
    int long_sector = trk*8;                    // DSK & PO are 256 long and not 512 a track is 4096
    int long_cluster = long_sector >> 6;
    int ft = fat2mgCluster[long_cluster];
    long rSector=database+(ft-2)*csize+(long_sector & (csize-1));
    return rSector;
}

enum STATUS get2mgTrackBitStream(int trk,unsigned char * buffer){
    int addr=get2mgSDAddr(trk,0,csize,database);
    const unsigned int blockNumber=8; 

    if (addr==-1){
        log_error("Error getting SDCard Address for DSK\n");
        return RET_ERR;
    }

    unsigned char * tmp=(unsigned char *)malloc(4096*sizeof(char));
    
    if (tmp==NULL){
        log_error("unable to allocate tmp for 4096 Bytes");
        return RET_ERR;
    }

    getDataBlocksBareMetal(addr,tmp,blockNumber);          // Needs to be improved and to remove the zeros
    while (fsState!=READY){}
    
    if (img22Nic(tmp,buffer,trk)==RET_ERR){
        log_error("dsk2nic return an error");
        free(tmp);
        return RET_ERR;
    }
    free(tmp);
    return RET_OK;
}

enum STATUS set2mgTrackBitStream(int trk,unsigned char * buffer){

return RET_OK;
}

enum STATUS mount2mgFile(char * filename){
    FRESULT fres; 
    FIL fil;  

    fres = f_open(&fil,filename , FA_READ);     // Step 2 Open the file long naming

    if(fres != FR_OK){
        log_error("File open Error: (%i)\r\n", fres);
        return -1;
    }

    long clusty=fil.obj.sclust;
    int i=0;
    fat2mgCluster[i]=clusty;
    log_info("file cluster %d:%ld\n",i,clusty);
    
    while (clusty!=1 && i<30){
        i++;
        clusty=get_fat((FFOBJID*)&fil,clusty);
        log_info("file cluster %d:%ld",i,clusty);
        fat2mgCluster[i]=clusty;
    }

    unsigned int pt;
    char * img2_header=(char*)malloc(IMG2_HEADER_SIZE*sizeof(char));
    f_read(&fil,img2_header,IMG2_HEADER_SIZE,&pt);

    if (!memcmp(img2_header,"2IMG",4)){                    //57 4F 5A 31
        log_info("Image:2mg 2IMG signature");

    }else if (!memcmp(img2_header,"GIMI2",4)){
        log_info("Image:2mg GIM2 Signature");
    }else{
        log_error("Error: not a woz file");
        return RET_ERR;
    }
    char * creator_id=(char*)malloc(5*sizeof(char));
    memcpy(creator_id,img2_header,4);
    creator_id[5]=0x0;

    log_info("creator ID:%s",creator_id);

    IMG2_DataBlockCount=get_u32le(img2_header+0x14);
    IMG2_DataOffset=get_u32le(img2_header+0x18);
    IMG2_DataByteCount=get_u32le(img2_header+0x1c);

    

    if(IMG2_DataBlockCount != 1600 && IMG2_DataBlockCount != 16390){
        log_error("Wrong 512x data block count ");
        return RET_ERR;
    }else{
        log_info("2MG 512 data blocks:%ld",IMG2_DataBlockCount);
        log_info("2MG Bytes size:%ld",IMG2_DataByteCount);
    }
		

    free(img2_header);

    f_close(&fil);

    return 0;
}

enum STATUS img22Nic(unsigned char *rawByte,unsigned char *buffer,uint8_t trk){

    const unsigned char volume = 0xfe;

    int i=0;
    char dst[512];
    char src[256+2];
    unsigned char * sectorMap;
    if (mountImageInfo.type==2)
        sectorMap=dsk2nicSectorMap;
    else if (mountImageInfo.type==3)
        sectorMap=po2nicSectorMap;
    else{
        log_error("Unable to match sectorMap with mountImageInfo.type");
        return RET_ERR;
    }
        
    for (i=0; i<0x16; i++) 
        dst[i]=0xff;

    // sync header
    dst[0x16]=0x03;
    dst[0x17]=0xfc;
    dst[0x18]=0xff;
    dst[0x19]=0x3f;
    dst[0x1a]=0xcf;
    dst[0x1b]=0xf3;
    dst[0x1c]=0xfc;
    dst[0x1d]=0xff;
    dst[0x1e]=0x3f;
    dst[0x1f]=0xcf;
    dst[0x20]=0xf3;
    dst[0x21]=0xfc;	

    // address header
    dst[0x22]=0xd5;
    dst[0x23]=0xaa;
    dst[0x24]=0x96;
    dst[0x2d]=0xde;
    dst[0x2e]=0xaa;
    dst[0x2f]=0xeb;
    
    // sync header
    for (i=0x30; i<0x35; i++) 
        dst[i]=0xff;

    // data
    dst[0x35]=0xd5;
    dst[0x36]=0xaa;
    dst[0x37]=0xad;
    dst[0x18f]=0xde;
    dst[0x190]=0xaa;
    dst[0x191]=0xeb;
    
    for (i=0x192; i<0x1a0; i++) 
        dst[i]=0xff;
    
    for (i=0x1a0; i<0x200; i++) 
        dst[i]=0x00;

    for (u_int8_t sector=0;sector<16;sector++){
        uint8_t sm=sectorMap[sector];
        memcpy(src,rawByte+sm * 256,256);
        src[256] = src[257] = 0;

        unsigned char c, x, ox = 0;

        dst[0x25]=((volume>>1)|0xaa);
        dst[0x26]=(volume|0xaa);
        dst[0x27]=((trk>>1)|0xaa);
        dst[0x28]=(trk|0xaa);
        dst[0x29]=((sector>>1)|0xaa);
        dst[0x2a]=(sector|0xaa);

        c = (volume^trk^sector);
        dst[0x2b]=((c>>1)|0xaa);
        dst[0x2c]=(c|0xaa);

        for (i = 0; i < 86; i++) {
            x = (FlipBit1[src[i] & 3] | FlipBit2[src[i + 86] & 3] | FlipBit3[src[i + 172] & 3]);
			dst[i+0x38] = encTable[(x^ox)&0x3f];
            ox = x;
        }

        for (i = 0; i < 256; i++) {
            x = (src[i] >> 2);
            dst[i+0x8e] = encTable[(x ^ ox) & 0x3f];
            ox = x;
        }
        
        dst[0x18e]=encTable[ox & 0x3f];
        memcpy(buffer+sector*NIBBLE_BLOCK_SIZE,dst,NIBBLE_BLOCK_SIZE);
    }
    return RET_OK;
}


enum STATUS nic22mg(char *rawByte,unsigned char *buffer,uint8_t trk){
return RET_OK;
}




