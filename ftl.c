/*
 * Lab #2 : Page Mapping FTL Simulator
 *  - Embedded Systems Design, ICE3028 (Fall, 2021)
 *
 * Sep. 23, 2021.
 *
 * TA: Youngjae Lee, Jeeyoon Jung
 * Prof: Dongkun Shin
 * Embedded Software Laboratory
 * Sungkyunkwan University
 * http://nyx.skku.ac.kr
 */

#include "ftl.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct _p_data{
    u32 PBN, PPN;
} p_data;
p_data * PMT;
typedef struct _cmt_data{
    int map_page;
    u32 data[N_MAP_ENTRIES_PER_PAGE];
    int dirty;
} cmt_data;
typedef struct _buff_data{
    int lpn;
    u32 data[SECTORS_PER_PAGE];
} buff_data;


int n_page_invalid[N_BANKS][BLKS_PER_BANK];
p_data next_page[N_BANKS][3];

int n_data_block[N_BANKS];
int n_trans_block[N_BANKS];
int block_state[N_BANKS][BLKS_PER_BANK]; // 0 : free, 1 : data, 2 : translation

cmt_data CMT[N_BANKS][N_CACHED_MAP_PAGE_PB];
p_data GTD[N_BANKS][N_MAP_PAGES_PB];
int cache_count[N_BANKS][N_CACHED_MAP_PAGE_PB];

buff_data buffer[N_BUFFERS];
int buffer_sector_bitmap[N_BUFFERS][SECTORS_PER_PAGE];
int buffer_empty_page = N_BUFFERS;
// you must increase stats.cache_hit when L2P is in CMT 
void get_next_page(u32 bank, u32* nextPBN, u32* nextPPN, int status) //status -  1 : data, 2: trans
{
    if(next_page[bank][status].PBN == -1 || next_page[bank][status].PPN + 1 >= PAGES_PER_BLK)
    {
        for(int i = 0; i < BLKS_PER_BANK; i++)
        {
            if(block_state[bank][i] == 0)
            {
                next_page[bank][status].PBN = i;
                next_page[bank][status].PPN = 0;
                break;
            }
        }
    }
    else next_page[bank][status].PPN++; 
    
    *nextPBN = next_page[bank][status].PBN; *nextPPN = next_page[bank][status].PPN;
}
int find_cache_victim(u32 bank)
{
    int max = -1;
    int victim_page = -1;
    for(int i = 0; i < N_CACHED_MAP_PAGE_PB; i++)
    {
        if(cache_count[bank][i] > max)
        {
            max = cache_count[bank][i];
            victim_page = i;
        }
    }
    return victim_page;
}

void update_cache_count(u32 bank, int cache_slot)
{
    for(int i = 0; i < N_CACHED_MAP_PAGE_PB; i++)
    {
        if(i == cache_slot) cache_count[bank][i] = 0;
        else cache_count[bank][i]++;
    }
}

