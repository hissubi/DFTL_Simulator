#include <stdio.h>

void make_input1(FILE* file)
{
    int N = 600; int M = 10;
    fprintf(file, "S 321\n");
    for(int i = 0; i < N; i++)
    {
        fprintf(file, "W %d 8 C\n", i*16);
        fprintf(file, "W %d 8 H\n", i*16+8);
    }
    
    for(int j = 0; j < M; j++)
    {
        for(int i = 0; i < N; i++)
        {
            fprintf(file, "W %d 8 H\n", i*16+8);
            fprintf(file, "W %d 24 C\n", 16*N + j*24*N + i*24);
        }
    }   
}

void make_input2(FILE* file)
{
    int N = 300; int M = 50; int ratio = 5;
    fprintf(file, "S 111\n");
    for(int i = 0; i < N; i++)
    {
        fprintf(file, "W %d 16 C\n", i*32);
        fprintf(file, "W %d 16 H\n", i*32+16);
        fprintf(file, "R %d 32\n", i*32);
    }
    
    for(int j = 0; j < M; j++)
    {
        for(int i = 0; i < N/2; i++)
        {
            fprintf(file, "W %d 16 H\n", N*32 + i*32);
            fprintf(file, "W %d 16 H\n", N*32 + i*32+16);
        }        
       
    
        if(j%ratio == ratio-1)   
        {
            for(int i = 0; i < N; i++)
            {
                fprintf(file, "W %d 24 C\n", i*32);
            }
        }
    
        for(int i = 0; i < N; i++)
        {
            fprintf(file, "W %d 16 H\n", i*32+16);
        }        
    }
    for(int i = 0; i < N; i++)
    {
        fprintf(file, "R %d 32\n", i*32);
    }
}

int main()
{
    FILE* file1 = fopen("myinput1.txt", "w");
    FILE* file2 = fopen("myinput2.txt", "w");
    FILE* file3 = fopen("myinput3.txt", "w");
    
    make_input1(file1);
    make_input2(file2);
    //make_input3(file3);
    
    fclose(file1); fclose(file2); fclose(file3);
}

