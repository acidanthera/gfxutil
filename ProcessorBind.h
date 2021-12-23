/*
 *  ProcessorBind.h
 *  gfxutil
 *
 *  Created by joevt on 21-12-21.
 *
 */

#ifndef __GFXUTIL_PROCESSORBIND_H__
#define __GFXUTIL_PROCESSORBIND_H__

#ifdef MDE_CPU_EBC
#elif defined(_MSC_EXTENSIONS)
#else
	#ifndef _Static_assert
		#define CONCAT_IMPL(x,y) x##y
		#define MACRO_CONCAT(x,y) CONCAT_IMPL(x,y)
		#define _Static_assert(COND,MSG) \
			typedef int MACRO_CONCAT( static_assertion, __LINE__ ) [(COND)?1:-1]
	#endif
#endif

#if __LP64__
	#include <../edk2/MdePkg/Include/X64/ProcessorBind.h>
#else
	#include <../edk2/MdePkg/Include/Ia32/ProcessorBind.h>
#endif

#endif
