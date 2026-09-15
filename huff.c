#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "huff.h"

static void zero(Tree *t)
{
    memset(t, 0, sizeof(Tree));
}

static int getfreq(Tree *t)
{
    if (t->kind == LEAF) return t->leaf.freq;
    return t->node.freq;
}

Tree *mkleaf(char ch, int freq)
{
    Tree *t = malloc(sizeof(Tree));

    if (t == NULL) return NULL;

    zero(t);

    t->kind = LEAF;
    t->leaf.ch = ch;
    t->leaf.freq = freq;

    return t;
}

void conn(Tree *parent, int isleft, Tree *child)
{
    if (isleft) parent->node.left = child;
    else parent->node.right = child;

    child->up = parent;
}

Tree *mknode(Tree *left, Tree *right)
{
    Tree *t = malloc(sizeof(Tree));

    if (t == NULL) return NULL;

    zero(t);

    t->kind = NODE;
    t->node.freq = getfreq(left) + getfreq(right);

    conn(t, 1, left);
    conn(t, 0, right);

    return t;
}

Tree *buildtree(int freq[256])
{
    Tree *trees[256];
    int count = 0;

    for (int i = 0; i < 256; i++) 
    {
        if (freq[i] > 0) 
        {
            Tree *leaf = mkleaf((char)i, freq[i]);

            if (leaf == NULL) return NULL;
            trees[count++] = leaf;
        }
    }

    if (count == 0) return NULL;

    while (count > 1) 
    {
        int min1 = 0;
        int min2 = 1;

        if (getfreq(trees[min2]) < getfreq(trees[min1])) 
        {
            int tmp = min1;
            min1 = min2;
            min2 = tmp;
        }

        for (int i = 2; i < count; i++) 
        {
            if (getfreq(trees[i]) < getfreq(trees[min1])) 
            {
                min2 = min1;
                min1 = i;
            }
            else if (getfreq(trees[i]) < getfreq(trees[min2])) min2 = i;
        }

        Tree *left = trees[min1];
        Tree *right = trees[min2];
        Tree *parent = mknode(left, right);

        if (parent == NULL) return NULL;

        if (min1 > min2) 
        {
            int tmp = min1;
            min1 = min2;
            min2 = tmp;
        }

        trees[min1] = parent;
        trees[min2] = trees[count - 1];

        count--;
    }

    return trees[0];
}

static void buildcodes_(Tree *tree, char path[256], int depth, char codes[256][256])
{
    if (tree == NULL) return;

    if (tree->kind == LEAF) 
    {
        if (depth == 0) 
        {
            codes[(unsigned char)tree->leaf.ch][0] = '0';
            codes[(unsigned char)tree->leaf.ch][1] = '\0';
            return;
        }
        path[depth] = '\0';
        strcpy(codes[(unsigned char)tree->leaf.ch], path);
        
        return;
    }

    path[depth] = '0';

    buildcodes_(tree->node.left, path, depth + 1, codes);

    path[depth] = '1';

    buildcodes_(tree->node.right, path, depth + 1, codes);
}

void buildcodes(Tree *root, char codes[256][256])
{
    char path[256] = {0};
    buildcodes_(root, path, 0, codes);
}

int encode(const char *text, char codes[256][256], char *output, size_t output_size)
{
    size_t used = 0;

    while (*text != '\0') 
    {
        unsigned char ch = (unsigned char)*text;
        const char *code = codes[ch];
        size_t code_len = strlen(code);

        if (used + code_len + 1 > output_size) return 0;
        memcpy(output + used, code, code_len);

        used += code_len;
        text++;
    }

    output[used] = '\0';
    return 1;
}

int decode(Tree *root, const char *bits, char *output, size_t output_size)
{
    if (root == NULL || output_size == 0) return 0;
    size_t used = 0;

    if (root->kind == LEAF) 
    {
        while (*bits != '\0') 
        {
            if (*bits != '0') return 0;
            if (used + 1 >= output_size) return 0;

            output[used++] = root->leaf.ch;
            bits++;
        }
        output[used] = '\0';
        
        return 1;
    }

    Tree *current = root;

    while (*bits != '\0') 
    {
        if (*bits == '0') current = current->node.left;
        else if (*bits == '1') current = current->node.right;
        else return 0;

        if (current == NULL) return 0;

        if (current->kind == LEAF) 
        {
            if (used + 1 >= output_size) return 0;
            output[used++] = current->leaf.ch;
            current = root;
        }
        bits++;
    }

    if (current != root) return 0;
    output[used] = '\0';

    return 1;
}

static void show_(Tree *tree, int depth)
{
    if (tree == NULL) return;

    for (int i = 0; i < depth; i++) printf("    ");

    if (tree->kind == LEAF) 
    {
        printf("Leaf '%c' freq=%d self=%p parent=%p\n", tree->leaf.ch, tree->leaf.freq, (void *)tree, (void *)tree->up);

        return;
    }

    printf("Node freq=%d self=%p parent=%p\n", tree->node.freq, (void *)tree, (void *)tree->up);

    show_(tree->node.left, depth + 1);
    show_(tree->node.right, depth + 1);
}

void show(Tree *tree)
{
    show_(tree, 0);
}