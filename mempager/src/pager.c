#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/mman.h> 

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "log.h"
#include "pager.h"
#include "mmu.h"
#include "mmuproto.h"
#define UNUSED_ID -1

typedef struct {
    int is_valid;
    int frame_number;
    int disk_block;
    int is_modified;
    intptr_t virtual_address;
} PageEntry;

typedef struct {
    pid_t pid;
    PageEntry *page_table;
    int page_count;
    int table_capacity;
} ProcessMemoryTable;

typedef struct {
    pid_t pid;
    int access_flag;
    PageEntry *page_entry;
} FrameEntry;

typedef struct {
    int total_frames;
    int size_of_page;
    int clock_pointer;
    FrameEntry *frame_table;
} PhysicalMemoryTable;

typedef struct {
    int is_used;
    PageEntry *associated_page;
} DiskEntry;

typedef struct {
    int total_blocks;
    DiskEntry *block_table;
} DiskMemory;

PhysicalMemoryTable phys_mem;
DiskMemory disk_mem;
ProcessMemoryTable *proc_mem_table;
int num_processes = 0;
int max_processes = 10;
pthread_mutex_t memory_mutex = PTHREAD_MUTEX_INITIALIZER;

void clear_page(PageEntry *page) {
    if (page->is_valid) {
        phys_mem.frame_table[page->frame_number].pid = UNUSED_ID;
        phys_mem.frame_table[page->frame_number].page_entry = NULL;
        phys_mem.frame_table[page->frame_number].access_flag = 0;
        page->is_valid = 0;
    }
}

void free_process_memory(ProcessMemoryTable *process) {
    for (int i = 0; i < process->page_count; i++) {
        PageEntry *page = &process->page_table[i];
        clear_page(page);
        disk_mem.block_table[page->disk_block].associated_page = NULL;
        disk_mem.block_table[page->disk_block].is_used = 0;
    }
    free(process->page_table);
}

int locate_free_frame() {
    for (int i = 0; i < phys_mem.total_frames; i++)
        if (phys_mem.frame_table[i].pid == UNUSED_ID)
            return i;
    return UNUSED_ID;
}

int locate_free_block() {
    for (int i = 0; i < disk_mem.total_blocks; i++)
        if (disk_mem.block_table[i].associated_page == NULL)
            return i;
    return UNUSED_ID;
}

ProcessMemoryTable *get_process(pid_t pid) {
    for (int i = 0; i < num_processes; i++)
        if (proc_mem_table[i].pid == pid)
            return &proc_mem_table[i];
    exit(UNUSED_ID);
}

PageEntry *get_page(ProcessMemoryTable *process, intptr_t addr) {
    for (int i = 0; i < process->page_count; i++) {
        PageEntry *page = &process->page_table[i];
        if (addr >= page->virtual_address && addr < (page->virtual_address + phys_mem.size_of_page))
            return page;
    }
    return NULL;
}

int select_victim_frame() {
    FrameEntry *frames = phys_mem.frame_table;
    int victim_index = UNUSED_ID;
    int start_pointer = phys_mem.clock_pointer;

    do {
        int current_pointer = phys_mem.clock_pointer;
        if (frames[current_pointer].access_flag == 0) {
            victim_index = current_pointer;
            break;
        } else {
            frames[current_pointer].access_flag = 0;
            mmu_chprot(frames[current_pointer].pid, (void *)frames[current_pointer].page_entry->virtual_address, PROT_NONE);
        }
        phys_mem.clock_pointer = (current_pointer + 1) % phys_mem.total_frames;
    } while (phys_mem.clock_pointer != start_pointer);

    if (victim_index == UNUSED_ID) {
        victim_index = start_pointer;
    }

    phys_mem.clock_pointer = (victim_index + 1) % phys_mem.total_frames;
    return victim_index;
}

void swap_out_page(int frame_index) {
    FrameEntry *frame = &phys_mem.frame_table[frame_index];
    PageEntry *page_to_evict = frame->page_entry;
    
    mmu_nonresident(frame->pid, (void *)page_to_evict->virtual_address);
    
    if (page_to_evict->is_modified) {
        disk_mem.block_table[page_to_evict->disk_block].is_used = 1;
        mmu_disk_write(frame_index, page_to_evict->disk_block);
    }
    
    page_to_evict->is_valid = 0;
    frame->pid = UNUSED_ID;
    frame->page_entry = NULL;
    frame->access_flag = 0;
}

