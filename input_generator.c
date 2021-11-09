#include <stdio.h>
int main()
{
    int N = 1000; 
    printf("S 321\n");
    for(int i = 0; i < N; i++)
    {
        printf("W %d 8 C\n", i*16);
        printf("W %d 8 H\n", i*16+8);
    }
    
    for(int i = 0; i < N; i++)
    {
        printf("W %d 8 H\n", i*16+8);
    }        
    for(int i = 0; i < N/2; i++)
    {
        printf("W %d 8 H\n", i*32+8);
    }
    for(int i = 0; i < N; i++)
    {
        printf("W %d 8 H\n", i*16+8);
    } 
}

