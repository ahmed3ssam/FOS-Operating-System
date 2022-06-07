#include <inc/lib.h>
uint32 U_Nextfit(int );
uint32 U_alloc(int , int , int );
struct Addr_Inf {
	uint32 Size;
	uint32 Var_Addr;
};
struct Addr_Inf addr_inf[(USER_HEAP_MAX - USER_HEAP_START) / PAGE_SIZE];
int Tracker[(USER_HEAP_MAX - USER_HEAP_START) / PAGE_SIZE] = { 0 };
uint32 Next_Fit_Addr = USER_HEAP_START;
#define u_heap_frames ((USER_HEAP_MAX - USER_HEAP_START) / PAGE_SIZE)
int u_lastAloocatedAdd = 0;
int u_taken[u_heap_frames] = { 0 };
uint32 u_allocatedSize[u_heap_frames];
uint32 U_Nextfit(int a) {
    uint32 returnStart = U_alloc(u_lastAloocatedAdd, u_heap_frames, a);
    if (returnStart != 0) {
        return returnStart;
    } else {
        return U_alloc(0, u_lastAloocatedAdd, a);
    }
    return 0;
}
uint32 U_alloc(int str, int end, int a) {
    int counter = 0;
    uint32 returnStart = 0;
    for (int i = str; i < end; i++) {
        if (u_taken[i] == 0) {
            counter++;
            if (returnStart == 0) {
                returnStart = (i * PAGE_SIZE) + USER_HEAP_START;
            }
            if (counter == a) {
                u_lastAloocatedAdd = i;
                return returnStart;
            }
        } else {
            counter = 0;
            returnStart = 0;
        }
    }
    return 0;
}
void* malloc(uint32 size) {
    size = ROUNDUP(size, PAGE_SIZE);
    int a = size/PAGE_SIZE;
    uint32 ret_start=0;
    if (sys_isUHeapPlacementStrategyNEXTFIT()) {
        ret_start = U_Nextfit(a);
        uint32 assign = ret_start;
        if (ret_start == 0) {
            return NULL;
        } else {
        	u_allocatedSize[(assign - USER_HEAP_START) / PAGE_SIZE]=a;
            for (int i = 0; i < a; i++) {
            	u_taken[(assign - USER_HEAP_START) / PAGE_SIZE] = 1;
            	assign+=PAGE_SIZE;
            }
            sys_allocateMem(ret_start, size);
            return (void*) ret_start;
        }
    }
    return NULL;
}
void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable) {
	panic("smalloc() is not required ..!!");
	return NULL;
}
void* sget(int32 ownerEnvID, char *sharedVarName) {
	panic("sget() is not required ..!!");
	return 0;
}
void free(void* virtual_address) {
	uint32 a = (uint32) virtual_address;
		int size = u_allocatedSize[(a - USER_HEAP_START) / PAGE_SIZE];
		if (u_taken[(a - USER_HEAP_START) / PAGE_SIZE] == 0) {
			return;
		}
		for (int i = 0; i < size; i++) {
			u_taken[(a - USER_HEAP_START) / PAGE_SIZE] = 0;
			a += PAGE_SIZE;
		}
	sys_freeMem((uint32) virtual_address, size*PAGE_SIZE);
}
void sfree(void* virtual_address) {
	panic("sfree() is not requried ..!!");
}
void *realloc(void *virtual_address, uint32 new_size) {
	panic("realloc() is not implemented yet...!!");
	return NULL;
}
