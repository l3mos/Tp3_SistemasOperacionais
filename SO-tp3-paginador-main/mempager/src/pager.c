#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mmu.h"
#include "pager.h"

#define INVALID_ID -1

typedef struct {
    int valid;
    int frame;
    int block;
    int modified;
    intptr_t address;
} VirtualPage;

typedef struct {
    pid_t process_id;
    VirtualPage *pages;
    int count;
    int capacity;
} ProcessMemory;

typedef struct {
    pid_t process_id;
    int access_bit;
    VirtualPage *page;
} MemoryFrame;

typedef struct {
    int size;
    int page_size;
    int clock_hand;
    MemoryFrame *frames;
} PhysicalMemory;

typedef struct {
    int used;
    VirtualPage *page;
} DiskBlock;

typedef struct {
    int count;
    DiskBlock *blocks;
} DiskStorage;

PhysicalMemory physical_memory;
DiskStorage disk_storage;
ProcessMemory *process_list;
int process_count = 0;
int process_capacity = 10;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

void release_page(VirtualPage *page) {
    if (page->valid) {
        physical_memory.frames[page->frame].process_id = INVALID_ID;
        physical_memory.frames[page->frame].page = NULL;
        physical_memory.frames[page->frame].access_bit = 0;
        page->valid = 0;
    }
}

void release_process_memory(ProcessMemory *process) {
    for (int i = 0; i < process->count; i++) {
        VirtualPage *page = &process->pages[i];
        release_page(page);
        disk_storage.blocks[page->block].page = NULL;
        disk_storage.blocks[page->block].used = 0;
    }
    free(process->pages);
}

int find_available_frame() {
    for (int i = 0; i < physical_memory.size; i++)
        if (physical_memory.frames[i].process_id == INVALID_ID)
            return i;
    return INVALID_ID;
}

int find_available_block() {
    for (int i = 0; i < disk_storage.count; i++)
        if (disk_storage.blocks[i].page == NULL)
            return i;
    return INVALID_ID;
}

ProcessMemory *find_process(pid_t pid) {
    for (int i = 0; i < process_count; i++)
        if (process_list[i].process_id == pid)
            return &process_list[i];
    exit(INVALID_ID);
}

VirtualPage *find_page(ProcessMemory *process, intptr_t addr) {
    for (int i = 0; i < process->count; i++) {
        VirtualPage *page = &process->pages[i];
        if (addr >= page->address && addr < (page->address + physical_memory.page_size))
            return page;
    }
    return NULL;
}

int clock_algorithm() {
    MemoryFrame *frames = physical_memory.frames;
    int victim = INVALID_ID;
    int start = physical_memory.clock_hand;

    do {
        int current = physical_memory.clock_hand;
        if (frames[current].access_bit == 0) {
            victim = current;
            break;
        } else {
            frames[current].access_bit = 0;
            mmu_chprot(frames[current].process_id, (void *)frames[current].page->address, PROT_NONE);
        }
        physical_memory.clock_hand = (current + 1) % physical_memory.size;
    } while (physical_memory.clock_hand != start);

    if (victim == INVALID_ID) {
        victim = start;
    }

    physical_memory.clock_hand = (victim + 1) % physical_memory.size;
    return victim;
}

void evict_page(int frame) {
    MemoryFrame *victim_frame = &physical_memory.frames[frame];
    VirtualPage *evicted_page = victim_frame->page;
    
    mmu_nonresident(victim_frame->process_id, (void *)evicted_page->address);
    
    if (evicted_page->modified) {
        disk_storage.blocks[evicted_page->block].used = 1;
        mmu_disk_write(frame, evicted_page->block);
    }
    
    evicted_page->valid = 0;
    victim_frame->process_id = INVALID_ID;
    victim_frame->page = NULL;
    victim_frame->access_bit = 0;
}

