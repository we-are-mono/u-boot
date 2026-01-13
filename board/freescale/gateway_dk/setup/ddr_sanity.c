/*
 * Quick verification of DDR banks
 */

#include <config.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int test_ddr_sanity(void)
{
	volatile uint32_t *addr;
	uint32_t test_val;
	uint32_t read_val;
	int errors = 0;
	int i;
	
	printf("DDR4 Memory         : ");
	
	/* Test Bank 0: start, middle, near end (safe from u-boot) */
	u64 bank0_locations[] = {
		0x80000000ULL,    /* Start */
		0xB0000000ULL,    /* Middle (~768MB in) */
		0xF0000000ULL     /* Near end (safe distance from u-boot) */
	};
	
	/* Test Bank 1: start, middle, end */
	u64 bank1_locations[] = {
		0x880000000ULL,   /* Start */
		0x900000000ULL,   /* Middle (~3GB in) */
		0x9FFFF0000ULL    /* Near end */
	};
	
	/* Test Bank 0 */
	for (i = 0; i < 3; i++) {
		addr = (volatile uint32_t *)bank0_locations[i];
		test_val = 0xDEADBEEF ^ (i << 16);
		
		*addr = test_val;
		read_val = *addr;
		
		if (read_val != test_val) {
			errors++;
		}
	}
	
	/* Test Bank 1 */
	for (i = 0; i < 3; i++) {
		addr = (volatile uint32_t *)bank1_locations[i];
		test_val = 0xCAFEBABE ^ (i << 16);
		
		*addr = test_val;
		read_val = *addr;
		
		if (read_val != test_val) {
			errors++;
		}
	}
	
	/* Print result to match your other test format */
	if (errors == 0) {
		printf("PASS (Bank0: %llu MB, Bank1: %llu MB)\n",
		       gd->bd->bi_dram[0].size / (1024 * 1024),
		       gd->bd->bi_dram[1].size / (1024 * 1024));
		return 0;
	} else {
		printf("FAIL (%d errors)\n", errors);
		return -1;
	}
}