#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifndef DEBUGLEVEL
#define DEBUGLEVEL 1
#endif

#if DEBUGLEVEL > 1
#define VERBOSE 1
#else
#define VERBOSE 0
#endif /* DEBUGLEVEL */

#if VERBOSE
#define DBG printf
#else
#define DBG {}if(0)printf
#endif /* VERBOSE */



#endif /* DEBUG_H */
