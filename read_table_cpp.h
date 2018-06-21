/*  -*- C++ -*-
 * read_table_cpp.h -- simple and robust general methods for reading numeric data
 * 	from text files, e.g. TSV or CSV
 * 
 * simple: should be usable in a few lines of code
 * robust: try to detect and signal errors (in format, overflow, underflow etc.)
 * 	especially considering cases that would be silently ignored with
 * 	scanf / iostreams or similar; avoid undefined behavior
 * 
 * C++-only version, which does not require the POSIX getline() function,
 * uses std::getline() from the C++ standard library which should be available
 * on all platforms
 * 
 * note that this requires C++11
 * 
 * Copyright 2018 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of the  nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#if __cplusplus >= 201703L
#include <string_view>
#endif

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
/* helper class to return read-only string parts
 * (if std::string_view is not available) */
struct string_view_custom {
	const char* str;
	size_t len;
	const char* data() const { return str; }
	size_t length() const { return len; }
	size_t size() const { return len; }
	char operator [] (size_t i) const { return str[i]; }
	int print(FILE* f) const { return len>INT32_MAX?-1:fprintf(f,"%.*s",(int)len,str); }
};
template<class ostream>
ostream& operator << (ostream& s, const string_view_custom& str) {
	s.write(str.str,str.len);
	return s;
}

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

struct line_parser_params {
	int base; /* base for integer conversions */
	char delim; /* delimiter to use; 0 means any blank (space or tab) note: cannot be newline */
	char comment; /* character to indicate comments; 0 means none */
	bool allow_nan_inf; /* further flags: whether reading a NaN or INF for double values is considered and error */
	line_parser_params():base(10),delim(0),comment(0),allow_nan_inf(true) { }
	line_parser_params& set_base(int base_) { base = base_; return *this; }
	line_parser_params& set_delim(char delim_) { delim = delim_; return *this; }
	line_parser_params& set_comment(char comment_) { comment = comment_; return *this; }
	line_parser_params& set_allow_nan_inf(bool allow_nan_inf_) { allow_nan_inf = allow_nan_inf_; return *this; }
};

/* "helper" class doing most of the work for parsing only one line */
struct line_parser {
	protected:
		std::string buf; /* buffer to hold the current line */
		size_t pos; /* current position in line */
		size_t col; /* current field (column) */
		int base; /* base for integer conversions */
		enum read_table_errors last_error; /* error code of the last operation */
		char delim; /* delimiter to use; 0 means any blank (space or tab) note: cannot be newline */
		char comment; /* character to indicate comments; 0 means none */
		bool allow_nan_inf; /* further flags: whether reading a NaN or INF for double values is considered and error */
		
		void line_parser_init(line_parser_params par) {
			pos = 0;
			col = 0;
			base = par.base;
			delim = par.delim;
			comment = par.comment;
			allow_nan_inf = par.allow_nan_inf;
			last_error = T_OK;
		}
		
	public:
		/* 1. constructors, either with an empty string, or anything that can be copied into a string */
		explicit line_parser(line_parser_params par = line_parser_params()) {
			line_parser_init(par);
		}
		template<class... Args>
		explicit line_parser(line_parser_params par, Args&&... args):buf(std::forward<Args>(args)...) {
			line_parser_init(par);
		}
		template<class... Args>
		explicit line_parser(Args&&... args):buf(std::forward<Args>(args)...) {
			line_parser_init(line_parser_params());
		}
		
		/* 2. set (copy) the internal string */
		template<class... Args>
		void set_line(Args&&... args) {
			buf = std::string(std::forward<Args>(args)...);
			col = 0;
			pos = 0;
			last_error = T_OK;
		}
		/* get current line string */
		const char* get_line_c_str() const { return buf.c_str(); }
		const std::string& get_line_str() const { return buf; }
		
		/* 3. main interface for parsing data; this uses templates and has 
		 * specializations for all data types supported:
		 * 16, 32 and 64-bit signed and unsigned integers, doubles,
		 * strings, pairs of doubles and special "types":
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
		
		size_t get_pos() const { return pos; }
		size_t get_col() const { return col; }
		
		
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
		/* read string, copying from the buffer */
		bool read_string(std::string& str);
		/* read string, return readonly view */
#if __cplusplus >= 201703L
		bool read_string_view(std::string_view& str);
#endif
		bool read_string_view_custom(string_view_custom& str);
	
