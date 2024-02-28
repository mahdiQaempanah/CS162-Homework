/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines. Remove this comment and provide
 * a summary of your allocator's design here.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <stdio.h>

/* Your final implementation should comment out this macro. */
// #define MM_USE_STUBS

s_block_ptr first = NULL;

s_block_ptr get_block(void *p){
    s_block_ptr po = first; 
    for(; po; po = po->next){
        if(po->ptr == p){
            return po; 
        }
    }
    return NULL;
}

size_t min(size_t i, size_t j){
    if(i <= j)
        return i; 
    else 
        return j; 
}

void split_block(s_block_ptr b, size_t s){
    if(s <= 0 || b == NULL || b->size < sizeof(s_block) + s)
        return; 

    s_block_ptr new_block = (s_block_ptr)(b->ptr + s);
    new_block->size = b->size -sizeof(s_block) -s;
    b->size = s; 
    new_block->ptr = b->ptr +sizeof(s_block) +s;   
    new_block->next = b->next;
    new_block->prev = b; 
    
    if(b->next != NULL)
        b->next->prev = new_block;
    b->next = new_block; 
}

s_block_ptr fusion(s_block_ptr b){
    if(!b->free)
        return NULL;
    
    if(b->next != NULL && b->next->free){
        s_block_ptr  elimination_block = b->next;
        b->size += sizeof(s_block) + elimination_block->size; 
        b->next = elimination_block->next; 
        if(elimination_block->next != NULL)
            elimination_block->next->prev = b; 
    }
    if(b->prev != NULL && b->prev->free){
        s_block_ptr root_block = b->prev;
        root_block->size += sizeof(s_block) + b->size; 
        root_block->next = b->next; 
        if(b->next != NULL)
            b->next->prev = root_block;
        return root_block; 
    }
    return b; 
}

s_block_ptr extend_heap(s_block_ptr last , size_t s){
    void *new_part = sbrk(sizeof(s_block) +s);
    if (new_part == (void *) -1)
        return NULL;
    s_block_ptr new_block = (s_block_ptr) new_part;
    new_block->ptr = sizeof(s_block) + new_part;
    new_block->size = s;
    new_block->prev = last;
    new_block->next = NULL;
    new_block->free = 0;

    if(last != NULL)
        last->next = new_block;
    else
        first = new_block;

    memset(new_block->ptr, 0, new_block->size);
    return new_block->ptr;
}

void* mm_malloc(size_t size)
{
#ifdef MM_USE_STUBS
//
#else
    if(size >= 10000000){
        printf("the requested size is too large.\n");
        return NULL;
    }

    if(size == 0)
        return NULL; 
    
    if(first == NULL){
        return extend_heap(first, size);
    }

    s_block_ptr po = first; 
    for(; po != NULL; po = po->next){
        if(po->free == 1 && po->size >= size){
            split_block(po, size);
            memset(po->ptr, 0, po->size);
            po->free = 0; 
            return po->ptr; 
        }
        if(po->next == NULL){
            return extend_heap(po, size); 
        }

    }
    return NULL;
#endif 
}

void* mm_realloc(void* ptr, size_t size)
{
#ifdef MM_USE_STUBS
    return realloc(ptr, size);
#else
    if(size >= 10000000){
        printf("the requested size is too large.\n");
        return NULL;
    }

    if(ptr!=NULL && size==0){
        mm_free(ptr);
        return NULL; 
    }
    else if(ptr==NULL && size>0){
        return mm_malloc(size); 
    }
    else if(ptr==NULL && size==0){
        mm_malloc(size);
        return NULL; 
    }

    
    s_block_ptr block = get_block(ptr);
    if(block == NULL){
        return NULL; 
    }
    void *new_allocation = mm_malloc(size);
    int* data = (int *) ptr; 
    memcpy(new_allocation, ptr, min(block->size, size));
    block->free = 1;
    fusion(block);
    return new_allocation;
#endif
}

void mm_free(void* ptr)
{
#ifdef MM_USE_STUBS
    free(ptr);
#else
    if (ptr == NULL)
        return;
    
    s_block_ptr allocated_block = get_block(ptr);
    
    if(allocated_block){
        allocated_block->free = 1;
        fusion(allocated_block);
    }
#endif
}
