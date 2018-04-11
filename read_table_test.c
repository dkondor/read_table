/*
 * read_table_test.c -- simple test cases for read_table.h functionality
 * 
 * only a few "manual" test cases; input is read from stdin or the given file
 * separate test for the C-only interface
 * 
 * Copyright 2018 Daniel Kondor <kondor.dani@gmail.com>
 * 
 */


#include <stdio.h>
#include "read_table.h"


uint32_t min1 = 1234;
uint32_t max1 = 1234567890;
int16_t min2 = -3000;
int16_t max2 = 4000;

/* note: hand-coded different test cases for different formats */
/* 1. unsigned integer between [1,1000], coordinates */
void test1(read_table* rt) {
	while(read_table_line(rt) == 0) {
		unsigned int x;
		double y1;
		double y2;
		if( read_table_uint32_limits(rt,&x,1U,100U) ||
			read_table_double_limits(rt,&y1,-180.0,180.0) ||
			read_table_double_limits(rt,&y2,-90.0,90.0) )
				read_table_write_error(rt,stderr);
		else fprintf(stdout,"Read: %u\t%g\t%g\n",x,y1,y2);
	}
}

/* 2. signed integer, skip, uint64_t, skip, uint16_t, double */
void test2(read_table* rt) {
	while(read_table_line(rt) == 0) {
		int x; uint64_t y; uint16_t z; double d;
		if( read_table_int32(rt,&x) || read_table_skip(rt) ||
			read_table_uint64(rt,&y) || read_table_skip(rt) ||
			read_table_uint16(rt,&z) || read_table_double(rt,&d) )
				read_table_write_error(rt,stderr);
		else fprintf(stdout,"Read: %d\t%lu\t%hu\t%f\n",x,y,z,d);
	}
}

/* 3. uint32_t, double, skip, skip, int16_t */
void test3(read_table* rt) {
	while(read_table_line(rt) == 0) {
		uint32_t x; int16_t y; double d;
		if( read_table_uint32_limits(rt,&x,min1,max1) ||
			read_table_double_limits(rt,&d,1e-10,123.0) || read_table_skip(rt) ||
			read_table_skip(rt) || read_table_int16_limits(rt,&y,min2,max2) )
				read_table_write_error(rt,stderr);
		else fprintf(stdout,"Read: %u\t%hd\t%f\n",x,y,d);
	}
}


void (*func[])(read_table*) = { test1, test2, test3 };


int main(int argc, char **argv)
{
	int testcase = 0;
	char* fn = 0;
	int i;
	for(i=1;i<argc;i++) if(argv[i][0] == '-') switch(argv[i][1]) {
		case 'i':
			fn = argv[i+1];
			break;
		case 'b':
			if(argv[i][2] == '1') {
				uint32_t min10 = strtoul(argv[i+1],0,10);
				uint32_t max10 = strtoul(argv[i+2],0,10);
				if(min10 < max10) {
					min1 = min10;
					max1 = max10;
					i+=2;
				}
				break;
			}
			if(argv[i][2] == '2') {
				int16_t min20 = atoi(argv[i+1]);
				int16_t max20 = atoi(argv[i+2]);
				if(min20 < max20) {
					min2 = min20;
					max2 = max20;
					i+=2;
				}
				break;
			}
		default:
			if(isdigit(argv[i][1])) {
				testcase = atoi(argv[i]+1);
				if(testcase >= 3) testcase = 0;
				break;
			}
			fprintf(stderr,"Unknown parameter: %s!\n",argv[i]);
			break;
	}
	
	read_table* rt;
	if(fn) rt = read_table_new_fn(fn);
	else rt = read_table_new(stdin);
	if(!rt) {
		fprintf(stderr,"Error opening input!\n");
		return 1;
	}
	
	func[testcase](rt);
	read_table_free(rt);
	
	return 0;
}

