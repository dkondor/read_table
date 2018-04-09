/*
 * read_table_cpp.h -- simple and robust general methods for reading numeric data
 * 	from text files, e.g. TSV or CSV
 * 
 * simple: should be usable in a few lines of code
 * robust: try to detect and signal errors (in format, overflow, underflow etc.)
 * 	especially considering cases that would be silently ignored with
 * 	scanf / iostreams or similar
 * 
 * C++-only version, which does not require the POSIX getline() function,
 * uses std::getline() from the C++ standard library which should be available
 * on all platforms
 * 
 * note that this requires C++11
 * 
 * Copyright 2018 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 * example usage in C++

istream f; ... // open input file or use stdin
read_table2 r(f);
while(r.read_line()) {
	int32_t id1,id2;
	double d1;
	uint64_t id3;
	if(!r.read(id1,d1,id3,id2)) break; // false return value indicates error
	... // do something with the values read
}
if(r.get_last_error() != T_EOF) { // handle error
	std::cerr<<"Error reading input:\n";
	r.write_error(std::cerr);
}

 */

#ifndef _READ_TABLE_H
#define _READ_TABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <utility>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include <string>

#include <cmath>
static inline bool _isnan(double x) { return std::isnan(x); }
static inline bool _isinf(double x) { return std::isinf(x); }


/* possible error codes */
enum read_table_errors {T_OK = 0, T_EOF = 1, T_EOL, T_MISSING, T_FORMAT,
	T_OVERFLOW, T_NAN, T_TYPE, T_COPIED, T_ERROR_FOPEN, T_READ_ERROR};
static const char * const error_desc[] = {"No error", "End of file", "Unexpected end of line",
		"Missing value", "Invalid value", "Overflow or underflow", "NaN or infinity read",
		"Unknown conversion requested", "Invalidated instance", "Error opening file",
		"Error reading input"};

/* convert error code to string description */
static const char* get_error_desc(enum read_table_errors err) {
	const static char unkn[] = "Unknown error";
	switch(err) {
		case T_OK:
			return error_desc[0];
		case T_EOF:
			return error_desc[1];
		case T_EOL:
			return error_desc[2];
		case T_MISSING:
			return error_desc[3];
		case T_FORMAT:
			return error_desc[4];
		case T_OVERFLOW:
			return error_desc[5];
		case T_NAN:
			return error_desc[6];
		case T_TYPE:
			return error_desc[7];
		case T_COPIED:
			return error_desc[8];
		case T_ERROR_FOPEN:
			return error_desc[9];
		case T_READ_ERROR:
			return error_desc[10];
		default:
			return unkn;
	}
}


/* helper classes to be given as parameters to read_table2::read_next() and
 * read_table2::read() */
 
 
/* dummy struct to be able to call the same interface to skip data */
struct read_table_skip_t { };
static const read_table_skip_t _read_table_skip1;
static const read_table_skip_t& read_table_skip() { return _read_table_skip1; }

/* struct to represent values with bounds */
template<class T>
struct read_bounds_t {
	read_bounds_t(T& val_, T min_, T max_):val(val_),min(min_),max(max_) { }
	T& val;
	T min;
	T max;
};
template<class T> read_bounds_t<T> read_bounds(T& val_, T min_, T max_) {
	return read_bounds_t<T>(val_,min_,max_);
}
/* shortcut to read coordinate pairs in the "obvious" format, i.e. the first
 * value should be between -180.0 and 180.0, the second value should be
 * between -90.0 and 90.0
 * -- note: this is the format that is obvious to me, different use cases
 * might have the coordinates in different order or the range of longitudes
 * could be 0 to 360 or even unbounded -- feel free to modify the bounds :) */
read_bounds_t<std::pair<double,double> > read_bounds_coords(std::pair<double,double>& coords) {
	return read_bounds_t<std::pair<double,double> >(coords,
		std::make_pair(-180.0,-90.0),std::make_pair(180.0,90.0));
}



