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
    int state;
} cmt_data;
typedef struct _buff_data{
    int lpn; char state;
    u32 data[SECTORS_PER_PAGE];
} buff_data;

enum block_state_{
    FREE = 0,
    DATA_HOT = 1,
    DATA_COLD = 2,
    TRANS_HOT = 3,
    TRANS_COLD = 4
};

int n_page_invalid[N_BANKS][BLKS_PER_BANK];
p_data next_page[N_BANKS][5];

int n_data_block[N_BANKS];
int n_trans_block[N_BANKS];
int block_state[N_BANKS][BLKS_PER_BANK]; 

cmt_data CMT[N_BANKS][N_CACHED_MAP_PAGE_PB];
p_data GTD[N_BANKS][N_MAP_PAGES_PB];
int cache_count[N_BANKS][N_CACHED_MAP_PAGE_PB];

buff_data buffer[N_BUFFERS];
int buffer_sector_bitmap[N_BUFFERS][SECTORS_PER_PAGE];
int buffer_empty_page = N_BUFFERS;
// you must increase stats.cache_hit when L2P is in CMT

void trans_defragmenter(u32 bank, int workload_type, int free_blk)
{
    //printf("trans_defragmenter call : bank %d workload_type %d\n", bank, workload_type);
    int workload = (workload_type == 3)? 4:3;
    int erased_npage = 0; int free_page_write = 0;
    int next_free_blk = -1;
    u32* data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(i == free_blk) continue;
        if(block_state[bank][i] != workload) continue;
        if(n_page_invalid[bank][i] == 0) continue;

        for(int j = 0; j < PAGES_PER_BLK; j++)
        {
            nand_read(bank, i, j, data, &spare);
            if(GTD[bank][spare].PBN != i || GTD[bank][spare].PPN != j)
            {
                erased_npage++; continue;
            }
            else
            {
                if(free_page_write == PAGES_PER_BLK)
                {
                    block_state[bank][free_blk] = workload;
                    free_blk = next_free_blk; free_page_write = 0;
                    n_trans_block[bank]++;
                }
                //printf("trans_def: write location bank %d PBN %d PPN %d\n", bank, free_blk, free_page_write);
                nand_write(bank, free_blk, free_page_write, data, &spare);
                stats.map_gc_write++;
                GTD[bank][spare].PBN = free_blk; GTD[bank][spare].PPN = free_page_write++;
            }
        }

        nand_erase(bank, i);
        block_state[bank][i] = FREE;
        n_page_invalid[bank][i] = 0;
        n_trans_block[bank]--;
        next_free_blk = i;
    }
    free(data);
    block_state[bank][free_blk] = workload;
    n_trans_block[bank]++;

    next_page[bank][workload].PBN = free_blk; next_page[bank][workload].PPN = free_page_write-1;
    if(erased_npage < PAGES_PER_BLK) printf("Defragmenter Error : NAND is full!\n");
   
}

