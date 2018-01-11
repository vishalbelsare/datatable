//------------------------------------------------------------------------------
//  Copyright 2017 H2O.ai
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//------------------------------------------------------------------------------
#ifndef dt_FREAD_H
#define dt_FREAD_H
#include <stdint.h>  // uint32_t
#include <stdlib.h>  // size_t
#include "utils.h"
#include "utils/omp.h"
#include "memorybuf.h"


// Ordered hierarchy of types
typedef enum {
  NEG            = -1,  // dummy to force signed type; sign bit used for out-of-sample type bump management
  CT_DROP        = 0,   // skip column requested by user; it is navigated as a string column with the prevailing quoteRule
  CT_BOOL8_N     = 1,   // int8_t; first enum value must be 1 not 0(=CT_DROP) so that it can be negated to -1.
  CT_BOOL8_U     = 2,
  CT_BOOL8_T     = 3,
  CT_BOOL8_L     = 4,
  CT_INT32       = 5,   // int32_t
  CT_INT64       = 6,   // int64_t
  CT_FLOAT32_HEX = 7,    // float, in hexadecimal format
  CT_FLOAT64     = 8,   // double (64-bit IEEE 754 float)
  CT_FLOAT64_EXT = 9,   // double, with various "NaN" literals
  CT_FLOAT64_HEX = 10,  // double, in hexadecimal format
  CT_STRING      = 11,  // lenOff struct below
  NUMTYPE        = 12   // placeholder for the number of types including drop
} colType;

extern int8_t typeSize[NUMTYPE];
extern const char typeName[NUMTYPE][10];
extern const long double pow10lookup[701];
extern const uint8_t hexdigits[256];


// Strings are pushed by fread_main using an offset from an anchor address plus
// string length; fread_impl.c then manages strings appropriately
struct lenOff {
  int32_t len;  // signed to distinguish NA vs empty ""
  int32_t off;
};


union field64 {
  int8_t   int8;
  int32_t  int32;
  int64_t  int64;
  uint32_t uint32;
  uint64_t uint64;
  float    float32;
  double   float64;
  lenOff   str32;
};


struct FieldParseContext {
  // Pointer to the current parsing location
  const char*& ch;

  // Where to write the parsed value. The pointer will be incremented after
  // each successful read.
  field64* target;

  // Anchor pointer for string parser, this pointer is the starting point
  // relative to which `str32.offset` is defined.
  const char* anchor;

  const char* eof;

  const char* const* NAstrings;

  // what to consider as whitespace to skip: ' ', '\t' or 0 means both
  // (when sep!=' ' && sep!='\t')
  char whiteChar;

  // Decimal separator for parsing floats. The default value is '.', but
  // in some cases ',' may also be used.
  char dec;

  // Field separator
  char sep;

  // Character used for field quoting.
  char quote;

  // How the fields are quoted.
  // TODO: split quoteRule differences into separate parsers.
  int8_t quoteRule;

  // Should white space be removed?
  bool stripWhite;

  // Do we consider blank as NA string?
  bool blank_is_a_NAstring;

  bool LFpresent;

  void skip_white();
  // bool eol(const char**);
  bool end_of_field();
  const char* end_NA_string(const char*);
  int countfields();
  bool nextGoodLine(int ncol, bool fill, bool skipBlankLines);
  bool skip_eol();
};

typedef void (*ParserFnPtr)(FieldParseContext& ctx);


#define NA_BOOL8         INT8_MIN
#define NA_INT32         INT32_MIN
#define NA_INT64         INT64_MIN
#define NA_FLOAT64_I64   0x7FF00000000007A2
#define NA_FLOAT32_I32   0x7F8007A2
#define NA_LENOFF        INT32_MIN  // lenOff.len only; lenOff.off undefined for NA
#define INF_FLOAT32_I32  0x7F800000
#define INF_FLOAT64_I64   0x7FF0000000000000



// Per-column per-thread temporary string buffers used to assemble processed
// string data. Length = `nstrcols`. Each element in this array has the
// following fields:
//     .buf -- memory region where all string data is stored.
//     .size -- allocation size of this memory buffer.
//     .ptr -- the `postprocessBuffer` stores here the total amount of string
//         data currently held in the buffer; while the `orderBuffer` function
//         puts here the offset within the global string buffer where the
//         current buffer should be copied to.
//     .idx8 -- index of the current column within the `buff8` array.
//     .idxdt -- index of the current column within the output DataTable.
//     .numuses -- synchronization lock. The purpose of this variable is to
//         prevent race conditions between threads that do memcpy, and another
//         thread that needs to realloc the underlying buffer. Without the lock,
//         if one thread is performing a mem-copy and the other thread wants to
//         reallocs the buffer, then the first thread will segfault in the
//         middle of its operation. In order to prevent this, we use this
//         `.numuses` variable: when positive it shows the number of threads
//         that are currently writing to the same buffer. However when this
//         variable is negative, it means the buffer is being realloced, and no
//         other threads is allowed to initiate a memcopy.
//
struct StrBuf {
  MemoryBuffer* mbuf;
  size_t ptr;
  int idx8;
  int idxdt;
  volatile int numuses;
  int : 32;
};




// *****************************************************************************

struct ThreadLocalFreadParsingContext
{
  // Pointer that serves as a starting point for all offsets within the `lenOff`
  // structs.
  const char *__restrict__ anchor;

  // Output buffer. Within the buffer the data is stored in row-major order,
  // i.e. in the same order as in the original CSV file.
  field64* __restrict__ buff;

  // Size (in bytes) for a single row of data within the buffer. This should be
  // equal to `ncol * 8`.
  size_t rowSize;

  // Starting row index within the output DataTable for the current data chunk.
  size_t DTi;

  // Number of rows currently being stored within the buffers. The allocation
  // size of each `buffX` is thus at least `nRows * rowSizeX`.
  size_t nRows;

  // Reference to the flag that controls the parser's execution. Setting this
  // flag to true will force parsing of the CSV file to terminate in the near
  // future.
  bool* stopTeam;

  int threadn;

  int quoteRule;

  char quote;

  int64_t : 56;

  // Any additional implementation-specific parameters.
  StrBuf* strbufs;
};


#endif