	protected:
		/* helper functions for the previous */
		bool read_table_pre_check();
		bool read_table_post_check(const char* c2);
		/* read string return start position and length
		 *  -- the other read_string functions then use these to create the string_view or copy to a string */
		bool read_string2(std::pair<size_t,size_t>& pos1);
};


/* main class containing main parameters for processing text */
struct read_table2 : public line_parser {
	protected:
		std::istream* is; /* input stream -- note: only a pointer is stored, the caller either supplies an input stream or a
			file name; in the former case, the original object should not go out of scope while this struct is used */
		std::ifstream* fs; /* file stream if it is opened by us */
		const char* fn; /* file name, stored optionally for error output */
		uint64_t line; /* current line (count starts from 1) */
		read_table2() = delete; /* user should supply either an input stream or a filename to use */
		read_table2(const read_table2& r) = delete; /* disallow copying, only moving is possible */
		/* helper function for the constructors to set default values */
		void read_table_init(line_parser_params par);
	public:
		
		/* 1. constructors -- need to give a file name or an already open input stream */
		
		/* constructor taking a file name; the given file is opened for reading
		 * and closed later in the destructor */
		explicit read_table2(const char* fn_, line_parser_params par = line_parser_params());
		/* constructor taking a reference to a stream -- it is NOT copied, i.e.
		 * the original instance of the stream need to be kept by the caller;
		 * also, the stream is not closed in the destructor in this case */
		explicit read_table2(std::istream& is_, line_parser_params par = line_parser_params());
		/* constructor either opening a file or taking the stream as fallback
		 * when fn_ is NULL */
		read_table2(const char* fn_, std::istream& is_, line_parser_params par = line_parser_params());
		read_table2(read_table2&& r);
		~read_table2();
		
		
		/* 2. read one line into the internal buffer
		 * 	the 'skip' parameter controls whether empty lines are skipped */
		bool read_line(bool skip = true);
		
		
		/* get current position in the file */
		uint64_t get_line() const { return line; }
		/* set filename (for better formatting of diagnostic messages) */
		void set_fn_for_diag(const char* fn_) { fn = fn; }
		
		/* write formatted error message to the given stream */
		void write_error(std::ostream& f) const;
};




/* constructor -- allocate new read_table2 struct, fill in the necessary fields */
void read_table2::read_table_init(line_parser_params par) {
	line_parser_init(par);
	line = 0;
	fn = 0;
}

read_table2::read_table2(const char* fn_, line_parser_params par) {
	fs = new std::ifstream(fn_);
	if( !fs || !(fs->is_open()) || fs->fail() ) last_error = T_ERROR_FOPEN;
	else fs->exceptions(std::ios_base::goodbit); /* clear exception mask -- no exceptions thrown, error checking done separately */
	is = fs;
	read_table_init(par);
	fn = fn_;
}

read_table2::read_table2(const char* fn_, std::istream& is_, line_parser_params par) {
	if(fn_) {
		fs = new std::ifstream(fn_);
			if( !fs || !(fs->is_open()) || fs->fail() ) last_error = T_ERROR_FOPEN;
		else fs->exceptions(std::ios_base::goodbit); /* clear exception mask -- no exceptions thrown, error checking done separately */
		is = fs;
	}
	else {
		fs = 0;
		is = &is_;
		is->exceptions(std::ios_base::goodbit); /* clear exception mask -- no exceptions thrown, error checking done separately */
	}
	read_table_init(par);
	fn = fn_;
}

read_table2::read_table2(std::istream& is_, line_parser_params par) {
	is = &is_;
	fs = 0;
	is->exceptions(std::ios_base::goodbit); /* clear exception mask -- no exceptions thrown, error checking done separately */
	read_table_init(par);
}

/* destructor -- closes the input stream only if it was opened in the 
 * constructor (i.e. the constructor was called with a filename */
read_table2::~read_table2() {
	if(fs) delete fs;
}

/* move constructor -- moves the stream to the new instance
 * the old instance is invalidated */
read_table2::read_table2(read_table2&& r):line_parser(std::move(r.buf)) {
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
			if(delim) pos = 0; /* if there is a delimiter character then whitespace at the beginning of a line is not skipped */
			if(pos < len) break; /* there is some data in the line */
		}
		else break; /* if empty lines should not be skipped */
	}
	col = 0; /* reset the counter for columns */
	last_error = T_OK;
	return true;
}