void get_next_page(u32 bank, u32* nextPBN, u32* nextPPN, int status) 
{
    if(next_page[bank][status].PBN == -1 || next_page[bank][status].PPN + 1 >= PAGES_PER_BLK)
    {
        int flag = 0;
        for(int i = 0; i < BLKS_PER_BANK; i++)
        {
            if(block_state[bank][i] == FREE)
            {
                next_page[bank][status].PBN = i;
                next_page[bank][status].PPN = 0;
                if(status == 1 || status == 2) n_data_block[bank]++;
                else n_trans_block[bank]++;
                block_state[bank][i] = status;
                flag = 1;
                break;
            }
        }
        if(!flag) printf("Error: NO FREE BLK!\n");
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

static void map_garbage_collection(u32 bank, int workload_type)
{
    //printf("map_gc call\n");
    stats.map_gc_cnt++;
    int max_invalid = 0;
    int victim_blk = -1;
    int free_blk = -1;
    int free_page_write = 0;

    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] == FREE) 
        {
            free_blk = i;
            break;
        }
    }
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] != workload_type) continue;
        if(n_page_invalid[bank][i] > max_invalid)
        {
            max_invalid = n_page_invalid[bank][i];
            victim_blk = i;
        }
    }
    if(victim_blk == -1) 
    {
        trans_defragmenter(bank, workload_type, free_blk);
        next_page[bank][workload_type].PBN = -1; next_page[bank][workload_type].PPN = -1; 
        return;
    }
    
    u32* data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    for(int i = 0; i < PAGES_PER_BLK; i++)
    {
        nand_read(bank, victim_blk, i, data, &spare);
        if(GTD[bank][spare].PBN != victim_blk || GTD[bank][spare].PPN != i) continue;
        else
        {
            //printf("map_gc: write location bank %d PBN %d PPN %d\n", bank, free_blk, free_page_write);
            nand_write(bank, free_blk, free_page_write, data, &spare);
            stats.map_gc_write++;
            GTD[bank][spare].PBN = free_blk; GTD[bank][spare].PPN = free_page_write++;
        }
    }
    free(data);
    block_state[bank][free_blk] = workload_type;
    block_state[bank][victim_blk] = FREE;
    n_page_invalid[bank][victim_blk] = 0;
    nand_erase(bank, victim_blk);

    next_page[bank][workload_type].PBN = free_blk; next_page[bank][workload_type].PPN = free_page_write-1;
    //printf("map_gc finish\n");
}
static void map_write(u32 bank, u32 map_page, u32 cache_slot)
{
    if(CMT[bank][cache_slot].dirty == 0) 
    {
        printf("ERROR : map_write call with clean cache_slot %d\n", cache_slot);
        return;
    }
    map_page = CMT[bank][cache_slot].map_page;
    int workload_type = CMT[bank][cache_slot].state+2;
    
    u32 nextPBN = -1; u32 nextPPN = -1;
    get_next_page(bank, &nextPBN, &nextPPN, workload_type);
    if(n_trans_block[bank] == N_PHY_MAP_BLK)
    {
        block_state[bank][nextPBN] = FREE;
        n_trans_block[bank]--;
        map_garbage_collection(bank, workload_type);
        get_next_page(bank, &nextPBN, &nextPPN, workload_type);
    }

    if(GTD[bank][map_page].PBN != -1)
    {
        int prePBN = GTD[bank][map_page].PBN; 
        n_page_invalid[bank][prePBN]++;
    }
    GTD[bank][map_page].PBN = nextPBN; GTD[bank][map_page].PPN = nextPPN;


    //printf("map_write: write info bank %d slot %d\n", bank, cache_slot);
    //printf("map_write: write location bank %d PBN %d PPN %d\n", bank, nextPBN, nextPPN);
    /*printf("map_write: write data: ");
    for(int i = 0; i < SECTORS_PER_PAGE; i++)
    {
        printf("%d ", CMT[bank][cache_slot].data[i]);
    }
    printf("\n");*/
    int x = nand_write(bank, nextPBN, nextPPN, CMT[bank][cache_slot].data, &map_page);
    stats.map_write++;
    //printf("map_write: write bank %d PBN %d PPN %d map_page %d errorcode %d\n", bank, nextPBN, nextPPN, map_page, x);
    CMT[bank][cache_slot].dirty = 0;
    CMT[bank][cache_slot].map_page = -1;
    CMT[bank][cache_slot].state = 0;
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
        CMT[bank][cache_slot].state = 0;
        for(int i = 0; i < SECTORS_PER_PAGE; i++)
            read_data[i] = 0xFFFFFFFF;
    }
    else 
    {
        nand_read(bank, GTD[bank][map_page].PBN, GTD[bank][map_page].PPN, read_data, &spare);
        CMT[bank][cache_slot].state = block_state[bank][GTD[bank][map_page].PBN];
    }
    CMT[bank][cache_slot].map_page = map_page;
    CMT[bank][cache_slot].dirty = 0;
   
    //printf("map_read: read location: bank %d PBN %d PPN %d\n", bank, GTD[bank][map_page].PBN, GTD[bank][map_page].PPN);
    //printf("map_read: read_data: ");
    for(int i = 0; i< SECTORS_PER_PAGE; i++)
    {
        CMT[bank][cache_slot].data[i] = read_data[i];
  //      printf("%d ", read_data[i]);
    }