/* main class containing main parameters for processing text */
struct read_table2 {
	protected:
		std::istream* is; /* input stream -- note: only a pointer is stored, the caller either supplies an input stream or a
			file name; in the former case, the original object should not go out of scope while this struct is used */
		std::ifstream* fs; /* file stream if it is opened by us */
		std::string buf; /* buffer to hold the current line */
		const char* fn; /* file name, stored optionally for error output */
		uint64_t line; /* current line (count starts from 1) */
		size_t pos; /* current position in line */
		size_t col; /* current field (column) */
		int base; /* base for integer conversions */
		enum read_table_errors last_error; /* error code of the last operation */
		char delim; /* delimiter to use; 0 means any blank (space or tab) note: cannot be newline */
		char comment; /* character to indicate comments; 0 means none */
		bool allow_nan_inf; /* further flags: whether reading a NaN or INF for double values is considered and error */
		read_table2() = delete; /* user should supply either an input stream or a filename to use */
		read_table2(const read_table2& r) = delete; /* disallow copying, only moving is possible */
		/* helper function for the constructors to set default values */
		void read_table_init();
	public:
		
		/* 1. constructors -- need to give a file name or an already open input stream */
		
		/* constructor taking a file name; the given file is opened for reading
		 * and closed later in the destructor */
		read_table2(const char* fn_);
		/* constructor taking a reference to a stream -- it is NOT copied, i.e.
		 * the original instance of the stream need to be kept by the caller;
		 * also, the stream is not closed in the destructor in this case */
		read_table2(std::istream& is_) : is(&is_),fs(0) { read_table_init(); }
		read_table2(read_table2&& r);
		~read_table2();
		
		
		/* 2. read one line into the internal buffer
		 * 	the 'skip' parameter controls whether empty lines are skipped */
		bool read_line(bool skip = true);
		
		
		/* 3. main interface for parsing data; this uses templates and has 
		 * specializations for all data types supported:
		 * 16, 32 and 64-bit signed and unsigned integers, doubles,
		 * pairs of doubles and special "types":
		 * 	- read_table_skip_t for skipping values
		 * 	- read_bounds_t for specifying minimum and maximum value for the input
		 * see below for more explanation */
		
		/* try to parse one value from the currently read line */
		template<class T> bool read_next(T& val);
		/* overload of the previous for reading values with bounds */
		template<class T> bool read_next(read_bounds_t<T> val);
		/* try to parse whole line (read previously with read_line()),
		 * into the given list of parameters */
		bool read() { return true; }
		template<class first, class ...rest>
		bool read(first&& val, rest&&... vals);
		
		
		/* 4. functions for setting parameters */
		/* set delimiter character (default is spaces and tabs) */
		void set_delim(char delim_) { delim = delim_; }
		/* get delimiter character (default is spaces and tabs) */
		char get_delim() const { return delim; }
		/* set comment character (default is none) */
		void set_comment(char comment_) { comment = comment_; }
		/* get comment character (default is none) */
		char get_comment() const { return comment; }
		
		/* get last error code */
		enum read_table_errors get_last_error() const { return last_error; }
		const char* get_last_error_str() const { return get_error_desc(last_error); }
		
		/* get current position in the file */
		uint64_t get_line() const { return line; }
		size_t get_pos() const { return pos; }
		size_t get_col() const { return col; }
		/* set filename (for better formatting of diagnostic messages) */
		void set_fn_for_diag(const char* fn_) { fn = fn; }
		/* get current line string */
		const char* get_line_c_str() const { return buf.c_str(); }
		const std::string& get_line_str() const { return buf; }
		
