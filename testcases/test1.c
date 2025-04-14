#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <assert.h>

#include "../common.h"
#include "helper.h"

/* 
 * Description:
 * This test checks if allocation(s) to a previously-mapped page is correctly
 * handled by the kernel module.
 */

int main(void);

bool test_allocate(void){
    struct alloc_info* allocCall;    
    allocCall = malloc(sizeof(struct alloc_info));
    /* 
        >> 12 << 12 shifts the address of main 12 bits to the right and then back 12 bits,
        effectively zero-ing out the lower 12 bits by dividing by 4096 then multiplying by
        4096
        this aligns the vaddr on the 4 KB page boundary
        means allocCall->vaddr is now the start of the page containing the main function
        in memory
        because it's already allocated to main, it shouldn't succeed
    */
    allocCall->vaddr                = (unsigned long) &main >> 12 << 12;
    allocCall->num_pages            = 1;
    allocCall->write                = true;
    if (ioctl(devfd, ALLOCATE, allocCall) != -1) {
        printf ("Failed at %p\n", &main);
    	return false;
    }

    int iter=10;
    char *ptr;
    while (iter>0) {
	ptr = mmap ( NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0 );
	if (ptr == MAP_FAILED) {
		// Should not happen, just added for sanity
		printf("Something went terribly wrong! (Re-RUN?)\n");
		assert(false);
	}
	// must access for on-demand paging
	*ptr = 'a';

	printf("Testing mapped page %p\n", ptr);
    	allocCall->vaddr                = (unsigned long) ptr;
    	allocCall->num_pages            = 1;
    	allocCall->write                = true;
    	if (ioctl(devfd, ALLOCATE, allocCall) != -1) {
        	printf ("Failed at %p\n", ptr);
		return false;
    	}
	iter--;
    }

    // unmap and now allocate
    munmap(ptr, 0x1000);	
    printf("Testing unmapped page %p\n", ptr);
    allocCall->vaddr                = (unsigned long) ptr;
    allocCall->num_pages            = 1;
    allocCall->write                = true;
    if (ioctl(devfd, ALLOCATE, allocCall) == -1) {
        printf ("Failed at %p\n", ptr);
	return false;
    }

    return true;
}

int main(void)
{
    printf("Executing: TEST1\n");

    if (!open_device_driver()) return -1;
    
    if(test_allocate()) {
    	printf("Passed: TEST1\n");
        return -1;
    }

    printf("Failed: TEST1\n");
    close(devfd);
    return 0;
}
