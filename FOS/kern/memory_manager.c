#include <kern/memory_manager.h>
#include <kern/file_manager.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <kern/trap.h>
#include <kern/kclock.h>
#include <kern/user_environment.h>
#include <kern/sched.h>
#include <kern/kheap.h>
#include <kern/file_manager.h>
extern uint32 number_of_frames;
extern uint32 size_of_base_mem;
extern uint32 size_of_extended_mem;
inline uint32 env_table_ws_get_size(struct Env *e);
inline void env_table_ws_invalidate(struct Env* e, uint32 virtual_address);
inline void env_table_ws_set_entry(struct Env* e, uint32 entry_index,
		uint32 virtual_address);
inline void env_table_ws_clear_entry(struct Env* e, uint32 entry_index);
inline uint32 env_table_ws_get_virtual_address(struct Env* e,
		uint32 entry_index);
inline uint32 env_table_ws_get_time_stamp(struct Env* e, uint32 entry_index);
inline uint32 env_table_ws_is_entry_empty(struct Env* e, uint32 entry_index);
void env_table_ws_print(struct Env *curenv);
inline uint32 pd_is_table_used(struct Env *e, uint32 virtual_address);
inline void pd_set_table_unused(struct Env *e, uint32 virtual_address);
inline void pd_clear_page_dir_entry(struct Env *e, uint32 virtual_address);
uint32* ptr_page_directory;
uint8* ptr_zero_page;
uint8* ptr_temp_page;
uint32 phys_page_directory;
char* ptr_free_mem;
struct Frame_Info* frames_info;
struct Frame_Info* disk_frames_info;
struct Linked_List free_frame_list;
struct Linked_List modified_frame_list;
void initialize_kernel_VM() {
	ptr_page_directory = boot_allocate_space(PAGE_SIZE, PAGE_SIZE);
	memset(ptr_page_directory, 0, PAGE_SIZE);
	phys_page_directory = STATIC_KERNEL_PHYSICAL_ADDRESS(ptr_page_directory);
	boot_map_range(ptr_page_directory, KERNEL_STACK_TOP - KERNEL_STACK_SIZE,
	KERNEL_STACK_SIZE, STATIC_KERNEL_PHYSICAL_ADDRESS(ptr_stack_bottom),
	PERM_WRITEABLE);
	unsigned long long sva = KERNEL_BASE;
	unsigned int nTables = 0;
	for (; sva < 0xFFFFFFFF; sva += PTSIZE)
	{
		++nTables;
		boot_get_page_table(ptr_page_directory, (uint32) sva, 1);
	}
	uint32 array_size;
	array_size = number_of_frames * sizeof(struct Frame_Info);
	frames_info = boot_allocate_space(array_size, PAGE_SIZE);
	memset(frames_info, 0, array_size);
	uint32 disk_array_size = PAGES_PER_FILE * sizeof(struct Frame_Info);
	disk_frames_info = boot_allocate_space(disk_array_size, PAGE_SIZE);
	memset(disk_frames_info, 0, disk_array_size);
	setup_listing_to_all_page_tables_entries();
	cprintf("Max Envs = %d\n", NENV);
	int envs_size = NENV * sizeof(struct Env);
	envs = boot_allocate_space(envs_size, PAGE_SIZE);
	memset(envs, 0, envs_size);
	boot_map_range(ptr_page_directory, UENVS, envs_size,
			STATIC_KERNEL_PHYSICAL_ADDRESS(envs), PERM_USER);
	ptr_page_directory[PDX(UENVS)] = ptr_page_directory[PDX(UENVS)]
			| (PERM_USER | (PERM_PRESENT & (~PERM_WRITEABLE)));
	if (USE_KHEAP) {
		boot_map_range(ptr_page_directory, KERNEL_BASE,
				(uint32) ptr_free_mem - KERNEL_BASE, 0, PERM_WRITEABLE);
	} else {
		boot_map_range(ptr_page_directory, KERNEL_BASE,
				0xFFFFFFFF - KERNEL_BASE, 0, PERM_WRITEABLE);
	}
	check_boot_pgdir();
	turn_on_paging();
}
void* boot_allocate_space(uint32 size, uint32 align) {
	extern char end_of_kernel[];
	if (ptr_free_mem == 0)
		ptr_free_mem = end_of_kernel;
	ptr_free_mem = ROUNDUP(ptr_free_mem, align);
	void *ptr_allocated_mem;
	ptr_allocated_mem = ptr_free_mem;
	ptr_free_mem += size;
	return ptr_allocated_mem;
}
void boot_map_range(uint32 *ptr_page_directory, uint32 virtual_address,
		uint32 size, uint32 physical_address, int perm) {
	int i = 0;
	for (i = 0; i < size; i += PAGE_SIZE)
	{
		uint32 *ptr_page_table = boot_get_page_table(ptr_page_directory,
				virtual_address, 1);
		uint32 index_page_table = PTX(virtual_address);
		ptr_page_table[index_page_table] = CONSTRUCT_ENTRY(physical_address,
				perm | PERM_PRESENT);
		physical_address += PAGE_SIZE;
		virtual_address += PAGE_SIZE;
	}
}
uint32* boot_get_page_table(uint32 *ptr_page_directory, uint32 virtual_address,
		int create) {
	uint32 index_page_directory = PDX(virtual_address);
	uint32 page_directory_entry = ptr_page_directory[index_page_directory];
	uint32 phys_page_table = EXTRACT_ADDRESS(page_directory_entry);
	uint32 *ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(phys_page_table);
	if (phys_page_table == 0) {
		if (create) {
			ptr_page_table = boot_allocate_space(PAGE_SIZE, PAGE_SIZE);
			phys_page_table = STATIC_KERNEL_PHYSICAL_ADDRESS(ptr_page_table);
			ptr_page_directory[index_page_directory] = CONSTRUCT_ENTRY(
					phys_page_table, PERM_PRESENT | PERM_WRITEABLE);
			return ptr_page_table;
		} else
			return 0;
	}
	return ptr_page_table;
}
extern void initialize_disk_page_file();
void initialize_paging() {
	int i;
	LIST_INIT(&free_frame_list);
	LIST_INIT(&modified_frame_list);
	frames_info[0].references = 1;
	frames_info[1].references = 1;
	frames_info[2].references = 1;
	ptr_zero_page = (uint8*) KERNEL_BASE + PAGE_SIZE;
	ptr_temp_page = (uint8*) KERNEL_BASE + 2 * PAGE_SIZE;
	i = 0;
	for (; i < 1024; i++) {
		ptr_zero_page[i] = 0;
		ptr_temp_page[i] = 0;
	}
	int range_end = ROUNDUP(PHYS_IO_MEM, PAGE_SIZE);
	for (i = 3; i < range_end / PAGE_SIZE; i++) {
		initialize_frame_info(&(frames_info[i]));
		LIST_INSERT_HEAD(&free_frame_list, &frames_info[i]);
	}
	for (i = PHYS_IO_MEM / PAGE_SIZE; i < PHYS_EXTENDED_MEM / PAGE_SIZE; i++) {
		frames_info[i].references = 1;
	}
	range_end = ROUNDUP(STATIC_KERNEL_PHYSICAL_ADDRESS(ptr_free_mem),
			PAGE_SIZE);
	for (i = PHYS_EXTENDED_MEM / PAGE_SIZE; i < range_end / PAGE_SIZE; i++) {
		frames_info[i].references = 1;
	}
	for (i = range_end / PAGE_SIZE; i < number_of_frames; i++) {
		initialize_frame_info(&(frames_info[i]));
		LIST_INSERT_HEAD(&free_frame_list, &frames_info[i]);
	}
	initialize_disk_page_file();
}
void initialize_frame_info(struct Frame_Info *ptr_frame_info) {
	memset(ptr_frame_info, 0, sizeof(*ptr_frame_info));
}
extern void env_free(struct Env *e);
int allocate_frame(struct Frame_Info **ptr_frame_info) {
	*ptr_frame_info = LIST_FIRST(&free_frame_list);
	int c = 0;
	if (*ptr_frame_info == NULL) {
		panic(
				"ERROR: Kernel run out of memory... allocate_frame cannot find a free frame.\n");
	}
	LIST_REMOVE(&free_frame_list, *ptr_frame_info);

	if ((*ptr_frame_info)->isBuffered) {
		pt_clear_page_table_entry((*ptr_frame_info)->environment,
				(*ptr_frame_info)->va);
	}

	initialize_frame_info(*ptr_frame_info);
	return 0;
}
void free_frame(struct Frame_Info *ptr_frame_info) {

	initialize_frame_info(ptr_frame_info);

	LIST_INSERT_HEAD(&free_frame_list, ptr_frame_info);
}
void decrement_references(struct Frame_Info* ptr_frame_info) {
	if (--(ptr_frame_info->references) == 0)
		free_frame(ptr_frame_info);
}
int get_page_table(uint32 *ptr_page_directory, const void *virtual_address,
		uint32 **ptr_page_table) {
	uint32 page_directory_entry = ptr_page_directory[PDX(virtual_address)];
	if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
		*ptr_page_table = (void *) kheap_virtual_address(
				EXTRACT_ADDRESS(page_directory_entry));
	} else {
		*ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
				EXTRACT_ADDRESS(page_directory_entry));
	}
	if ((page_directory_entry & PERM_PRESENT) == PERM_PRESENT) {
		return TABLE_IN_MEMORY;
	} else if (page_directory_entry != 0)
			{
		lcr2((uint32) virtual_address);
		fault_handler(NULL);
		page_directory_entry = ptr_page_directory[PDX(virtual_address)];
		if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
			*ptr_page_table = (void *) kheap_virtual_address(
					EXTRACT_ADDRESS(page_directory_entry));
		} else {
			*ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
					EXTRACT_ADDRESS(page_directory_entry));
		}
		return TABLE_IN_MEMORY;
	} else
	{
		*ptr_page_table = 0;
		return TABLE_NOT_EXIST;
	}
}
void * create_page_table(uint32 *ptr_page_directory,
		const uint32 virtual_address) {
	uint32* ptr_page_table_start_h_v = (uint32*) kmalloc(1);
	if (ptr_page_table_start_h_v == NULL) {
		return (void*) NULL;
	}
	uint32 ptr_page_table_start_h_ph = kheap_physical_address(
			(unsigned int) ptr_page_table_start_h_v);
	int i = 0;
	for (; i < 1024; i++)
		ptr_page_table_start_h_v[i] = 0;
	ptr_page_directory[PDX(virtual_address)] = CONSTRUCT_ENTRY(
			(uint32 )ptr_page_table_start_h_ph,
			PERM_PRESENT | PERM_USER | PERM_WRITEABLE);
	tlbflush();
	return (void*) ptr_page_table_start_h_v;
}
void __static_cpt(uint32 *ptr_page_directory, const uint32 virtual_address,
		uint32 **ptr_page_table) {
	panic("this function is not required...!!");
}
int map_frame(uint32 *ptr_page_directory, struct Frame_Info *ptr_frame_info,
		void *virtual_address, int perm) {
	uint32 physical_address = to_physical_address(ptr_frame_info);
	uint32 *ptr_page_table;
	if (get_page_table(ptr_page_directory, virtual_address,
			&ptr_page_table) == TABLE_NOT_EXIST) {
		if (USE_KHEAP) {
			ptr_page_table = create_page_table(ptr_page_directory,
					(uint32) virtual_address);
		} else {
			__static_cpt(ptr_page_directory, (uint32) virtual_address,
					&ptr_page_table);
		}
	}
	uint32 page_table_entry = ptr_page_table[PTX(virtual_address)];
	if ((page_table_entry & PERM_PRESENT) == PERM_PRESENT) {
		if (EXTRACT_ADDRESS(page_table_entry) == physical_address)
			return 0;
		else
			unmap_frame(ptr_page_directory, virtual_address);
	}
	ptr_frame_info->references++;
	ptr_page_table[PTX(virtual_address)] = CONSTRUCT_ENTRY(physical_address,
			perm | PERM_PRESENT);
	return 0;
}
struct Frame_Info * get_frame_info(uint32 *ptr_page_directory,
		void *virtual_address, uint32 **ptr_page_table) {
	uint32 ret = get_page_table(ptr_page_directory, virtual_address,
			ptr_page_table);
	if ((*ptr_page_table) != 0) {
		uint32 index_page_table = PTX(virtual_address);
		uint32 page_table_entry = (*ptr_page_table)[index_page_table];
		if (page_table_entry != 0) {
			return to_frame_info(EXTRACT_ADDRESS(page_table_entry));
		}
		return 0;
	}
	return 0;
}
void unmap_frame(uint32 *ptr_page_directory, void *virtual_address) {
	uint32 *ptr_page_table;
	struct Frame_Info* ptr_frame_info = get_frame_info(ptr_page_directory,
			virtual_address, &ptr_page_table);
	if (ptr_frame_info != 0) {
		if (ptr_frame_info->isBuffered
				&& !CHECK_IF_KERNEL_ADDRESS((uint32 )virtual_address))
			cprintf("Freeing BUFFERED frame at va %x!!!\n", virtual_address);
		decrement_references(ptr_frame_info);
		ptr_page_table[PTX(virtual_address)] = 0;
		tlb_invalidate(ptr_page_directory, virtual_address);
	}
}
int loadtime_map_frame(uint32 *ptr_page_directory,
		struct Frame_Info *ptr_frame_info, void *virtual_address, int perm) {
	uint32 physical_address = to_physical_address(ptr_frame_info);
	uint32 *ptr_page_table;
	uint32 page_directory_entry = ptr_page_directory[PDX(virtual_address)];
	if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
		ptr_page_table = (uint32*) kheap_virtual_address(
				EXTRACT_ADDRESS(page_directory_entry));
	} else {
		ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
				EXTRACT_ADDRESS(page_directory_entry));
	}
	if (page_directory_entry == 0) {
		if (USE_KHEAP) {
			ptr_page_table = create_page_table(ptr_page_directory,
					(uint32) virtual_address);
		} else {
			__static_cpt(ptr_page_directory, (uint32) virtual_address,
					&ptr_page_table);
		}
	}
	ptr_frame_info->references++;
	ptr_page_table[PTX(virtual_address)] = CONSTRUCT_ENTRY(physical_address,
			perm | PERM_PRESENT);
	return 0;
}
void allocateMem(struct Env* e, uint32 virtual_address, uint32 size) {
	size = ROUNDUP(size, PAGE_SIZE);
	int Num_Of_Pages = size / PAGE_SIZE;
	for (int i = 0; i < Num_Of_Pages; i++) {
		pf_add_empty_env_page(e, virtual_address, 0);
		virtual_address = virtual_address + PAGE_SIZE;
	}

}
void freeMem(struct Env* e, uint32 virtual_address, uint32 size) {
	virtual_address = ROUNDDOWN(virtual_address, PAGE_SIZE);
	size = ROUNDUP(size, PAGE_SIZE);
	uint32 *Ptr_Page_Table;
	uint32 Free_Pages;
	for (int i = virtual_address; i < size + virtual_address; i += PAGE_SIZE)
	{
		pf_remove_env_page(e, i);
		for (int j = 0; j < e->page_WS_max_size; j++) {
			if (e->ptr_pageWorkingSet[j].virtual_address == i) {
				env_page_ws_clear_entry(e, j);
				unmap_frame(e->env_page_directory, (void *) i);
				break;
			}
		}
	}
	for (int i = virtual_address; i <= (virtual_address + size);
			i += (PAGE_SIZE * 1024)) {
		get_page_table(e->env_page_directory, (void *) i, &Ptr_Page_Table);
		if (Ptr_Page_Table == NULL)
			continue;

		for (int j = 0; j < 1024; ++j) {
			if (Ptr_Page_Table[j] != 0)
				goto jump;
		}
		kfree((void *) Ptr_Page_Table);
		e->env_page_directory[PDX(i)] = 0;
		jump:
			continue;
	}
	tlbflush();
}
int nfgdsjklgbhkj=89;
void __freeMem_with_buffering(struct Env* e, uint32 virtual_address,
		uint32 size) {
}
void moveMem(struct Env* e, uint32 src_virtual_address,
		uint32 dst_virtual_address, uint32 size) {
	panic("moveMem() is not implemented yet...!!");
}
uint32 calculate_required_frames(uint32* ptr_page_directory,
		uint32 start_virtual_address, uint32 size) {
	LOG_STATMENT(
			cprintf("calculate_required_frames: Starting at address %x",
					start_virtual_address));
	uint32 number_of_tables = 0;
	long i = 0;
	uint32 current_virtual_address = ROUNDDOWN(start_virtual_address,
			PAGE_SIZE*1024);
	for (; current_virtual_address < (start_virtual_address + size);
			current_virtual_address += PAGE_SIZE * 1024) {
		uint32 *ptr_page_table;
		get_page_table(ptr_page_directory, (void*) current_virtual_address,
				&ptr_page_table);
		if (ptr_page_table == 0) {
			(number_of_tables)++;
		}
	}
	uint32 number_of_pages = 0;
	current_virtual_address = ROUNDDOWN(start_virtual_address, PAGE_SIZE);
	for (; current_virtual_address < (start_virtual_address + size);
			current_virtual_address += PAGE_SIZE)
			{
		uint32 *ptr_page_table;
		if (get_frame_info(ptr_page_directory, (void*) current_virtual_address,
				&ptr_page_table) == 0) {
			(number_of_pages)++;
		}
	}
	LOG_STATMENT(cprintf("calculate_required_frames: Done!"));
	return number_of_tables + number_of_pages;
}
struct freeFramesCounters calculate_available_frames() {
	struct Frame_Info *ptr;
	uint32 totalFreeUnBuffered = 0;
	uint32 totalFreeBuffered = 0;
	uint32 totalModified = 0;
	LIST_FOREACH(ptr, &free_frame_list)
	{
		if (ptr->isBuffered)
			totalFreeBuffered++;
		else
			totalFreeUnBuffered++;
	}
	LIST_FOREACH(ptr, &modified_frame_list)
	{
		totalModified++;
	}
	struct freeFramesCounters counters;
	counters.freeBuffered = totalFreeBuffered;
	counters.freeNotBuffered = totalFreeUnBuffered;
	counters.modified = totalModified;
	return counters;
}
uint32 calculate_free_frames() {
	return LIST_SIZE(&free_frame_list);
}
inline uint32 env_page_ws_get_size(struct Env *e) {
	int i = 0, counter = 0;
	for (; i < e->page_WS_max_size; i++)
		if (e->ptr_pageWorkingSet[i].empty == 0)
			counter++;
	return counter;
}
inline void env_page_ws_invalidate(struct Env* e, uint32 virtual_address) {
	int i = 0;
	for (; i < e->page_WS_max_size; i++) {
		if (ROUNDDOWN(e->ptr_pageWorkingSet[i].virtual_address,
				PAGE_SIZE) == ROUNDDOWN(virtual_address, PAGE_SIZE)) {
			env_page_ws_clear_entry(e, i);
			break;
		}
	}
}
inline void env_page_ws_set_entry(struct Env* e, uint32 entry_index,
		uint32 virtual_address) {
	assert(entry_index >= 0 && entry_index < e->page_WS_max_size);
	assert(virtual_address >= 0 && virtual_address < USER_TOP);
	e->ptr_pageWorkingSet[entry_index].virtual_address = ROUNDDOWN(
			virtual_address, PAGE_SIZE);
	e->ptr_pageWorkingSet[entry_index].empty = 0;
	e->ptr_pageWorkingSet[entry_index].time_stamp = 0x80000000;
	return;
}
inline void env_page_ws_clear_entry(struct Env* e, uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < (e->page_WS_max_size));
	e->ptr_pageWorkingSet[entry_index].virtual_address = 0;
	e->ptr_pageWorkingSet[entry_index].empty = 1;
	e->ptr_pageWorkingSet[entry_index].time_stamp = 0;
}
inline uint32 env_page_ws_get_virtual_address(struct Env* e, uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < (e->page_WS_max_size));
	return ROUNDDOWN(e->ptr_pageWorkingSet[entry_index].virtual_address,
			PAGE_SIZE);
}
inline uint32 env_page_ws_get_time_stamp(struct Env* e, uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < (e->page_WS_max_size));
	return e->ptr_pageWorkingSet[entry_index].time_stamp;
}
inline uint32 env_page_ws_is_entry_empty(struct Env* e, uint32 entry_index) {
	return e->ptr_pageWorkingSet[entry_index].empty;
}
void env_page_ws_print(struct Env *curenv) {
	uint32 i;
	cprintf("PAGE WS:\n");
	for (i = 0; i < (curenv->page_WS_max_size); i++) {
		if (curenv->ptr_pageWorkingSet[i].empty) {
			cprintf("EMPTY LOCATION");
			if (i == curenv->page_last_WS_index) {
				cprintf("		<--");
			}
			cprintf("\n");
			continue;
		}
		uint32 virtual_address = curenv->ptr_pageWorkingSet[i].virtual_address;
		uint32 time_stamp = curenv->ptr_pageWorkingSet[i].time_stamp;
		uint32 perm = pt_get_page_permissions(curenv, virtual_address);
		char isModified = ((perm & PERM_MODIFIED) ? 1 : 0);
		char isUsed = ((perm & PERM_USED) ? 1 : 0);
		char isBuffered = ((perm & PERM_BUFFERED) ? 1 : 0);
		cprintf("address @ %d = %x", i,
				curenv->ptr_pageWorkingSet[i].virtual_address);
		cprintf(", used= %d, modified= %d, buffered= %d, time stamp= %x",
				isUsed, isModified, isBuffered, time_stamp);
		if (i == curenv->page_last_WS_index) {
			cprintf(" <--");
		}
		cprintf("\n");
	}
}
void env_table_ws_print(struct Env *curenv) {
	uint32 i;
	cprintf("---------------------------------------------------\n");
	cprintf("TABLE WS:\n");
	for (i = 0; i < __TWS_MAX_SIZE; i++) {
		if (curenv->__ptr_tws[i].empty) {
			cprintf("EMPTY LOCATION");
			if (i == curenv->table_last_WS_index) {
				cprintf("		<--");
			}
			cprintf("\n");
			continue;
		}
		uint32 virtual_address = curenv->__ptr_tws[i].virtual_address;
		cprintf("env address at %d = %x", i,
				curenv->__ptr_tws[i].virtual_address);
		cprintf(", used bit = %d, time stamp = %d",
				pd_is_table_used(curenv, virtual_address),
				curenv->__ptr_tws[i].time_stamp);
		if (i == curenv->table_last_WS_index) {
			cprintf(" <--");
		}
		cprintf("\n");
	}
}
inline uint32 env_table_ws_get_size(struct Env *e) {
	int i = 0, counter = 0;
	for (; i < __TWS_MAX_SIZE; i++)
		if (e->__ptr_tws[i].empty == 0)
			counter++;
	return counter;
}
inline void env_table_ws_invalidate(struct Env* e, uint32 virtual_address) {
	int i = 0;
	for (; i < __TWS_MAX_SIZE; i++) {
		if (ROUNDDOWN(e->__ptr_tws[i].virtual_address,
				PAGE_SIZE*1024) == ROUNDDOWN(virtual_address, PAGE_SIZE*1024)) {
			env_table_ws_clear_entry(e, i);
			break;
		}
	}
}
inline void env_table_ws_set_entry(struct Env* e, uint32 entry_index,
		uint32 virtual_address) {
	assert(entry_index >= 0 && entry_index < __TWS_MAX_SIZE);
	assert(virtual_address >= 0 && virtual_address < USER_TOP);
	e->__ptr_tws[entry_index].virtual_address = ROUNDDOWN(virtual_address,
			PAGE_SIZE*1024);
	e->__ptr_tws[entry_index].empty = 0;
	e->__ptr_tws[entry_index].time_stamp = 0x80000000;
	return;
}
inline void env_table_ws_clear_entry(struct Env* e, uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < __TWS_MAX_SIZE);
	e->__ptr_tws[entry_index].virtual_address = 0;
	e->__ptr_tws[entry_index].empty = 1;
	e->__ptr_tws[entry_index].time_stamp = 0;
}
inline uint32 env_table_ws_get_virtual_address(struct Env* e,
		uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < __TWS_MAX_SIZE);
	return ROUNDDOWN(e->__ptr_tws[entry_index].virtual_address, PAGE_SIZE*1024);
}
inline uint32 env_table_ws_get_time_stamp(struct Env* e, uint32 entry_index) {
	assert(entry_index >= 0 && entry_index < __TWS_MAX_SIZE);
	return e->__ptr_tws[entry_index].time_stamp;
}
inline uint32 env_table_ws_is_entry_empty(struct Env* e, uint32 entry_index) {
	return e->__ptr_tws[entry_index].empty;
}
void addTableToTableWorkingSet(struct Env *e, uint32 tableAddress) {
	tableAddress = ROUNDDOWN(tableAddress, PAGE_SIZE*1024);
	e->__ptr_tws[e->table_last_WS_index].virtual_address = tableAddress;
	e->__ptr_tws[e->table_last_WS_index].empty = 0;
	e->__ptr_tws[e->table_last_WS_index].time_stamp = 0x00000000;
	e->table_last_WS_index++;
	e->table_last_WS_index %= __TWS_MAX_SIZE;
}
void bufferList_add_page(struct Linked_List* bufferList,
		struct Frame_Info *ptr_frame_info) {
	LIST_INSERT_TAIL(bufferList, ptr_frame_info);
}
void bufferlist_remove_page(struct Linked_List* bufferList,
		struct Frame_Info *ptr_frame_info) {
	LIST_REMOVE(bufferList, ptr_frame_info);
}
inline uint32 pd_is_table_used(struct Env* ptr_env, uint32 virtual_address) {
	return ((ptr_env->env_page_directory[PDX(virtual_address)] & PERM_USED)
			== PERM_USED ? 1 : 0);
}
inline void pd_set_table_unused(struct Env* ptr_env, uint32 virtual_address) {
	ptr_env->env_page_directory[PDX(virtual_address)] &= (~PERM_USED);
	tlb_invalidate((void *) NULL, (void *) virtual_address);
}
inline void pd_clear_page_dir_entry(struct Env* ptr_env, uint32 virtual_address) {
	uint32 * ptr_pgdir = ptr_env->env_page_directory;
	ptr_pgdir[PDX(virtual_address)] = 0;
	tlbflush();
}
extern int __pf_write_env_table(struct Env* ptr_env, uint32 virtual_address,
		uint32* tableKVirtualAddress);