		/* write formatted error message to the given stream */
		void write_error(std::ostream& f) const;
		
		
		/* 5. non-templated functions for reading specific data types and values */
		/* skip next field, ignoring any content */
		bool read_skip();
		/* read one 32-bit signed integer in the given limits */
		bool read_int32_limits(int32_t& i, int32_t min, int32_t max);
		bool read_int32(int32_t& i) { return read_int32_limits(i,INT32_MIN,INT32_MAX); }
		/* read one 32-bit unsigned integer in the given limits */
		bool read_uint32_limits(uint32_t& i, uint32_t min, uint32_t max);
		bool read_uint32(uint32_t& i) { return read_uint32_limits(i,0,UINT32_MAX); }
		/* read one 64-bit signed integer in the given limits */
		bool read_int64_limits(int64_t& i, int64_t min, int64_t max);
		bool read_int64(int64_t& i) { return read_int64_limits(i,INT64_MIN,INT64_MAX); }
		/* read one 64-bit unsigned integer in the given limits */
		bool read_uint64_limits(uint64_t& i, uint64_t min, uint64_t max);
		bool read_uint64(uint64_t& i) { return read_uint64_limits(i,0,UINT64_MAX); }
		/* read one 16-bit signed integer in the given limits */
		bool read_int16_limits(int16_t& i, int16_t min, int16_t max);
		bool read_int16(int16_t& i) { return read_int16_limits(i,INT16_MIN,INT16_MAX); }
		/* read one 16-bit unsigned integer in the given limits */
		bool read_uint16_limits(uint16_t& i, uint16_t min, uint16_t max);
		bool read_uint16(uint16_t& i) { return read_uint16_limits(i,0,UINT16_MAX); }
		/* read one double value in the given limits */
		bool read_double_limits(double& d, double min, double max);
		bool read_double(double& d);
	
	protected:
		/* helper functions for the previous */
		bool read_table_pre_check();
		bool read_table_post_check(const char* c2);
};




/* constructor -- allocate new read_table2 struct, fill in the necessary fields */
void read_table2::read_table_init() {
	line = 0;
	pos = 0;
	col = 0;
	last_error = T_OK;
	delim = 0;
	comment = 0;
	fn = 0;
	base = 10;
	allow_nan_inf = true;
}

read_table2::read_table2(const char* fn_) {
	fs = new std::ifstream(fn_);
	if( !fs || !(fs->is_open()) || fs->fail() ) last_error = T_ERROR_FOPEN;
	else fs->exceptions(std::ios_base::goodbit); /* clear exception mask -- no exceptions thrown, error checking done separately */
	is = fs;
	read_table_init();
}

/* destructor -- closes the input stream only if it was opened in the 
 * constructor (i.e. the constructor was called with a filename */
read_table2::~read_table2() {
	if(fs) delete fs;
}

/* move constructor -- moves the stream to the new instance
 * the old instance is invalidated */
read_table2::read_table2(read_table2&& r) {
	/* copy all elements */
	line = r.line;
	pos = r.pos;
	col = r.col;
	last_error = r.last_error;
	delim = r.delim;
	comment = r.comment;
	fn = r.fn;
	base = r.base;
	allow_nan_inf = r.allow_nan_inf;
	fs = r.fs;
	is = r.is;
	r.last_error = T_COPIED;
	r.fs = 0;
}

/* read a new line (discarding any remaining data in the current line)
 * returns true if a line was read, false on failure
 * note that failure can mean end of file, which should be checked separately
 * if skip == 1, empty lines are skipped (i.e. reading continues until a
 * nonempty line is found); otherwise, empty lines are read and stored as well,
 * which will probably result in errors if data is tried to be parsed from it */
bool read_table2::read_line(bool skip) {
	if(last_error == T_EOF || last_error == T_COPIED ||
		last_error == T_ERROR_FOPEN) return false;
	if(is->eof()) { last_error = T_EOF; return false; }
	while(1) {
		std::getline(*is,buf);
		if(is->eof()) { last_error = T_EOF; return false; }
		if(is->fail()) { last_error = T_READ_ERROR; return false; }
		size_t len = buf.size();
		line++; 
		pos = 0;
		/* check that there is actual data in the line, empty lines are skipped */
		if(skip) {
			for(; pos < len; pos++)
				if( ! (buf[pos] == ' ' || buf[pos] == '\t') ) break;
			if(comment) if(buf[pos] == comment) continue; /* check for comment character first */
			if(pos < len) break; /* there is some data in the line */
		}
		else break; /* if empty lines should not be skipped */
	}
	col = 0; /* reset the counter for columns */
	last_error = T_OK;
	return true;
}

