/*
 * Lab #1 : NAND Simulator
 *  - Embedded Systems Design, ICE3028 (Fall, 2021)
 *
 * Sep. 16, 2021.
 *
 * TA: Youngjae Lee, Jeeyoon Jung
 * Prof: Dongkun Shin
 * Embedded Software Laboratory
 * Sungkyunkwan University
 * http://nyx.skku.ac.kr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nand.h"

/*
 * define your own data structure for NAND flash implementation
 */
typedef struct  _page {
    void * data;
    void * spare;
} page;
typedef struct _block {
    char IsEmpty;
    int write_pos;
    page * pages;
} block;
typedef struct _bank {
    block * blocks;
} bank;
typedef struct _pos {
    int bank, blk, page;
} pos;

int nbank, nblk, npage;
char*** nand_bitmap;
bank* banks;



/*
 * initialize the NAND flash memory
 * @nbanks: number of bank
 * @nblks: number of blocks per bank
 * @npages: number of pages per block
 *
 * Returns:
 *   0 on success
 *   NAND_ERR_INVALID if given dimension is invalid
 */

int nand_init(int nbanks, int nblks, int npages)
{
	if(nbanks<=0 || nblks<=0 || npages<=0) return NAND_ERR_INVALID;

    int i, j, k;
    nbank = nbanks; nblk = nblks; npage = npages;

    nand_bitmap = (char ***)malloc(sizeof(char**)*nbanks);
    
    for(i=0; i<nbanks; i++)
    {
        nand_bitmap[i] = (char**)malloc(sizeof(char*)*nblks);
        for(j=0; j<nblks; j++)
        {
            nand_bitmap[i][j] = (char*)malloc(sizeof(char)*npages);
            for(k=0; k<npages; k++)
            {
                nand_bitmap[i][j][k] = '0';
            }
        }
    }
    
    banks = (bank *)malloc(sizeof(bank)*nbanks);
    for(i=0; i<nbanks; i++)
    {
        banks[i].blocks= (block *)malloc(sizeof(block)*nblks);
        for(j=0; j<nblks; j++)
        {
            banks[i].blocks[j].IsEmpty = '1';
            banks[i].blocks[j].write_pos = 0;
            banks[i].blocks[j].pages = (page *)malloc(sizeof(page)*npages);
            for(k=0; k<npages; k++)
            {
                banks[i].blocks[j].pages[k].data = (char*)malloc(32);
                banks[i].blocks[j].pages[k].spare = (char*)malloc(4);
            }
        }
    }
    
    return NAND_SUCCESS;
}

/*
 * write data and spare into the NAND flash memory page
 *
 * Returns:
 *   0 on success
 *   NAND_ERR_INVALID if target flash page address is invalid
 *   NAND_ERR_OVERWRITE if target page is already written
 *   NAND_ERR_POSITION if target page is empty but not the position to be written
 */
int nand_write(int bank, int blk, int page, void *data, void *spare)
{
    //printf("nand : write at %d %d %d\n", bank, blk, page);
    
    if(bank<0 || blk<0 || page<0 || bank>=nbank || blk>=nblk || page>=npage) return NAND_ERR_INVALID;
    if(nand_bitmap[bank][blk][page] == '1') return NAND_ERR_OVERWRITE;
    if(banks[bank].blocks[blk].write_pos != page) return NAND_ERR_POSITION; 
    
    //printf("write ");
    int i; //char tmp[4];
    for(i=0; i<32; i++)
    {
        //tmp[i%4] = *(int*)(data+i);
        *(char*)(banks[bank].blocks[blk].pages[page].data+i) = *(char*)(data+i);
        //if(i%4 == 3) printf("%x ", *(int*)tmp);
    }
    for(i=0; i<4; i++)
    {
        *(char*)(banks[bank].blocks[blk].pages[page].spare+i) = *(char*)(spare+i);
    }
    //printf("\n");


    nand_bitmap[bank][blk][page] = '1';
    banks[bank].blocks[blk].IsEmpty = '0';
    banks[bank].blocks[blk].write_pos++; 
    
    
    //strncpy((char*)banks[bank].blocks[blk].pages[page].data, (char*)data, 32);
    //strncpy((char*)banks[bank].blocks[blk].pages[page].spare, (char*)spare, 4);
    
    return NAND_SUCCESS;
}


/*
 * read data and spare from the NAND flash memory page
 *
 * Returns:
 *   0 on success
 *   NAND_ERR_INVALID if target flash page address is invalid
 *   NAND_ERR_EMPTY if target page is empty
 */
int nand_read(int bank, int blk, int page, void *data, void *spare)
{
	//printf("nand : read at %d %d %d\n", bank, blk, page);
        
    if(bank<0 || blk<0 || page<0 || bank>=nbank || blk>=nblk || page>=npage) return NAND_ERR_INVALID;
    if(nand_bitmap[bank][blk][page] == '0') return NAND_ERR_EMPTY;
    
//    printf("read data : %8x %8x\n", banks[bank].blocks[blk].pages[page].data[0], banks[bank].blocks[blk].pages[page].data[1]);
    //printf("read ");
    int i; //char tmp[4];
    for(i=0; i<32; i++)
    {
        //tmp[i%4] = *(int*)(banks[bank].blocks[blk].pages[page].data+i);
        *(char*)(data+i) = *(char*)(banks[bank].blocks[blk].pages[page].data+i);
        //if(i%4 == 3) printf("%x ", *(int*)tmp);
    }
    for(i=0; i<4; i++)
    {
        *(char*)(spare+i) = *(char*)(banks[bank].blocks[blk].pages[page].spare+i);
    }

    //printf("\n");
    
    
    //strncpy((char*)data, (char*)banks[bank].blocks[blk].pages[page].data, 32);
    //strncpy((char*)spare, (char*)banks[bank].blocks[blk].pages[page].spare, 4);
    return NAND_SUCCESS;
}

/*
 * erase the NAND flash memory block
 *
 * Returns:
 *   0 on success
 *   NAND_ERR_INVALID if target flash block address is invalid
 *   NAND_ERR_EMPTY if target block is already erased
 */
int nand_erase(int bank, int blk)
{
	
	if(bank<0 || blk<0 || bank>=nbank || blk>=nblk) return NAND_ERR_INVALID;
    if(banks[bank].blocks[blk].IsEmpty == '1')  return NAND_ERR_EMPTY;
    
    int i;
    banks[bank].blocks[blk].IsEmpty = '1';
    banks[bank].blocks[blk].write_pos = 0;
    for(i=0; i<npage; i++)
    {
        nand_bitmap[bank][blk][i] = '0';
    }
    

    return NAND_SUCCESS;
}
