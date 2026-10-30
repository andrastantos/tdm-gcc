/* Target macros for libgcc on TDM.
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* Code uses the word below the stack pointer, [$sp-1], as scratch, so
   __strub_leave can't clear the stack below its own: it would clear the
   scratch word holding its bound.  Clear an array allocated over the
   range instead.  The array starts above the outgoing arguments of the
   memset call that clears it (the free word [$sp] and the stack
   arguments), so make it longer by that much, and some.  */
#define TARGET_STRUB_USE_DYNAMIC_ARRAY 1
#define TARGET_STRUB_DYNAMIC_ARRAY_EXTRA 8