/* checks to be performed before trying to convert a field */
bool read_table2::read_table_pre_check() {
	if(last_error == T_EOF || last_error == T_EOL ||
		last_error == T_READ_ERROR || last_error == T_ERROR_FOPEN) return false;
	/* 1. skip any blanks */
	size_t len = buf.size();
	for(;pos < len; pos++)
		if( ! (buf[pos] == ' ' || buf[pos] == '\t') ) break;
	/* 2. check for end of line or comment */
	if(pos == len || buf[pos] == '\n' || (comment && buf[pos] == comment) ) {
		last_error = T_EOL;
		return false;
	}
	/* 3. check for field delimiter (if we have any) */
	if(delim && buf[pos] == delim) {
		last_error = T_MISSING;
		return false;
	}
	return true;
}

/* perform checks needed after number conversion */
bool read_table2::read_table_post_check(const char* c2) {
	/* 0. check for format errors and overflow as indicated by strto* */
	if(errno == EINVAL || c2 == buf.c_str() + pos) {
		last_error = T_FORMAT;
		return false;
	}
	if(errno == ERANGE) {
		last_error = T_OVERFLOW;
		return false;
	}
	/* 1. skip the converted number and any blanks */
	bool have_blank = false;
	size_t len = buf.size();
	for(pos = c2 - buf.c_str();pos<len;pos++)
		if( ! (buf[pos] == ' ' || buf[pos] == '\t') ) break;
		else have_blank = true;
	last_error = T_OK;
	/* 2. check for end of line -- this is not a problem here */
	if(pos == len || buf[pos] == '\n' ||
		(comment && buf[pos] == comment) ) return true;
	if(delim == 0 && have_blank == false) {
		/* if there is no explicit delimiter, then there need to be at least
		 * one blank after the converted number if it is not the end of line */
		last_error = T_FORMAT;
		return false;
	}
	/* 3. otherwise, check for proper delimiter, if needed */
	if(delim) {
		if(buf[pos] != delim) {
			last_error = T_FORMAT;
			return false;
		}
		pos++; /* in this case, advance position further, past the delimiter */
	}
	col++; /* advance column counter as well */
	return true; /* everything OK */
}


/* skip next field, ignoring any content
 * if we have a delimiter, this means advancing until the next delimiter and
 * 	then one more position
 * if no delimiter, this means skipping any blanks, than any nonblanks and
 * 	ending at the next blank */
bool read_table2::read_skip() {
	size_t len = buf.size();
	if(delim) {
		/* if there is a delimiter, just advance until after the next one */
		for(;pos<len;pos++) if(buf[pos] == delim) break;
		if(pos == len) {
			last_error = T_EOL;
			return false;
		}
		pos++; /* note: we do not care what is after the delimiter */
	}
	else {
		/* no delimiter, skip any blanks, then skip all non-blanks */
		for(;pos<len;pos++)
			if( ! (buf[pos] == ' ' || buf[pos] == '\t') ) break;
		if(pos == len || buf[pos] == '\n' || (comment && buf[pos] == comment)) {
			last_error = T_EOL;
			return false;
		}
		for(;pos<len;pos++) if(buf[pos] == ' ' || buf[pos] == '\t' ||
			buf[pos] == '\n' || (comment && buf[pos] == comment)) break;
		/* we do not care what is after the field, now we are either at a
		* 	blank or line end */
	}
	
	col++;
	last_error = T_OK;
	return true;
}