//    printf("\n");
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

void data_defragmenter(u32 bank, int workload_type, int free_blk)
{
    //printf("data_deragementer call\n");
    int workload = (workload_type == 1)? 2:1;
    int erased_npage = 0; int free_page_write = 0; 
    int next_free_blk = -1;
    u32* data = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    
    block_state[bank][free_blk] = workload;
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(i == free_blk) continue;
        if(block_state[bank][i] != workload) continue;
        if(n_page_invalid[bank][i] == 0) continue;

        for(int j = 0; j < PAGES_PER_BLK; j++)
        {
            nand_read(bank, i, j, data, &spare); 
            int offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
            u32 cash_slot = lpn2ppn(spare);
            if(CMT[bank][cash_slot].data[offset] != i * PAGES_PER_BLK + j)
            {
                erased_npage++; continue;
            }
            else
            {
                if(free_page_write == PAGES_PER_BLK)
                {
                    block_state[bank][next_free_blk] = workload;
                    free_blk = next_free_blk; free_page_write = 0;
                    n_data_block[bank]++;
                }
                //printf("data_def: write location bank %d PBN %d PPN %d\n", bank, free_blk, free_page_write);
                nand_write(bank, free_blk, free_page_write, data, &spare);
                stats.gc_write++;

                offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
                CMT[bank][cash_slot].data[offset] = free_blk * PAGES_PER_BLK + free_page_write++;
                CMT[bank][cash_slot].map_page = spare / (N_BANKS * SECTORS_PER_PAGE);
                CMT[bank][cash_slot].dirty = 1;
                CMT[bank][cash_slot].state = workload_type;
                //printf("data_def: CMT change bank %d slot %d offset %d data %d\n", bank, cash_slot, offset, CMT[bank][cash_slot].data[offset]);
            }
        }

        nand_erase(bank, i);
        block_state[bank][i] = FREE;
        n_page_invalid[bank][i] = 0;
        n_data_block[bank]--;
        next_free_blk = i;
    }
    //printf("data_def: bank %d PBN %d state %d\n", bank, free_blk, workload);
    n_data_block[bank]++;
    free(data);
    next_page[bank][workload].PBN = free_blk; next_page[bank][workload].PPN = free_page_write-1;
    if(erased_npage < PAGES_PER_BLK) printf("Defragmenter Error : NAND is full!\n");
}

