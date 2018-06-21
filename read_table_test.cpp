/*
 * read_table_test.cpp -- simple test cases for read_table.h functionality
 * 
 * only a few "manual" test cases; input is read from stdin or the given file
 * 
 * Copyright 2018 Daniel Kondor <kondor.dani@gmail.com>
 * 
 */


#include <stdio.h>

#ifdef USE_CPP
/* test the C++ only version (requires C++ iostream) */
#include <iostream>
#include "read_table_cpp.h"
#define input_stream std::cin
#define err_stream std::cerr
#else
#include "read_table.h"
#define input_stream stdin
#define err_stream stderr
#endif


uint32_t min1 = 1234;
uint32_t max1 = 1234567890;
int16_t min2 = -3000;
int16_t max2 = 4000;

/* note: hand-coded different test cases for different formats */
/* 1. unsigned integer between [1,1000], coordinates */
void test1(read_table2&& rt) {
	while(rt.read_line()) {
		unsigned int x;
		std::pair<double,double> y;
		if( !rt.read(read_bounds(x,1U,100U),read_bounds_coords(y)) ) rt.write_error(err_stream);
		else fprintf(stdout,"Read: %u\t%g\t%g\n",x,y.first,y.second);
	}
}

/* 2. signed integer, skip, uint64_t, skip, uint16_t, double */
void test2(read_table2&& rt) {
	while(rt.read_line()) {
		int x; uint64_t y; uint16_t z; double d;
		if( !rt.read( x, read_table_skip(), y, read_table_skip(), z, d ) ) rt.write_error(err_stream);
		else fprintf(stdout,"Read: %d\t%lu\t%hu\t%f\n",x,y,z,d);
	}
}

/* 3. uint32_t, double, skip, skip, int16_t */
void test3(read_table2&& rt) {
	while(rt.read_line()) {
		uint32_t x; int16_t y; double d;
		if( !rt.read( read_bounds(x,min1,max1), read_bounds(d, 1e-10, 123.0), read_table_skip(), read_bounds(y,min2,max2) ) ) rt.write_error(err_stream);
		else fprintf(stdout,"Read: %u\t%hd\t%f\n",x,y,d);
	}
}

/* 4. uint32_t, double, string, int16_t */
void test4(read_table2&& rt) {
	while(rt.read_line()) {
		uint32_t x; int16_t y; double d;
#if __cplusplus >= 201703L
		std::string_view str;
#else
		string_view_custom str;
#endif
		if( !rt.read( read_bounds(x,min1,max1), d, str, read_bounds(y,min2,max2) ) ) rt.write_error(err_stream);
		else fprintf(stdout,"Read: %u\t%f\t%hd\t%.*s\n",x,d,y,(int)str.length(),str.data());
	}
}

void (*func[])(read_table2&&) = { test1, test2, test3, test4 };


int main(int argc, char **argv)
{
	int testcase = 0;
	char* fn = 0;
	char delim = 0;
	char comment = 0;
	for(int i=1;i<argc;i++) if(argv[i][0] == '-') switch(argv[i][1]) {
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
			fprintf(stderr,"Unknown parameter: %s!\n",argv[i]);
			break;
		case 'd':
			delim = argv[i+1][0];
			i++;
			break;
		case 'c':
			comment = argv[i+1][0];
			i++;
			break;
		default:
			if(isdigit(argv[i][1])) {
				testcase = atoi(argv[i]+1);
				if(testcase >= 4) testcase = 0;
				break;
			}
			fprintf(stderr,"Unknown parameter: %s!\n",argv[i]);
			break;
	}
	
	read_table2 rt(fn,input_stream);
	if(delim) rt.set_delim(delim);
	if(comment) rt.set_comment(comment);
	func[testcase](std::move(rt));
	
	return 0;
}

