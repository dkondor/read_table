# read_table
Simple header-only utility library to read numeric data from text (TSV, CSV, etc.)
files in C/C++ with error checking. File format should be known at compile time.

### Main motivation
The main motivation for this library is that if using the C stdio / C++ iostream functions to read
numeric data from text files, certain format errors will be silently ignored or lead to undefined behavior.
Depending on the exact usage, this can include missing values, overflows and underflows, or reading negative
values when expecting a signed integer, which will not be reported by the corresponding library functions.
Specifically, a '-' sign is even accepted by [strtoul()](http://en.cppreference.com/w/c/string/byte/strtoul)
without reporting an error. This requires cumbersome error checking or can lead to cases when a trivial input
format error results in errors or incorrect results much later which are hard to detect and trace back to
their origin.

The goal of this library is to provide a simple and robust way to read numeric data from text files in C and
C++ when the format is known at compile time. This should mean:
- simple: reading data from a text file should not be longer than a few lines including error checking
- robust: all format errors should be detected and reported (e.g. missing values, incorrect format, values out of range, etc.)


### Usage:
There are two versions, both are header-only, so including the header in any file that needs it is sufficient.
The difference is the following:

- read_table.h -- Combined C/C++ implementation, provides both a C and C++ interface (the latter only if
__cplusplus is defined so it can be used with a C-only compiler as well); it uses C FILE* objects to do IO,
which means that it can also use FILE* objects opened with e.g. popen() or fdopen(). Requires the presence
of the POSIX getline() function in stdio.h, which is typically not available on Windows.

- read_table_cpp.h -- Only C++ interface, uses only the C++ iostreams standard library do to IO, so it should
be portable to any platform

Note that the C++ interface in both files is the same and that it requires a C++11 compiler.

Basic example usage is provided in the header files and in the test programs.


