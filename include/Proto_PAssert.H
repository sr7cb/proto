#pragma once

#include <cassert>
#include <cstdio>
#include <cstdlib>

// Custom Assert macro with printf-style reporting
#ifdef NDEBUG

#define PROTO_ASSERT(stmt,args...)

#else

#define PROTO_ASSERT(stmt,args...) \
  assert( (stmt) ||                                         \
     (fprintf(stderr,"%s:%d: error: ",__FILE__,__LINE__) && \
     fprintf(stderr,"Assertion `" #stmt "` failed.\n\t") && \
     fprintf(stderr,args) && \
     fprintf(stderr,"\n"))); \
  if (!(stmt)){std::abort();}

#endif