void pager_init(int nframes, int nblocks) {
    pthread_mutex_lock(&global_lock);
    
    physical_memory.size = nframes;
    physical_memory.page_size = sysconf(_SC_PAGESIZE);
    physical_memory.clock_hand = 0;
    physical_memory.frames = calloc(nframes, sizeof(MemoryFrame));
    
    for (int i = 0; i < nframes; i++) {
        physical_memory.frames[i].process_id = INVALID_ID;
        physical_memory.frames[i].page = NULL;
        physical_memory.frames[i].access_bit = 0;
    }

    disk_storage.count = nblocks;
    disk_storage.blocks = calloc(nblocks, sizeof(DiskBlock));

    process_list = malloc(process_capacity * sizeof(ProcessMemory));
    
    pthread_mutex_unlock(&global_lock);
}

void pager_create(pid_t pid) {
    pthread_mutex_lock(&global_lock);
    
    if (process_count == process_capacity) {
        process_capacity *= 2;
        process_list = realloc(process_list, process_capacity * sizeof(ProcessMemory));
    }

    ProcessMemory *new_process = &process_list[process_count++];
    new_process->process_id = pid;
    new_process->pages = NULL;
    new_process->count = 0;
    new_process->capacity = 0;

    pthread_mutex_unlock(&global_lock);
}

void *pager_extend(pid_t pid) {
    pthread_mutex_lock(&global_lock);
    
    int block = find_available_block();
    if (block == INVALID_ID) {
        pthread_mutex_unlock(&global_lock);
        return NULL;
    }

    ProcessMemory *process = find_process(pid);

    if (process->count == process->capacity) {
        process->capacity = process->capacity == 0 ? 1 : process->capacity * 2;
        process->pages = realloc(process->pages, process->capacity * sizeof(VirtualPage));
    }

    VirtualPage *new_page = &process->pages[process->count++];
    new_page->valid = 0;
    new_page->address = UVM_BASEADDR + (process->count - 1) * physical_memory.page_size;
    new_page->block = block;
    new_page->modified = 0;

    disk_storage.blocks[block].page = new_page;
    disk_storage.blocks[block].used = 0;

    pthread_mutex_unlock(&global_lock);
    return (void *)new_page->address;
}

void handle_page_fault(VirtualPage *page, pid_t pid, void *addr) {
    if (page->valid) {
        mmu_chprot(pid, addr, PROT_READ | PROT_WRITE);
        physical_memory.frames[page->frame].access_bit = 1;
        page->modified = 1;
    } else {
        int frame = find_available_frame();

        if (frame == INVALID_ID) {
            frame = clock_algorithm();
            evict_page(frame);
        }

        MemoryFrame *new_frame = &physical_memory.frames[frame];
        new_frame->process_id = pid;
        new_frame->page = page;
        new_frame->access_bit = 1;

        page->valid = 1;
        page->frame = frame;
        page->modified = 0;

        if (disk_storage.blocks[page->block].used)
            mmu_disk_read(page->block, frame);
        else
            mmu_zero_fill(frame);

        mmu_resident(pid, addr, frame, PROT_READ);
    }
}

void pager_fault(pid_t pid, void *addr) {
    pthread_mutex_lock(&global_lock);
    
    ProcessMemory *process = find_process(pid);
    addr = (void *)((intptr_t)addr - (intptr_t)addr % physical_memory.page_size);
    VirtualPage *page = find_page(process, (intptr_t)addr);

    handle_page_fault(page, pid, addr);

    pthread_mutex_unlock(&global_lock);
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
    pthread_mutex_lock(&global_lock);
    
    ProcessMemory *process = find_process(pid);
    unsigned char *buffer = malloc(len);

    for (size_t i = 0; i < len; i++) {
        VirtualPage *page = find_page(process, (intptr_t)addr + i);
        if (!page || !page->valid) {
            free(buffer);
            pthread_mutex_unlock(&global_lock);
            return -1;
        }
        buffer[i] = pmem[page->frame * physical_memory.page_size + ((intptr_t)addr + i) % physical_memory.page_size];
    }

    for (size_t i = 0; i < len; i++)
        printf("%02x", buffer[i]);
    if (len > 0)
        printf("\n");

    free(buffer);
    pthread_mutex_unlock(&global_lock);
    return 0;
}

void pager_destroy(pid_t pid) {
    pthread_mutex_lock(&global_lock);
    ProcessMemory *process = find_process(pid);
    release_process_memory(process);
    pthread_mutex_unlock(&global_lock);
}