extern int __pf_read_env_table(struct Env* ptr_env, uint32 virtual_address,
		uint32* tableKVirtualAddress);
inline void pt_set_page_permissions(struct Env* ptr_env, uint32 virtual_address,
		uint32 permissions_to_set, uint32 permissions_to_clear) {
	uint32 * ptr_pgdir = ptr_env->env_page_directory;
	uint32* ptr_page_table;
	uint32 page_directory_entry = ptr_pgdir[PDX(virtual_address)];
	if ((page_directory_entry & PERM_PRESENT) == PERM_PRESENT) {
		if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
			ptr_page_table = (uint32*) kheap_virtual_address(
					EXTRACT_ADDRESS(page_directory_entry));
		} else {
			ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
					EXTRACT_ADDRESS(page_directory_entry));
		}
		ptr_page_table[PTX(virtual_address)] |= (permissions_to_set);
		ptr_page_table[PTX(virtual_address)] &= (~permissions_to_clear);
	} else if (page_directory_entry != 0)
			{
		int success = __pf_read_env_table(ptr_env, virtual_address,
				(void*) ptr_temp_page);
		ptr_page_table = (uint32*) ptr_temp_page;
		if (success == E_TABLE_NOT_EXIST_IN_PF)
			panic(
					"pt_set_page_permissions: table not found in PF when expected to find one !. please revise your table fault\
			handling code");
		ptr_page_table[PTX(virtual_address)] |= (permissions_to_set);
		ptr_page_table[PTX(virtual_address)] &= (~permissions_to_clear);
		__pf_write_env_table(ptr_env, virtual_address, (void*) ptr_temp_page);
	} else {
		panic(
				"function pt_set_page_permissions() called with invalid virtual address. The corresponding page table doesn't exist\n");
	}
	tlb_invalidate((void *) NULL, (void *) virtual_address);
}
inline void pt_clear_page_table_entry(struct Env* ptr_env,
		uint32 virtual_address) {
	uint32 * ptr_pgdir = ptr_env->env_page_directory;
	uint32* ptr_page_table;
	uint32 page_directory_entry = ptr_pgdir[PDX(virtual_address)];
	if ((page_directory_entry & PERM_PRESENT) == PERM_PRESENT) {
		if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
			ptr_page_table = (uint32*) kheap_virtual_address(
					EXTRACT_ADDRESS(page_directory_entry));
		} else {
			ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
					EXTRACT_ADDRESS(page_directory_entry));
		}
		ptr_page_table[PTX(virtual_address)] = 0;
	} else if (page_directory_entry != 0)
			{
		int success = __pf_read_env_table(ptr_env, virtual_address,
				(void*) ptr_temp_page);
		ptr_page_table = (uint32*) ptr_temp_page;
		if (success == E_TABLE_NOT_EXIST_IN_PF)
			panic(
					"pt_clear_page_table_entry: table not found in PF when expected to find one !. please revise your table fault\
			handling code");
		ptr_page_table[PTX(virtual_address)] = 0;
		__pf_write_env_table(ptr_env, virtual_address, (void*) ptr_temp_page);
	} else
		panic(
				"function pt_clear_page_table_entry() called with invalid virtual address. The corresponding page table doesn't exist\n");
	tlb_invalidate((void *) NULL, (void *) virtual_address);
}
inline uint32 pt_get_page_permissions(struct Env* ptr_env,
		uint32 virtual_address) {
	uint32 * ptr_pgdir = ptr_env->env_page_directory;
	uint32* ptr_page_table;
	uint32 page_directory_entry = ptr_pgdir[PDX(virtual_address)];
	if ((page_directory_entry & PERM_PRESENT) == PERM_PRESENT) {
		if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address)) {
			ptr_page_table = (uint32*) kheap_virtual_address(
					EXTRACT_ADDRESS(page_directory_entry));
		} else {
			ptr_page_table = STATIC_KERNEL_VIRTUAL_ADDRESS(
					EXTRACT_ADDRESS(page_directory_entry));
		}
	} else if (page_directory_entry != 0)
			{
		int success = __pf_read_env_table(ptr_env, virtual_address,
				(void*) ptr_temp_page);
		ptr_page_table = (uint32*) ptr_temp_page;
		if (success == E_TABLE_NOT_EXIST_IN_PF)
			panic(
					"pt_get_page_permissions: table not found in PF when expected to find one !. please revise your table fault\
			handling code");
	} else
		return 0;
	return (ptr_page_table[PTX(virtual_address)] & 0x00000FFF);
}
inline uint32* create_frames_storage() {
	uint32* frames_storage = (void *) kmalloc(PAGE_SIZE);
	if (frames_storage == NULL) {
		panic("NOT ENOUGH KERNEL HEAP SPACE");
	}
	return frames_storage;
}
inline void add_frame_to_storage(uint32* frames_storage,
		struct Frame_Info* ptr_frame_info, uint32 index) {
	uint32 va = index * PAGE_SIZE;
	uint32 *ptr_page_table;
	int r = get_page_table(frames_storage, (void*) va, &ptr_page_table);
	if (r == TABLE_NOT_EXIST) {
		if (USE_KHEAP) {
			ptr_page_table = create_page_table(frames_storage, (uint32) va);
		} else {
			__static_cpt(frames_storage, (uint32) va, &ptr_page_table);
		}
	}
	ptr_page_table[PTX(va)] = CONSTRUCT_ENTRY(
			to_physical_address(ptr_frame_info), 0 | PERM_PRESENT);
}
inline struct Frame_Info* get_frame_from_storage(uint32* frames_storage,
		uint32 index) {
	struct Frame_Info* ptr_frame_info;
	uint32 *ptr_page_table;
	uint32 va = index * PAGE_SIZE;
	ptr_frame_info = get_frame_info(frames_storage, (void*) va,
			&ptr_page_table);
	return ptr_frame_info;
}
inline void clear_frames_storage(uint32* frames_storage) {
	int fourMega = 1024 * PAGE_SIZE;
	int i;
	for (i = 0; i < 1024; i++) {
		if (frames_storage[i] != 0) {
			if (USE_KHEAP) {
				kfree(
						(void*) kheap_virtual_address(
								EXTRACT_ADDRESS(frames_storage[i])));
			} else {
				free_frame(to_frame_info(EXTRACT_ADDRESS(frames_storage[i])));
			}
			frames_storage[i] = 0;
		}
	}
}
void setUHeapPlacementStrategyFIRSTFIT() {
	_UHeapPlacementStrategy = UHP_PLACE_FIRSTFIT;
}
void setUHeapPlacementStrategyBESTFIT() {
	_UHeapPlacementStrategy = UHP_PLACE_BESTFIT;
}
void setUHeapPlacementStrategyNEXTFIT() {
	_UHeapPlacementStrategy = UHP_PLACE_NEXTFIT;
}
void setUHeapPlacementStrategyWORSTFIT() {
	_UHeapPlacementStrategy = UHP_PLACE_WORSTFIT;
}
uint32 isUHeapPlacementStrategyFIRSTFIT() {
	if (_UHeapPlacementStrategy == UHP_PLACE_FIRSTFIT)
		return 1;
	return 0;
}
uint32 isUHeapPlacementStrategyBESTFIT() {
	if (_UHeapPlacementStrategy == UHP_PLACE_BESTFIT)
		return 1;
	return 0;
}
uint32 isUHeapPlacementStrategyNEXTFIT() {
	if (_UHeapPlacementStrategy == UHP_PLACE_NEXTFIT)
		return 1;
	return 0;
}
uint32 isUHeapPlacementStrategyWORSTFIT() {
	if (_UHeapPlacementStrategy == UHP_PLACE_WORSTFIT)
		return 1;
	return 0;
}
void setKHeapPlacementStrategyCONTALLOC() {
	_KHeapPlacementStrategy = KHP_PLACE_CONTALLOC;
}
void setKHeapPlacementStrategyFIRSTFIT() {
	_KHeapPlacementStrategy = KHP_PLACE_FIRSTFIT;
}
void setKHeapPlacementStrategyBESTFIT() {
	_KHeapPlacementStrategy = KHP_PLACE_BESTFIT;
}
void setKHeapPlacementStrategyNEXTFIT() {
	_KHeapPlacementStrategy = KHP_PLACE_NEXTFIT;
}
void setKHeapPlacementStrategyWORSTFIT() {
	_KHeapPlacementStrategy = KHP_PLACE_WORSTFIT;
}
uint32 isKHeapPlacementStrategyCONTALLOC() {
	if (_KHeapPlacementStrategy == KHP_PLACE_CONTALLOC)
		return 1;
	return 0;
}
uint32 isKHeapPlacementStrategyFIRSTFIT() {
	if (_KHeapPlacementStrategy == KHP_PLACE_FIRSTFIT)
		return 1;
	return 0;
}
uint32 isKHeapPlacementStrategyBESTFIT() {
	if (_KHeapPlacementStrategy == KHP_PLACE_BESTFIT)
		return 1;
	return 0;
}
uint32 isKHeapPlacementStrategyNEXTFIT() {
	if (_KHeapPlacementStrategy == KHP_PLACE_NEXTFIT)
		return 1;
	return 0;
}
uint32 isKHeapPlacementStrategyWORSTFIT() {
	if (_KHeapPlacementStrategy == KHP_PLACE_WORSTFIT)
		return 1;
	return 0;
}