static void map_garbage_collection(u32 bank)
{
    stats.map_gc_cnt++;
    
    int max_invalid = 0;
    int victim_blk = -1;
    int free_blk = -1;
    int free_page_write = 0;

    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] == 0) 
        {
            free_blk = i;
            break;
        }
    }
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] != 2) continue;
        if(n_page_invalid[bank][i] > max_invalid)
        {
            max_invalid = n_page_invalid[bank][i];
            victim_blk = i;
        }
    }
    block_state[bank][free_blk] = 2;

    u32* data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    for(int i = 0; i < PAGES_PER_BLK; i++)
    {
        nand_read(bank, victim_blk, i, data, &spare);
        if(GTD[bank][spare].PBN != victim_blk || GTD[bank][spare].PPN != i) continue;
        else
        {
            nand_write(bank, free_blk, free_page_write, data, &spare);
            stats.map_gc_write++;
            GTD[bank][spare].PBN = free_blk; GTD[bank][spare].PPN = free_page_write++;
        }
    }
    free(data);
    block_state[bank][victim_blk] = 0;
    n_page_invalid[bank][victim_blk] = 0;
    nand_erase(bank, victim_blk);

    next_page[bank][2].PBN = free_blk; next_page[bank][2].PPN = free_page_write;
}
static void map_write(u32 bank, u32 map_page, u32 cache_slot)
{
    if(CMT[bank][cache_slot].dirty == 0) 
    {
        printf("ERROR : map_write call with clean cache_slot %d\n", cache_slot);
        return;
    }
    CMT[bank][cache_slot].dirty = 0;
    map_page = CMT[bank][cache_slot].map_page;
    CMT[bank][cache_slot].map_page = -1;
    
    u32 nextPBN = -1; u32 nextPPN = -1;
    get_next_page(bank, &nextPBN, &nextPPN, 2);

    if(block_state[bank][nextPBN] == 0)
    {
        block_state[bank][nextPBN] = 2;
        n_trans_block[bank]++;
    }
    
    if(n_trans_block[bank] == N_PHY_MAP_BLK)
    {
        block_state[bank][nextPBN] = 0;
        n_trans_block[bank]--;
        map_garbage_collection(bank);
        nextPBN = next_page[bank][2].PBN; nextPPN = next_page[bank][2].PPN;

    }

    if(GTD[bank][map_page].PBN != -1)
    {
        int prePBN = GTD[bank][map_page].PBN; 
        n_page_invalid[bank][prePBN]++;
    }

    GTD[bank][map_page].PBN = nextPBN; GTD[bank][map_page].PPN = nextPPN;

    nand_write(bank, nextPBN, nextPPN, CMT[bank][cache_slot].data, &map_page);
    stats.map_write++;

    for(int i = 0; i < SECTORS_PER_PAGE; i++)
    {
        CMT[bank][cache_slot].data[i] = 0xFFFFFFFF;
    }
    
    return;
}
static void map_read(u32 bank, u32 map_page, u32 cache_slot)
{
    u32* read_data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE);
    u32 spare;

    if(CMT[bank][cache_slot].dirty == 1)  map_write(bank, CMT[bank][cache_slot].map_page, cache_slot);
    
    if(GTD[bank][map_page].PBN == -1)
    {
        for(int i = 0; i < SECTORS_PER_PAGE; i++)
            read_data[i] = 0xFFFFFFFF;
    }
    else nand_read(bank, GTD[bank][map_page].PBN, GTD[bank][map_page].PPN, read_data, &spare);
    CMT[bank][cache_slot].map_page = map_page;
    CMT[bank][cache_slot].dirty = 0;
    
    for(int i = 0; i< SECTORS_PER_PAGE; i++)
    {
        CMT[bank][cache_slot].data[i] = read_data[i];
    }
    
    free(read_data);

    return;
}
u32 lpn2ppn(int lpn)
{
    int bank = lpn % N_BANKS;
    int map_page = lpn / (N_BANKS * N_MAP_ENTRIES_PER_PAGE); 
    u32 free_cache_slot = -1;

    int cache_hit = 0;
    for(int i = 0; i< N_CACHED_MAP_PAGE_PB; i++)
    {
        if(CMT[bank][i].map_page == map_page)
        {
            cache_hit = 1;
            stats.cache_hit++;
            update_cache_count(bank, i);
            return i;
        }
        else if(CMT[bank][i].map_page == -1) free_cache_slot = i;
    }
    if(!cache_hit)
    {
        stats.cache_miss++;
        if(free_cache_slot == -1) free_cache_slot = find_cache_victim(bank);
        map_read(bank, map_page, free_cache_slot);
        update_cache_count(bank, free_cache_slot);
        return free_cache_slot;
        
    
    }
    return -1;
}