/* checks to be performed before trying to convert a field */
bool line_parser::read_table_pre_check() {
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
bool line_parser::read_table_post_check(const char* c2) {
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
bool line_parser::read_skip() {
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


/* return the string value in the next field -- internal helper */
bool line_parser::read_string2(std::pair<size_t,size_t>& pos1) {
	size_t len = buf.size();
	if(delim) {
		if(last_error == T_EOF || last_error == T_EOL ||
			last_error == T_READ_ERROR || last_error == T_ERROR_FOPEN) return false;
		/* note: having an empty string is OK in this case */
		size_t p1 = pos; /* start of the string */
		for(;pos<len;pos++) if(buf[pos] == delim || buf[pos] == '\n' ||
			(comment && buf[pos] == comment)) break;
		pos1.first = p1;
		pos1.second = pos - p1;
		if(pos<len && buf[pos] == delim) pos++; /* note: we do not care what is after the delimiter */
		else last_error = T_EOL; /* save that we were already at the end of a line; trying to read another field will result in an error */
	}
	else {
		if(!read_table_pre_check()) return false;
		size_t p1 = pos; /* start of the string */
		for(;pos<len;pos++) if(buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' ||
			(comment && buf[pos] == comment)) break;
		/* we do not care what is after the field, now we are either at a
		 * 	blank or line end */
		pos1.first = p1;
		pos1.second = pos - p1;
	}
	return true;
}

#if __cplusplus >= 201703L
/* return the string value in the next field as a string_view
 * NOTE: it will be invalidated when a new line is read */
bool line_parser::read_string_view(std::string_view& str) {
	std::pair<size_t,size_t> pos1;
	if(!read_string2(pos1)) return false;
	str = std::string_view(buf.data() + pos1.first, pos1.second);
	return true;
}
#endif
/* same but using a custom class instead of relying on the C++17
 * std::string_view */
bool line_parser::read_string_view_custom(string_view_custom& str) {
	std::pair<size_t,size_t> pos1;
	if(!read_string2(pos1)) return false;
	str.str = buf.data() + pos1.first;
	str.len = pos1.second;
	return true;
}

/* return the string value in the next field as a copy */
bool line_parser::read_string(std::string& str) {
	std::pair<size_t,size_t> pos1;
	if(!read_string2(pos1)) return false;
	str.assign(buf,pos1.first,pos1.second);
	return true;
}


/* try to convert the next value to integer
 * check explicitely that it is within the limits provided
 * (note: the limits are inclusive, so either min or max is OK)
 * return true on success, false on error */
bool line_parser::read_int32_limits(int32_t& i, int32_t min, int32_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	long res = strtol(buf.c_str() + pos, &c2, base);
	/* check that result fits in the given range -- note: long might be 64-bit */
	if(res > (long)max || res < (long)min) {
		last_error = T_OVERFLOW;
		if(res > (long)max) i = max;
		if(res < (long)min) i = min;
		return false;
	}
	i = res; /* store potential result -- at this point, it is ensured that this does not involve overflow */
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 64-bit integer
 * return true on success, false on error */
bool line_parser::read_int64_limits(int64_t& i, int64_t min, int64_t max) {
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
			if(res > (long)max) i = max;
			if(res < (long)min) i = min;
			return false;
		}
		i = (int64_t)res; /* store potential result */
	}
	else {
		res2 = strtoll(buf.c_str() + pos, &c2, base);
		if(res2 > (long long)max || res2 < (long long)min) {
			last_error = T_OVERFLOW;
			if(res2 > (long long)max) i = max;
			if(res2 < (long long)min) i = min;
			return false;
		}
		i = (int64_t)res2; /* store potential result */
	}
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 32-bit unsigned integer
 * return true on success, false on error */
bool line_parser::read_uint32_limits(uint32_t& i, uint32_t min, uint32_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	/* stricly require that the next character is alphanumeric
	 * -- strtoul() will silently accept and negate negative values */
	if( ! (isalnum(buf[pos]) || buf[pos] == '+') ) {
		if(buf[pos] == '-') last_error = T_OVERFLOW;
		else last_error = T_FORMAT;
		i = 0;
		return false;
	}
	unsigned long res = strtoul(buf.c_str() + pos, &c2, base);
	/* check that result fits in bounds -- long might be 64-bit */
	if(res > (unsigned long)max || res < (unsigned int)min) {
		last_error = T_OVERFLOW;
		if(res > (unsigned long)max) i = max;
		if(res < (unsigned long)min) i = min;
		return false;
	}
	i = res; /* store potential result */
	/* advance position after the number, check if there is proper field separator */
	return read_table_post_check(c2);
}

/* try to convert the next value to 64-bit unsigned integer
 * return true on success, false on error */
bool line_parser::read_uint64_limits(uint64_t& i, uint64_t min, uint64_t max) {
	if(!read_table_pre_check()) return false;
	errno = 0;
	char* c2;
	/* stricly require that the next character is alphanumeric
	 * -- strtoul() will silently accept and negate negative values */
	if( ! (isalnum(buf[pos]) || buf[pos] == '+') ) {
		if(buf[pos] == '-') last_error = T_OVERFLOW;
		else last_error = T_FORMAT;
		i = 0;
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
			if(res > (unsigned long)max) i = max;
			if(res < (unsigned long)min) i = min;
			return false;
		}
		i = res; /* store potential result */
	}
	else {
		res2 = strtoull(buf.c_str() + pos, &c2, base);
		if(res2 > (unsigned long long)max || res < (unsigned long long)min) {
			last_error = T_OVERFLOW;
			if(res > (unsigned long long)max) i = max;
			if(res < (unsigned long long)min) i = min;
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
bool line_parser::read_int16_limits(int16_t& i, int16_t min, int16_t max) {
	/* just use the previous function and check for overflow */
	int32_t i2;
	/* note: the following function already check for overflow as well */
	bool ret = read_int32_limits(i2,(int32_t)min,(int32_t)max);
	i = i2;
	return ret;
}

/* try to convert the next value to a 16-bit unsigned integer
 * return true on success, false on error */
bool line_parser::read_uint16_limits(uint16_t& i, uint16_t min, uint16_t max) {
	/* just use the previous function and check for overflow */
	uint32_t i2;
	bool ret = !read_uint32_limits(i2,(uint32_t)min,(uint32_t)max);
	i = i2;
	return ret;
}

/* try to convert the next value to a double precision float value
 * return true on success, false on error */
bool line_parser::read_double(double& d) {
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
bool line_parser::read_double_limits(double& d, double min, double max) {
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
template<> bool line_parser::read_next(int32_t& val) { return read_int32(val); }
template<> bool line_parser::read_next(uint32_t& val) { return read_uint32(val); }
template<> bool line_parser::read_next(int16_t& val) { return read_int16(val); }
template<> bool line_parser::read_next(uint16_t& val) { return read_uint16(val); }
template<> bool line_parser::read_next(int64_t& val) { return read_int64(val); }
template<> bool line_parser::read_next(uint64_t& val) { return read_uint64(val); }
template<> bool line_parser::read_next(double& val) { return read_double(val); }
template<> bool line_parser::read_next(std::pair<double,double>& p) {
	double x,y;
	if( !( read_double(x) && read_double(y) ) ) return false;
	p = std::pair<double,double>(x,y);
	return true;
}
template<> bool line_parser::read_next(std::string& str) { return read_string(str); }
#if __cplusplus >= 201703L
template<> bool line_parser::read_next(std::string_view& str) { return read_string_view(str); }
#endif
template<> bool line_parser::read_next(string_view_custom& str) { return read_string_view_custom(str); }

/* dummy struct to be able to call the same interface to skip data
 * (useful if used with the variadic template below) */
template<> bool line_parser::read_next(const read_table_skip_t& skip) { return read_skip(); }
//~ template<> bool line_parser::read_next(read_table_skip_t skip) { return read_skip(); }


/* overloads for reading with bounds
 * example usage:
line_parser r(...);
uint32_t x;
r.read_next(read_bounds(x,1000U,2000U));
*/
template<> bool line_parser::read_next(read_bounds_t<int32_t> b) {
	return read_int32_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<uint32_t> b) {
	return read_uint32_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<int64_t> b) {
	return read_int64_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<uint64_t> b) {
	return read_uint64_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<int16_t> b) {
	return read_int16_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<uint16_t> b) {
	return read_uint16_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<double> b) {
	return read_double_limits(b.val,b.min,b.max);
}
template<> bool line_parser::read_next(read_bounds_t<std::pair<double,double> > b) {
	double x,y;
	if( !(read_double_limits(x,b.min.first,b.max.first) &&
		read_double_limits(y,b.min.second,b.max.second) ) ) return false;
	b.val = std::make_pair(x,y);
	return true;
}


/* recursive templated function to convert whole line using one function call only
 * note: recursion will be probably eliminated and the whole function expanded to
 * the actual sequence of conversions needed */
//~ bool line_parser::read() { return true; }
template<class first, class ...rest>
bool line_parser::read(first&& val, rest&&... vals) {
	if(!read_next(val)) return false;
	return read(vals...);
}


#endif /* _READ_TABLE_H */

