/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "disk.h"
#include "page_table.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define UNKNOWN -1

#define TYPE_RAND 1
#define TYPE_FIFO 2
#define TYPE_CUSTOM 3

#define GET_PAGE_PHYS_ADDR(pt, frame) (&page_table_get_physmem(pt)[frame * PAGE_SIZE]) 
#define HEAD_INDEX ((int)(head - frame_info))

struct {
	int page_faults;
	int free_pages;
	int type;
	int disk_writes;
	int disk_reads;
	int invalidatings;
} state = {.type = UNKNOWN};

struct frame_info *frame_info;
struct disk *disk;
struct frame_info *head = NULL;
struct frame_info *tail = NULL;

struct frame_info {
	int is_free : 1;
	int dirty : 1;
	int cached : 1;
	int bits : 4; // 2 bits actually should be enough for r/w

	int page_num;

	struct frame_info *next;
	struct frame_info *prev;
};

void die(char *str) {
	fputs(str, stderr);
	exit(254);
}

void list_push_back(int frame) {
	struct frame_info *node = &frame_info[frame];

	if (node->cached) {
		return;
	}

	if (!head) { // empty list
		head = node;
		tail = node;
		node->cached = 1;
	} else {
		tail->next = node;
		node->next = NULL;
		node->prev = tail;
		tail = node;

		node->cached = 1;
	}
}

int list_pop() {
	int frame = UNKNOWN;
	if (head == tail) { // 1 element in list
		frame = HEAD_INDEX;
		head->cached = 0;
		head = tail = NULL;
	} else if (head || tail) {
		frame = HEAD_INDEX;
		head->cached = 0;
		head = head->next;
		head->prev = NULL;
	}

	return frame;
}

void flush_page(struct page_table *pt, int frame) {
	if (frame_info[frame].bits & PROT_WRITE) {
		disk_write(disk, frame_info[frame].page_num, GET_PAGE_PHYS_ADDR(pt, frame));
		++state.disk_writes;
	}
	//Invalidating
	page_table_set_entry(pt, frame_info[frame].page_num, frame, PROT_NONE);
	frame_info[frame].bits = PROT_NONE;
	++state.invalidatings;
}

int get_next_free_frame(int nframes) {
	if (state.free_pages >= nframes) return UNKNOWN;

	for (int i = 0; i < nframes; ++i) {
		if (frame_info[i].is_free == 0) {
			++state.free_pages;
			return i;
		}
	}
	return UNKNOWN;
}


void page_fault_handler(struct page_table *pt, int page) {
	++state.page_faults;
#ifdef DEBUG
	if (state.page_faults % 100 == 0) printf("Fault num: %d\n", state.page_faults);
	printf("Page fault at %d\n", page);
	page_table_print(pt);
#endif

	int frame, bit;
	page_table_get_entry(pt, page, &frame, &bit);

	int new_frame = UNKNOWN;
	if (!bit) { // read page fault
		bit |= PROT_READ;
		if ((new_frame = get_next_free_frame(page_table_get_nframes(pt))) == UNKNOWN) {
			
			switch (state.type) {
			case TYPE_RAND:
				new_frame = random() % page_table_get_nframes(pt);
				break;
			case TYPE_FIFO:
				new_frame = list_pop();

				if (new_frame == UNKNOWN) {
				#ifdef DEBUG
					puts("Attempt to pop from empty list");
				#endif
					return;
				}
				break;
			case TYPE_CUSTOM:
				die("Not implemented");
				break;
			default:
				die("Unknown handler type"); // should be actually procecuted earlier
			}
			flush_page(pt, new_frame);
		}

		disk_read(disk, page, GET_PAGE_PHYS_ADDR(pt, frame));
		++state.disk_reads;
	} else if (bit ^ PROT_WRITE) { // write page fault
		bit |= PROT_WRITE;
		new_frame = frame;
	} else {
		die("Wrong protection bits");
	}

	page_table_set_entry(pt, page, new_frame, bit);
	frame_info[new_frame].page_num = page;
	frame_info[new_frame].bits = bit;
	frame_info[new_frame].is_free = 1;

	switch (state.type) {
		case TYPE_FIFO:
			list_push_back(new_frame);
			break;
	}
}

void parse_type(char *str) {
	if (!strcmp(str, "rand")) {
		state.type = TYPE_RAND;
		srandom(time(0));
	} else if (!strcmp(str, "fifo")) {
		state.type = TYPE_FIFO;
	} else if (!strcmp(str, "custom")) {
		state.type = TYPE_CUSTOM;
	}
}

int main( int argc, char *argv[] ) {
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|fifo|custom> <sort|scan|focus>\n");
		return 1;
	}

	int npages = atoi(argv[1]);
	int nframes = atoi(argv[2]);
	parse_type(argv[3]);
	const char *program = argv[4];

	disk = disk_open("myvirtualdisk",npages);
	if(!disk) {
		fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
		return 1;
	}


	struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
	if(!pt) {
		fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
		return 1;
	}

	frame_info = malloc(nframes * sizeof(struct frame_info));
	memset(frame_info, 0, nframes * sizeof(struct frame_info));

	char *virtmem = page_table_get_virtmem(pt);

    if(!strcmp(program,"sort")) {
		sort_program(virtmem,npages*PAGE_SIZE);
	} else if(!strcmp(program,"scan")) {
		scan_program(virtmem,npages*PAGE_SIZE);
	} else if(!strcmp(program,"focus")) {
		focus_program(virtmem,npages*PAGE_SIZE);
	} else {
		fprintf(stderr,"unknown program: %s\n",argv[4]);
	}

	printf("Page faults: %d\tDisk writes: %d\tDisk reads: %d\tInvalidatings: %d\n", 
		state.page_faults, state.disk_writes, state.disk_reads, state.invalidatings);

	free(frame_info);
	page_table_delete(pt);
	disk_close(disk);

	return 0;
}