static void garbage_collection(u32 bank)
{
    int max_invalid = 0;
    int victim_blk = -1;
    int free_blk = -1;
    int free_page_write = 0;
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] == 0) 
        {
            free_blk = i;
            break;
        }
    }
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] != 1) continue;
        if(n_page_invalid[bank][i] > max_invalid)
        {
            max_invalid = n_page_invalid[bank][i];
            victim_blk = i;
        }
    }
    block_state[bank][free_blk] = 1;

    u32* data = malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    for(int i = 0; i < PAGES_PER_BLK; i++)
    {
        nand_read(bank, victim_blk, i, data, &spare); 
        int offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
        u32 cash_slot = lpn2ppn(spare);
        if(CMT[bank][cash_slot].data[offset] != victim_blk * PAGES_PER_BLK + i) continue;
        else
        {
            nand_write(bank, free_blk, free_page_write, data, &spare);
            stats.gc_write++;

            offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
            CMT[bank][cash_slot].data[offset] = free_blk * PAGES_PER_BLK + free_page_write++;
            CMT[bank][cash_slot].map_page = spare / (N_BANKS * SECTORS_PER_PAGE);
            CMT[bank][cash_slot].dirty = 1;
        }
    }
    free(data); 
    block_state[bank][victim_blk] = 0;
    n_page_invalid[bank][victim_blk] = 0;
    nand_erase(bank, victim_blk);
   
    next_page[bank][1].PBN = free_blk; next_page[bank][1].PPN = free_page_write;
    
    stats.gc_cnt++;
/***************************************
Add

stats.gc_write++;

for every nand_write call (every valid page copy)
that you issue in this function
***************************************/

	return;
}

void ftl_open()
{
    nand_init(N_BANKS, BLKS_PER_BANK, PAGES_PER_BLK);

    for(int i=0; i<N_BANKS; i++)
    {
        next_page[i][1].PBN = -1; next_page[i][1].PPN = -1;
        next_page[i][2].PBN = -1; next_page[i][2].PPN = -1;
        for(int j = 0; j<N_CACHED_MAP_PAGE_PB; j++)
        {
            CMT[i][j].dirty = 0;
            CMT[i][j].map_page = -1;
            for(int k = 0; k < N_MAP_ENTRIES_PER_PAGE; k++)    
            {
                CMT[i][j].data[k] = -1;
            }
        }

        for(int j = 0; j < N_MAP_PAGES_PB; j++)
        {
            GTD[i][j].PBN = -1; GTD[i][j].PPN = -1;
        }
    }

    for(int i = 0; i < N_BUFFERS; i++)
    {
        buffer[i].lpn = -1;
    }
}

int buffer_read(u32 lpn, u32 offset, u32 nsect, u32* read_buffer, int* sector_bitmap)
{
    int flag1 = 0; int flag2 = 1;
    for(int i = 0; i < N_BUFFERS; i++)
    {
        if(buffer[i].lpn == lpn)
        {
            flag1 = 1;
            for(int j = offset; j < offset+nsect; j++)
            {
                if(j >= SECTORS_PER_PAGE) break;
                if(buffer_sector_bitmap[i][j] == 1) 
                {
                    read_buffer[j] = buffer[i].data[j];
                    sector_bitmap[j] = 1;   
                }
                else 
                {
                    sector_bitmap[j] = 0;
                    flag2 = 0;
                }       
            }
            break;
        }
    }

    return flag1*flag2;
}
void ftl_read(u32 lba, u32 nsect, u32 *read_buffer)
{
    u32 lpn = lba/SECTORS_PER_PAGE, offset = lba%SECTORS_PER_PAGE;
    u32* buffer_data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE);
    int* sector_bitmap = (int*)malloc(sizeof(int)*SECTORS_PER_PAGE);
    u32* data = (u32*)malloc(sizeof(u32) * SECTORS_PER_PAGE); u32 spare;
    u32 tmpnsect = nsect;
    u32 rb_cursor = 0;
    while(1)
    {
        for(int i = 0; i < SECTORS_PER_PAGE; i++)
            sector_bitmap[i] = 0;
        int isbuffer = buffer_read(lpn, offset, tmpnsect, buffer_data, sector_bitmap);
        if(isbuffer)
        {
            for(int i = 0; i < SECTORS_PER_PAGE; i++)
                data[i] = buffer_data[i];
        }
        else
        {
            u32 bank = lpn%N_BANKS;
            int map_page_offset = (lpn / N_BANKS) % SECTORS_PER_PAGE;
            u32 read_ppn = -1; 
            u32 cash_slot = lpn2ppn(lpn);
            read_ppn = CMT[bank][cash_slot].data[map_page_offset];
        
            int pbn = read_ppn / PAGES_PER_BLK; int ppn = read_ppn % PAGES_PER_BLK;

            if(read_ppn == -1)
            {
                for(int i = 0; i<SECTORS_PER_PAGE; i++)
                {
                    data[i] = 0xFFFFFFFF;
                }
            }
            else nand_read(bank, pbn, ppn, data, &spare);
            for(int i = 0; i < SECTORS_PER_PAGE; i++)
            {
                if(sector_bitmap[i] == 1) data[i] = buffer_data[i];
            }
        }

        if(offset + tmpnsect > SECTORS_PER_PAGE)
        {
            for(int i = offset; i < SECTORS_PER_PAGE; i++)
            {
                read_buffer[rb_cursor++] = data[i];
            }
            
            tmpnsect -= SECTORS_PER_PAGE - offset;
            offset = 0;
            lpn++;
        }
        else
        {
            for(int i = offset; i < offset + tmpnsect; i++)
            {
                read_buffer[rb_cursor++] = data[i];
            }
            break;
        }
    }
    free(data); free(buffer_data); free(sector_bitmap);
}