void pager_init(int num_frames, int num_blocks) {
    pthread_mutex_lock(&memory_mutex);
    
    phys_mem.total_frames = num_frames;
    phys_mem.size_of_page = sysconf(_SC_PAGESIZE);
    phys_mem.clock_pointer = 0;
    phys_mem.frame_table = calloc(num_frames, sizeof(FrameEntry));
    
    for (int i = 0; i < num_frames; i++) {
        phys_mem.frame_table[i].pid = UNUSED_ID;
        phys_mem.frame_table[i].page_entry = NULL;
        phys_mem.frame_table[i].access_flag = 0;
    }

    disk_mem.total_blocks = num_blocks;
    disk_mem.block_table = calloc(num_blocks, sizeof(DiskEntry));

    proc_mem_table = malloc(max_processes * sizeof(ProcessMemoryTable));
    
    pthread_mutex_unlock(&memory_mutex);
}

void pager_create(pid_t pid) {
    pthread_mutex_lock(&memory_mutex);
    
    if (num_processes == max_processes) {
        max_processes *= 2;
        proc_mem_table = realloc(proc_mem_table, max_processes * sizeof(ProcessMemoryTable));
    }

    ProcessMemoryTable *new_process = &proc_mem_table[num_processes++];
    new_process->pid = pid;
    new_process->page_table = NULL;
    new_process->page_count = 0;
    new_process->table_capacity = 0;

    pthread_mutex_unlock(&memory_mutex);
}

void *pager_extend(pid_t pid) {
    pthread_mutex_lock(&memory_mutex);
    
    int block_index = locate_free_block();
    if (block_index == UNUSED_ID) {
        pthread_mutex_unlock(&memory_mutex);
        return NULL;
    }

    ProcessMemoryTable *process = get_process(pid);

    if (process->page_count == process->table_capacity) {
        process->table_capacity = process->table_capacity == 0 ? 1 : process->table_capacity * 2;
        process->page_table = realloc(process->page_table, process->table_capacity * sizeof(PageEntry));
    }

    PageEntry *new_page = &process->page_table[process->page_count++];
    new_page->is_valid = 0;
    new_page->virtual_address = UVM_BASEADDR + (process->page_count - 1) * phys_mem.size_of_page;
    new_page->disk_block = block_index;
    new_page->is_modified = 0;

    disk_mem.block_table[block_index].associated_page = new_page;
    disk_mem.block_table[block_index].is_used = 0;

    pthread_mutex_unlock(&memory_mutex);
    return (void *)new_page->virtual_address;
}

void resolve_page_fault(PageEntry *page, pid_t pid, void *addr) {
    if (page->is_valid) {
        mmu_chprot(pid, addr, PROT_READ | PROT_WRITE);
        phys_mem.frame_table[page->frame_number].access_flag = 1;
        page->is_modified = 1;
    } else {
        int frame_index = locate_free_frame();

        if (frame_index == UNUSED_ID) {
            frame_index = select_victim_frame();
            swap_out_page(frame_index);
        }

        FrameEntry *new_frame = &phys_mem.frame_table[frame_index];
        new_frame->pid = pid;
        new_frame->page_entry = page;
        new_frame->access_flag = 1;

        page->is_valid = 1;
        page->frame_number = frame_index;
        page->is_modified = 0;

        if (disk_mem.block_table[page->disk_block].is_used)
            mmu_disk_read(page->disk_block, frame_index);
        else
            mmu_zero_fill(frame_index);

        mmu_resident(pid, addr, frame_index, PROT_READ);
    }
}

void pager_fault(pid_t pid, void *addr) {
    pthread_mutex_lock(&memory_mutex);
    
    ProcessMemoryTable *process = get_process(pid);
    addr = (void *)((intptr_t)addr - (intptr_t)addr % phys_mem.size_of_page);
    PageEntry *page = get_page(process, (intptr_t)addr);

    resolve_page_fault(page, pid, addr);

    pthread_mutex_unlock(&memory_mutex);
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
    pthread_mutex_lock(&memory_mutex);
    
    ProcessMemoryTable *process = get_process(pid);
    unsigned char *buffer = malloc(len);

    for (size_t i = 0; i < len; i++) {
        PageEntry *page = get_page(process, (intptr_t)addr + i);
        if (!page || !page->is_valid) {
            free(buffer);
            pthread_mutex_unlock(&memory_mutex);
            return -1;
        }
        buffer[i] = pmem[page->frame_number * phys_mem.size_of_page + ((intptr_t)addr + i) % phys_mem.size_of_page];
    }

    for (size_t i = 0; i < len; i++)
        printf("%02x", buffer[i]);
    if (len > 0)
        printf("\n");

    free(buffer);
    pthread_mutex_unlock(&memory_mutex);
    return 0;
}

void pager_destroy(pid_t pid) {
    pthread_mutex_lock(&memory_mutex);
    ProcessMemoryTable *process = get_process(pid);
    free_process_memory(process);
    pthread_mutex_unlock(&memory_mutex);
}
