#include <stdio.h>
#include <string.h>
#include "huff.h"

void countfreq(const char *text, int freq[256])
{
    while (*text != '\0') 
    {
        freq[(unsigned char)*text]++;
        text++;
    }
}

void showfreq(int freq[256])
{
    for (int i = 0; i < 256; i++) 
    {
        if (freq[i] > 0) printf("'%c' = %d\n", i, freq[i]);
    }
}

void showcodes(int freq[256], char codes[256][256])
{
    for (int i = 0; i < 256; i++) 
    {
        if (freq[i] > 0) printf("'%c' = %s\n", i, codes[i]);
    }
}

int main(void)
{
    const char *text = "aaabbcdddd";
    int freq[256] = {0};

    countfreq(text, freq);
    printf("Frequency:\n");
    showfreq(freq);

    Tree *root = buildtree(freq);

    if (root == NULL) 
    {
        printf("failed to build tree\n");
        return 1;
    }

    printf("\nHuffman Tree:\n");
    show(root);

    char codes[256][256] = {{0}};

    buildcodes(root, codes);
    printf("\nHuffman Codes:\n");
    showcodes(freq, codes);

    char encoded[4096];

    if (!encode(text, codes, encoded, sizeof(encoded))) 
    {
        printf("encode failed\n");
        return 1;
    }

    printf("\nOriginal:\n");
    printf("%s\n", text);
    printf("\nEncoded:\n");
    printf("%s\n", encoded);

    char decoded[1024];

    if (!decode(root, encoded, decoded, sizeof(decoded))) 
    {
        printf("decode failed\n");
        return 1;
    }

    printf("\nDecoded:\n");
    printf("%s\n", decoded);
    printf("\nResult:\n");

    if (strcmp(text, decoded) == 0) printf("SUCCESS\n");
    else printf("FAILED\n");

    return 0;
}