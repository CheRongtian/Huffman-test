#ifndef HUFF_H
#define HUFF_H

#include <stddef.h>

typedef enum 
{
    LEAF,
    NODE
} Kind;

typedef struct Tree Tree;

struct Tree 
{
    Kind kind;
    Tree *up;

    union 
    {
        struct 
        {
            char ch;
            int freq;
        } leaf;

        struct 
        {
            int freq;
            Tree *left;
            Tree *right;
        } node;
    };
};

Tree *mkleaf(char ch, int freq);
Tree *mknode(Tree *left, Tree *right);

void conn(Tree *parent, int isleft, Tree *child);
void show(Tree *tree);

Tree *buildtree(int freq[256]);

void buildcodes(Tree *root, char codes[256][256]);
int encode(const char *text, char codes[256][256], char *output, size_t output_size);
int decode(Tree *root, const char *bits, char *output, size_t output_size);

#endif