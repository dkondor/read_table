# read_table
Simple header-only utility library to read numeric data from text (TSV, CSV, etc.)
files in C/C++ with error checking. File format should be known at compile time.

### Main motivation
The main motivation for this library is to simplify the common task of reading numeric data from text files.
IMO, the C stdio / C++ iostream functions have a shortcoming that certain format errors are hard to detect,
and "simple" usage (e.g. fscanf(file,"%d %u %g\n",&i,&u,&d)) can result in errors silently ignored or undefined
behavior. Depending on the exact usage, this can include missing values, overflows and underflows, or reading
negative values when expecting a signed integer. Specifically, a '-' sign is even accepted by
[strtoul()](http://en.cppreference.com/w/c/string/byte/strtoul) without reporting an error. Furthermore, the
[scanf()](http://en.cppreference.com/w/c/io/fscanf) function will always skip ANY whitespace before any numeric
conversion, including newline characters. This requires cumbersome error checking or parsing (e.g. to ensure
that each line contains the right number of values or that no negative number is accidentally present in a field
expected to be unsigned). Even with efforts to check for errors, it can lead to cases when a trivial input format
error results in errors or incorrect results much later which are hard to detect and trace back to their origin.

The goal of this library is to provide a simple and robust way to read numeric data from text files in C and
C++ when the format is known at compile time. This should mean:
- simple: reading data from a text file should not be longer than a few lines including error checking
- robust: all format errors should be detected and reported (e.g. missing values, incorrect format, values out of range, etc.)

In line with this, the functionality is limited to this main use case. It supports:
- reading numbers separated by spaces, tabs, or a given separator character
- parsing / converting 16, 32 and 64-bit signed and unsigned integers and doubles
- skipping over any value (including strings, but NOT supporting quotation, escapes, etc.)
- strictly checking that each line contains the right amount of fields
- reporting error if there is an overflow or format error or the value is outside a desired range


### Usage
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


