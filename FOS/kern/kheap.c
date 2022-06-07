#include <inc/memlayout.h>
#include <kern/kheap.h>
#include <kern/memory_manager.h>
#define heap_frames (KERNEL_HEAP_MAX-KERNEL_HEAP_START)/PAGE_SIZE
int lastAloocatedAdd = 0;
uint32 alloc(int, int, int);
uint32 Bestfit(int );
int taken[heap_frames];
uint32 allocatedSize[heap_frames];
uint32 Nextfit(int a) {
	if (isKHeapPlacementStrategyNEXTFIT()) {
		uint32 returnStart = alloc(lastAloocatedAdd, heap_frames, a);
		if (returnStart != 0) {
			return returnStart;
		} else {
			return alloc(0, lastAloocatedAdd, a);
		}
	}
	return 0;
}
uint32 alloc(int str, int end, int a) {
	int counter = 0;
	uint32 returnStart = 0;
	for (int i = str; i < end; i++) {
		if (taken[i] == 0) {
			counter++;
			if (returnStart == 0) {
				returnStart = (i * PAGE_SIZE) + KERNEL_HEAP_START;
			}
			if (counter == a) {
				lastAloocatedAdd = i;
				return returnStart;
			}
		} else {
			counter = 0;
			returnStart = 0;
		}
	}
	return 0;
}
uint32 Bestfit(int a) {
	int extra = heap_frames;
	uint32 best_va = 0;
	uint32 temp_va = KERNEL_HEAP_START;
	for(;;) {
		temp_va = alloc((temp_va - KERNEL_HEAP_START) / PAGE_SIZE, heap_frames, a);
		if (temp_va == 0)
			return best_va;
		int index = (temp_va - KERNEL_HEAP_START) / PAGE_SIZE;
		index += a;
		int temp_extra = 0;
		while(index < heap_frames) {
			if (taken[index] == 1)
				break;
			temp_extra++;
			index++;
		}
		if (temp_extra == 0) {
			return temp_va;
		}
		if (temp_extra < extra) {
			extra = temp_extra;
			best_va = temp_va;
		}
		temp_va = index*PAGE_SIZE + KERNEL_HEAP_START;
	}
}
void* kmalloc(unsigned int size) {
	uint32 neededSize = ROUNDUP(size, PAGE_SIZE);
	int a = neededSize / PAGE_SIZE;
	uint32 ret = 0;
	if(isKHeapPlacementStrategyNEXTFIT())
		 ret = Nextfit(a);
	else if (isKHeapPlacementStrategyBESTFIT())
		ret = Bestfit(a);
	uint32 assign = ret;
	if (ret == 0) {
		return NULL;
	} else {
		for (int i = 0; i < a; i++) {
			struct Frame_Info* newFrame = NULL;
			int ret = allocate_frame(&newFrame);
			if (ret == E_NO_MEM) {
				return NULL;
			} else {
				map_frame(ptr_page_directory, newFrame, (void*) assign,
				PERM_WRITEABLE | PERM_PRESENT);
				taken[(assign - KERNEL_HEAP_START) / PAGE_SIZE] = 1;
				assign += PAGE_SIZE;
			}
		}
	}
	allocatedSize[(ret - KERNEL_HEAP_START) / PAGE_SIZE] = a;
	return (void*) ret;
}
void kfree(void* virtual_address) {
	uint32 a = (uint32) virtual_address;
	int size = allocatedSize[(a - KERNEL_HEAP_START) / PAGE_SIZE];
	if (taken[(a - KERNEL_HEAP_START) / PAGE_SIZE] == 0) {
		return;
	}
	for (int i = 0; i < size; i++) {
		unmap_frame(ptr_page_directory, (void*) a);
		taken[(a - KERNEL_HEAP_START) / PAGE_SIZE] = 0;
		a += PAGE_SIZE;
	}
}
unsigned int kheap_virtual_address(unsigned int physical_address) {
	struct Frame_Info * PhysFrame = to_frame_info(physical_address);
	int vframe_num;
	for (vframe_num = 0; vframe_num < heap_frames; vframe_num++) {
		uint32* ptr_PT = NULL;
		uint32 va = (vframe_num * PAGE_SIZE) + KERNEL_HEAP_START;
		struct Frame_Info * VirtFrame = get_frame_info(ptr_page_directory,
				(void*) va, &ptr_PT);
		if (VirtFrame == PhysFrame) {
			return va;
		}
	}
	return 0;
}
unsigned int kheap_physical_address(unsigned int virtual_address) {
	uint32* ptr_PT = NULL;
	get_page_table(ptr_page_directory, (void*) virtual_address, &ptr_PT);
	if (ptr_PT != NULL) {
		return (unsigned int) ((ptr_PT[PTX(virtual_address)] >> 12) * PAGE_SIZE)
				+ (virtual_address & (0x00000FFF));
	}
	return 0;
}