void ftl_write(u32 lba, u32 nsect, u32 *write_buffer)
{
    u32 lpn = lba/SECTORS_PER_PAGE, offset = lba%SECTORS_PER_PAGE;
    u32* data  = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    u32 tmpnsect = nsect;
    u32 wb_cursor = 0;

    while(1)
    {
        u32 bank = lpn%N_BANKS;
        for(int i=0; i<SECTORS_PER_PAGE; i++)
        {
            data[i] = 0xFFFFFFFF;
        }
        
        u32 nextPBN = -1; u32 nextPPN = -1;
        get_next_page(bank, &nextPBN, &nextPPN, 1);
        
        if(block_state[bank][nextPBN] == 0)
        {
            block_state[bank][nextPBN] = 1;
            n_data_block[bank]++;
        }
        if(n_data_block[bank] == N_PHY_DATA_BLK) 
        {
            block_state[bank][nextPBN] = 0;
            n_data_block[bank]--;
            garbage_collection(bank);
            nextPBN = next_page[bank][1].PBN; nextPPN = next_page[bank][1].PPN;
        }
        
        u32 map_page = lpn/(N_BANKS * SECTORS_PER_PAGE);
        u32 read_ppn = -1;
        int map_page_offset = (lpn / N_BANKS) % SECTORS_PER_PAGE; 
        u32 cash_slot = lpn2ppn(lpn);
        read_ppn = CMT[bank][cash_slot].data[map_page_offset];
        CMT[bank][cash_slot].data[map_page_offset] = nextPBN * PAGES_PER_BLK + nextPPN;
        CMT[bank][cash_slot].dirty = 1;
        CMT[bank][cash_slot].map_page = map_page;
        
        if(read_ppn != -1)
        {
            int prePBN = read_ppn / PAGES_PER_BLK; int prePPN = read_ppn % PAGES_PER_BLK;
            n_page_invalid[bank][prePBN]++;
            nand_read(bank, prePBN, prePPN, data, &spare);
        }
        

        if(offset + tmpnsect > SECTORS_PER_PAGE)
        {
            for(int i = 0; i < SECTORS_PER_PAGE; i++)
            {
                if(i >= offset && i < SECTORS_PER_PAGE) data[i] = write_buffer[wb_cursor++];
            }
            spare = lpn;
            nand_write(bank, nextPBN, nextPPN, data, &spare);
            stats.nand_write++;
            tmpnsect -= (SECTORS_PER_PAGE - offset);
            offset = 0;
            lpn++;
        }
        else
        {
            for(int i = 0; i < SECTORS_PER_PAGE; i++)
            {
                if(i >= offset && i < offset + tmpnsect) data[i] = write_buffer[wb_cursor++];
            }
            spare = lpn;
            nand_write(bank, nextPBN, nextPPN, data, &spare);
            stats.nand_write++;
            break;
        }

    }

/***************************************
Add

stats.nand_write++;

for every nand_write call (every valid page copy)
that you issue in this function
***************************************/
    free(data);
	stats.host_write += nsect;
	return;
}