/* try to convert the next value to integer
 * check explicitely that it is within the limits provided
 * (note: the limits are inclusive, so either min or max is OK)
 * return true on success, false on error */
bool read_table2::read_int32_limits(int32_t& i, int32_t min, int32_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	long res = strtol(buf.c_str() + pos, &c2, base);
	/* check that result fits in the given range -- note: long might be 64-bit */
	if(res > (long)max || res < (long)min) {
		last_error = T_OVERFLOW;
		return false;
	}
	i = res; /* store potential result */
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 64-bit integer
 * return true on success, false on error */
bool read_table2::read_int64_limits(int64_t& i, int64_t min, int64_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	/* note: try to determine if we should use long or long long */
	long res;
	long long res2;
	if(LONG_MAX >= INT64_MAX && LONG_MIN <= INT64_MIN) {
		res = strtol(buf.c_str() + pos, &c2, base);
		if(res > (long)max || res < (long)min) {
			last_error = T_OVERFLOW;
			return false;
		}
		i = (int64_t)res; /* store potential result */
	}
	else {
		res2 = strtoll(buf.c_str() + pos, &c2, base);
		if(res2 > (long long)max || res2 < (long long)min) {
			last_error = T_OVERFLOW;
			return false;
		}
		i = (int64_t)res2; /* store potential result */
	}
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 32-bit unsigned integer
 * return true on success, false on error */
bool read_table2::read_uint32_limits(uint32_t& i, uint32_t min, uint32_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	/* stricly require that the next character is alphanumeric
	 * -- strtoul() will silently accept and negate negative values */
	if( ! (isalnum(buf[pos]) || buf[pos] == '+') ) {
		if(buf[pos] == '-') last_error = T_OVERFLOW;
		else last_error = T_FORMAT;
		return false;
	}
	unsigned long res = strtoul(buf.c_str() + pos, &c2, base);
	/* check that result fits in bounds -- long might be 64-bit */
	if(res > (unsigned long)max || res < (unsigned int)min) {
		last_error = T_OVERFLOW;
		return false;
	}
	i = res; /* store potential result */
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 64-bit unsigned integer
 * return true on success, false on error */
bool read_table2::read_uint64_limits(uint64_t& i, uint64_t min, uint64_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	/* stricly require that the next character is alphanumeric
	 * -- strtoul() will silently accept and negate negative values */
	if( ! (isalnum(buf[pos]) || buf[pos] == '+') ) {
		if(buf[pos] == '-') last_error = T_OVERFLOW;
		else last_error = T_FORMAT;
		return false;
	}
	/* note: try to determine if to use long or long long */
	unsigned long res;
	unsigned long long res2;
	if(ULONG_MAX >= UINT64_MAX) {
		res = strtoul(buf.c_str() + pos, &c2, base);
		/* note: this check might be unnecessary */
		if(res > (unsigned long)max || res < (unsigned long)min) {
			last_error = T_OVERFLOW;
			return false;
		}
		i = res; /* store potential result */
	}
	else {
		res2 = strtoull(buf.c_str() + pos, &c2, base);
		if(res2 > (unsigned long long)max || res < (unsigned long long)min) {
			last_error = T_OVERFLOW;
			return false;
		}
		i = res2; /* store potential result */
	}
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to a 16-bit signed integer
 * return true on success, false on error
 * note: this uses the previous functions as there is no separate library
 * function for 16-bit integers anyway */
bool read_table2::read_int16_limits(int16_t& i, int16_t min, int16_t max) {
	/* just use the previous function and check for overflow */
	int32_t i2;
	/* note: the following function already check for overflow as well */
	if(!read_int32_limits(i2,(int32_t)min,(int32_t)max)) return false;
	i = i2;
	return true;
}

/* try to convert the next value to a 16-bit unsigned integer
 * return true on success, false on error */
bool read_table2::read_uint16_limits(uint16_t& i, uint16_t min, uint16_t max) {
	/* just use the previous function and check for overflow */
	uint32_t i2;
	if(!read_uint32_limits(i2,(uint32_t)min,(uint32_t)max)) return false;
	i = i2;
	return true;
}

/* try to convert the next value to a double precision float value
 * return true on success, false on error */
bool read_table2::read_double(double& d) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	d = strtod(buf.c_str() + pos, &c2);
	/* advance position after the number, check if there is proper field separator */
	if(!read_table_post_check(c2)) return false;
	if(allow_nan_inf == false) {
		if(_isnan(d) || _isinf(d)) {
			last_error = T_NAN;
			return false;
		}
	}
	return true;
}
bool read_table2::read_double_limits(double& d, double min, double max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	d = strtod(buf.c_str() + pos, &c2);
	if(!read_table_post_check(c2)) return false;
	if(_isnan(d)) {
		last_error = T_NAN;
		return false;
	}
	
	/* note: this will not be true if min or max is NaN, not sure if
	 * that's a problem */
	if(d > max || d < min) {
		last_error = T_OVERFLOW;
		return false;
	}
	return true;
}



/* write formatted error message to the given stream */
void read_table2::write_error(std::ostream& f) const {
	f<<"read_table, ";
	if(fn) f<<"file "<<fn<<", ";
	else f<<"input ";
	f<<"line "<<line<<", position "<<pos<<" / column "<<col<<": "<<get_error_desc(last_error)<<"\n";
}



/* templated functions, template specializations to automatically handle
 * reading the proper types */


/* template specializations to use the same function name */
template<> bool read_table2::read_next(int32_t& val) { return read_int32(val); }
template<> bool read_table2::read_next(uint32_t& val) { return read_uint32(val); }
template<> bool read_table2::read_next(int16_t& val) { return read_int16(val); }
template<> bool read_table2::read_next(uint16_t& val) { return read_uint16(val); }
template<> bool read_table2::read_next(int64_t& val) { return read_int64(val); }
template<> bool read_table2::read_next(uint64_t& val) { return read_uint64(val); }
template<> bool read_table2::read_next(double& val) { return read_double(val); }
template<> bool read_table2::read_next(std::pair<double,double>& p) {
	double x,y;
	if( !( read_double(x) && read_double(y) ) ) return false;
	p = std::pair<double,double>(x,y);
	return true;
}
/* dummy struct to be able to call the same interface to skip data
 * (useful if used with the variadic template below) */
template<> bool read_table2::read_next(const read_table_skip_t& skip) { return read_skip(); }
//~ template<> bool read_table2::read_next(read_table_skip_t skip) { return read_skip(); }


/* overloads for reading with bounds
 * example usage:
read_table2 r(...);
uint32_t x;
r.read_next(read_bounds(x,1000U,2000U));
*/
template<> bool read_table2::read_next(read_bounds_t<int32_t> b) {
	return read_int32_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<uint32_t> b) {
	return read_uint32_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<int64_t> b) {
	return read_int64_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<uint64_t> b) {
	return read_uint64_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<int16_t> b) {
	return read_int16_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<uint16_t> b) {
	return read_uint16_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<double> b) {
	return read_double_limits(b.val,b.min,b.max);
}
template<> bool read_table2::read_next(read_bounds_t<std::pair<double,double> > b) {
	double x,y;
	if( !(read_double_limits(x,b.min.first,b.max.first) &&
		read_double_limits(y,b.min.second,b.max.second) ) ) return false;
	b.val = std::make_pair(x,y);
	return true;
}


/* recursive templated function to convert whole line using one function call only
 * note: recursion will be probably eliminated and the whole function expanded to
 * the actual sequence of conversions needed */
//~ bool read_table2::read() { return true; }
template<class first, class ...rest>
bool read_table2::read(first&& val, rest&&... vals) {
	if(!read_next(val)) return false;
	return read(vals...);
}


#endif /* _READ_TABLE_H */

