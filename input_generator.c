#include <stdio.h>

void make_input1(FILE* file)
{
    int N = 800; int M = 100;
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
        }        
        for(int i = 0; i < N/2; i++)
        {
            fprintf(file, "W %d 8 H\n", i*32+8);
        }
        for(int i = 0; i < N; i++)
        {
           fprintf(file, "W %d 8 H\n", i*16+8);
        }               
    }   
}

void make_input2(FILE* file)
{
    int N = 800; int M = 50; int ratio = 10;
    fprintf(file, "S 111\n");
    for(int i = 0; i < N; i++)
    {
        fprintf(file, "W %d 12 C\n", i*24);
        fprintf(file, "W %d 12 H\n", i*24+12);
        fprintf(file, "R %d 24\n", i*24);
    }
    
    for(int j = 0; j < M; j++)
    {
        for(int i = 0; i < N/2; i++)
        {
            fprintf(file, "W %d 12 H\n", i*48+12);
            fprintf(file, "W %d 4 H\n", i*48+36);
        }        
       
    
        if(j%ratio == ratio-1)   
        {
            for(int i = 0; i < N/2; i++)
            {
                fprintf(file, "W %d 30 C\n", i*30);
            }
        }
    
        for(int i = 0; i < N/2; i++)
        {
            fprintf(file, "W %d 12 H\n", i*48+12);
            fprintf(file, "W %d 4 H\n", i*48+36);
        }        
    }
    for(int i = 0; i < N; i++)
    {
        fprintf(file, "R %d 12\n", i*24);
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