static void garbage_collection(u32 bank, int workload_type)
{
    //printf("GC call\n");
    stats.gc_cnt++;
    int max_invalid = 0;
    int victim_blk = -1;
    int free_blk = -1;
    int free_page_write = 0;
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] == FREE) 
        {
            free_blk = i;
            break;
        }
    }
    
    for(int i = 0; i < BLKS_PER_BANK; i++)
    {
        if(block_state[bank][i] != workload_type) continue;
        if(n_page_invalid[bank][i] > max_invalid)
        {
            max_invalid = n_page_invalid[bank][i];
            victim_blk = i;
        }
    }
    if(victim_blk == -1) 
    {
        data_defragmenter(bank, workload_type, free_blk);
        next_page[bank][workload_type].PBN = -1; next_page[bank][workload_type].PPN = -1;
        return;
    }

    block_state[bank][free_blk] = workload_type;
    n_data_block[bank]++;

    u32* data = malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    for(int i = 0; i < PAGES_PER_BLK; i++)
    {
        nand_read(bank, victim_blk, i, data, &spare); 
        int offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
        u32 cash_slot = lpn2ppn(spare);
        if(CMT[bank][cash_slot].data[offset] != victim_blk * PAGES_PER_BLK + i) continue;
        else
        {
            //printf("gc: write location bank %d PBN %d PPN %d\n", bank, free_blk, free_page_write);
            nand_write(bank, free_blk, free_page_write, data, &spare);
            stats.gc_write++;

            offset = (spare / N_BANKS) % SECTORS_PER_PAGE; 
            CMT[bank][cash_slot].data[offset] = free_blk * PAGES_PER_BLK + free_page_write++;
            CMT[bank][cash_slot].map_page = spare / (N_BANKS * SECTORS_PER_PAGE);
            CMT[bank][cash_slot].dirty = 1;
            CMT[bank][cash_slot].state = workload_type;
            //printf("gc: CMT change bank %d slot %d offset %d data %d\n", bank, cash_slot, offset, CMT[bank][cash_slot].data[offset]);
        }
    }
    free(data); 
    block_state[bank][victim_blk] = FREE;
    n_page_invalid[bank][victim_blk] = 0;
    nand_erase(bank, victim_blk);
    n_data_block[bank]--;
   
    next_page[bank][workload_type].PBN = free_blk; next_page[bank][workload_type].PPN = free_page_write-1;
    
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
        next_page[i][3].PBN = -1; next_page[i][3].PPN = -1;
        next_page[i][4].PBN = -1; next_page[i][4].PPN = -1;

        for(int j = 0; j<N_CACHED_MAP_PAGE_PB; j++)
        {
            CMT[i][j].state = 0;
            CMT[i][j].dirty = 0;
            CMT[i][j].map_page = -1;
            for(int k = 0; k < N_MAP_ENTRIES_PER_PAGE; k++)    
            {
                CMT[i][j].data[k] = 0xFFFFFFFF;
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
        buffer[i].state = 0;
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

void ftl_write_direct(u32 lba, u32 nsect, char workload_type, u32 *write_buffer)
{
    u32 lpn = lba/SECTORS_PER_PAGE, offset = lba%SECTORS_PER_PAGE;
    u32* data  = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
    u32 tmpnsect = nsect;
    u32 wb_cursor = 0;
    int input_state = (workload_type == 'H')? DATA_HOT : DATA_COLD;

    while(1)
    {
        
        u32 bank = lpn%N_BANKS;
        for(int i=0; i<SECTORS_PER_PAGE; i++)
        {
            data[i] = 0xFFFFFFFF;
        }
        
        u32 nextPBN = -1; u32 nextPPN = -1;
        get_next_page(bank, &nextPBN, &nextPPN, input_state);
        if(n_data_block[bank] == N_PHY_DATA_BLK) 
        {
            block_state[bank][nextPBN] = FREE;
            n_data_block[bank]--;
            garbage_collection(bank, input_state);
            get_next_page(bank, &nextPBN, &nextPPN, input_state);
        }
        u32 map_page = lpn/(N_BANKS * SECTORS_PER_PAGE);
        u32 read_ppn = -1;
        int map_page_offset = (lpn / N_BANKS) % SECTORS_PER_PAGE; 
        u32 cash_slot = lpn2ppn(lpn);
        read_ppn = CMT[bank][cash_slot].data[map_page_offset];
        
        CMT[bank][cash_slot].data[map_page_offset] = nextPBN * PAGES_PER_BLK + nextPPN;
        CMT[bank][cash_slot].dirty = 1;
        CMT[bank][cash_slot].map_page = map_page;
        CMT[bank][cash_slot].state = input_state;
        //printf("ftl_write: CMT change bank %d slot %d offset %d data %d\n", bank, cash_slot, map_page_offset, CMT[bank][cash_slot].data[map_page_offset]);
       
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
            //printf("ftl_write: write location bank %d PBN %d PPN %d\n", bank, nextPBN, nextPPN);
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
            //printf("ftl_write: write location bank %d PBN %d PPN %d\n", bank, nextPBN, nextPPN);
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


void ftl_write(u32 lba, u32 nsect, char workload_type, u32 *write_buffer)
{
    u32 lpn = lba / SECTORS_PER_PAGE; u32 offset = lba % SECTORS_PER_PAGE;
    u32 tmpnsect = nsect; u32 wb_cursor = 0;
    while(1) {
        if(tmpnsect == 0) break;
        int buffer_hit = 0;
        for(int i = 0; i < N_BUFFERS; i++) {
            if(buffer[i].lpn != lpn) continue;
            buffer_hit = 1;
            buffer[i].state = workload_type;
            if(tmpnsect + offset > SECTORS_PER_PAGE) {
                for(int j = offset; j < SECTORS_PER_PAGE; j++) {
                    buffer[i].data[j] = write_buffer[wb_cursor++];
                    buffer_sector_bitmap[i][j] = 1;
                }
                lpn++;
                tmpnsect -= SECTORS_PER_PAGE - offset;
                offset = 0;
                break;
            }
            else {
                for(int j = offset; j < offset+tmpnsect; j++) {
                    buffer[i].data[j] = write_buffer[wb_cursor++];
                    buffer_sector_bitmap[i][j] = 1;
                }
                tmpnsect = 0;
                break;
            }
        }
        if(buffer_hit) continue;
        if(buffer_empty_page == 0) ftl_flush();
        for(int i = 0; i < N_BUFFERS; i++) {
            if(buffer[i].lpn != -1) continue;
            buffer_empty_page--;
            buffer[i].lpn = lpn;
            buffer[i].state = workload_type;
            if(tmpnsect + offset > SECTORS_PER_PAGE) {
                for(int j = offset; j < SECTORS_PER_PAGE; j++) {
                    buffer[i].data[j] = write_buffer[wb_cursor++];
                    buffer_sector_bitmap[i][j] = 1;
                }
                lpn++;
                tmpnsect -= SECTORS_PER_PAGE - offset;
                offset = 0;
            }
            else {
                for(int j = offset; j < offset+tmpnsect; j++) {
                    buffer[i].data[j] = write_buffer[wb_cursor++];
                    buffer_sector_bitmap[i][j] = 1;
                }
                tmpnsect = 0;
            }
            break;
        }

    }

}

void ftl_flush()
{
    for(int i = 0; i < N_BUFFERS; i++)
    {
        int buffer_full = 1;
        for(int j = 0; j < SECTORS_PER_PAGE; j++)
        {
            if(buffer_sector_bitmap[i][j] == 0) {
                buffer_full = 0;
                break;
            }
        }
        if(buffer_full) ftl_write_direct(buffer[i].lpn * SECTORS_PER_PAGE, SECTORS_PER_PAGE, buffer[i].state, buffer[i].data);
        else 
        {
            u32* read_buffer = (u32*)malloc(sizeof(u32)*SECTORS_PER_PAGE); u32 spare;
            u32 bank = buffer[i].lpn % N_BANKS;
            u32 map_page_offset = (buffer[i].lpn / N_BANKS) % SECTORS_PER_PAGE; 
            u32 cash_slot = lpn2ppn(buffer[i].lpn);
            u32 read_ppn = CMT[bank][cash_slot].data[map_page_offset];
            if(read_ppn == -1) {
                for(int j = 0; j < SECTORS_PER_PAGE; j++) {
                    read_buffer[j] = 0xFFFFFFFF;
                }
            }
            else nand_read(bank, read_ppn / PAGES_PER_BLK, read_ppn % PAGES_PER_BLK, read_buffer, &spare);

            for(int j = 0; j < SECTORS_PER_PAGE; j++) {
                if(buffer_sector_bitmap[i][j] == 1) read_buffer[j] = buffer[i].data[j];
            }
            ftl_write_direct(buffer[i].lpn * SECTORS_PER_PAGE, SECTORS_PER_PAGE, buffer[i].state, read_buffer);
        }

        buffer[i].lpn = -1;
        for(int j = 0; j < SECTORS_PER_PAGE; j++) {
            buffer_sector_bitmap[i][j] = 0;
        }
    }
    buffer_empty_page = N_BUFFERS;

}
